#include "gui/LabRecorderClient.h"
#include "gui/LabRecorderFilenamePolicy.h"
#include "gui/PerformanceBudgets.h"

#include <algorithm>
#include <utility>

LabRecorderClient::LabRecorderClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<RecorderConnectionState>("RecorderConnectionState");
    qRegisterMetaType<RecorderRecordingState>("RecorderRecordingState");
    qRegisterMetaType<RecorderOperationState>("RecorderOperationState");
    connection_timeout_.setSingleShot(true);
    command_timeout_.setSingleShot(true);
    connect(&socket_, &QTcpSocket::connected, this, &LabRecorderClient::onConnected);
    connect(&socket_, &QTcpSocket::disconnected, this, &LabRecorderClient::onDisconnected);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &LabRecorderClient::onSocketError);
    connect(&socket_, &QTcpSocket::bytesWritten, this, &LabRecorderClient::onBytesWritten);
    connect(&socket_, &QTcpSocket::readyRead, this, &LabRecorderClient::onReadyRead);
    connect(&connection_timeout_, &QTimer::timeout,
            this, &LabRecorderClient::onConnectionTimeout);
    connect(&command_timeout_, &QTimer::timeout, this, &LabRecorderClient::onCommandTimeout);
}

LabRecorderClient::~LabRecorderClient() {
    connection_timeout_.stop();
    command_timeout_.stop();
    QObject::disconnect(&socket_, nullptr, this, nullptr);
    QObject::disconnect(&connection_timeout_, nullptr, this, nullptr);
    QObject::disconnect(&command_timeout_, nullptr, this, nullptr);
    socket_.abort();
}

void LabRecorderClient::connectToServer(const QString& host,
                                        quint16 port,
                                        int connection_timeout_ms,
                                        int command_timeout_ms) {
    if (shutdown_requested_) {
        emit commandFinished("connect", false, "Recorder shutdown is already in progress");
        return;
    }
    const bool active_recording_work =
        start_may_have_reached_server_ ||
        recording_state_ == RecorderRecordingState::Recording ||
        desired_recording_state_ == RecorderRecordingState::Recording ||
        hasWork(CommandKind::Start) || hasWork(CommandKind::Stop);
    if ((connection_state_ == RecorderConnectionState::Connected ||
         connection_state_ == RecorderConnectionState::Connecting) &&
        active_recording_work) {
        emit commandFinished(
            "connect", false,
            "Recorder connection cannot be replaced while recording work is active");
        return;
    }
    const bool preserve_uncertain_start = start_may_have_reached_server_;
    connection_timeout_.stop();
    command_timeout_.stop();
    clearQueued("LabRecorder connection replaced");
    if (have_active_batch_) {
        finishActiveBatch(false, "LabRecorder connection replaced", false);
    }
    pending_payload_.clear();
    response_buffer_.clear();
    socket_.abort();
    connection_timeout_.setInterval((std::max)(1, connection_timeout_ms));
    command_timeout_.setInterval((std::max)(1, command_timeout_ms));
    start_may_have_reached_server_ = preserve_uncertain_start;
    setRecordingState(RecorderRecordingState::Unknown);
    setDesiredRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Connecting,
                       "Connecting to LabRecorder RCS...");
    socket_.connectToHost(host, port);
    if (connection_state_ == RecorderConnectionState::Connecting) {
        connection_timeout_.start();
    }
    updateOperationState();
}

void LabRecorderClient::disconnectFromServer() {
    connection_timeout_.stop();
    command_timeout_.stop();
    clearQueued("LabRecorder connection detached");
    if (have_active_batch_) {
        finishActiveBatch(false, "LabRecorder connection detached", false);
    }
    pending_payload_.clear();
    response_buffer_.clear();
    socket_.abort();
    start_may_have_reached_server_ = false;
    setRecordingState(RecorderRecordingState::Unknown);
    setDesiredRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Disconnected,
                       "LabRecorder connection detached");
    updateOperationState();
}

