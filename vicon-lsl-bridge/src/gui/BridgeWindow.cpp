#include "BridgeWindow.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

#include <exception>
#include "gui/BridgeWindowSettings.h"
#include "gui/BridgeWindowUi.h"
#include "gui/LabRecorderFilenamePolicy.h"
#include "gui/LabRecorderRuntimePolicy.h"
#include "gui/PreviewPanel.h"

namespace {

constexpr qint64 kBridgeCloseTimeoutMs = 4000;
constexpr qint64 kRecordingCloseTimeoutMs = 15000;

} // namespace

// --- BridgeWorker ---

BridgeWorker::BridgeWorker(const Config& config, QObject* parent)
    : QThread(parent), bridge_(std::make_unique<ViconLSLBridge>(config)) {}

void BridgeWorker::run() {
    try {
        bridge_->setStatusCallback([this](const BridgeStatus& status) {
            emit statusUpdate(static_cast<int>(status.state),
                              static_cast<unsigned long long>(status.marker_count),
                              static_cast<unsigned long long>(status.segment_count),
                              status.frame_count,
                              QString::fromStdString(status.message));
        });
        bridge_->run();
    } catch (const std::exception& ex) {
        emit failed(QString::fromUtf8(ex.what()));
    } catch (...) {
        emit failed("Unknown bridge worker failure");
    }
}

void BridgeWorker::stopBridge() {
    bridge_->stop();
}

// --- BridgeWindow ---

