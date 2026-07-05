#include "gui/PreviewPanel.h"

#include "preview/ObjMesh.h"
#include "preview/PreviewCsv.h"
#include "preview/PreviewXdf.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <exception>

namespace vicon_lsl {
namespace {

// Fixed physical-stair placement from the Python preview auto-alignment.
// Z is the height axis in the Vicon/stair data; Y centers the walking path
// across the measured physical stair width.
constexpr PreviewVec3 kPhysicalStairTranslationM{
    -2.853343307500,
    0.292672723112,
    0.006432986454,
};

QDoubleSpinBox* makeDistanceSpin(double value = 0.0) {
    auto* spin = new QDoubleSpinBox();
    spin->setRange(-100.0, 100.0);
    spin->setDecimals(3);
    spin->setSingleStep(0.01);
    spin->setValue(value);
    return spin;
}

QDoubleSpinBox* makeAngleSpin(double value = 0.0) {
    auto* spin = new QDoubleSpinBox();
    spin->setRange(-360.0, 360.0);
    spin->setDecimals(2);
    spin->setSingleStep(1.0);
    spin->setValue(value);
    return spin;
}

} // namespace

PreviewPanel::PreviewPanel(QWidget* parent) : QWidget(parent) {
    qRegisterMetaType<vicon_lsl::PreviewFrame>("vicon_lsl::PreviewFrame");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    widget_ = new PreviewWidget();
    layout->addWidget(widget_, 1);

    auto* controls_group = new QGroupBox("Live Preview");
    auto* controls_layout = new QVBoxLayout(controls_group);
    controls_layout->setContentsMargins(8, 8, 8, 8);
    controls_layout->setSpacing(6);

    auto* stream_grid = new QGridLayout();
    stream_grid->setHorizontalSpacing(8);
    stream_grid->setVerticalSpacing(4);
    marker_stream_edit_ = new QLineEdit("ViconMarkers");
    segment_stream_edit_ = new QLineEdit("ViconSegments");
    gaze_stream_edit_ = new QLineEdit("HoloLensGaze");
    tolerance_spin_ = new QDoubleSpinBox();
    tolerance_spin_->setRange(0.001, 1.0);
    tolerance_spin_->setDecimals(3);
    tolerance_spin_->setSingleStep(0.005);
    tolerance_spin_->setValue(0.05);
    trail_points_spin_ = new QSpinBox();
    trail_points_spin_->setRange(2, 500);
    trail_points_spin_->setValue(24);
    playback_speed_spin_ = new QDoubleSpinBox();
    playback_speed_spin_->setRange(0.1, 4.0);
    playback_speed_spin_->setDecimals(1);
    playback_speed_spin_->setSingleStep(0.1);
    playback_speed_spin_->setValue(1.0);
    stream_grid->addWidget(new QLabel("Markers:"), 0, 0);
    stream_grid->addWidget(marker_stream_edit_, 0, 1);
    stream_grid->addWidget(new QLabel("Segments:"), 0, 2);
    stream_grid->addWidget(segment_stream_edit_, 0, 3);
    stream_grid->addWidget(new QLabel("Gaze:"), 1, 0);
    stream_grid->addWidget(gaze_stream_edit_, 1, 1);
    stream_grid->addWidget(new QLabel("Match tol. (s):"), 1, 2);
    stream_grid->addWidget(tolerance_spin_, 1, 3);
    stream_grid->addWidget(new QLabel("Trail points:"), 2, 0);
    stream_grid->addWidget(trail_points_spin_, 2, 1);
    stream_grid->addWidget(new QLabel("Playback speed:"), 2, 2);
    stream_grid->addWidget(playback_speed_spin_, 2, 3);
    stream_grid->setColumnStretch(1, 1);
    stream_grid->setColumnStretch(3, 1);
    controls_layout->addLayout(stream_grid);

    auto* transforms = new QGridLayout();
    transforms->setHorizontalSpacing(6);
    transforms->setVerticalSpacing(4);
    gaze_tx_spin_ = makeDistanceSpin();
    gaze_ty_spin_ = makeDistanceSpin();
    gaze_tz_spin_ = makeDistanceSpin();
    gaze_rx_spin_ = makeAngleSpin();
    gaze_ry_spin_ = makeAngleSpin();
    gaze_rz_spin_ = makeAngleSpin();
    transforms->addWidget(new QLabel("HoloLens T:"), 0, 0);
    transforms->addWidget(gaze_tx_spin_, 0, 1);
    transforms->addWidget(gaze_ty_spin_, 0, 2);
    transforms->addWidget(gaze_tz_spin_, 0, 3);
    transforms->addWidget(new QLabel("HoloLens R:"), 1, 0);
    transforms->addWidget(gaze_rx_spin_, 1, 1);
    transforms->addWidget(gaze_ry_spin_, 1, 2);
    transforms->addWidget(gaze_rz_spin_, 1, 3);
    controls_layout->addLayout(transforms);

    auto* stair_row = new QHBoxLayout();
    stair_model_edit_ = new QLineEdit();
    auto* browse_stair_button = new QPushButton("Browse");
    auto* reload_stair_button = new QPushButton("Reload Stair");
    stair_row->addWidget(new QLabel("Stair OBJ:"));
    stair_row->addWidget(stair_model_edit_, 1);
    stair_row->addWidget(browse_stair_button);
    stair_row->addWidget(reload_stair_button);
    controls_layout->addLayout(stair_row);

    auto* button_row = new QHBoxLayout();
    start_button_ = new QPushButton("Start Preview");
    stop_button_ = new QPushButton("Stop Preview");
    stop_button_->setEnabled(false);
    open_csv_button_ = new QPushButton("Open CSV");
    open_xdf_button_ = new QPushButton("Open XDF");
    play_csv_button_ = new QPushButton("Play Recording");
    play_csv_button_->setEnabled(false);
    status_label_ = new QLabel("Preview stopped");
    status_label_->setWordWrap(true);
    button_row->addWidget(start_button_);
    button_row->addWidget(stop_button_);
    button_row->addWidget(open_csv_button_);
    button_row->addWidget(open_xdf_button_);
    button_row->addWidget(play_csv_button_);
    button_row->addWidget(status_label_, 1);
    controls_layout->addLayout(button_row);

    layout->addWidget(controls_group);

    csv_timer_ = new QTimer(this);
    csv_timer_->setInterval(16);

    connect(start_button_, &QPushButton::clicked, this, &PreviewPanel::startPreview);
    connect(stop_button_, &QPushButton::clicked, this, &PreviewPanel::stopPreview);
    connect(open_csv_button_, &QPushButton::clicked, this, &PreviewPanel::openMergedCsv);
    connect(open_xdf_button_, &QPushButton::clicked, this, &PreviewPanel::openXdf);
    connect(play_csv_button_, &QPushButton::clicked, this, &PreviewPanel::toggleCsvPlayback);
    connect(csv_timer_, &QTimer::timeout, this, &PreviewPanel::advanceCsvPlayback);
    connect(browse_stair_button, &QPushButton::clicked, this, &PreviewPanel::browseStairModel);
    connect(reload_stair_button, &QPushButton::clicked, this, &PreviewPanel::reloadStairModel);
    connect(trail_points_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            widget_, &PreviewWidget::setTrailPointLimit);

    loadSettings();
    reloadStairModel();
}

PreviewPanel::~PreviewPanel() {
    stopPreview();
    saveSettings();
}

void PreviewPanel::startPreview() {
    stopPreview();
    csv_timer_->stop();
    play_csv_button_->setText("Play Recording");
    saveSettings();
    widget_->setTrailPointLimit(trail_points_spin_->value());

    PreviewWorkerConfig config;
    config.marker_stream_name = marker_stream_edit_->text().trimmed();
    config.segment_stream_name = segment_stream_edit_->text().trimmed();
    config.gaze_stream_name = gaze_stream_edit_->text().trimmed();
    config.match_tolerance_seconds = tolerance_spin_->value();
    config.vicon_transform.name = "Vicon";
    config.vicon_transform.scale = 0.001;
    config.gaze_transform = gazeTransform();

    worker_ = new PreviewStreamWorker(config, this);
    connect(worker_, &PreviewStreamWorker::frameReady, widget_, &PreviewWidget::setFrame);
    connect(worker_, &PreviewStreamWorker::statusChanged, this, &PreviewPanel::setStatus);
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &QThread::finished, this, [this]() {
        worker_ = nullptr;
        start_button_->setEnabled(true);
        stop_button_->setEnabled(false);
    });
    start_button_->setEnabled(false);
    stop_button_->setEnabled(true);
    setStatus("Preview resolving LSL streams...");
    worker_->start();
}

