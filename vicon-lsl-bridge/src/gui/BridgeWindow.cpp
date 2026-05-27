#include "BridgeWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QSettings>

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
    setMinimumWidth(460);

    auto* main_layout = new QVBoxLayout(this);

    // Connection settings
    auto* settings_group = new QGroupBox("Connection Settings");
    auto* form = new QFormLayout(settings_group);

    server_edit_ = new QLineEdit("localhost:801");
    marker_stream_edit_ = new QLineEdit("ViconMarkers");
    segment_stream_edit_ = new QLineEdit("ViconSegments");

    form->addRow("Vicon server:", server_edit_);
    form->addRow("Marker stream:", marker_stream_edit_);
    form->addRow("Segment stream:", segment_stream_edit_);
    main_layout->addWidget(settings_group);

    auto* gaze_group = new QGroupBox("HoloLens Gaze Relay");
    auto* gaze_form = new QFormLayout(gaze_group);

    gaze_enabled_check_ = new QCheckBox("Enable UDP relay");
    gaze_enabled_check_->setChecked(true);
    gaze_port_spin_ = new QSpinBox();
    gaze_port_spin_->setRange(1, 65535);
    gaze_port_spin_->setValue(16571);
    gaze_stream_edit_ = new QLineEdit("HoloLensGaze");

    gaze_form->addRow("HoloLens gaze relay:", gaze_enabled_check_);
    gaze_form->addRow("UDP port:", gaze_port_spin_);
    gaze_form->addRow("Gaze stream:", gaze_stream_edit_);
    main_layout->addWidget(gaze_group);

    // Buttons
    auto* button_layout = new QHBoxLayout();
    start_button_ = new QPushButton("Start Streaming");
    stop_button_ = new QPushButton("Stop");
    stop_button_->setEnabled(false);
    button_layout->addWidget(start_button_);
    button_layout->addWidget(stop_button_);
    main_layout->addLayout(button_layout);

    // Status
    auto* status_group = new QGroupBox("Status");
    auto* status_layout = new QFormLayout(status_group);

    status_label_ = new QLabel("Disconnected");
    gaze_status_label_ = new QLabel("Disabled");
    markers_label_ = new QLabel("0");
    segments_label_ = new QLabel("0");
    frames_label_ = new QLabel("0");
    gaze_samples_label_ = new QLabel("0");
    malformed_packets_label_ = new QLabel("0");
    last_error_label_ = new QLabel("-");

    status_layout->addRow("Bridge state:", status_label_);
    status_layout->addRow("Gaze relay:", gaze_status_label_);
    status_layout->addRow("Vicon frames:", frames_label_);
    status_layout->addRow("Markers:", markers_label_);
    status_layout->addRow("Segments:", segments_label_);
    status_layout->addRow("Gaze samples:", gaze_samples_label_);
    status_layout->addRow("Malformed packets:", malformed_packets_label_);
    status_layout->addRow("Last error:", last_error_label_);
    main_layout->addWidget(status_group);

    main_layout->addStretch();

    connect(start_button_, &QPushButton::clicked, this, &BridgeWindow::onStart);
    connect(stop_button_, &QPushButton::clicked, this, &BridgeWindow::onStop);
    connect(gaze_enabled_check_, &QCheckBox::toggled, gaze_port_spin_, &QSpinBox::setEnabled);
    connect(gaze_enabled_check_, &QCheckBox::toggled, gaze_stream_edit_, &QLineEdit::setEnabled);

    loadSettings();
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
}

void BridgeWindow::onWorkerFinished() {
    start_button_->setEnabled(true);
    stop_button_->setEnabled(false);
    setInputsEnabled(true);

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
}

void BridgeWindow::saveSettings() const {
    QSettings settings("ViconLSL", "ViconLSLBridge");
    settings.setValue("server", server_edit_->text());
    settings.setValue("markerStream", marker_stream_edit_->text());
    settings.setValue("segmentStream", segment_stream_edit_->text());
    settings.setValue("gazeEnabled", gaze_enabled_check_->isChecked());
    settings.setValue("gazePort", gaze_port_spin_->value());
    settings.setValue("gazeStream", gaze_stream_edit_->text());
}

void BridgeWindow::setInputsEnabled(bool enabled) {
    server_edit_->setEnabled(enabled);
    marker_stream_edit_->setEnabled(enabled);
    segment_stream_edit_->setEnabled(enabled);
    gaze_enabled_check_->setEnabled(enabled);
    gaze_port_spin_->setEnabled(enabled && gaze_enabled_check_->isChecked());
    gaze_stream_edit_->setEnabled(enabled && gaze_enabled_check_->isChecked());
}