BridgeWindow::BridgeWindow(QWidget* parent, bool enable_preview) : QWidget(parent) {
    ui_ = vicon_lsl::gui_detail::buildBridgeWindowUi(this, enable_preview);
    auto* browse_root_button = ui_->browse_root_button;
    auto* browse_labrecorder_button = ui_->browse_labrecorder_button;

    connect(ui_->start_button, &QPushButton::clicked, this, &BridgeWindow::onStart);
    connect(ui_->stop_button, &QPushButton::clicked, this, &BridgeWindow::onStop);
    connect(ui_->start_session_button, &QPushButton::clicked,
            this, &BridgeWindow::onStartSession);
    connect(ui_->stop_session_button, &QPushButton::clicked,
            this, &BridgeWindow::onStopSession);
    connect(browse_root_button, &QPushButton::clicked, this, &BridgeWindow::onBrowseStudyRoot);
    connect(browse_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onBrowseLabRecorder);
    connect(ui_->launch_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onLaunchLabRecorder);
    connect(ui_->connect_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onConnectLabRecorder);
    connect(ui_->refresh_streams_button, &QPushButton::clicked, this, &BridgeWindow::onRefreshLabRecorder);
    connect(ui_->start_recording_button, &QPushButton::clicked, this, &BridgeWindow::onStartRecording);
    connect(ui_->stop_recording_button, &QPushButton::clicked, this, &BridgeWindow::onStopRecording);
    connect(&labrecorder_client_, &LabRecorderClient::connectionStateChanged, this,
            [this](RecorderConnectionState state, const QString& message) {
                if (state == RecorderConnectionState::Connected && labrecorder_retry_timer_) {
                    labrecorder_retry_timer_->stop();
                }
                if (!message.isEmpty()) {
                    setLabRecorderStatus(message);
                }
                ui_->connect_labrecorder_button->setEnabled(
                    state != RecorderConnectionState::Connecting);
                updateRecordingButtons();
                updateReadiness();
                scheduleFilenameSync();
                if (session_state_ == SessionState::Starting &&
                    state == RecorderConnectionState::Error) {
                    session_state_ = SessionState::Error;
                    setSessionStatus(message.isEmpty()
                        ? "Could not connect to LabRecorder."
                        : message);
                } else if (session_state_ == SessionState::Starting) {
                    advanceSessionStart();
                } else if (session_state_ == SessionState::Stopping) {
                    advanceSessionStop();
                }
            });
    connect(&labrecorder_client_, &LabRecorderClient::recordingStateChanged, this,
            [this](RecorderRecordingState state) {
                updateRecordingButtons();
                updateReadiness();
                if (state == RecorderRecordingState::Recording && filename_sync_timer_) {
                    filename_sync_timer_->stop();
                } else {
                    scheduleFilenameSync();
                }
                if (state == RecorderRecordingState::Stopped &&
                    session_state_ == SessionState::Recording) {
                    session_state_ = SessionState::Ready;
                    setSessionStatus("Recording stopped; bridge and preview are still running.");
                }
                updateSessionButtons();
            });
    connect(&labrecorder_client_, &LabRecorderClient::commandFinished, this,
            [this](const QString& operation, bool ok, const QString& message) {
                setLabRecorderStatus(ok
                    ? operation + " completed"
                    : operation + " failed: " + message);
                updateRecordingButtons();
                updateReadiness();
                if (session_state_ == SessionState::Starting) {
                    if (operation == "start recording") {
                        if (!ok) {
                            session_state_ = SessionState::Error;
                            setSessionStatus("Recording could not start: " + message);
                        } else {
                            session_state_ = SessionState::Recording;
                            setSessionStatus("Recording");
                        }
                    } else {
                        advanceSessionStart();
                    }
                } else if (session_state_ == SessionState::Stopping) {
                    advanceSessionStop();
                }
                if (close_pending_ && !labrecorder_client_.busy() &&
                    labrecorder_client_.recordingState() ==
                        RecorderRecordingState::Recording) {
                    labrecorder_client_.stopRecording();
                }
                updateSessionButtons();
            });

    if (ui_->preview_panel) {
        connect(ui_->preview_panel, &vicon_lsl::PreviewPanel::runningChanged,
                this, [this](bool) {
                    updateSessionButtons();
                    if (session_state_ == SessionState::Stopping) {
                        advanceSessionStop();
                    }
                });
    }

    filename_sync_timer_ = new QTimer(this);
    filename_sync_timer_->setSingleShot(true);
    filename_sync_timer_->setInterval(300);
    connect(filename_sync_timer_, &QTimer::timeout,
            this, &BridgeWindow::syncFilenameToLabRecorder);

    const auto preview_update = [this]() {
        updateFilenamePreview();
        scheduleFilenameSync();
    };
    connect(ui_->study_root_edit, &QLineEdit::textChanged, this, preview_update);
    connect(ui_->filename_template_edit, &QLineEdit::textChanged, this, preview_update);
    connect(ui_->participant_edit, &QLineEdit::textChanged, this, preview_update);
    connect(ui_->session_edit, &QLineEdit::textChanged, this, preview_update);
    connect(ui_->task_edit, &QLineEdit::textChanged, this, preview_update);
    connect(ui_->run_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, preview_update);
    connect(ui_->acquisition_edit, &QLineEdit::textChanged, this, preview_update);
    connect(ui_->modality_edit, &QLineEdit::textChanged, this, preview_update);

    ui_->applySettings(vicon_lsl::gui_detail::loadBridgeWindowSettings());
    updateFilenamePreview();
    status_timer_.start();
    status_stale_timer_ = new QTimer(this);
    status_stale_timer_->setInterval(500);
    connect(status_stale_timer_, &QTimer::timeout, this, &BridgeWindow::onStatusStaleCheck);
    status_stale_timer_->start();
    labrecorder_retry_timer_ = new QTimer(this);
    labrecorder_retry_timer_->setInterval(250);
    connect(labrecorder_retry_timer_, &QTimer::timeout,
            this, &BridgeWindow::onLabRecorderRetry);
    close_poll_timer_ = new QTimer(this);
    close_poll_timer_->setInterval(50);
    connect(close_poll_timer_, &QTimer::timeout, this, &BridgeWindow::onClosePoll);
    updateRecordingButtons();
    updateReadiness();
    updateSessionButtons();

    QTimer::singleShot(0, this, &BridgeWindow::beginLabRecorderStartup);
}

BridgeWindow::~BridgeWindow() {
    if (close_poll_timer_) {
        close_poll_timer_->stop();
    }
    stopOwnedLabRecorder();
    if (worker_) {
        worker_->stopBridge();
        worker_->wait();
    }
}

