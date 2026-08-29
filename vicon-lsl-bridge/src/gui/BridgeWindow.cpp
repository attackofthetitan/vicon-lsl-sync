#include "BridgeWindow.h"

#include "gui/BridgeWindowUi.h"
#include "gui/LabRecorderClient.h"
#include "gui/LabRecorderRuntimePolicy.h"
#include "gui/PerformanceBudgets.h"
#include "gui/PreviewPanel.h"
#include "gui/RecorderProcessController.h"
#include "gui/StreamDiscoveryWorker.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <exception>
#include <utility>

namespace {

using vicon_lsl::gui::GuiServices;
using vicon_lsl::gui::RecorderProcessKind;
using vicon_lsl::gui::SessionCalibrationState;
using vicon_lsl::gui::SessionFileState;
using vicon_lsl::gui::SessionWorkflowState;
using vicon_lsl::gui::StreamBinding;
using vicon_lsl::gui::StreamIdentity;
using vicon_lsl::gui::StreamReconnectionMode;

constexpr int kFilenameSyncDelayMs = 300;
constexpr int kStatusStaleMs = 3000;
constexpr int kVerificationFileTimeoutMs = 15000;

GuiServices normalizedServices(GuiServices services) {
    GuiServices defaults = vicon_lsl::gui::defaultGuiServices();
    if (!services.settings) services.settings = std::move(defaults.settings);
    if (!services.create_preview_worker) {
        services.create_preview_worker = std::move(defaults.create_preview_worker);
    }
    return services;
}
QString preflightLevelText(PreflightLevel level) {
    switch (level) {
        case PreflightLevel::Required: return "Required";
        case PreflightLevel::Warning: return "Warning";
        case PreflightLevel::Information: return "Information";
    }
    return "Information";
}

QString resultText(const PreflightItem& item) {
    if (item.passed) return "Pass";
    return item.level == PreflightLevel::Information ? "Note" : "Action needed";
}

QString formatDuration(qint64 milliseconds) {
    const qint64 total_seconds = (std::max)(qint64{0}, milliseconds / 1000);
    const qint64 hours = total_seconds / 3600;
    const qint64 minutes = (total_seconds % 3600) / 60;
    const qint64 seconds = total_seconds % 60;
    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString gibText(qint64 bytes) {
    if (bytes < 0) return "Storage: unknown";
    return "Storage: " + QString::number(
        static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GiB";
}

QJsonArray streamInventoryJson(const QVector<StreamIdentity>& streams) {
    QJsonArray result;
    for (const StreamIdentity& stream : streams) result.push_back(stream.toJson());
    return result;
}

bool identityMatchesBinding(const StreamIdentity& identity,
                            const StreamBinding& binding) {
    if (binding.reconnection == StreamReconnectionMode::SourceIdentity &&
        !binding.source_id.trimmed().isEmpty()) {
        return identity.source_id == binding.source_id;
    }
    return identity.name == binding.name;
}

QString stateDetail(RecorderConnectionState connection,
                    RecorderRecordingState recording,
                    RecorderOperationState operation) {
    return recorderConnectionStateText(connection) + " / " +
           recorderRecordingStateText(recording) + " / " +
           recorderOperationStateText(operation);
}

} // namespace

// --- BridgeWorker ---

BridgeWorker::BridgeWorker(const Config& config, QObject* parent)
    : QThread(parent), bridge_(std::make_unique<ViconLSLBridge>(config)) {}

void BridgeWorker::setLifecycleState(ComponentLifecycleState state,
                                     const QString& detail) {
    lifecycle_state_.store(state);
    emit lifecycleChanged(state, detail);
}

void BridgeWorker::run() {
    setLifecycleState(ComponentLifecycleState::Starting,
                      "Bridge worker started");
    try {
        bridge_->setStatusCallback([this](const BridgeStatus& status) {
            if (status.state == BridgeState::Streaming &&
                lifecycle_state_.load() != ComponentLifecycleState::Running) {
                setLifecycleState(ComponentLifecycleState::Running,
                                  "Vicon frames are streaming");
            }
            emit statusUpdate(static_cast<int>(status.state),
                              static_cast<unsigned long long>(status.marker_count),
                              static_cast<unsigned long long>(status.segment_count),
                              status.frame_count,
                              QString::fromStdString(status.message));
        });
        bridge_->run();
        setLifecycleState(ComponentLifecycleState::Stopped,
                          stop_requested_.load() ? "Bridge stopped on request"
                                                 : "Bridge run completed");
        emit terminal(BridgeExitResult::Stopped, {});
    } catch (const std::exception& ex) {
        const QString message = QString::fromUtf8(ex.what());
        setLifecycleState(ComponentLifecycleState::Failed, message);
        emit terminal(BridgeExitResult::Failed, message);
    } catch (...) {
        const QString message = "Unknown bridge worker failure";
        setLifecycleState(ComponentLifecycleState::Failed, message);
        emit terminal(BridgeExitResult::Failed, message);
    }
}

void BridgeWorker::stopBridge() {
    if (stop_requested_.exchange(true)) return;
    setLifecycleState(ComponentLifecycleState::Stopping,
                      "Bridge stop requested");
    bridge_->stop();
}

// --- BridgeWindow ---

BridgeWindow::BridgeWindow(QWidget* parent,
                           bool enable_preview,
                           GuiServices services)
    : QWidget(parent), services_(normalizedServices(std::move(services))) {
    monotonic_clock_.start();
    qRegisterMetaType<BridgeExitResult>("BridgeExitResult");
    qRegisterMetaType<vicon_lsl::gui::RecordingVerificationReport>(
        "vicon_lsl::gui::RecordingVerificationReport");
    ui_ = vicon_lsl::gui_detail::buildBridgeWindowUi(
        this, enable_preview, services_);
    labrecorder_client_ = new LabRecorderClient(this);
    recorder_process_ = new vicon_lsl::gui::RecorderProcessController(this);

    filename_sync_timer_ = new QTimer(this);
    filename_sync_timer_->setSingleShot(true);
    filename_sync_timer_->setInterval(kFilenameSyncDelayMs);
    labrecorder_retry_timer_ = new QTimer(this);
    labrecorder_retry_timer_->setInterval(250);
    close_poll_timer_ = new QTimer(this);
    close_poll_timer_->setInterval(50);
    status_stale_timer_ = new QTimer(this);
    status_stale_timer_->setInterval(500);
    dashboard_timer_ = new QTimer(this);
    dashboard_timer_->setInterval(250);
    verification_file_timer_ = new QTimer(this);
    verification_file_timer_->setInterval(100);

    connectSignals();
    loadSettings();
    status_timer_.start();
    status_stale_timer_->start();
    dashboard_timer_->start();
    validateRecordingPath();
    updateRecordingButtons();
    updateReadiness();
    updateDashboard();
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Application interface initialized");

    // The endpoint probe is always first. Automatic launch is considered only
    // after the probe has produced a definite failure.
    QTimer::singleShot(0, this, &BridgeWindow::beginLabRecorderStartup);
}

BridgeWindow::~BridgeWindow() {
    if (!close_pending_) saveSettings();
}

void BridgeWindow::connectSignals() {
    connect(ui_->start_button, &QPushButton::clicked,
            this, &BridgeWindow::onStart);
    connect(ui_->stop_button, &QPushButton::clicked,
            this, &BridgeWindow::onStop);
    connect(ui_->start_session_button, &QPushButton::clicked,
            this, &BridgeWindow::onStartSession);
    connect(ui_->stop_session_button, &QPushButton::clicked,
            this, &BridgeWindow::onStopSession);
    connect(ui_->emergency_stop_button, &QPushButton::clicked,
            this, &BridgeWindow::onEmergencyStop);
    connect(ui_->run_preflight_button, &QPushButton::clicked,
            this, &BridgeWindow::onRunPreflight);
    connect(ui_->preflight_override_button, &QPushButton::clicked,
            this, &BridgeWindow::onOverridePreflight);
    connect(ui_->browse_root_button, &QPushButton::clicked,
            this, &BridgeWindow::onBrowseStudyRoot);
    connect(ui_->browse_labrecorder_button, &QPushButton::clicked,
            this, &BridgeWindow::onBrowseLabRecorder);
    connect(ui_->launch_labrecorder_button, &QPushButton::clicked,
            this, &BridgeWindow::onLaunchLabRecorder);
    connect(ui_->connect_labrecorder_button, &QPushButton::clicked,
            this, &BridgeWindow::onConnectLabRecorder);
    connect(ui_->detach_labrecorder_button, &QPushButton::clicked,
            this, &BridgeWindow::onDetachLabRecorder);
    connect(ui_->refresh_streams_button, &QPushButton::clicked,
            this, &BridgeWindow::onRefreshLabRecorder);
    connect(ui_->start_recording_button, &QPushButton::clicked,
            this, &BridgeWindow::onStartRecording);
    connect(ui_->stop_recording_button, &QPushButton::clicked,
            this, &BridgeWindow::onStopRecording);
    connect(ui_->discover_streams_button, &QPushButton::clicked,
            this, &BridgeWindow::onDiscoverStreams);
    connect(ui_->find_next_run_button, &QPushButton::clicked,
            this, &BridgeWindow::onFindNextRun);
    connect(ui_->reset_configuration_button, &QPushButton::clicked,
            this, &BridgeWindow::onResetConfiguration);
    connect(ui_->save_preset_button, &QPushButton::clicked,
            this, &BridgeWindow::onSavePreset);
    connect(ui_->load_preset_button, &QPushButton::clicked,
            this, &BridgeWindow::onLoadPreset);
    connect(ui_->import_configuration_button, &QPushButton::clicked,
            this, &BridgeWindow::onImportConfiguration);
    connect(ui_->export_configuration_button, &QPushButton::clicked,
            this, &BridgeWindow::onExportConfiguration);
    connect(ui_->copy_diagnostics_button, &QPushButton::clicked,
            this, &BridgeWindow::onCopyDiagnostics);
    connect(ui_->export_diagnostics_button, &QPushButton::clicked,
            this, &BridgeWindow::onExportDiagnostics);
    connect(ui_->verification_details_button, &QPushButton::clicked,
            this, &BridgeWindow::onShowVerificationDetails);
    connect(ui_->acknowledge_error_button, &QPushButton::clicked, this, [this]() {
        session_controller_.eventLog().acknowledgeLastError();
        updateEventLog();
        updateDashboard();
    });
    connect(ui_->open_verified_recording_button, &QPushButton::clicked, this, [this]() {
        if (ui_->preview_panel && !verification_report_.path.isEmpty()) {
            ui_->preview_panel->openRecording(verification_report_.path);
        }
    });

    connect(filename_sync_timer_, &QTimer::timeout,
            this, &BridgeWindow::syncFilenameToLabRecorder);
    connect(labrecorder_retry_timer_, &QTimer::timeout,
            this, &BridgeWindow::onLabRecorderRetry);
    connect(close_poll_timer_, &QTimer::timeout,
            this, &BridgeWindow::onClosePoll);
    connect(status_stale_timer_, &QTimer::timeout,
            this, &BridgeWindow::onStatusStaleCheck);
    connect(dashboard_timer_, &QTimer::timeout,
            this, &BridgeWindow::onDashboardTick);
    connect(verification_file_timer_, &QTimer::timeout,
            this, &BridgeWindow::onVerificationFilePoll);

    const auto path_changed = [this]() {
        updateConfigurationFromUi();
        validateRecordingPath();
        scheduleFilenameSync();
    };
    connect(ui_->study_root_edit, &QLineEdit::textChanged, this, path_changed);
    connect(ui_->filename_template_edit, &QLineEdit::textChanged, this, path_changed);
    connect(ui_->participant_edit, &QLineEdit::textChanged, this, path_changed);
    connect(ui_->session_edit, &QLineEdit::textChanged, this, path_changed);
    connect(ui_->task_edit, &QLineEdit::textChanged, this, path_changed);
    connect(ui_->run_spin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, path_changed);
    connect(ui_->acquisition_edit, &QLineEdit::textChanged, this, path_changed);
    connect(ui_->modality_edit, &QLineEdit::textChanged, this, path_changed);
    connect(ui_->storage_warning_spin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, path_changed);
    connect(ui_->allow_overwrite_check, &QCheckBox::toggled, this, path_changed);
    connect(ui_->allow_outside_root_check, &QCheckBox::toggled, this, path_changed);
    connect(ui_->automatic_run_increment_check, &QCheckBox::toggled,
            this, path_changed);

    const auto bridge_names_changed = [this]() {
        updateConfigurationFromUi();
        if (ui_->preview_panel) ui_->preview_panel->applySessionConfiguration(configuration_);
        populateBindingCombos();
        updateReadiness();
    };
    connect(ui_->server_edit, &QLineEdit::textChanged,
            this, bridge_names_changed);
    connect(ui_->marker_stream_edit, &QLineEdit::textChanged,
            this, bridge_names_changed);
    connect(ui_->segment_stream_edit, &QLineEdit::textChanged,
            this, bridge_names_changed);
    connect(ui_->preview_external_streams_check, &QCheckBox::toggled,
            this, bridge_names_changed);

    const auto recorder_settings_changed = [this]() {
        updateConfigurationFromUi();
        updateRecordingButtons();
        updateDashboard();
    };
    connect(ui_->labrecorder_host_edit, &QLineEdit::textChanged,
            this, recorder_settings_changed);
    connect(ui_->labrecorder_port_spin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, recorder_settings_changed);
    connect(ui_->labrecorder_executable_edit, &QLineEdit::textChanged,
            this, recorder_settings_changed);
    connect(ui_->automatic_launch_check, &QCheckBox::toggled,
            this, recorder_settings_changed);
    connect(ui_->record_every_visible_check, &QCheckBox::toggled,
            this, recorder_settings_changed);
    connect(ui_->recorder_only_check, &QCheckBox::toggled,
            this, recorder_settings_changed);

    connect(ui_->stream_table, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* item) {
                if (!item || item->row() < 0 || item->row() >= stream_inventory_.size()) return;
                if (item->column() == 0) {
                    stream_inventory_[item->row()].selected =
                        item->checkState() == Qt::Checked;
                } else if (item->column() == 1) {
                    stream_inventory_[item->row()].required =
                        item->checkState() == Qt::Checked;
                } else {
                    return;
                }
                updateConfigurationFromUi();
                updateReadiness();
            });
    const auto binding_changed = [this]() {
        updateBindingsFromUi();
        if (ui_->preview_panel) ui_->preview_panel->applySessionConfiguration(configuration_);
    };
    QComboBox* const binding_combos[] = {
        ui_->marker_binding_combo, ui_->segment_binding_combo,
        ui_->gaze_binding_combo, ui_->calibration_binding_combo,
    };
    for (QComboBox* combo : binding_combos) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, binding_changed);
    }
    QCheckBox* const follow_checks[] = {
        ui_->marker_follow_name_check, ui_->segment_follow_name_check,
        ui_->gaze_follow_name_check, ui_->calibration_follow_name_check,
    };
    for (QCheckBox* check : follow_checks) {
        connect(check, &QCheckBox::toggled, this, binding_changed);
    }
    connect(ui_->event_severity_filter,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { updateEventLog(); });
    connect(ui_->event_component_filter,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { updateEventLog(); });

    connect(labrecorder_client_, &LabRecorderClient::connectionStateChanged,
            this, [this](RecorderConnectionState state, const QString& message) {
                dashboard_.recorder_connection = state;
                if (!message.isEmpty()) {
                    setLabRecorderStatus(message,
                        state == RecorderConnectionState::Error
                            ? EventSeverity::Error : EventSeverity::Information);
                }
                if (state == RecorderConnectionState::Connected) {
                    startup_endpoint_probe_ = false;
                    labrecorder_retry_timer_->stop();
                    appendEvent(SessionComponent::Recorder, EventSeverity::Information,
                                "Connected to configured recorder endpoint");
                    if (guided_start_pending_) advanceGuidedStart();
                } else if (startup_endpoint_probe_ &&
                           state != RecorderConnectionState::Connecting) {
                    startup_endpoint_probe_ = false;
                    if (configuration_.recorder_automatic_launch) {
                        launchConfiguredRecorder();
                    }
                }
                if (close_pending_ &&
                    (state == RecorderConnectionState::Disconnected ||
                     state == RecorderConnectionState::Error) &&
                    !labrecorder_client_->shutdownSettledSafely()) {
                    appendEvent(SessionComponent::Recorder, EventSeverity::Error,
                                "Recorder connection was lost while shutdown work remained");
                }
                updateRecordingButtons();
                updateReadiness();
                updateDashboard();
                scheduleFilenameSync();
                if (close_pending_) updateShutdownStatus();
            });
    connect(labrecorder_client_, &LabRecorderClient::recordingStateChanged,
            this, [this](RecorderRecordingState state) {
                dashboard_.recorder_recording = state;
                if (state == RecorderRecordingState::Recording) {
                    dashboard_.workflow = SessionWorkflowState::Recording;
                    dashboard_.recording_started_at = QDateTime::currentDateTimeUtc();
                    recording_elapsed_.restart();
                    appendEvent(SessionComponent::Recorder, EventSeverity::Information,
                                "Recorder acknowledged Recording");
                } else if (state == RecorderRecordingState::Stopped &&
                           dashboard_.workflow == SessionWorkflowState::Stopping) {
                    requestVerification();
                }
                updateRecordingButtons();
                updateReadiness();
                updateDashboard();
                if (guided_stop_pending_) advanceGuidedStop();
                if (close_pending_) updateShutdownStatus();
            });
    connect(labrecorder_client_, &LabRecorderClient::operationStateChanged,
            this, [this](RecorderOperationState state) {
                dashboard_.recorder_operation = state;
                ui_->labrecorder_operation_label->setText(
                    recorderOperationStateText(state));
                updateRecordingButtons();
                updateDashboard();
                if (close_pending_) updateShutdownStatus();
            });
    connect(labrecorder_client_, &LabRecorderClient::commandProgress,
            this, [this](const QString& operation, int number, int count,
                         const QString& command) {
                ui_->labrecorder_operation_progress->setRange(0, (std::max)(1, count));
                ui_->labrecorder_operation_progress->setValue((std::max)(0, number - 1));
                ui_->labrecorder_operation_progress->setFormat(
                    QString("%1 %2/%3").arg(operation).arg(number).arg(count));
                ui_->labrecorder_operation_label->setText(
                    operation + ": awaiting acknowledgement for " + command);
            });
    connect(labrecorder_client_, &LabRecorderClient::commandFinished,
            this, [this](const QString& operation, bool ok, const QString& message) {
                ui_->labrecorder_operation_progress->setValue(
                    ui_->labrecorder_operation_progress->maximum());
                setLabRecorderStatus(
                    ok ? operation + " completed"
                       : operation + " failed: " + message,
                    ok ? EventSeverity::Information : EventSeverity::Error);
                if (operation.contains("stop recording", Qt::CaseInsensitive) && ok) {
                    requestVerification();
                }
                if (operation.contains("start recording", Qt::CaseInsensitive) && !ok) {
                    dashboard_.workflow = SessionWorkflowState::Failed;
                }
                updateRecordingButtons();
                updateReadiness();
                updateDashboard();
                if (guided_stop_pending_) advanceGuidedStop();
                if (close_pending_) updateShutdownStatus();
            });

    connect(recorder_process_, &vicon_lsl::gui::RecorderProcessController::stateChanged,
            this, [this](RecorderProcessState state, const QString& detail) {
                dashboard_.recorder_process = state;
                appendEvent(SessionComponent::Recorder,
                            state == RecorderProcessState::LaunchFailed
                                ? EventSeverity::Error : EventSeverity::Information,
                            detail);
                if (state == RecorderProcessState::OwnedRunning &&
                    recorder_process_->kind() == RecorderProcessKind::GraphicalRecorder) {
                    labrecorder_retry_elapsed_.restart();
                    labrecorder_retry_timer_->start();
                    onLabRecorderRetry();
                }
                updateDashboard();
                updateRecordingButtons();
                if (guided_stop_pending_) advanceGuidedStop();
                if (close_pending_) updateShutdownStatus();
            });
    connect(recorder_process_,
            &vicon_lsl::gui::RecorderProcessController::recordingStateChanged,
            this, [this](RecorderRecordingState state) {
                dashboard_.recorder_recording = state;
                if (state == RecorderRecordingState::Recording) {
                    dashboard_.workflow = SessionWorkflowState::Recording;
                    dashboard_.recording_started_at = QDateTime::currentDateTimeUtc();
                    recording_elapsed_.restart();
                    appendEvent(SessionComponent::Recorder, EventSeverity::Information,
                                "Allowlist recorder is recording");
                } else if (state == RecorderRecordingState::Stopped) {
                    requestVerification();
                }
                updateDashboard();
                updateRecordingButtons();
                if (guided_stop_pending_) advanceGuidedStop();
                if (close_pending_) updateShutdownStatus();
            });
    connect(recorder_process_, &vicon_lsl::gui::RecorderProcessController::outputLine,
            this, [this](EventSeverity severity, const QString& line) {
                appendEvent(SessionComponent::Recorder, severity,
                            "Recorder process: " + line);
            });
    connect(recorder_process_, &vicon_lsl::gui::RecorderProcessController::processExited,
            this, [this](int exit_code, bool expected, RecorderProcessKind kind) {
                if (kind == RecorderProcessKind::AllowlistRecorder && !expected) {
                    appendEvent(SessionComponent::Recorder, EventSeverity::Error,
                                "Allowlist recorder exited unexpectedly with code " +
                                    QString::number(exit_code));
                    dashboard_.workflow = SessionWorkflowState::Failed;
                }
                if (close_pending_) updateShutdownStatus();
            });

    if (ui_->preview_panel) {
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::lifecycleChanged,
                this, [this](ComponentLifecycleState state, const QString& detail) {
                    dashboard_.preview = state;
                    appendEvent(SessionComponent::Preview,
                                state == ComponentLifecycleState::Failed
                                    ? EventSeverity::Error : EventSeverity::Information,
                                detail);
                    updateDashboard();
                    if (guided_start_pending_) advanceGuidedStart();
                    if (guided_stop_pending_) advanceGuidedStop();
                    if (close_pending_) updateShutdownStatus();
                });
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::streamInventoryChanged,
                this, [this](const QVector<StreamIdentity>& streams) {
                    for (const StreamIdentity& stream : streams) {
                        auto found = std::find_if(stream_inventory_.begin(),
                            stream_inventory_.end(), [&stream](const StreamIdentity& existing) {
                                return existing.stableKey() == stream.stableKey();
                            });
                        if (found != stream_inventory_.end()) {
                            const bool selected = found->selected;
                            const bool required = found->required;
                            *found = stream;
                            found->selected = selected;
                            found->required = required;
                        } else {
                            stream_inventory_.push_back(stream);
                        }
                    }
                    populateStreamTable();
                });
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::calibrationStateChanged,
                this, [this](SessionCalibrationState state, const QString& quality,
                             bool) {
                    dashboard_.calibration = state;
                    ui_->calibration_state_label->setToolTip(quality);
                    updateDashboard();
                });
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::fileStateChanged,
                this, [this](SessionFileState state, const QString& detail) {
                    dashboard_.file = state;
                    appendEvent(SessionComponent::File,
                                state == SessionFileState::Failed
                                    ? EventSeverity::Error : EventSeverity::Information,
                                detail);
                    updateDashboard();
                    if (close_pending_) updateShutdownStatus();
                });
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::deliveryMetricsChanged,
                this, [this](const vicon_lsl::PreviewDeliveryMetrics& metrics) {
                    dashboard_.preview_replaced_frames =
                        metrics.replaced_before_display;
                    dashboard_.preview_coalesced_input_samples =
                        metrics.coalesced_input_samples;
                    dashboard_.preview_latency_ms =
                        metrics.display_latency_ms;
                    updateDashboard();
                });
    }
}

