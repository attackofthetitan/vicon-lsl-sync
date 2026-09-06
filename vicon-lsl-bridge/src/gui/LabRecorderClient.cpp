#include "gui/LabRecorderClient.h"
#include "gui/LabRecorderFilenamePolicy.h"

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
    connect(&connection_timeout_, &QTimer::timeout, this, &LabRecorderClient::onConnectionTimeout);
    connect(&command_timeout_, &QTimer::timeout, this, &LabRecorderClient::onCommandTimeout);
}

LabRecorderClient::~LabRecorderClient() {
    connection_timeout_.stop();
    command_timeout_.stop();
    QObject::disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
}

void LabRecorderClient::connectToServer(const QString& host, quint16 port, int conn_timeout_ms, int cmd_timeout_ms) {
    if (shutdown_requested_) {
        emit commandFinished("connect", false, "Recorder shutdown is already in progress");
        return;
    }
    if (active_batch_ || start_may_have_reached_server_ || recording_state_ == RecorderRecordingState::Recording) {
        emit commandFinished("connect", false, "Recorder connection cannot be replaced while recording work is active");
        return;
    }
    connection_timeout_.stop();
    command_timeout_.stop();
    pending_payload_.clear();
    response_buffer_.clear();
    socket_.abort();
    connection_timeout_.setInterval((std::max)(1, conn_timeout_ms));
    command_timeout_.setInterval((std::max)(1, cmd_timeout_ms));
    setRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Connecting, "Connecting to LabRecorder remote control...");
    socket_.connectToHost(host, port);
    if (connection_state_ == RecorderConnectionState::Connecting) connection_timeout_.start();
}

void LabRecorderClient::disconnectFromServer() {
    connection_timeout_.stop();
    command_timeout_.stop();
    if (active_batch_) finishActiveBatch(false, "LabRecorder connection detached");
    pending_payload_.clear();
    response_buffer_.clear();
    socket_.abort();
    start_may_have_reached_server_ = false;
    setRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Disconnected, "LabRecorder connection detached");
    updateOperationState();
}

bool LabRecorderClient::isConnected() const {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

RecorderRecordingState LabRecorderClient::desiredRecordingState() const {
    if (operation_state_ == RecorderOperationState::Starting) return RecorderRecordingState::Recording;
    if (operation_state_ == RecorderOperationState::Stopping) return RecorderRecordingState::Stopped;
    return recording_state_;
}

bool LabRecorderClient::shutdownReady() const {
    return shutdown_requested_ && !active_batch_;
}

bool LabRecorderClient::shutdownSettledSafely() const {
    return shutdownReady() && !start_may_have_reached_server_;
}

bool LabRecorderClient::sendCommand(const QString& command) {
    return beginBatch(RecorderOperationState::Refreshing, command, {command});
}

bool LabRecorderClient::refreshStreams() {
    return beginBatch(RecorderOperationState::Refreshing, "refresh streams", {"update"});
}

bool LabRecorderClient::updateFilename(const LabRecorderFilenameFields& fields) {
    if (recording_state_ == RecorderRecordingState::Recording) {
        emit commandFinished("update filename", false, "Filename changes are unavailable while recording");
        return false;
    }
    return beginBatch(RecorderOperationState::UpdatingFilename, "update filename",
                      {LabRecorderFilenamePolicy::filenameCommand(fields)});
}

bool LabRecorderClient::startRecording(const LabRecorderFilenameFields& fields, bool select_all_first) {
    if (start_may_have_reached_server_ || recording_state_ == RecorderRecordingState::Recording) {
        emit commandFinished("start recording", false, "A recording is already active");
        return false;
    }
    return beginBatch(RecorderOperationState::Starting, "start recording",
                      LabRecorderFilenamePolicy::startRecordingCommands(fields, select_all_first));
}

bool LabRecorderClient::stopRecording() {
    if (shutdown_requested_) {
        emit commandFinished("stop recording", false, "Recorder shutdown is already in progress");
        return false;
    }
    return requestStop("stop recording");
}

bool LabRecorderClient::beginShutdown() {
    if (!shutdown_requested_) {
        shutdown_requested_ = true;
        continueShutdown();
    }
    return !shutdownReady();
}

bool LabRecorderClient::beginBatch(RecorderOperationState state, QString operation, QStringList commands) {
    if (shutdown_requested_ && state != RecorderOperationState::Stopping) {
        emit commandFinished(operation, false, "Recorder shutdown is already in progress");
        return false;
    }
    if (active_batch_) {
        emit commandFinished(operation, false, "Another LabRecorder operation is already active");
        return false;
    }
    if (!isConnected()) {
        emit commandFinished(operation, false, "LabRecorder remote control is not connected");
        return false;
    }
    if (commands.isEmpty()) {
        emit commandFinished(operation, false, "No LabRecorder commands were provided");
        return false;
    }
    active_batch_ = CommandBatch{state, std::move(operation), std::move(commands)};
    pending_payload_.clear();
    response_buffer_.clear();
    updateOperationState();
    writeNextCommand();
    return true;
}

bool LabRecorderClient::requestStop(QString operation) {
    return beginBatch(RecorderOperationState::Stopping, std::move(operation), {"stop"});
}

void LabRecorderClient::continueShutdown() {
    if (!shutdown_requested_) return;
    if (active_batch_) {
        updateOperationState();
        return;
    }
    if (start_may_have_reached_server_ || recording_state_ == RecorderRecordingState::Recording) {
        if (isConnected()) requestStop("shutdown stop recording");
        else {
            setRecordingState(RecorderRecordingState::Unknown);
            updateOperationState();
        }
        return;
    }
    updateOperationState();
}

void LabRecorderClient::writeNextCommand() {
    if (!active_batch_) return;
    if (active_batch_->next_command >= active_batch_->commands.size()) {
        finishActiveBatch(true, "Recorder confirmed the command");
        return;
    }
    if (pending_payload_.isEmpty()) {
        const QString cmd = active_batch_->commands.at(active_batch_->next_command);
        pending_payload_ = cmd.toUtf8() + '\n';
        response_buffer_.clear();
        command_timeout_.start();
        emit commandProgress(active_batch_->operation, static_cast<int>(active_batch_->next_command + 1),
                             static_cast<int>(active_batch_->commands.size()), cmd);
    }
    const qint64 accepted = socket_.write(pending_payload_);
    if (accepted < 0) {
        failActiveConnection(socket_.errorString());
        return;
    }
    if (accepted > 0) {
        const QString cmd = active_batch_->commands.at(active_batch_->next_command);
        if (active_batch_->state == RecorderOperationState::Starting && cmd == "start") start_may_have_reached_server_ = true;
        pending_payload_.remove(0, accepted);
    }
}

void LabRecorderClient::finishActiveBatch(bool ok, const QString& message) {
    if (!active_batch_) return;
    command_timeout_.stop();
    pending_payload_.clear();
    response_buffer_.clear();
    const CommandBatch completed = std::move(*active_batch_);
    active_batch_.reset();

    if (ok) {
        if (completed.state == RecorderOperationState::Starting) {
            setRecordingState(RecorderRecordingState::Recording);
        } else if (completed.state == RecorderOperationState::Stopping) {
            setRecordingState(RecorderRecordingState::Stopped);
            start_may_have_reached_server_ = false;
        }
    } else {
        setRecordingState(RecorderRecordingState::Unknown);
    }
    emit commandFinished(completed.operation, ok, message);
    updateOperationState();
    continueShutdown();
}

void LabRecorderClient::failActiveConnection(const QString& message) {
    connection_timeout_.stop();
    command_timeout_.stop();
    socket_.abort();
    setRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Error, message);
    if (active_batch_) finishActiveBatch(false, message);
    pending_payload_.clear();
    response_buffer_.clear();
    updateOperationState();
}

