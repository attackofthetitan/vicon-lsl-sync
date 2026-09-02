#include "BridgeWindow.h"

#include "gui/BridgeWindowUi.h"
#include "gui/LabRecorderClient.h"
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
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace {

using vicon_lsl::gui::RecorderProcessKind;
using vicon_lsl::gui::SessionCalibrationState;
using vicon_lsl::gui::SessionConfiguration;
using vicon_lsl::gui::SessionFileState;
using vicon_lsl::gui::StreamBinding;
using vicon_lsl::gui::StreamIdentity;
using vicon_lsl::gui::StreamReconnectionMode;
using vicon_lsl::gui_detail::BridgeWindowUi;

constexpr int kFilenameSyncDelayMs = 300;
constexpr qint64 kRecorderRetryTimeoutMs = 15000;
constexpr qint64 kRecorderStopDeadlineMs = 15000;
constexpr int kStatusStaleMs = 3000;
constexpr int kVerificationFileTimeoutMs = 15000;
// Must match the SessionEventLog cap so an append that evicts the oldest entry
// falls back to a full redraw.
constexpr int kMaximumRetainedEvents = 1000;

struct BindingControl {
    QString role;
    QComboBox* combo;
    QCheckBox* follow;
    StreamBinding* binding;
};

struct TextControl {
    QLineEdit* edit;
    QString* value;
    bool trim;
};

std::array<TextControl, 12> textControls(BridgeWindowUi& ui, SessionConfiguration& conf) {
    return {{{ui.server_edit, &conf.vicon_endpoint, true},
             {ui.marker_stream_edit, &conf.marker_output_name, true},
             {ui.segment_stream_edit, &conf.segment_output_name, true},
             {ui.study_root_edit, &conf.recording_root, true},
             {ui.filename_template_edit, &conf.recording_template, false},
             {ui.participant_edit, &conf.participant, false},
             {ui.session_edit, &conf.session, false},
             {ui.task_edit, &conf.task, false},
             {ui.acquisition_edit, &conf.acquisition, false},
             {ui.modality_edit, &conf.modality, false},
             {ui.labrecorder_executable_edit, &conf.recorder_executable, true},
             {ui.labrecorder_host_edit, &conf.recorder_host, true}}};
}

struct CheckControl {
    QCheckBox* check;
    bool* value;
};

std::array<CheckControl, 7> checkControls(BridgeWindowUi& ui, SessionConfiguration& conf) {
    return {{{ui.allow_overwrite_check, &conf.allow_overwrite},
             {ui.allow_outside_root_check, &conf.allow_outside_study_root},
             {ui.automatic_run_increment_check, &conf.automatic_run_increment},
             {ui.automatic_launch_check, &conf.recorder_automatic_launch},
             {ui.record_every_visible_check, &conf.record_every_visible_stream},
             {ui.recorder_only_check, &conf.recorder_only_mode},
             {ui.preview_external_streams_check, &conf.preview_external_streams}}};
}

std::array<BindingControl, 4> bindingControls(BridgeWindowUi& ui, SessionConfiguration& conf) {
    return {{{"markers", ui.marker_binding_combo, ui.marker_follow_name_check, &conf.preview_markers},
             {"segments", ui.segment_binding_combo, ui.segment_follow_name_check, &conf.preview_segments},
             {"gaze", ui.gaze_binding_combo, ui.gaze_follow_name_check, &conf.preview_gaze},
             {"calibration", ui.calibration_binding_combo, ui.calibration_follow_name_check, &conf.preview_calibration}}};
}

std::shared_ptr<QSettings> sessionSettings(std::shared_ptr<QSettings> settings) {
    return settings ? settings : std::make_shared<QSettings>("ViconLSL", "ViconLSLBridge");
}

bool recorderCanStart(const LabRecorderClient& recorder) {
    return recorder.connectionState() == RecorderConnectionState::Connected &&
           recorder.recordingState() != RecorderRecordingState::Recording &&
           recorder.operationState() == RecorderOperationState::Idle &&
           !recorder.shutdownRequested();
}

QString setupCheckLevelDisplayText(SetupCheckLevel level) {
    switch (level) {
        case SetupCheckLevel::Required: return "Required";
        case SetupCheckLevel::Warning: return "Warning";
        case SetupCheckLevel::Information: return "Information";
    }
    return "Information";
}

QString resultText(const SetupCheckItem& item) {
    if (item.passed) return "Pass";
    return item.level == SetupCheckLevel::Information ? "Note" : "Action needed";
}

EventSeverity errorIf(bool failed) {
    return failed ? EventSeverity::Error : EventSeverity::Information;
}

QString formatDuration(qint64 ms) {
    const qint64 total_sec = (std::max)(qint64{0}, ms / 1000);
    return QString("%1:%2:%3")
        .arg(total_sec / 3600, 2, 10, QLatin1Char('0'))
        .arg((total_sec % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(total_sec % 60, 2, 10, QLatin1Char('0'));
}

QString gibText(qint64 bytes) {
    if (bytes < 0) return "Storage: unknown";
    return "Storage: " + QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GiB";
}

QJsonArray streamInventoryJson(const QVector<StreamIdentity>& streams) {
    QJsonArray res;
    for (const StreamIdentity& s : streams) res.push_back(s.toJson());
    return res;
}

QString stateDetail(RecorderConnectionState conn, RecorderRecordingState rec, RecorderOperationState op) {
    return recorderConnectionStateText(conn) + " / " + recorderRecordingStateText(rec) + " / " + recorderOperationStateText(op);
}

} // namespace

// --- BridgeWorker ---

BridgeWorker::BridgeWorker(const Config& config, QObject* parent)
    : QThread(parent), bridge_(std::make_unique<ViconLSLBridge>(config)) {}

void BridgeWorker::setLifecycleState(ComponentLifecycleState state, const QString& detail) {
    lifecycle_state_.store(state);
    emit lifecycleChanged(state, detail);
}

void BridgeWorker::run() {
    setLifecycleState(ComponentLifecycleState::Starting, "Bridge worker started");
    try {
        bridge_->setStatusCallback([this](const BridgeStatus& status) {
            if (status.state == BridgeState::Streaming && lifecycle_state_.load() != ComponentLifecycleState::Running) {
                setLifecycleState(ComponentLifecycleState::Running, "Vicon frames are streaming");
            }
            emit statusUpdate(static_cast<int>(status.state), static_cast<unsigned long long>(status.marker_count),
                              static_cast<unsigned long long>(status.segment_count), status.frame_count,
                              QString::fromStdString(status.message));
        });
        bridge_->run();
        setLifecycleState(ComponentLifecycleState::Stopped, stop_requested_.load() ? "Bridge stopped on request" : "Bridge run completed");
    } catch (const std::exception& ex) {
        setLifecycleState(ComponentLifecycleState::Failed, QString::fromUtf8(ex.what()));
    } catch (...) {
        setLifecycleState(ComponentLifecycleState::Failed, "Unknown bridge worker failure");
    }
}

void BridgeWorker::stopBridge() {
    if (stop_requested_.exchange(true)) return;
    setLifecycleState(ComponentLifecycleState::Stopping, "Bridge stop requested");
    bridge_->stop();
}

// --- BridgeWindow ---

BridgeWindow::BridgeWindow(QWidget* parent, bool enable_preview, std::shared_ptr<QSettings> settings)
    : QWidget(parent), settings_(sessionSettings(std::move(settings))) {
    monotonic_clock_.start();
    qRegisterMetaType<vicon_lsl::gui::RecordingVerificationReport>("vicon_lsl::gui::RecordingVerificationReport");
    ui_ = vicon_lsl::gui_detail::buildBridgeWindowUi(this, enable_preview, settings_);
    labrecorder_client_ = new LabRecorderClient(this);
    recorder_process_ = new vicon_lsl::gui::RecorderProcessController(this);

    filename_sync_timer_ = new QTimer(this);
    filename_sync_timer_->setSingleShot(true);
    filename_sync_timer_->setInterval(kFilenameSyncDelayMs);
    labrecorder_retry_timer_ = new QTimer(this);
    labrecorder_retry_timer_->setInterval(250);
    close_poll_timer_ = new QTimer(this);
    close_poll_timer_->setInterval(50);
    heartbeat_timer_ = new QTimer(this);
    heartbeat_timer_->setInterval(250);
    verification_file_timer_ = new QTimer(this);
    verification_file_timer_->setInterval(100);

    connectSignals();
    loadSettings();
    status_timer_.start();
    heartbeat_timer_->start();
    validateRecordingPath();
    refreshUi();
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Application interface initialized");

    QTimer::singleShot(0, this, &BridgeWindow::beginLabRecorderStartup);
}

BridgeWindow::~BridgeWindow() {
    if (!closing() && !close_finalizing_) saveSettings();
}

template <typename Handler>
void BridgeWindow::onEdited(std::initializer_list<QWidget*> widgets, Handler handler) {
    for (QWidget* widget : widgets) {
        if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            connect(edit, &QLineEdit::textChanged, this, handler);
        } else if (auto* check = qobject_cast<QCheckBox*>(widget)) {
            connect(check, &QCheckBox::toggled, this, handler);
        } else if (auto* spin = qobject_cast<QSpinBox*>(widget)) {
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, handler);
        } else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(widget)) {
            connect(dspin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, handler);
        } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, handler);
        }
    }
}