bool LabRecorderClient::isConnected() const {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

bool LabRecorderClient::shutdownReady() const {
    return shutdown_requested_ && !have_active_batch_ && batches_.isEmpty();
}

bool LabRecorderClient::shutdownSettledSafely() const {
    return shutdownReady() && !start_may_have_reached_server_;
}

QString LabRecorderClient::activeOperation() const {
    return have_active_batch_ ? active_batch_.operation : QString();
}

QString LabRecorderClient::activeCommand() const {
    if (!have_active_batch_ || active_batch_.next_command >= active_batch_.commands.size()) {
        return {};
    }
    return active_batch_.commands.at(active_batch_.next_command);
}

int LabRecorderClient::activeCommandNumber() const {
    return have_active_batch_ ? static_cast<int>(active_batch_.next_command + 1) : 0;
}

int LabRecorderClient::activeCommandCount() const {
    return have_active_batch_ ? static_cast<int>(active_batch_.commands.size()) : 0;
}

bool LabRecorderClient::sendCommand(const QString& command) {
    if (shutdown_requested_) {
        emit commandFinished(command, false,
                             "Recorder shutdown is already in progress");
        return false;
    }
    return enqueueCommands(CommandKind::Generic, command, {command},
                           RecorderRecordingState::Unknown);
}

bool LabRecorderClient::refreshStreams() {
    if (shutdown_requested_ || hasWork(CommandKind::Start) || hasWork(CommandKind::Stop)) {
        emit commandFinished("refresh streams", false,
                             "Stream refresh is unavailable during recording state changes");
        return false;
    }
    if (hasWork(CommandKind::Refresh)) {
        return true;
    }
    return enqueueCommands(CommandKind::Refresh, "refresh streams", {"update"},
                           RecorderRecordingState::Unknown);
}

bool LabRecorderClient::updateFilename(const LabRecorderFilenameFields& fields) {
    if (shutdown_requested_ || desired_recording_state_ == RecorderRecordingState::Recording ||
        hasWork(CommandKind::Start) || hasWork(CommandKind::Stop)) {
        emit commandFinished("update filename", false,
                             "Filename changes are unavailable while recording is starting, active, or stopping");
        return false;
    }
    const QString command = filenameCommand(fields);
    if (replaceQueuedFilename(command)) {
        updateOperationState();
        return true;
    }
    return enqueueCommands(CommandKind::Filename, "update filename", {command},
                           RecorderRecordingState::Unknown);
}

bool LabRecorderClient::startRecording(const LabRecorderFilenameFields& fields,
                                       bool select_all_first) {
    if (shutdown_requested_) {
        emit commandFinished("start recording", false,
                             "Recorder shutdown is already in progress");
        return false;
    }
    if (start_may_have_reached_server_ ||
        desired_recording_state_ == RecorderRecordingState::Recording ||
        recording_state_ == RecorderRecordingState::Recording ||
        hasWork(CommandKind::Start) || hasWork(CommandKind::Stop)) {
        emit commandFinished("start recording", false,
                             "A recording Start or Stop is already active or queued");
        return false;
    }
    removeQueuedNonessential("Superseded by recording Start", false);
    setDesiredRecordingState(RecorderRecordingState::Recording);
    const bool queued = enqueueCommands(CommandKind::Start,
                                        "start recording",
                                        startRecordingCommands(fields, select_all_first),
                                        RecorderRecordingState::Recording);
    if (!queued) {
        setDesiredRecordingState(RecorderRecordingState::Unknown);
    }
    return queued;
}

bool LabRecorderClient::stopRecording() {
    return enqueueStop(false);
}

bool LabRecorderClient::beginShutdown() {
    if (shutdown_requested_) {
        return !shutdownReady();
    }
    shutdown_requested_ = true;
    const bool start_pending = hasWork(CommandKind::Start);
    const bool should_stop = start_pending || start_may_have_reached_server_ ||
                             desired_recording_state_ == RecorderRecordingState::Recording ||
                             recording_state_ == RecorderRecordingState::Recording;
    if (!should_stop) {
        removeQueuedNonessential("Canceled during recorder shutdown", true);
        updateOperationState();
        return false;
    }
    return enqueueStop(true);
}

bool LabRecorderClient::enqueueStop(bool shutdown) {
    if (hasWork(CommandKind::Stop)) {
        emit commandFinished("stop recording", false,
                             "A recording Stop is already active or queued");
        return false;
    }
    if (!isConnected()) {
        clearQueued("Recorder connection was lost before Stop");
        if (have_active_batch_) {
            finishActiveBatch(false, "Recorder connection was lost before Stop", false);
        }
        setDesiredRecordingState(RecorderRecordingState::Unknown);
        emit commandFinished("stop recording", false, "LabRecorder RCS is not connected");
        updateOperationState();
        return false;
    }

    setDesiredRecordingState(RecorderRecordingState::Stopped);
    removeQueuedNonessential("Superseded by recording Stop", true);

    if (have_active_batch_ && active_batch_.kind == CommandKind::Start &&
        !active_batch_.start_command_sent) {
        active_batch_.cancel_after_current = true;
        active_cancel_reason_ = shutdown
            ? "Recording Start canceled before it reached LabRecorder during shutdown"
            : "Recording Start canceled before it reached LabRecorder";
        updateOperationState();
        return false;
    }

    return enqueueCommands(CommandKind::Stop,
                           shutdown ? "shutdown stop recording" : "stop recording",
                           {"stop"},
                           RecorderRecordingState::Stopped);
}

bool LabRecorderClient::enqueueCommands(CommandKind kind,
                                        QString operation,
                                        QStringList commands,
                                        RecorderRecordingState success_state) {
    if (!isConnected()) {
        emit commandFinished(operation, false, "LabRecorder RCS is not connected");
        return false;
    }
    if (commands.isEmpty()) {
        emit commandFinished(operation, false, "LabRecorder command batch is empty");
        return false;
    }
    if (queueDepth() >=
        vicon_lsl::gui::PerformanceBudgets::MaximumRecorderQueueDepth) {
        emit commandFinished(operation, false, "LabRecorder command queue is full");
        return false;
    }
    batches_.enqueue({kind, std::move(operation), std::move(commands), 0,
                      success_state, false, false});
    updateOperationState();
    startNextBatch();
    return true;
}

bool LabRecorderClient::hasWork(CommandKind kind) const {
    if (have_active_batch_ && active_batch_.kind == kind) {
        return true;
    }
    for (const CommandBatch& batch : batches_) {
        if (batch.kind == kind) {
            return true;
        }
    }
    return false;
}

bool LabRecorderClient::replaceQueuedFilename(const QString& command) {
    for (CommandBatch& batch : batches_) {
        if (batch.kind == CommandKind::Filename) {
            batch.commands = {command};
            batch.next_command = 0;
            return true;
        }
    }
    return false;
}

void LabRecorderClient::removeQueuedNonessential(const QString& reason,
                                                 bool emit_failures) {
    QQueue<CommandBatch> retained;
    while (!batches_.isEmpty()) {
        CommandBatch batch = batches_.dequeue();
        if (batch.kind == CommandKind::Stop) {
            retained.enqueue(std::move(batch));
        } else if (emit_failures) {
            emit commandFinished(batch.operation, false, reason);
        }
    }
    batches_ = std::move(retained);
    updateOperationState();
}

void LabRecorderClient::clearQueued(const QString& reason, bool emit_failures) {
    while (!batches_.isEmpty()) {
        const CommandBatch batch = batches_.dequeue();
        if (emit_failures) {
            emit commandFinished(batch.operation, false, reason);
        }
    }
    updateOperationState();
}

void LabRecorderClient::startNextBatch() {
    if (have_active_batch_ || batches_.isEmpty() || !isConnected()) {
        updateOperationState();
        return;
    }
    active_batch_ = batches_.dequeue();
    have_active_batch_ = true;
    active_cancel_reason_.clear();
    pending_payload_.clear();
    response_buffer_.clear();
    updateOperationState();
    writeNextCommand();
}

void LabRecorderClient::writeNextCommand() {
    if (!have_active_batch_) {
        return;
    }
    if (active_batch_.next_command >= active_batch_.commands.size()) {
        finishActiveBatch(true, "Command acknowledged");
        return;
    }

    if (pending_payload_.isEmpty()) {
        const QString command = active_batch_.commands[active_batch_.next_command];
        pending_payload_ = command.toUtf8();
        pending_payload_.append('\n');
        response_buffer_.clear();
        command_timeout_.start();
        emit commandProgress(active_batch_.operation,
                             static_cast<int>(active_batch_.next_command + 1),
                             static_cast<int>(active_batch_.commands.size()),
                             command);
    }

    const qint64 accepted = socket_.write(pending_payload_);
    if (accepted < 0) {
        failActiveConnection(socket_.errorString());
        return;
    }
    if (accepted > 0) {
        const QString command = active_batch_.commands[active_batch_.next_command];
        if (active_batch_.kind == CommandKind::Start && command == "start") {
            active_batch_.start_command_sent = true;
            start_may_have_reached_server_ = true;
        }
        pending_payload_.remove(0, accepted);
    }
}

void LabRecorderClient::finishActiveBatch(bool ok,
                                          const QString& message,
                                          bool start_next,
                                          bool preserve_recording_state) {
    if (!have_active_batch_) {
        return;
    }
    command_timeout_.stop();
    pending_payload_.clear();
    response_buffer_.clear();
    const QString operation = active_batch_.operation;
    const CommandKind kind = active_batch_.kind;
    const RecorderRecordingState success_state = active_batch_.success_state;
    have_active_batch_ = false;
    active_cancel_reason_.clear();
    if (ok) {
        if (success_state != RecorderRecordingState::Unknown) {
            setRecordingState(success_state);
        }
        if (kind == CommandKind::Stop) {
            start_may_have_reached_server_ = false;
            setDesiredRecordingState(RecorderRecordingState::Stopped);
        }
    } else if (!preserve_recording_state) {
        setRecordingState(RecorderRecordingState::Unknown);
        setDesiredRecordingState(RecorderRecordingState::Unknown);
    }
    emit commandFinished(operation, ok, message);
    updateOperationState();
    if (start_next) {
        startNextBatch();
    }
}

void LabRecorderClient::failActiveConnection(const QString& message) {
    connection_timeout_.stop();
    command_timeout_.stop();
    clearQueued(message);
    setRecordingState(RecorderRecordingState::Unknown);
    setDesiredRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Error, message);
    if (have_active_batch_) {
        finishActiveBatch(false, message, false);
    }
    pending_payload_.clear();
    response_buffer_.clear();
    socket_.abort();
    updateOperationState();
}