bool BridgeWindow::labRecorderConnected() const {
    return labrecorder_client_ &&
           labrecorder_client_->connectionState() == RecorderConnectionState::Connected;
}

bool BridgeWindow::labRecorderOwnedProcessRunning() const {
    return recorder_process_ && recorder_process_->ownsRunningProcess();
}

bool BridgeWindow::stairModelLoaded() const {
    return ui_->preview_panel && ui_->preview_panel->stairModelLoaded();
}

bool BridgeWindow::configurableTooltipsPresent() const {
    return ui_->configurableTooltipsPresent();
}

bool BridgeWindow::accessibilityContractSatisfied() const {
    return ui_->accessibilityContractSatisfied();
}

SessionWorkflowState BridgeWindow::workflowState() const {
    return dashboard_.workflow;
}

ComponentLifecycleState BridgeWindow::bridgeLifecycleState() const {
    return bridge_lifecycle_;
}

void BridgeWindow::loadSettings() {
    configuration_ = services_.settings->loadConfiguration();
    ui_state_ = services_.settings->loadUiState();
    applyConfigurationToUi();
    restoreUiState();
    refreshPresetList();
}

void BridgeWindow::saveSettings() {
    updateConfigurationFromUi();
    services_.settings->saveConfiguration(configuration_);
    saveUiState();
}

