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
    command_timeout_.setSingleShot(true);
    connect(&socket_, &QTcpSocket::connected, this, &LabRecorderClient::onConnected);
    connect(&socket_, &QTcpSocket::disconnected, this, &LabRecorderClient::onDisconnected);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &LabRecorderClient::onSocketError);
    connect(&socket_, &QTcpSocket::bytesWritten, this, &LabRecorderClient::onBytesWritten);
    connect(&socket_, &QTcpSocket::readyRead, this, &LabRecorderClient::onReadyRead);
    connect(&command_timeout_, &QTimer::timeout, this, &LabRecorderClient::onCommandTimeout);
}

void LabRecorderClient::connectToServer(const QString& host, quint16 port, int timeout_ms) {
    command_timeout_.stop();
    batches_.clear();
    have_active_batch_ = false;
    response_buffer_.clear();
    socket_.abort();
    last_error_.clear();
    command_timeout_.setInterval((std::max)(1, timeout_ms));
    setRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Connecting,
                       "Connecting to LabRecorder RCS...");
    socket_.connectToHost(host, port);
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
    response_buffer_.clear();
    writeNextCommand();
}

void LabRecorderClient::writeNextCommand() {
    if (!have_active_batch_) {
        return;
    }
    if (active_batch_.next_command >= active_batch_.commands.size()) {
        finishActiveBatch(true, "Command sent");
        return;
    }
    QByteArray payload = active_batch_.commands[active_batch_.next_command].toUtf8();
    payload.append('\n');
    const qint64 accepted = socket_.write(payload);
    if (accepted < 0) {
        finishActiveBatch(false, socket_.errorString());
        return;
    }
    command_timeout_.start();
    if (socket_.bytesToWrite() == 0) {
        onBytesWritten(accepted);
    }
}

void LabRecorderClient::finishActiveBatch(bool ok, const QString& message) {
    if (!have_active_batch_) {
        return;
    }
    command_timeout_.stop();
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
    startNextBatch();
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
    setConnectionState(RecorderConnectionState::Connected, "Connected to LabRecorder RCS");
    setRecordingState(RecorderRecordingState::Stopped);
    startNextBatch();
}

void LabRecorderClient::onDisconnected() {
    if (have_active_batch_) {
        finishActiveBatch(false, "LabRecorder disconnected during command");
    }
    batches_.clear();
    setRecordingState(RecorderRecordingState::Unknown);
    if (connection_state_ != RecorderConnectionState::Error) {
        setConnectionState(RecorderConnectionState::Disconnected, "LabRecorder disconnected");
    }
}

void LabRecorderClient::onSocketError(QAbstractSocket::SocketError error) {
    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    last_error_ = socket_.errorString();
    if (have_active_batch_) {
        finishActiveBatch(false, last_error_);
    }
    batches_.clear();
    setRecordingState(RecorderRecordingState::Unknown);
    setConnectionState(RecorderConnectionState::Error, last_error_);
}

void LabRecorderClient::onBytesWritten(qint64) {
    if (!have_active_batch_ || socket_.bytesToWrite() != 0) {
        return;
    }
    command_timeout_.stop();
    ++active_batch_.next_command;
    writeNextCommand();
}

void LabRecorderClient::onReadyRead() {
    response_buffer_.append(socket_.readAll());
}

void LabRecorderClient::onCommandTimeout() {
    finishActiveBatch(false, "Timed out writing LabRecorder command");
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
