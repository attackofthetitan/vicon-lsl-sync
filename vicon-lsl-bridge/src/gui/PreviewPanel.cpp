#include "gui/PreviewPanel.h"

#include "preview/ObjMesh.h"
#include "preview/PreviewCsv.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewMath.h"
#include "preview/PreviewXdf.h"
#include "StreamDefaults.h"

#include <QCoreApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QProgressBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QSlider>
#include <QMimeData>
#include <QSignalBlocker>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>
#include "gui/PerformanceBudgets.h"

#include <exception>
#include <map>
#include <optional>
#include <utility>

namespace vicon_lsl {
namespace {

QLabel* makeTooltipLabel(const QString& text, QWidget* control, const QString& tooltip) {
    auto* label = new QLabel(text);
    label->setToolTip(tooltip);
    if (control) {
        control->setToolTip(tooltip);
    }
    return label;
}

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

PreviewPanel::PreviewPanel(QWidget* parent, gui::GuiServices services)
    : QWidget(parent), services_(std::move(services)) {
    qRegisterMetaType<vicon_lsl::PreviewFrame>("vicon_lsl::PreviewFrame");
    qRegisterMetaType<vicon_lsl::CalibrationTargetPose>("vicon_lsl::CalibrationTargetPose");
    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    widget_ = new PreviewWidget();
    layout->addWidget(widget_, 1);

    auto* controls_group = new QGroupBox("Live Preview");
    auto* controls_layout = new QVBoxLayout(controls_group);
    controls_layout->setContentsMargins(8, 8, 8, 8);
    controls_layout->setSpacing(6);

    auto* settings_tabs = new QTabWidget();
    auto* sources_page = new QWidget();
    auto* sources_layout = new QVBoxLayout(sources_page);
    sources_layout->setContentsMargins(6, 6, 6, 6);
    sources_layout->setSpacing(6);
    auto* alignment_page = new QWidget();
    auto* alignment_layout = new QVBoxLayout(alignment_page);
    alignment_layout->setContentsMargins(6, 6, 6, 6);
    alignment_layout->setSpacing(6);

    auto* stream_grid = new QGridLayout();
    stream_grid->setHorizontalSpacing(8);
    stream_grid->setVerticalSpacing(4);
    marker_stream_edit_ = new QLineEdit(stream_defaults::ViconMarkers);
    segment_stream_edit_ = new QLineEdit(stream_defaults::ViconSegments);
    gaze_stream_edit_ = new QLineEdit(stream_defaults::HoloLensGaze);
    calibration_stream_edit_ = new QLineEdit(stream_defaults::HoloLensModelTargetPose);
    tolerance_spin_ = new QDoubleSpinBox();
    tolerance_spin_->setRange(0.001, 1.0);
    tolerance_spin_->setDecimals(3);
    tolerance_spin_->setSingleStep(0.005);
    tolerance_spin_->setValue(0.05);
    cache_megabytes_spin_ = new QSpinBox();
    cache_megabytes_spin_->setRange(16, 2048);
    cache_megabytes_spin_->setSingleStep(16);
    cache_megabytes_spin_->setSuffix(" MiB");
    cache_megabytes_spin_->setValue(128);
    trail_points_spin_ = new QSpinBox();
    trail_points_spin_->setRange(2, 500);
    trail_points_spin_->setValue(24);
    playback_speed_spin_ = new QDoubleSpinBox();
    playback_speed_spin_->setRange(0.1, 4.0);
    playback_speed_spin_->setDecimals(1);
    playback_speed_spin_->setSingleStep(0.1);
    playback_speed_spin_->setValue(1.0);
    stream_grid->addWidget(makeTooltipLabel(
                               "Markers:", marker_stream_edit_,
                               "LSL stream containing Vicon marker samples for the preview."),
                           0, 0);
    stream_grid->addWidget(marker_stream_edit_, 0, 1);
    stream_grid->addWidget(makeTooltipLabel(
                               "Segments:", segment_stream_edit_,
                               "LSL stream containing Vicon segment samples for the preview."),
                           0, 2);
    stream_grid->addWidget(segment_stream_edit_, 0, 3);
    stream_grid->addWidget(makeTooltipLabel(
                               "Gaze:", gaze_stream_edit_,
                               "LSL stream containing HoloLens gaze samples."),
                           1, 0);
    stream_grid->addWidget(gaze_stream_edit_, 1, 1);
    stream_grid->addWidget(makeTooltipLabel(
                               "Match tol. (s):", tolerance_spin_,
                               "Maximum timestamp gap, in seconds, when matching preview samples."),
                           1, 2);
    stream_grid->addWidget(tolerance_spin_, 1, 3);
    stream_grid->addWidget(makeTooltipLabel(
                               "Trail points:", trail_points_spin_,
                               "Number of recent preview frames retained in the trail."),
                           2, 0);
    stream_grid->addWidget(trail_points_spin_, 2, 1);
    stream_grid->addWidget(makeTooltipLabel(
                               "Playback speed:", playback_speed_spin_,
                               "Playback speed multiplier for CSV and XDF recordings."),
                           2, 2);
    stream_grid->addWidget(playback_speed_spin_, 2, 3);
    stream_grid->addWidget(makeTooltipLabel(
                               "Stair target:", calibration_stream_edit_,
                               "LSL stream containing tracked stair-target poses for calibration."),
                           3, 0);
    stream_grid->addWidget(calibration_stream_edit_, 3, 1);
    stream_grid->addWidget(makeTooltipLabel(
                               "Playback cache:", cache_megabytes_spin_,
                               "Maximum memory retained by each indexed or decoded playback cache."),
                           3, 2);
    stream_grid->addWidget(cache_megabytes_spin_, 3, 3);
    stream_grid->setColumnStretch(1, 1);
    stream_grid->setColumnStretch(3, 1);
    sources_layout->addLayout(stream_grid);

    auto* transforms = new QGridLayout();
    transforms->setHorizontalSpacing(6);
    transforms->setVerticalSpacing(4);
    gaze_tx_spin_ = makeDistanceSpin();
    gaze_ty_spin_ = makeDistanceSpin();
    gaze_tz_spin_ = makeDistanceSpin();
    gaze_rx_spin_ = makeAngleSpin();
    gaze_ry_spin_ = makeAngleSpin();
    gaze_rz_spin_ = makeAngleSpin();
    auto* translation_label = new QLabel("HoloLens T:");
    translation_label->setToolTip("Manual HoloLens translation in metres (X, Y, Z).\n"
                                  "Used when manual transform mode is selected.");
    gaze_tx_spin_->setToolTip("Manual HoloLens translation X in metres.");
    gaze_ty_spin_->setToolTip("Manual HoloLens translation Y in metres.");
    gaze_tz_spin_->setToolTip("Manual HoloLens translation Z in metres.");
    transforms->addWidget(translation_label, 0, 0);
    transforms->addWidget(gaze_tx_spin_, 0, 1);
    transforms->addWidget(gaze_ty_spin_, 0, 2);
    transforms->addWidget(gaze_tz_spin_, 0, 3);
    auto* rotation_label = new QLabel("HoloLens R:");
    rotation_label->setToolTip("Manual HoloLens rotation in degrees about X, Y, and Z.\n"
                               "Used when manual transform mode is selected.");
    gaze_rx_spin_->setToolTip("Manual HoloLens rotation X in degrees.");
    gaze_ry_spin_->setToolTip("Manual HoloLens rotation Y in degrees.");
    gaze_rz_spin_->setToolTip("Manual HoloLens rotation Z in degrees.");
    transforms->addWidget(rotation_label, 1, 0);
    transforms->addWidget(gaze_rx_spin_, 1, 1);
    transforms->addWidget(gaze_ry_spin_, 1, 2);
    transforms->addWidget(gaze_rz_spin_, 1, 3);
    alignment_layout->addLayout(transforms);

    auto* calibration_row = new QHBoxLayout();
    calibrate_button_ = new QPushButton("Calibrate from Stair Target");
    use_manual_transform_button_ = new QPushButton("Use Manual Transform");
    calibrate_button_->setToolTip(
        "Collect stable poses from the stair-target stream and apply a HoloLens transform for this session only.");
    use_manual_transform_button_->setToolTip(
        "Stop automatic calibration and use the manual HoloLens translation and rotation fields.");
    calibration_row->addWidget(calibrate_button_);
    calibration_row->addWidget(use_manual_transform_button_);
    calibration_row->addStretch(1);
    alignment_layout->addLayout(calibration_row);
    alignment_layout->addStretch(1);

    auto* stair_row = new QHBoxLayout();
    stair_model_edit_ = new QLineEdit();
    auto* browse_stair_button = new QPushButton("Browse");
    stair_row->addWidget(makeTooltipLabel(
                              "Stair OBJ:", stair_model_edit_,
                              "Wavefront OBJ file used to render the stair target."));
    stair_row->addWidget(stair_model_edit_, 1);
    stair_row->addWidget(browse_stair_button);
    sources_layout->addLayout(stair_row);

    settings_tabs->addTab(sources_page, "Sources");
    settings_tabs->addTab(alignment_page, "Alignment");
    controls_layout->addWidget(settings_tabs);

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

    auto* load_row = new QHBoxLayout();
    file_state_label_ = new QLabel("No recording loaded");
    memory_label_ = new QLabel("cache 0 MiB");
    load_progress_ = new QProgressBar();
    load_progress_->setRange(0, 100);
    load_progress_->setValue(0);
    load_progress_->setVisible(false);
    cancel_load_button_ = new QPushButton("Cancel Load");
    cancel_load_button_->setEnabled(false);
    cancel_load_button_->setToolTip("Cancel the background file load and keep the current source.");
    recent_files_combo_ = new QComboBox();
    recent_files_combo_->setMinimumContentsLength(16);
    recent_files_combo_->setToolTip("Recently opened CSV and XDF recordings.");
    auto* open_recent_button = new QPushButton("Open Recent");
    open_recent_button->setToolTip("Open the selected recent recording.");
    load_row->addWidget(file_state_label_, 1);
    load_row->addWidget(memory_label_);
    load_row->addWidget(load_progress_);
    load_row->addWidget(cancel_load_button_);
    load_row->addWidget(recent_files_combo_);
    load_row->addWidget(open_recent_button);
    controls_layout->addLayout(load_row);

    auto* playback_row = new QHBoxLayout();
    auto* jump_start_button = new QPushButton("|<");
    auto* step_back_button = new QPushButton("<");
    auto* jump_back_button = new QPushButton("- Jump");
    auto* jump_forward_button = new QPushButton("Jump +");
    auto* step_forward_button = new QPushButton(">");
    auto* jump_end_button = new QPushButton(">|");
    jump_start_button->setAccessibleName("Jump to recording start");
    step_back_button->setAccessibleName("Step one frame backward");
    jump_back_button->setAccessibleName("Jump backward by selected time");
    jump_forward_button->setAccessibleName("Jump forward by selected time");
    step_forward_button->setAccessibleName("Step one frame forward");
    jump_end_button->setAccessibleName("Jump to recording end");
    jump_start_button->setToolTip("Seek to the first cached frame.");
    step_back_button->setToolTip("Seek one cached frame backward.");
    jump_back_button->setToolTip("Seek backward by the selected time interval.");
    jump_forward_button->setToolTip("Seek forward by the selected time interval.");
    step_forward_button->setToolTip("Seek one cached frame forward.");
    jump_end_button->setToolTip("Seek to the final cached frame.");
    timeline_slider_ = new QSlider(Qt::Horizontal);
    timeline_slider_->setRange(0, 10000);
    timeline_slider_->setEnabled(false);
    timeline_slider_->setAccessibleName("Recording playback timeline");
    jump_seconds_spin_ = new QDoubleSpinBox();
    jump_seconds_spin_->setRange(0.1, 600.0);
    jump_seconds_spin_->setValue(5.0);
    jump_seconds_spin_->setSuffix(" s");
    jump_seconds_spin_->setToolTip("Time used by the backward and forward Jump controls.");
    loop_playback_check_ = new QCheckBox("Loop");
    loop_playback_check_->setToolTip("Restart explicitly at the end of the recording.");
    playback_position_label_ = new QLabel("0.000 / 0.000 s | frame 0/0");
    playback_row->addWidget(jump_start_button);
    playback_row->addWidget(step_back_button);
    playback_row->addWidget(jump_back_button);
    playback_row->addWidget(jump_seconds_spin_);
    playback_row->addWidget(jump_forward_button);
    playback_row->addWidget(step_forward_button);
    playback_row->addWidget(jump_end_button);
    playback_row->addWidget(timeline_slider_, 1);
    playback_row->addWidget(loop_playback_check_);
    playback_row->addWidget(playback_position_label_);
    controls_layout->addLayout(playback_row);

    auto* controls_scroll = new QScrollArea();
    controls_scroll->setWidgetResizable(true);
    controls_scroll->setFrameShape(QFrame::NoFrame);
    controls_scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    controls_scroll->setAccessibleName("Scrollable preview controls");
    controls_scroll->setWidget(controls_group);
    controls_scroll->setMinimumSize(0, 0);
    controls_scroll->setMaximumHeight(390);
    layout->addWidget(controls_scroll);

    csv_timer_ = new QTimer(this);
    csv_timer_->setInterval(16);
    playback_elapsed_.start();

    connect(start_button_, &QPushButton::clicked, this, &PreviewPanel::startPreview);
    connect(stop_button_, &QPushButton::clicked, this, &PreviewPanel::stopPreview);
    connect(open_csv_button_, &QPushButton::clicked, this, &PreviewPanel::openMergedCsv);
    connect(open_xdf_button_, &QPushButton::clicked, this, &PreviewPanel::openXdf);
    connect(play_csv_button_, &QPushButton::clicked, this, &PreviewPanel::toggleCsvPlayback);
    connect(csv_timer_, &QTimer::timeout, this, &PreviewPanel::advanceCsvPlayback);
    connect(playback_speed_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double speed) {
                playback_clock_.setSpeed(speed, playback_elapsed_.elapsed() / 1000.0);
            });
    connect(browse_stair_button, &QPushButton::clicked, this, &PreviewPanel::browseStairModel);
    connect(stair_model_edit_, &QLineEdit::editingFinished,
            this, &PreviewPanel::reloadStairModel);
    connect(calibrate_button_, &QPushButton::clicked, this, &PreviewPanel::beginCalibration);
    connect(use_manual_transform_button_, &QPushButton::clicked, this, &PreviewPanel::useManualTransform);
    connect(trail_points_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            widget_, &PreviewWidget::setTrailPointLimit);

