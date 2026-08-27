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
        label->setBuddy(control);
        control->setToolTip(tooltip);
        QString accessible = text;
        accessible.remove('&');
        accessible.remove(':');
        control->setAccessibleName(accessible.trimmed());
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

QDoubleSpinBox* makeQuaternionSpin(double value = 0.0) {
    auto* spin = new QDoubleSpinBox();
    spin->setRange(-1.0, 1.0);
    spin->setDecimals(6);
    spin->setSingleStep(0.01);
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
    marker_stream_edit_->setObjectName("previewMarkerInput");
    segment_stream_edit_->setObjectName("previewSegmentInput");
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

    auto* profiles_group = new QGroupBox("Managed Calibration Profile");
    auto* profiles_layout = new QVBoxLayout(profiles_group);
    auto* profile_form = new QFormLayout();
    calibration_profile_combo_ = new QComboBox();
    calibration_profile_combo_->setToolTip(
        "Saved calibration setup. Automatic solutions remain session-only until explicitly saved.");
    calibration_profile_name_edit_ = new QLineEdit();
    calibration_profile_name_edit_->setToolTip("Human-readable calibration profile name.");
    calibration_setup_id_edit_ = new QLineEdit();
    calibration_setup_id_edit_->setToolTip(
        "Stable identifier for the physical stair, room, and tracker arrangement.");
    calibration_notes_edit_ = new QLineEdit();
    calibration_notes_edit_->setToolTip("Setup notes needed to reproduce the physical alignment.");
    gaze_frame_edit_ = new QLineEdit("hololens_stationary_shared_with_gaze");
    target_frame_edit_ = new QLineEdit("hololens_stationary_shared_with_gaze");
    gaze_frame_edit_->setToolTip("Coordinate-frame identifier expected on the gaze stream.");
    target_frame_edit_->setToolTip("Coordinate-frame identifier expected on the stair-target stream.");
    profile_form->addRow("Profile:", calibration_profile_combo_);
    profile_form->addRow("Name:", calibration_profile_name_edit_);
    profile_form->addRow("Physical setup ID:", calibration_setup_id_edit_);
    profile_form->addRow("Setup notes:", calibration_notes_edit_);
    profile_form->addRow("Gaze frame:", gaze_frame_edit_);
    profile_form->addRow("Target frame:", target_frame_edit_);
    profiles_layout->addLayout(profile_form);

    auto* pose_grid = new QGridLayout();
    stair_tx_spin_ = makeDistanceSpin();
    stair_ty_spin_ = makeDistanceSpin();
    stair_tz_spin_ = makeDistanceSpin();
    stair_qx_spin_ = makeQuaternionSpin();
    stair_qy_spin_ = makeQuaternionSpin();
    stair_qz_spin_ = makeQuaternionSpin();
    stair_qw_spin_ = makeQuaternionSpin(1.0);
    const QString pose_tooltip =
        "Measured rigid pose of the stair target in Vicon coordinates. Translation is metres; rotation is a quaternion.";
    QWidget* const pose_controls[] = {stair_tx_spin_, stair_ty_spin_, stair_tz_spin_,
                                      stair_qx_spin_, stair_qy_spin_, stair_qz_spin_,
                                      stair_qw_spin_};
    for (QWidget* control : pose_controls) control->setToolTip(pose_tooltip);
    pose_grid->addWidget(new QLabel("Measured stair T (m):"), 0, 0);
    pose_grid->addWidget(stair_tx_spin_, 0, 1);
    pose_grid->addWidget(stair_ty_spin_, 0, 2);
    pose_grid->addWidget(stair_tz_spin_, 0, 3);
    pose_grid->addWidget(new QLabel("Measured stair Q:"), 1, 0);
    pose_grid->addWidget(stair_qx_spin_, 1, 1);
    pose_grid->addWidget(stair_qy_spin_, 1, 2);
    pose_grid->addWidget(stair_qz_spin_, 1, 3);
    pose_grid->addWidget(stair_qw_spin_, 1, 4);
    profiles_layout->addLayout(pose_grid);

    auto* profile_buttons = new QHBoxLayout();
    auto* apply_profile_button = new QPushButton("Apply");
    auto* save_profile_button = new QPushButton("Save Session Calibration");
    auto* duplicate_profile_button = new QPushButton("Duplicate");
    auto* retire_profile_button = new QPushButton("Retire");
    auto* import_profile_button = new QPushButton("Import");
    auto* export_profile_button = new QPushButton("Export");
    apply_profile_button->setToolTip("Apply the selected saved profile to live preview and diagnostics.");
    save_profile_button->setToolTip(
        "Persist the current session calibration with physical setup and quality metadata.");
    duplicate_profile_button->setToolTip("Duplicate the selected profile with a new stable ID.");
    retire_profile_button->setToolTip(
        "Hide the selected profile from normal selection without deleting its history.");
    import_profile_button->setToolTip("Import a versioned calibration profile JSON file.");
    export_profile_button->setToolTip("Export the selected calibration profile as JSON.");
    profile_buttons->addWidget(apply_profile_button);
    profile_buttons->addWidget(save_profile_button);
    profile_buttons->addWidget(duplicate_profile_button);
    profile_buttons->addWidget(retire_profile_button);
    profile_buttons->addWidget(import_profile_button);
    profile_buttons->addWidget(export_profile_button);
    profiles_layout->addLayout(profile_buttons);
    calibration_quality_label_ = new QLabel("Quality: not calibrated");
    calibration_quality_label_->setObjectName("calibrationQuality");
    calibration_quality_label_->setWordWrap(true);
    calibration_metadata_label_ = new QLabel("Coordinate metadata: not observed");
    calibration_metadata_label_->setWordWrap(true);
    profiles_layout->addWidget(calibration_quality_label_);
    profiles_layout->addWidget(calibration_metadata_label_);
    alignment_layout->addWidget(profiles_group);
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
    delivery_metrics_label_ = new QLabel(
        "display replacements 0 | input backlog discarded 0 | latency 0 ms");
    delivery_metrics_label_->setToolTip(
        "Frames intentionally replaced before display and latest-frame display latency.");
    auto* fit_view_button = new QPushButton("Fit View");
    auto* reset_camera_button = new QPushButton("Reset Camera");
    auto* export_image_button = new QPushButton("Export Image");
    fit_view_button->setToolTip("Fit the camera to all currently visible data.");
    reset_camera_button->setToolTip("Restore the default camera angle, zoom, and fit.");
    export_image_button->setToolTip(
        "Export the current preview view as a PNG image without changing source data.");
    button_row->addWidget(start_button_);
    button_row->addWidget(stop_button_);
    button_row->addWidget(open_csv_button_);
    button_row->addWidget(open_xdf_button_);
    button_row->addWidget(play_csv_button_);
    button_row->addWidget(fit_view_button);
    button_row->addWidget(reset_camera_button);
    button_row->addWidget(export_image_button);
    button_row->addWidget(delivery_metrics_label_);
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
    live_render_timer_ = new QTimer(this);
    live_render_timer_->setTimerType(Qt::PreciseTimer);
    live_render_timer_->setInterval(1000 / gui::PerformanceBudgets::DefaultRenderHz);

    start_button_->setShortcut(QKeySequence("Alt+P"));
    stop_button_->setShortcut(QKeySequence("Alt+Shift+P"));
    open_xdf_button_->setShortcut(QKeySequence::Open);
    play_csv_button_->setShortcut(Qt::Key_Space);
    fit_view_button->setShortcut(Qt::Key_F);
    reset_camera_button->setShortcut(Qt::Key_R);
    const auto buttons = findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (button->accessibleName().trimmed().isEmpty()) {
            QString accessible = button->text();
            accessible.remove('&');
            button->setAccessibleName(accessible.trimmed());
        }
    }
    status_label_->setAccessibleName("Preview status");
    delivery_metrics_label_->setAccessibleName("Preview delivery health");
    file_state_label_->setAccessibleName("Recording file load state");
    memory_label_->setAccessibleName("Playback cache memory estimate");
    playback_position_label_->setAccessibleName("Playback time and frame position");
    calibration_quality_label_->setAccessibleName("Persistent calibration quality");
    calibration_metadata_label_->setAccessibleName("Calibration metadata compatibility");

    connect(start_button_, &QPushButton::clicked, this, &PreviewPanel::startPreview);
    connect(stop_button_, &QPushButton::clicked, this, &PreviewPanel::stopPreview);
    connect(open_csv_button_, &QPushButton::clicked, this, &PreviewPanel::openMergedCsv);
    connect(open_xdf_button_, &QPushButton::clicked, this, &PreviewPanel::openXdf);
    connect(play_csv_button_, &QPushButton::clicked, this, &PreviewPanel::toggleCsvPlayback);
    connect(csv_timer_, &QTimer::timeout, this, &PreviewPanel::advanceCsvPlayback);
    connect(live_render_timer_, &QTimer::timeout,
            this, &PreviewPanel::displayLatestLiveFrame);
    connect(fit_view_button, &QPushButton::clicked, this, &PreviewPanel::fitView);
    connect(reset_camera_button, &QPushButton::clicked, this, &PreviewPanel::resetCamera);
    connect(export_image_button, &QPushButton::clicked,
            this, &PreviewPanel::exportPreviewImage);
    connect(cancel_load_button_, &QPushButton::clicked, this, &PreviewPanel::cancelFileLoad);
    connect(open_recent_button, &QPushButton::clicked, this, &PreviewPanel::openRecentRecording);
    connect(timeline_slider_, &QSlider::valueChanged,
            this, &PreviewPanel::seekPlaybackFromSlider);
    connect(loop_playback_check_, &QCheckBox::toggled, this, [this](bool looping) {
        playback_clock_.setLooping(looping, playback_elapsed_.elapsed() / 1000.0);
    });
    connect(jump_start_button, &QPushButton::clicked, this, [this]() { seekToFrame(0); });
    connect(jump_end_button, &QPushButton::clicked, this, [this]() {
        if (!csv_frames_.empty()) seekToFrame(csv_frames_.size() - 1);
    });
    connect(step_back_button, &QPushButton::clicked, this, [this]() {
        if (csv_frames_.empty()) return;
        const std::size_t index = playback_clock_.frameIndex(playback_elapsed_.elapsed() / 1000.0);
        seekToFrame(index == 0 ? 0 : index - 1);
    });
    connect(step_forward_button, &QPushButton::clicked, this, [this]() {
        if (csv_frames_.empty()) return;
        const std::size_t index = playback_clock_.frameIndex(playback_elapsed_.elapsed() / 1000.0);
        seekToFrame((std::min)(csv_frames_.size() - 1, index + 1));
    });
    connect(jump_back_button, &QPushButton::clicked, this,
            [this]() { seekBySeconds(-jump_seconds_spin_->value()); });
    connect(jump_forward_button, &QPushButton::clicked, this,
            [this]() { seekBySeconds(jump_seconds_spin_->value()); });
    connect(playback_speed_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double speed) {
                playback_clock_.setSpeed(speed, playback_elapsed_.elapsed() / 1000.0);
            });
    connect(browse_stair_button, &QPushButton::clicked, this, &PreviewPanel::browseStairModel);
    connect(stair_model_edit_, &QLineEdit::editingFinished,
            this, &PreviewPanel::reloadStairModel);
    connect(calibrate_button_, &QPushButton::clicked, this, &PreviewPanel::beginCalibration);
    connect(use_manual_transform_button_, &QPushButton::clicked, this, &PreviewPanel::useManualTransform);
    connect(apply_profile_button, &QPushButton::clicked,
            this, &PreviewPanel::applySelectedCalibrationProfile);
    connect(save_profile_button, &QPushButton::clicked,
            this, &PreviewPanel::saveSessionCalibrationProfile);
    connect(duplicate_profile_button, &QPushButton::clicked,
            this, &PreviewPanel::duplicateCalibrationProfile);
    connect(retire_profile_button, &QPushButton::clicked,
            this, &PreviewPanel::retireCalibrationProfile);
    connect(import_profile_button, &QPushButton::clicked,
            this, &PreviewPanel::importCalibrationProfile);
    connect(export_profile_button, &QPushButton::clicked,
            this, &PreviewPanel::exportCalibrationProfile);
    connect(calibration_profile_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshCalibrationProfileUi(); });
    QDoubleSpinBox* const pose_spins[] = {
        stair_tx_spin_, stair_ty_spin_, stair_tz_spin_, stair_qx_spin_,
        stair_qy_spin_, stair_qz_spin_, stair_qw_spin_,
    };
    for (QDoubleSpinBox* spin : pose_spins) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &PreviewPanel::updateMeasuredStairPose);
    }
    connect(trail_points_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            widget_, &PreviewWidget::setTrailPointLimit);

    loadSettings();
    loadCalibrationProfiles();
    reloadStairModel();
    calibration_progress_throttle_.start();
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
        worker_->disconnect(this);
        worker_->setParent(nullptr);
        worker_ = nullptr;
    }
    saveSettings();
}