void BridgeWindow::applyConfigurationToUi() {
    const QSignalBlocker server_blocker(ui_->server_edit);
    const QSignalBlocker marker_blocker(ui_->marker_stream_edit);
    const QSignalBlocker segment_blocker(ui_->segment_stream_edit);
    const QSignalBlocker root_blocker(ui_->study_root_edit);
    const QSignalBlocker template_blocker(ui_->filename_template_edit);
    const QSignalBlocker participant_blocker(ui_->participant_edit);
    const QSignalBlocker session_blocker(ui_->session_edit);
    const QSignalBlocker task_blocker(ui_->task_edit);
    const QSignalBlocker run_blocker(ui_->run_spin);
    const QSignalBlocker acquisition_blocker(ui_->acquisition_edit);
    const QSignalBlocker modality_blocker(ui_->modality_edit);
    const QSignalBlocker recorder_host_blocker(ui_->labrecorder_host_edit);
    const QSignalBlocker recorder_port_blocker(ui_->labrecorder_port_spin);
    const QSignalBlocker recorder_executable_blocker(ui_->labrecorder_executable_edit);
    const QSignalBlocker external_blocker(ui_->preview_external_streams_check);

    ui_->server_edit->setText(configuration_.vicon_endpoint);
    ui_->marker_stream_edit->setText(configuration_.marker_output_name);
    ui_->segment_stream_edit->setText(configuration_.segment_output_name);
    ui_->study_root_edit->setText(configuration_.recording_root);
    ui_->filename_template_edit->setText(configuration_.recording_template);
    ui_->participant_edit->setText(configuration_.participant);
    ui_->session_edit->setText(configuration_.session);
    ui_->task_edit->setText(configuration_.task);
    ui_->run_spin->setValue(configuration_.run);
    ui_->acquisition_edit->setText(configuration_.acquisition);
    ui_->modality_edit->setText(configuration_.modality);
    ui_->storage_warning_spin->setValue(configuration_.storage_warning_gib);
    ui_->allow_overwrite_check->setChecked(configuration_.allow_overwrite);
    ui_->allow_outside_root_check->setChecked(
        configuration_.allow_outside_study_root);
    ui_->automatic_run_increment_check->setChecked(
        configuration_.automatic_run_increment);
    ui_->labrecorder_executable_edit->setText(configuration_.recorder_executable);
    ui_->labrecorder_host_edit->setText(configuration_.recorder_host);
    ui_->labrecorder_port_spin->setValue(configuration_.recorder_port);
    ui_->automatic_launch_check->setChecked(configuration_.recorder_automatic_launch);
    ui_->record_every_visible_check->setChecked(
        configuration_.record_every_visible_stream);
    ui_->recorder_only_check->setChecked(configuration_.recorder_only_mode);
    ui_->preview_external_streams_check->setChecked(
        configuration_.preview_external_streams);
    ui_->marker_follow_name_check->setChecked(
        configuration_.preview_markers.reconnection ==
        StreamReconnectionMode::FollowName);
    ui_->segment_follow_name_check->setChecked(
        configuration_.preview_segments.reconnection ==
        StreamReconnectionMode::FollowName);
    ui_->gaze_follow_name_check->setChecked(
        configuration_.preview_gaze.reconnection ==
        StreamReconnectionMode::FollowName);
    ui_->calibration_follow_name_check->setChecked(
        configuration_.preview_calibration.reconnection ==
        StreamReconnectionMode::FollowName);
    ui_->marker_binding_combo->setEnabled(configuration_.preview_external_streams);
    ui_->segment_binding_combo->setEnabled(configuration_.preview_external_streams);
    if (ui_->preview_panel) {
        ui_->preview_panel->applySessionConfiguration(configuration_);
        dashboard_.preview =
            ui_->preview_panel->lifecycleState();
        dashboard_.calibration =
            ui_->preview_panel->sessionCalibrationState();
    }
    populateBindingCombos();
    validateRecordingPath();
    updateDashboard();
}

void BridgeWindow::updateConfigurationFromUi() {
    configuration_.vicon_endpoint = ui_->server_edit->text().trimmed();
    configuration_.marker_output_name = ui_->marker_stream_edit->text().trimmed();
    configuration_.segment_output_name = ui_->segment_stream_edit->text().trimmed();
    configuration_.preview_external_streams =
        ui_->preview_external_streams_check->isChecked();
    if (ui_->preview_panel) {
        ui_->preview_panel->updateSessionConfiguration(configuration_);
    }
    if (!configuration_.preview_external_streams) configuration_.bindPreviewOutputs();
    configuration_.recorder_host = ui_->labrecorder_host_edit->text().trimmed();
    configuration_.recorder_port = ui_->labrecorder_port_spin->value();
    configuration_.recorder_executable =
        ui_->labrecorder_executable_edit->text().trimmed();
    configuration_.recorder_automatic_launch =
        ui_->automatic_launch_check->isChecked();
    configuration_.record_every_visible_stream =
        ui_->record_every_visible_check->isChecked();
    configuration_.recording_root = ui_->study_root_edit->text().trimmed();
    configuration_.recording_template = ui_->filename_template_edit->text();
    configuration_.participant = ui_->participant_edit->text();
    configuration_.session = ui_->session_edit->text();
    configuration_.task = ui_->task_edit->text();
    configuration_.run = ui_->run_spin->value();
    configuration_.acquisition = ui_->acquisition_edit->text();
    configuration_.modality = ui_->modality_edit->text();
    configuration_.storage_warning_gib = ui_->storage_warning_spin->value();
    configuration_.automatic_run_increment =
        ui_->automatic_run_increment_check->isChecked();
    configuration_.allow_overwrite = ui_->allow_overwrite_check->isChecked();
    configuration_.allow_outside_study_root =
        ui_->allow_outside_root_check->isChecked();
    configuration_.recorder_only_mode = ui_->recorder_only_check->isChecked();

    if (!stream_inventory_.isEmpty()) {
        QVector<StreamBinding> bindings;
        for (const StreamIdentity& identity : stream_inventory_) {
            if (!identity.selected && !identity.required) continue;
            StreamBinding binding;
            binding.role = identity.role;
            binding.name = identity.name;
            binding.source_id = identity.source_id;
            binding.reconnection = identity.source_id.isEmpty()
                ? StreamReconnectionMode::FollowName
                : StreamReconnectionMode::SourceIdentity;
            binding.required = identity.required;
            binding.expected_channels = identity.channel_count;
            binding.expected_nominal_rate = identity.nominal_rate;
            binding.expected_coordinate_frame = identity.coordinate_frame;
            bindings.push_back(binding);
        }
        configuration_.recording_streams = std::move(bindings);
    }
}

void BridgeWindow::restoreUiState() {
    if (!ui_state_.geometry.isEmpty()) restoreGeometry(ui_state_.geometry);
    if (!ui_state_.splitter_state.isEmpty()) {
        ui_->main_splitter->restoreState(ui_state_.splitter_state);
    }
    ui_->controls_tabs->setCurrentIndex(
        (std::max)(0, (std::min)(ui_state_.active_control_tab,
                                 ui_->controls_tabs->count() - 1)));
}

void BridgeWindow::saveUiState() {
    ui_state_.geometry = saveGeometry();
    ui_state_.splitter_state = ui_->main_splitter->saveState();
    ui_state_.active_control_tab = ui_->controls_tabs->currentIndex();
    services_.settings->saveUiState(ui_state_);
}

void BridgeWindow::refreshPresetList(const QString& select) {
    const QString current = select.isEmpty()
        ? ui_->preset_combo->currentText() : select;
    const QSignalBlocker blocker(ui_->preset_combo);
    ui_->preset_combo->clear();
    ui_->preset_combo->addItems(services_.settings->presetNames());
    ui_->preset_combo->setEditText(current);
}

void BridgeWindow::onResetConfiguration() {
    if (recordingActiveOrPending()) {
        appendEvent(SessionComponent::Application, EventSeverity::Warning,
                    "Configuration reset rejected while recording work is active");
        return;
    }
    configuration_ = vicon_lsl::gui::SessionConfiguration();
    stream_inventory_.clear();
    applyConfigurationToUi();
    populateStreamTable();
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Session configuration reset to defaults");
}

void BridgeWindow::onSavePreset() {
    updateConfigurationFromUi();
    const QString name = ui_->preset_combo->currentText().trimmed();
    QString error;
    if (services_.settings->savePreset(name, configuration_, &error)) {
        refreshPresetList(name);
        appendEvent(SessionComponent::Application, EventSeverity::Information,
                    "Saved session preset " + name);
    } else {
        appendEvent(SessionComponent::Application, EventSeverity::Error,
                    "Could not save preset: " + error);
    }
}

void BridgeWindow::onLoadPreset() {
    if (recordingActiveOrPending()) {
        appendEvent(SessionComponent::Application, EventSeverity::Warning,
                    "Preset load rejected while recording work is active");
        return;
    }
    const QString name = ui_->preset_combo->currentText().trimmed();
    vicon_lsl::gui::SessionConfiguration loaded;
    QString error;
    if (services_.settings->loadPreset(name, loaded, &error)) {
        configuration_ = std::move(loaded);
        stream_inventory_.clear();
        applyConfigurationToUi();
        populateStreamTable();
        appendEvent(SessionComponent::Application, EventSeverity::Information,
                    "Loaded session preset " + name);
    } else {
        appendEvent(SessionComponent::Application, EventSeverity::Error,
                    "Could not load preset: " + error);
    }
}

void BridgeWindow::onImportConfiguration() {
    if (recordingActiveOrPending()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Import Session Configuration",
        ui_state_.recent_preset_directory, "Session configuration (*.json)");
    if (path.isEmpty()) return;
    vicon_lsl::gui::SessionConfiguration loaded;
    QString error;
    if (!vicon_lsl::gui::SessionConfigurationStore::importConfiguration(
            path, loaded, &error)) {
        appendEvent(SessionComponent::Application, EventSeverity::Error,
                    "Configuration import failed: " + error);
        return;
    }
    configuration_ = std::move(loaded);
    ui_state_.recent_preset_directory = QFileInfo(path).absolutePath();
    stream_inventory_.clear();
    applyConfigurationToUi();
    populateStreamTable();
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Imported session configuration " + path);
}

void BridgeWindow::onExportConfiguration() {
    updateConfigurationFromUi();
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Session Configuration",
        QDir(ui_state_.recent_preset_directory).filePath("session-configuration.json"),
        "Session configuration (*.json)");
    if (path.isEmpty()) return;
    QString error;
    if (!vicon_lsl::gui::SessionConfigurationStore::exportConfiguration(
            path, configuration_, &error)) {
        appendEvent(SessionComponent::Application, EventSeverity::Error,
                    "Configuration export failed: " + error);
        return;
    }
    ui_state_.recent_preset_directory = QFileInfo(path).absolutePath();
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Exported session configuration " + path);
}

void BridgeWindow::onBrowseStudyRoot() {
    const QString root = QFileDialog::getExistingDirectory(
        this, "Select Study Root", ui_->study_root_edit->text());
    if (!root.isEmpty()) ui_->study_root_edit->setText(QDir::toNativeSeparators(root));
}

void BridgeWindow::onBrowseLabRecorder() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Select Recorder Executable",
        ui_->labrecorder_executable_edit->text(), "Executable (*.exe);;All files (*)");
    if (!path.isEmpty()) {
        ui_->labrecorder_executable_edit->setText(
            QDir::toNativeSeparators(path));
    }
}

LabRecorderFilenameFields BridgeWindow::filenameFields() const {
    return ui_->filenameFields();
}