void LabRecorderClient::setConnectionState(RecorderConnectionState state,
                                           const QString& message) {
    if (connection_state_ == state && message.isEmpty()) {
        return;
    }
    connection_state_ = state;
    emit connectionStateChanged(state, message);
}

void LabRecorderClient::setRecordingState(RecorderRecordingState state) {
    if (recording_state_ == state) {
        return;
    }
    recording_state_ = state;
    emit recordingStateChanged(state);
}

void LabRecorderClient::setDesiredRecordingState(RecorderRecordingState state) {
    if (desired_recording_state_ == state) {
        return;
    }
    desired_recording_state_ = state;
    emit desiredRecordingStateChanged(state);
}

RecorderOperationState LabRecorderClient::operationForKind(CommandKind kind) {
    switch (kind) {
        case CommandKind::Generic:
        case CommandKind::Refresh: return RecorderOperationState::Refreshing;
        case CommandKind::Filename: return RecorderOperationState::UpdatingFilename;
        case CommandKind::Start: return RecorderOperationState::Starting;
        case CommandKind::Stop: return RecorderOperationState::Stopping;
    }
    return RecorderOperationState::Idle;
}

void LabRecorderClient::updateOperationState() {
    RecorderOperationState next = RecorderOperationState::Idle;
    if (have_active_batch_) {
        next = operationForKind(active_batch_.kind);
    } else if (!batches_.isEmpty()) {
        next = operationForKind(batches_.head().kind);
    } else if (shutdown_requested_) {
        next = RecorderOperationState::ShuttingDown;
    }
    const int depth = queueDepth();
    if (next == operation_state_ && depth == last_emitted_queue_depth_) {
        return;
    }
    operation_state_ = next;
    last_emitted_queue_depth_ = depth;
    emit operationStateChanged(operation_state_, depth);
}

