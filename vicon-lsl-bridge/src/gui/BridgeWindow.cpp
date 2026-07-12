#include "BridgeWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSettings>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QSplitter>
#include <QCloseEvent>
#include <QCoreApplication>

#include <exception>
#include "StreamDefaults.h"
#include "gui/LabRecorderRuntimePolicy.h"

namespace {

QLabel* makeTooltipLabel(const QString& text, QWidget* control, const QString& tooltip) {
    auto* label = new QLabel(text);
    label->setToolTip(tooltip);
    if (control) {
        control->setToolTip(tooltip);
    }
    return label;
}

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
        emit terminal(BridgeExitResult::Stopped, {});
    } catch (const std::exception& ex) {
        emit terminal(BridgeExitResult::Failed, QString::fromUtf8(ex.what()));
    } catch (...) {
        emit terminal(BridgeExitResult::Failed, "Unknown bridge worker failure");
    }
}

void BridgeWorker::stopBridge() {
    bridge_->stop();
}

// --- BridgeWindow ---

BridgeWindow::BridgeWindow(QWidget* parent) : QWidget(parent) {
    qRegisterMetaType<BridgeExitResult>("BridgeExitResult");
    setWindowTitle("Vicon LSL Bridge");
    setMinimumWidth(860);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);
    auto* main_splitter = new QSplitter(Qt::Vertical);
    auto* controls_page = new QWidget();
    auto* content_layout = new QHBoxLayout(controls_page);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(8);
    auto* left_layout = new QVBoxLayout();
    left_layout->setSpacing(8);
    auto* right_layout = new QVBoxLayout();
    right_layout->setSpacing(8);

    // Connection settings
    auto* settings_group = new QGroupBox("Connection Settings");
    auto* form = new QFormLayout(settings_group);
    form->setContentsMargins(8, 8, 8, 8);
    form->setVerticalSpacing(4);

    server_edit_ = new QLineEdit("localhost:801");
    marker_stream_edit_ = new QLineEdit(vicon_lsl::stream_defaults::ViconMarkers);
    segment_stream_edit_ = new QLineEdit(vicon_lsl::stream_defaults::ViconSegments);

    form->addRow(makeTooltipLabel(
                     "Vicon server:", server_edit_,
                     "Vicon DataStream endpoint, for example localhost:801."),
                 server_edit_);
    form->addRow(makeTooltipLabel(
                     "Marker stream:", marker_stream_edit_,
                     "LSL stream name for Vicon marker samples."),
                 marker_stream_edit_);
    form->addRow(makeTooltipLabel(
                     "Segment stream:", segment_stream_edit_,
                     "LSL stream name for Vicon segment samples."),
                 segment_stream_edit_);
    left_layout->addWidget(settings_group);

    // Buttons
    auto* button_layout = new QHBoxLayout();
    start_button_ = new QPushButton("Start Streaming");
    stop_button_ = new QPushButton("Stop");
    stop_button_->setEnabled(false);
    button_layout->addWidget(start_button_);
    button_layout->addWidget(stop_button_);
    left_layout->addLayout(button_layout);

    // Status
    auto* status_group = new QGroupBox("Status");
    auto* status_layout = new QGridLayout(status_group);
    status_layout->setContentsMargins(8, 8, 8, 8);
    status_layout->setHorizontalSpacing(10);
    status_layout->setVerticalSpacing(4);

    status_label_ = new QLabel("Disconnected");
    markers_label_ = new QLabel("0");
    segments_label_ = new QLabel("0");
    frames_label_ = new QLabel("0");
    frame_rate_label_ = new QLabel("0.0 Hz");
    last_error_label_ = new QLabel("-");
    last_error_label_->setWordWrap(true);

    int status_row = 0;
    status_layout->addWidget(new QLabel("Bridge state:"), status_row, 0);
    status_layout->addWidget(status_label_, status_row, 1, 1, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Vicon frames:"), status_row, 0);
    status_layout->addWidget(frames_label_, status_row, 1);
    status_layout->addWidget(new QLabel("Vicon rate:"), status_row, 2);
    status_layout->addWidget(frame_rate_label_, status_row, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Markers:"), status_row, 0);
    status_layout->addWidget(markers_label_, status_row, 1);
    status_layout->addWidget(new QLabel("Segments:"), status_row, 2);
    status_layout->addWidget(segments_label_, status_row, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Last error:"), status_row, 0);
    status_layout->addWidget(last_error_label_, status_row, 1, 1, 3);
    status_layout->setColumnStretch(1, 1);
    status_layout->setColumnStretch(3, 1);
    left_layout->addWidget(status_group);
    left_layout->addStretch();

    auto* recording_group = new QGroupBox("Recording");
    auto* recording_layout = new QVBoxLayout(recording_group);
    recording_layout->setContentsMargins(8, 8, 8, 8);
    recording_layout->setSpacing(6);
    auto* recording_form = new QFormLayout();
    recording_form->setVerticalSpacing(4);

    auto* root_layout = new QHBoxLayout();
    study_root_edit_ = new QLineEdit();
    auto* browse_root_button = new QPushButton("Browse");
    root_layout->addWidget(study_root_edit_);
    root_layout->addWidget(browse_root_button);

    filename_template_edit_ = new QLineEdit("sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf");
    participant_edit_ = new QLineEdit("P001");
    session_edit_ = new QLineEdit("S001");
    task_edit_ = new QLineEdit("Task");
    run_spin_ = new QSpinBox();
    run_spin_->setRange(1, 9999);
    run_spin_->setValue(1);
    acquisition_edit_ = new QLineEdit("vicon");
    modality_edit_ = new QLineEdit("beh");
    filename_preview_label_ = new QLineEdit();
    filename_preview_label_->setReadOnly(true);
    filename_preview_label_->setPlaceholderText("Complete the recording fields to preview the output path");

    recording_form->addRow(makeTooltipLabel(
                               "Study root:", study_root_edit_,
                               "Directory where LabRecorder stores recordings."),
                           root_layout);
    recording_form->addRow(makeTooltipLabel(
                               "File template:", filename_template_edit_,
                               "LabRecorder path template. Tokens: %p participant, %s session, "
                               "%b task/block, %r or %n run, %a acquisition, %m modality."),
                           filename_template_edit_);

    auto* metadata_grid = new QGridLayout();
    metadata_grid->setHorizontalSpacing(8);
    metadata_grid->setVerticalSpacing(4);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Participant:", participant_edit_,
                                 "Participant identifier substituted for %p."),
                             0, 0);
    metadata_grid->addWidget(participant_edit_, 0, 1);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Session:", session_edit_,
                                 "Session identifier substituted for %s."),
                             0, 2);
    metadata_grid->addWidget(session_edit_, 0, 3);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Task/block:", task_edit_,
                                 "Task or block identifier substituted for %b."),
                             1, 0);
    metadata_grid->addWidget(task_edit_, 1, 1);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Run:", run_spin_,
                                 "Run number substituted for %r."),
                             1, 2);
    metadata_grid->addWidget(run_spin_, 1, 3);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Acquisition:", acquisition_edit_,
                                 "Acquisition label substituted for %a."),
                             2, 0);
    metadata_grid->addWidget(acquisition_edit_, 2, 1);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Modality:", modality_edit_,
                                 "Modality label substituted for %m."),
                             2, 2);
    metadata_grid->addWidget(modality_edit_, 2, 3);
    metadata_grid->setColumnStretch(1, 1);
    metadata_grid->setColumnStretch(3, 1);
    auto* metadata_label = new QLabel("Metadata:");
    metadata_label->setToolTip("Values used to expand the filename template tokens.");
    recording_form->addRow(metadata_label, metadata_grid);
    recording_form->addRow("Filename preview:", filename_preview_label_);
    recording_layout->addLayout(recording_form);

    select_all_before_start_check_ = new QCheckBox("Select all streams before start");
    select_all_before_start_check_->setToolTip(
        "Select all LabRecorder streams before starting a recording.");
    select_all_before_start_check_->setChecked(true);
    recording_layout->addWidget(select_all_before_start_check_);

    auto* labrecorder_form = new QFormLayout();
    labrecorder_form->setVerticalSpacing(4);
    auto* executable_layout = new QHBoxLayout();
    labrecorder_executable_edit_ = new QLineEdit();
    auto* browse_labrecorder_button = new QPushButton("Browse");
    executable_layout->addWidget(labrecorder_executable_edit_);
    executable_layout->addWidget(browse_labrecorder_button);

    labrecorder_host_edit_ = new QLineEdit("localhost");
    labrecorder_port_spin_ = new QSpinBox();
    labrecorder_port_spin_->setRange(1, 65535);
    labrecorder_port_spin_->setValue(22345);

    labrecorder_form->addRow(makeTooltipLabel(
                                  "LabRecorder executable:", labrecorder_executable_edit_,
                                  "Optional LabRecorder executable path. Leave blank for the automatic bundled "
                                  "startup; set a path before using Launch LabRecorder."),
                              executable_layout);
    auto* rcs_layout = new QHBoxLayout();
    auto* rcs_host_label = makeTooltipLabel(
        "Host:", labrecorder_host_edit_, "LabRecorder remote-control server host.");
    rcs_layout->addWidget(rcs_host_label);
    rcs_layout->addWidget(labrecorder_host_edit_, 1);
    auto* rcs_port_label = makeTooltipLabel(
        "Port:", labrecorder_port_spin_, "LabRecorder remote-control server TCP port.");
    rcs_layout->addWidget(rcs_port_label);
    rcs_layout->addWidget(labrecorder_port_spin_);
    auto* rcs_label = new QLabel("RCS:");
    rcs_label->setToolTip("Host and TCP port for LabRecorder's remote-control server.");
    labrecorder_form->addRow(rcs_label, rcs_layout);
    recording_layout->addLayout(labrecorder_form);

    auto* recording_buttons = new QGridLayout();
    recording_buttons->setHorizontalSpacing(6);
    recording_buttons->setVerticalSpacing(4);
    launch_labrecorder_button_ = new QPushButton("Launch LabRecorder");
    connect_labrecorder_button_ = new QPushButton("Connect");
    refresh_streams_button_ = new QPushButton("Refresh Streams");
    start_recording_button_ = new QPushButton("Start Recording");
    stop_recording_button_ = new QPushButton("Stop Recording");
    recording_buttons->addWidget(launch_labrecorder_button_, 0, 0);
    recording_buttons->addWidget(connect_labrecorder_button_, 0, 1);
    recording_buttons->addWidget(refresh_streams_button_, 0, 2);
    recording_buttons->addWidget(start_recording_button_, 1, 0, 1, 2);
    recording_buttons->addWidget(stop_recording_button_, 1, 2);
    recording_layout->addLayout(recording_buttons);

    labrecorder_status_label_ = new QLabel("Disconnected");
    labrecorder_status_label_->setWordWrap(true);
    recording_layout->addWidget(labrecorder_status_label_);
    readiness_label_ = new QLabel();
    readiness_label_->setWordWrap(true);
    recording_layout->addWidget(readiness_label_);

    right_layout->addWidget(recording_group);
    right_layout->addStretch();
    content_layout->addLayout(left_layout, 2);
    content_layout->addLayout(right_layout, 3);
    main_splitter->addWidget(controls_page);
    preview_panel_ = new vicon_lsl::PreviewPanel();
    main_splitter->addWidget(preview_panel_);
    main_splitter->setStretchFactor(0, 0);
    main_splitter->setStretchFactor(1, 1);
    main_layout->addWidget(main_splitter, 1);

    connect(start_button_, &QPushButton::clicked, this, &BridgeWindow::onStart);
    connect(stop_button_, &QPushButton::clicked, this, &BridgeWindow::onStop);
    connect(browse_root_button, &QPushButton::clicked, this, &BridgeWindow::onBrowseStudyRoot);
    connect(browse_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onBrowseLabRecorder);
    connect(launch_labrecorder_button_, &QPushButton::clicked, this, &BridgeWindow::onLaunchLabRecorder);
    connect(connect_labrecorder_button_, &QPushButton::clicked, this, &BridgeWindow::onConnectLabRecorder);
    connect(refresh_streams_button_, &QPushButton::clicked, this, &BridgeWindow::onRefreshLabRecorder);
    connect(start_recording_button_, &QPushButton::clicked, this, &BridgeWindow::onStartRecording);
    connect(stop_recording_button_, &QPushButton::clicked, this, &BridgeWindow::onStopRecording);
    connect(&labrecorder_client_, &LabRecorderClient::connectionStateChanged, this,
            [this](RecorderConnectionState state, const QString& message) {
                if (state == RecorderConnectionState::Connected && labrecorder_retry_timer_) {
                    labrecorder_retry_timer_->stop();
                }
                if (!message.isEmpty()) {
                    setLabRecorderStatus(message);
                }
                connect_labrecorder_button_->setEnabled(
                    state != RecorderConnectionState::Connecting);
                updateRecordingButtons();
                updateReadiness();
            });
    connect(&labrecorder_client_, &LabRecorderClient::recordingStateChanged, this,
            [this](RecorderRecordingState) {
                updateRecordingButtons();
                updateReadiness();
            });
    connect(&labrecorder_client_, &LabRecorderClient::commandFinished, this,
            [this](const QString& operation, bool ok, const QString& message) {
                setLabRecorderStatus(ok
                    ? operation + " completed"
                    : operation + " failed: " + message);
                updateRecordingButtons();
                updateReadiness();
            });

    const auto preview_update = [this]() { updateFilenamePreview(); };
    connect(study_root_edit_, &QLineEdit::textChanged, this, preview_update);
    connect(filename_template_edit_, &QLineEdit::textChanged, this, preview_update);
    connect(participant_edit_, &QLineEdit::textChanged, this, preview_update);
    connect(session_edit_, &QLineEdit::textChanged, this, preview_update);
    connect(task_edit_, &QLineEdit::textChanged, this, preview_update);
    connect(run_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, preview_update);
    connect(acquisition_edit_, &QLineEdit::textChanged, this, preview_update);
    connect(modality_edit_, &QLineEdit::textChanged, this, preview_update);

    loadSettings();
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

    // Resolve and launch the recorder after the window has been constructed so
    // QProcess and the RCS retry loop are owned by the GUI thread.
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