    loadSettings();
    reloadStairModel();
}

PreviewPanel::~PreviewPanel() {
    if (file_loader_) {
        file_loader_->cancel();
        file_loader_->disconnect(this);
        file_loader_->setParent(nullptr);
        file_loader_ = nullptr;
    }
    if (worker_) {
        worker_->requestInterruption();
        worker_->wait();
    }
    saveSettings();
}

bool PreviewPanel::configurableTooltipsPresent() const {
    const QWidget* const controls[] = {
        marker_stream_edit_,
        segment_stream_edit_,
        gaze_stream_edit_,
        calibration_stream_edit_,
        stair_model_edit_,
        tolerance_spin_,
        cache_megabytes_spin_,
        playback_speed_spin_,
        gaze_tx_spin_,
        gaze_ty_spin_,
        gaze_tz_spin_,
        gaze_rx_spin_,
        gaze_ry_spin_,
        gaze_rz_spin_,
        trail_points_spin_,
        calibrate_button_,
        use_manual_transform_button_,
    };
    for (const QWidget* control : controls) {
        if (!control || control->toolTip().trimmed().isEmpty()) {
            return false;
        }
    }
    return true;
}

void PreviewPanel::startPreview() {
    if (worker_) {
        stopPreview();
        if (worker_) {
            return;
        }
    }
    csv_timer_->stop();
    play_csv_button_->setText("Play Recording");
    saveSettings();
    widget_->setTrailPointLimit(trail_points_spin_->value());
    widget_->resetForNewSource();
    calibration_samples_.clear();
    automatic_gaze_transform_ = {};
    calibration_state_ = CalibrationState::Collecting;

    PreviewWorkerConfig config;
    config.marker_stream_name = marker_stream_edit_->text().trimmed();
    config.segment_stream_name = segment_stream_edit_->text().trimmed();
    config.gaze_stream_name = gaze_stream_edit_->text().trimmed();
    config.calibration_stream_name = calibration_stream_edit_->text().trimmed();
    config.match_tolerance_seconds = tolerance_spin_->value();
    config.vicon_transform.name = "Vicon";
    config.vicon_transform.scale = 0.001;
    config.gaze_transform = gazeTransform();

    worker_ = new PreviewStreamWorker(config, this);
    worker_stopping_ = false;
    PreviewStreamWorker* const started_worker = worker_;
    connect(worker_, &PreviewStreamWorker::frameReady, widget_, &PreviewWidget::setFrame);
    connect(worker_, &PreviewStreamWorker::targetPoseReady, this, &PreviewPanel::handleTargetPose);
    connect(worker_, &PreviewStreamWorker::statusChanged, this, &PreviewPanel::setStatus);
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &QThread::finished, this, [this, started_worker]() {
        if (worker_ != started_worker) {
            return;
        }
        worker_ = nullptr;
        worker_stopping_ = false;
        start_button_->setEnabled(true);
        stop_button_->setEnabled(false);
        open_csv_button_->setEnabled(true);
        open_xdf_button_->setEnabled(true);
        if (pending_recording_open_ != PendingRecordingOpen::None) {
            processPendingRecordingOpen();
        } else {
            setStatus("Preview stopped");
        }
    });
    start_button_->setEnabled(false);
    stop_button_->setEnabled(true);
    setStatus("Preview resolving LSL streams and calibrating from the stair target...");
    worker_->start();
}

