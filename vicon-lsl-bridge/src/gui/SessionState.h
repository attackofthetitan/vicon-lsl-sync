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

namespace vicon_lsl::gui {

enum class SessionWorkflowState {
    Idle,
    Preparing,
    SetupBlocked,
    Ready,
    Starting,
    Recording,
    Stopping,
    Verifying,
    Complete,
    Failed,
    Closing,
};

enum class SessionCalibrationState {
    Manual,
    Collecting,
    AutomaticSession,
    SavedProfile,
    Failed,
};

enum class SessionFileState {
    None,
    Loading,
    Loaded,
    Canceled,
    Failed,
};

} // namespace vicon_lsl::gui

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

enum class SetupCheckLevel {
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

    static QString componentText(SessionComponent component);
    static QString severityText(EventSeverity severity);

private:
    std::size_t maximum_entries_;
    QVector<SessionEvent> entries_;
    QString last_error_;
    std::function<QDateTime()> clock_;
};

struct SetupCheckItem {
    SessionComponent component = SessionComponent::Application;
    SetupCheckLevel level = SetupCheckLevel::Information;
    bool passed = true;
    QString message;
    QString corrective_action;
};

struct SetupCheckResult {
    QDateTime completed_at;
    QVector<SetupCheckItem> items;
    bool override_used = false;
    QString override_reason;

    bool hasRequiredFailures() const;
    bool hasWarnings() const;
    QString summary() const;
    QJsonObject toJson() const;
};

QString recorderConnectionStateText(RecorderConnectionState state);
QString recorderRecordingStateText(RecorderRecordingState state);
QString recorderOperationStateText(RecorderOperationState state);
QString componentLifecycleStateText(ComponentLifecycleState state);
QString recorderProcessStateText(RecorderProcessState state);
QString verificationStateText(RecordingVerificationState state);

namespace vicon_lsl::gui {
QString workflowStateText(SessionWorkflowState state);
QString calibrationStateText(SessionCalibrationState state);
QString fileStateText(SessionFileState state);
} // namespace vicon_lsl::gui

Q_DECLARE_METATYPE(RecorderConnectionState)
Q_DECLARE_METATYPE(RecorderRecordingState)
Q_DECLARE_METATYPE(RecorderOperationState)
Q_DECLARE_METATYPE(ComponentLifecycleState)
Q_DECLARE_METATYPE(RecorderProcessState)
Q_DECLARE_METATYPE(RecordingVerificationState)
Q_DECLARE_METATYPE(vicon_lsl::gui::SessionWorkflowState)
Q_DECLARE_METATYPE(vicon_lsl::gui::SessionCalibrationState)
Q_DECLARE_METATYPE(vicon_lsl::gui::SessionFileState)