QVector<gui::StreamIdentity> PreviewPanel::streamInventory() const {
    return worker_ ? worker_->streamInventory() : latest_stream_inventory_;
}

void PreviewPanel::applySessionConfiguration(
    const gui::SessionConfiguration& configuration) {
    marker_binding_ = configuration.preview_markers;
    segment_binding_ = configuration.preview_segments;
    gaze_binding_ = configuration.preview_gaze;
    calibration_binding_ = configuration.preview_calibration;
    marker_stream_edit_->setText(marker_binding_.name);
    segment_stream_edit_->setText(segment_binding_.name);
    gaze_stream_edit_->setText(gaze_binding_.name);
    calibration_stream_edit_->setText(calibration_binding_.name);
    marker_stream_edit_->setReadOnly(!configuration.preview_external_streams);
    segment_stream_edit_->setReadOnly(!configuration.preview_external_streams);
    tolerance_spin_->setValue(configuration.preview_match_tolerance);
    cache_megabytes_spin_->setValue(configuration.preview_cache_megabytes);
    trail_points_spin_->setValue(configuration.preview_trail_points);
    playback_speed_spin_->setValue(configuration.preview_playback_speed);
    loop_playback_check_->setChecked(configuration.preview_loop_playback);
    gaze_tx_spin_->setValue(configuration.preview_gaze_translation.x);
    gaze_ty_spin_->setValue(configuration.preview_gaze_translation.y);
    gaze_tz_spin_->setValue(configuration.preview_gaze_translation.z);
    gaze_rx_spin_->setValue(configuration.preview_gaze_rotation_degrees.x);
    gaze_ry_spin_->setValue(configuration.preview_gaze_rotation_degrees.y);
    gaze_rz_spin_->setValue(configuration.preview_gaze_rotation_degrees.z);
    const int render_hz = std::clamp(configuration.preview_render_hz, 1,
                                     gui::PerformanceBudgets::MaximumRenderHz);
    live_render_timer_->setInterval((std::max)(1, 1000 / render_hz));
    if (!configuration.stair_model_path.trimmed().isEmpty()) {
        stair_model_edit_->setText(configuration.stair_model_path);
    }
    const int profile_index = calibration_profile_combo_
        ? calibration_profile_combo_->findData(configuration.calibration_profile_id)
        : -1;
    if (profile_index >= 0) calibration_profile_combo_->setCurrentIndex(profile_index);
}