void PreviewPanel::stopPreview() {
    resetCalibrationSession();
    if (!worker_) {
        worker_stopping_ = false;
        start_button_->setEnabled(true);
        stop_button_->setEnabled(false);
        return;
    }
    if (worker_stopping_) {
        return;
    }

    worker_stopping_ = true;
    PreviewStreamWorker* const stopping_worker = worker_;
    stopping_worker->requestInterruption();
    start_button_->setEnabled(false);
    stop_button_->setEnabled(false);
    open_csv_button_->setEnabled(false);
    open_xdf_button_->setEnabled(false);
    setStatus("Preview stopping...");
    if (!stopping_worker->wait(1000)) {
        setStatus("Preview is still stopping; restart is disabled until it finishes");
    }
}

void PreviewPanel::beginCalibration() {
    if (!worker_) {
        setStatus("Start the preview before calibrating from the stair target");
        return;
    }
    calibration_samples_.clear();
    calibration_state_ = CalibrationState::Collecting;
    const auto& profile = defaultStairCalibrationProfile();
    setStatus("Waiting for " + QString::number(profile.required_samples) +
              " stable tracked stair-target poses...");
}

void PreviewPanel::useManualTransform() {
    calibration_state_ = CalibrationState::Manual;
    calibration_samples_.clear();
    if (worker_) {
        worker_->setGazeTransform(gazeTransform());
    }
    widget_->requestViewRefit();
    saveSettings();
    setStatus("Using manual HoloLens transform");
}