bool BridgeWindow::labRecorderConnected() const {
    return labrecorder_client_.connectionState() == RecorderConnectionState::Connected;
}

bool BridgeWindow::labRecorderOwnedProcessRunning() const {
    return labrecorder_process_owned_ && labrecorder_process_ &&
           labrecorder_process_->state() != QProcess::NotRunning;
}

bool BridgeWindow::stairModelLoaded() const {
    return preview_panel_ && preview_panel_->stairModelLoaded();
}

bool BridgeWindow::configurableTooltipsPresent() const {
    const QWidget* const controls[] = {
        server_edit_,
        marker_stream_edit_,
        segment_stream_edit_,
        study_root_edit_,
        filename_template_edit_,
        participant_edit_,
        session_edit_,
        task_edit_,
        run_spin_,
        acquisition_edit_,
        modality_edit_,
        select_all_before_start_check_,
        labrecorder_executable_edit_,
        labrecorder_host_edit_,
        labrecorder_port_spin_,
    };
    for (const QWidget* control : controls) {
        if (!control || control->toolTip().trimmed().isEmpty()) {
            return false;
        }
    }
    return preview_panel_ && preview_panel_->configurableTooltipsPresent();
}

void BridgeWindow::onStart() {
    Config config;
    config.vicon_server = server_edit_->text().toStdString();
    config.marker_stream_name = marker_stream_edit_->text().toStdString();
    config.segment_stream_name = segment_stream_edit_->text().toStdString();

    saveSettings();
    start_button_->setEnabled(false);
    stop_button_->setEnabled(true);
    setInputsEnabled(false);

    worker_ = new BridgeWorker(config, this);
    connect(worker_, &BridgeWorker::statusUpdate,
            this, &BridgeWindow::onStatusUpdate);
    connect(worker_, &BridgeWorker::terminal, this,
            [this](BridgeExitResult result, const QString& message) {
                if (result == BridgeExitResult::Failed) {
                    last_error_label_->setText(message);
                    status_label_->setText("Bridge worker failed - " + message);
                }
            });
    connect(worker_, &BridgeWorker::finished,
            this, &BridgeWindow::onWorkerFinished);
    worker_->start();
}

