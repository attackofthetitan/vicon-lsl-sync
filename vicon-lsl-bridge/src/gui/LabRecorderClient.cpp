#include "gui/LabRecorderClient.h"

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

LabRecorderClient::LabRecorderClient(QObject* parent) : QObject(parent) {}

bool LabRecorderClient::connectToServer(const QString& host, quint16 port, int timeout_ms) {
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.disconnectFromHost();
        socket_.waitForDisconnected(timeout_ms);
    }

    last_error_.clear();
    socket_.connectToHost(host, port);
    if (!socket_.waitForConnected(timeout_ms)) {
        last_error_ = socket_.errorString();
        return false;
    }
    return true;
}

bool LabRecorderClient::isConnected() const {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

QString LabRecorderClient::lastError() const {
    return last_error_;
}

bool LabRecorderClient::sendCommand(const QString& command) {
    if (!isConnected()) {
        last_error_ = "LabRecorder RCS is not connected";
        return false;
    }

    QByteArray payload = command.toUtf8();
    payload.append('\n');

    qint64 written = socket_.write(payload);
    if (written != payload.size() || !socket_.waitForBytesWritten(1000)) {
        last_error_ = socket_.errorString();
        return false;
    }

    last_error_.clear();
    return true;
}

bool LabRecorderClient::refreshStreams() {
    return sendCommand("update");
}

bool LabRecorderClient::startRecording(const LabRecorderFilenameFields& fields, bool select_all_first) {
    const QStringList commands = startRecordingCommands(fields, select_all_first);
    for (const QString& command : commands) {
        if (!sendCommand(command)) {
            return false;
        }
    }
    return true;
}

bool LabRecorderClient::stopRecording() {
    return sendCommand("stop");
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