void PreviewPanel::handleTargetPose(CalibrationTargetPose pose) {
    const auto& profile = defaultStairCalibrationProfile();
    if (calibration_state_ != CalibrationState::Collecting) {
        return;
    }
    if (!pose.tracked) {
        calibration_samples_.clear();
        setStatus("Stair target lost; waiting for a stable acquisition...");
        return;
    }

    if (!calibration_samples_.empty() &&
        !targetPoseWithinTolerance(calibration_samples_.front(), pose, profile)) {
        calibration_samples_.clear();
        calibration_samples_.push_back(pose);
        setStatus("Stair target moved; restarting stable-pose collection (1/" +
                  QString::number(static_cast<qulonglong>(profile.required_samples)) + ")");
        return;
    }

    calibration_samples_.push_back(pose);
    if (calibration_samples_.size() < profile.required_samples) {
        setStatus("Collecting stair-target poses: " +
                  QString::number(calibration_samples_.size()) + "/" +
                  QString::number(profile.required_samples));
        return;
    }

    const auto solution = solveTrackedTargetCalibration(calibration_samples_, profile);
    calibration_state_ = CalibrationState::Manual;
    calibration_samples_.clear();
    if (!solution) {
        setStatus("Calibration failed: stair-target motion exceeded quality limits");
        return;
    }

    automatic_gaze_transform_ = gazeTransformFromTargetCalibration(
        profile,
        solution->holo_from_target);
    calibration_state_ = CalibrationState::AutomaticSession;
    if (worker_) {
        worker_->setGazeTransform(gazeTransform());
    }
    widget_->requestViewRefit();
    setStatus("Stair-target calibration " + QString::fromStdString(profile.id) +
              " applied for this session (RMS " +
              QString::number(solution->quality.translation_rms_m * 1000.0, 'f', 1) +
              " mm, " + QString::number(solution->quality.rotation_rms_degrees, 'f', 2) +
              " deg)");
}