RecordingPathValidationOptions BridgeWindow::pathValidationOptions(
    bool create_parent) const {
    RecordingPathValidationOptions options;
    options.allow_outside_study_root =
        ui_->allow_outside_root_check->isChecked();
    options.allow_overwrite = ui_->allow_overwrite_check->isChecked();
    options.create_parent_directories = create_parent;
    options.verify_write_access = true;
    options.storage_warning_bytes = static_cast<qint64>(
        ui_->storage_warning_spin->value() * 1024.0 * 1024.0 * 1024.0);
    return options;
}

void BridgeWindow::validateRecordingPath(bool create_parent) {
    path_result_ = LabRecorderFilenamePolicy::validate(
        filenameFields(), pathValidationOptions(create_parent));
    ui_->filename_preview_label->setText(path_result_.absolute_path);
    ui_->filename_preview_label->setToolTip(path_result_.absolute_path);
    ui_->filename_preview_label->setCursorPosition(0);
    ui_->path_validation_label->setText(path_result_.summary());
    ui_->path_validation_label->setToolTip(path_result_.summary());
    dashboard_.recording_path = path_result_.absolute_path;
    dashboard_.available_storage_bytes =
        path_result_.available_storage_bytes;
    updateRecordingButtons();
    updateReadiness();
    updateDashboard();
}

void BridgeWindow::updateFilenamePreview() {
    validateRecordingPath();
}

void BridgeWindow::onFindNextRun() {
    const int next = LabRecorderFilenamePolicy::findNextRun(
        filenameFields(), ui_->run_spin->value(), pathValidationOptions(false));
    if (next > ui_->run_spin->value()) {
        ui_->run_spin->setValue(next);
        appendEvent(SessionComponent::Path, EventSeverity::Information,
                    "Selected next unused run " + QString::number(next));
    } else {
        appendEvent(SessionComponent::Path, EventSeverity::Warning,
                    "No unused run was found within the supported range");
    }
}

void BridgeWindow::appendEvent(SessionComponent component,
                               EventSeverity severity,
                               const QString& message) {
    if (message.trimmed().isEmpty()) return;
    session_controller_.eventLog().append(component, severity, message);
    updateEventLog();
    if (severity == EventSeverity::Error) {
        ui_->last_error_label->setText(
            session_controller_.eventLog().lastError());
    }
}

void BridgeWindow::updateEventLog() {
    if (!ui_ || !ui_->event_log) return;
    EventSeverity minimum = EventSeverity::Information;
    if (ui_->event_severity_filter->currentIndex() == 1) {
        minimum = EventSeverity::Warning;
    } else if (ui_->event_severity_filter->currentIndex() >= 2) {
        minimum = EventSeverity::Error;
    }
    QVector<SessionComponent> components;
    const int component_index = ui_->event_component_filter->currentIndex();
    if (component_index > 0) {
        components.push_back(static_cast<SessionComponent>(component_index - 1));
    }
    const QScrollBar* scroll = ui_->event_log->verticalScrollBar();
    const bool at_end = scroll->value() >= scroll->maximum();
    ui_->event_log->setPlainText(
        session_controller_.eventLog().toText(minimum, components));
    if (at_end) ui_->event_log->verticalScrollBar()->setValue(
        ui_->event_log->verticalScrollBar()->maximum());
    const QString last_error = session_controller_.eventLog().lastError();
    ui_->last_error_label->setText(last_error.isEmpty() ? "-" : last_error);
}

void BridgeWindow::setLabRecorderStatus(const QString& status,
                                        EventSeverity severity) {
    ui_->labrecorder_status_label->setText(status);
    appendEvent(SessionComponent::Recorder, severity, status);
}

void BridgeWindow::setInputsEnabled(bool enabled) {
    ui_->setBridgeInputsEnabled(enabled);
}

void BridgeWindow::updateDashboard() {
    auto& dashboard = dashboard_;
    dashboard.bridge = bridge_lifecycle_;
    if (ui_->preview_panel) {
        dashboard.preview = ui_->preview_panel->lifecycleState();
        dashboard.calibration = ui_->preview_panel->sessionCalibrationState();
    }
    dashboard.recorder_connection = labrecorder_client_->connectionState();
    dashboard.recorder_recording = effectiveRecordingState();
    dashboard.recorder_operation = effectiveOperationState();
    dashboard.recorder_process = recorder_process_->state();
    dashboard.recording_path = path_result_.absolute_path;
    dashboard.run_identifier = QString("run %1").arg(ui_->run_spin->value());
    dashboard.available_storage_bytes = path_result_.available_storage_bytes;

    ui_->workflow_state_label->setText(
        vicon_lsl::gui::SessionController::workflowStateText(dashboard.workflow));
    const RecorderRecordingState recording = effectiveRecordingState();
    const RecorderOperationState operation = effectiveOperationState();
    if (recording == RecorderRecordingState::Recording) {
        ui_->recording_indicator_label->setText("RECORDING");
    } else if (operation == RecorderOperationState::Starting) {
        ui_->recording_indicator_label->setText("STARTING");
    } else if (operation == RecorderOperationState::Stopping ||
               dashboard.workflow == SessionWorkflowState::Stopping) {
        ui_->recording_indicator_label->setText("STOPPING");
    } else {
        ui_->recording_indicator_label->setText("NOT RECORDING");
    }
    ui_->recording_elapsed_label->setText(
        recording_elapsed_.isValid() &&
                (recording == RecorderRecordingState::Recording ||
                 dashboard.workflow == SessionWorkflowState::Stopping ||
                 dashboard.workflow == SessionWorkflowState::Verifying)
            ? formatDuration(recording_elapsed_.elapsed()) : "00:00:00");
    ui_->recording_path_label->setText(
        path_result_.absolute_path.isEmpty()
            ? "No validated destination" : path_result_.absolute_path);
    ui_->recording_path_label->setToolTip(path_result_.summary());
    ui_->run_identifier_label->setText(dashboard.run_identifier);
    ui_->bridge_state_label->setText(componentLifecycleStateText(bridge_lifecycle_));
    ui_->recorder_state_label->setText(stateDetail(
        labrecorder_client_->connectionState(), recording, operation));
    ui_->preview_state_label->setText(
        componentLifecycleStateText(dashboard.preview));
    ui_->calibration_state_label->setText(
        vicon_lsl::gui::SessionController::calibrationStateText(
            dashboard.calibration));
    ui_->file_state_dashboard_label->setText(
        vicon_lsl::gui::SessionController::fileStateText(dashboard.file));
    ui_->verification_state_label->setText(
        verificationStateText(dashboard.verification));
    ui_->recorder_owner_label->setText(
        recorderProcessStateText(recorder_process_->state()));
    ui_->recorder_endpoint_label->setText(
        configuration_.recorder_host + ":" +
        QString::number(configuration_.recorder_port));
    ui_->storage_label->setText(gibText(path_result_.available_storage_bytes));
    ui_->drop_label->setText(
        "Display replacements " +
        QString::number(dashboard.preview_replaced_frames) +
        "; input backlog discarded " +
        QString::number(dashboard.preview_coalesced_input_samples) +
        "; latency " + QString::number(dashboard.preview_latency_ms) + " ms");
    ui_->verification_details_button->setEnabled(
        verification_report_.state != RecordingVerificationState::NotRun);
    ui_->open_verified_recording_button->setEnabled(
        !verification_report_.path.isEmpty() &&
        QFileInfo::exists(verification_report_.path));
    ui_->start_session_button->setEnabled(
        !close_pending_ && !guided_start_pending_ && !recordingActiveOrPending());
    ui_->stop_session_button->setEnabled(
        !close_pending_ &&
        (guided_start_pending_ || recordingActiveOrPending() ||
         worker_ || (ui_->preview_panel &&
                     !ui_->preview_panel->shutdownReady())));
    ui_->emergency_stop_button->setEnabled(
        !close_pending_ && recordingActiveOrPending());
}

void BridgeWindow::onDashboardTick() {
    updateDashboard();
    if (guided_start_pending_) advanceGuidedStart();
    if (guided_stop_pending_) advanceGuidedStop();
    if (close_pending_) updateShutdownStatus();
}

QString BridgeWindow::resolveLabRecorderExecutable() const {
    return LabRecorderRuntimePolicy::resolveExecutable(
        ui_->labrecorder_executable_edit->text(),
        QCoreApplication::applicationDirPath());
}

QString BridgeWindow::resolveAllowlistExecutable() const {
    return vicon_lsl::gui::RecorderProcessController::bundledAllowlistExecutable(
        resolveLabRecorderExecutable(), QCoreApplication::applicationDirPath());
}

void BridgeWindow::beginLabRecorderStartup() {
    if (close_pending_ || labrecorder_client_->connectionState() ==
                              RecorderConnectionState::Connecting) {
        return;
    }
    updateConfigurationFromUi();
    startup_endpoint_probe_ = true;
    startup_launch_attempted_ = false;
    setLabRecorderStatus("Probing configured recorder endpoint");
    labrecorder_client_->connectToServer(
        configuration_.recorder_host,
        static_cast<quint16>(configuration_.recorder_port), 500);
}

void BridgeWindow::launchConfiguredRecorder() {
    if (startup_launch_attempted_) return;
    startup_launch_attempted_ = true;
    const QString executable = resolveLabRecorderExecutable();
    if (executable.isEmpty()) {
        setLabRecorderStatus(
            "Configured endpoint is unavailable and no recorder executable was found",
            EventSeverity::Warning);
        return;
    }
    ui_->labrecorder_executable_edit->setText(executable);
    QString error;
    if (!recorder_process_->launchGraphicalRecorder(executable, &error)) {
        setLabRecorderStatus("Recorder launch failed: " + error,
                             EventSeverity::Error);
        return;
    }
    setLabRecorderStatus("Launching recorder asynchronously");
}

void BridgeWindow::onLaunchLabRecorder() {
    if (recordingActiveOrPending() ||
        labrecorder_client_->connectionState() ==
            RecorderConnectionState::Connected ||
        labrecorder_client_->connectionState() ==
            RecorderConnectionState::Connecting) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning,
                    "Recorder launch rejected because an endpoint or recording operation is already active");
        return;
    }
    updateConfigurationFromUi();
    startup_launch_attempted_ = false;
    launchConfiguredRecorder();
}

void BridgeWindow::onConnectLabRecorder() {
    const RecorderConnectionState state =
        labrecorder_client_->connectionState();
    if (recordingActiveOrPending() &&
        (state == RecorderConnectionState::Connected ||
         state == RecorderConnectionState::Connecting)) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning,
                    "Recorder connection replacement rejected while recording work is active");
        return;
    }
    labrecorder_retry_timer_->stop();
    startup_endpoint_probe_ = false;
    updateConfigurationFromUi();
    saveSettings();
    labrecorder_client_->connectToServer(
        configuration_.recorder_host,
        static_cast<quint16>(configuration_.recorder_port));
}

void BridgeWindow::onDetachLabRecorder() {
    if (recordingActiveOrPending()) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning,
                    "Disconnect or detach rejected while recording work is active");
        return;
    }
    labrecorder_retry_timer_->stop();
    labrecorder_client_->disconnectFromServer();
    if (recorder_process_->ownsRunningProcess()) {
        recorder_process_->detach();
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning,
                    "Owned recorder was deliberately detached and will remain running");
    } else {
        appendEvent(SessionComponent::Recorder, EventSeverity::Information,
                    "Disconnected from externally managed recorder");
    }
    updateRecordingButtons();
    updateDashboard();
}

void BridgeWindow::onLabRecorderRetry() {
    const RecorderConnectionState state = labrecorder_client_->connectionState();
    if (state == RecorderConnectionState::Connected) {
        labrecorder_retry_timer_->stop();
        return;
    }
    const qint64 elapsed = labrecorder_retry_elapsed_.isValid()
        ? labrecorder_retry_elapsed_.elapsed()
        : LabRecorderRuntimePolicy::RetryTimeoutMs;
    if (LabRecorderRuntimePolicy::retryExpired(elapsed)) {
        labrecorder_retry_timer_->stop();
        setLabRecorderStatus(
            "Recorder endpoint was not ready within the 15 second startup window",
            EventSeverity::Error);
        return;
    }
    if (!LabRecorderRuntimePolicy::shouldAttemptConnection(state, elapsed)) return;
    labrecorder_client_->connectToServer(
        ui_->labrecorder_host_edit->text().trimmed(),
        static_cast<quint16>(ui_->labrecorder_port_spin->value()), 200);
}

