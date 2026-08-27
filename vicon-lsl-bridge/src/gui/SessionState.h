#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <cstddef>
#include <functional>
#include <utility>

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

enum class RecorderOperationState {
    Idle,
    Refreshing,
    UpdatingFilename,
    Starting,
    Stopping,
    ShuttingDown,
};

enum class ComponentLifecycleState {
    Idle,
    Starting,
    Running,
    Stopping,
    Stopped,
    Failed,
};

enum class RecorderProcessState {
    External,
    Launching,
    OwnedRunning,
    OwnedExited,
    LaunchFailed,
    Detached,
};

enum class SessionComponent {
    Application,
    Bridge,
    Recorder,
    Preview,
    Calibration,
    File,
    Path,
    Streams,
    Verification,
};

enum class EventSeverity {
    Information,
    Warning,
    Error,
};

enum class PreflightLevel {
    Required,
    Warning,
    Information,
};

enum class RecordingVerificationState {
    NotRun,
    Running,
    Verified,
    VerifiedWithWarnings,
    NeedsAttention,
};

struct SessionEvent {
    QDateTime timestamp;
    SessionComponent component = SessionComponent::Application;
    EventSeverity severity = EventSeverity::Information;
    QString message;
};

class SessionEventLog {
public:
    explicit SessionEventLog(std::size_t maximum_entries = 1000);

    void setClock(std::function<QDateTime()> clock) { clock_ = std::move(clock); }
    void append(SessionComponent component, EventSeverity severity, const QString& message);
    const QVector<SessionEvent>& entries() const { return entries_; }
    QString lastError() const { return last_error_; }
    void acknowledgeLastError();
    QString toText(EventSeverity minimum = EventSeverity::Information,
                   const QVector<SessionComponent>& components = {}) const;
    QJsonArray toJson() const;
    std::size_t maximumEntries() const { return maximum_entries_; }

    static QString componentText(SessionComponent component);
    static QString severityText(EventSeverity severity);

private:
    std::size_t maximum_entries_;
    QVector<SessionEvent> entries_;
    QString last_error_;
    std::function<QDateTime()> clock_;
};

struct PreflightItem {
    QString id;
    SessionComponent component = SessionComponent::Application;
    PreflightLevel level = PreflightLevel::Information;
    bool passed = true;
    QString message;
    QString corrective_action;
};

struct PreflightResult {
    QDateTime completed_at;
    QVector<PreflightItem> items;
    bool override_used = false;
    QString override_reason;

    bool hasRequiredFailures() const;
    bool hasWarnings() const;
    bool canStart() const;
    QString summary() const;
    QJsonObject toJson() const;
};

QString recorderConnectionStateText(RecorderConnectionState state);
QString recorderRecordingStateText(RecorderRecordingState state);
QString recorderOperationStateText(RecorderOperationState state);
QString componentLifecycleStateText(ComponentLifecycleState state);
QString recorderProcessStateText(RecorderProcessState state);
QString verificationStateText(RecordingVerificationState state);

Q_DECLARE_METATYPE(RecorderConnectionState)
Q_DECLARE_METATYPE(RecorderRecordingState)
Q_DECLARE_METATYPE(RecorderOperationState)
Q_DECLARE_METATYPE(ComponentLifecycleState)
Q_DECLARE_METATYPE(RecorderProcessState)
Q_DECLARE_METATYPE(RecordingVerificationState)