void LabRecorderClient::setConnectionState(RecorderConnectionState state, const QString& message) {
    if (connection_state_ == state && message.isEmpty()) return;
    connection_state_ = state;
    emit connectionStateChanged(state, message);
}

void LabRecorderClient::setRecordingState(RecorderRecordingState state) {
    if (recording_state_ == state) return;
    recording_state_ = state;
    emit recordingStateChanged(state);
}

void LabRecorderClient::updateOperationState() {
    RecorderOperationState next = active_batch_ ? active_batch_->state
                                 : (shutdown_requested_ ? RecorderOperationState::ShuttingDown : RecorderOperationState::Idle);
    if (next == operation_state_) return;
    operation_state_ = next;
    emit operationStateChanged(operation_state_);
}

void LabRecorderClient::onConnected() {
    connection_timeout_.stop();
    setConnectionState(RecorderConnectionState::Connected,
                       "Connected to LabRecorder remote control; the recorder must confirm Start or Stop before its state is known");
    setRecordingState(RecorderRecordingState::Unknown);
}

void LabRecorderClient::onDisconnected() {
    connection_timeout_.stop();
    command_timeout_.stop();
    if (connection_state_ != RecorderConnectionState::Error) {
        setConnectionState(RecorderConnectionState::Disconnected, "LabRecorder disconnected");
    }
    if (active_batch_) finishActiveBatch(false, "LabRecorder disconnected during command");
    pending_payload_.clear();
    response_buffer_.clear();
    setRecordingState(RecorderRecordingState::Unknown);
    updateOperationState();
}

void LabRecorderClient::onSocketError(QAbstractSocket::SocketError error) {
    if (error != QAbstractSocket::RemoteHostClosedError) failActiveConnection(socket_.errorString());
}

void LabRecorderClient::onBytesWritten(qint64) {
    if (active_batch_ && !pending_payload_.isEmpty()) writeNextCommand();
}

void LabRecorderClient::onReadyRead() {
    response_buffer_.append(socket_.readAll());
    if (!active_batch_ || !pending_payload_.isEmpty()) return;

    while (!response_buffer_.isEmpty() &&
           (response_buffer_.front() == '\r' || response_buffer_.front() == '\n' ||
            response_buffer_.front() == ' ' || response_buffer_.front() == '\t')) {
        response_buffer_.remove(0, 1);
    }
    if (response_buffer_.size() < 2) return;
    if (!response_buffer_.startsWith("OK")) {
        failActiveConnection("Unexpected LabRecorder reply: " + QString::fromUtf8(response_buffer_.left(80)));
        return;
    }
    command_timeout_.stop();
    response_buffer_.remove(0, 2);
    ++active_batch_->next_command;
    writeNextCommand();
}

void LabRecorderClient::onConnectionTimeout() {
    if (connection_state_ == RecorderConnectionState::Connecting) {
        failActiveConnection("Timed out connecting to LabRecorder remote control");
    }
}

void LabRecorderClient::onCommandTimeout() {
    if (active_batch_) failActiveConnection("Timed out waiting for a LabRecorder reply");
}