void PreviewPanel::stopPreview() {
    if (!worker_) {
        start_button_->setEnabled(true);
        stop_button_->setEnabled(false);
        return;
    }
    worker_->requestInterruption();
    worker_->wait(1000);
    worker_ = nullptr;
    start_button_->setEnabled(true);
    stop_button_->setEnabled(false);
    setStatus("Preview stopped");
}

void PreviewPanel::openMergedCsv() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open merged preview CSV",
        QString(),
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    stopPreview();
    csv_timer_->stop();
    play_csv_button_->setText("Play Recording");

    try {
        PreviewTransformProfile vicon_transform;
        vicon_transform.name = "Vicon";
        vicon_transform.scale = 0.001;
        const auto recording = loadMergedPreviewCsv(path.toStdString(), vicon_transform, gazeTransform());
        csv_frames_ = recording.frames;
        csv_frame_cursor_ = 0.0;
        if (csv_frames_.empty()) {
            play_csv_button_->setEnabled(false);
            setStatus("CSV has no preview frames");
            return;
        }
        widget_->setFrame(csv_frames_.front());
        play_csv_button_->setEnabled(true);
        setStatus("Loaded CSV: " + QFileInfo(path).fileName() + " (" +
                  QString::fromStdString(recording.summary) + ")");
    } catch (const std::exception& ex) {
        csv_frames_.clear();
        play_csv_button_->setEnabled(false);
        setStatus("Failed to load CSV: " + QString::fromStdString(ex.what()));
    }
}