void BridgeWindow::connectSignals() {
    connect(ui_->start_button, &QPushButton::clicked, this, &BridgeWindow::onStart);
    connect(ui_->stop_button, &QPushButton::clicked, this, &BridgeWindow::onStop);
    connect(ui_->start_session_button, &QPushButton::clicked, this, &BridgeWindow::onStartSession);
    connect(ui_->stop_session_button, &QPushButton::clicked, this, &BridgeWindow::onStopSession);
    connect(ui_->run_setup_check_button, &QPushButton::clicked, this, &BridgeWindow::onRunSetupCheck);
    connect(ui_->setup_check_override_button, &QPushButton::clicked, this, &BridgeWindow::onOverrideSetupCheck);
    connect(ui_->browse_root_button, &QPushButton::clicked, this, &BridgeWindow::onBrowseStudyRoot);
    connect(ui_->browse_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onBrowseLabRecorder);
    connect(ui_->launch_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onLaunchLabRecorder);
    connect(ui_->connect_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onConnectLabRecorder);
    connect(ui_->detach_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onDetachLabRecorder);
    connect(ui_->refresh_streams_button, &QPushButton::clicked, this, &BridgeWindow::onRefreshLabRecorder);
    connect(ui_->start_recording_button, &QPushButton::clicked, this, &BridgeWindow::onStartRecording);
    connect(ui_->stop_recording_button, &QPushButton::clicked, this, &BridgeWindow::onStopRecording);
    connect(ui_->discover_streams_button, &QPushButton::clicked, this, [this]() { startStreamDiscovery(false); });
    connect(ui_->find_next_run_button, &QPushButton::clicked, this, &BridgeWindow::onFindNextRun);
    connect(ui_->reset_configuration_button, &QPushButton::clicked, this, &BridgeWindow::onResetConfiguration);
    connect(ui_->save_preset_button, &QPushButton::clicked, this, &BridgeWindow::onSavePreset);
    connect(ui_->load_preset_button, &QPushButton::clicked, this, &BridgeWindow::onLoadPreset);
    connect(ui_->import_configuration_button, &QPushButton::clicked, this, &BridgeWindow::onImportConfiguration);
    connect(ui_->export_configuration_button, &QPushButton::clicked, this, &BridgeWindow::onExportConfiguration);
    connect(ui_->copy_diagnostics_button, &QPushButton::clicked, this, &BridgeWindow::onCopyDiagnostics);
    connect(ui_->export_diagnostics_button, &QPushButton::clicked, this, &BridgeWindow::onExportDiagnostics);
    connect(ui_->verification_details_button, &QPushButton::clicked, this, &BridgeWindow::onShowVerificationDetails);

    connect(ui_->acknowledge_error_button, &QPushButton::clicked, this, [this]() {
        event_log_.acknowledgeLastError();
        updateEventLog();
        updateDashboard();
    });
    connect(ui_->open_verified_recording_button, &QPushButton::clicked, this, [this]() {
        if (ui_->preview_panel && !verification_report_.path.isEmpty()) {
            ui_->preview_panel->openRecording(verification_report_.path);
        }
    });

    connect(filename_sync_timer_, &QTimer::timeout, this, &BridgeWindow::syncFilenameToLabRecorder);
    connect(labrecorder_retry_timer_, &QTimer::timeout, this, &BridgeWindow::onLabRecorderRetry);
    connect(close_poll_timer_, &QTimer::timeout, this, &BridgeWindow::updateShutdownStatus);
    connect(heartbeat_timer_, &QTimer::timeout, this, &BridgeWindow::onHeartbeatTick);
    connect(verification_file_timer_, &QTimer::timeout, this, &BridgeWindow::onVerificationFilePoll);

    onEdited({ui_->study_root_edit, ui_->filename_template_edit, ui_->participant_edit,
              ui_->session_edit, ui_->task_edit, ui_->acquisition_edit, ui_->modality_edit,
              ui_->run_spin, ui_->storage_warning_spin, ui_->allow_overwrite_check,
              ui_->allow_outside_root_check, ui_->automatic_run_increment_check}, [this]() {
        updateConfigurationFromUi();
        validateRecordingPath();
        scheduleFilenameSync();
    });

    onEdited({ui_->server_edit, ui_->marker_stream_edit, ui_->segment_stream_edit,
              ui_->preview_external_streams_check}, [this]() {
        updateConfigurationFromUi();
        if (ui_->preview_panel) ui_->preview_panel->applySessionConfiguration(configuration_);
        populateBindingCombos();
        updateReadiness();
    });

    onEdited({ui_->labrecorder_host_edit, ui_->labrecorder_executable_edit, ui_->labrecorder_port_spin,
              ui_->automatic_launch_check, ui_->record_every_visible_check, ui_->recorder_only_check}, [this]() {
        updateConfigurationFromUi();
        refreshUi();
    });

    connect(ui_->stream_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!item || item->row() < 0 || item->row() >= stream_inventory_.size()) return;
        if (item->column() < 0 || item->column() > 1) return;
        bool& checked = item->column() == 0 ? stream_inventory_[item->row()].selected : stream_inventory_[item->row()].required;
        checked = (item->checkState() == Qt::Checked);
        updateConfigurationFromUi();
        updateReadiness();
    });

    for (const BindingControl& c : bindingControls(*ui_, configuration_)) {
        onEdited({c.combo, c.follow}, [this]() {
            updateBindingsFromUi();
            if (ui_->preview_panel) ui_->preview_panel->applySessionConfiguration(configuration_);
        });
    }
    onEdited({ui_->event_severity_filter, ui_->event_component_filter}, [this]() { updateEventLog(); });

    connect(labrecorder_client_, &LabRecorderClient::connectionStateChanged, this, [this](RecorderConnectionState state, const QString& msg) {
        if (!msg.isEmpty()) setLabRecorderStatus(msg, errorIf(state == RecorderConnectionState::Error));
        if (state == RecorderConnectionState::Connected) {
            startup_endpoint_probe_ = false;
            labrecorder_retry_timer_->stop();
            appendEvent(SessionComponent::Recorder, EventSeverity::Information, "Connected to the recorder");
        } else if (startup_endpoint_probe_ && state != RecorderConnectionState::Connecting) {
            startup_endpoint_probe_ = false;
            if (configuration_.recorder_automatic_launch) launchConfiguredRecorder();
        }
        refreshUi();
        scheduleFilenameSync();
        pumpSession();
    });
    connect(labrecorder_client_, &LabRecorderClient::recordingStateChanged, this, [this](RecorderRecordingState state) {
        if (state == RecorderRecordingState::Recording) {
            stop_requested_ = false;
            recording_elapsed_.restart();
            appendEvent(SessionComponent::Recorder, EventSeverity::Information, "Recorder confirmed that recording started");
        } else if (state == RecorderRecordingState::Stopped && stop_requested_) {
            requestVerification();
        }
        refreshUi();
        pumpSession();
    });
    connect(labrecorder_client_, &LabRecorderClient::operationStateChanged, this, [this](RecorderOperationState state) {
        ui_->labrecorder_operation_label->setText(recorderOperationStateText(state));
        refreshUi();
        pumpSession();
    });
    connect(labrecorder_client_, &LabRecorderClient::commandProgress, this, [this](const QString& op, int n, int cnt, const QString& cmd) {
        ui_->labrecorder_operation_progress->setRange(0, (std::max)(1, cnt));
        ui_->labrecorder_operation_progress->setValue((std::max)(0, n - 1));
        ui_->labrecorder_operation_progress->setFormat(QString("%1 %2/%3").arg(op).arg(n).arg(cnt));
        ui_->labrecorder_operation_label->setText(op + ": waiting for a reply to " + cmd);
    });
    connect(labrecorder_client_, &LabRecorderClient::commandFinished, this, [this](const QString& op, bool ok, const QString& msg) {
        ui_->labrecorder_operation_progress->setValue(ui_->labrecorder_operation_progress->maximum());
        setLabRecorderStatus(ok ? op + " completed" : op + " failed: " + msg, ok ? EventSeverity::Information : EventSeverity::Error);
        if (op.contains("stop recording", Qt::CaseInsensitive) && ok) requestVerification();
        refreshUi();
        pumpSession();
    });

    connect(recorder_process_, &vicon_lsl::gui::RecorderProcessController::stateChanged, this, [this](RecorderProcessState s, const QString& d) {
        appendEvent(SessionComponent::Recorder, errorIf(s == RecorderProcessState::LaunchFailed), d);
        if (s == RecorderProcessState::OwnedRunning && recorder_process_->kind() == RecorderProcessKind::GraphicalRecorder) {
            labrecorder_retry_elapsed_.restart();
            labrecorder_retry_timer_->start();
            onLabRecorderRetry();
        }
        refreshUi();
        pumpSession();
    });
    connect(recorder_process_, &vicon_lsl::gui::RecorderProcessController::recordingStateChanged, this, [this](RecorderRecordingState s) {
        if (s == RecorderRecordingState::Recording) {
            stop_requested_ = false;
            recording_elapsed_.restart();
            appendEvent(SessionComponent::Recorder, EventSeverity::Information, "Selected-stream recorder is recording");
        } else if (s == RecorderRecordingState::Stopped) {
            requestVerification();
        }
        refreshUi();
        pumpSession();
    });
    connect(recorder_process_, &vicon_lsl::gui::RecorderProcessController::outputLine, this, [this](EventSeverity sev, const QString& line) {
        appendEvent(SessionComponent::Recorder, sev, "Recorder: " + line);
    });
    connect(recorder_process_, &vicon_lsl::gui::RecorderProcessController::processExited, this, [this](int code, bool exp, RecorderProcessKind kind) {
        if (kind == RecorderProcessKind::SelectedStreamRecorder && !exp) {
            appendEvent(SessionComponent::Recorder, EventSeverity::Error, "Selected-stream recorder closed unexpectedly with code " + QString::number(code));
        }
        pumpSession();
    });

    if (ui_->preview_panel) {
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::lifecycleChanged, this, [this](ComponentLifecycleState s, const QString& d) {
            appendEvent(SessionComponent::Preview, errorIf(s == ComponentLifecycleState::Failed), d);
            updateDashboard();
            pumpSession();
        });
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::streamInventoryChanged, this, [this](const QVector<StreamIdentity>& s) {
            mergeStreamInventory(s);
            populateStreamTable();
        });
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::calibrationStateChanged, this, [this](SessionCalibrationState, const QString& q, bool) {
            ui_->calibration_state_label->setToolTip(q);
            updateDashboard();
        });
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::fileStateChanged, this, [this](SessionFileState s, const QString& d) {
            file_state_ = s;
            appendEvent(SessionComponent::File, errorIf(s == SessionFileState::Failed), d);
            updateDashboard();
            pumpSession();
        });
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::deliveryMetricsChanged, this, [this](const vicon_lsl::PreviewDeliveryMetrics& m) {
            preview_replaced_frames_ = m.replaced_before_display;
            preview_coalesced_samples_ = m.coalesced_input_samples;
            preview_latency_ms_ = m.display_latency_ms;
            updateDashboard();
        });
    }
}

