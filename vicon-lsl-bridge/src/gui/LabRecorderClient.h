#pragma once

#include <QObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QQueue>
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
    RecorderRecordingState desiredRecordingState() const { return desired_recording_state_; }
    RecorderOperationState operationState() const { return operation_state_; }
    int queueDepth() const { return batches_.size() + (have_active_batch_ ? 1 : 0); }
    bool shutdownRequested() const { return shutdown_requested_; }
    bool shutdownReady() const;
    bool shutdownSettledSafely() const;
    bool startMayHaveReachedServer() const { return start_may_have_reached_server_; }
    QString activeOperation() const;
    QString activeCommand() const;
    int activeCommandNumber() const;
    int activeCommandCount() const;

    bool sendCommand(const QString& command);
    bool refreshStreams();
    bool updateFilename(const LabRecorderFilenameFields& fields);
    bool startRecording(const LabRecorderFilenameFields& fields, bool select_all_first);
    bool stopRecording();
    bool beginShutdown();
    void disconnectFromServer();

    static QString filenameCommand(const LabRecorderFilenameFields& fields);
    static QString renderedFilename(const LabRecorderFilenameFields& fields);
    static bool hasUnresolvedFilenamePlaceholders(const LabRecorderFilenameFields& fields);
    static QStringList startRecordingCommands(const LabRecorderFilenameFields& fields, bool select_all_first);
    static QString sanitizedValue(QString value);

signals:
    void connectionStateChanged(RecorderConnectionState state, const QString& message);
    void recordingStateChanged(RecorderRecordingState state);
    void desiredRecordingStateChanged(RecorderRecordingState state);
    void operationStateChanged(RecorderOperationState state, int queue_depth);
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
        bool cancel_after_current = false;
        bool start_command_sent = false;
    };

    bool enqueueCommands(CommandKind kind,
                         QString operation,
                         QStringList commands,
                         RecorderRecordingState success_state);
    bool enqueueStop(bool shutdown);
    bool hasWork(CommandKind kind) const;
    bool replaceQueuedFilename(const QString& command);
    void removeQueuedNonessential(const QString& reason, bool emit_failures);
    void clearQueued(const QString& reason, bool emit_failures = true);
    void startNextBatch();
    void writeNextCommand();
    void finishActiveBatch(bool ok,
                           const QString& message,
                           bool start_next = true,
                           bool preserve_recording_state = false);
    void failActiveConnection(const QString& message);
    void setConnectionState(RecorderConnectionState state, const QString& message = {});
    void setRecordingState(RecorderRecordingState state);
    void setDesiredRecordingState(RecorderRecordingState state);
    void updateOperationState();
    static RecorderOperationState operationForKind(CommandKind kind);

    QTcpSocket socket_;
    QTimer connection_timeout_;
    QTimer command_timeout_;
    QQueue<CommandBatch> batches_;
    CommandBatch active_batch_;
    bool have_active_batch_ = false;
    QByteArray pending_payload_;
    QByteArray response_buffer_;
    RecorderConnectionState connection_state_ = RecorderConnectionState::Disconnected;
    RecorderRecordingState recording_state_ = RecorderRecordingState::Unknown;
    RecorderRecordingState desired_recording_state_ = RecorderRecordingState::Unknown;
    RecorderOperationState operation_state_ = RecorderOperationState::Idle;
    bool shutdown_requested_ = false;
    bool start_may_have_reached_server_ = false;
    QString active_cancel_reason_;
    int last_emitted_queue_depth_ = -1;
};