void PreviewPanel::openXdf() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open recorded XDF",
        QString(),
        "XDF files (*.xdf);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    stopPreview();
    csv_timer_->stop();
    play_csv_button_->setText("Play Recording");

    try {
        PreviewTransformProfile vicon_transform;
        vicon_transform.name = "Vicon";
        vicon_transform.scale = 0.001;
        const auto recording = loadXdfPreviewRecording(path.toStdString(),
                                                       vicon_transform,
                                                       gazeTransform(),
                                                       tolerance_spin_->value());
        csv_frames_ = recording.frames;
        csv_frame_cursor_ = 0.0;
        if (csv_frames_.empty()) {
            play_csv_button_->setEnabled(false);
            setStatus("XDF has no preview frames");
            return;
        }
        widget_->setFrame(csv_frames_.front());
        play_csv_button_->setEnabled(true);
        setStatus("Loaded XDF: " + QFileInfo(path).fileName() + " (" +
                  QString::fromStdString(recording.summary) + ")");
    } catch (const std::exception& ex) {
        csv_frames_.clear();
        play_csv_button_->setEnabled(false);
        setStatus("Failed to load XDF: " + QString::fromStdString(ex.what()));
    }
}

void PreviewPanel::toggleCsvPlayback() {
    if (csv_frames_.empty()) {
        return;
    }
    if (csv_timer_->isActive()) {
        csv_timer_->stop();
        play_csv_button_->setText("Play Recording");
    } else {
        csv_timer_->start();
        play_csv_button_->setText("Pause Recording");
    }
}

void PreviewPanel::advanceCsvPlayback() {
    if (csv_frames_.empty()) {
        csv_timer_->stop();
        play_csv_button_->setText("Play Recording");
        return;
    }
    csv_frame_cursor_ += playback_speed_spin_->value();
    if (csv_frame_cursor_ >= static_cast<double>(csv_frames_.size())) {
        csv_frame_cursor_ = 0.0;
    }
    widget_->setFrame(csv_frames_[static_cast<std::size_t>(csv_frame_cursor_)]);
}

void PreviewPanel::browseStairModel() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Select stair OBJ",
        stair_model_edit_->text(),
        "Wavefront OBJ (*.obj);;All files (*)");
    if (!path.isEmpty()) {
        stair_model_edit_->setText(QDir::toNativeSeparators(path));
        reloadStairModel();
    }
}