void PreviewPanel::updateSessionConfiguration(
    gui::SessionConfiguration& configuration) const {
    configuration.preview_markers = marker_binding_;
    configuration.preview_segments = segment_binding_;
    configuration.preview_gaze = gaze_binding_;
    configuration.preview_calibration = calibration_binding_;
    configuration.preview_markers.name = marker_stream_edit_->text().trimmed();
    configuration.preview_segments.name = segment_stream_edit_->text().trimmed();
    configuration.preview_gaze.name = gaze_stream_edit_->text().trimmed();
    configuration.preview_calibration.name = calibration_stream_edit_->text().trimmed();
    configuration.preview_match_tolerance = tolerance_spin_->value();
    configuration.preview_cache_megabytes = cache_megabytes_spin_->value();
    configuration.preview_trail_points = trail_points_spin_->value();
    configuration.preview_playback_speed = playback_speed_spin_->value();
    configuration.preview_loop_playback = loop_playback_check_->isChecked();
    configuration.preview_gaze_translation = {
        gaze_tx_spin_->value(), gaze_ty_spin_->value(), gaze_tz_spin_->value()};
    configuration.preview_gaze_rotation_degrees = {
        gaze_rx_spin_->value(), gaze_ry_spin_->value(), gaze_rz_spin_->value()};
    configuration.stair_model_path = stair_model_edit_->text().trimmed();
    if (calibration_profile_combo_) {
        configuration.calibration_profile_id =
            calibration_profile_combo_->currentData().toString();
    }
}

void PreviewPanel::requestShutdown() {
    if (file_loader_) cancelFileLoad();
    stopPreview();
    csv_timer_->stop();
}