void PreviewPanel::openMergedCsv() {
    const QString path = services_.file_dialogs->openFile(
        this, "Open merged preview CSV", QString(),
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    if (worker_) {
        pending_recording_open_ = PendingRecordingOpen::Csv;
        pending_recording_path_ = path;
        stopPreview();
        if (worker_) {
            setStatus("Stopping preview before opening " + QFileInfo(path).fileName() + "...");
            return;
        }
    }

    loadMergedCsv(path);
}

void PreviewPanel::loadMergedCsv(const QString& path) {
    startFileLoad(PreviewFileType::Csv, path);
}

void PreviewPanel::openXdf() {
    const QString path = services_.file_dialogs->openFile(
        this, "Open recorded XDF", QString(),
        "XDF files (*.xdf);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    if (worker_) {
        pending_recording_open_ = PendingRecordingOpen::Xdf;
        pending_recording_path_ = path;
        stopPreview();
        if (worker_) {
            setStatus("Stopping preview before opening " + QFileInfo(path).fileName() + "...");
            return;
        }
    }

    loadXdf(path);
}

void PreviewPanel::loadXdf(const QString& path) {
    startFileLoad(PreviewFileType::Xdf, path);
}

void PreviewPanel::startFileLoad(PreviewFileType type, const QString& path) {
    if (file_loader_) {
        setStatus("A recording is already loading; cancel it before opening another file");
        return;
    }
    PreviewTransformProfile vicon_transform;
    vicon_transform.name = "Vicon";
    vicon_transform.scale = 0.001;
    PreviewLoadOptions options;
    options.maximum_preview_frames = gui::PerformanceBudgets::MaximumPreviewFrames;
    options.maximum_memory_bytes =
        static_cast<std::size_t>(cache_megabytes_spin_->value()) * 1024ULL * 1024ULL;
    options.maximum_stored_values_per_stream =
        gui::PerformanceBudgets::MaximumStoredValuesPerXdfStream;
    options.maximum_file_bytes = gui::PerformanceBudgets::MaximumXdfFileBytes;
    options.maximum_samples_per_stream =
        gui::PerformanceBudgets::MaximumSamplesPerXdfStream;
    options.maximum_channels = gui::PerformanceBudgets::MaximumXdfChannels;
    options.maximum_streams = gui::PerformanceBudgets::MaximumXdfStreams;
    options.maximum_header_bytes = gui::PerformanceBudgets::MaximumHeaderBytes;
    options.cancellation_check_sample_interval =
        gui::PerformanceBudgets::FileCancelSampleInterval;

    auto* loader = services_.create_file_loader(
        type, path, vicon_transform, gazeTransform(), tolerance_spin_->value(),
        options, this);
    file_loader_ = loader;
    file_state_label_->setText("Loading " + QFileInfo(path).fileName());
    emit fileStateChanged(gui::SessionFileState::Loading,
                          "Loading " + QFileInfo(path).fileName());
    load_progress_->setValue(0);
    load_progress_->setVisible(true);
    cancel_load_button_->setEnabled(true);
    open_csv_button_->setEnabled(false);
    open_xdf_button_->setEnabled(false);
    connect(loader, &PreviewFileLoader::progressChanged, this,
            [this, loader](const QString& stage, int percent, const QString& detail) {
                if (file_loader_ != loader) return;
                load_progress_->setValue(percent);
                file_state_label_->setText(stage + (detail.isEmpty() ? QString() : ": " + detail));
            });
    connect(loader, &PreviewFileLoader::mappingRequired, this,
            [this, loader](const XdfMappingAnalysis& analysis) {
                requestRecordedStreamMapping(loader, analysis);
            });
    connect(loader, &PreviewFileLoader::loadSucceeded, this,
            [this, loader](const QString& summary) {
                applyLoadedRecording(loader, summary);
            });
    connect(loader, &PreviewFileLoader::loadFailed, this,
            [this, loader](const QString& error, bool canceled) {
                if (file_loader_ != loader) return;
                file_state_label_->setText(canceled ? "Load canceled" : "Load failed");
                emit fileStateChanged(canceled ? gui::SessionFileState::Canceled
                                               : gui::SessionFileState::Failed,
                                      error);
                setStatus((canceled ? "Canceled file load; previous source retained: "
                                    : "Failed to load recording; previous source retained: ") + error);
            });
    connect(loader, &QThread::finished, loader, &QObject::deleteLater);
    connect(loader, &QThread::finished, this, [this, loader]() {
        if (file_loader_ != loader) return;
        file_loader_ = nullptr;
        load_progress_->setVisible(false);
        cancel_load_button_->setEnabled(false);
        open_csv_button_->setEnabled(true);
        open_xdf_button_->setEnabled(true);
    });
    loader->start();
}

void PreviewPanel::applyLoadedRecording(PreviewFileLoader* loader,
                                        const QString& summary) {
    if (file_loader_ != loader) return;
    std::optional<PreviewRecording> loaded = loader->takeRecording();
    if (!loaded || loaded->frames.empty()) {
        setStatus("Recording contained no usable preview frames; previous source retained");
        file_state_label_->setText("No usable frames");
        return;
    }
    csv_timer_->stop();
    play_csv_button_->setText("Play Recording");
    widget_->resetForNewSource();
    csv_frames_ = std::move(loaded->frames);
    playback_clock_.setFrameTimeline(csv_frames_);
    playback_clock_.setLooping(loop_playback_check_->isChecked(),
                               playback_elapsed_.elapsed() / 1000.0);
    playback_clock_.setSpeed(playback_speed_spin_->value(),
                             playback_elapsed_.elapsed() / 1000.0);
    widget_->setFrame(csv_frames_.front());
    play_csv_button_->setEnabled(true);
    timeline_slider_->setEnabled(true);
    memory_label_->setText("cache " + QString::number(
        static_cast<double>(loaded->estimated_memory_bytes) / (1024.0 * 1024.0), 'f', 1) +
        " MiB");
    file_state_label_->setText("Loaded " + QFileInfo(loader->path()).fileName());
    current_recording_path_ = QDir::toNativeSeparators(
        QFileInfo(loader->path()).absoluteFilePath());
    emit fileStateChanged(gui::SessionFileState::Loaded, current_recording_path_);
    rememberRecentFile(loader->path());
    updatePlaybackDisplay();
    setStatus("Loaded " + QFileInfo(loader->path()).fileName() + " (" + summary + ")");
}

void PreviewPanel::requestRecordedStreamMapping(
    PreviewFileLoader* loader,
    const XdfMappingAnalysis& analysis) {
    if (file_loader_ != loader) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Map recorded streams");
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(QString::fromStdString(analysis.explanation));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* form = new QFormLayout();
    std::map<PreviewStreamRole, QComboBox*> role_combos;
    const PreviewStreamRole roles[] = {
        PreviewStreamRole::ViconMarkers, PreviewStreamRole::ViconSegments,
        PreviewStreamRole::HoloLensGaze, PreviewStreamRole::HoloLensCalibrationTarget,
    };
    for (PreviewStreamRole role : roles) {
        auto* combo = new QComboBox();
        QSet<QString> seen_groups;
        for (const XdfStreamCandidate& candidate : analysis.candidates) {
            if (candidate.role != role) continue;
            const QString group = QString::fromStdString(candidate.group_key);
            if (seen_groups.contains(group)) continue;
            seen_groups.insert(group);
            combo->addItem(QString::fromStdString(candidate.display_name + " | source " +
                (candidate.source_id.empty() ? "<missing>" : candidate.source_id) +
                " | host " + candidate.hostname + " | " +
                std::to_string(candidate.sample_count) + " samples"),
                static_cast<qulonglong>(candidate.stream_id));
        }
        if (combo->count() > 0) {
            const QString label = role == PreviewStreamRole::ViconMarkers ? "Markers:" :
                                  role == PreviewStreamRole::ViconSegments ? "Segments:" :
                                  role == PreviewStreamRole::HoloLensGaze ? "Gaze:" :
                                  "Calibration:";
            form->addRow(label, combo);
            role_combos[role] = combo;
        } else {
            delete combo;
        }
    }
    auto* master_combo = new QComboBox();
    for (const XdfStreamCandidate& candidate : analysis.candidates) {
        if (candidate.role == PreviewStreamRole::HoloLensCalibrationTarget) continue;
        master_combo->addItem(QString::fromStdString(candidate.display_name + " | " +
            std::to_string(candidate.sample_count) + " samples"),
            static_cast<qulonglong>(candidate.stream_id));
    }
    const int suggested_master = master_combo->findData(
        static_cast<qulonglong>(analysis.suggested_mapping.master_stream_id));
    if (suggested_master >= 0) master_combo->setCurrentIndex(suggested_master);
    form->addRow("Master timeline:", master_combo);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        loader->cancel();
        return;
    }
    XdfStreamMapping mapping;
    mapping.master_stream_id = static_cast<std::uint32_t>(master_combo->currentData().toULongLong());
    for (const auto& item : role_combos) {
        mapping.selected_stream_ids.push_back(
            static_cast<std::uint32_t>(item.second->currentData().toULongLong()));
    }
    loader->provideMapping(mapping);
}

