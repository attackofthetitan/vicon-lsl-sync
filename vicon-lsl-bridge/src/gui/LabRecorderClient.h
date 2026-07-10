#pragma once

#include <QObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QQueue>
#include <QTimer>
#include <QTcpSocket>

enum class RecorderConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error,
};

enum class RecorderRecordingState {
    Unknown,
    Stopped,
    Recording,
};

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

    void connectToServer(const QString& host, quint16 port, int timeout_ms = 1000);
    bool isConnected() const;
    RecorderConnectionState connectionState() const { return connection_state_; }
    RecorderRecordingState recordingState() const { return recording_state_; }
    QString lastError() const;

    bool sendCommand(const QString& command);
    bool refreshStreams();
    bool startRecording(const LabRecorderFilenameFields& fields, bool select_all_first);
    bool stopRecording();

    static QString filenameCommand(const LabRecorderFilenameFields& fields);
    static QString renderedFilename(const LabRecorderFilenameFields& fields);
    static bool hasUnresolvedFilenamePlaceholders(const LabRecorderFilenameFields& fields);
    static QStringList startRecordingCommands(const LabRecorderFilenameFields& fields, bool select_all_first);
    static QString sanitizedValue(QString value);

signals:
    void connectionStateChanged(RecorderConnectionState state, const QString& message);
    void recordingStateChanged(RecorderRecordingState state);
    void commandFinished(const QString& operation, bool ok, const QString& message);

private slots:
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onBytesWritten(qint64 bytes);
    void onReadyRead();
    void onConnectionTimeout();
    void onCommandTimeout();

private:
    struct CommandBatch {
        QString operation;
        QStringList commands;
        qsizetype next_command = 0;
        RecorderRecordingState success_state = RecorderRecordingState::Unknown;
    };

    bool enqueueCommands(QString operation,
                         QStringList commands,
                         RecorderRecordingState success_state);
    void startNextBatch();
    void writeNextCommand();
    void finishActiveBatch(bool ok, const QString& message, bool start_next = true);
    void failActiveConnection(const QString& message);
    void setConnectionState(RecorderConnectionState state, const QString& message = {});
    void setRecordingState(RecorderRecordingState state);

    QTcpSocket socket_;
    QTimer connection_timeout_;
    QTimer command_timeout_;
    QQueue<CommandBatch> batches_;
    CommandBatch active_batch_;
    bool have_active_batch_ = false;
    QByteArray pending_payload_;
    QByteArray response_buffer_;
    QString last_error_;
    RecorderConnectionState connection_state_ = RecorderConnectionState::Disconnected;
    RecorderRecordingState recording_state_ = RecorderRecordingState::Unknown;
};

Q_DECLARE_METATYPE(RecorderConnectionState)
Q_DECLARE_METATYPE(RecorderRecordingState)