void BridgeWindow::loadSettings() {
    configuration_ = vicon_lsl::gui::SessionConfigurationStore::load(*settings_);
    ui_state_ = vicon_lsl::gui::SessionConfigurationStore::loadUiState(*settings_);
    applyConfigurationToUi();
    restoreUiState();
    refreshPresetList();
}

void BridgeWindow::saveSettings() {
    updateConfigurationFromUi();
    vicon_lsl::gui::SessionConfigurationStore::save(*settings_, configuration_);
    saveUiState();
}

void BridgeWindow::applyConfigurationToUi() {
    std::vector<QSignalBlocker> blockers;
    for (const TextControl& c : textControls(*ui_, configuration_)) blockers.emplace_back(c.edit);
    for (const CheckControl& c : checkControls(*ui_, configuration_)) blockers.emplace_back(c.check);
    for (const BindingControl& c : bindingControls(*ui_, configuration_)) {
        blockers.emplace_back(c.combo);
        blockers.emplace_back(c.follow);
    }
    for (QWidget* w : std::initializer_list<QWidget*>{
             ui_->run_spin, ui_->storage_warning_spin, ui_->labrecorder_port_spin}) {
        blockers.emplace_back(w);
    }
    for (const TextControl& c : textControls(*ui_, configuration_)) c.edit->setText(*c.value);
    for (const CheckControl& c : checkControls(*ui_, configuration_)) c.check->setChecked(*c.value);
    ui_->run_spin->setValue(configuration_.run);
    ui_->storage_warning_spin->setValue(configuration_.storage_warning_gib);
    ui_->labrecorder_port_spin->setValue(configuration_.recorder_port);
    for (const BindingControl& c : bindingControls(*ui_, configuration_)) {
        c.follow->setChecked(c.binding->reconnection == StreamReconnectionMode::FollowName);
    }
    ui_->marker_binding_combo->setEnabled(configuration_.preview_external_streams);
    ui_->segment_binding_combo->setEnabled(configuration_.preview_external_streams);
    if (ui_->preview_panel) ui_->preview_panel->applySessionConfiguration(configuration_);
    populateBindingCombos();
    validateRecordingPath();
    updateDashboard();
}

void BridgeWindow::updateConfigurationFromUi() {
    for (const TextControl& c : textControls(*ui_, configuration_)) {
        *c.value = c.trim ? c.edit->text().trimmed() : c.edit->text();
    }
    for (const CheckControl& c : checkControls(*ui_, configuration_)) *c.value = c.check->isChecked();
    if (ui_->preview_panel) ui_->preview_panel->updateSessionConfiguration(configuration_);
    if (!configuration_.preview_external_streams) configuration_.bindPreviewOutputs();
    configuration_.recorder_port = ui_->labrecorder_port_spin->value();
    configuration_.run = ui_->run_spin->value();
    configuration_.storage_warning_gib = ui_->storage_warning_spin->value();

    if (!stream_inventory_.isEmpty()) {
        QVector<StreamBinding> bindings;
        for (const StreamIdentity& id : stream_inventory_) {
            if (!id.selected && !id.required) continue;
            bindings.push_back({
                id.role, id.name, id.source_id,
                id.source_id.isEmpty() ? StreamReconnectionMode::FollowName : StreamReconnectionMode::SourceIdentity,
                id.required, id.channel_count, id.nominal_rate, id.coordinate_frame
            });
        }
        configuration_.recording_streams = std::move(bindings);
    }
}

void BridgeWindow::restoreUiState() {
    if (!ui_state_.geometry.isEmpty()) restoreGeometry(ui_state_.geometry);
    if (!ui_state_.splitter_state.isEmpty()) ui_->main_splitter->restoreState(ui_state_.splitter_state);
    ui_->controls_tabs->setCurrentIndex((std::max)(0, (std::min)(ui_state_.active_control_tab, ui_->controls_tabs->count() - 1)));
}

void BridgeWindow::saveUiState() {
    ui_state_.geometry = saveGeometry();
    ui_state_.splitter_state = ui_->main_splitter->saveState();
    ui_state_.active_control_tab = ui_->controls_tabs->currentIndex();
    vicon_lsl::gui::SessionConfigurationStore::saveUiState(*settings_, ui_state_);
}

void BridgeWindow::refreshPresetList(const QString& select) {
    const QString cur = select.isEmpty() ? ui_->preset_combo->currentText() : select;
    const QSignalBlocker blocker(ui_->preset_combo);
    ui_->preset_combo->clear();
    ui_->preset_combo->addItems(vicon_lsl::gui::SessionConfigurationStore::presetNames(*settings_));
    ui_->preset_combo->setEditText(cur);
}

void BridgeWindow::onResetConfiguration() {
    if (recordingActiveOrPending()) {
        appendEvent(SessionComponent::Application, EventSeverity::Warning, "Configuration reset rejected while recording work is active");
        return;
    }
    configuration_ = vicon_lsl::gui::SessionConfiguration();
    stream_inventory_.clear();
    applyConfigurationToUi();
    populateStreamTable();
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Session configuration reset to defaults");
}

void BridgeWindow::onSavePreset() {
    updateConfigurationFromUi();
    const QString name = ui_->preset_combo->currentText().trimmed();
    QString err;
    if (vicon_lsl::gui::SessionConfigurationStore::savePreset(*settings_, name, configuration_, &err)) {
        refreshPresetList(name);
        appendEvent(SessionComponent::Application, EventSeverity::Information, "Saved session preset " + name);
    } else appendEvent(SessionComponent::Application, EventSeverity::Error, "Could not save preset: " + err);
}

void BridgeWindow::onLoadPreset() {
    if (recordingActiveOrPending()) {
        appendEvent(SessionComponent::Application, EventSeverity::Warning, "Preset load rejected while recording work is active");
        return;
    }
    const QString name = ui_->preset_combo->currentText().trimmed();
    vicon_lsl::gui::SessionConfiguration loaded;
    QString err;
    if (vicon_lsl::gui::SessionConfigurationStore::loadPreset(*settings_, name, loaded, &err)) {
        configuration_ = std::move(loaded);
        stream_inventory_.clear();
        applyConfigurationToUi();
        populateStreamTable();
        appendEvent(SessionComponent::Application, EventSeverity::Information, "Loaded session preset " + name);
    } else appendEvent(SessionComponent::Application, EventSeverity::Error, "Could not load preset: " + err);
}

void BridgeWindow::onImportConfiguration() {
    if (recordingActiveOrPending()) return;
    const QString path = QFileDialog::getOpenFileName(this, "Import Session Configuration", ui_state_.recent_preset_directory, "Session configuration (*.json)");
    if (path.isEmpty()) return;
    vicon_lsl::gui::SessionConfiguration loaded;
    QString err;
    if (!vicon_lsl::gui::SessionConfigurationStore::importConfiguration(path, loaded, &err)) {
        appendEvent(SessionComponent::Application, EventSeverity::Error, "Configuration import failed: " + err);
        return;
    }
    configuration_ = std::move(loaded);
    ui_state_.recent_preset_directory = QFileInfo(path).absolutePath();
    stream_inventory_.clear();
    applyConfigurationToUi();
    populateStreamTable();
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Imported session configuration " + path);
}

void BridgeWindow::onExportConfiguration() {
    updateConfigurationFromUi();
    const QString path = QFileDialog::getSaveFileName(this, "Export Session Configuration",
        QDir(ui_state_.recent_preset_directory).filePath("session-configuration.json"), "Session configuration (*.json)");
    if (path.isEmpty()) return;
    QString err;
    if (!vicon_lsl::gui::SessionConfigurationStore::exportConfiguration(path, configuration_, &err)) {
        appendEvent(SessionComponent::Application, EventSeverity::Error, "Configuration export failed: " + err);
        return;
    }
    ui_state_.recent_preset_directory = QFileInfo(path).absolutePath();
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Exported session configuration " + path);
}

void BridgeWindow::onBrowseStudyRoot() {
    const QString root = QFileDialog::getExistingDirectory(this, "Select Study Root", ui_->study_root_edit->text());
    if (!root.isEmpty()) ui_->study_root_edit->setText(QDir::toNativeSeparators(root));
}

void BridgeWindow::onBrowseLabRecorder() {
    const QString path = QFileDialog::getOpenFileName(this, "Select Recorder Program", ui_->labrecorder_executable_edit->text(), "Programs (*.exe);;All files (*)");
    if (!path.isEmpty()) ui_->labrecorder_executable_edit->setText(QDir::toNativeSeparators(path));
}

RecordingPathValidationOptions BridgeWindow::pathValidationOptions(bool create_parent) const {
    RecordingPathValidationOptions opt;
    opt.allow_outside_study_root = ui_->allow_outside_root_check->isChecked();
    opt.allow_overwrite = ui_->allow_overwrite_check->isChecked();
    opt.create_parent_directories = create_parent;
    opt.verify_write_access = true;
    opt.storage_warning_bytes = static_cast<qint64>(ui_->storage_warning_spin->value() * 1024.0 * 1024.0 * 1024.0);
    return opt;
}

void BridgeWindow::validateRecordingPath(bool create_parent) {
    path_result_ = LabRecorderFilenamePolicy::validate(ui_->filenameFields(), pathValidationOptions(create_parent));
    ui_->filename_preview_label->setText(path_result_.absolute_path);
    ui_->filename_preview_label->setToolTip(path_result_.absolute_path);
    ui_->filename_preview_label->setCursorPosition(0);
    ui_->path_validation_label->setText(path_result_.summary());
    ui_->path_validation_label->setToolTip(path_result_.summary());
    refreshUi();
}

void BridgeWindow::onFindNextRun() {
    const int next = LabRecorderFilenamePolicy::findNextRun(ui_->filenameFields(), ui_->run_spin->value(), pathValidationOptions(false));
    if (next > ui_->run_spin->value()) {
        ui_->run_spin->setValue(next);
        appendEvent(SessionComponent::Path, EventSeverity::Information, "Selected next unused run " + QString::number(next));
    } else appendEvent(SessionComponent::Path, EventSeverity::Warning, "No unused run was found within the supported range");
}

