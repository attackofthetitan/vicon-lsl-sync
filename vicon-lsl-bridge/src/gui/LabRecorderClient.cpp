#include "gui/LabRecorderClient.h"

#include <algorithm>
#include <utility>

namespace {

struct FilenameToken {
    const char* placeholder;
    QString LabRecorderFilenameFields::*field;
};

const FilenameToken kFilenameTokens[] = {
    {"%p", &LabRecorderFilenameFields::participant},
    {"%s", &LabRecorderFilenameFields::session},
    {"%b", &LabRecorderFilenameFields::task},
    {"%r", &LabRecorderFilenameFields::run},
    {"%n", &LabRecorderFilenameFields::run},
    {"%a", &LabRecorderFilenameFields::acquisition},
    {"%m", &LabRecorderFilenameFields::modality},
};

void appendField(QString& command, const QString& key, const QString& value) {
    const QString sanitized = LabRecorderClient::sanitizedValue(value);
    if (sanitized.isEmpty()) {
        return;
    }
    command += " {" + key + ":" + sanitized + "}";
}

} // namespace

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

void LabRecorderClient::connectToServer(const QString& host, quint16 port, int timeout_ms) {
    connection_timeout_.stop();
    command_timeout_.stop();
    batches_.clear();
    if (have_active_batch_) {
        finishActiveBatch(false, "LabRecorder connection replaced", false);
    }
    pending_payload_.clear();
    response_buffer_.clear();
    socket_.abort();
    last_error_.clear();
    const int timeout = (std::max)(1, timeout_ms);
    connection_timeout_.setInterval(timeout);
    command_timeout_.setInterval(timeout);
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

QString LabRecorderClient::lastError() const {
    return last_error_;
}

bool LabRecorderClient::sendCommand(const QString& command) {
    return enqueueCommands(command, {command}, RecorderRecordingState::Unknown);
}

bool LabRecorderClient::refreshStreams() {
    return enqueueCommands("refresh streams", {"update"}, RecorderRecordingState::Unknown);
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
        last_error_ = "LabRecorder RCS is not connected";
        emit commandFinished(operation, false, last_error_);
        return false;
    }
    if (commands.isEmpty()) {
        last_error_ = "LabRecorder command batch is empty";
        emit commandFinished(operation, false, last_error_);
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
        last_error_.clear();
        if (success_state != RecorderRecordingState::Unknown) {
            setRecordingState(success_state);
        }
    } else {
        last_error_ = message;
        setRecordingState(RecorderRecordingState::Unknown);
    }
    emit commandFinished(operation, ok, message);
    if (start_next) {
        startNextBatch();
    }
}

void LabRecorderClient::failActiveConnection(const QString& message) {
    last_error_ = message;
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
    QString command = "filename";
    appendField(command, "root", fields.root);
    appendField(command, "template", fields.templ);
    appendField(command, "participant", fields.participant);
    appendField(command, "session", fields.session);
    appendField(command, "task", fields.task);
    appendField(command, "run", fields.run);
    appendField(command, "acquisition", fields.acquisition);
    appendField(command, "modality", fields.modality);
    return command;
}

QString LabRecorderClient::renderedFilename(const LabRecorderFilenameFields& fields) {
    QString rendered = sanitizedValue(fields.templ);
    for (const FilenameToken& token : kFilenameTokens) {
        rendered.replace(QLatin1String(token.placeholder), sanitizedValue(fields.*(token.field)));
    }
    return rendered;
}

bool LabRecorderClient::hasUnresolvedFilenamePlaceholders(const LabRecorderFilenameFields& fields) {
    const QString templ = sanitizedValue(fields.templ);
    for (const FilenameToken& token : kFilenameTokens) {
        if (templ.contains(QLatin1String(token.placeholder)) &&
            sanitizedValue(fields.*(token.field)).isEmpty()) {
            return true;
        }
    }
    return renderedFilename(fields).contains('%');
}

QStringList LabRecorderClient::startRecordingCommands(const LabRecorderFilenameFields& fields,
                                                      bool select_all_first) {
    QStringList commands;
    if (select_all_first) {
        commands.append("select all");
    }
    commands.append(filenameCommand(fields));
    commands.append("start");
    return commands;
}

QString LabRecorderClient::sanitizedValue(QString value) {
    value.replace('{', '_');
    value.replace('}', '_');
    value.replace('\n', ' ');
    value.replace('\r', ' ');
    return value.trimmed();
}
