#include "gui/LabRecorderClient.h"
#include "gui/LabRecorderFilenamePolicy.h"

#include <algorithm>
#include <utility>

LabRecorderClient::LabRecorderClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<RecorderConnectionState>("RecorderConnectionState");
    qRegisterMetaType<RecorderRecordingState>("RecorderRecordingState");
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

void LabRecorderClient::connectToServer(const QString& host,
                                        quint16 port,
                                        int connection_timeout_ms,
                                        int command_timeout_ms) {
    connection_timeout_.stop();
    command_timeout_.stop();
    batches_.clear();
    if (have_active_batch_) {
        finishActiveBatch(false, "LabRecorder connection replaced", false);
    }
    pending_payload_.clear();
    response_buffer_.clear();
    socket_.abort();
    connection_timeout_.setInterval((std::max)(1, connection_timeout_ms));
    command_timeout_.setInterval((std::max)(1, command_timeout_ms));
    setRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Connecting,
                       "Connecting to LabRecorder RCS...");
    socket_.connectToHost(host, port);
    if (connection_state_ == RecorderConnectionState::Connecting) {
        connection_timeout_.start();
    }
}

bool LabRecorderClient::isConnected() const {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

bool LabRecorderClient::sendCommand(const QString& command) {
    return enqueueCommands(command, {command}, RecorderRecordingState::Unknown);
}

bool LabRecorderClient::refreshStreams() {
    return enqueueCommands("refresh streams", {"update"}, RecorderRecordingState::Unknown);
}

bool LabRecorderClient::updateFilename(const LabRecorderFilenameFields& fields) {
    return enqueueCommands(
        "update filename", {filenameCommand(fields)}, RecorderRecordingState::Unknown);
}

bool LabRecorderClient::startRecording(const LabRecorderFilenameFields& fields, bool select_all_first) {
    return enqueueCommands("start recording",
                           startRecordingCommands(fields, select_all_first),
                           RecorderRecordingState::Recording);
}

bool LabRecorderClient::stopRecording() {
    return enqueueCommands("stop recording", {"stop"}, RecorderRecordingState::Stopped);
}

bool LabRecorderClient::enqueueCommands(QString operation,
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
    batches_.enqueue({std::move(operation), std::move(commands), 0, success_state});
    startNextBatch();
    return true;
}

void LabRecorderClient::startNextBatch() {
    if (have_active_batch_ || batches_.isEmpty() || !isConnected()) {
        return;
    }
    active_batch_ = batches_.dequeue();
    have_active_batch_ = true;
    pending_payload_.clear();
    response_buffer_.clear();
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
        pending_payload_ = active_batch_.commands[active_batch_.next_command].toUtf8();
        pending_payload_.append('\n');
        response_buffer_.clear();
        command_timeout_.start();
    }

    const qint64 accepted = socket_.write(pending_payload_);
    if (accepted < 0) {
        failActiveConnection(socket_.errorString());
        return;
    }
    if (accepted > 0) {
        pending_payload_.remove(0, accepted);
    }
}

void LabRecorderClient::finishActiveBatch(bool ok,
                                          const QString& message,
                                          bool start_next) {
    if (!have_active_batch_) {
        return;
    }
    command_timeout_.stop();
    pending_payload_.clear();
    response_buffer_.clear();
    const QString operation = active_batch_.operation;
    const RecorderRecordingState success_state = active_batch_.success_state;
    have_active_batch_ = false;
    if (ok) {
        if (success_state != RecorderRecordingState::Unknown) {
            setRecordingState(success_state);
        }
    } else {
        setRecordingState(RecorderRecordingState::Unknown);
    }
    emit commandFinished(operation, ok, message);
    if (start_next) {
        startNextBatch();
    }
}

void LabRecorderClient::failActiveConnection(const QString& message) {
    connection_timeout_.stop();
    command_timeout_.stop();
    batches_.clear();
    setRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Error, message);
    if (have_active_batch_) {
        finishActiveBatch(false, message, false);
    }
    pending_payload_.clear();
    response_buffer_.clear();
    socket_.abort();
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

void LabRecorderClient::onConnected() {
    connection_timeout_.stop();
    setConnectionState(
        RecorderConnectionState::Connected,
        "Connected to LabRecorder RCS; recording state is unknown until Start or Stop is acknowledged");
    setRecordingState(RecorderRecordingState::Unknown);
    startNextBatch();
}

void LabRecorderClient::onDisconnected() {
    connection_timeout_.stop();
    command_timeout_.stop();
    if (have_active_batch_) {
        finishActiveBatch(false, "LabRecorder disconnected during command", false);
    }
    batches_.clear();
    pending_payload_.clear();
    response_buffer_.clear();
    setRecordingState(RecorderRecordingState::Unknown);
    if (connection_state_ != RecorderConnectionState::Error) {
        setConnectionState(RecorderConnectionState::Disconnected, "LabRecorder disconnected");
    }
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

bool LabRecorderClient::hasUnresolvedFilenamePlaceholders(const LabRecorderFilenameFields& fields) {
    return LabRecorderFilenamePolicy::hasUnresolvedFilenamePlaceholders(fields);
}

QStringList LabRecorderClient::startRecordingCommands(const LabRecorderFilenameFields& fields,
                                                      bool select_all_first) {
    return LabRecorderFilenamePolicy::startRecordingCommands(fields, select_all_first);
}

QString LabRecorderClient::sanitizedValue(QString value) {
    return LabRecorderFilenamePolicy::sanitizedValue(std::move(value));
}