void BridgeWindow::eventLogFilter(EventSeverity& minimum,
                                  QVector<SessionComponent>& components) const {
    minimum = EventSeverity::Information;
    if (ui_->event_severity_filter->currentIndex() == 1) minimum = EventSeverity::Warning;
    else if (ui_->event_severity_filter->currentIndex() >= 2) minimum = EventSeverity::Error;

    components.clear();
    const int idx = ui_->event_component_filter->currentIndex();
    if (idx > 0) components.push_back(static_cast<SessionComponent>(idx - 1));
}

void BridgeWindow::appendEvent(SessionComponent comp, EventSeverity sev, const QString& msg) {
    if (msg.trimmed().isEmpty()) return;
    event_log_.append(comp, sev, msg);
    if (!ui_ || !ui_->event_log) return;

    // Append the one new line rather than re-rendering every retained entry;
    // recorder output can arrive line by line for the length of a session.
    EventSeverity min_sev = EventSeverity::Information;
    QVector<SessionComponent> comps;
    eventLogFilter(min_sev, comps);
    const QVector<SessionEvent>& entries = event_log_.entries();
    const bool dropped_oldest = entries.size() == kMaximumRetainedEvents;
    if (dropped_oldest) {
        updateEventLog();
        return;
    }
    if (!entries.isEmpty() &&
        SessionEventLog::matchesFilter(entries.back(), min_sev, comps)) {
        QScrollBar* scroll = ui_->event_log->verticalScrollBar();
        const bool at_end = scroll->value() >= scroll->maximum();
        ui_->event_log->appendPlainText(SessionEventLog::formatEvent(entries.back()));
        if (at_end) scroll->setValue(scroll->maximum());
    }
    const QString err = event_log_.lastError();
    ui_->last_error_label->setText(err.isEmpty() ? "-" : err);
}

void BridgeWindow::updateEventLog() {
    if (!ui_ || !ui_->event_log) return;
    EventSeverity min_sev = EventSeverity::Information;
    QVector<SessionComponent> comps;
    eventLogFilter(min_sev, comps);

    QScrollBar* scroll = ui_->event_log->verticalScrollBar();
    const bool at_end = scroll->value() >= scroll->maximum();
    ui_->event_log->setPlainText(event_log_.toText(min_sev, comps));
    if (at_end) scroll->setValue(scroll->maximum());
    const QString err = event_log_.lastError();
    ui_->last_error_label->setText(err.isEmpty() ? "-" : err);
}

void BridgeWindow::setLabRecorderStatus(const QString& status, EventSeverity sev) {
    ui_->labrecorder_status_label->setText(status);
    appendEvent(SessionComponent::Recorder, sev, status);
}

void BridgeWindow::updateDashboard() {
    const RecorderRecordingState rec = effectiveRecordingState();
    const RecorderOperationState op = effectiveOperationState();
    const ComponentLifecycleState prev = ui_->preview_panel ? ui_->preview_panel->lifecycleState() : ComponentLifecycleState::Idle;
    const SessionCalibrationState cal = ui_->preview_panel ? ui_->preview_panel->sessionCalibrationState() : SessionCalibrationState::Manual;

    if (rec == RecorderRecordingState::Recording) ui_->recording_indicator_label->setText("RECORDING");
    else if (op == RecorderOperationState::Starting) ui_->recording_indicator_label->setText("STARTING");
    else if (op == RecorderOperationState::Stopping || stop_requested_) ui_->recording_indicator_label->setText("STOPPING");
    else ui_->recording_indicator_label->setText("NOT RECORDING");

    ui_->recording_elapsed_label->setText(
        recording_elapsed_.isValid() && (rec == RecorderRecordingState::Recording || stop_requested_ ||
                                         verification_report_.state == RecordingVerificationState::Running)
            ? formatDuration(recording_elapsed_.elapsed()) : "00:00:00");
    ui_->recording_path_label->setText(path_result_.absolute_path.isEmpty() ? "No validated destination" : path_result_.absolute_path);
    ui_->recording_path_label->setToolTip(path_result_.summary());
    ui_->run_identifier_label->setText(QString("run %1").arg(ui_->run_spin->value()));
    ui_->bridge_state_label->setText(componentLifecycleStateText(bridge_lifecycle_));
    ui_->recorder_state_label->setText(stateDetail(labrecorder_client_->connectionState(), rec, op));
    ui_->preview_state_label->setText(componentLifecycleStateText(prev));
    ui_->calibration_state_label->setText(calibrationStateText(cal));
    ui_->file_state_dashboard_label->setText(fileStateText(file_state_));
    ui_->verification_state_label->setText(verificationStateText(verification_report_.state));
    ui_->recorder_owner_label->setText(recorderProcessStateText(recorder_process_->state()));
    ui_->recorder_endpoint_label->setText(configuration_.recorder_host + ":" + QString::number(configuration_.recorder_port));
    ui_->storage_label->setText(gibText(path_result_.available_storage_bytes));
    ui_->drop_label->setText("Preview: " + QString::number(preview_replaced_frames_) + " frame(s) skipped, " +
                             QString::number(preview_coalesced_samples_) + " update(s) combined, " +
                             QString::number(preview_latency_ms_) + " ms behind");

    ui_->verification_details_button->setEnabled(verification_report_.state != RecordingVerificationState::NotRun);
    ui_->open_verified_recording_button->setEnabled(!verification_report_.path.isEmpty() && QFileInfo::exists(verification_report_.path));
    ui_->start_session_button->setEnabled(!closing() && !startingSession() && !recordingActiveOrPending());
    ui_->stop_session_button->setEnabled(!closing() && (startingSession() || recordingActiveOrPending() || worker_ || (ui_->preview_panel && !ui_->preview_panel->shutdownReady())));
}

void BridgeWindow::onHeartbeatTick() {
    if (bridge_streaming_ && have_previous_status_ && !bridge_status_stale_ && status_timer_.elapsed() - previous_status_ms_ > kStatusStaleMs) {
        bridge_status_stale_ = true;
        bridge_effective_rate_ = 0.0;
        ui_->frame_rate_label->setText("0.0 Hz");
        appendEvent(SessionComponent::Bridge, EventSeverity::Warning, "Bridge has not reported an update for three seconds");
        updateReadiness();
    }
    updateDashboard();
    pumpSession();
}

QString BridgeWindow::resolveLabRecorderExecutable() const {
    const QString configured_path = ui_->labrecorder_executable_edit->text().trimmed();
    const QFileInfo configured(configured_path);
    if (!configured_path.isEmpty() && configured.exists() && configured.isFile()) return QDir::toNativeSeparators(configured.absoluteFilePath());
    return vicon_lsl::gui::RecorderProcessController::bundledGraphicalRecorderExecutable(
        QCoreApplication::applicationDirPath());
}

QString BridgeWindow::resolveSelectedStreamExecutable() const {
    return vicon_lsl::gui::RecorderProcessController::bundledSelectedStreamExecutable(resolveLabRecorderExecutable(), QCoreApplication::applicationDirPath());
}

void BridgeWindow::beginLabRecorderStartup() {
    if (closing() || labrecorder_client_->connectionState() == RecorderConnectionState::Connecting) return;
    updateConfigurationFromUi();
    startup_endpoint_probe_ = true;
    startup_launch_attempted_ = false;
    setLabRecorderStatus("Looking for the recorder at the configured address");
    labrecorder_client_->connectToServer(configuration_.recorder_host, static_cast<quint16>(configuration_.recorder_port), 500);
}

void BridgeWindow::launchConfiguredRecorder() {
    if (startup_launch_attempted_) return;
    startup_launch_attempted_ = true;
    const QString exe = resolveLabRecorderExecutable();
    if (exe.isEmpty()) {
        setLabRecorderStatus("The recorder could not be reached or found on this computer", EventSeverity::Warning);
        return;
    }
    ui_->labrecorder_executable_edit->setText(exe);
    QString err;
    if (!recorder_process_->launchGraphicalRecorder(exe, &err)) {
        setLabRecorderStatus("Recorder launch failed: " + err, EventSeverity::Error);
        return;
    }
    setLabRecorderStatus("Starting the recorder in the background");
}

void BridgeWindow::onLaunchLabRecorder() {
    if (recordingActiveOrPending() || labrecorder_client_->connectionState() == RecorderConnectionState::Connected ||
        labrecorder_client_->connectionState() == RecorderConnectionState::Connecting) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning, "The recorder cannot be started while it is connected or recording");
        return;
    }
    updateConfigurationFromUi();
    startup_launch_attempted_ = false;
    launchConfiguredRecorder();
}

void BridgeWindow::onConnectLabRecorder() {
    const RecorderConnectionState state = labrecorder_client_->connectionState();
    if (recordingActiveOrPending() && (state == RecorderConnectionState::Connected || state == RecorderConnectionState::Connecting)) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning, "Recorder connection replacement rejected while recording work is active");
        return;
    }
    labrecorder_retry_timer_->stop();
    startup_endpoint_probe_ = false;
    updateConfigurationFromUi();
    saveSettings();
    labrecorder_client_->connectToServer(configuration_.recorder_host, static_cast<quint16>(configuration_.recorder_port));
}

void BridgeWindow::onDetachLabRecorder() {
    if (recordingActiveOrPending()) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning, "Disconnect or detach rejected while recording work is active");
        return;
    }
    labrecorder_retry_timer_->stop();
    labrecorder_client_->disconnectFromServer();
    if (recorder_process_->ownsRunningProcess()) {
        recorder_process_->detach();
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning, "Disconnected from the recorder and left it running");
    } else {
        appendEvent(SessionComponent::Recorder, EventSeverity::Information, "Disconnected from externally managed recorder");
    }
    refreshUi();
}