void LabRecorderClient::onConnected() {
    connection_timeout_.stop();
    setConnectionState(
        RecorderConnectionState::Connected,
        "Connected to LabRecorder RCS; recording state is unknown until Start or Stop is acknowledged");
    setRecordingState(RecorderRecordingState::Unknown);
    setDesiredRecordingState(RecorderRecordingState::Unknown);
    startNextBatch();
}

void LabRecorderClient::onDisconnected() {
    connection_timeout_.stop();
    command_timeout_.stop();
    if (have_active_batch_) {
        finishActiveBatch(false, "LabRecorder disconnected during command", false);
    }
    clearQueued("LabRecorder disconnected before command execution");
    pending_payload_.clear();
    response_buffer_.clear();
    setRecordingState(RecorderRecordingState::Unknown);
    setDesiredRecordingState(RecorderRecordingState::Unknown);
    if (connection_state_ != RecorderConnectionState::Error) {
        setConnectionState(RecorderConnectionState::Disconnected, "LabRecorder disconnected");
    }
    updateOperationState();
}

void LabRecorderClient::onSocketError(QAbstractSocket::SocketError error) {
    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    failActiveConnection(socket_.errorString());
}

void LabRecorderClient::onBytesWritten(qint64) {
    if (!have_active_batch_ || pending_payload_.isEmpty()) {
        return;
    }
    writeNextCommand();
}