void BridgeWindow::onStop() {
    stop_button_->setEnabled(false);
    if (worker_) {
        worker_->stopBridge();
    }
}

void BridgeWindow::onBrowseStudyRoot() {
    QString root = QFileDialog::getExistingDirectory(this, "Select Study Root", study_root_edit_->text());
    if (!root.isEmpty()) {
        study_root_edit_->setText(QDir::toNativeSeparators(root));
    }
}

void BridgeWindow::onBrowseLabRecorder() {
    QString path = QFileDialog::getOpenFileName(this, "Select LabRecorder", labrecorder_executable_edit_->text());
    if (!path.isEmpty()) {
        labrecorder_executable_edit_->setText(QDir::toNativeSeparators(path));
    }
}

void BridgeWindow::onLaunchLabRecorder() {
    const QString executable = labrecorder_executable_edit_->text().trimmed();
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
    setLabRecorderStatus("LabRecorder launched. Connect after RCS is ready.");
    labrecorder_retry_elapsed_.restart();
    labrecorder_retry_timer_->start();
    onLabRecorderRetry();
}

void BridgeWindow::onConnectLabRecorder() {
    labrecorder_retry_timer_->stop();
    saveSettings();
    labrecorder_client_.connectToServer(
        labrecorder_host_edit_->text(),
        static_cast<quint16>(labrecorder_port_spin_->value()));
}