void BridgeWindow::onRefreshLabRecorder() {
    if (labrecorder_client_->refreshStreams()) {
        setLabRecorderStatus("Recorder stream refresh queued");
    } else {
        setLabRecorderStatus("Recorder stream refresh is unavailable in the current state",
                             EventSeverity::Warning);
    }
}

void BridgeWindow::scheduleFilenameSync() {
    if (!filename_sync_timer_ || close_pending_) return;
    if (configuration_.record_every_visible_stream &&
        path_result_.valid() &&
        LabRecorderRuntimePolicy::canStartRecording(
            labrecorder_client_->connectionState(),
            labrecorder_client_->recordingState(),
            labrecorder_client_->operationState(),
            labrecorder_client_->shutdownRequested())) {
        filename_sync_timer_->start();
    } else {
        filename_sync_timer_->stop();
    }
}

void BridgeWindow::syncFilenameToLabRecorder() {
    validateRecordingPath();
    if (!path_result_.valid() || !configuration_.record_every_visible_stream) return;
    if (labrecorder_client_->updateFilename(path_result_.normalized_fields)) {
        setLabRecorderStatus("Exact normalized filename update queued");
    }
}

BridgeWindow::RecorderBackend BridgeWindow::activeRecorderBackend() const {
    return configuration_.record_every_visible_stream
        ? RecorderBackend::RemoteControl : RecorderBackend::AllowlistProcess;
}

RecorderRecordingState BridgeWindow::effectiveRecordingState() const {
    if (recorder_process_ &&
        recorder_process_->kind() == RecorderProcessKind::AllowlistRecorder) {
        if (recorder_process_->allowlistRecording()) {
            return RecorderRecordingState::Recording;
        }
        if (recorder_process_->ownsRunningProcess()) {
            return dashboard_.recorder_recording;
        }
    }
    return labrecorder_client_ ? labrecorder_client_->recordingState()
                               : RecorderRecordingState::Unknown;
}

RecorderOperationState BridgeWindow::effectiveOperationState() const {
    if (activeRecorderBackend() == RecorderBackend::AllowlistProcess) {
        if (recorder_process_->state() == RecorderProcessState::Launching) {
            return RecorderOperationState::Starting;
        }
        if (dashboard_.workflow ==
            SessionWorkflowState::Stopping) {
            return RecorderOperationState::Stopping;
        }
        return close_pending_ ? RecorderOperationState::ShuttingDown
                              : RecorderOperationState::Idle;
    }
    return labrecorder_client_->operationState();
}

bool BridgeWindow::recordingActiveOrPending() const {
    const RecorderRecordingState recording = effectiveRecordingState();
    const RecorderOperationState operation = effectiveOperationState();
    return pending_recording_start_ ||
           recording == RecorderRecordingState::Recording ||
           operation == RecorderOperationState::Starting ||
           operation == RecorderOperationState::Stopping ||
           labrecorder_client_->startMayHaveReachedServer() ||
           labrecorder_client_->desiredRecordingState() ==
               RecorderRecordingState::Recording;
}

void BridgeWindow::updateRecordingButtons() {
    if (!ui_ || !labrecorder_client_) return;
    updateConfigurationFromUi();
    const bool remote = configuration_.record_every_visible_stream;
    ui_->refresh_streams_button->setEnabled(
        !close_pending_ && remote &&
        LabRecorderRuntimePolicy::canRefreshStreams(
            labrecorder_client_->connectionState(),
            labrecorder_client_->recordingState(),
            labrecorder_client_->operationState(),
            labrecorder_client_->shutdownRequested()));
    ui_->start_recording_button->setEnabled(
        !close_pending_ && !recordingActiveOrPending());
    ui_->stop_recording_button->setEnabled(
        !close_pending_ && recordingActiveOrPending());
    ui_->connect_labrecorder_button->setEnabled(
        !close_pending_ &&
        labrecorder_client_->connectionState() != RecorderConnectionState::Connecting &&
        (!recordingActiveOrPending() ||
         labrecorder_client_->connectionState() == RecorderConnectionState::Disconnected ||
         labrecorder_client_->connectionState() == RecorderConnectionState::Error));
    ui_->detach_labrecorder_button->setEnabled(
        !close_pending_ && !recordingActiveOrPending() &&
        (labrecorder_client_->connectionState() != RecorderConnectionState::Disconnected ||
         recorder_process_->ownsRunningProcess()));
    ui_->launch_labrecorder_button->setEnabled(
        !close_pending_ && !recordingActiveOrPending() &&
        !recorder_process_->ownsRunningProcess() &&
        labrecorder_client_->connectionState() != RecorderConnectionState::Connected &&
        labrecorder_client_->connectionState() != RecorderConnectionState::Connecting);
}

void BridgeWindow::updateReadiness() {
    if (!ui_ || !ui_->readiness_label) return;
    const QString bridge_text = configuration_.recorder_only_mode
        ? "bridge not required"
        : (bridge_streaming_ && !bridge_status_stale_
            ? "bridge streaming at " + ui_->frame_rate_label->text()
            : "bridge unavailable or stale");
    const QString recorder_text = configuration_.record_every_visible_stream
        ? recorderConnectionStateText(labrecorder_client_->connectionState())
        : (resolveAllowlistExecutable().isEmpty()
            ? "allowlist recorder missing" : "allowlist recorder available");
    const QString path_text = path_result_.valid()
        ? "destination valid" : "destination blocked";
    ui_->readiness_label->setText(
        QString("Readiness: %1; recorder %2; %3; %4 inventoried stream(s).")
            .arg(bridge_text, recorder_text, path_text)
            .arg(stream_inventory_.size()));
}

void BridgeWindow::onStart() {
    if (worker_ || close_pending_) return;
    updateConfigurationFromUi();
    saveSettings();
    Config config;
    config.vicon_server = configuration_.vicon_endpoint.toStdString();
    config.marker_stream_name = configuration_.marker_output_name.toStdString();
    config.segment_stream_name = configuration_.segment_output_name.toStdString();
    worker_ = new BridgeWorker(config, this);
    bridge_lifecycle_ = ComponentLifecycleState::Starting;
    dashboard_.bridge = bridge_lifecycle_;
    ui_->start_button->setEnabled(false);
    ui_->stop_button->setEnabled(true);
    setInputsEnabled(false);
    appendEvent(SessionComponent::Bridge, EventSeverity::Information,
                "Bridge start requested for " + configuration_.vicon_endpoint);
    connect(worker_, &BridgeWorker::statusUpdate,
            this, &BridgeWindow::onStatusUpdate);
    connect(worker_, &BridgeWorker::lifecycleChanged,
            this, [this](ComponentLifecycleState state, const QString& detail) {
                bridge_lifecycle_ = state;
                dashboard_.bridge = state;
                appendEvent(SessionComponent::Bridge,
                            state == ComponentLifecycleState::Failed
                                ? EventSeverity::Error : EventSeverity::Information,
                            detail);
                updateDashboard();
                if (guided_start_pending_) advanceGuidedStart();
                if (guided_stop_pending_) advanceGuidedStop();
                if (close_pending_) updateShutdownStatus();
            });
    connect(worker_, &BridgeWorker::terminal,
            this, [this](BridgeExitResult result, const QString& message) {
                if (result == BridgeExitResult::Failed) {
                    dashboard_.workflow =
                        SessionWorkflowState::Failed;
                    appendEvent(SessionComponent::Bridge, EventSeverity::Error,
                                message);
                }
            });
    connect(worker_, &QThread::finished,
            this, &BridgeWindow::onWorkerFinished);
    worker_->start();
    updateDashboard();
}

void BridgeWindow::onStop() {
    if (!worker_) return;
    ui_->stop_button->setEnabled(false);
    bridge_lifecycle_ = ComponentLifecycleState::Stopping;
    worker_->stopBridge();
    appendEvent(SessionComponent::Bridge, EventSeverity::Information,
                "Bridge stop requested");
    updateDashboard();
}

void BridgeWindow::onStatusUpdate(int state,
                                  unsigned long long markers,
                                  unsigned long long segments,
                                  unsigned int frames,
                                  const QString& message) {
    const auto bridge_state = static_cast<BridgeState>(state);
    QString state_text;
    switch (bridge_state) {
        case BridgeState::Disconnected: state_text = "Disconnected"; break;
        case BridgeState::Connecting: state_text = "Connecting"; break;
        case BridgeState::Streaming: state_text = "Streaming"; break;
        case BridgeState::Stopped: state_text = "Stopped"; break;
    }
    if (!message.isEmpty()) state_text += " - " + message;
    ui_->status_label->setText(state_text);
    bridge_streaming_ = bridge_state == BridgeState::Streaming;
    bridge_status_stale_ = false;
    if (bridge_streaming_) bridge_lifecycle_ = ComponentLifecycleState::Running;
    ui_->markers_label->setText(QString::number(markers));
    ui_->segments_label->setText(QString::number(segments));
    ui_->frames_label->setText(QString::number(frames));
    const qint64 now_ms = status_timer_.elapsed();
    if (have_previous_status_) {
        const qint64 delta_ms = now_ms - previous_status_ms_;
        if (delta_ms > 0) {
            const unsigned int delta =
                frames >= previous_frames_ ? frames - previous_frames_ : 0;
            bridge_effective_rate_ =
                static_cast<double>(delta) * 1000.0 /
                static_cast<double>(delta_ms);
            ui_->frame_rate_label->setText(
                QString::number(bridge_effective_rate_, 'f', 1) + " Hz");
        }
    }
    previous_status_ms_ = now_ms;
    previous_frames_ = frames;
    have_previous_status_ = true;
    updateReadiness();
    updateDashboard();
    if (guided_start_pending_) advanceGuidedStart();
}

bool BridgeWindow::bridgeStatusRecent() const {
    return bridge_streaming_ && have_previous_status_ &&
           !bridge_status_stale_ &&
           status_timer_.elapsed() - previous_status_ms_ <= kStatusStaleMs;
}

void BridgeWindow::onStatusStaleCheck() {
    if (!bridge_streaming_ || !have_previous_status_) return;
    if (status_timer_.elapsed() - previous_status_ms_ <= kStatusStaleMs ||
        bridge_status_stale_) {
        return;
    }
    bridge_status_stale_ = true;
    bridge_effective_rate_ = 0.0;
    ui_->frame_rate_label->setText("0.0 Hz");
    appendEvent(SessionComponent::Bridge, EventSeverity::Warning,
                "Bridge status became stale after three seconds without an update");
    updateReadiness();
    updateDashboard();
}

void BridgeWindow::onWorkerFinished() {
    BridgeWorker* completed = worker_;
    worker_ = nullptr;
    bridge_streaming_ = false;
    bridge_status_stale_ = false;
    have_previous_status_ = false;
    bridge_effective_rate_ = 0.0;
    if (bridge_lifecycle_ != ComponentLifecycleState::Failed) {
        bridge_lifecycle_ = ComponentLifecycleState::Stopped;
    }
    ui_->frame_rate_label->setText("0.0 Hz");
    ui_->start_button->setEnabled(!close_pending_);
    ui_->stop_button->setEnabled(false);
    setInputsEnabled(!close_pending_);
    if (completed) completed->deleteLater();
    updateReadiness();
    updateDashboard();
    if (guided_start_pending_) advanceGuidedStart();
    if (guided_stop_pending_) advanceGuidedStop();
    if (close_pending_) updateShutdownStatus();
}

void BridgeWindow::onDiscoverStreams() {
    startStreamDiscovery(false);
}