void BridgeWindow::onStart() {
    if (worker_) {
        return;
    }
    Config config;
    config.vicon_server = ui_->server_edit->text().toStdString();
    config.marker_stream_name = ui_->marker_stream_edit->text().toStdString();
    config.segment_stream_name = ui_->segment_stream_edit->text().toStdString();

    saveSettings();
    ui_->start_button->setEnabled(false);
    ui_->stop_button->setEnabled(true);
    ui_->setBridgeInputsEnabled(false);

    worker_ = new BridgeWorker(config, this);
    connect(worker_, &BridgeWorker::statusUpdate,
            this, &BridgeWindow::onStatusUpdate);
    connect(worker_, &BridgeWorker::failed, this,
            [this](const QString& message) {
                ui_->last_error_label->setText(message);
                ui_->status_label->setText("Bridge stopped - " + message);
                if (session_state_ == SessionState::Starting ||
                    session_state_ == SessionState::Recording) {
                    session_state_ = SessionState::Error;
                    setSessionStatus("Bridge stopped: " + message);
                }
            });
    connect(worker_, &BridgeWorker::finished,
            this, &BridgeWindow::onWorkerFinished);
    worker_->start();
    updateSessionButtons();
}

void BridgeWindow::onStop() {
    ui_->stop_button->setEnabled(false);
    if (worker_) {
        worker_->stopBridge();
    }
    updateSessionButtons();
}

void BridgeWindow::onStartSession() {
    if (session_state_ == SessionState::Starting ||
        session_state_ == SessionState::Recording ||
        session_state_ == SessionState::Stopping) {
        return;
    }

    const QString error = filenameValidationError();
    if (!error.isEmpty()) {
        setSessionStatus(error);
        return;
    }
    if (labrecorder_client_.recordingState() == RecorderRecordingState::Recording) {
        setSessionStatus("LabRecorder is already recording.");
        return;
    }
    if (ui_->preview_panel && ui_->preview_panel->recordingIsLoading()) {
        setSessionStatus("Wait for the recording preview to finish loading.");
        return;
    }

    session_state_ = SessionState::Starting;
    if (filename_sync_timer_) {
        filename_sync_timer_->stop();
    }
    saveSettings();
    setSessionStatus("Starting bridge and preview...");

    if (!worker_) {
        onStart();
    }
    if (ui_->preview_panel && !ui_->preview_panel->isRunning()) {
        ui_->preview_panel->startSessionPreview();
    }
    if (labrecorder_client_.connectionState() != RecorderConnectionState::Connected &&
        labrecorder_client_.connectionState() != RecorderConnectionState::Connecting &&
        !labrecorder_client_.busy()) {
        onConnectLabRecorder();
    }
    advanceSessionStart();
}

void BridgeWindow::advanceSessionStart() {
    if (session_state_ != SessionState::Starting ||
        !bridge_streaming_ || labrecorder_client_.busy()) {
        return;
    }
    if (labrecorder_client_.connectionState() != RecorderConnectionState::Connected) {
        setSessionStatus("Waiting for LabRecorder...");
        return;
    }

    const QString error = filenameValidationError();
    if (!error.isEmpty()) {
        session_state_ = SessionState::Error;
        setSessionStatus(error);
        return;
    }

    setSessionStatus("Starting recording...");
    if (!labrecorder_client_.startRecording(filenameFields(), true)) {
        session_state_ = SessionState::Error;
    }
    updateSessionButtons();
}

void BridgeWindow::onStopSession() {
    if (session_state_ == SessionState::Stopping) {
        return;
    }
    session_state_ = SessionState::Stopping;
    setSessionStatus("Stopping recording...");
    advanceSessionStop();
}

