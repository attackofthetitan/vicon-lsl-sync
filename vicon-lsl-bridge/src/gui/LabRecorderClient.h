#pragma once

#include <QObject>
#include <QString>
#include <QTcpSocket>

struct LabRecorderFilenameFields {
    QString root;
    QString templ;
    QString participant;
    QString session;
    QString task;
    QString run;
    QString acquisition;
    QString modality;
};

class LabRecorderClient : public QObject {
    Q_OBJECT
public:
    explicit LabRecorderClient(QObject* parent = nullptr);

    bool connectToServer(const QString& host, quint16 port, int timeout_ms = 1000);
    void disconnectFromServer();
    bool isConnected() const;
    QString lastError() const;

    bool sendCommand(const QString& command);
    bool refreshStreams();
    bool selectAll();
    bool selectNone();
    bool updateFilename(const LabRecorderFilenameFields& fields);
    bool startRecording();
    bool stopRecording();

    static QString filenameCommand(const LabRecorderFilenameFields& fields);
    static QString sanitizedValue(QString value);

private:
    QTcpSocket socket_;
    QString last_error_;
};