void BridgeWindow::startStreamDiscovery(bool continue_recording_start) {
    if (discovery_worker_) {
        if (continue_recording_start) pending_recording_start_ = true;
        appendEvent(SessionComponent::Streams, EventSeverity::Information,
                    "Stream discovery is already in progress");
        return;
    }
    updateConfigurationFromUi();
    if (ui_->preview_panel) {
        const QVector<StreamIdentity> preview_streams =
            ui_->preview_panel->streamInventory();
        for (const StreamIdentity& preview_stream : preview_streams) {
            auto existing = std::find_if(
                stream_inventory_.begin(), stream_inventory_.end(),
                [&preview_stream](const StreamIdentity& stream) {
                    return stream.stableKey() == preview_stream.stableKey();
                });
            if (existing == stream_inventory_.end()) {
                stream_inventory_.push_back(preview_stream);
                continue;
            }
            const bool selected = existing->selected;
            const bool required = existing->required;
            *existing = preview_stream;
            existing->selected = selected;
            existing->required = required;
        }
    }
    pending_recording_start_ =
        pending_recording_start_ || continue_recording_start;
    ui_->discover_streams_button->setEnabled(false);
    ui_->stream_discovery_status_label->setText("Discovering...");
    discovery_worker_ = new vicon_lsl::StreamDiscoveryWorker(configuration_, this);
    vicon_lsl::StreamDiscoveryWorker* started = discovery_worker_;
    connect(started, &vicon_lsl::StreamDiscoveryWorker::lifecycleChanged,
            this, [this](ComponentLifecycleState state, const QString& detail) {
                ui_->stream_discovery_status_label->setText(detail);
                if (state == ComponentLifecycleState::Failed) {
                    appendEvent(SessionComponent::Streams, EventSeverity::Error,
                                detail);
                }
            });
    connect(started, &vicon_lsl::StreamDiscoveryWorker::discoveryFinished,
            this, [this](QVector<StreamIdentity> discovered,
                         const QString& warning) {
                QVector<StreamIdentity> reconciled;
                reconciled.reserve(discovered.size() + stream_inventory_.size());
                for (StreamIdentity& identity : discovered) {
                    const auto old = std::find_if(
                        stream_inventory_.cbegin(), stream_inventory_.cend(),
                        [&identity](const StreamIdentity& candidate) {
                            return candidate.stableKey() == identity.stableKey();
                        });
                    if (old != stream_inventory_.cend()) {
                        identity.selected = old->selected;
                        identity.required = old->required;
                        identity.freshness_ms = old->freshness_ms;
                        identity.effective_rate = old->effective_rate;
                    } else {
                        const auto configured = std::find_if(
                            configuration_.recording_streams.cbegin(),
                            configuration_.recording_streams.cend(),
                            [&identity](const StreamBinding& binding) {
                                return identityMatchesBinding(identity, binding);
                            });
                        if (configured != configuration_.recording_streams.cend()) {
                            identity.selected = true;
                            identity.required = configured->required;
                        } else {
                            identity.selected =
                                configuration_.record_every_visible_stream;
                        }
                    }
                    reconciled.push_back(std::move(identity));
                }
                for (const StreamIdentity& old : stream_inventory_) {
                    if (!old.selected && !old.required) continue;
                    const bool present = std::any_of(
                        reconciled.cbegin(), reconciled.cend(),
                        [&old](const StreamIdentity& candidate) {
                            return candidate.stableKey() == old.stableKey();
                        });
                    if (!present) {
                        StreamIdentity missing = old;
                        missing.present = false;
                        missing.freshness_ms = -1;
                        missing.warning =
                            "Previously selected identity is not currently visible";
                        reconciled.push_back(std::move(missing));
                    }
                }
                stream_inventory_ = std::move(reconciled);
                populateStreamTable();
                populateBindingCombos();
                updateConfigurationFromUi();
                ui_->stream_discovery_status_label->setText(
                    QString("Discovered %1 visible stream(s)")
                        .arg(std::count_if(
                            stream_inventory_.cbegin(), stream_inventory_.cend(),
                            [](const StreamIdentity& stream) {
                                return stream.present;
                            })));
                appendEvent(SessionComponent::Streams,
                            warning.isEmpty() ? EventSeverity::Information
                                              : EventSeverity::Warning,
                            warning.isEmpty()
                                ? "Stream discovery completed"
                                : "Stream discovery completed: " + warning);
            });
    connect(started, &QThread::finished, this, [this, started]() {
        const bool was_pending = pending_recording_start_;
        if (discovery_worker_ == started) discovery_worker_ = nullptr;
        started->deleteLater();
        ui_->discover_streams_button->setEnabled(!close_pending_);
        if (was_pending && !close_pending_) completePendingRecordingStart();
        if (close_pending_) updateShutdownStatus();
    });
    appendEvent(SessionComponent::Streams, EventSeverity::Information,
                "Immediate pre-recording stream discovery started");
    started->start();
}

void BridgeWindow::cancelStreamDiscovery() {
    if (!discovery_worker_) return;
    discovery_worker_->requestInterruption();
    ui_->stream_discovery_status_label->setText("Canceling discovery...");
}

void BridgeWindow::populateStreamTable() {
    const QSignalBlocker blocker(ui_->stream_table);
    ui_->stream_table->setRowCount(stream_inventory_.size());
    for (int row = 0; row < stream_inventory_.size(); ++row) {
        const StreamIdentity& stream = stream_inventory_[row];
        auto* record = new QTableWidgetItem();
        record->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                         Qt::ItemIsUserCheckable);
        record->setCheckState(stream.selected ? Qt::Checked : Qt::Unchecked);
        auto* required = new QTableWidgetItem();
        required->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                           Qt::ItemIsUserCheckable);
        required->setCheckState(stream.required ? Qt::Checked : Qt::Unchecked);
        ui_->stream_table->setItem(row, 0, record);
        ui_->stream_table->setItem(row, 1, required);
        const QStringList values = {
            stream.role,
            stream.name,
            stream.type,
            stream.source_id.isEmpty() ? "<missing>" : stream.source_id,
            stream.hostname,
            stream.session_id,
            QString::number(stream.channel_count),
            stream.nominal_rate > 0.0
                ? QString::number(stream.nominal_rate, 'f', 1) : "irregular",
            stream.effective_rate > 0.0
                ? QString::number(stream.effective_rate, 'f', 1) : "not measured",
            stream.coordinate_frame.isEmpty() ? "<missing>" : stream.coordinate_frame,
            stream.present
                ? (stream.warning.isEmpty()
                    ? QString("%1 ms").arg(stream.freshness_ms)
                    : stream.warning)
                : "Missing: " + stream.warning,
        };
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            if (!stream.present || !stream.schema_compatible ||
                !stream.warning.isEmpty()) {
                item->setToolTip(stream.warning);
            }
            ui_->stream_table->setItem(row, column + 2, item);
        }
    }
    updateReadiness();
}

void BridgeWindow::selectBindingCombo(QComboBox* combo,
                                      const StreamBinding& binding) {
    if (!combo) return;
    int selected = -1;
    for (int index = 0; index < combo->count(); ++index) {
        const QString source_id = combo->itemData(index, Qt::UserRole).toString();
        const QString name = combo->itemData(index, Qt::UserRole + 1).toString();
        if ((!binding.source_id.isEmpty() && source_id == binding.source_id) ||
            (binding.source_id.isEmpty() && name == binding.name)) {
            selected = index;
            break;
        }
    }
    if (selected < 0) {
        combo->addItem(
            "Configured: " + binding.name +
                (binding.source_id.isEmpty()
                    ? QString() : " [" + binding.source_id + "]"),
            binding.source_id);
        const int index = combo->count() - 1;
        combo->setItemData(index, binding.name, Qt::UserRole + 1);
        selected = index;
    }
    combo->setCurrentIndex(selected);
}

void BridgeWindow::populateBindingCombos() {
    struct RoleUi {
        QString role;
        QComboBox* combo;
        const StreamBinding* binding;
    };
    const RoleUi roles[] = {
        {"markers", ui_->marker_binding_combo, &configuration_.preview_markers},
        {"segments", ui_->segment_binding_combo, &configuration_.preview_segments},
        {"gaze", ui_->gaze_binding_combo, &configuration_.preview_gaze},
        {"calibration", ui_->calibration_binding_combo,
         &configuration_.preview_calibration},
    };
    for (const RoleUi& role : roles) {
        const QSignalBlocker blocker(role.combo);
        role.combo->clear();
        for (const StreamIdentity& stream : stream_inventory_) {
            if (!stream.present || stream.role != role.role) continue;
            role.combo->addItem(stream.displayText(), stream.source_id);
            const int index = role.combo->count() - 1;
            role.combo->setItemData(index, stream.name, Qt::UserRole + 1);
            role.combo->setItemData(index, stream.stableKey(), Qt::UserRole + 2);
            role.combo->setItemData(index, stream.warning, Qt::ToolTipRole);
        }
        selectBindingCombo(role.combo, *role.binding);
    }
    ui_->marker_binding_combo->setEnabled(configuration_.preview_external_streams);
    ui_->segment_binding_combo->setEnabled(configuration_.preview_external_streams);
}

void BridgeWindow::updateBindingsFromUi() {
    auto update = [this](QComboBox* combo, QCheckBox* follow,
                         StreamBinding& binding) {
        if (!combo || combo->currentIndex() < 0) return;
        const QString key = combo->itemData(
            combo->currentIndex(), Qt::UserRole + 2).toString();
        const auto found = std::find_if(
            stream_inventory_.cbegin(), stream_inventory_.cend(),
            [&key](const StreamIdentity& identity) {
                return identity.stableKey() == key;
            });
        if (found != stream_inventory_.cend()) {
            binding.name = found->name;
            binding.source_id = found->source_id;
            binding.expected_channels = found->channel_count;
            binding.expected_nominal_rate = found->nominal_rate;
            binding.expected_coordinate_frame = found->coordinate_frame;
        } else {
            binding.name = combo->itemData(
                combo->currentIndex(), Qt::UserRole + 1).toString();
            binding.source_id = combo->itemData(
                combo->currentIndex(), Qt::UserRole).toString();
        }
        binding.reconnection = follow->isChecked()
            ? StreamReconnectionMode::FollowName
            : StreamReconnectionMode::SourceIdentity;
    };
    update(ui_->marker_binding_combo, ui_->marker_follow_name_check,
           configuration_.preview_markers);
    update(ui_->segment_binding_combo, ui_->segment_follow_name_check,
           configuration_.preview_segments);
    update(ui_->gaze_binding_combo, ui_->gaze_follow_name_check,
           configuration_.preview_gaze);
    update(ui_->calibration_binding_combo,
           ui_->calibration_follow_name_check,
           configuration_.preview_calibration);
    configuration_.preview_external_streams =
        ui_->preview_external_streams_check->isChecked();
    if (!configuration_.preview_external_streams) configuration_.bindPreviewOutputs();
}

QVector<StreamIdentity> BridgeWindow::selectedStreams() const {
    QVector<StreamIdentity> result;
    for (StreamIdentity stream : stream_inventory_) {
        if (!stream.present) continue;
        if (configuration_.record_every_visible_stream) stream.selected = true;
        if (stream.selected) result.push_back(std::move(stream));
    }
    return result;
}

vicon_lsl::gui::SessionPreflightInputs BridgeWindow::preflightInputs() const {
    vicon_lsl::gui::SessionPreflightInputs inputs;
    inputs.configuration = configuration_;
    inputs.bridge_state = bridge_lifecycle_;
    inputs.bridge_status_recent = bridgeStatusRecent();
    inputs.bridge_effective_rate = bridge_effective_rate_;
    inputs.recorder_connection = labrecorder_client_->connectionState();
    inputs.recorder_recording = effectiveRecordingState();
    inputs.recorder_operation = effectiveOperationState();
    inputs.allowlist_recorder_available = !resolveAllowlistExecutable().isEmpty();
    inputs.path = path_result_;
    inputs.streams = stream_inventory_;
    if (ui_->preview_panel) {
        inputs.stair_model_loaded = ui_->preview_panel->stairModelLoaded();
        inputs.calibration_state =
            ui_->preview_panel->sessionCalibrationState();
        inputs.calibration_metadata_compatible =
            ui_->preview_panel->calibrationMetadataCompatible();
    }
    return inputs;
}

void BridgeWindow::populatePreflight(const PreflightResult& result) {
    ui_->preflight_tree->clear();
    for (const PreflightItem& item : result.items) {
        QString details = item.message;
        if (!item.passed && !item.corrective_action.isEmpty()) {
            details += " — " + item.corrective_action;
        }
        auto* row = new QTreeWidgetItem({
            preflightLevelText(item.level),
            SessionEventLog::componentText(item.component),
            resultText(item),
            details,
        });
        row->setToolTip(3, details);
        ui_->preflight_tree->addTopLevelItem(row);
    }
    ui_->preflight_override_button->setEnabled(
        result.hasRequiredFailures() && !result.override_used);
    updateDashboard();
}