QString BridgeWindow::resolveLabRecorderExecutable() const {
    return LabRecorderRuntimePolicy::resolveExecutable(
        labrecorder_executable_edit_->text(),
        QCoreApplication::applicationDirPath());
}

void BridgeWindow::beginLabRecorderStartup() {
    const QString executable = resolveLabRecorderExecutable();
    if (executable.isEmpty() || !QFileInfo::exists(executable)) {
        setLabRecorderStatus("LabRecorder executable not found; use Browse or Launch.");
        updateReadiness();
        return;
    }
    // Keep a valid saved custom path; otherwise show the bundled fallback so
    // the automatic launch path is visible and reproducible.
    labrecorder_executable_edit_->setText(executable);
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
        setLabRecorderStatus("LabRecorder RCS was not ready within 15 seconds.");
        return;
    }
    if (!LabRecorderRuntimePolicy::shouldAttemptConnection(state, elapsed)) {
        return;
    }
    labrecorder_client_.connectToServer(
        labrecorder_host_edit_->text(),
        static_cast<quint16>(labrecorder_port_spin_->value()),
        200);
}

void BridgeWindow::onRefreshLabRecorder() {
    if (labrecorder_client_.refreshStreams()) {
        setLabRecorderStatus("Refresh command queued.");
    }
}