void BridgeWindow::advanceSessionStop() {
    if (session_state_ != SessionState::Stopping || labrecorder_client_.busy()) {
        return;
    }
    if (labrecorder_client_.recordingState() == RecorderRecordingState::Recording) {
        labrecorder_client_.stopRecording();
        return;
    }

    if (ui_->preview_panel && ui_->preview_panel->isRunning()) {
        ui_->preview_panel->stopSessionPreview();
    }
    if (worker_) {
        onStop();
    }

    const bool preview_stopped =
        !ui_->preview_panel || !ui_->preview_panel->isRunning();
    if (!worker_ && preview_stopped) {
        session_state_ = SessionState::Ready;
        setSessionStatus("Ready");
    } else {
        setSessionStatus("Stopping preview and bridge...");
    }
    updateSessionButtons();
}

void BridgeWindow::setSessionStatus(const QString& status) {
    ui_->session_status_label->setText(status);
    updateSessionButtons();
}

void BridgeWindow::updateSessionButtons() {
    const bool preview_running =
        ui_->preview_panel && ui_->preview_panel->isRunning();
    const bool active = worker_ || preview_running || labrecorder_client_.busy() ||
        labrecorder_client_.recordingState() == RecorderRecordingState::Recording;
    const bool can_start = session_state_ == SessionState::Ready ||
        (session_state_ == SessionState::Error && !active);
    ui_->start_session_button->setEnabled(can_start);
    ui_->stop_session_button->setEnabled(
        active || session_state_ != SessionState::Ready);
}

void BridgeWindow::onBrowseStudyRoot() {
    QString root = QFileDialog::getExistingDirectory(this, "Select Study Root", ui_->study_root_edit->text());
    if (!root.isEmpty()) {
        ui_->study_root_edit->setText(QDir::toNativeSeparators(root));
    }
}

void BridgeWindow::onBrowseLabRecorder() {
    QString path = QFileDialog::getOpenFileName(this, "Select LabRecorder", ui_->labrecorder_executable_edit->text());
    if (!path.isEmpty()) {
        ui_->labrecorder_executable_edit->setText(QDir::toNativeSeparators(path));
    }
}

void BridgeWindow::onLaunchLabRecorder() {
    const QString executable = ui_->labrecorder_executable_edit->text().trimmed();
    if (executable.isEmpty()) {
        setLabRecorderStatus("Set a LabRecorder executable path before launching.");
        return;
    }
    if (!QFileInfo::exists(executable)) {
        setLabRecorderStatus("LabRecorder executable does not exist: " + executable);
        return;
    }

    if (!labrecorder_process_) {
        labrecorder_process_ = std::make_unique<QProcess>();
        connect(labrecorder_process_.get(), &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError) {
                    setLabRecorderStatus("LabRecorder process error: " +
                                         labrecorder_process_->errorString());
                });
        connect(labrecorder_process_.get(), &QProcess::finished, this,
                [this](int, QProcess::ExitStatus) {
                    if (labrecorder_process_owned_) {
                        labrecorder_process_owned_ = false;
                    }
                    setLabRecorderStatus("LabRecorder process exited.");
                    updateReadiness();
                });
    }
    if (labrecorder_process_->state() != QProcess::NotRunning) {
        setLabRecorderStatus("LabRecorder process is already running.");
        return;
    }

    labrecorder_process_owned_ = true;
    labrecorder_process_->start(executable);
    if (!labrecorder_process_->waitForStarted(2000)) {
        labrecorder_process_owned_ = false;
        setLabRecorderStatus("Failed to launch LabRecorder: " + labrecorder_process_->errorString());
        return;
    }
    setLabRecorderStatus("LabRecorder launched. Waiting for it to become ready.");
    labrecorder_retry_elapsed_.restart();
    labrecorder_retry_timer_->start();
    onLabRecorderRetry();
}

void BridgeWindow::onConnectLabRecorder() {
    labrecorder_retry_timer_->stop();
    saveSettings();
    labrecorder_client_.connectToServer(
        ui_->labrecorder_host_edit->text(),
        static_cast<quint16>(ui_->labrecorder_port_spin->value()));
}