void BridgeWindow::onRunPreflight() {
    updateConfigurationFromUi();
    validateRecordingPath(false);
    const PreflightResult result =
        session_controller_.runPreflight(preflightInputs());
    dashboard_.workflow = result.hasRequiredFailures()
        ? SessionWorkflowState::PreflightBlocked
        : SessionWorkflowState::Ready;
    populatePreflight(result);
    appendEvent(SessionComponent::Application,
                result.hasRequiredFailures()
                    ? EventSeverity::Warning : EventSeverity::Information,
                result.summary());
}

void BridgeWindow::onStartRecording() {
    if (pending_recording_start_ || recordingActiveOrPending() || close_pending_) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning,
                    "Duplicate recording Start was rejected");
        return;
    }
    saveSettings();
    filename_sync_timer_->stop();
    pending_recording_start_ = true;
    preflight_start_waiting_ = false;
    dashboard_.workflow = SessionWorkflowState::Preparing;
    startStreamDiscovery(true);
    updateRecordingButtons();
    updateDashboard();
}

void BridgeWindow::completePendingRecordingStart() {
    if (!pending_recording_start_ || close_pending_) return;
    updateConfigurationFromUi();
    validateRecordingPath(true);
    const PreflightResult result =
        session_controller_.runPreflight(preflightInputs());
    dashboard_.workflow = result.hasRequiredFailures()
        ? SessionWorkflowState::PreflightBlocked
        : SessionWorkflowState::Ready;
    populatePreflight(result);
    if (result.hasRequiredFailures()) {
        preflight_start_waiting_ = true;
        pending_recording_start_ = false;
        appendEvent(SessionComponent::Application, EventSeverity::Warning,
                    "Recording remains blocked until preflight is corrected or a reasoned override is accepted");
        updateRecordingButtons();
        return;
    }
    beginRecordingAfterPreflight();
}

void BridgeWindow::onOverridePreflight() {
    const QString reason =
        ui_->preflight_override_reason_edit->text().trimmed();
    if (!session_controller_.overridePreflight(reason)) {
        appendEvent(SessionComponent::Application, EventSeverity::Warning,
                    "Record Anyway requires blocked preflight checks and a non-empty reason");
        return;
    }
    dashboard_.workflow = SessionWorkflowState::Ready;
    populatePreflight(session_controller_.lastPreflight());
    if (preflight_start_waiting_) {
        preflight_start_waiting_ = false;
        pending_recording_start_ = true;
        beginRecordingAfterPreflight();
    }
}

void BridgeWindow::beginRecordingAfterPreflight() {
    if (!pending_recording_start_ || close_pending_) return;
    validateRecordingPath(true);
    if (!path_result_.valid()) {
        pending_recording_start_ = false;
        dashboard_.workflow =
            SessionWorkflowState::PreflightBlocked;
        appendEvent(SessionComponent::Path, EventSeverity::Error,
                    path_result_.firstError());
        return;
    }
    recording_inventory_ = selectedStreams();
    pending_recording_path_ = path_result_.absolute_path;
    dashboard_.selected_streams = recording_inventory_;
    dashboard_.recording_path = pending_recording_path_;
    dashboard_.verification =
        RecordingVerificationState::NotRun;
    verification_report_ = {};
    dashboard_.workflow = SessionWorkflowState::Starting;

    bool accepted = false;
    if (activeRecorderBackend() == RecorderBackend::RemoteControl) {
        accepted = labrecorder_client_->startRecording(
            path_result_.normalized_fields, true);
    } else {
        QString error;
        const QString executable = resolveAllowlistExecutable();
        accepted = recorder_process_->launchAllowlistRecorder(
            executable, path_result_.absolute_path,
            recording_inventory_, &error);
        if (!accepted) {
            appendEvent(SessionComponent::Recorder, EventSeverity::Error,
                        "Allowlist recorder could not start: " + error);
        }
    }
    pending_recording_start_ = false;
    if (accepted) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Information,
                    "Recording Start accepted for exact destination " +
                        pending_recording_path_ + " with " +
                        QString::number(recording_inventory_.size()) +
                        " inventoried stream(s)");
    } else {
        dashboard_.workflow = SessionWorkflowState::Failed;
        appendEvent(SessionComponent::Recorder, EventSeverity::Error,
                    "Recording Start was rejected by the active recorder backend");
    }
    updateRecordingButtons();
    updateDashboard();
}

void BridgeWindow::onStopRecording() {
    if (!recordingActiveOrPending()) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning,
                    "Duplicate or inapplicable recording Stop was rejected");
        return;
    }
    pending_recording_start_ = false;
    preflight_start_waiting_ = false;
    dashboard_.workflow = SessionWorkflowState::Stopping;
    bool accepted = false;
    if (recorder_process_->kind() == RecorderProcessKind::AllowlistRecorder &&
        recorder_process_->ownsRunningProcess()) {
        accepted = recorder_process_->stopAllowlistRecording();
    } else {
        accepted = labrecorder_client_->stopRecording();
    }
    appendEvent(SessionComponent::Recorder,
                accepted ? EventSeverity::Information : EventSeverity::Warning,
                accepted ? "One recording Stop operation was requested"
                         : "Recording Stop was already active or could not be queued");
    updateRecordingButtons();
    updateDashboard();
}

void BridgeWindow::onEmergencyStop() {
    appendEvent(SessionComponent::Recorder, EventSeverity::Warning,
                "Emergency recording Stop requested");
    onStopRecording();
}

void BridgeWindow::onStartSession() {
    if (guided_start_pending_ || recordingActiveOrPending() || close_pending_) return;
    saveSettings();
    guided_stop_pending_ = false;
    guided_start_pending_ = true;
    dashboard_.workflow = SessionWorkflowState::Preparing;
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                configuration_.recorder_only_mode
                    ? "Guided recorder-only session start requested"
                    : "Guided bridge, preview, and recording start requested");
    advanceGuidedStart();
}

void BridgeWindow::advanceGuidedStart() {
    if (!guided_start_pending_ || close_pending_) return;
    if (!configuration_.recorder_only_mode) {
        if (bridge_lifecycle_ == ComponentLifecycleState::Failed) {
            guided_start_pending_ = false;
            dashboard_.workflow = SessionWorkflowState::Failed;
            appendEvent(SessionComponent::Bridge, EventSeverity::Error,
                        "Guided session stopped because the bridge failed");
            return;
        }
        if (bridge_lifecycle_ != ComponentLifecycleState::Running) {
            if (!worker_) onStart();
            return;
        }
    }
    if (ui_->preview_panel) {
        const ComponentLifecycleState preview =
            ui_->preview_panel->lifecycleState();
        if (preview == ComponentLifecycleState::Failed) {
            guided_start_pending_ = false;
            dashboard_.workflow = SessionWorkflowState::Failed;
            appendEvent(SessionComponent::Preview, EventSeverity::Error,
                        "Guided session stopped because preview startup failed; direct components remain controllable");
            return;
        }
        if (preview != ComponentLifecycleState::Running) {
            if (preview != ComponentLifecycleState::Starting) {
                ui_->preview_panel->startPreview();
            }
            return;
        }
    }
    guided_start_pending_ = false;
    onStartRecording();
}

void BridgeWindow::onStopSession() {
    if (guided_stop_pending_ || close_pending_) return;
    guided_start_pending_ = false;
    guided_stop_pending_ = true;
    dashboard_.workflow = SessionWorkflowState::Stopping;
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Guided reverse-order session stop requested");
    advanceGuidedStop();
}

void BridgeWindow::advanceGuidedStop() {
    if (!guided_stop_pending_ || close_pending_) return;
    if (recordingActiveOrPending()) {
        if (effectiveOperationState() != RecorderOperationState::Stopping) {
            onStopRecording();
        }
        return;
    }
    if (verification_waiting_for_file_ || verifier_) return;
    if (ui_->preview_panel && !ui_->preview_panel->shutdownReady()) {
        ui_->preview_panel->requestShutdown();
        return;
    }
    if (worker_) {
        onStop();
        return;
    }
    if (recorder_process_->ownsRunningProcess() &&
        recorder_process_->kind() == RecorderProcessKind::GraphicalRecorder) {
        recorder_process_->endOwnedProcess();
        return;
    }
    guided_stop_pending_ = false;
    dashboard_.workflow =
        verification_report_.state == RecordingVerificationState::NeedsAttention
            ? SessionWorkflowState::Failed : SessionWorkflowState::Complete;
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Guided session stop completed");
    updateDashboard();
}

void BridgeWindow::requestVerification() {
    if (pending_recording_path_.isEmpty() || verifier_ ||
        verification_waiting_for_file_) {
        return;
    }
    dashboard_.workflow = SessionWorkflowState::Verifying;
    dashboard_.verification =
        RecordingVerificationState::Running;
    verification_waiting_for_file_ = true;
    verification_file_elapsed_.restart();
    verification_file_timer_->start();
    appendEvent(SessionComponent::Verification, EventSeverity::Information,
                "Waiting for the recorder to finalize the exact output before verification");
    onVerificationFilePoll();
}

void BridgeWindow::onVerificationFilePoll() {
    if (!verification_waiting_for_file_) {
        verification_file_timer_->stop();
        return;
    }
    const QFileInfo output(pending_recording_path_);
    if (output.exists() && output.isFile() && output.size() > 0) {
        verification_waiting_for_file_ = false;
        verification_file_timer_->stop();
        startVerifier();
        return;
    }
    if (verification_file_elapsed_.elapsed() < kVerificationFileTimeoutMs) return;
    verification_waiting_for_file_ = false;
    verification_file_timer_->stop();
    vicon_lsl::gui::RecordingVerificationReport report;
    report.path = pending_recording_path_;
    report.started_at = QDateTime::currentDateTimeUtc();
    report.completed_at = report.started_at;
    report.state = RecordingVerificationState::NeedsAttention;
    report.findings.push_back({
        EventSeverity::Error,
        "output-not-found",
        {},
        "The exact expected output did not appear within 15 seconds after Stop",
        "Keep the recorder open, confirm its status, and inspect the destination without overwriting it.",
    });
    finishVerification(report);
}

void BridgeWindow::startVerifier() {
    if (verifier_ || pending_recording_path_.isEmpty()) return;
    vicon_lsl::gui::RecordingVerificationRequest request;
    request.path = pending_recording_path_;
    request.preflight_inventory = recording_inventory_;
    request.expected_streams = configuration_.recording_streams;
    request.record_every_visible_stream =
        configuration_.record_every_visible_stream;
    verifier_ = new vicon_lsl::gui::RecordingVerifier(
        std::move(request), this);
    auto* started = verifier_;
    connect(started, &vicon_lsl::gui::RecordingVerifier::progressChanged,
            this, [this](const QString& stage, int percent,
                         const QString& detail) {
                ui_->verification_state_label->setText(
                    QString("%1 %2% — %3").arg(stage).arg(percent).arg(detail));
            });
    connect(started, &vicon_lsl::gui::RecordingVerifier::lifecycleChanged,
            this, [this](ComponentLifecycleState state, const QString& detail) {
                if (state == ComponentLifecycleState::Failed) {
                    appendEvent(SessionComponent::Verification,
                                EventSeverity::Error, detail);
                }
                if (close_pending_) updateShutdownStatus();
            });
    connect(started, &vicon_lsl::gui::RecordingVerifier::verificationFinished,
            this, &BridgeWindow::finishVerification);
    connect(started, &QThread::finished, this, [this, started]() {
        if (verifier_ == started) verifier_ = nullptr;
        started->deleteLater();
        if (guided_stop_pending_) advanceGuidedStop();
        if (close_pending_) updateShutdownStatus();
    });
    appendEvent(SessionComponent::Verification, EventSeverity::Information,
                "Background recording verification started");
    started->start();
}

void BridgeWindow::finishVerification(
    const vicon_lsl::gui::RecordingVerificationReport& report) {
    verification_report_ = report;
    dashboard_.verification = report.state;
    dashboard_.workflow =
        report.state == RecordingVerificationState::NeedsAttention
            ? SessionWorkflowState::Failed : SessionWorkflowState::Complete;
    appendEvent(SessionComponent::Verification,
                report.hasErrors() ? EventSeverity::Error
                    : (report.hasWarnings() ? EventSeverity::Warning
                                            : EventSeverity::Information),
                report.summary());
    const bool output_exists = QFileInfo::exists(report.path);
    const bool policy_passed =
        !configuration_.increment_run_after_verified_only ||
        report.state == RecordingVerificationState::Verified ||
        report.state == RecordingVerificationState::VerifiedWithWarnings;
    if (configuration_.automatic_run_increment &&
        output_exists && policy_passed) {
        ui_->run_spin->setValue(ui_->run_spin->value() + 1);
        appendEvent(SessionComponent::Path, EventSeverity::Information,
                    "Run incremented after completed recording verification");
    }
    updateDashboard();
    if (guided_stop_pending_) advanceGuidedStop();
}

