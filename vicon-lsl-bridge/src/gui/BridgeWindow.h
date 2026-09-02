#pragma once

#include "ViconLSLBridge.h"
#include "gui/LabRecorderFilenamePolicy.h"
#include "gui/RecordingVerifier.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionSequencer.h"
#include "gui/SessionState.h"

#include <QElapsedTimer>
#include <QMetaType>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <atomic>

class QCloseEvent;
class QComboBox;
class QWidget;
class QJsonObject;
class QSettings;

class LabRecorderClient;

namespace vicon_lsl {
class StreamDiscoveryWorker;
namespace gui {
class RecorderProcessController;
}
namespace gui_detail {
struct BridgeWindowUi;
}
}

class BridgeWorker : public QThread {
    Q_OBJECT
public:
    explicit BridgeWorker(const Config& config, QObject* parent = nullptr);
    void stopBridge();

signals:
    void statusUpdate(int state, unsigned long long markers, unsigned long long segments,
                      unsigned int frames, const QString& message);
    void lifecycleChanged(ComponentLifecycleState state, QString detail);

protected:
    void run() override;

private:
    void setLifecycleState(ComponentLifecycleState state, const QString& detail = {});

    std::unique_ptr<ViconLSLBridge> bridge_;
    std::atomic<ComponentLifecycleState> lifecycle_state_{ComponentLifecycleState::Idle};
    std::atomic<bool> stop_requested_{false};
};

class BridgeWindow : public QWidget {
    Q_OBJECT
public:
    explicit BridgeWindow(
        QWidget* parent = nullptr,
        bool enable_preview = true,
        std::shared_ptr<QSettings> settings = {});
    ~BridgeWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStart();
    void onStop();
    void onStartSession();
    void onStopSession();
    void onBrowseStudyRoot();
    void onBrowseLabRecorder();
    void onLaunchLabRecorder();
    void onConnectLabRecorder();
    void onDetachLabRecorder();
    void onRefreshLabRecorder();
    void onStartRecording();
    void onStopRecording();
    void onRunSetupCheck();
    void onOverrideSetupCheck();
    void onFindNextRun();
    void onResetConfiguration();
    void onSavePreset();
    void onLoadPreset();
    void onImportConfiguration();
    void onExportConfiguration();
    void onCopyDiagnostics();
    void onExportDiagnostics();
    void onShowVerificationDetails();
    void syncFilenameToLabRecorder();
    void onHeartbeatTick();
    void onStatusUpdate(int state, unsigned long long markers, unsigned long long segments,
                        unsigned int frames, const QString& message);
    void onWorkerFinished();
    void onLabRecorderRetry();
    void onVerificationFilePoll();
    void updateShutdownStatus();

private:
    void connectSignals();
    // Connect whichever "the user changed this" signal each widget type has.
    template <typename Handler>
    void onEdited(std::initializer_list<QWidget*> widgets, Handler handler);
    void loadSettings();
    void saveSettings();
    void applyConfigurationToUi();
    void updateConfigurationFromUi();
    void refreshPresetList(const QString& select = {});
    void restoreUiState();
    void saveUiState();
    RecordingPathValidationOptions pathValidationOptions(bool create_parent) const;
    void validateRecordingPath(bool create_parent = false);
    void setLabRecorderStatus(const QString& status,
                              EventSeverity severity = EventSeverity::Information);
    void scheduleFilenameSync();
    void updateReadiness();
    void updateRecordingButtons();
    void updateDashboard();
    void refreshUi();
    void pumpSession();
    void updateEventLog();
    void eventLogFilter(EventSeverity& minimum,
                        QVector<SessionComponent>& components) const;
    QJsonObject diagnosticBundle() const;
    void appendEvent(SessionComponent component,
                     EventSeverity severity,
                     const QString& message);
    void populateSetupCheck(const SetupCheckResult& result);
    SetupCheckResult runSetupCheck() const;
    void beginRecordingAfterSetupCheck();
    void completePendingRecordingStart();
    void populateStreamTable();
    void populateBindingCombos();
    void updateBindingsFromUi();
    static void selectBindingCombo(QComboBox* combo,
                                   const vicon_lsl::gui::StreamBinding& binding);
    void startStreamDiscovery(bool continue_recording_start);
    void cancelStreamDiscovery();
    QString resolveLabRecorderExecutable() const;
    QString resolveSelectedStreamExecutable() const;
    void beginLabRecorderStartup();
    void launchConfiguredRecorder();
    void requestVerification();
    void startVerifier();
    void finishVerification(const vicon_lsl::gui::RecordingVerificationReport& report);
    void advanceGuidedStart();
    void advanceGuidedStop();
    vicon_lsl::gui::ShutdownInputs shutdownInputs() const;
    RecorderRecordingState effectiveRecordingState() const;
    RecorderOperationState effectiveOperationState() const;
    bool recordingActiveOrPending() const;
    bool bridgeStatusRecent() const;
    void beginClose();