void BridgeWindow::onLabRecorderRetry() {
    const RecorderConnectionState state = labrecorder_client_->connectionState();
    if (state == RecorderConnectionState::Connected) {
        labrecorder_retry_timer_->stop();
        return;
    }
    const qint64 el = labrecorder_retry_elapsed_.isValid() ? labrecorder_retry_elapsed_.elapsed() : kRecorderRetryTimeoutMs;
    if (el >= kRecorderRetryTimeoutMs) {
        labrecorder_retry_timer_->stop();
        setLabRecorderStatus("The recorder was not ready within 15 seconds", EventSeverity::Error);
        return;
    }
    if (state == RecorderConnectionState::Connecting) return;
    labrecorder_client_->connectToServer(ui_->labrecorder_host_edit->text().trimmed(), static_cast<quint16>(ui_->labrecorder_port_spin->value()), 200);
}

void BridgeWindow::onRefreshLabRecorder() {
    if (labrecorder_client_->refreshStreams()) setLabRecorderStatus("Recorder stream refresh queued");
    else setLabRecorderStatus("Recorder stream refresh is unavailable in the current state", EventSeverity::Warning);
}

void BridgeWindow::scheduleFilenameSync() {
    if (!filename_sync_timer_ || closing()) return;
    if (configuration_.record_every_visible_stream && path_result_.valid() && recorderCanStart(*labrecorder_client_)) filename_sync_timer_->start();
    else filename_sync_timer_->stop();
}

void BridgeWindow::syncFilenameToLabRecorder() {
    validateRecordingPath();
    if (!path_result_.valid() || !configuration_.record_every_visible_stream) return;
    if (labrecorder_client_->updateFilename(path_result_.normalized_fields)) setLabRecorderStatus("Recording file path update queued");
}

RecorderRecordingState BridgeWindow::effectiveRecordingState() const {
    if (recorder_process_ && recorder_process_->kind() == RecorderProcessKind::SelectedStreamRecorder) {
        return recorder_process_->selectedStreamRecording() ? RecorderRecordingState::Recording : RecorderRecordingState::Stopped;
    }
    return labrecorder_client_ ? labrecorder_client_->recordingState() : RecorderRecordingState::Unknown;
}

RecorderOperationState BridgeWindow::effectiveOperationState() const {
    if (!configuration_.record_every_visible_stream) {
        if (recorder_process_->state() == RecorderProcessState::Launching) return RecorderOperationState::Starting;
        if (stop_requested_) return RecorderOperationState::Stopping;
        return closing() ? RecorderOperationState::ShuttingDown : RecorderOperationState::Idle;
    }
    return labrecorder_client_->operationState();
}

bool BridgeWindow::recordingActiveOrPending() const {
    const RecorderRecordingState rec = effectiveRecordingState();
    const RecorderOperationState op = effectiveOperationState();
    return pending_recording_start_ || rec == RecorderRecordingState::Recording || op == RecorderOperationState::Starting ||
           op == RecorderOperationState::Stopping || labrecorder_client_->startMayHaveReachedServer() ||
           labrecorder_client_->desiredRecordingState() == RecorderRecordingState::Recording;
}

void BridgeWindow::updateRecordingButtons() {
    if (!ui_ || !labrecorder_client_) return;
    const bool remote = configuration_.record_every_visible_stream;
    ui_->refresh_streams_button->setEnabled(!closing() && remote && recorderCanStart(*labrecorder_client_));
    ui_->start_recording_button->setEnabled(!closing() && !recordingActiveOrPending());
    ui_->stop_recording_button->setEnabled(!closing() && recordingActiveOrPending());
    ui_->connect_labrecorder_button->setEnabled(
        !closing() && labrecorder_client_->connectionState() != RecorderConnectionState::Connecting &&
        (!recordingActiveOrPending() || labrecorder_client_->connectionState() == RecorderConnectionState::Disconnected ||
         labrecorder_client_->connectionState() == RecorderConnectionState::Error));
    ui_->detach_labrecorder_button->setEnabled(
        !closing() && !recordingActiveOrPending() &&
        (labrecorder_client_->connectionState() != RecorderConnectionState::Disconnected || recorder_process_->ownsRunningProcess()));
    ui_->launch_labrecorder_button->setEnabled(
        !closing() && !recordingActiveOrPending() && !recorder_process_->ownsRunningProcess() &&
        labrecorder_client_->connectionState() != RecorderConnectionState::Connected &&
        labrecorder_client_->connectionState() != RecorderConnectionState::Connecting);
}

void BridgeWindow::refreshUi() {
    updateRecordingButtons();
    updateReadiness();
    updateDashboard();
}

void BridgeWindow::pumpSession() {
    if (startingSession()) advanceGuidedStart();
    if (stoppingSession()) advanceGuidedStop();
    if (closing()) updateShutdownStatus();
}

void BridgeWindow::updateReadiness() {
    if (!ui_ || !ui_->readiness_label) return;
    const QString b_text = configuration_.recorder_only_mode
        ? "bridge not required"
        : (bridge_streaming_ && !bridge_status_stale_ ? "bridge streaming at " + ui_->frame_rate_label->text() : "bridge unavailable or not recently updated");
    const QString r_text = configuration_.record_every_visible_stream
        ? recorderConnectionStateText(labrecorder_client_->connectionState())
        : (resolveSelectedStreamExecutable().isEmpty() ? "selected-stream recorder missing" : "selected-stream recorder available");
    const QString p_text = path_result_.valid() ? "destination valid" : "destination blocked";
    ui_->readiness_label->setText(QString("Readiness: %1; recorder %2; %3; %4 inventoried stream(s).").arg(b_text, r_text, p_text).arg(stream_inventory_.size()));
}

void BridgeWindow::onStart() {
    if (worker_ || closing()) return;
    updateConfigurationFromUi();
    saveSettings();
    Config config;
    config.vicon_server = configuration_.vicon_endpoint.toStdString();
    config.marker_stream_name = configuration_.marker_output_name.toStdString();
    config.segment_stream_name = configuration_.segment_output_name.toStdString();
    worker_ = new BridgeWorker(config, this);
    bridge_lifecycle_ = ComponentLifecycleState::Starting;
    ui_->start_button->setEnabled(false);
    ui_->stop_button->setEnabled(true);
    ui_->setBridgeInputsEnabled(false);
    appendEvent(SessionComponent::Bridge, EventSeverity::Information, "Bridge start requested for " + configuration_.vicon_endpoint);
    connect(worker_, &BridgeWorker::statusUpdate, this, &BridgeWindow::onStatusUpdate);
    connect(worker_, &BridgeWorker::lifecycleChanged, this, [this](ComponentLifecycleState s, const QString& d) {
        bridge_lifecycle_ = s;
        appendEvent(SessionComponent::Bridge, errorIf(s == ComponentLifecycleState::Failed), d);
        updateDashboard();
        pumpSession();
    });
    connect(worker_, &QThread::finished, this, &BridgeWindow::onWorkerFinished);
    worker_->start();
    updateDashboard();
}

void BridgeWindow::onStop() {
    if (!worker_) return;
    ui_->stop_button->setEnabled(false);
    bridge_lifecycle_ = ComponentLifecycleState::Stopping;
    worker_->stopBridge();
    appendEvent(SessionComponent::Bridge, EventSeverity::Information, "Bridge stop requested");
    updateDashboard();
}

void BridgeWindow::onStatusUpdate(int state, unsigned long long markers, unsigned long long segments, unsigned int frames, const QString& message) {
    const auto bridge_state = static_cast<BridgeState>(state);
    QString txt;
    switch (bridge_state) {
        case BridgeState::Disconnected: txt = "Disconnected"; break;
        case BridgeState::Connecting: txt = "Connecting"; break;
        case BridgeState::Streaming: txt = "Streaming"; break;
        case BridgeState::Stopped: txt = "Stopped"; break;
    }
    if (!message.isEmpty()) txt += " - " + message;
    ui_->status_label->setText(txt);
    bridge_streaming_ = (bridge_state == BridgeState::Streaming);
    bridge_status_stale_ = false;
    if (bridge_streaming_) bridge_lifecycle_ = ComponentLifecycleState::Running;
    ui_->markers_label->setText(QString::number(markers));
    ui_->segments_label->setText(QString::number(segments));
    ui_->frames_label->setText(QString::number(frames));
    const qint64 now_ms = status_timer_.elapsed();
    if (have_previous_status_) {
        const qint64 delta_ms = now_ms - previous_status_ms_;
        if (delta_ms > 0) {
            const unsigned int delta = frames >= previous_frames_ ? frames - previous_frames_ : 0;
            bridge_effective_rate_ = static_cast<double>(delta) * 1000.0 / static_cast<double>(delta_ms);
            ui_->frame_rate_label->setText(QString::number(bridge_effective_rate_, 'f', 1) + " Hz");
        }
    }
    previous_status_ms_ = now_ms;
    previous_frames_ = frames;
    have_previous_status_ = true;
    updateReadiness();
    updateDashboard();
    pumpSession();
}

bool BridgeWindow::bridgeStatusRecent() const {
    return bridge_streaming_ && have_previous_status_ && !bridge_status_stale_ &&
           status_timer_.elapsed() - previous_status_ms_ <= kStatusStaleMs;
}

void BridgeWindow::onWorkerFinished() {
    BridgeWorker* completed = worker_;
    worker_ = nullptr;
    bridge_streaming_ = false;
    bridge_status_stale_ = false;
    have_previous_status_ = false;
    bridge_effective_rate_ = 0.0;
    if (bridge_lifecycle_ != ComponentLifecycleState::Failed) bridge_lifecycle_ = ComponentLifecycleState::Stopped;
    ui_->frame_rate_label->setText("0.0 Hz");
    ui_->start_button->setEnabled(!closing());
    ui_->stop_button->setEnabled(false);
    ui_->setBridgeInputsEnabled(!closing());
    if (completed) completed->deleteLater();
    updateReadiness();
    updateDashboard();
    pumpSession();
}