void PreviewPanel::reloadStairModel() {
    const QString path = stair_model_edit_->text().trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        setStatus("Stair model not loaded");
        return;
    }

    try {
        const PreviewMesh mesh = loadObjMesh(QDir::toNativeSeparators(path).toStdString());
        widget_->setStairMesh(mesh, stairTransform());
        setStatus("Stair model loaded: " + QFileInfo(path).fileName());
    } catch (const std::exception& ex) {
        setStatus("Failed to load stair model: " + QString::fromStdString(ex.what()));
    }
}

PreviewTransformProfile PreviewPanel::gazeTransform() const {
    PreviewTransformProfile transform;
    transform.name = "HoloLens";
    transform.scale = 1.0;
    transform.translation = {gaze_tx_spin_->value(), gaze_ty_spin_->value(), gaze_tz_spin_->value()};
    transform.rotation_degrees = {gaze_rx_spin_->value(), gaze_ry_spin_->value(), gaze_rz_spin_->value()};
    return transform;
}

PreviewTransformProfile PreviewPanel::stairTransform() const {
    PreviewTransformProfile transform;
    transform.name = "Stair";
    transform.scale = 0.001;
    transform.translation = kPhysicalStairTranslationM;
    transform.rotation_degrees = {0.0, 0.0, 0.0};
    return transform;
}

void PreviewPanel::loadSettings() {
    QSettings settings("ViconLSL", "ViconLSLBridge");
    marker_stream_edit_->setText(settings.value("preview/markerStream", "ViconMarkers").toString());
    segment_stream_edit_->setText(settings.value("preview/segmentStream", "ViconSegments").toString());
    gaze_stream_edit_->setText(settings.value("preview/gazeStream", "HoloLensGaze").toString());
    tolerance_spin_->setValue(settings.value("preview/matchTolerance", 0.05).toDouble());
    trail_points_spin_->setValue(settings.value("preview/trailPoints", 24).toInt());
    playback_speed_spin_->setValue(settings.value("preview/playbackSpeed", 1.0).toDouble());
    gaze_tx_spin_->setValue(settings.value("preview/gazeTx", 0.0).toDouble());
    gaze_ty_spin_->setValue(settings.value("preview/gazeTy", 0.0).toDouble());
    gaze_tz_spin_->setValue(settings.value("preview/gazeTz", 0.0).toDouble());
    gaze_rx_spin_->setValue(settings.value("preview/gazeRx", 0.0).toDouble());
    gaze_ry_spin_->setValue(settings.value("preview/gazeRy", 0.0).toDouble());
    gaze_rz_spin_->setValue(settings.value("preview/gazeRz", 0.0).toDouble());
    stair_model_edit_->setText(settings.value("preview/stairModel", defaultStairModelPath()).toString());
}

void PreviewPanel::saveSettings() const {
    QSettings settings("ViconLSL", "ViconLSLBridge");
    settings.setValue("preview/markerStream", marker_stream_edit_->text());
    settings.setValue("preview/segmentStream", segment_stream_edit_->text());
    settings.setValue("preview/gazeStream", gaze_stream_edit_->text());
    settings.setValue("preview/matchTolerance", tolerance_spin_->value());
    settings.setValue("preview/trailPoints", trail_points_spin_->value());
    settings.setValue("preview/playbackSpeed", playback_speed_spin_->value());
    settings.setValue("preview/gazeTx", gaze_tx_spin_->value());
    settings.setValue("preview/gazeTy", gaze_ty_spin_->value());
    settings.setValue("preview/gazeTz", gaze_tz_spin_->value());
    settings.setValue("preview/gazeRx", gaze_rx_spin_->value());
    settings.setValue("preview/gazeRy", gaze_ry_spin_->value());
    settings.setValue("preview/gazeRz", gaze_rz_spin_->value());
    settings.setValue("preview/stairModel", stair_model_edit_->text());
}

QString PreviewPanel::defaultStairModelPath() const {
    const QString app_dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(app_dir).filePath("stair_model/stair_model1.obj"),
        QDir::current().filePath("stair_model/stair_model1.obj"),
        QDir::current().filePath("vicon-lsl-bridge-windows-x64/stair_model/stair_model1.obj"),
        QDir(QDir::currentPath()).filePath("../vicon-lsl-bridge-windows-x64/stair_model/stair_model1.obj"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }
    return {};
}

void PreviewPanel::setStatus(const QString& status) {
    status_label_->setText(status);
}

} // namespace vicon_lsl