bool PreviewPanel::shutdownReady() const {
    return worker_ == nullptr && file_loader_ == nullptr;
}

gui::SessionCalibrationState PreviewPanel::sessionCalibrationState() const {
    switch (calibration_state_) {
        case CalibrationState::Manual: return gui::SessionCalibrationState::Manual;
        case CalibrationState::Collecting: return gui::SessionCalibrationState::Collecting;
        case CalibrationState::AutomaticSession:
            return gui::SessionCalibrationState::AutomaticSession;
        case CalibrationState::SavedProfile:
            return gui::SessionCalibrationState::SavedProfile;
    }
    return gui::SessionCalibrationState::Manual;
}

QString PreviewPanel::calibrationQualityText() const {
    return calibration_quality_label_ ? calibration_quality_label_->text() : QString();
}

PreviewDeliveryMetrics PreviewPanel::deliveryMetrics() const {
    return worker_ ? worker_->deliveryMetrics() : last_delivery_metrics_;
}

void PreviewPanel::openRecording(const QString& path) {
    if (path.endsWith(".xdf", Qt::CaseInsensitive)) {
        loadXdf(path);
    } else if (path.endsWith(".csv", Qt::CaseInsensitive)) {
        loadMergedCsv(path);
    } else {
        setStatus("Unsupported preview recording type: " + QFileInfo(path).suffix());
    }
}

void PreviewPanel::fitView() {
    widget_->fitView();
}

void PreviewPanel::resetCamera() {
    widget_->resetCamera();
}

