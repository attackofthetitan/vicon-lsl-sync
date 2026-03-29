#include "BridgeWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>

// --- BridgeWorker ---

BridgeWorker::BridgeWorker(const Config& config, QObject* parent)
    : QThread(parent), config_(config) {}

void BridgeWorker::run() {
    bridge_ = std::make_unique<ViconLSLBridge>(config_);
    bridge_->setStatusCallback([this](const BridgeStatus& status) {
        emit statusUpdate(static_cast<int>(status.state),
                          status.marker_count, status.segment_count,
                          status.frame_count,
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
    setMinimumWidth(400);

    auto* main_layout = new QVBoxLayout(this);

    // Connection settings
    auto* settings_group = new QGroupBox("Connection Settings");
    auto* form = new QFormLayout(settings_group);

    server_edit_ = new QLineEdit("localhost:801");
    marker_stream_edit_ = new QLineEdit("ViconMarkers");
    segment_stream_edit_ = new QLineEdit("ViconSegments");

    form->addRow("Server:", server_edit_);
    form->addRow("Marker Stream:", marker_stream_edit_);
    form->addRow("Segment Stream:", segment_stream_edit_);
    main_layout->addWidget(settings_group);

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
    markers_label_ = new QLabel("0");
    segments_label_ = new QLabel("0");
    frames_label_ = new QLabel("0");

    status_layout->addRow("Status:", status_label_);
    status_layout->addRow("Markers:", markers_label_);
    status_layout->addRow("Segments:", segments_label_);
    status_layout->addRow("Frames:", frames_label_);
    main_layout->addWidget(status_group);

    main_layout->addStretch();

    connect(start_button_, &QPushButton::clicked, this, &BridgeWindow::onStart);
    connect(stop_button_, &QPushButton::clicked, this, &BridgeWindow::onStop);
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

    start_button_->setEnabled(false);
    stop_button_->setEnabled(true);
    server_edit_->setEnabled(false);
    marker_stream_edit_->setEnabled(false);
    segment_stream_edit_->setEnabled(false);

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

void BridgeWindow::onStatusUpdate(int state, size_t markers, size_t segments,
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
    markers_label_->setText(QString::number(markers));
    segments_label_->setText(QString::number(segments));
    frames_label_->setText(QString::number(frames));
}

void BridgeWindow::onWorkerFinished() {
    start_button_->setEnabled(true);
    stop_button_->setEnabled(false);
    server_edit_->setEnabled(true);
    marker_stream_edit_->setEnabled(true);
    segment_stream_edit_->setEnabled(true);

    worker_->deleteLater();
    worker_ = nullptr;
}