void BridgeWindow::beginLabRecorderStartup() {
    const QString executable = LabRecorderRuntimePolicy::resolveExecutable(
        ui_->labrecorder_executable_edit->text(),
        QCoreApplication::applicationDirPath());
    if (executable.isEmpty() || !QFileInfo::exists(executable)) {
        setLabRecorderStatus("LabRecorder executable not found; use Browse or Launch.");
        updateReadiness();
        return;
    }
    ui_->labrecorder_executable_edit->setText(executable);
    onLaunchLabRecorder();
}

void BridgeWindow::onLabRecorderRetry() {
    const RecorderConnectionState state = labrecorder_client_.connectionState();
    if (state == RecorderConnectionState::Connected) {
        labrecorder_retry_timer_->stop();
        return;
    }
    const qint64 elapsed = labrecorder_retry_elapsed_.isValid()
        ? labrecorder_retry_elapsed_.elapsed()
        : LabRecorderRuntimePolicy::RetryTimeoutMs;
    if (LabRecorderRuntimePolicy::retryExpired(elapsed)) {
        labrecorder_retry_timer_->stop();
        setLabRecorderStatus("LabRecorder did not become ready within 15 seconds.");
        if (session_state_ == SessionState::Starting) {
            session_state_ = SessionState::Error;
            setSessionStatus("LabRecorder did not become ready.");
        }
        return;
    }
    if (!LabRecorderRuntimePolicy::shouldAttemptConnection(state, elapsed)) {
        return;
    }
    labrecorder_client_.connectToServer(
        ui_->labrecorder_host_edit->text(),
        static_cast<quint16>(ui_->labrecorder_port_spin->value()),
        200);
}

void BridgeWindow::onRefreshLabRecorder() {
    if (labrecorder_client_.refreshStreams()) {
        setLabRecorderStatus("Refreshing streams...");
    }
}

void BridgeWindow::onStartRecording() {
    saveSettings();
    if (filename_sync_timer_) {
        filename_sync_timer_->stop();
    }
    const QString validation_error = filenameValidationError();
    if (!validation_error.isEmpty()) {
        setLabRecorderStatus(validation_error);
        return;
    }

    if (labrecorder_client_.startRecording(filenameFields(), true)) {
        setLabRecorderStatus("Starting recording...");
    }
}

void BridgeWindow::onStopRecording() {
    if (labrecorder_client_.stopRecording()) {
        setLabRecorderStatus("Stopping recording...");
    }
}

void BridgeWindow::updateFilenamePreview() {
    if (ui_->filename_preview_label) {
        const QString preview =
            LabRecorderFilenamePolicy::renderedFilenamePreview(filenameFields());
        ui_->filename_preview_label->setText(preview);
        ui_->filename_preview_label->setToolTip(preview);
        ui_->filename_preview_label->setCursorPosition(0);
    }
    updateRecordingButtons();
    updateReadiness();
}

void BridgeWindow::syncFilenameToLabRecorder() {
    const RecorderConnectionState connection_state = labrecorder_client_.connectionState();
    const RecorderRecordingState recording_state = labrecorder_client_.recordingState();
    if (labrecorder_client_.busy() ||
        !LabRecorderRuntimePolicy::canStartRecording(connection_state, recording_state) ||
        !isFilenameValid()) {
        return;
    }

    if (labrecorder_client_.updateFilename(filenameFields())) {
        setLabRecorderStatus("Recording path updated.");
    }
}

void BridgeWindow::onStatusStaleCheck() {
    if (!bridge_streaming_ || !have_previous_status_) {
        return;
    }

    const qint64 now_ms = status_timer_.elapsed();
    if (now_ms - previous_status_ms_ <= 3000 || bridge_status_stale_) {
        return;
    }

    bridge_status_stale_ = true;
    ui_->frame_rate_label->setText("0.0 Hz");
    ui_->status_label->setText(ui_->status_label->text() + " - stale status");
    updateReadiness();
}

