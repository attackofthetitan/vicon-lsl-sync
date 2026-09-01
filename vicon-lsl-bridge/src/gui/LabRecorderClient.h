#pragma once

#include <QObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTcpSocket>

#include "gui/LabRecorderFilenamePolicy.h"
#include "gui/SessionState.h"

class LabRecorderClient : public QObject {
    Q_OBJECT
public:
    explicit LabRecorderClient(QObject* parent = nullptr);
    ~LabRecorderClient() override;

    void connectToServer(const QString& host,
                         quint16 port,
                         int connection_timeout_ms = 1000,
                         int command_timeout_ms = 5000);
    bool isConnected() const;
    RecorderConnectionState connectionState() const { return connection_state_; }
    RecorderRecordingState recordingState() const { return recording_state_; }
    RecorderRecordingState desiredRecordingState() const;
    RecorderOperationState operationState() const { return operation_state_; }
    bool shutdownRequested() const { return shutdown_requested_; }
    bool shutdownReady() const;
    bool shutdownSettledSafely() const;
    bool startMayHaveReachedServer() const { return start_may_have_reached_server_; }

    bool sendCommand(const QString& command);
    bool refreshStreams();
    bool updateFilename(const LabRecorderFilenameFields& fields);
    bool startRecording(const LabRecorderFilenameFields& fields, bool select_all_first);
    bool stopRecording();
    bool beginShutdown();
    void disconnectFromServer();

signals:
    void connectionStateChanged(RecorderConnectionState state, const QString& message);
    void recordingStateChanged(RecorderRecordingState state);
    void operationStateChanged(RecorderOperationState state);
    void commandProgress(const QString& operation,
                         int command_number,
                         int command_count,
                         const QString& command);
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
    enum class CommandKind {
        Generic,
        Refresh,
        Filename,
        Start,
        Stop,
    };

    struct CommandBatch {
        CommandKind kind = CommandKind::Generic;
        QString operation;
        QStringList commands;
        qsizetype next_command = 0;
        RecorderRecordingState success_state = RecorderRecordingState::Unknown;
    };

    bool beginBatch(CommandKind kind,
                    QString operation,
                    QStringList commands,
                    RecorderRecordingState success_state);
    bool requestStop(QString operation);
    void continueShutdown();
    void writeNextCommand();
    void finishActiveBatch(bool ok, const QString& message);
    void failActiveConnection(const QString& message);
    void setConnectionState(RecorderConnectionState state, const QString& message = {});
    void setRecordingState(RecorderRecordingState state);
    void updateOperationState();
    static RecorderOperationState operationForKind(CommandKind kind);

    QTcpSocket socket_;
    QTimer connection_timeout_;
    QTimer command_timeout_;
    CommandBatch active_batch_;
    bool have_active_batch_ = false;
    QByteArray pending_payload_;
    QByteArray response_buffer_;
    RecorderConnectionState connection_state_ = RecorderConnectionState::Disconnected;
    RecorderRecordingState recording_state_ = RecorderRecordingState::Unknown;
    RecorderOperationState operation_state_ = RecorderOperationState::Idle;
    bool shutdown_requested_ = false;
    bool start_may_have_reached_server_ = false;
};