    std::shared_ptr<QSettings> settings_;
    std::unique_ptr<vicon_lsl::gui_detail::BridgeWindowUi> ui_;
    vicon_lsl::gui::SessionConfiguration configuration_;
    vicon_lsl::gui::SessionUiState ui_state_;
    SessionEventLog event_log_;
    SetupCheckResult setup_check_;
    RecordingPathResult path_result_;
    QVector<vicon_lsl::gui::StreamIdentity> stream_inventory_;
    QVector<vicon_lsl::gui::StreamIdentity> recording_inventory_;
    vicon_lsl::gui::RecordingVerificationReport verification_report_;

    LabRecorderClient* labrecorder_client_ = nullptr;
    vicon_lsl::gui::RecorderProcessController* recorder_process_ = nullptr;
    vicon_lsl::StreamDiscoveryWorker* discovery_worker_ = nullptr;
    vicon_lsl::gui::RecordingVerifier* verifier_ = nullptr;
    BridgeWorker* worker_ = nullptr;

    QTimer* labrecorder_retry_timer_ = nullptr;
    QTimer* filename_sync_timer_ = nullptr;
    QTimer* close_poll_timer_ = nullptr;
    QTimer* heartbeat_timer_ = nullptr;
    QTimer* verification_file_timer_ = nullptr;
    QElapsedTimer labrecorder_retry_elapsed_;
    QElapsedTimer monotonic_clock_;
    QElapsedTimer status_timer_;
    QElapsedTimer recording_elapsed_;
    QElapsedTimer verification_file_elapsed_;

    enum class SessionSequence { None, StartingSession, StoppingSession, Closing };
    bool startingSession() const { return sequence_ == SessionSequence::StartingSession; }
    bool stoppingSession() const { return sequence_ == SessionSequence::StoppingSession; }
    bool closing() const { return sequence_ == SessionSequence::Closing; }

    SessionSequence sequence_ = SessionSequence::None;
    bool stop_requested_ = false;
    vicon_lsl::gui::SessionFileState file_state_ =
        vicon_lsl::gui::SessionFileState::None;
    ComponentLifecycleState bridge_lifecycle_ = ComponentLifecycleState::Idle;
    bool have_previous_status_ = false;
    unsigned int previous_frames_ = 0;
    qint64 previous_status_ms_ = 0;
    double bridge_effective_rate_ = 0.0;
    unsigned long long preview_replaced_frames_ = 0;
    unsigned long long preview_coalesced_samples_ = 0;
    qint64 preview_latency_ms_ = 0;
    qint64 close_started_ms_ = -1;
    bool bridge_streaming_ = false;
    bool bridge_status_stale_ = false;
    bool startup_endpoint_probe_ = false;
    bool startup_launch_attempted_ = false;
    bool pending_recording_start_ = false;
    bool setup_check_start_waiting_ = false;
    bool verification_waiting_for_file_ = false;
    bool close_finalizing_ = false;
    bool owned_process_end_requested_ = false;
    bool recorder_connection_loss_reported_ = false;
    QString pending_recording_path_;
};