void BridgeWindow::closeEvent(QCloseEvent* event) {
    if (close_finalizing_) {
        event->accept();
        return;
    }
    if (!close_pending_) {
        close_pending_ = true;
        close_elapsed_.restart();
        const bool stopping_recording =
            labrecorder_client_.recordingState() == RecorderRecordingState::Recording ||
            labrecorder_client_.startingRecording();
        if (ui_->preview_panel) {
            ui_->preview_panel->stopSessionPreview();
        }
        if (worker_) {
            onStop();
            ui_->status_label->setText("Stopping bridge before closing...");
        }
        if (!labrecorder_client_.busy() &&
            labrecorder_client_.recordingState() == RecorderRecordingState::Recording) {
            labrecorder_client_.stopRecording();
        }
        if (stopping_recording) {
            setLabRecorderStatus("Stopping recording before closing...");
        }
        if (close_poll_timer_) {
            close_poll_timer_->start();
        }
    }
    event->ignore();
    finishCloseIfReady();
}

void BridgeWindow::onClosePoll() {
    finishCloseIfReady();
}

void BridgeWindow::finishCloseIfReady() {
    if (!close_pending_) {
        return;
    }
    const qint64 elapsed_ms = close_elapsed_.elapsed();
    const bool bridge_done = worker_ == nullptr;
    const bool preview_done =
        !ui_->preview_panel || !ui_->preview_panel->isRunning();
    const bool recording_done = !labrecorder_client_.busy() &&
        labrecorder_client_.recordingState() != RecorderRecordingState::Recording;
    if ((!bridge_done || !preview_done) && elapsed_ms < kBridgeCloseTimeoutMs) {
        return;
    }
    if (!recording_done && elapsed_ms < kRecordingCloseTimeoutMs) {
        return;
    }
    if (close_poll_timer_) {
        close_poll_timer_->stop();
    }
    close_pending_ = false;
    close_finalizing_ = true;
    stopOwnedLabRecorder();
    QTimer::singleShot(0, this, [this]() { QWidget::close(); });
}

void BridgeWindow::stopOwnedLabRecorder() {
    if (!labrecorder_process_owned_ || !labrecorder_process_) {
        return;
    }
    const bool process_running = labrecorder_process_->state() != QProcess::NotRunning;
    if (process_running) {
        labrecorder_process_->terminate();
        if (!labrecorder_process_->waitForFinished(1000)) {
            labrecorder_process_->kill();
            labrecorder_process_->waitForFinished(500);
        }
    }
    labrecorder_process_owned_ = false;
}

void BridgeWindow::onStatusUpdate(int state, unsigned long long markers, unsigned long long segments,
                                   unsigned int frames, const QString& message) {
    auto bridge_state = static_cast<BridgeState>(state);

    QString state_text;
    switch (bridge_state) {
        case BridgeState::Disconnected: state_text = "Disconnected"; break;
        case BridgeState::Connecting:   state_text = "Connecting..."; break;
        case BridgeState::Streaming:    state_text = "Streaming"; break;
        case BridgeState::Stopped:      state_text = "Stopped"; break;
    }
    if (!message.isEmpty()) {
        state_text += " - " + message;
    }

    ui_->status_label->setText(state_text);
    bridge_streaming_ = bridge_state == BridgeState::Streaming;
    bridge_status_stale_ = false;
    ui_->markers_label->setText(QString::number(markers));
    ui_->segments_label->setText(QString::number(segments));
    ui_->frames_label->setText(QString::number(frames));
    ui_->last_error_label->setText(message.isEmpty() ? "-" : message);

    qint64 now_ms = status_timer_.elapsed();
    if (have_previous_status_) {
        qint64 delta_ms = now_ms - previous_status_ms_;
        if (delta_ms > 0) {
            double seconds = static_cast<double>(delta_ms) / 1000.0;
            unsigned int frame_delta = frames >= previous_frames_ ? frames - previous_frames_ : 0;
            double frame_rate = static_cast<double>(frame_delta) / seconds;
            ui_->frame_rate_label->setText(QString::number(frame_rate, 'f', 1) + " Hz");
        }
    }
    previous_status_ms_ = now_ms;
    previous_frames_ = frames;
    have_previous_status_ = true;
    updateReadiness();
    if (session_state_ == SessionState::Starting) {
        advanceSessionStart();
    }
    updateSessionButtons();
}