void BridgeWindow::startStreamDiscovery(bool continue_recording_start) {
    if (discovery_worker_) {
        if (continue_recording_start) pending_recording_start_ = true;
        appendEvent(SessionComponent::Streams, EventSeverity::Information, "Stream discovery is already in progress");
        return;
    }
    updateConfigurationFromUi();
    if (ui_->preview_panel) mergeStreamInventory(ui_->preview_panel->streamInventory());
    pending_recording_start_ = pending_recording_start_ || continue_recording_start;
    ui_->discover_streams_button->setEnabled(false);
    ui_->stream_discovery_status_label->setText("Discovering...");
    discovery_worker_ = new vicon_lsl::StreamDiscoveryWorker(configuration_, this);
    vicon_lsl::StreamDiscoveryWorker* started = discovery_worker_;
    connect(started, &vicon_lsl::StreamDiscoveryWorker::lifecycleChanged, this, [this](ComponentLifecycleState s, const QString& d) {
        ui_->stream_discovery_status_label->setText(d);
        if (s == ComponentLifecycleState::Failed) appendEvent(SessionComponent::Streams, EventSeverity::Error, d);
    });
    connect(started, &vicon_lsl::StreamDiscoveryWorker::discoveryFinished, this, [this](QVector<StreamIdentity> discovered, const QString& warning) {
        QVector<StreamIdentity> reconciled;
        reconciled.reserve(discovered.size() + stream_inventory_.size());
        for (StreamIdentity& id : discovered) {
            auto old = std::find_if(stream_inventory_.cbegin(), stream_inventory_.cend(),
                [&id](const StreamIdentity& c) { return c.stableKey() == id.stableKey(); });
            if (old != stream_inventory_.cend()) {
                id.selected = old->selected;
                id.required = old->required;
                id.freshness_ms = old->freshness_ms;
                id.effective_rate = old->effective_rate;
            } else {
                auto conf = std::find_if(configuration_.recording_streams.cbegin(), configuration_.recording_streams.cend(),
                    [&id](const StreamBinding& b) { return b.matches(id); });
                if (conf != configuration_.recording_streams.cend()) {
                    id.selected = true;
                    id.required = conf->required;
                } else {
                    id.selected = configuration_.record_every_visible_stream;
                }
            }
            reconciled.push_back(std::move(id));
        }
        for (const StreamIdentity& old : stream_inventory_) {
            if (!old.selected && !old.required) continue;
            bool present = std::any_of(reconciled.cbegin(), reconciled.cend(),
                [&old](const StreamIdentity& c) { return c.stableKey() == old.stableKey(); });
            if (!present) {
                StreamIdentity missing = old;
                missing.present = false;
                missing.freshness_ms = -1;
                missing.warning = "Previously selected stream is not currently visible";
                reconciled.push_back(std::move(missing));
            }
        }
        stream_inventory_ = std::move(reconciled);
        populateStreamTable();
        populateBindingCombos();
        updateConfigurationFromUi();
        ui_->stream_discovery_status_label->setText(
            QString("Discovered %1 visible stream(s)").arg(std::count_if(stream_inventory_.cbegin(), stream_inventory_.cend(), [](const auto& s) { return s.present; })));
        appendEvent(SessionComponent::Streams, warning.isEmpty() ? EventSeverity::Information : EventSeverity::Warning,
                    warning.isEmpty() ? "Stream discovery completed" : "Stream discovery completed: " + warning);
    });
    connect(started, &QThread::finished, this, [this, started]() {
        const bool was_pending = pending_recording_start_;
        if (discovery_worker_ == started) discovery_worker_ = nullptr;
        started->deleteLater();
        ui_->discover_streams_button->setEnabled(!closing());
        if (was_pending && !closing()) completePendingRecordingStart();
        pumpSession();
    });
    appendEvent(SessionComponent::Streams, EventSeverity::Information, "Immediate pre-recording stream discovery started");
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
        const StreamIdentity& s = stream_inventory_[row];
        auto* record = new QTableWidgetItem();
        record->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        record->setCheckState(s.selected ? Qt::Checked : Qt::Unchecked);
        auto* required = new QTableWidgetItem();
        required->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        required->setCheckState(s.required ? Qt::Checked : Qt::Unchecked);
        ui_->stream_table->setItem(row, 0, record);
        ui_->stream_table->setItem(row, 1, required);
        const QStringList values = {
            s.role, s.name, s.type, s.source_id.isEmpty() ? "<missing>" : s.source_id,
            s.hostname, s.session_id, QString::number(s.channel_count),
            s.nominal_rate > 0.0 ? QString::number(s.nominal_rate, 'f', 1) : "irregular",
            s.effective_rate > 0.0 ? QString::number(s.effective_rate, 'f', 1) : "not measured",
            s.coordinate_frame.isEmpty() ? "<missing>" : s.coordinate_frame,
            s.present ? (!s.warning.isEmpty() ? s.warning
                         : s.freshness_ms < 0 ? QString("not measured")
                                              : QString("%1 ms").arg(s.freshness_ms))
                      : "Missing: " + s.warning,
        };
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]);
            if (!s.present || !s.schema_compatible || !s.warning.isEmpty()) item->setToolTip(s.warning);
            ui_->stream_table->setItem(row, col + 2, item);
        }
    }
    updateReadiness();
}

void BridgeWindow::mergeStreamInventory(const QVector<StreamIdentity>& streams) {
    for (const StreamIdentity& s : streams) {
        auto ex = std::find_if(stream_inventory_.begin(), stream_inventory_.end(),
            [&s](const StreamIdentity& c) { return c.stableKey() == s.stableKey(); });
        if (ex == stream_inventory_.end()) stream_inventory_.push_back(s);
        else {
            const bool selected = ex->selected, required = ex->required;
            *ex = s;
            ex->selected = selected;
            ex->required = required;
        }
    }
}

void BridgeWindow::selectBindingCombo(QComboBox* combo, const StreamBinding& binding) {
    if (!combo) return;
    int sel = -1;
    for (int i = 0; i < combo->count(); ++i) {
        const QString src = combo->itemData(i, Qt::UserRole).toString();
        const QString name = combo->itemData(i, Qt::UserRole + 1).toString();
        if ((!binding.source_id.isEmpty() && src == binding.source_id) || (binding.source_id.isEmpty() && name == binding.name)) {
            sel = i;
            break;
        }
    }
    if (sel < 0) {
        combo->addItem("Configured: " + binding.name + (binding.source_id.isEmpty() ? QString() : " [" + binding.source_id + "]"), binding.source_id);
        sel = combo->count() - 1;
        combo->setItemData(sel, binding.name, Qt::UserRole + 1);
    }
    combo->setCurrentIndex(sel);
}

void BridgeWindow::populateBindingCombos() {
    for (const BindingControl& c : bindingControls(*ui_, configuration_)) {
        const QSignalBlocker blocker(c.combo);
        c.combo->clear();
        for (const StreamIdentity& s : stream_inventory_) {
            if (!s.present || s.role != c.role) continue;
            c.combo->addItem(s.displayText(), s.source_id);
            const int idx = c.combo->count() - 1;
            c.combo->setItemData(idx, s.name, Qt::UserRole + 1);
            c.combo->setItemData(idx, s.stableKey(), Qt::UserRole + 2);
            c.combo->setItemData(idx, s.warning, Qt::ToolTipRole);
        }
        selectBindingCombo(c.combo, *c.binding);
    }
    ui_->marker_binding_combo->setEnabled(configuration_.preview_external_streams);
    ui_->segment_binding_combo->setEnabled(configuration_.preview_external_streams);
}

void BridgeWindow::updateBindingsFromUi() {
    auto update = [this](QComboBox* combo, QCheckBox* follow, StreamBinding& b) {
        if (!combo || combo->currentIndex() < 0) return;
        const QString key = combo->itemData(combo->currentIndex(), Qt::UserRole + 2).toString();
        auto found = std::find_if(stream_inventory_.cbegin(), stream_inventory_.cend(),
            [&key](const StreamIdentity& id) { return id.stableKey() == key; });
        if (found != stream_inventory_.cend()) {
            b.name = found->name;
            b.source_id = found->source_id;
            b.expected_channels = found->channel_count;
            b.expected_nominal_rate = found->nominal_rate;
            b.expected_coordinate_frame = found->coordinate_frame;
        } else {
            b.name = combo->itemData(combo->currentIndex(), Qt::UserRole + 1).toString();
            b.source_id = combo->itemData(combo->currentIndex(), Qt::UserRole).toString();
        }
        b.reconnection = follow->isChecked() ? StreamReconnectionMode::FollowName : StreamReconnectionMode::SourceIdentity;
    };
    for (const BindingControl& c : bindingControls(*ui_, configuration_)) update(c.combo, c.follow, *c.binding);
    configuration_.preview_external_streams = ui_->preview_external_streams_check->isChecked();
    if (!configuration_.preview_external_streams) configuration_.bindPreviewOutputs();
}

QVector<StreamIdentity> BridgeWindow::selectedStreams() const {
    QVector<StreamIdentity> result;
    for (StreamIdentity s : stream_inventory_) {
        if (!s.present) continue;
        if (configuration_.record_every_visible_stream) s.selected = true;
        if (s.selected) result.push_back(std::move(s));
    }
    return result;
}