void LabRecorderClient::onReadyRead() {
    response_buffer_.append(socket_.readAll());
    if (!have_active_batch_ || !pending_payload_.isEmpty()) {
        return;
    }

    while (!response_buffer_.isEmpty() &&
           (response_buffer_.front() == '\r' || response_buffer_.front() == '\n' ||
            response_buffer_.front() == ' ' || response_buffer_.front() == '\t')) {
        response_buffer_.remove(0, 1);
    }
    if (response_buffer_.size() < 2) {
        return;
    }
    if (!response_buffer_.startsWith("OK")) {
        failActiveConnection("Unexpected LabRecorder reply: " +
                             QString::fromUtf8(response_buffer_.left(80)));
        return;
    }

    command_timeout_.stop();
    response_buffer_.remove(0, 2);
    pending_payload_.clear();
    ++active_batch_.next_command;
    if (active_batch_.cancel_after_current && !active_batch_.start_command_sent) {
        const QString reason = active_cancel_reason_.isEmpty()
            ? "Recording Start canceled before it reached LabRecorder"
            : active_cancel_reason_;
        finishActiveBatch(false, reason, true, true);
        return;
    }
    writeNextCommand();
}

void LabRecorderClient::onConnectionTimeout() {
    if (connection_state_ != RecorderConnectionState::Connecting) {
        return;
    }
    failActiveConnection("Timed out connecting to LabRecorder RCS");
}

void LabRecorderClient::onCommandTimeout() {
    if (!have_active_batch_) {
        return;
    }
    failActiveConnection("Timed out waiting for LabRecorder command acknowledgement");
}

QString LabRecorderClient::filenameCommand(const LabRecorderFilenameFields& fields) {
    return LabRecorderFilenamePolicy::filenameCommand(fields);
}

QString LabRecorderClient::renderedFilename(const LabRecorderFilenameFields& fields) {
    return LabRecorderFilenamePolicy::renderedFilename(fields);
}

bool LabRecorderClient::hasUnresolvedFilenamePlaceholders(
    const LabRecorderFilenameFields& fields) {
    return LabRecorderFilenamePolicy::hasUnresolvedFilenamePlaceholders(fields);
}

QStringList LabRecorderClient::startRecordingCommands(
    const LabRecorderFilenameFields& fields,
    bool select_all_first) {
    return LabRecorderFilenamePolicy::startRecordingCommands(fields, select_all_first);
}

QString LabRecorderClient::sanitizedValue(QString value) {
    return LabRecorderFilenamePolicy::sanitizedValue(std::move(value));
}