void PreviewPanel::exportPreviewImage() {
    const QString path = services_.file_dialogs->saveFile(
        this, "Export preview image", QString(), "PNG images (*.png)");
    if (path.isEmpty()) return;
    QString normalized = path;
    if (!normalized.endsWith(".png", Qt::CaseInsensitive)) normalized += ".png";
    if (!widget_->grab().save(normalized, "PNG")) {
        setStatus("Could not export preview image to " + normalized);
        return;
    }
    setStatus("Exported preview image to " + QDir::toNativeSeparators(normalized));
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
        jump_seconds_spin_,
        recent_files_combo_,
        cancel_load_button_,
        calibration_profile_combo_,
        calibration_profile_name_edit_,
        calibration_setup_id_edit_,
        calibration_notes_edit_,
        gaze_frame_edit_,
        target_frame_edit_,
        stair_tx_spin_, stair_ty_spin_, stair_tz_spin_,
        stair_qx_spin_, stair_qy_spin_, stair_qz_spin_, stair_qw_spin_,
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

    PreviewWorkerConfig config;
    config.marker_stream_name = marker_stream_edit_->text().trimmed();
    config.segment_stream_name = segment_stream_edit_->text().trimmed();
    config.gaze_stream_name = gaze_stream_edit_->text().trimmed();
    config.calibration_stream_name = calibration_stream_edit_->text().trimmed();
    config.match_tolerance_seconds = tolerance_spin_->value();
    config.marker_source_id = marker_binding_.source_id;
    config.segment_source_id = segment_binding_.source_id;
    config.gaze_source_id = gaze_binding_.source_id;
    config.calibration_source_id = calibration_binding_.source_id;
    config.marker_follow_by_name =
        marker_binding_.reconnection == gui::StreamReconnectionMode::FollowName;
    config.segment_follow_by_name =
        segment_binding_.reconnection == gui::StreamReconnectionMode::FollowName;
    config.gaze_follow_by_name =
        gaze_binding_.reconnection == gui::StreamReconnectionMode::FollowName;
    config.calibration_follow_by_name =
        calibration_binding_.reconnection == gui::StreamReconnectionMode::FollowName;
    config.vicon_transform.name = "Vicon";
    config.vicon_transform.scale = 0.001;
    config.gaze_transform = gazeTransform();

    worker_ = services_.create_preview_worker(std::move(config), this);
    worker_stopping_ = false;
    PreviewStreamWorker* const started_worker = worker_;
    connect(worker_, &PreviewStreamWorker::targetPoseReady, this, &PreviewPanel::handleTargetPose);
    connect(worker_, &PreviewStreamWorker::statusChanged, this, &PreviewPanel::setStatus);
    connect(worker_, &PreviewStreamWorker::lifecycleChanged, this,
            [this](ComponentLifecycleState state, const QString& detail) {
                lifecycle_state_ = state;
                if (!detail.isEmpty()) setStatus(detail);
                emit lifecycleChanged(state, detail);
            });
    connect(worker_, &PreviewStreamWorker::streamIdentityChanged, this,
            [this](const gui::StreamIdentity& identity, const QString& warning) {
                gui::StreamIdentity updated = identity;
                updated.warning = warning;
                const auto update_binding = [&updated](gui::StreamBinding& binding) {
                    if (binding.reconnection == gui::StreamReconnectionMode::SourceIdentity &&
                        binding.source_id.isEmpty()) {
                        binding.source_id = updated.source_id;
                    }
                };
                if (updated.role == "markers") update_binding(marker_binding_);
                if (updated.role == "segments") update_binding(segment_binding_);
                if (updated.role == "gaze") update_binding(gaze_binding_);
                if (updated.role == "calibration") update_binding(calibration_binding_);
                latest_stream_inventory_.erase(std::remove_if(
                    latest_stream_inventory_.begin(), latest_stream_inventory_.end(),
                    [&updated](const gui::StreamIdentity& existing) {
                        return existing.role == updated.role;
                    }), latest_stream_inventory_.end());
                latest_stream_inventory_.push_back(updated);
                QString gaze_frame;
                QString target_frame;
                for (const gui::StreamIdentity& stream : latest_stream_inventory_) {
                    if (stream.role == "gaze") gaze_frame = stream.coordinate_frame;
                    if (stream.role == "calibration") target_frame = stream.coordinate_frame;
                }
                calibration_metadata_compatible_ = !gaze_frame.isEmpty() &&
                    !target_frame.isEmpty() &&
                    calibrationCoordinateFramesCompatible(gaze_frame.toStdString(),
                                                          target_frame.toStdString());
                calibration_metadata_label_->setText(calibration_metadata_compatible_
                    ? "Coordinate metadata: compatible (" + gaze_frame + ")"
                    : "Coordinate metadata: missing, fallback, or incompatible");
                emit streamInventoryChanged(latest_stream_inventory_);
                emit calibrationStateChanged(sessionCalibrationState(),
                                             calibrationQualityText(),
                                             calibration_metadata_compatible_);
            });
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &QThread::finished, this, [this, started_worker]() {
        if (worker_ != started_worker) {
            return;
        }
        worker_ = nullptr;
        lifecycle_state_ = ComponentLifecycleState::Stopped;
        emit lifecycleChanged(lifecycle_state_, "Preview stopped");
        live_render_timer_->stop();
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
    setStatus("Preview resolving configured LSL streams; stair calibration is ready on request...");
    lifecycle_state_ = ComponentLifecycleState::Starting;
    emit lifecycleChanged(lifecycle_state_, "Resolving configured streams");
    live_render_timer_->start();
    worker_->start();
}

bool PreviewPanel::accessibilityContractSatisfied() const {
    if (!widget_ || widget_->accessibleName().trimmed().isEmpty() ||
        !timeline_slider_ || timeline_slider_->accessibleName().trimmed().isEmpty()) {
        return false;
    }
    const auto buttons = findChildren<QPushButton*>();
    for (const QPushButton* button : buttons) {
        if (button->accessibleName().trimmed().isEmpty() ||
            button->focusPolicy() == Qt::NoFocus) {
            return false;
        }
    }
    return true;
}

void PreviewPanel::stopPreview() {
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
    lifecycle_state_ = ComponentLifecycleState::Stopping;
    emit lifecycleChanged(lifecycle_state_, "Preview stop requested");
    PreviewStreamWorker* const stopping_worker = worker_;
    stopping_worker->requestInterruption();
    start_button_->setEnabled(false);
    stop_button_->setEnabled(false);
    open_csv_button_->setEnabled(false);
    open_xdf_button_->setEnabled(false);
    setStatus("Preview stopping asynchronously...");
}

void PreviewPanel::displayLatestLiveFrame() {
    if (!worker_) {
        return;
    }
    PreviewFrame frame;
    PreviewDeliveryMetrics metrics;
    if (worker_->takeLatestFrame(frame, metrics)) {
        widget_->setFrame(std::move(frame));
    }
    metrics = worker_->deliveryMetrics();
    last_delivery_metrics_ = metrics;
    const bool late = metrics.display_latency_ms >
        gui::PerformanceBudgets::MaximumLivePreviewLatencyMs;
    delivery_metrics_label_->setText(
        QString(late ? "PREVIEW LATE | display replacements "
                     : "display replacements ") +
        QString::number(metrics.replaced_before_display) +
        " | input backlog discarded " +
        QString::number(metrics.coalesced_input_samples) +
        " | latency " + QString::number(metrics.display_latency_ms) + " ms");
    delivery_metrics_label_->setToolTip(
        late ? "Display latency exceeds the documented live-preview budget of " +
                   QString::number(gui::PerformanceBudgets::MaximumLivePreviewLatencyMs) +
                   " ms; source-rate measurements remain independent."
             : "Latest-frame delivery is within the live-preview latency budget.");
    emit deliveryMetricsChanged(metrics);
}

void PreviewPanel::beginCalibration() {
    if (!worker_) {
        setStatus("Start the preview before calibrating from the stair target");
        return;
    }
    if (!calibration_metadata_compatible_) {
        const auto answer = QMessageBox::warning(
            this, "Coordinate metadata unavailable",
            "Gaze and stair-target coordinate metadata is missing or incompatible. "
            "Continue with fallback compatibility for this session?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            updateCalibrationPersistentStatus(
                gui::SessionCalibrationState::Failed,
                "Calibration rejected: coordinate metadata was not confirmed", false);
            return;
        }
    }
    calibration_samples_.clear();
    calibration_state_ = CalibrationState::Collecting;
    calibration_rejection_reason_.clear();
    const CalibrationProfile profile = activeSolverProfile();
    setStatus("Waiting for " + QString::number(profile.required_samples) +
              " stable tracked stair-target poses...");
    updateCalibrationPersistentStatus(gui::SessionCalibrationState::Collecting,
                                      "Quality: collecting 0/" +
                                          QString::number(profile.required_samples),
                                      calibration_metadata_compatible_);
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
    calibration_quality_ = {};
    updateCalibrationPersistentStatus(gui::SessionCalibrationState::Manual,
                                      "Quality: manual transform (not solved)",
                                      calibration_metadata_compatible_);
}