SetupCheckResult BridgeWindow::runSetupCheck() const {
    SetupCheckResult result;
    result.completed_at = QDateTime::currentDateTimeUtc();
    auto add = [&result](SessionComponent comp, SetupCheckLevel lvl, bool passed, QString msg, QString act = {}) {
        result.items.push_back({comp, lvl, passed, std::move(msg), std::move(act)});
    };

    if (!configuration_.recorder_only_mode) {
        const bool bridge_ready = (bridge_lifecycle_ == ComponentLifecycleState::Running && bridgeStatusRecent());
        add(SessionComponent::Bridge, SetupCheckLevel::Required, bridge_ready,
            bridge_ready ? "Vicon bridge is ready" : "Vicon bridge is not sending current data", "Start the bridge and wait for data.");
    }

    const bool remote = configuration_.record_every_visible_stream;
    const bool recorder_ready = remote ? (labrecorder_client_->connectionState() == RecorderConnectionState::Connected)
                                       : !resolveSelectedStreamExecutable().isEmpty();
    const bool recorder_idle = (effectiveOperationState() == RecorderOperationState::Idle && effectiveRecordingState() != RecorderRecordingState::Recording);
    add(SessionComponent::Recorder, SetupCheckLevel::Required, recorder_ready && recorder_idle,
        recorder_ready && recorder_idle ? "Recorder is ready" : "Recorder is not ready", "Connect the recorder and stop any current recording.");

    add(SessionComponent::Path, SetupCheckLevel::Required, path_result_.valid(),
        path_result_.valid() ? "Recording folder is ready" : path_result_.firstError(), "Choose a writable recording folder.");
    for (const RecordingPathIssue& issue : path_result_.issues) {
        if (issue.level == RecordingPathIssueLevel::Warning) add(SessionComponent::Path, SetupCheckLevel::Warning, false, issue.message, issue.corrective_action);
    }

    for (const StreamBinding& b : configuration_.recording_streams) {
        if (!b.required) continue;
        auto found = std::find_if(stream_inventory_.cbegin(), stream_inventory_.cend(),
            [&b](const StreamIdentity& s) { return s.present && b.matches(s); });
        bool ready = (found != stream_inventory_.cend());
        if (ready) {
            ready = (found->freshness_ms < 0 || found->freshness_ms <= 2000) && found->schema_compatible &&
                    (b.expected_channels <= 0 || found->channel_count == b.expected_channels) &&
                    (b.expected_coordinate_frame.isEmpty() || found->coordinate_frame == b.expected_coordinate_frame);
        }
        const QString name = b.role.isEmpty() ? b.name : b.role;
        add(SessionComponent::Streams, SetupCheckLevel::Required, ready,
            ready ? name + " stream is ready" : name + " stream is not ready", "Start the saved stream source or choose the current one.");
    }

    if (!remote) {
        const bool selected = std::any_of(stream_inventory_.cbegin(), stream_inventory_.cend(), [](const auto& s) { return s.present && s.selected; });
        add(SessionComponent::Streams, SetupCheckLevel::Required, selected,
            selected ? "Recording streams are selected" : "No recording streams are selected", "Select at least one visible stream.");
    }

    if (configuration_.calibration_required) {
        const SessionCalibrationState st = ui_->preview_panel ? ui_->preview_panel->sessionCalibrationState() : SessionCalibrationState::Manual;
        const bool ready = ui_->preview_panel && ui_->preview_panel->stairModelLoaded() &&
                           (st == SessionCalibrationState::AutomaticSession || st == SessionCalibrationState::SavedProfile);
        add(SessionComponent::Calibration, SetupCheckLevel::Required, ready,
            ready ? "Calibration is ready" : "Calibration is not ready", "Load the stair model and apply a calibration.");
    }
    return result;
}

void BridgeWindow::populateSetupCheck(const SetupCheckResult& result) {
    ui_->setup_check_tree->clear();
    for (const SetupCheckItem& item : result.items) {
        QString details = item.message;
        if (!item.passed && !item.corrective_action.isEmpty()) details += " — " + item.corrective_action;
        auto* row = new QTreeWidgetItem({setupCheckLevelDisplayText(item.level), SessionEventLog::componentText(item.component), resultText(item), details});
        row->setToolTip(3, details);
        ui_->setup_check_tree->addTopLevelItem(row);
    }
    ui_->setup_check_override_button->setEnabled(result.hasRequiredFailures() && !result.override_used);
    updateDashboard();
}

void BridgeWindow::onRunSetupCheck() {
    updateConfigurationFromUi();
    validateRecordingPath(false);
    setup_check_ = runSetupCheck();
    populateSetupCheck(setup_check_);
    appendEvent(SessionComponent::Application, setup_check_.hasRequiredFailures() ? EventSeverity::Warning : EventSeverity::Information, setup_check_.summary());
}

void BridgeWindow::onStartRecording() {
    if (pending_recording_start_ || recordingActiveOrPending() || closing()) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning, "Duplicate recording Start was rejected");
        return;
    }
    saveSettings();
    filename_sync_timer_->stop();
    pending_recording_start_ = true;
    setup_check_start_waiting_ = false;
    stop_requested_ = false;
    startStreamDiscovery(true);
    refreshUi();
}

void BridgeWindow::completePendingRecordingStart() {
    if (!pending_recording_start_ || closing()) return;
    updateConfigurationFromUi();
    validateRecordingPath(true);
    setup_check_ = runSetupCheck();
    populateSetupCheck(setup_check_);
    if (setup_check_.hasRequiredFailures()) {
        setup_check_start_waiting_ = true;
        pending_recording_start_ = false;
        appendEvent(SessionComponent::Application, EventSeverity::Warning, "Fix the failed setup checks or enter a reason to record anyway");
        updateRecordingButtons();
        return;
    }
    beginRecordingAfterSetupCheck();
}

void BridgeWindow::onOverrideSetupCheck() {
    const QString reason = ui_->setup_check_override_reason_edit->text().trimmed();
    if (!setup_check_.hasRequiredFailures() || reason.isEmpty()) {
        appendEvent(SessionComponent::Application, EventSeverity::Warning, "Enter a reason before choosing Record Anyway");
        return;
    }
    setup_check_.override_used = true;
    setup_check_.override_reason = reason;
    appendEvent(SessionComponent::Application, EventSeverity::Warning, "Failed setup check accepted: " + reason);
    populateSetupCheck(setup_check_);
    if (setup_check_start_waiting_) {
        setup_check_start_waiting_ = false;
        pending_recording_start_ = true;
        beginRecordingAfterSetupCheck();
    }
}

void BridgeWindow::beginRecordingAfterSetupCheck() {
    if (!pending_recording_start_ || closing()) return;
    validateRecordingPath(true);
    if (!path_result_.valid()) {
        pending_recording_start_ = false;
        appendEvent(SessionComponent::Path, EventSeverity::Error, path_result_.firstError());
        return;
    }
    recording_inventory_ = selectedStreams();
    pending_recording_path_ = path_result_.absolute_path;
    verification_report_ = {};
    stop_requested_ = false;

    bool accepted = false;
    if (configuration_.record_every_visible_stream) {
        accepted = labrecorder_client_->startRecording(path_result_.normalized_fields, true);
    } else {
        QString err;
        accepted = recorder_process_->launchSelectedStreamRecorder(resolveSelectedStreamExecutable(), path_result_.absolute_path, recording_inventory_, &err);
        if (!accepted) appendEvent(SessionComponent::Recorder, EventSeverity::Error, "The selected-stream recorder could not start: " + err);
    }
    pending_recording_start_ = false;
    if (accepted) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Information,
                    "Recording started at " + pending_recording_path_ + " with " + QString::number(recording_inventory_.size()) + " stream(s)");
    } else {
        appendEvent(SessionComponent::Recorder, EventSeverity::Error, "The selected recorder could not start recording");
    }
    refreshUi();
}

void BridgeWindow::onStopRecording() {
    if (!recordingActiveOrPending()) {
        appendEvent(SessionComponent::Recorder, EventSeverity::Warning, "Stop ignored because nothing is recording");
        return;
    }
    pending_recording_start_ = false;
    setup_check_start_waiting_ = false;
    stop_requested_ = true;
    bool accepted = (recorder_process_->kind() == RecorderProcessKind::SelectedStreamRecorder && recorder_process_->ownsRunningProcess())
        ? recorder_process_->stopSelectedStreamRecording() : labrecorder_client_->stopRecording();
    appendEvent(SessionComponent::Recorder, accepted ? EventSeverity::Information : EventSeverity::Warning,
                accepted ? "The recorder was asked to stop" : "The recorder is already stopping or could not stop");
    refreshUi();
}

void BridgeWindow::onStartSession() {
    if (startingSession() || recordingActiveOrPending() || closing()) return;
    saveSettings();
    sequence_ = SessionSequence::StartingSession;
    stop_requested_ = false;
    appendEvent(SessionComponent::Application, EventSeverity::Information,
                configuration_.recorder_only_mode ? "Starting a recorder-only session" : "Starting the bridge, preview, and recorder");
    advanceGuidedStart();
}

void BridgeWindow::advanceGuidedStart() {
    if (!startingSession()) return;
    if (!configuration_.recorder_only_mode) {
        if (bridge_lifecycle_ == ComponentLifecycleState::Failed) {
            sequence_ = SessionSequence::None;
            appendEvent(SessionComponent::Bridge, EventSeverity::Error, "Session could not start because the bridge failed");
            return;
        }
        if (bridge_lifecycle_ != ComponentLifecycleState::Running) {
            if (!worker_) onStart();
            return;
        }
    }
    if (ui_->preview_panel) {
        const ComponentLifecycleState prev = ui_->preview_panel->lifecycleState();
        if (prev == ComponentLifecycleState::Failed) {
            sequence_ = SessionSequence::None;
            appendEvent(SessionComponent::Preview, EventSeverity::Error, "Session could not start because the preview failed");
            return;
        }
        if (prev != ComponentLifecycleState::Running) {
            if (prev != ComponentLifecycleState::Starting) ui_->preview_panel->startPreview();
            return;
        }
    }
    sequence_ = SessionSequence::None;
    onStartRecording();
}

void BridgeWindow::onStopSession() {
    if (stoppingSession() || closing()) return;
    sequence_ = SessionSequence::StoppingSession;
    stop_requested_ = true;
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Stopping the recorder, preview, and bridge");
    advanceGuidedStop();
}

void BridgeWindow::advanceGuidedStop() {
    if (!stoppingSession()) return;
    if (recordingActiveOrPending()) {
        if (effectiveOperationState() != RecorderOperationState::Stopping) onStopRecording();
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
    if (recorder_process_->ownsRunningProcess() && recorder_process_->kind() == RecorderProcessKind::GraphicalRecorder) {
        recorder_process_->endOwnedProcess();
        return;
    }
    sequence_ = SessionSequence::None;
    stop_requested_ = false;
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Session stopped");
    updateDashboard();
}

void BridgeWindow::requestVerification() {
    if (pending_recording_path_.isEmpty() || verifier_ || verification_waiting_for_file_) return;
    verification_report_.state = RecordingVerificationState::Running;
    verification_waiting_for_file_ = true;
    verification_file_elapsed_.restart();
    verification_file_timer_->start();
    appendEvent(SessionComponent::Verification, EventSeverity::Information, "Waiting for the recorder to finish writing the file before checking it");
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
        EventSeverity::Error, "output-not-found", {},
        "The exact expected output did not appear within 15 seconds after Stop",
        "Keep the recorder open, confirm its status, and inspect the destination without overwriting it.",
    });
    finishVerification(report);
}