void PreviewPanel::processPendingRecordingOpen() {
    const PendingRecordingOpen pending = pending_recording_open_;
    const QString path = pending_recording_path_;
    pending_recording_open_ = PendingRecordingOpen::None;
    pending_recording_path_.clear();
    if (pending == PendingRecordingOpen::Csv) {
        loadMergedCsv(path);
    } else if (pending == PendingRecordingOpen::Xdf) {
        loadXdf(path);
    }
}

void PreviewPanel::toggleCsvPlayback() {
    if (csv_frames_.empty()) {
        return;
    }
    if (csv_timer_->isActive()) {
        playback_clock_.pause(playback_elapsed_.elapsed() / 1000.0);
        csv_timer_->stop();
        play_csv_button_->setText("Play Recording");
    } else {
        if (playback_clock_.atEnd(playback_elapsed_.elapsed() / 1000.0)) {
            playback_clock_.seek(0.0, playback_elapsed_.elapsed() / 1000.0);
        }
        playback_clock_.play(playback_elapsed_.elapsed() / 1000.0);
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
    const double now = playback_elapsed_.elapsed() / 1000.0;
    widget_->setFrame(csv_frames_[playback_clock_.frameIndex(now)]);
    updatePlaybackDisplay();
    if (playback_clock_.atEnd(now)) {
        playback_clock_.pause(now);
        csv_timer_->stop();
        play_csv_button_->setText("Play Recording");
    }
}

void PreviewPanel::cancelFileLoad() {
    if (file_loader_) {
        file_loader_->cancel();
        cancel_load_button_->setEnabled(false);
        file_state_label_->setText("Canceling load...");
    }
}

void PreviewPanel::seekPlaybackFromSlider(int value) {
    if (csv_frames_.empty()) return;
    const double duration = playback_clock_.duration();
    playback_clock_.seek(duration * static_cast<double>(value) /
                             static_cast<double>(timeline_slider_->maximum()),
                         playback_elapsed_.elapsed() / 1000.0);
    const std::size_t index = playback_clock_.frameIndex(playback_elapsed_.elapsed() / 1000.0);
    widget_->setFrame(csv_frames_[index]);
    updatePlaybackDisplay();
}

void PreviewPanel::seekToFrame(std::size_t frame_index) {
    if (csv_frames_.empty()) return;
    frame_index = (std::min)(frame_index, csv_frames_.size() - 1);
    const double position = csv_frames_[frame_index].timestamp - csv_frames_.front().timestamp;
    playback_clock_.seek(position, playback_elapsed_.elapsed() / 1000.0);
    widget_->setFrame(csv_frames_[frame_index]);
    updatePlaybackDisplay();
}

void PreviewPanel::seekBySeconds(double seconds) {
    if (csv_frames_.empty()) return;
    const double now = playback_elapsed_.elapsed() / 1000.0;
    playback_clock_.seek(playback_clock_.position(now) + seconds, now);
    const std::size_t index = playback_clock_.frameIndex(now);
    widget_->setFrame(csv_frames_[index]);
    updatePlaybackDisplay();
}

void PreviewPanel::updatePlaybackDisplay() {
    if (csv_frames_.empty()) {
        playback_position_label_->setText("0.000 / 0.000 s | frame 0/0");
        return;
    }
    const double now = playback_elapsed_.elapsed() / 1000.0;
    const double position = playback_clock_.position(now);
    const std::size_t index = playback_clock_.frameIndex(now);
    {
        const QSignalBlocker blocker(timeline_slider_);
        const int slider_value = playback_clock_.duration() <= 0.0
            ? 0
            : static_cast<int>(timeline_slider_->maximum() *
                position / playback_clock_.duration());
        timeline_slider_->setValue(slider_value);
    }
    playback_position_label_->setText(
        QString::number(position, 'f', 3) + " / " +
        QString::number(playback_clock_.duration(), 'f', 3) + " s | frame " +
        QString::number(index + 1) + "/" + QString::number(csv_frames_.size()));
}

void PreviewPanel::rememberRecentFile(const QString& path) {
    const QString normalized = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QStringList files;
    files.push_back(normalized);
    for (int index = 0; index < recent_files_combo_->count(); ++index) {
        const QString existing = recent_files_combo_->itemData(index).toString();
        if (existing.compare(normalized, Qt::CaseInsensitive) != 0) files.push_back(existing);
    }
    while (files.size() > 10) files.removeLast();
    recent_files_combo_->clear();
    for (const QString& file : files) {
        recent_files_combo_->addItem(QFileInfo(file).fileName(), file);
    }
}

void PreviewPanel::openRecentRecording() {
    const QString path = recent_files_combo_->currentData().toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        setStatus("The selected recent recording is no longer available");
        return;
    }
    if (path.endsWith(".xdf", Qt::CaseInsensitive)) loadXdf(path);
    else loadMergedCsv(path);
}

void PreviewPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            const QString path = url.toLocalFile();
            if (path.endsWith(".xdf", Qt::CaseInsensitive) ||
                path.endsWith(".csv", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void PreviewPanel::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(".xdf", Qt::CaseInsensitive)) {
            loadXdf(path);
            event->acceptProposedAction();
            return;
        }
        if (path.endsWith(".csv", Qt::CaseInsensitive)) {
            loadMergedCsv(path);
            event->acceptProposedAction();
            return;
        }
    }
}

void PreviewPanel::browseStairModel() {
    const QString path = services_.file_dialogs->openFile(
        this, "Select stair OBJ", stair_model_edit_->text(),
        "Wavefront OBJ (*.obj);;All files (*)");
    if (!path.isEmpty()) {
        stair_model_edit_->setText(QDir::toNativeSeparators(path));
        reloadStairModel();
    }
}

void PreviewPanel::reloadStairModel() {
    const QString path = stair_model_edit_->text().trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        stair_model_loaded_ = false;
        setStatus("Stair model not loaded");
        return;
    }

    try {
        const PreviewMesh mesh = loadObjMesh(QDir::toNativeSeparators(path).toStdString());
        widget_->setStairMesh(mesh, stairTransform());
        stair_model_loaded_ = true;
        setStatus("Stair model loaded: " + QFileInfo(path).fileName());
    } catch (const std::exception& ex) {
        stair_model_loaded_ = false;
        setStatus("Failed to load stair model: " + QString::fromStdString(ex.what()));
    }
}