void BridgeWindow::onStartRecording() {
    saveSettings();
    const QString validation_error = filenameValidationError();
    if (!validation_error.isEmpty()) {
        setLabRecorderStatus(validation_error);
        return;
    }

    if (labrecorder_client_.startRecording(
            filenameFields(), select_all_before_start_check_->isChecked())) {
        setLabRecorderStatus("Recording start commands queued.");
    }
}

void BridgeWindow::onStopRecording() {
    if (labrecorder_client_.stopRecording()) {
        setLabRecorderStatus("Recording stop command queued.");
    }
}

void BridgeWindow::updateFilenamePreview() {
    if (filename_preview_label_) {
        const QString preview = renderedFilenamePreview();
        filename_preview_label_->setText(preview);
        filename_preview_label_->setToolTip(preview);
        filename_preview_label_->setCursorPosition(0);
    }
    updateRecordingButtons();
    updateReadiness();
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
    frame_rate_label_->setText("0.0 Hz");
    status_label_->setText(status_label_->text() + " - stale status");
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
        close_stop_requested_ = false;
        if (worker_) {
            onStop();
            status_label_->setText("Stopping bridge before closing...");
        }
        if (labrecorder_client_.recordingState() == RecorderRecordingState::Recording) {
            close_stop_requested_ = labrecorder_client_.stopRecording();
            if (close_stop_requested_) {
                setLabRecorderStatus("Stopping recording before closing...");
            }
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
    const bool bridge_done = worker_ == nullptr;
    const bool recording_done = !close_stop_requested_ ||
        labrecorder_client_.recordingState() == RecorderRecordingState::Stopped ||
        close_elapsed_.elapsed() >= 2000;
    if (!bridge_done || !recording_done) {
        if (close_elapsed_.elapsed() < 4000) {
            return;
        }
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

    status_label_->setText(state_text);
    bridge_streaming_ = bridge_state == BridgeState::Streaming;
    bridge_status_stale_ = false;
    markers_label_->setText(QString::number(markers));
    segments_label_->setText(QString::number(segments));
    frames_label_->setText(QString::number(frames));
    last_error_label_->setText(message.isEmpty() ? "-" : message);

    qint64 now_ms = status_timer_.elapsed();
    if (have_previous_status_) {
        qint64 delta_ms = now_ms - previous_status_ms_;
        if (delta_ms > 0) {
            double seconds = static_cast<double>(delta_ms) / 1000.0;
            unsigned int frame_delta = frames >= previous_frames_ ? frames - previous_frames_ : 0;
            double frame_rate = static_cast<double>(frame_delta) / seconds;
            frame_rate_label_->setText(QString::number(frame_rate, 'f', 1) + " Hz");
        }
    }
    previous_status_ms_ = now_ms;
    previous_frames_ = frames;
    have_previous_status_ = true;
    updateReadiness();
}

void BridgeWindow::onWorkerFinished() {
    start_button_->setEnabled(true);
    stop_button_->setEnabled(false);
    setInputsEnabled(true);
    bridge_streaming_ = false;
    bridge_status_stale_ = false;
    have_previous_status_ = false;
    frame_rate_label_->setText("0.0 Hz");
    updateReadiness();

    worker_->deleteLater();
    worker_ = nullptr;
    if (close_pending_) {
        finishCloseIfReady();
    }
}

void BridgeWindow::loadSettings() {
    QSettings settings("ViconLSL", "ViconLSLBridge");
    server_edit_->setText(settings.value("server", "localhost:801").toString());
    marker_stream_edit_->setText(settings.value(
        "markerStream", vicon_lsl::stream_defaults::ViconMarkers).toString());
    segment_stream_edit_->setText(settings.value(
        "segmentStream", vicon_lsl::stream_defaults::ViconSegments).toString());
    study_root_edit_->setText(settings.value("recordingRoot", QDir::homePath()).toString());
    filename_template_edit_->setText(settings.value("recordingTemplate",
        "sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf").toString());
    participant_edit_->setText(settings.value("participant", "P001").toString());
    session_edit_->setText(settings.value("session", "S001").toString());
    task_edit_->setText(settings.value("task", "Task").toString());
    run_spin_->setValue(settings.value("run", 1).toInt());
    acquisition_edit_->setText(settings.value("acquisition", "vicon").toString());
    modality_edit_->setText(settings.value("modality", "beh").toString());
    select_all_before_start_check_->setChecked(settings.value("selectAllBeforeStart", true).toBool());
    labrecorder_executable_edit_->setText(settings.value("labRecorderExecutable", "").toString());
    labrecorder_host_edit_->setText(settings.value("labRecorderHost", "localhost").toString());
    labrecorder_port_spin_->setValue(settings.value("labRecorderPort", 22345).toInt());
}

void BridgeWindow::saveSettings() const {
    QSettings settings("ViconLSL", "ViconLSLBridge");
    settings.setValue("server", server_edit_->text());
    settings.setValue("markerStream", marker_stream_edit_->text());
    settings.setValue("segmentStream", segment_stream_edit_->text());
    settings.setValue("recordingRoot", study_root_edit_->text());
    settings.setValue("recordingTemplate", filename_template_edit_->text());
    settings.setValue("participant", participant_edit_->text());
    settings.setValue("session", session_edit_->text());
    settings.setValue("task", task_edit_->text());
    settings.setValue("run", run_spin_->value());
    settings.setValue("acquisition", acquisition_edit_->text());
    settings.setValue("modality", modality_edit_->text());
    settings.setValue("selectAllBeforeStart", select_all_before_start_check_->isChecked());
    settings.setValue("labRecorderExecutable", labrecorder_executable_edit_->text());
    settings.setValue("labRecorderHost", labrecorder_host_edit_->text());
    settings.setValue("labRecorderPort", labrecorder_port_spin_->value());
}

void BridgeWindow::setInputsEnabled(bool enabled) {
    server_edit_->setEnabled(enabled);
    marker_stream_edit_->setEnabled(enabled);
    segment_stream_edit_->setEnabled(enabled);
}

LabRecorderFilenameFields BridgeWindow::filenameFields() const {
    LabRecorderFilenameFields fields;
    fields.root = study_root_edit_->text();
    fields.templ = filename_template_edit_->text();
    fields.participant = participant_edit_->text();
    fields.session = session_edit_->text();
    fields.task = task_edit_->text();
    fields.run = QString::number(run_spin_->value());
    fields.acquisition = acquisition_edit_->text();
    fields.modality = modality_edit_->text();
    return fields;
}

QString BridgeWindow::renderedFilenamePreview() const {
    const LabRecorderFilenameFields fields = filenameFields();
    const QString rendered = LabRecorderClient::renderedFilename(fields);
    const QString root = LabRecorderClient::sanitizedValue(fields.root);
    if (!root.isEmpty()) {
        return QDir::toNativeSeparators(QDir(root).filePath(rendered));
    }
    return QDir::toNativeSeparators(rendered);
}

QString BridgeWindow::filenameValidationError() const {
    const LabRecorderFilenameFields fields = filenameFields();
    const QString root = LabRecorderClient::sanitizedValue(fields.root);
    if (root.isEmpty()) {
        return "Set a study root before starting recording.";
    }

    QFileInfo root_info(fields.root);
    if (!root_info.exists() || !root_info.isDir()) {
        return "Study root does not exist or is not a directory: " + fields.root;
    }

    if (LabRecorderClient::sanitizedValue(fields.templ).isEmpty()) {
        return "Set a filename template before starting recording.";
    }

    QStringList missing_fields;
    if (LabRecorderClient::sanitizedValue(fields.participant).isEmpty()) {
        missing_fields.append("participant");
    }
    if (LabRecorderClient::sanitizedValue(fields.session).isEmpty()) {
        missing_fields.append("session");
    }
    if (LabRecorderClient::sanitizedValue(fields.task).isEmpty()) {
        missing_fields.append("task");
    }
    if (LabRecorderClient::sanitizedValue(fields.acquisition).isEmpty()) {
        missing_fields.append("acquisition");
    }
    if (LabRecorderClient::sanitizedValue(fields.modality).isEmpty()) {
        missing_fields.append("modality");
    }
    if (!missing_fields.isEmpty()) {
        return "Complete recording metadata before starting: " + missing_fields.join(", ") + ".";
    }

    if (LabRecorderClient::hasUnresolvedFilenamePlaceholders(fields)) {
        return "Resolve all filename template placeholders before starting recording.";
    }

    if (renderedFilenamePreview().trimmed().isEmpty()) {
        return "Filename preview is empty; check the study root and template.";
    }

    return {};
}

void BridgeWindow::setLabRecorderStatus(const QString& status) {
    labrecorder_status_label_->setText(status);
}

void BridgeWindow::updateRecordingButtons() {
    if (!refresh_streams_button_ || !start_recording_button_ || !stop_recording_button_) {
        return;
    }

    const bool connected =
        labrecorder_client_.connectionState() == RecorderConnectionState::Connected;
    const RecorderRecordingState recording_state = labrecorder_client_.recordingState();
    refresh_streams_button_->setEnabled(
        connected && recording_state == RecorderRecordingState::Stopped);
    start_recording_button_->setEnabled(
        connected && recording_state == RecorderRecordingState::Stopped && isFilenameValid());
    stop_recording_button_->setEnabled(
        connected && recording_state != RecorderRecordingState::Stopped);
}

bool BridgeWindow::isFilenameValid() const {
    return filenameValidationError().isEmpty();
}

void BridgeWindow::updateReadiness() {
    if (!readiness_label_) {
        return;
    }

    const QString bridge_text = bridge_streaming_
        ? QString("Bridge streaming at %1").arg(bridge_status_stale_ ? "0.0 Hz (stale)" : frame_rate_label_->text())
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
    readiness_label_->setText(QString("Readiness: %1; %2; %3.")
                                  .arg(bridge_text, labrecorder_text, filename_text));
}
