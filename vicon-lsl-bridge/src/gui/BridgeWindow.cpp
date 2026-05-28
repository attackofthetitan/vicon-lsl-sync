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

// --- BridgeWorker ---

BridgeWorker::BridgeWorker(const Config& config, QObject* parent)
    : QThread(parent), config_(config) {}

void BridgeWorker::run() {
    bridge_ = std::make_unique<ViconLSLBridge>(config_);
    bridge_->setStatusCallback([this](const BridgeStatus& status) {
        emit statusUpdate(static_cast<int>(status.state),
                          static_cast<unsigned long long>(status.marker_count),
                          static_cast<unsigned long long>(status.segment_count),
                          status.frame_count,
                          status.gaze_enabled,
                          status.gaze_listening,
                          status.gaze_sample_count,
                          status.gaze_malformed_packet_count,
                          QString::fromStdString(status.gaze_last_error),
                          QString::fromStdString(status.message));
    });
    bridge_->run();
}

void BridgeWorker::stopBridge() {
    if (bridge_) {
        bridge_->stop();
    }
}

// --- BridgeWindow ---

BridgeWindow::BridgeWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Vicon LSL Bridge");
    setMinimumWidth(860);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);
    auto* content_layout = new QHBoxLayout();
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
    marker_stream_edit_ = new QLineEdit("ViconMarkers");
    segment_stream_edit_ = new QLineEdit("ViconSegments");

    form->addRow("Vicon server:", server_edit_);
    form->addRow("Marker stream:", marker_stream_edit_);
    form->addRow("Segment stream:", segment_stream_edit_);
    left_layout->addWidget(settings_group);

    auto* gaze_group = new QGroupBox("HoloLens Gaze Relay");
    auto* gaze_form = new QFormLayout(gaze_group);
    gaze_form->setContentsMargins(8, 8, 8, 8);
    gaze_form->setVerticalSpacing(4);

    gaze_enabled_check_ = new QCheckBox("Enable UDP relay");
    gaze_enabled_check_->setChecked(true);
    gaze_port_spin_ = new QSpinBox();
    gaze_port_spin_->setRange(1, 65535);
    gaze_port_spin_->setValue(16571);
    gaze_stream_edit_ = new QLineEdit("HoloLensGaze");

    gaze_form->addRow("HoloLens gaze relay:", gaze_enabled_check_);
    gaze_form->addRow("UDP port:", gaze_port_spin_);
    gaze_form->addRow("Gaze stream:", gaze_stream_edit_);
    left_layout->addWidget(gaze_group);

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
    gaze_status_label_ = new QLabel("Disabled");
    markers_label_ = new QLabel("0");
    segments_label_ = new QLabel("0");
    frames_label_ = new QLabel("0");
    frame_rate_label_ = new QLabel("0.0 Hz");
    gaze_samples_label_ = new QLabel("0");
    gaze_rate_label_ = new QLabel("0.0 Hz");
    malformed_packets_label_ = new QLabel("0");
    last_error_label_ = new QLabel("-");
    last_error_label_->setWordWrap(true);

    int status_row = 0;
    status_layout->addWidget(new QLabel("Bridge state:"), status_row, 0);
    status_layout->addWidget(status_label_, status_row, 1, 1, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Gaze relay:"), status_row, 0);
    status_layout->addWidget(gaze_status_label_, status_row, 1);
    status_layout->addWidget(new QLabel("Vicon rate:"), status_row, 2);
    status_layout->addWidget(frame_rate_label_, status_row, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Vicon frames:"), status_row, 0);
    status_layout->addWidget(frames_label_, status_row, 1);
    status_layout->addWidget(new QLabel("Gaze rate:"), status_row, 2);
    status_layout->addWidget(gaze_rate_label_, status_row, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Markers:"), status_row, 0);
    status_layout->addWidget(markers_label_, status_row, 1);
    status_layout->addWidget(new QLabel("Segments:"), status_row, 2);
    status_layout->addWidget(segments_label_, status_row, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Gaze samples:"), status_row, 0);
    status_layout->addWidget(gaze_samples_label_, status_row, 1);
    status_layout->addWidget(new QLabel("Malformed:"), status_row, 2);
    status_layout->addWidget(malformed_packets_label_, status_row, 3);
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

    recording_form->addRow("Study root:", root_layout);
    recording_form->addRow("File template:", filename_template_edit_);

    auto* metadata_grid = new QGridLayout();
    metadata_grid->setHorizontalSpacing(8);
    metadata_grid->setVerticalSpacing(4);
    metadata_grid->addWidget(new QLabel("Participant:"), 0, 0);
    metadata_grid->addWidget(participant_edit_, 0, 1);
    metadata_grid->addWidget(new QLabel("Session:"), 0, 2);
    metadata_grid->addWidget(session_edit_, 0, 3);
    metadata_grid->addWidget(new QLabel("Task/block:"), 1, 0);
    metadata_grid->addWidget(task_edit_, 1, 1);
    metadata_grid->addWidget(new QLabel("Run:"), 1, 2);
    metadata_grid->addWidget(run_spin_, 1, 3);
    metadata_grid->addWidget(new QLabel("Acquisition:"), 2, 0);
    metadata_grid->addWidget(acquisition_edit_, 2, 1);
    metadata_grid->addWidget(new QLabel("Modality:"), 2, 2);
    metadata_grid->addWidget(modality_edit_, 2, 3);
    metadata_grid->setColumnStretch(1, 1);
    metadata_grid->setColumnStretch(3, 1);
    recording_form->addRow("Metadata:", metadata_grid);
    recording_form->addRow("Filename preview:", filename_preview_label_);
    recording_layout->addLayout(recording_form);

    select_all_before_start_check_ = new QCheckBox("Select all streams before start");
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

    labrecorder_form->addRow("LabRecorder executable:", executable_layout);
    auto* rcs_layout = new QHBoxLayout();
    rcs_layout->addWidget(new QLabel("Host:"));
    rcs_layout->addWidget(labrecorder_host_edit_, 1);
    rcs_layout->addWidget(new QLabel("Port:"));
    rcs_layout->addWidget(labrecorder_port_spin_);
    labrecorder_form->addRow("RCS:", rcs_layout);
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
    main_layout->addLayout(content_layout);

    connect(start_button_, &QPushButton::clicked, this, &BridgeWindow::onStart);
    connect(stop_button_, &QPushButton::clicked, this, &BridgeWindow::onStop);
    connect(gaze_enabled_check_, &QCheckBox::toggled, gaze_port_spin_, &QSpinBox::setEnabled);
    connect(gaze_enabled_check_, &QCheckBox::toggled, gaze_stream_edit_, &QLineEdit::setEnabled);
    connect(browse_root_button, &QPushButton::clicked, this, &BridgeWindow::onBrowseStudyRoot);
    connect(browse_labrecorder_button, &QPushButton::clicked, this, &BridgeWindow::onBrowseLabRecorder);
    connect(launch_labrecorder_button_, &QPushButton::clicked, this, &BridgeWindow::onLaunchLabRecorder);
    connect(connect_labrecorder_button_, &QPushButton::clicked, this, &BridgeWindow::onConnectLabRecorder);
    connect(refresh_streams_button_, &QPushButton::clicked, this, &BridgeWindow::onRefreshLabRecorder);
    connect(start_recording_button_, &QPushButton::clicked, this, &BridgeWindow::onStartRecording);
    connect(stop_recording_button_, &QPushButton::clicked, this, &BridgeWindow::onStopRecording);

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
    updateRecordingButtons();
    updateReadiness();
}

BridgeWindow::~BridgeWindow() {
    if (worker_) {
        worker_->stopBridge();
        worker_->wait();
    }
}

void BridgeWindow::onStart() {
    Config config;
    config.vicon_server = server_edit_->text().toStdString();
    config.marker_stream_name = marker_stream_edit_->text().toStdString();
    config.segment_stream_name = segment_stream_edit_->text().toStdString();
    config.enable_hololens_gaze = gaze_enabled_check_->isChecked();
    config.hololens_gaze_port = static_cast<unsigned short>(gaze_port_spin_->value());
    config.hololens_gaze_stream_name = gaze_stream_edit_->text().toStdString();

    saveSettings();
    start_button_->setEnabled(false);
    stop_button_->setEnabled(true);
    setInputsEnabled(false);

    worker_ = new BridgeWorker(config, this);
    connect(worker_, &BridgeWorker::statusUpdate,
            this, &BridgeWindow::onStatusUpdate);
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
    QString executable = labrecorder_executable_edit_->text().trimmed();
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
    }
    if (labrecorder_process_->state() != QProcess::NotRunning) {
        setLabRecorderStatus("LabRecorder process is already running.");
        return;
    }

    labrecorder_process_->start(executable);
    if (!labrecorder_process_->waitForStarted(2000)) {
        setLabRecorderStatus("Failed to launch LabRecorder: " + labrecorder_process_->errorString());
        return;
    }
    setLabRecorderStatus("LabRecorder launched. Connect after RCS is ready.");
}

void BridgeWindow::onConnectLabRecorder() {
    saveSettings();
    if (labrecorder_client_.connectToServer(labrecorder_host_edit_->text(),
                                            static_cast<quint16>(labrecorder_port_spin_->value()))) {
        labrecorder_connected_ = true;
        recording_requested_ = false;
        setLabRecorderStatus("Connected to LabRecorder RCS.");
        updateRecordingButtons();
        updateReadiness();
        return;
    }
    labrecorder_connected_ = false;
    recording_requested_ = false;
    setLabRecorderStatus("LabRecorder connection failed: " + labrecorder_client_.lastError());
    updateRecordingButtons();
    updateReadiness();
}

void BridgeWindow::onRefreshLabRecorder() {
    sendLabRecorderCommand(labrecorder_client_.refreshStreams(), "Refresh command sent.");
}

void BridgeWindow::onStartRecording() {
    saveSettings();
    const QString validation_error = filenameValidationError();
    if (!validation_error.isEmpty()) {
        setLabRecorderStatus(validation_error);
        return;
    }

    if (sendLabRecorderCommand(
            labrecorder_client_.startRecording(filenameFields(), select_all_before_start_check_->isChecked()),
            "Recording start requested.")) {
        recording_requested_ = true;
        updateRecordingButtons();
        updateReadiness();
    }
}

void BridgeWindow::onStopRecording() {
    if (sendLabRecorderCommand(labrecorder_client_.stopRecording(), "Recording stop requested.")) {
        recording_requested_ = false;
        updateRecordingButtons();
        updateReadiness();
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
    gaze_rate_label_->setText("0.0 Hz");
    status_label_->setText(status_label_->text() + " - stale status");
    updateReadiness();
}

void BridgeWindow::onStatusUpdate(int state, unsigned long long markers, unsigned long long segments,
                                   unsigned int frames, bool gaze_enabled, bool gaze_listening,
                                   unsigned long long gaze_samples,
                                   unsigned long long gaze_malformed_packets,
                                   const QString& gaze_last_error,
                                   const QString& message) {
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
    if (!gaze_enabled) {
        gaze_status_label_->setText("Disabled");
    } else if (gaze_listening) {
        gaze_status_label_->setText("Listening");
    } else {
        gaze_status_label_->setText("Starting or stopped");
    }
    markers_label_->setText(QString::number(markers));
    segments_label_->setText(QString::number(segments));
    frames_label_->setText(QString::number(frames));
    gaze_samples_label_->setText(QString::number(gaze_samples));
    malformed_packets_label_->setText(QString::number(gaze_malformed_packets));
    last_error_label_->setText(gaze_last_error.isEmpty() ? "-" : gaze_last_error);

    qint64 now_ms = status_timer_.elapsed();
    if (have_previous_status_) {
        qint64 delta_ms = now_ms - previous_status_ms_;
        if (delta_ms > 0) {
            double seconds = static_cast<double>(delta_ms) / 1000.0;
            unsigned int frame_delta = frames >= previous_frames_ ? frames - previous_frames_ : 0;
            unsigned long long gaze_delta = gaze_samples >= previous_gaze_samples_
                ? gaze_samples - previous_gaze_samples_
                : 0;
            double frame_rate = static_cast<double>(frame_delta) / seconds;
            double gaze_rate = static_cast<double>(gaze_delta) / seconds;
            frame_rate_label_->setText(QString::number(frame_rate, 'f', 1) + " Hz");
            gaze_rate_label_->setText(QString::number(gaze_rate, 'f', 1) + " Hz");
        }
    }
    previous_status_ms_ = now_ms;
    previous_frames_ = frames;
    previous_gaze_samples_ = gaze_samples;
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
    gaze_rate_label_->setText("0.0 Hz");
    updateReadiness();

    worker_->deleteLater();
    worker_ = nullptr;
}

void BridgeWindow::loadSettings() {
    QSettings settings("ViconLSL", "ViconLSLBridge");
    server_edit_->setText(settings.value("server", "localhost:801").toString());
    marker_stream_edit_->setText(settings.value("markerStream", "ViconMarkers").toString());
    segment_stream_edit_->setText(settings.value("segmentStream", "ViconSegments").toString());
    gaze_enabled_check_->setChecked(settings.value("gazeEnabled", true).toBool());
    gaze_port_spin_->setValue(settings.value("gazePort", 16571).toInt());
    gaze_stream_edit_->setText(settings.value("gazeStream", "HoloLensGaze").toString());
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
    settings.setValue("gazeEnabled", gaze_enabled_check_->isChecked());
    settings.setValue("gazePort", gaze_port_spin_->value());
    settings.setValue("gazeStream", gaze_stream_edit_->text());
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
    gaze_enabled_check_->setEnabled(enabled);
    gaze_port_spin_->setEnabled(enabled && gaze_enabled_check_->isChecked());
    gaze_stream_edit_->setEnabled(enabled && gaze_enabled_check_->isChecked());
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

bool BridgeWindow::sendLabRecorderCommand(bool ok, const QString& success_message) {
    if (!ok) {
        if (!labrecorder_client_.isConnected()) {
            labrecorder_connected_ = false;
            recording_requested_ = false;
            updateRecordingButtons();
            updateReadiness();
        }
        setLabRecorderStatus("LabRecorder command failed: " + labrecorder_client_.lastError());
        return false;
    }
    setLabRecorderStatus(success_message);
    return true;
}

void BridgeWindow::updateRecordingButtons() {
    if (!refresh_streams_button_ || !start_recording_button_ || !stop_recording_button_) {
        return;
    }

    const bool connected = labrecorder_connected_ && labrecorder_client_.isConnected();
    refresh_streams_button_->setEnabled(connected && !recording_requested_);
    start_recording_button_->setEnabled(connected && !recording_requested_ && isFilenameValid());
    stop_recording_button_->setEnabled(connected && recording_requested_);
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
    const QString labrecorder_text = labrecorder_connected_ ? "LabRecorder connected" : "LabRecorder disconnected";
    const QString filename_text = isFilenameValid() ? "filename valid" : "filename incomplete";
    readiness_label_->setText(QString("Readiness: %1; %2; %3.")
                                  .arg(bridge_text, labrecorder_text, filename_text));
}