void PreviewPanel::handleTargetPose(CalibrationTargetPose pose) {
    const CalibrationProfile profile = activeSolverProfile();
    if (calibration_state_ != CalibrationState::Collecting) {
        return;
    }
    if (!pose.tracked) {
        calibration_samples_.clear();
        calibration_rejection_reason_ = "Stair target lost";
        if (calibration_progress_throttle_.elapsed() >= 100) {
            setStatus("Stair target lost; waiting for a stable acquisition...");
            calibration_progress_throttle_.restart();
        }
        return;
    }

    if (!calibration_samples_.empty() &&
        !targetPoseWithinTolerance(calibration_samples_.front(), pose, profile)) {
        calibration_samples_.clear();
        calibration_samples_.push_back(pose);
        calibration_rejection_reason_ = "Target motion exceeded the stability tolerance";
        if (calibration_progress_throttle_.elapsed() >= 100) {
            setStatus("Stair target moved; restarting stable-pose collection (1/" +
                      QString::number(static_cast<qulonglong>(profile.required_samples)) + ")");
            calibration_progress_throttle_.restart();
        }
        return;
    }

    calibration_samples_.push_back(pose);
    if (calibration_samples_.size() < profile.required_samples) {
        if (calibration_progress_throttle_.elapsed() >= 100) {
            const QString progress = "Collecting stair-target poses: " +
                QString::number(calibration_samples_.size()) + "/" +
                QString::number(profile.required_samples);
            setStatus(progress);
            calibration_quality_label_->setText("Quality: " + progress);
            calibration_progress_throttle_.restart();
        }
        return;
    }

    const auto solution = solveTrackedTargetCalibration(calibration_samples_, profile);
    calibration_state_ = CalibrationState::Manual;
    calibration_samples_.clear();
    if (!solution) {
        calibration_rejection_reason_ =
            "Translation or rotation RMS exceeded the selected profile limits";
        setStatus("Calibration failed: " + calibration_rejection_reason_);
        updateCalibrationPersistentStatus(gui::SessionCalibrationState::Failed,
            "Quality: rejected — " + calibration_rejection_reason_,
            calibration_metadata_compatible_);
        return;
    }

    automatic_gaze_transform_ = gazeTransformFromTargetCalibration(
        profile,
        solution->holo_from_target);
    calibration_state_ = CalibrationState::AutomaticSession;
    calibration_quality_ = solution->quality;
    if (worker_) {
        worker_->setGazeTransform(gazeTransform());
    }
    widget_->requestViewRefit();
    setStatus("Stair-target calibration " + QString::fromStdString(profile.id) +
              " applied for this session (RMS " +
              QString::number(solution->quality.translation_rms_m * 1000.0, 'f', 1) +
              " mm, " + QString::number(solution->quality.rotation_rms_degrees, 'f', 2) +
              " deg)");
    updateCalibrationPersistentStatus(
        gui::SessionCalibrationState::AutomaticSession,
        "Quality: " + QString::number(solution->quality.sample_count) +
            " samples, translation RMS " +
            QString::number(solution->quality.translation_rms_m * 1000.0, 'f', 1) +
            " mm, rotation RMS " +
            QString::number(solution->quality.rotation_rms_degrees, 'f', 2) + " deg",
        calibration_metadata_compatible_);
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
    if (calibration_state_ == CalibrationState::AutomaticSession ||
        calibration_state_ == CalibrationState::SavedProfile) {
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
        activeSolverProfile().vicon_from_target, "Stair");
    transform.scale = 0.001;
    return transform;
}

void PreviewPanel::loadCalibrationProfiles() {
    calibration_profiles_ = services_.settings->loadCalibrationProfiles();
    const gui::SessionConfiguration configuration =
        services_.settings->loadConfiguration();
    refreshCalibrationProfileUi(configuration.calibration_profile_id);
}

void PreviewPanel::saveCalibrationProfiles() {
    QString error;
    if (!services_.settings->saveCalibrationProfiles(
            calibration_profiles_, &error)) {
        setStatus("Could not save calibration profiles: " + error);
        return;
    }
    gui::SessionConfiguration configuration =
        services_.settings->loadConfiguration();
    configuration.calibration_profile_id =
        calibration_profile_combo_->currentData().toString();
    services_.settings->saveConfiguration(configuration);
}

void PreviewPanel::refreshCalibrationProfileUi(const QString& select_id) {
    if (!calibration_profile_combo_) return;
    QString desired = select_id;
    if (desired.isEmpty()) desired = calibration_profile_combo_->currentData().toString();
    {
        const QSignalBlocker blocker(calibration_profile_combo_);
        calibration_profile_combo_->clear();
        for (const gui::ManagedCalibrationProfile& profile : calibration_profiles_) {
            if (!profile.retired || profile.id == desired) {
                calibration_profile_combo_->addItem(
                    profile.display_name + (profile.retired ? " (retired)" : ""),
                    profile.id);
            }
        }
        int index = calibration_profile_combo_->findData(desired);
        if (index < 0 && calibration_profile_combo_->count() > 0) index = 0;
        calibration_profile_combo_->setCurrentIndex(index);
    }
    const gui::ManagedCalibrationProfile* profile = selectedCalibrationProfile();
    if (!profile) return;
    const QSignalBlocker name_blocker(calibration_profile_name_edit_);
    const QSignalBlocker setup_blocker(calibration_setup_id_edit_);
    const QSignalBlocker notes_blocker(calibration_notes_edit_);
    const QSignalBlocker gaze_blocker(gaze_frame_edit_);
    const QSignalBlocker target_blocker(target_frame_edit_);
    const QSignalBlocker tx_blocker(stair_tx_spin_);
    const QSignalBlocker ty_blocker(stair_ty_spin_);
    const QSignalBlocker tz_blocker(stair_tz_spin_);
    const QSignalBlocker qx_blocker(stair_qx_spin_);
    const QSignalBlocker qy_blocker(stair_qy_spin_);
    const QSignalBlocker qz_blocker(stair_qz_spin_);
    const QSignalBlocker qw_blocker(stair_qw_spin_);
    calibration_profile_name_edit_->setText(profile->display_name);
    calibration_setup_id_edit_->setText(profile->physical_setup_id);
    calibration_notes_edit_->setText(profile->setup_notes);
    gaze_frame_edit_->setText(profile->gaze_coordinate_frame);
    target_frame_edit_->setText(profile->target_coordinate_frame);
    stair_tx_spin_->setValue(profile->vicon_from_target.translation.x);
    stair_ty_spin_->setValue(profile->vicon_from_target.translation.y);
    stair_tz_spin_->setValue(profile->vicon_from_target.translation.z);
    stair_qx_spin_->setValue(profile->vicon_from_target.rotation.x);
    stair_qy_spin_->setValue(profile->vicon_from_target.rotation.y);
    stair_qz_spin_->setValue(profile->vicon_from_target.rotation.z);
    stair_qw_spin_->setValue(profile->vicon_from_target.rotation.w);
    if (profile->quality.sample_count > 0) {
        calibration_quality_label_->setText(
            "Saved quality: " + QString::number(profile->quality.sample_count) +
            " samples, translation RMS " +
            QString::number(profile->quality.translation_rms_m * 1000.0, 'f', 1) +
            " mm, rotation RMS " +
            QString::number(profile->quality.rotation_rms_degrees, 'f', 2) + " deg");
    }
}

