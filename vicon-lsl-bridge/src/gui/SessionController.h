#pragma once

#include "gui/LabRecorderFilenamePolicy.h"
#include "gui/PerformanceBudgets.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"

#include <QJsonObject>
#include <QStringList>

#include <functional>
#include <utility>

namespace vicon_lsl::gui {

enum class SessionWorkflowState {
    Idle,
    Preparing,
    PreflightBlocked,
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

enum class ShutdownResult {
    NotStarted,
    InProgress,
    Completed,
    DeadlineExceeded,
    RecorderConnectionLost,
};

struct SessionPreflightInputs {
    SessionConfiguration configuration;
    ComponentLifecycleState bridge_state = ComponentLifecycleState::Idle;
    bool bridge_status_recent = false;
    double bridge_effective_rate = 0.0;
    RecorderConnectionState recorder_connection = RecorderConnectionState::Disconnected;
    RecorderRecordingState recorder_recording = RecorderRecordingState::Unknown;
    RecorderOperationState recorder_operation = RecorderOperationState::Idle;
    bool allowlist_recorder_available = false;
    RecordingPathResult path;
    QVector<StreamIdentity> streams;
    bool stair_model_loaded = false;
    SessionCalibrationState calibration_state = SessionCalibrationState::Manual;
    bool calibration_metadata_compatible = true;
};

struct SessionDashboardState {
    SessionWorkflowState workflow = SessionWorkflowState::Idle;
    ComponentLifecycleState bridge = ComponentLifecycleState::Idle;
    ComponentLifecycleState preview = ComponentLifecycleState::Idle;
    RecorderConnectionState recorder_connection = RecorderConnectionState::Disconnected;
    RecorderRecordingState recorder_recording = RecorderRecordingState::Unknown;
    RecorderOperationState recorder_operation = RecorderOperationState::Idle;
    RecorderProcessState recorder_process = RecorderProcessState::External;
    SessionCalibrationState calibration = SessionCalibrationState::Manual;
    SessionFileState file = SessionFileState::None;
    RecordingVerificationState verification = RecordingVerificationState::NotRun;
    QString recording_path;
    QString run_identifier;
    QDateTime recording_started_at;
    QVector<StreamIdentity> selected_streams;
    qint64 available_storage_bytes = -1;
    unsigned long long preview_replaced_frames = 0;
    unsigned long long preview_coalesced_input_samples = 0;
    qint64 preview_latency_ms = 0;
};

struct ComponentShutdownStatus {
    SessionComponent component = SessionComponent::Application;
    bool required = false;
    bool stopped = true;
    qint64 requested_at_ms = -1;
    qint64 deadline_ms = -1;
    bool deadline_exceeded = false;
    QString detail;
};

struct SessionShutdownStatus {
    ShutdownResult result = ShutdownResult::NotStarted;
    qint64 started_at_ms = -1;
    QVector<ComponentShutdownStatus> components;

    bool complete() const;
    bool ownedRecorderMayBeEnded(qint64 now_ms) const;
    QStringList delayedComponents(qint64 now_ms) const;
    QJsonObject toJson(qint64 now_ms) const;
};

class SessionController {
public:
    SessionController();
    void setClock(std::function<QDateTime()> clock);

    SessionEventLog& eventLog() { return event_log_; }
    const SessionEventLog& eventLog() const { return event_log_; }
    SessionDashboardState& dashboard() { return dashboard_; }
    const SessionDashboardState& dashboard() const { return dashboard_; }
    const PreflightResult& lastPreflight() const { return last_preflight_; }
    const SessionShutdownStatus& shutdownStatus() const { return shutdown_; }

    PreflightResult runPreflight(const SessionPreflightInputs& inputs);
    bool overridePreflight(const QString& reason);
    void beginShutdown(qint64 now_ms,
                       bool bridge_required,
                       bool preview_required,
                       bool recorder_required,
                       bool file_required,
                       bool verification_required);
    void updateShutdownComponent(SessionComponent component,
                                 bool stopped,
                                 const QString& detail,
                                 qint64 now_ms);
    void markRecorderConnectionLostDuringShutdown(qint64 now_ms,
                                                  const QString& detail);
    void updateShutdownDeadlines(qint64 now_ms);
    QJsonObject toJson(qint64 now_ms) const;

    static QString workflowStateText(SessionWorkflowState state);
    static QString calibrationStateText(SessionCalibrationState state);
    static QString fileStateText(SessionFileState state);
    static QString shutdownResultText(ShutdownResult result);

private:
    static ComponentShutdownStatus makeShutdownComponent(SessionComponent component,
                                                         bool required,
                                                         qint64 now_ms,
                                                         qint64 deadline_ms);

    SessionEventLog event_log_;
    SessionDashboardState dashboard_;
    PreflightResult last_preflight_;
    SessionShutdownStatus shutdown_;
    std::function<QDateTime()> clock_;
};

} // namespace vicon_lsl::gui

Q_DECLARE_METATYPE(vicon_lsl::gui::SessionWorkflowState)
Q_DECLARE_METATYPE(vicon_lsl::gui::SessionCalibrationState)
Q_DECLARE_METATYPE(vicon_lsl::gui::SessionFileState)
Q_DECLARE_METATYPE(vicon_lsl::gui::ShutdownResult)