PreviewTransformProfile PreviewPanel::manualGazeTransform() const {
    PreviewTransformProfile transform;
    transform.name = "HoloLens";
    transform.scale = 1.0;
    transform.translation = {gaze_tx_spin_->value(), gaze_ty_spin_->value(), gaze_tz_spin_->value()};
    transform.rotation_degrees = {gaze_rx_spin_->value(), gaze_ry_spin_->value(), gaze_rz_spin_->value()};
    return transform;
}

PreviewTransformProfile PreviewPanel::gazeTransform() const {
    if (calibration_state_ == CalibrationState::AutomaticSession) {
        return automatic_gaze_transform_;
    }
    return manualGazeTransform();
}

void PreviewPanel::resetCalibrationSession() {
    calibration_samples_.clear();
    calibration_state_ = CalibrationState::Manual;
    automatic_gaze_transform_ = {};
}

PreviewTransformProfile PreviewPanel::stairTransform() const {
    PreviewTransformProfile transform = transformProfileFromRigid(
        defaultStairCalibrationProfile().vicon_from_target,
        "Stair");
    transform.scale = 0.001;
    return transform;
}

void PreviewPanel::loadSettings() {
    QSettings settings("ViconLSL", "ViconLSLBridge");
    marker_stream_edit_->setText(settings.value(
        "preview/markerStream", stream_defaults::ViconMarkers).toString());
    segment_stream_edit_->setText(settings.value(
        "preview/segmentStream", stream_defaults::ViconSegments).toString());
    gaze_stream_edit_->setText(settings.value(
        "preview/gazeStream", stream_defaults::HoloLensGaze).toString());
    calibration_stream_edit_->setText(settings.value(
        "preview/calibrationStream", stream_defaults::HoloLensModelTargetPose).toString());
    tolerance_spin_->setValue(settings.value("preview/matchTolerance", 0.05).toDouble());
    trail_points_spin_->setValue(settings.value("preview/trailPoints", 24).toInt());
    playback_speed_spin_->setValue(settings.value("preview/playbackSpeed", 1.0).toDouble());
    gaze_tx_spin_->setValue(settings.value("preview/gazeTx", 0.0).toDouble());
    gaze_ty_spin_->setValue(settings.value("preview/gazeTy", 0.0).toDouble());
    gaze_tz_spin_->setValue(settings.value("preview/gazeTz", 0.0).toDouble());
    gaze_rx_spin_->setValue(settings.value("preview/gazeRx", 0.0).toDouble());
    gaze_ry_spin_->setValue(settings.value("preview/gazeRy", 0.0).toDouble());
    gaze_rz_spin_->setValue(settings.value("preview/gazeRz", 0.0).toDouble());
    resetCalibrationSession();
    const QStringList obsolete_automatic_keys = {
        "preview/gazeUseQuaternion",
        "preview/gazeQTx", "preview/gazeQTy", "preview/gazeQTz",
        "preview/gazeQx", "preview/gazeQy", "preview/gazeQz", "preview/gazeQw",
    };
    for (const QString& key : obsolete_automatic_keys) {
        settings.remove(key);
    }
    QString stair_model = settings.value("preview/stairModel", "").toString().trimmed();
    if (stair_model.isEmpty() || !QFileInfo::exists(stair_model)) {
        stair_model = defaultStairModelPath();
    }
    stair_model_edit_->setText(stair_model);
}

void PreviewPanel::saveSettings() const {
    QSettings settings("ViconLSL", "ViconLSLBridge");
    settings.setValue("preview/markerStream", marker_stream_edit_->text());
    settings.setValue("preview/segmentStream", segment_stream_edit_->text());
    settings.setValue("preview/gazeStream", gaze_stream_edit_->text());
    settings.setValue("preview/calibrationStream", calibration_stream_edit_->text());
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
        QDir::current().filePath("assets/stair_model/stair_model1.obj"),
        QDir::current().filePath("vicon-lsl-bridge/assets/stair_model/stair_model1.obj"),
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