QJsonObject BridgeWindow::diagnosticBundle() const {
    const qint64 now_ms = monotonic_clock_.msecsSinceReference();
    QJsonObject performance{
        {"maximumGuiThreadStallMs",
         vicon_lsl::gui::PerformanceBudgets::MaximumGuiThreadStallMs},
        {"bridgeStopDeadlineMs",
         vicon_lsl::gui::PerformanceBudgets::BridgeStopDeadlineMs},
        {"previewStopDeadlineMs",
         vicon_lsl::gui::PerformanceBudgets::PreviewStopDeadlineMs},
        {"recorderStopDeadlineMs",
         vicon_lsl::gui::PerformanceBudgets::RecorderStopDeadlineMs},
        {"fileCancelLatencyMs",
         vicon_lsl::gui::PerformanceBudgets::FileCancelLatencyMs},
        {"fileCancelSampleInterval",
         static_cast<double>(
             vicon_lsl::gui::PerformanceBudgets::FileCancelSampleInterval)},
        {"maximumLivePreviewLatencyMs",
         vicon_lsl::gui::PerformanceBudgets::MaximumLivePreviewLatencyMs},
        {"maximumQueuedLiveFrames",
         static_cast<int>(vicon_lsl::gui::PerformanceBudgets::MaximumQueuedLiveFrames)},
        {"maximumEventLogEntries",
         static_cast<int>(vicon_lsl::gui::PerformanceBudgets::MaximumEventLogEntries)},
        {"maximumProcessOutputBytes",
         static_cast<int>(vicon_lsl::gui::PerformanceBudgets::MaximumProcessOutputBytes)},
        {"maximumPreviewFrames",
         static_cast<double>(vicon_lsl::gui::PerformanceBudgets::MaximumPreviewFrames)},
        {"configuredPlaybackCacheMegabytes",
         configuration_.preview_cache_megabytes},
    };
    QJsonObject rates{
        {"bridgeEffectiveHz", bridge_effective_rate_},
        {"previewReplacedFrames",
         static_cast<double>(
             dashboard_.preview_replaced_frames)},
        {"previewCoalescedInputSamples",
         static_cast<double>(
             dashboard_.preview_coalesced_input_samples)},
        {"previewLatencyMs",
         static_cast<double>(
             dashboard_.preview_latency_ms)},
    };
    return {
        {"formatVersion", 1},
        {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"applicationVersion", QCoreApplication::applicationVersion()},
        {"configuration", configuration_.toJson()},
        {"session", session_controller_.toJson(dashboard_, now_ms)},
        {"visibleStreamInventory", streamInventoryJson(stream_inventory_)},
        {"recordingStreamInventory", streamInventoryJson(recording_inventory_)},
        {"recordingPath", path_result_.absolute_path},
        {"recordingPathSummary", path_result_.summary()},
        {"verification", verification_report_.toJson()},
        {"recorderProcessState",
         recorderProcessStateText(recorder_process_->state())},
        {"recorderProcessOutput",
         QString::fromLocal8Bit(recorder_process_->boundedOutput())},
        {"rates", rates},
        {"performanceBudgets", performance},
        {"containsRecordingSamples", false},
    };
}

void BridgeWindow::onCopyDiagnostics() {
    QApplication::clipboard()->setText(
        QString::fromUtf8(QJsonDocument(diagnosticBundle())
                              .toJson(QJsonDocument::Indented)));
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Diagnostic bundle copied without recording sample data");
}

void BridgeWindow::onExportDiagnostics() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Diagnostic Bundle",
        QDir(ui_state_.recent_diagnostic_directory)
            .filePath("session-diagnostics.json"),
        "Diagnostic bundle (*.json)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendEvent(SessionComponent::Application, EventSeverity::Error,
                    "Could not export diagnostics: " + file.errorString());
        return;
    }
    const QByteArray bytes =
        QJsonDocument(diagnosticBundle()).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        appendEvent(SessionComponent::Application, EventSeverity::Error,
                    "Could not complete diagnostic export: " + file.errorString());
        return;
    }
    file.close();
    ui_state_.recent_diagnostic_directory = QFileInfo(path).absolutePath();
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Diagnostic bundle exported to " + path);
}

void BridgeWindow::onShowVerificationDetails() {
    if (verification_report_.state == RecordingVerificationState::NotRun) return;
    QStringList lines{verification_report_.summary()};
    for (const auto& finding : verification_report_.findings) {
        lines.push_back(
            SessionEventLog::severityText(finding.severity) + ": " +
            finding.message +
            (finding.corrective_action.isEmpty()
                ? QString() : "\n  Action: " + finding.corrective_action));
    }
    QMessageBox::information(this, "Recording Verification",
                             lines.join("\n\n"));
}

void BridgeWindow::closeEvent(QCloseEvent* event) {
    if (close_finalizing_) {
        event->accept();
        return;
    }
    event->ignore();
    beginClose();
}

void BridgeWindow::beginClose() {
    if (close_pending_) {
        updateShutdownStatus();
        return;
    }
    close_pending_ = true;
    guided_start_pending_ = false;
    guided_stop_pending_ = false;
    pending_recording_start_ = false;
    preflight_start_waiting_ = false;
    saveSettings();
    filename_sync_timer_->stop();
    labrecorder_retry_timer_->stop();
    ui_->start_session_button->setEnabled(false);
    ui_->stop_session_button->setEnabled(false);
    ui_->run_preflight_button->setEnabled(false);
    ui_->start_button->setEnabled(false);
    ui_->start_recording_button->setEnabled(false);
    ui_->discover_streams_button->setEnabled(false);
    setInputsEnabled(false);

    const bool bridge_required = worker_ != nullptr;
    const bool preview_required =
        ui_->preview_panel && !ui_->preview_panel->shutdownReady();
    const bool file_required =
        discovery_worker_ != nullptr ||
        (ui_->preview_panel && ui_->preview_panel->fileLoadActive());
    const bool verification_required =
        verifier_ != nullptr || verification_waiting_for_file_;
    const bool recorder_required =
        recordingActiveOrPending() ||
        recorder_process_->ownsRunningProcess() ||
        !labrecorder_client_->shutdownReady();
    session_controller_.beginShutdown(
        monotonic_clock_.msecsSinceReference(),
        bridge_required, preview_required, recorder_required,
        file_required, verification_required);
    dashboard_.workflow = SessionWorkflowState::Closing;

    labrecorder_client_->beginShutdown();
    if (recorder_process_->kind() == RecorderProcessKind::AllowlistRecorder &&
        recorder_process_->ownsRunningProcess()) {
        recorder_process_->stopAllowlistRecording();
    }
    if (ui_->preview_panel) ui_->preview_panel->requestShutdown();
    cancelStreamDiscovery();
    if (verifier_) verifier_->cancel();
    verification_waiting_for_file_ = false;
    verification_file_timer_->stop();
    if (worker_) worker_->stopBridge();
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                "Responsive shutdown sequence started");
    close_poll_timer_->start();
    updateShutdownStatus();
}

void BridgeWindow::onClosePoll() {
    updateShutdownStatus();
}

void BridgeWindow::updateShutdownStatus() {
    if (!close_pending_) return;
    const qint64 now_ms = monotonic_clock_.msecsSinceReference();
    const bool bridge_done = worker_ == nullptr;
    const bool preview_done =
        !ui_->preview_panel || ui_->preview_panel->shutdownReady();
    const bool file_done =
        discovery_worker_ == nullptr &&
        (!ui_->preview_panel || !ui_->preview_panel->fileLoadActive());
    const bool verification_done =
        verifier_ == nullptr && !verification_waiting_for_file_;

    bool remote_safe = labrecorder_client_->shutdownSettledSafely();
    const bool recorder_connection_lost =
        !remote_safe &&
        labrecorder_client_->shutdownReady() &&
        (labrecorder_client_->connectionState() ==
             RecorderConnectionState::Disconnected ||
         labrecorder_client_->connectionState() ==
             RecorderConnectionState::Error);
    const bool allowlist_backend =
        recorder_process_->kind() == RecorderProcessKind::AllowlistRecorder;
    if (allowlist_backend) {
        remote_safe = !recorder_process_->ownsRunningProcess();
    }
    const bool owns_process = recorder_process_->ownsRunningProcess();
    bool recorder_done = remote_safe && !owns_process;

    session_controller_.updateShutdownComponent(
        SessionComponent::Bridge, bridge_done,
        bridge_done ? "Stopped" : "Waiting for bridge worker",
        now_ms);
    session_controller_.updateShutdownComponent(
        SessionComponent::Preview, preview_done,
        preview_done ? "Stopped" : "Waiting for preview worker or file loader",
        now_ms);
    session_controller_.updateShutdownComponent(
        SessionComponent::File, file_done,
        file_done ? "No active file or discovery worker"
                  : "Canceling file load or bounded discovery",
        now_ms);
    session_controller_.updateShutdownComponent(
        SessionComponent::Verification, verification_done,
        verification_done ? "Stopped" : "Canceling verification",
        now_ms);

    const bool recorder_deadline =
        session_controller_.shutdownStatus().ownedRecorderMayBeEnded(now_ms);
    if (owns_process && !owned_process_end_requested_ &&
        (remote_safe || recorder_deadline)) {
        owned_process_end_requested_ = true;
        recorder_process_->endOwnedProcess();
        appendEvent(SessionComponent::Recorder,
                    recorder_deadline && !remote_safe
                        ? EventSeverity::Warning : EventSeverity::Information,
                    recorder_deadline && !remote_safe
                        ? "Recorder deadline elapsed; ending only the owned process"
                        : "Recorder Stop is settled; ending the owned process");
    }
    recorder_done = remote_safe && !recorder_process_->ownsRunningProcess();
    if (recorder_connection_lost && !recorder_process_->ownsRunningProcess()) {
        session_controller_.markRecorderConnectionLostDuringShutdown(
            now_ms, "Remote-control connection unavailable");
        recorder_done = true;
    } else {
        session_controller_.updateShutdownComponent(
            SessionComponent::Recorder, recorder_done,
            recorder_done ? "Recorder shutdown settled"
                          : (recorder_connection_lost
                                ? "Connection lost; waiting for recorder deadline"
                                : "Waiting for final Stop acknowledgement or owned process exit"),
            now_ms);
    }
    session_controller_.updateShutdownDeadlines(now_ms);

    const QStringList delayed =
        session_controller_.shutdownStatus().delayedComponents(now_ms);
    ui_->shutdown_label->setText(
        delayed.isEmpty()
            ? "Shutdown components stopped"
            : "Closing — " + delayed.join("; "));
    updateDashboard();
    finishCloseIfReady();
}

void BridgeWindow::finishCloseIfReady() {
    if (!close_pending_ || close_finalizing_) return;
    const bool bridge_done = worker_ == nullptr;
    const bool preview_done =
        !ui_->preview_panel || ui_->preview_panel->shutdownReady();
    const bool file_done =
        discovery_worker_ == nullptr &&
        (!ui_->preview_panel || !ui_->preview_panel->fileLoadActive());
    const bool verification_done =
        verifier_ == nullptr && !verification_waiting_for_file_;
    const bool recorder_done =
        labrecorder_client_->shutdownSettledSafely() &&
        !recorder_process_->ownsRunningProcess();
    const bool recorder_lost_and_external =
        !recorder_process_->ownsRunningProcess() &&
        (labrecorder_client_->connectionState() ==
             RecorderConnectionState::Disconnected ||
         labrecorder_client_->connectionState() ==
             RecorderConnectionState::Error);
    if (!bridge_done || !preview_done || !file_done ||
        !verification_done || (!recorder_done && !recorder_lost_and_external)) {
        return;
    }
    close_poll_timer_->stop();
    close_finalizing_ = true;
    close_pending_ = false;
    saveUiState();
    QTimer::singleShot(0, this, [this]() { QWidget::close(); });
}