void BridgeWindow::startVerifier() {
    if (verifier_ || pending_recording_path_.isEmpty()) return;
    verifier_ = new vicon_lsl::gui::RecordingVerifier({pending_recording_path_, recording_inventory_,
        configuration_.recording_streams, configuration_.record_every_visible_stream}, this);
    auto* started = verifier_;
    connect(started, &vicon_lsl::gui::RecordingVerifier::progressChanged, this, [this](const QString& stage, int pct, const QString& d) {
        ui_->verification_state_label->setText(QString("%1 %2% — %3").arg(stage).arg(pct).arg(d));
    });
    connect(started, &vicon_lsl::gui::RecordingVerifier::lifecycleChanged, this, [this](ComponentLifecycleState s, const QString& d) {
        if (s == ComponentLifecycleState::Failed) appendEvent(SessionComponent::Verification, EventSeverity::Error, d);
        pumpSession();
    });
    connect(started, &vicon_lsl::gui::RecordingVerifier::verificationFinished, this, &BridgeWindow::finishVerification);
    connect(started, &QThread::finished, this, [this, started]() {
        if (verifier_ == started) verifier_ = nullptr;
        started->deleteLater();
        pumpSession();
    });
    appendEvent(SessionComponent::Verification, EventSeverity::Information, "Recording file check started");
    started->start();
}

void BridgeWindow::finishVerification(const vicon_lsl::gui::RecordingVerificationReport& report) {
    verification_report_ = report;
    stop_requested_ = false;
    if (report.state == RecordingVerificationState::NotRun) {
        updateDashboard();
        pumpSession();
        return;
    }
    appendEvent(SessionComponent::Verification, report.hasErrors() ? EventSeverity::Error : (report.hasWarnings() ? EventSeverity::Warning : EventSeverity::Information), report.summary());
    const bool verified = report.state == RecordingVerificationState::Verified ||
                          report.state == RecordingVerificationState::VerifiedWithWarnings;
    if (configuration_.automatic_run_increment && verified && QFileInfo::exists(report.path)) {
        ui_->run_spin->setValue(ui_->run_spin->value() + 1);
        appendEvent(SessionComponent::Path, EventSeverity::Information, "Run incremented after a successful file check");
    }
    updateDashboard();
    pumpSession();
}

QJsonObject BridgeWindow::diagnosticBundle() const {
    const ComponentLifecycleState prev = ui_->preview_panel ? ui_->preview_panel->lifecycleState() : ComponentLifecycleState::Idle;
    const SessionCalibrationState cal = ui_->preview_panel ? ui_->preview_panel->sessionCalibrationState() : SessionCalibrationState::Manual;
    return {
        {"formatVersion", 1},
        {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"applicationVersion", QCoreApplication::applicationVersion()},
        {"configuration", configuration_.toJson()},
        {"session", QJsonObject{
            {"bridge", componentLifecycleStateText(bridge_lifecycle_)},
            {"preview", componentLifecycleStateText(prev)},
            {"recorder", stateDetail(labrecorder_client_->connectionState(), effectiveRecordingState(), effectiveOperationState())},
            {"calibration", calibrationStateText(cal)},
            {"file", fileStateText(file_state_)},
            {"verification", verificationStateText(verification_report_.state)},
            {"lastSetupCheck", setup_check_.toJson()},
            {"events", event_log_.toJson()},
            {"lastError", event_log_.lastError()},
        }},
        {"visibleStreamInventory", streamInventoryJson(stream_inventory_)},
        {"recordingStreamInventory", streamInventoryJson(recording_inventory_)},
        {"recordingPath", path_result_.absolute_path},
        {"recordingPathSummary", path_result_.summary()},
        {"verification", verification_report_.toJson()},
        {"recorderProcessState", recorderProcessStateText(recorder_process_->state())},
        {"recorderProcessOutput", QString::fromLocal8Bit(recorder_process_->boundedOutput())},
        {"rates", QJsonObject{
            {"bridgeEffectiveHz", bridge_effective_rate_},
            {"previewSkippedFrames", static_cast<double>(preview_replaced_frames_)},
            {"previewCombinedSamples", static_cast<double>(preview_coalesced_samples_)},
            {"previewLatencyMs", static_cast<double>(preview_latency_ms_)},
        }},
        {"containsRecordingSamples", false},
    };
}

void BridgeWindow::onCopyDiagnostics() {
    QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(diagnosticBundle()).toJson(QJsonDocument::Indented)));
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Session details copied without recorded samples");
}

void BridgeWindow::onExportDiagnostics() {
    const QString path = QFileDialog::getSaveFileName(this, "Export Session Details",
        QDir(ui_state_.recent_diagnostic_directory).filePath("session-details.json"), "Session details (*.json)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(diagnosticBundle()).toJson(QJsonDocument::Indented)) < 0) {
        appendEvent(SessionComponent::Application, EventSeverity::Error, "Could not export session details: " + file.errorString());
        return;
    }
    file.close();
    ui_state_.recent_diagnostic_directory = QFileInfo(path).absolutePath();
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Session details exported to " + path);
}

void BridgeWindow::onShowVerificationDetails() {
    if (verification_report_.state == RecordingVerificationState::NotRun) return;
    QStringList lines{verification_report_.summary()};
    for (const auto& f : verification_report_.findings) {
        lines.push_back(SessionEventLog::severityText(f.severity) + ": " + f.message + (f.corrective_action.isEmpty() ? QString() : "\n  Action: " + f.corrective_action));
    }
    QMessageBox::information(this, "Recording File Check", lines.join("\n\n"));
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
    if (closing()) {
        updateShutdownStatus();
        return;
    }
    sequence_ = SessionSequence::Closing;
    pending_recording_start_ = false;
    setup_check_start_waiting_ = false;
    saveSettings();
    filename_sync_timer_->stop();
    labrecorder_retry_timer_->stop();
    ui_->start_session_button->setEnabled(false);
    ui_->stop_session_button->setEnabled(false);
    ui_->run_setup_check_button->setEnabled(false);
    ui_->start_button->setEnabled(false);
    ui_->start_recording_button->setEnabled(false);
    ui_->discover_streams_button->setEnabled(false);
    ui_->setBridgeInputsEnabled(false);

    close_started_ms_ = monotonic_clock_.elapsed();
    owned_process_end_requested_ = false;
    recorder_connection_loss_reported_ = false;

    labrecorder_client_->beginShutdown();
    if (recorder_process_->kind() == RecorderProcessKind::SelectedStreamRecorder && recorder_process_->ownsRunningProcess()) {
        recorder_process_->stopSelectedStreamRecording();
    }
    if (ui_->preview_panel) ui_->preview_panel->requestShutdown();
    cancelStreamDiscovery();
    if (verifier_) verifier_->cancel();
    verification_waiting_for_file_ = false;
    verification_file_timer_->stop();
    if (worker_) worker_->stopBridge();
    appendEvent(SessionComponent::Application, EventSeverity::Information, "Closing safely");
    close_poll_timer_->start();
    updateShutdownStatus();
}

void BridgeWindow::updateShutdownStatus() {
    if (!closing()) return;
    const qint64 now_ms = monotonic_clock_.elapsed();
    const bool bridge_done = (worker_ == nullptr);
    const bool preview_done = (!ui_->preview_panel || ui_->preview_panel->shutdownReady());
    const bool file_done = (discovery_worker_ == nullptr && (!ui_->preview_panel || !ui_->preview_panel->fileLoadActive()));
    const bool verification_done = (verifier_ == nullptr && !verification_waiting_for_file_);

    bool remote_safe = labrecorder_client_->shutdownSettledSafely();
    const bool recorder_connection_lost = !remote_safe && labrecorder_client_->shutdownReady() &&
        (labrecorder_client_->connectionState() == RecorderConnectionState::Disconnected || labrecorder_client_->connectionState() == RecorderConnectionState::Error);
    if (recorder_process_->kind() == RecorderProcessKind::SelectedStreamRecorder) {
        remote_safe = !recorder_process_->ownsRunningProcess();
    }
    const bool owns_process = recorder_process_->ownsRunningProcess();
    const bool recorder_deadline = close_started_ms_ >= 0 && now_ms - close_started_ms_ >= kRecorderStopDeadlineMs;
    if (owns_process && !owned_process_end_requested_ && (remote_safe || recorder_deadline)) {
        owned_process_end_requested_ = true;
        recorder_process_->endOwnedProcess();
        appendEvent(SessionComponent::Recorder, recorder_deadline && !remote_safe ? EventSeverity::Warning : EventSeverity::Information,
                    recorder_deadline && !remote_safe ? "The recorder did not stop in time; closing only the recorder started here"
                                                      : "Recording has stopped; closing the recorder started here");
    }
    const bool recorder_owned = recorder_process_->ownsRunningProcess();
    const bool recorder_done = remote_safe && !recorder_owned;
    const bool recorder_lost_and_external = recorder_connection_lost && !recorder_owned;
    if (recorder_lost_and_external && !recorder_connection_loss_reported_) {
        recorder_connection_loss_reported_ = true;
        appendEvent(SessionComponent::Recorder, EventSeverity::Error, "Recorder connection was lost while closing");
    }

    QStringList waiting;
    if (!bridge_done) waiting.push_back("bridge");
    if (!preview_done) waiting.push_back("preview");
    if (!file_done) waiting.push_back("file loading");
    if (!verification_done) waiting.push_back("file check");
    if (!recorder_done && !recorder_lost_and_external) waiting.push_back("recorder");
    ui_->shutdown_label->setText(waiting.isEmpty() ? "Ready to close" : "Closing: waiting for " + waiting.join(", "));
    updateDashboard();
    if (!waiting.isEmpty() || close_finalizing_) return;
    close_poll_timer_->stop();
    close_finalizing_ = true;
    sequence_ = SessionSequence::None;
    saveUiState();
    QTimer::singleShot(0, this, [this]() { QWidget::close(); });
}