void BridgeWindow::onWorkerFinished() {
    ui_->start_button->setEnabled(true);
    ui_->stop_button->setEnabled(false);
    ui_->setBridgeInputsEnabled(true);
    bridge_streaming_ = false;
    bridge_status_stale_ = false;
    have_previous_status_ = false;
    ui_->frame_rate_label->setText("0.0 Hz");
    updateReadiness();

    worker_->deleteLater();
    worker_ = nullptr;
    updateSessionButtons();
    if (session_state_ == SessionState::Stopping) {
        advanceSessionStop();
    } else if (session_state_ == SessionState::Starting ||
               session_state_ == SessionState::Recording) {
        session_state_ = SessionState::Error;
        setSessionStatus("Bridge stopped before the session finished.");
    }
    if (close_pending_) {
        finishCloseIfReady();
    }
}

void BridgeWindow::saveSettings() const {
    vicon_lsl::gui_detail::saveBridgeWindowSettings(ui_->settings());
}

LabRecorderFilenameFields BridgeWindow::filenameFields() const {
    return ui_->filenameFields();
}

QString BridgeWindow::filenameValidationError() const {
    return LabRecorderFilenamePolicy::validationError(filenameFields());
}

void BridgeWindow::setLabRecorderStatus(const QString& status) {
    ui_->labrecorder_status_label->setText(status);
}

void BridgeWindow::updateRecordingButtons() {
    if (!ui_->refresh_streams_button || !ui_->start_recording_button || !ui_->stop_recording_button) {
        return;
    }

    const RecorderConnectionState connection_state = labrecorder_client_.connectionState();
    const RecorderRecordingState recording_state = labrecorder_client_.recordingState();
    ui_->refresh_streams_button->setEnabled(LabRecorderRuntimePolicy::canRefreshStreams(
        connection_state, recording_state) && !labrecorder_client_.busy());
    ui_->start_recording_button->setEnabled(
        LabRecorderRuntimePolicy::canStartRecording(connection_state, recording_state) &&
        isFilenameValid() && !labrecorder_client_.busy());
    ui_->stop_recording_button->setEnabled(LabRecorderRuntimePolicy::canStopRecording(
        connection_state, recording_state) && !labrecorder_client_.busy());
}

bool BridgeWindow::isFilenameValid() const {
    return filenameValidationError().isEmpty();
}

void BridgeWindow::scheduleFilenameSync() {
    if (!filename_sync_timer_ || labrecorder_client_.busy() ||
        session_state_ != SessionState::Ready) {
        if (filename_sync_timer_) {
            filename_sync_timer_->stop();
        }
        return;
    }

    const RecorderConnectionState connection_state = labrecorder_client_.connectionState();
    const RecorderRecordingState recording_state = labrecorder_client_.recordingState();
    if (LabRecorderRuntimePolicy::canStartRecording(connection_state, recording_state) &&
        isFilenameValid()) {
        filename_sync_timer_->start();
    } else {
        filename_sync_timer_->stop();
    }
}

void BridgeWindow::updateReadiness() {
    if (!ui_->readiness_label) {
        return;
    }

    const QString bridge_text = bridge_streaming_
        ? QString("Bridge streaming at %1").arg(bridge_status_stale_ ? "0.0 Hz (stale)" : ui_->frame_rate_label->text())
        : "Bridge not streaming";
    QString labrecorder_text;
    switch (labrecorder_client_.connectionState()) {
        case RecorderConnectionState::Disconnected:
            labrecorder_text = "LabRecorder disconnected";
            break;
        case RecorderConnectionState::Connecting:
            labrecorder_text = "LabRecorder connecting";
            break;
        case RecorderConnectionState::Connected:
            labrecorder_text = "LabRecorder connected";
            break;
        case RecorderConnectionState::Error:
            labrecorder_text = "LabRecorder connection error";
            break;
    }
    const QString filename_text = isFilenameValid() ? "filename valid" : "filename incomplete";
    ui_->readiness_label->setText(QString("Readiness: %1; %2; %3.")
                                  .arg(bridge_text, labrecorder_text, filename_text));
}