gui::ManagedCalibrationProfile* PreviewPanel::selectedCalibrationProfile() {
    if (!calibration_profile_combo_) return nullptr;
    const QString id = calibration_profile_combo_->currentData().toString();
    for (gui::ManagedCalibrationProfile& profile : calibration_profiles_) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

const gui::ManagedCalibrationProfile* PreviewPanel::selectedCalibrationProfile() const {
    return const_cast<PreviewPanel*>(this)->selectedCalibrationProfile();
}

CalibrationProfile PreviewPanel::activeSolverProfile() const {
    CalibrationProfile result = defaultStairCalibrationProfile();
    if (const gui::ManagedCalibrationProfile* profile = selectedCalibrationProfile()) {
        result = profile->solverProfile();
    }
    if (stair_tx_spin_) {
        result.vicon_from_target.translation = {
            stair_tx_spin_->value(), stair_ty_spin_->value(), stair_tz_spin_->value()};
        result.vicon_from_target.rotation = normalizeQuaternion({
            stair_qx_spin_->value(), stair_qy_spin_->value(),
            stair_qz_spin_->value(), stair_qw_spin_->value()});
    }
    return result;
}

void PreviewPanel::applySelectedCalibrationProfile() {
    gui::ManagedCalibrationProfile* profile = selectedCalibrationProfile();
    if (!profile) return;
    QString reason;
    if (!profile->complete(&reason)) {
        setStatus("Calibration profile is incomplete: " + reason);
        return;
    }
    bool metadata_matches = true;
    for (const gui::StreamIdentity& identity : latest_stream_inventory_) {
        if (identity.role == "gaze" && !identity.coordinate_frame.isEmpty() &&
            identity.coordinate_frame != profile->gaze_coordinate_frame) {
            metadata_matches = false;
        }
        if (identity.role == "calibration" && !identity.coordinate_frame.isEmpty() &&
            identity.coordinate_frame != profile->target_coordinate_frame) {
            metadata_matches = false;
        }
    }
    if ((!calibration_metadata_compatible_ || !metadata_matches) &&
        !profile->metadata_fallback_confirmed) {
        const auto answer = QMessageBox::warning(
            this, "Calibration metadata mismatch",
            "The live coordinate metadata is missing or differs from this profile. "
            "Apply the profile using fallback compatibility?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            setStatus("Calibration profile was not applied because metadata was not confirmed");
            return;
        }
        profile->metadata_fallback_confirmed = true;
        saveCalibrationProfiles();
    }
    automatic_gaze_transform_ = profile->gaze_transform;
    calibration_quality_ = profile->quality;
    calibration_state_ = CalibrationState::SavedProfile;
    if (worker_) worker_->setGazeTransform(gazeTransform());
    reloadStairModel();
    widget_->requestViewRefit();
    updateCalibrationPersistentStatus(
        gui::SessionCalibrationState::SavedProfile,
        profile->quality.sample_count > 0
            ? "Quality: saved translation RMS " +
                  QString::number(profile->quality.translation_rms_m * 1000.0, 'f', 1) +
                  " mm, rotation RMS " +
                  QString::number(profile->quality.rotation_rms_degrees, 'f', 2) + " deg"
            : "Quality: saved profile has no measured RMS values",
        calibration_metadata_compatible_ && metadata_matches);
    setStatus("Applied calibration profile " + profile->display_name);
}

void PreviewPanel::saveSessionCalibrationProfile() {
    if (calibration_state_ != CalibrationState::AutomaticSession &&
        calibration_state_ != CalibrationState::SavedProfile) {
        setStatus("Solve or apply a calibration before saving a managed profile");
        return;
    }
    gui::ManagedCalibrationProfile* selected = selectedCalibrationProfile();
    const QString display_name = calibration_profile_name_edit_->text().trimmed();
    const bool create_new = !selected || selected->quality.sample_count == 0 || selected->retired;
    gui::ManagedCalibrationProfile profile = selected
        ? *selected : gui::CalibrationProfileStore::defaultProfile();
    if (create_new) {
        profile.id = gui::CalibrationProfileStore::newProfileId(
            display_name, calibration_profiles_);
        profile.created_at = services_.clock->nowUtc();
        profile.retired = false;
    }
    profile.display_name = display_name;
    profile.physical_setup_id = calibration_setup_id_edit_->text().trimmed();
    profile.setup_notes = calibration_notes_edit_->text().trimmed();
    profile.stair_model_path = stair_model_edit_->text().trimmed();
    profile.stair_model_identity =
        gui::CalibrationProfileStore::stairModelIdentity(profile.stair_model_path);
    if (profile.stair_model_identity.isEmpty()) {
        profile.stair_model_identity = QFileInfo(profile.stair_model_path).fileName();
    }
    profile.vicon_from_target = activeSolverProfile().vicon_from_target;
    profile.gaze_transform = gazeTransform();
    profile.gaze_coordinate_frame = gaze_frame_edit_->text().trimmed();
    profile.target_coordinate_frame = target_frame_edit_->text().trimmed();
    profile.quality = calibration_quality_;
    profile.metadata_fallback_confirmed = !calibration_metadata_compatible_;
    QString reason;
    if (!profile.complete(&reason)) {
        setStatus("Cannot save calibration profile: " + reason);
        return;
    }
    if (create_new) {
        calibration_profiles_.push_back(profile);
    } else {
        *selected = profile;
    }
    saveCalibrationProfiles();
    refreshCalibrationProfileUi(profile.id);
    calibration_state_ = CalibrationState::SavedProfile;
    updateCalibrationPersistentStatus(gui::SessionCalibrationState::SavedProfile,
                                      calibrationQualityText(),
                                      calibration_metadata_compatible_);
    setStatus("Saved calibration profile " + profile.display_name);
}

void PreviewPanel::duplicateCalibrationProfile() {
    const gui::ManagedCalibrationProfile* selected = selectedCalibrationProfile();
    if (!selected) return;
    gui::ManagedCalibrationProfile duplicate =
        gui::CalibrationProfileStore::duplicate(*selected, calibration_profiles_);
    calibration_profiles_.push_back(duplicate);
    saveCalibrationProfiles();
    refreshCalibrationProfileUi(duplicate.id);
    setStatus("Duplicated calibration profile as " + duplicate.display_name);
}

void PreviewPanel::retireCalibrationProfile() {
    const gui::ManagedCalibrationProfile* selected = selectedCalibrationProfile();
    if (!selected) return;
    const QString id = selected->id;
    const auto answer = QMessageBox::question(
        this, "Retire calibration profile",
        "Retire " + selected->display_name + "? Existing diagnostic records keep its ID.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (gui::CalibrationProfileStore::retire(calibration_profiles_, id)) {
        saveCalibrationProfiles();
        refreshCalibrationProfileUi();
        setStatus("Calibration profile retired");
    }
}

void PreviewPanel::importCalibrationProfile() {
    const QString path = services_.file_dialogs->openFile(
        this, "Import calibration profile", QString(), "JSON files (*.json)");
    if (path.isEmpty()) return;
    gui::ManagedCalibrationProfile profile;
    QString error;
    if (!gui::CalibrationProfileStore::importProfile(path, profile, &error)) {
        setStatus("Could not import calibration profile: " + error);
        return;
    }
    if (std::any_of(calibration_profiles_.begin(), calibration_profiles_.end(),
                    [&profile](const auto& existing) { return existing.id == profile.id; })) {
        profile.id = gui::CalibrationProfileStore::newProfileId(
            profile.display_name, calibration_profiles_);
    }
    calibration_profiles_.push_back(profile);
    saveCalibrationProfiles();
    refreshCalibrationProfileUi(profile.id);
    setStatus("Imported calibration profile " + profile.display_name);
}

void PreviewPanel::exportCalibrationProfile() {
    const gui::ManagedCalibrationProfile* profile = selectedCalibrationProfile();
    if (!profile) return;
    const QString path = services_.file_dialogs->saveFile(
        this, "Export calibration profile", profile->id + ".json",
        "JSON files (*.json)");
    if (path.isEmpty()) return;
    QString error;
    if (!gui::CalibrationProfileStore::exportProfile(path, *profile, &error)) {
        setStatus("Could not export calibration profile: " + error);
        return;
    }
    setStatus("Exported calibration profile to " + QDir::toNativeSeparators(path));
}

void PreviewPanel::updateMeasuredStairPose() {
    if (!stair_model_loaded_) return;
    try {
        const PreviewMesh mesh = loadObjMesh(
            QDir::toNativeSeparators(stair_model_edit_->text()).toStdString());
        widget_->setStairMesh(mesh, stairTransform());
        widget_->requestViewRefit();
    } catch (const std::exception&) {
        // The normal stair-model error path reports file errors on reload.
    }
}

void PreviewPanel::updateCalibrationPersistentStatus(
    gui::SessionCalibrationState state,
    const QString& text,
    bool metadata_compatible) {
    if (calibration_quality_label_) calibration_quality_label_->setText(text);
    emit calibrationStateChanged(state, text, metadata_compatible);
}

void PreviewPanel::loadSettings() {
    gui::SessionConfiguration configuration =
        services_.settings->loadConfiguration();
    applySessionConfiguration(configuration);
    resetCalibrationSession();
    QString stair_model = configuration.stair_model_path.trimmed();
    if (stair_model.isEmpty() || !QFileInfo::exists(stair_model)) {
        stair_model = defaultStairModelPath();
    }
    stair_model_edit_->setText(stair_model);
    const QStringList recent_files =
        services_.settings->loadUiState().recent_recordings;
    recent_files_combo_->clear();
    for (const QString& path : recent_files) {
        if (!path.trimmed().isEmpty()) {
            recent_files_combo_->addItem(QFileInfo(path).fileName(), path);
        }
    }
}

void PreviewPanel::saveSettings() const {
    gui::SessionConfiguration configuration =
        services_.settings->loadConfiguration();
    updateSessionConfiguration(configuration);
    services_.settings->saveConfiguration(configuration);
    gui::SessionUiState ui_state = services_.settings->loadUiState();
    QStringList recent_files;
    for (int index = 0; index < recent_files_combo_->count(); ++index) {
        recent_files.push_back(recent_files_combo_->itemData(index).toString());
    }
    ui_state.recent_recordings = recent_files;
    services_.settings->saveUiState(ui_state);
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
