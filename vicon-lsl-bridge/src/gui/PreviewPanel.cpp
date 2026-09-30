#include "gui/PreviewPanel.h"
#include "gui/FlowLayout.h"
#include "gui/WidgetHelpers.h"

#include "preview/ObjMesh.h"
#include "StreamDefaults.generated.h"

#include <QCoreApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QProgressBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QSlider>
#include <QMimeData>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <utility>

namespace vicon_lsl {
namespace {

using namespace vicon_lsl::gui_detail;

constexpr int kDefaultRenderHz = 30;
constexpr int kMaximumLivePreviewDelayMs = 100;
constexpr int kMaximumRenderHz = 60;

QDoubleSpinBox* makePoseSpin(double val = 0.0) { return makeDoubleSpin(-100.0, 100.0, 6, 0.001, val); }
QDoubleSpinBox* makeQuaternionSpin(double val = 0.0) { return makeDoubleSpin(-1.0, 1.0, 6, 0.01, val); }

std::optional<PreviewFileType> recordingFileType(const QString& path) {
    if (path.endsWith(".xdf", Qt::CaseInsensitive)) return PreviewFileType::Xdf;
    if (path.endsWith(".csv", Qt::CaseInsensitive)) return PreviewFileType::Csv;
    return std::nullopt;
}

// Vicon sends millimetres, but the preview draws metres.
PreviewTransformProfile viconPreviewTransform() {
    PreviewTransformProfile transform;
    transform.scale = 0.001;
    return transform;
}

QString errorText(const CalibrationQuality& quality) {
    return "position error " + QString::number(quality.translation_rms_m * 1000.0, 'f', 1) +
           " mm, angle error " + QString::number(quality.rotation_rms_degrees, 'f', 2) + " deg";
}

} // namespace

PreviewPanel::PreviewPanel(QWidget* parent, std::shared_ptr<QSettings> settings)
    : QWidget(parent), settings_(std::move(settings)) {
    qRegisterMetaType<vicon_lsl::PreviewFrame>("vicon_lsl::PreviewFrame");
    qRegisterMetaType<vicon_lsl::CalibrationTargetPose>("vicon_lsl::CalibrationTargetPose");
    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    widget_ = new PreviewWidget();
    // The drawing works at any size, so it gets whatever height the controls leave.
    widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    layout->addWidget(widget_, 1);

    auto [controls_group, controls_layout] = makeGroup<QVBoxLayout>("Live Preview");
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

    marker_stream_edit_ = new QLineEdit(stream_defaults::ViconMarkers);
    segment_stream_edit_ = new QLineEdit(stream_defaults::ViconSegments);
    marker_stream_edit_->setObjectName("previewMarkerInput");
    segment_stream_edit_->setObjectName("previewSegmentInput");
    gaze_stream_edit_ = new QLineEdit(stream_defaults::HoloLensGaze);
    calibration_stream_edit_ = new QLineEdit(stream_defaults::HoloLensModelTargetPose);
    tolerance_spin_ = makeDoubleSpin(0.001, 1.0, 3, 0.005, 0.05);

    cache_megabytes_spin_ = new QSpinBox();
    cache_megabytes_spin_->setRange(16, 2048);
    cache_megabytes_spin_->setSingleStep(16);
    cache_megabytes_spin_->setSuffix(" MiB");
    cache_megabytes_spin_->setValue(128);

    trail_points_spin_ = makeSpin(2, 500, 24);
    playback_speed_spin_ = makeDoubleSpin(0.1, 4.0, 1, 0.1, 1.0);

    // The fields wrap onto more rows when the panel is narrow.
    auto* stream_fields = new FlowLayout(10, 4);
    stream_fields->addWidget(makeFieldChip("Markers:", marker_stream_edit_, "LSL stream containing Vicon marker samples for the preview."));
    stream_fields->addWidget(makeFieldChip("Segments:", segment_stream_edit_, "LSL stream containing Vicon segment samples for the preview."));
    stream_fields->addWidget(makeFieldChip("Gaze:", gaze_stream_edit_, "LSL stream containing HoloLens gaze samples."));
    stream_fields->addWidget(makeFieldChip("Stair target:", calibration_stream_edit_, "LSL stream containing tracked stair-target poses for calibration."));
    stream_fields->addWidget(makeFieldChip("Max time gap (s):", tolerance_spin_, "Largest time difference allowed when matching preview samples.", 90));
    stream_fields->addWidget(makeFieldChip("Trail points:", trail_points_spin_, "Number of recent preview frames retained in the trail.", 90));
    stream_fields->addWidget(makeFieldChip("Playback speed:", playback_speed_spin_, "Playback speed multiplier for CSV and XDF recordings.", 90));
    stream_fields->addWidget(makeFieldChip("Playback memory:", cache_megabytes_spin_, "Maximum memory used while reading and displaying a recording.", 110));
    sources_layout->addLayout(stream_fields);

    auto* alignment_note = new QLabel(
        "The HoloLens-to-Vicon transform is only known once it is measured. Solve it "
        "from the stair target, or apply a saved calibration. Until then gaze is drawn "
        "in its published HoloLens frame and is not aligned to Vicon.");
    alignment_note->setWordWrap(true);
    alignment_layout->addWidget(alignment_note);

    auto* calibration_row = new FlowLayout();
    calibrate_button_ = makeButton("Calibrate from Stair Target", "Collect stable poses from the stair-target stream and apply a HoloLens transform for this session only.");
    clear_calibration_button_ = makeButton("Clear Calibration", "Discard the calibration in use and draw gaze in its published HoloLens frame again.");
    calibration_row->addWidget(calibrate_button_);
    calibration_row->addWidget(clear_calibration_button_);
    alignment_layout->addLayout(calibration_row);

    auto [profiles_group, profiles_layout] = makeGroup<QVBoxLayout>("Saved Calibration");
    auto* profile_form = new QFormLayout();
    calibration_profile_combo_ = new QComboBox();
    calibration_profile_combo_->setToolTip("Saved calibration setup. Automatic results remain available only for this session until saved.");
    calibration_profile_name_edit_ = makeEdit("Name shown for this saved calibration.");
    calibration_setup_id_edit_ = makeEdit("Name used to identify this stair, room, and tracker arrangement.");
    calibration_notes_edit_ = makeEdit("Setup notes needed to reproduce the physical alignment.");
    gaze_frame_edit_ = makeEdit("Coordinate name expected on the gaze stream.",
                                "hololens_stationary_shared_with_gaze");
    target_frame_edit_ = makeEdit("Coordinate name expected on the stair-target stream.",
                                  "hololens_stationary_shared_with_gaze");
    profile_form->addRow("Saved calibration:", calibration_profile_combo_);
    profile_form->addRow("Name:", calibration_profile_name_edit_);
    profile_form->addRow("Setup name:", calibration_setup_id_edit_);
    profile_form->addRow("Setup notes:", calibration_notes_edit_);
    profile_form->addRow("Gaze frame:", gaze_frame_edit_);
    profile_form->addRow("Target frame:", target_frame_edit_);
    profiles_layout->addLayout(profile_form);

    stair_tx_spin_ = makePoseSpin();
    stair_ty_spin_ = makePoseSpin();
    stair_tz_spin_ = makePoseSpin();
    stair_qx_spin_ = makeQuaternionSpin();
    stair_qy_spin_ = makeQuaternionSpin();
    stair_qz_spin_ = makeQuaternionSpin();
    stair_qw_spin_ = makeQuaternionSpin(1.0);
    const QString pose_tooltip = "Measured rigid pose of the stair target in Vicon coordinates. Translation is metres; rotation is a quaternion.\nFixed for this setup and shown for reference; it is read from the saved calibration, not from this display.";
    // The caption sits above the spin boxes so the row can wrap when narrow.
    const auto addPoseRow = [layout = profiles_layout, &pose_tooltip](
                                const QString& text, std::initializer_list<QDoubleSpinBox*> spins) {
        auto* caption = new QLabel(text);
        caption->setToolTip(pose_tooltip);
        layout->addWidget(caption);
        auto* row = new FlowLayout();
        for (QDoubleSpinBox* spin : spins) {
            spin->setToolTip(pose_tooltip);
            row->addWidget(spin);
        }
        layout->addLayout(row);
    };
    addPoseRow("Measured stair T (m):", {stair_tx_spin_, stair_ty_spin_, stair_tz_spin_});
    addPoseRow("Measured stair Q:", {stair_qx_spin_, stair_qy_spin_, stair_qz_spin_, stair_qw_spin_});
    for (QDoubleSpinBox* spin : {stair_tx_spin_, stair_ty_spin_, stair_tz_spin_,
                                 stair_qx_spin_, stair_qy_spin_, stair_qz_spin_, stair_qw_spin_}) {
        spin->setReadOnly(true);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    }

    auto* profile_buttons = new FlowLayout();
    auto* apply_profile_button = makeButton("Apply", "Apply the selected saved calibration to the live preview and session details.");
    save_calibration_button_ = makeButton("Save Session Calibration", "Save the current calibration with its physical setup and quality details.");
    auto* duplicate_profile_button = makeButton("Copy", "Copy the selected calibration with a separate ID.");
    auto* retire_profile_button = makeButton("Hide", "Hide the selected calibration without deleting it from past session records.");
    auto* import_profile_button = makeButton("Import", "Import a saved calibration file.");
    auto* export_profile_button = makeButton("Export", "Export the selected calibration to a file.");
    profile_selection_controls_ = {apply_profile_button, duplicate_profile_button, retire_profile_button, export_profile_button};
    for (QWidget* control : {apply_profile_button, save_calibration_button_, duplicate_profile_button,
                             retire_profile_button, import_profile_button, export_profile_button}) {
        profile_buttons->addWidget(control);
    }
    profiles_layout->addLayout(profile_buttons);

    calibration_quality_label_ = new QLabel("Quality: not calibrated");
    calibration_quality_label_->setObjectName("calibrationQuality");
    calibration_quality_label_->setWordWrap(true);
    calibration_metadata_label_ = new QLabel("Coordinate details: not observed");
    calibration_metadata_label_->setWordWrap(true);
    profiles_layout->addWidget(calibration_quality_label_);
    profiles_layout->addWidget(calibration_metadata_label_);
    alignment_layout->addWidget(profiles_group);
    alignment_layout->addStretch(1);

    auto* stair_row = new QHBoxLayout();
    stair_model_edit_ = new QLineEdit();
    auto* browse_stair_button = new QPushButton("Browse");
    stair_row->addWidget(makeTooltipLabel("Stair OBJ:", stair_model_edit_, "Wavefront OBJ file used to render the stair target."));
    stair_row->addWidget(stair_model_edit_, 1);
    stair_row->addWidget(browse_stair_button);
    sources_layout->addLayout(stair_row);
    sources_layout->addStretch(1);

    // Each settings page scrolls on its own and the tabs have a height limit, so
    // the buttons and playback controls below always stay in view.
    settings_tabs->addTab(scrollable(sources_page), "Sources");
    settings_tabs->addTab(scrollable(alignment_page), "Alignment");
    settings_tabs->setMaximumHeight(fontMetrics().height() * 13);
    controls_layout->addWidget(settings_tabs);

    auto* button_row = new FlowLayout();
    start_button_ = new QPushButton("Start Preview");
    stop_button_ = new QPushButton("Stop Preview");
    open_csv_button_ = new QPushButton("Open CSV");
    open_xdf_button_ = new QPushButton("Open XDF");
    play_recording_button_ = new QPushButton("Play Recording");
    play_recording_button_->setEnabled(false);
    status_label_ = new ElidingLabel("Preview stopped");
    auto* delivery_metrics = new ElidingLabel("skipped preview frames 0 | combined updates 0 | delay 0 ms");
    // Explain the numbers rather than repeat them in the tooltip.
    delivery_metrics->setAutomaticToolTip(false);
    delivery_metrics->setToolTip("Older preview updates are skipped so the display stays current.");
    delivery_metrics_label_ = delivery_metrics;
    auto* fit_view_button = makeButton("Fit View", "Fit the camera to all currently visible data.");
    auto* reset_camera_button = makeButton("Reset Camera", "Restore the default camera angle, zoom, and fit.");
    auto* export_image_button = makeButton("Export Image", "Export the current preview view as a PNG image without changing source data.");
    for (QWidget* control : {start_button_, stop_button_, open_csv_button_, open_xdf_button_,
                             play_recording_button_, fit_view_button, reset_camera_button, export_image_button}) {
        button_row->addWidget(control);
    }
    controls_layout->addLayout(button_row);

    // One line each, so the two texts never overlap.
    controls_layout->addWidget(status_label_);
    controls_layout->addWidget(delivery_metrics_label_);

    auto* load_row = new FlowLayout();
    file_state_label_ = new ElidingLabel("No recording loaded");
    memory_label_ = new QLabel("memory 0 MiB");
    load_progress_ = new QProgressBar();
    load_progress_->setRange(0, 100);
    load_progress_->setValue(0);
    load_progress_->setVisible(false);
    cancel_load_button_ = makeButton("Cancel Load", "Cancel the background file load and keep the current source.");
    cancel_load_button_->setEnabled(false);
    recent_files_combo_ = new QComboBox();
    recent_files_combo_->setMinimumContentsLength(16);
    recent_files_combo_->setToolTip("Recently opened CSV and XDF recordings.");
    open_recent_button_ = makeButton("Open Recent", "Open the selected recent recording.");
    load_row->addWidget(file_state_label_);
    load_row->addWidget(memory_label_);
    load_row->addWidget(load_progress_);
    load_row->addWidget(cancel_load_button_);
    load_row->addWidget(recent_files_combo_);
    load_row->addWidget(open_recent_button_);
    controls_layout->addLayout(load_row);

    auto* playback_row = new FlowLayout();
    auto* jump_start_button = makeButton("|<", "Go to the first loaded frame.", "Jump to recording start");
    auto* step_back_button = makeButton("<", "Go back one loaded frame.", "Step one frame backward");
    auto* jump_back_button = makeButton("- Jump", "Seek backward by the selected time interval.", "Jump backward by selected time");
    auto* jump_forward_button = makeButton("Jump +", "Seek forward by the selected time interval.", "Jump forward by selected time");
    auto* step_forward_button = makeButton(">", "Go forward one loaded frame.", "Step one frame forward");
    auto* jump_end_button = makeButton(">|", "Go to the final loaded frame.", "Jump to recording end");
    timeline_slider_ = new QSlider(Qt::Horizontal);
    timeline_slider_->setRange(0, 10000);
    timeline_slider_->setEnabled(false);
    timeline_slider_->setAccessibleName("Recording playback timeline");
    jump_seconds_spin_ = new QDoubleSpinBox();
    jump_seconds_spin_->setRange(0.1, 600.0);
    jump_seconds_spin_->setValue(5.0);
    jump_seconds_spin_->setSuffix(" s");
    jump_seconds_spin_->setToolTip("Time used by the backward and forward Jump controls.");
    loop_playback_check_ = makeCheck("Loop", "Restart explicitly at the end of the recording.");
    playback_position_label_ = new QLabel("0.000 / 0.000 s | frame 0/0");
    playback_row->addWidget(jump_start_button);
    playback_row->addWidget(step_back_button);
    playback_row->addWidget(jump_back_button);
    playback_row->addWidget(jump_seconds_spin_);
    playback_row->addWidget(jump_forward_button);
    playback_row->addWidget(step_forward_button);
    playback_row->addWidget(jump_end_button);
    playback_row->addWidget(loop_playback_check_);
    playback_row->addWidget(playback_position_label_);
    // Keep the one-symbol buttons narrow so the row does not wrap.
    for (QPushButton* glyph : {jump_start_button, step_back_button,
                               step_forward_button, jump_end_button}) {
        glyph->setFixedWidth(34);
    }
    playback_controls_ = {jump_start_button, step_back_button, jump_back_button,
                          jump_forward_button, step_forward_button, jump_end_button};
    // Playback controls stay hidden until a recording is loaded, to save space.
    playback_area_ = new QWidget();
    auto* playback_area_layout = new QVBoxLayout(playback_area_);
    playback_area_layout->setContentsMargins(0, 0, 0, 0);
    playback_area_layout->setSpacing(4);
    recording_calibration_warning_ = new QLabel();
    recording_calibration_warning_->setObjectName("recordingCalibrationWarning");
    recording_calibration_warning_->setAccessibleName("Recording gaze alignment warning");
    recording_calibration_warning_->setWordWrap(true);
    recording_calibration_warning_->setVisible(false);
    playback_area_layout->addWidget(recording_calibration_warning_);
    playback_area_layout->addLayout(playback_row);
    // The timeline gets its own full-width row.
    playback_area_layout->addWidget(timeline_slider_);
    playback_area_->setVisible(false);
    controls_layout->addWidget(playback_area_);

    controls_scroll_ = new ContentSizedScrollArea();
    controls_scroll_->setWidgetResizable(true);
    controls_scroll_->setFrameShape(QFrame::NoFrame);
    // Sized to fit the controls, with resizeEvent() capping its height so they
    // only scroll when the panel is short.
    controls_scroll_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    controls_scroll_->setAccessibleName("Scrollable preview controls");
    controls_scroll_->setWidget(controls_group);
    layout->addWidget(controls_scroll_);

    playback_timer_ = new QTimer(this);
    playback_timer_->setInterval(16);
    playback_elapsed_.start();
    live_render_timer_ = new QTimer(this);
    live_render_timer_->setTimerType(Qt::PreciseTimer);
    live_render_timer_->setInterval(1000 / kDefaultRenderHz);

    start_button_->setShortcut(QKeySequence("Alt+P"));
    stop_button_->setShortcut(QKeySequence("Alt+Shift+P"));
    open_xdf_button_->setShortcut(QKeySequence::Open);
    play_recording_button_->setShortcut(Qt::Key_Space);
    fit_view_button->setShortcut(Qt::Key_F);
    reset_camera_button->setShortcut(Qt::Key_R);

    for (QPushButton* btn : findChildren<QPushButton*>()) {
        if (btn->accessibleName().trimmed().isEmpty()) {
            QString acc = btn->text();
            acc.remove('&');
            btn->setAccessibleName(acc.trimmed());
        }
    }
    status_label_->setAccessibleName("Preview status");
    delivery_metrics_label_->setAccessibleName("Preview delivery health");
    file_state_label_->setAccessibleName("Recording file load state");
    memory_label_->setAccessibleName("Playback memory estimate");
    playback_position_label_->setAccessibleName("Playback time and frame position");
    calibration_quality_label_->setAccessibleName("Saved calibration quality");
    calibration_metadata_label_->setAccessibleName("Calibration coordinate details");

    connect(start_button_, &QPushButton::clicked, this, &PreviewPanel::startPreview);
    connect(stop_button_, &QPushButton::clicked, this, &PreviewPanel::stopPreview);
    connect(open_csv_button_, &QPushButton::clicked, this, &PreviewPanel::openMergedCsv);
    connect(open_xdf_button_, &QPushButton::clicked, this, &PreviewPanel::openXdf);
    connect(play_recording_button_, &QPushButton::clicked, this, &PreviewPanel::togglePlayback);
    connect(playback_timer_, &QTimer::timeout, this, &PreviewPanel::advancePlayback);
    connect(live_render_timer_, &QTimer::timeout, this, &PreviewPanel::displayLatestLiveFrame);
    connect(fit_view_button, &QPushButton::clicked, this, &PreviewPanel::fitView);
    connect(reset_camera_button, &QPushButton::clicked, this, &PreviewPanel::resetCamera);
    connect(export_image_button, &QPushButton::clicked, this, &PreviewPanel::exportPreviewImage);
    connect(cancel_load_button_, &QPushButton::clicked, this, &PreviewPanel::cancelFileLoad);
    connect(open_recent_button_, &QPushButton::clicked, this, &PreviewPanel::openRecentRecording);
    connect(timeline_slider_, &QSlider::valueChanged, this, &PreviewPanel::seekPlaybackFromSlider);
    connect(loop_playback_check_, &QCheckBox::toggled, this, [this](bool loop) {
        playback_clock_.setLooping(loop, playback_elapsed_.elapsed() / 1000.0);
    });
    connect(jump_start_button, &QPushButton::clicked, this, [this]() { seekToFrame(0); });
    connect(jump_end_button, &QPushButton::clicked, this, [this]() {
        if (!recording_frames_.empty()) seekToFrame(recording_frames_.size() - 1);
    });
    connect(step_back_button, &QPushButton::clicked, this, [this]() {
        if (recording_frames_.empty()) return;
        const std::size_t idx = playback_clock_.frameIndex(playback_elapsed_.elapsed() / 1000.0);
        seekToFrame(idx == 0 ? 0 : idx - 1);
    });
    connect(step_forward_button, &QPushButton::clicked, this, [this]() {
        if (recording_frames_.empty()) return;
        const std::size_t idx = playback_clock_.frameIndex(playback_elapsed_.elapsed() / 1000.0);
        seekToFrame((std::min)(recording_frames_.size() - 1, idx + 1));
    });
    connect(jump_back_button, &QPushButton::clicked, this, [this]() { seekBySeconds(-jump_seconds_spin_->value()); });
    connect(jump_forward_button, &QPushButton::clicked, this, [this]() { seekBySeconds(jump_seconds_spin_->value()); });
    connect(playback_speed_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double s) {
        playback_clock_.setSpeed(s, playback_elapsed_.elapsed() / 1000.0);
    });
    connect(browse_stair_button, &QPushButton::clicked, this, &PreviewPanel::browseStairModel);
    connect(stair_model_edit_, &QLineEdit::editingFinished, this, &PreviewPanel::reloadStairModel);
    connect(calibrate_button_, &QPushButton::clicked, this, &PreviewPanel::beginCalibration);
    connect(clear_calibration_button_, &QPushButton::clicked, this, &PreviewPanel::clearCalibration);
    connect(apply_profile_button, &QPushButton::clicked, this, &PreviewPanel::applySelectedCalibrationProfile);
    connect(save_calibration_button_, &QPushButton::clicked, this, &PreviewPanel::saveSessionCalibrationProfile);
    connect(duplicate_profile_button, &QPushButton::clicked, this, &PreviewPanel::duplicateCalibrationProfile);
    connect(retire_profile_button, &QPushButton::clicked, this, &PreviewPanel::retireCalibrationProfile);
    connect(import_profile_button, &QPushButton::clicked, this, &PreviewPanel::importCalibrationProfile);
    connect(export_profile_button, &QPushButton::clicked, this, &PreviewPanel::exportCalibrationProfile);
    connect(calibration_profile_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshCalibrationProfileUi();
    });

    connect(trail_points_spin_, QOverload<int>::of(&QSpinBox::valueChanged), widget_, &PreviewWidget::setTrailPointLimit);

    loadSettings();
    loadCalibrationProfiles();
    reloadStairModel();
    refreshControlStates();
    calibration_progress_throttle_.start();
}

PreviewPanel::~PreviewPanel() {
    if (file_loader_) {
        file_loader_->cancel();
        file_loader_->wait();
    }
    if (worker_) {
        worker_->requestInterruption();
        worker_->wait();
    }
    saveSettings();
}

QVector<gui::StreamIdentity> PreviewPanel::streamInventory() const {
    return worker_ ? worker_->streamInventory() : latest_stream_inventory_;
}

void PreviewPanel::applySessionConfiguration(const gui::SessionConfiguration& c) {
    marker_binding_ = c.preview_markers;
    segment_binding_ = c.preview_segments;
    gaze_binding_ = c.preview_gaze;
    calibration_binding_ = c.preview_calibration;
    marker_stream_edit_->setText(marker_binding_.name);
    segment_stream_edit_->setText(segment_binding_.name);
    gaze_stream_edit_->setText(gaze_binding_.name);
    calibration_stream_edit_->setText(calibration_binding_.name);
    marker_stream_edit_->setReadOnly(!c.preview_external_streams);
    segment_stream_edit_->setReadOnly(!c.preview_external_streams);
    tolerance_spin_->setValue(c.preview_match_tolerance);
    cache_megabytes_spin_->setValue(c.preview_cache_megabytes);
    trail_points_spin_->setValue(c.preview_trail_points);
    playback_speed_spin_->setValue(c.preview_playback_speed);
    loop_playback_check_->setChecked(c.preview_loop_playback);
    const int hz = std::clamp(c.preview_render_hz, 1, kMaximumRenderHz);
    live_render_timer_->setInterval((std::max)(1, 1000 / hz));
    if (!c.stair_model_path.trimmed().isEmpty()) stair_model_edit_->setText(c.stair_model_path);
    const int idx = calibration_profile_combo_ ? calibration_profile_combo_->findData(c.calibration_profile_id) : -1;
    if (idx >= 0) calibration_profile_combo_->setCurrentIndex(idx);
}

void PreviewPanel::updateSessionConfiguration(gui::SessionConfiguration& c) const {
    c.preview_markers = marker_binding_;
    c.preview_segments = segment_binding_;
    c.preview_gaze = gaze_binding_;
    c.preview_calibration = calibration_binding_;
    c.preview_markers.name = marker_stream_edit_->text().trimmed();
    c.preview_segments.name = segment_stream_edit_->text().trimmed();
    c.preview_gaze.name = gaze_stream_edit_->text().trimmed();
    c.preview_calibration.name = calibration_stream_edit_->text().trimmed();
    c.preview_match_tolerance = tolerance_spin_->value();
    c.preview_cache_megabytes = cache_megabytes_spin_->value();
    c.preview_trail_points = trail_points_spin_->value();
    c.preview_playback_speed = playback_speed_spin_->value();
    c.preview_loop_playback = loop_playback_check_->isChecked();
    c.stair_model_path = stair_model_edit_->text().trimmed();
    if (calibration_profile_combo_) c.calibration_profile_id = calibration_profile_combo_->currentData().toString();
}

void PreviewPanel::requestShutdown() {
    pending_recording_path_.clear();
    if (file_loader_) cancelFileLoad();
    stopPreview();
    playback_timer_->stop();
}

bool PreviewPanel::shutdownReady() const {
    return worker_ == nullptr && file_loader_ == nullptr;
}

void PreviewPanel::openRecording(const QString& path) {
    const auto type = recordingFileType(path);
    if (!type) {
        setStatus("Unsupported preview recording type: " + QFileInfo(path).suffix());
        return;
    }
    if (worker_) {
        pending_recording_path_ = path;
        stopPreview();
        setStatus("Stopping preview before opening " + QFileInfo(path).fileName() + "...");
        return;
    }
    startFileLoad(*type, path);
}

void PreviewPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!controls_scroll_) return;
    // Cap the controls so a little over half the panel stays free for the
    // drawing, setting only a maximum because a minimum would stop the window
    // getting shorter.
    const int reserved = (std::max)(widget_->minimumHeight(),
                                    static_cast<int>(height() * 0.55));
    const int cap = (std::max)(0, height() - reserved - layout()->spacing());
    // Setting it calls this handler again, so only set it when it changes.
    if (controls_scroll_->maximumHeight() != cap) controls_scroll_->setMaximumHeight(cap);
}

void PreviewPanel::fitView() { widget_->fitView(); }
void PreviewPanel::resetCamera() { widget_->resetCamera(); }

void PreviewPanel::exportPreviewImage() {
    const QString path = QFileDialog::getSaveFileName(this, "Export preview image", QString(), "PNG images (*.png)");
    if (path.isEmpty()) return;
    QString norm = path.endsWith(".png", Qt::CaseInsensitive) ? path : path + ".png";
    if (!widget_->grab().save(norm, "PNG")) {
        setStatus("Could not export preview image to " + norm);
        return;
    }
    setStatus("Exported preview image to " + QDir::toNativeSeparators(norm));
}

void PreviewPanel::startPreview() {
    if (file_loader_ || !pending_recording_path_.isEmpty()) return;
    if (worker_) {
        stopPreview();
        if (worker_) return;
    }
    playback_timer_->stop();
    play_recording_button_->setText("Play Recording");
    saveSettings();
    widget_->setTrailPointLimit(trail_points_spin_->value());
    widget_->resetForNewSource();
    calibration_samples_.clear();

    // The stream names come from the text fields, and the rest from the bindings.
    const auto named = [](gui::StreamBinding binding, const QLineEdit* edit) {
        binding.name = edit->text().trimmed();
        return binding;
    };
    PreviewWorkerConfig config;
    config.markers = named(marker_binding_, marker_stream_edit_);
    config.segments = named(segment_binding_, segment_stream_edit_);
    config.gaze = named(gaze_binding_, gaze_stream_edit_);
    config.calibration = named(calibration_binding_, calibration_stream_edit_);
    config.match_tolerance_seconds = tolerance_spin_->value();
    config.vicon_transform = viconPreviewTransform();
    config.gaze_transform = gazeTransform();

    worker_ = new PreviewStreamWorker(std::move(config), this);
    worker_stopping_ = false;
    PreviewStreamWorker* const started_worker = worker_;
    connect(worker_, &PreviewStreamWorker::targetPoseReady, this, &PreviewPanel::handleTargetPose);
    connect(worker_, &PreviewStreamWorker::statusChanged, this, &PreviewPanel::setStatus);
    connect(worker_, &PreviewStreamWorker::lifecycleChanged, this, [this](ComponentLifecycleState state, const QString& detail) {
        lifecycle_state_ = state;
        if (!detail.isEmpty()) setStatus(detail);
        emit lifecycleChanged(state, detail);
    });
    connect(worker_, &PreviewStreamWorker::streamIdentityChanged, this, [this](const gui::StreamIdentity& identity, const QString& warning) {
        gui::StreamIdentity updated = identity;
        updated.warning = warning;
        auto update_binding = [&updated](gui::StreamBinding& b) {
            if (b.reconnection == gui::StreamReconnectionMode::SourceIdentity && b.source_id.isEmpty()) b.source_id = updated.source_id;
        };
        if (updated.role == "markers") update_binding(marker_binding_);
        if (updated.role == "segments") update_binding(segment_binding_);
        if (updated.role == "gaze") update_binding(gaze_binding_);
        if (updated.role == "calibration") update_binding(calibration_binding_);
        latest_stream_inventory_.erase(std::remove_if(latest_stream_inventory_.begin(), latest_stream_inventory_.end(),
            [&updated](const gui::StreamIdentity& ex) { return ex.role == updated.role; }), latest_stream_inventory_.end());
        latest_stream_inventory_.push_back(updated);
        QString gaze_frame, target_frame;
        for (const gui::StreamIdentity& s : latest_stream_inventory_) {
            if (s.role == "gaze") gaze_frame = s.coordinate_frame;
            if (s.role == "calibration") target_frame = s.coordinate_frame;
        }
        calibration_metadata_compatible_ = !gaze_frame.isEmpty() && !target_frame.isEmpty() &&
            calibrationCoordinateFramesCompatible(gaze_frame.toStdString(), target_frame.toStdString());
        calibration_metadata_label_->setText(calibration_metadata_compatible_
            ? "Coordinate details: matching (" + gaze_frame + ")" : "Coordinate details: missing or do not match");
        emit streamInventoryChanged(latest_stream_inventory_);
        emit calibrationStateChanged(sessionCalibrationState(), calibration_quality_label_->text(), calibration_metadata_compatible_);
    });
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &QThread::finished, this, [this, started_worker]() {
        if (worker_ != started_worker) return;
        worker_ = nullptr;
        lifecycle_state_ = ComponentLifecycleState::Stopped;
        emit lifecycleChanged(lifecycle_state_, "Preview stopped");
        live_render_timer_->stop();
        worker_stopping_ = false;
        refreshControlStates();
        if (!pending_recording_path_.isEmpty()) {
            const QString path = std::exchange(pending_recording_path_, QString());
            openRecording(path);
        } else setStatus("Preview stopped");
    });
    refreshControlStates();
    setStatus("Finding the selected LSL streams; stair calibration is ready when requested...");
    lifecycle_state_ = ComponentLifecycleState::Starting;
    emit lifecycleChanged(lifecycle_state_, "Finding selected streams");
    live_render_timer_->start();
    worker_->start();
}

void PreviewPanel::stopPreview() {
    if (!worker_ || worker_stopping_) return;
    worker_stopping_ = true;
    lifecycle_state_ = ComponentLifecycleState::Stopping;
    emit lifecycleChanged(lifecycle_state_, "Preview stop requested");
    worker_->requestInterruption();
    refreshControlStates();
    setStatus("Preview stopping in the background...");
}

void PreviewPanel::displayLatestLiveFrame() {
    if (!worker_) return;
    PreviewFrame frame;
    PreviewDeliveryMetrics metrics;
    if (worker_->takeLatestFrame(frame, metrics)) widget_->setFrame(std::move(frame));
    const bool late = metrics.display_latency_ms > kMaximumLivePreviewDelayMs;
    delivery_metrics_label_->setText(
        QString(late ? "PREVIEW DELAYED | skipped preview frames " : "skipped preview frames ") +
        QString::number(metrics.replaced_before_display) + " | combined updates " +
        QString::number(metrics.coalesced_input_samples) + " | delay " + QString::number(metrics.display_latency_ms) + " ms");
    delivery_metrics_label_->setToolTip(late
        ? "Preview delay is above the " + QString::number(kMaximumLivePreviewDelayMs) + " ms target; source-rate measurements are unaffected."
        : "Preview updates are arriving within the delay target.");
    emit deliveryMetricsChanged(metrics);
}

void PreviewPanel::beginCalibration() {
    if (!worker_) {
        setStatus("Start the preview before calibrating from the stair target");
        return;
    }
    if (!calibration_metadata_compatible_) {
        const auto ans = QMessageBox::warning(
            this, "Coordinate details unavailable",
            "Gaze and stair-target coordinate details are missing or do not match. Continue for this session anyway?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes) {
            updateCalibrationPersistentStatus(gui::SessionCalibrationState::Failed,
                "Calibration canceled because the coordinate details were not confirmed", false);
            return;
        }
    }
    calibration_samples_.clear();
    calibration_state_ = gui::SessionCalibrationState::Collecting;
    calibration_rejection_reason_.clear();
    const CalibrationProfile profile = activeSolverProfile();
    refreshControlStates();
    setStatus("Waiting for " + QString::number(profile.required_samples) + " stable tracked stair-target poses...");
    updateCalibrationPersistentStatus(gui::SessionCalibrationState::Collecting,
        "Quality: collecting 0/" + QString::number(profile.required_samples), calibration_metadata_compatible_);
}

void PreviewPanel::clearCalibration() {
    resetCalibrationSession();
    calibration_quality_ = {};
    calibration_rejection_reason_.clear();
    if (worker_) worker_->setGazeTransform(gazeTransform());
    widget_->requestViewRefit();
    refreshControlStates();
    setStatus("Calibration cleared; gaze is drawn in its published HoloLens frame");
    updateCalibrationPersistentStatus(gui::SessionCalibrationState::Uncalibrated,
        "Quality: not calibrated", calibration_metadata_compatible_);
}

void PreviewPanel::handleTargetPose(CalibrationTargetPose pose) {
    const CalibrationProfile profile = activeSolverProfile();
    if (calibration_state_ != gui::SessionCalibrationState::Collecting) return;
    if (!pose.tracked) {
        calibration_samples_.clear();
        calibration_rejection_reason_ = "Stair target lost";
        if (calibration_progress_throttle_.elapsed() >= 100) {
            setStatus("Stair target lost; waiting for a steady reading...");
            calibration_progress_throttle_.restart();
        }
        return;
    }

    if (!calibration_samples_.empty() && !targetPoseWithinTolerance(calibration_samples_.front(), pose, profile)) {
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
            const QString prog = "Collecting stair-target poses: " + QString::number(calibration_samples_.size()) + "/" +
                                 QString::number(profile.required_samples);
            setStatus(prog);
            calibration_quality_label_->setText("Quality: " + prog);
            calibration_progress_throttle_.restart();
        }
        return;
    }

    const auto solution = solveTrackedTargetCalibration(calibration_samples_, profile);
    calibration_state_ = gui::SessionCalibrationState::Uncalibrated;
    calibration_samples_.clear();
    if (!solution) {
        calibration_rejection_reason_ = "Position or angle error exceeded the selected limits";
        // A rejected calibration leaves none in use, so draw gaze without one again.
        calibration_quality_ = {};
        if (worker_) worker_->setGazeTransform(gazeTransform());
        widget_->requestViewRefit();
        refreshControlStates();
        setStatus("Calibration failed: " + calibration_rejection_reason_);
        updateCalibrationPersistentStatus(gui::SessionCalibrationState::Failed,
            "Quality: rejected — " + calibration_rejection_reason_, calibration_metadata_compatible_);
        return;
    }

    automatic_gaze_transform_ = gazeTransformFromTargetCalibration(profile, solution->holo_from_target);
    calibration_state_ = gui::SessionCalibrationState::AutomaticSession;
    const gui::ManagedCalibrationProfile* measured_with = selectedCalibrationProfile();
    calibration_source_id_ = measured_with ? measured_with->id : QString();
    calibration_quality_ = solution->quality;
    if (worker_) worker_->setGazeTransform(gazeTransform());
    reloadStairModel();
    widget_->requestViewRefit();
    refreshControlStates();
    setStatus(solution->uses_frozen_reference
        ? "Calibration applied from the frozen stair reference (Vuforia paused)"
        : "Stair-target calibration applied for this session (" + errorText(solution->quality) + ")");
    updateCalibrationPersistentStatus(gui::SessionCalibrationState::AutomaticSession,
        solution->uses_frozen_reference
            ? "Quality: frozen stair reference; repeated samples are not new tracking measurements"
            : "Quality: " + QString::number(solution->quality.sample_count) + " samples, " +
                  errorText(solution->quality),
        calibration_metadata_compatible_);
}

void PreviewPanel::openMergedCsv() { browseRecording("Open merged preview CSV", "CSV files (*.csv);;All files (*)"); }
void PreviewPanel::openXdf() { browseRecording("Open recorded XDF", "XDF files (*.xdf);;All files (*)"); }

void PreviewPanel::browseRecording(const QString& title, const QString& filter) {
    const QString path = QFileDialog::getOpenFileName(this, title, QString(), filter);
    if (!path.isEmpty()) openRecording(path);
}

void PreviewPanel::startFileLoad(PreviewFileType type, const QString& path) {
    if (file_loader_) {
        setStatus("A recording is already loading; cancel it before opening another file");
        return;
    }
    PreviewLoadOptions opt;
    opt.maximum_memory_bytes = static_cast<std::size_t>(cache_megabytes_spin_->value()) * 1024ULL * 1024ULL;

    auto* loader = new PreviewFileLoader(type, path, viconPreviewTransform(), gazeTransform(),
                                         tolerance_spin_->value(), opt, this);
    file_loader_ = loader;
    refreshControlStates();
    setFileState("Loading " + QFileInfo(path).fileName());
    emit fileStateChanged(gui::SessionFileState::Loading, "Loading " + QFileInfo(path).fileName());
    load_progress_->setValue(0);
    load_progress_->setVisible(true);
    cancel_load_button_->setEnabled(true);
    connect(loader, &PreviewFileLoader::progressChanged, this, [this, loader](const QString& stage, int pct, const QString& detail) {
        if (file_loader_ != loader) return;
        load_progress_->setValue(pct);
        setFileState(stage + (detail.isEmpty() ? QString() : ": " + detail));
    });
    connect(loader, &PreviewFileLoader::mappingRequired, this, [this, loader](const XdfMappingAnalysis& a) {
        requestRecordedStreamMapping(loader, a);
    });
    connect(loader, &PreviewFileLoader::loadSucceeded, this, [this, loader](const QString& sum) {
        applyLoadedRecording(loader, sum);
    });
    connect(loader, &PreviewFileLoader::loadFailed, this, [this, loader](const QString& err, bool canceled) {
        if (file_loader_ != loader) return;
        setFileState(canceled ? "Load canceled" : "Load failed");
        emit fileStateChanged(canceled ? gui::SessionFileState::Canceled : gui::SessionFileState::Failed, err);
        setStatus((canceled ? "Canceled file load; previous source retained: " : "Failed to load recording; previous source retained: ") + err);
    });
    connect(loader, &QThread::finished, loader, &QObject::deleteLater);
    connect(loader, &QThread::finished, this, [this, loader]() {
        if (file_loader_ != loader) return;
        file_loader_ = nullptr;
        load_progress_->setVisible(false);
        cancel_load_button_->setEnabled(false);
        refreshControlStates();
    });
    loader->start();
}

void PreviewPanel::applyLoadedRecording(PreviewFileLoader* loader, const QString& summary) {
    if (file_loader_ != loader) return;
    std::optional<PreviewRecording> loaded = loader->takeRecording();
    if (!loaded || loaded->frames.empty()) {
        setStatus("Recording contained no usable preview frames; previous source retained");
        setFileState("No usable frames");
        return;
    }
    playback_timer_->stop();
    play_recording_button_->setText("Play Recording");
    widget_->resetForNewSource();
    recording_frames_ = std::move(loaded->frames);
    recording_calibration_warning_->setText(QString::fromStdString(loaded->calibration_warning));
    recording_calibration_warning_->setVisible(!loaded->calibration_warning.empty());
    playback_clock_.setFrameTimeline(recording_frames_);
    playback_clock_.setLooping(loop_playback_check_->isChecked(), playback_elapsed_.elapsed() / 1000.0);
    playback_clock_.setSpeed(playback_speed_spin_->value(), playback_elapsed_.elapsed() / 1000.0);
    widget_->setFrame(recording_frames_.front());
    play_recording_button_->setEnabled(true);
    timeline_slider_->setEnabled(true);
    refreshControlStates();
    memory_label_->setText("memory " + QString::number(static_cast<double>(loaded->estimated_memory_bytes) / (1024.0 * 1024.0), 'f', 1) + " MiB");
    setFileState("Loaded " + QFileInfo(loader->path()).fileName());
    emit fileStateChanged(gui::SessionFileState::Loaded, QDir::toNativeSeparators(QFileInfo(loader->path()).absoluteFilePath()));
    rememberRecentFile(loader->path());
    updatePlaybackDisplay();
    setStatus("Loaded " + QFileInfo(loader->path()).fileName() + " (" + summary + ")");
}

void PreviewPanel::requestRecordedStreamMapping(PreviewFileLoader* loader, const XdfMappingAnalysis& analysis) {
    if (file_loader_ != loader) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Choose Recorded Streams");
    auto* layout = new QVBoxLayout(&dialog);
    auto* exp = new QLabel(QString::fromStdString(analysis.explanation));
    exp->setWordWrap(true);
    layout->addWidget(exp);
    auto* form = new QFormLayout();
    std::map<PreviewStreamRole, QComboBox*> role_combos;
    const PreviewStreamRole roles[] = {
        PreviewStreamRole::ViconMarkers, PreviewStreamRole::ViconSegments,
        PreviewStreamRole::HoloLensGaze, PreviewStreamRole::HoloLensCalibrationTarget,
    };
    for (PreviewStreamRole role : roles) {
        auto* combo = new QComboBox();
        QSet<QString> seen;
        for (const XdfStreamCandidate& c : analysis.candidates) {
            if (c.role != role) continue;
            const QString group = QString::fromStdString(c.group_key);
            if (seen.contains(group)) continue;
            seen.insert(group);
            combo->addItem(QString::fromStdString(c.display_name + " | source " +
                (c.source_id.empty() ? "<missing>" : c.source_id) + " | host " + c.hostname + " | " +
                std::to_string(c.sample_count) + " samples"), static_cast<qulonglong>(c.stream_id));
        }
        if (combo->count() > 0) {
            const QString lbl = role == PreviewStreamRole::ViconMarkers ? "Markers:" :
                                role == PreviewStreamRole::ViconSegments ? "Segments:" :
                                role == PreviewStreamRole::HoloLensGaze ? "Gaze:" : "Calibration:";
            form->addRow(lbl, combo);
            role_combos[role] = combo;
        } else delete combo;
    }
    auto* master_combo = new QComboBox();
    for (const XdfStreamCandidate& c : analysis.candidates) {
        if (c.role == PreviewStreamRole::HoloLensCalibrationTarget) continue;
        master_combo->addItem(QString::fromStdString(c.display_name + " | " + std::to_string(c.sample_count) + " samples"),
                              static_cast<qulonglong>(c.stream_id));
    }
    const int s_master = master_combo->findData(static_cast<qulonglong>(analysis.suggested_mapping.master_stream_id));
    if (s_master >= 0) master_combo->setCurrentIndex(s_master);
    form->addRow("Timing source:", master_combo);
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
        mapping.selected_stream_ids.push_back(static_cast<std::uint32_t>(item.second->currentData().toULongLong()));
    }
    loader->provideMapping(mapping);
}

void PreviewPanel::togglePlayback() {
    if (recording_frames_.empty()) return;
    if (playback_timer_->isActive()) {
        playback_clock_.pause(playback_elapsed_.elapsed() / 1000.0);
        playback_timer_->stop();
        play_recording_button_->setText("Play Recording");
    } else {
        if (playback_clock_.atEnd(playback_elapsed_.elapsed() / 1000.0)) {
            playback_clock_.seek(0.0, playback_elapsed_.elapsed() / 1000.0);
        }
        playback_clock_.play(playback_elapsed_.elapsed() / 1000.0);
        playback_timer_->start();
        play_recording_button_->setText("Pause Recording");
    }
}

void PreviewPanel::advancePlayback() {
    if (recording_frames_.empty()) {
        playback_timer_->stop();
        play_recording_button_->setText("Play Recording");
        return;
    }
    const double now = playback_elapsed_.elapsed() / 1000.0;
    widget_->setFrame(recording_frames_[playback_clock_.frameIndex(now)]);
    updatePlaybackDisplay();
    if (playback_clock_.atEnd(now)) {
        playback_clock_.pause(now);
        playback_timer_->stop();
        play_recording_button_->setText("Play Recording");
    }
}

void PreviewPanel::cancelFileLoad() {
    if (file_loader_) {
        file_loader_->cancel();
        cancel_load_button_->setEnabled(false);
        setFileState("Canceling load...");
    }
}

void PreviewPanel::seekPlaybackFromSlider(int value) {
    if (recording_frames_.empty()) return;
    const double dur = playback_clock_.duration();
    playback_clock_.seek(dur * static_cast<double>(value) / static_cast<double>(timeline_slider_->maximum()),
                         playback_elapsed_.elapsed() / 1000.0);
    const std::size_t idx = playback_clock_.frameIndex(playback_elapsed_.elapsed() / 1000.0);
    widget_->setFrame(recording_frames_[idx]);
    updatePlaybackDisplay();
}

void PreviewPanel::seekToFrame(std::size_t frame_index) {
    if (recording_frames_.empty()) return;
    frame_index = (std::min)(frame_index, recording_frames_.size() - 1);
    const double pos = recording_frames_[frame_index].timestamp - recording_frames_.front().timestamp;
    playback_clock_.seek(pos, playback_elapsed_.elapsed() / 1000.0);
    widget_->setFrame(recording_frames_[frame_index]);
    updatePlaybackDisplay();
}

void PreviewPanel::seekBySeconds(double seconds) {
    if (recording_frames_.empty()) return;
    const double now = playback_elapsed_.elapsed() / 1000.0;
    playback_clock_.seek(playback_clock_.position(now) + seconds, now);
    const std::size_t idx = playback_clock_.frameIndex(now);
    widget_->setFrame(recording_frames_[idx]);
    updatePlaybackDisplay();
}

void PreviewPanel::updatePlaybackDisplay() {
    if (recording_frames_.empty()) {
        playback_position_label_->setText("0.000 / 0.000 s | frame 0/0");
        return;
    }
    const double now = playback_elapsed_.elapsed() / 1000.0;
    const double pos = playback_clock_.position(now);
    const std::size_t idx = playback_clock_.frameIndex(now);
    {
        const QSignalBlocker blocker(timeline_slider_);
        const int s_val = playback_clock_.duration() <= 0.0
            ? 0 : static_cast<int>(timeline_slider_->maximum() * pos / playback_clock_.duration());
        timeline_slider_->setValue(s_val);
    }
    playback_position_label_->setText(
        QString::number(pos, 'f', 3) + " / " + QString::number(playback_clock_.duration(), 'f', 3) + " s | frame " +
        QString::number(idx + 1) + "/" + QString::number(recording_frames_.size()));
}

void PreviewPanel::rememberRecentFile(const QString& path) {
    const QString norm = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QStringList files = {norm};
    for (int i = 0; i < recent_files_combo_->count(); ++i) {
        const QString ex = recent_files_combo_->itemData(i).toString();
        if (ex.compare(norm, Qt::CaseInsensitive) != 0) files.push_back(ex);
    }
    while (files.size() > 10) files.removeLast();
    recent_files_combo_->clear();
    for (const QString& f : files) recent_files_combo_->addItem(QFileInfo(f).fileName(), f);
    refreshControlStates();
}

void PreviewPanel::openRecentRecording() {
    const QString path = recent_files_combo_->currentData().toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        setStatus("The selected recent recording is no longer available");
        return;
    }
    openRecording(path);
}

void PreviewPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            const QString path = url.toLocalFile();
            if (recordingFileType(path)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void PreviewPanel::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (recordingFileType(path)) {
            openRecording(path);
            event->acceptProposedAction();
            return;
        }
    }
}

void PreviewPanel::browseStairModel() {
    const QString path = QFileDialog::getOpenFileName(this, "Select stair OBJ", stair_model_edit_->text(), "Wavefront OBJ (*.obj);;All files (*)");
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
        setStatus("Stair model could not be read: " + QString::fromUtf8(ex.what()));
    }
}

bool PreviewPanel::calibrationInUse() const {
    return calibration_state_ == gui::SessionCalibrationState::AutomaticSession ||
           calibration_state_ == gui::SessionCalibrationState::SavedProfile;
}

// Without a calibration, gaze is drawn in its own HoloLens coordinates rather
// than in a guessed Vicon position.
PreviewTransformProfile PreviewPanel::gazeTransform() const {
    return calibrationInUse() ? automatic_gaze_transform_ : PreviewTransformProfile{};
}

void PreviewPanel::refreshControlStates() {
    start_button_->setEnabled(!worker_ && !file_loader_ && pending_recording_path_.isEmpty());
    stop_button_->setEnabled(worker_ && !worker_stopping_);
    const bool can_open = !worker_stopping_ && !file_loader_;
    open_csv_button_->setEnabled(can_open);
    open_xdf_button_->setEnabled(can_open);
    const bool calibrated = calibrationInUse();
    const bool collecting = calibration_state_ == gui::SessionCalibrationState::Collecting;
    const bool live_preview = worker_ != nullptr && !worker_stopping_;
    if (calibrate_button_) calibrate_button_->setEnabled(live_preview && !collecting);
    if (clear_calibration_button_) clear_calibration_button_->setEnabled(calibrated);
    if (save_calibration_button_) save_calibration_button_->setEnabled(calibrated);
    const bool has_profile = selectedCalibrationProfile() != nullptr;
    for (QWidget* control : profile_selection_controls_) control->setEnabled(has_profile);
    const bool recording_loaded = !recording_frames_.empty();
    if (playback_area_) playback_area_->setVisible(recording_loaded);
    for (QWidget* control : playback_controls_) control->setEnabled(recording_loaded);
    if (open_recent_button_ && recent_files_combo_) {
        open_recent_button_->setEnabled(recent_files_combo_->count() > 0);
    }
}

void PreviewPanel::resetCalibrationSession() {
    calibration_samples_.clear();
    calibration_state_ = gui::SessionCalibrationState::Uncalibrated;
    automatic_gaze_transform_ = {};
    calibration_source_id_.clear();
}

PreviewTransformProfile PreviewPanel::stairTransform() const {
    PreviewTransformProfile t = transformProfileFromRigid(activeSolverProfile().vicon_from_target);
    t.scale = 0.001;
    return t;
}

void PreviewPanel::loadCalibrationProfiles() {
    calibration_profiles_ = gui::CalibrationProfileStore::load(*settings_);
    const gui::SessionConfiguration conf = gui::SessionConfigurationStore::load(*settings_);
    refreshCalibrationProfileUi(conf.calibration_profile_id);
}

void PreviewPanel::saveCalibrationProfiles() {
    QString err;
    if (!gui::CalibrationProfileStore::save(*settings_, calibration_profiles_, &err)) {
        setStatus("Could not save calibrations: " + err);
        return;
    }
    gui::SessionConfiguration conf = gui::SessionConfigurationStore::load(*settings_);
    conf.calibration_profile_id = calibration_profile_combo_->currentData().toString();
    gui::SessionConfigurationStore::save(*settings_, conf);
}

void PreviewPanel::refreshCalibrationProfileUi(const QString& select_id) {
    if (!calibration_profile_combo_) return;
    QString desired = select_id.isEmpty() ? calibration_profile_combo_->currentData().toString() : select_id;
    {
        const QSignalBlocker blocker(calibration_profile_combo_);
        calibration_profile_combo_->clear();
        for (const gui::ManagedCalibrationProfile& p : calibration_profiles_) {
            if (!p.retired || p.id == desired) {
                calibration_profile_combo_->addItem(p.display_name + (p.retired ? " (retired)" : ""), p.id);
            }
        }
        int idx = calibration_profile_combo_->findData(desired);
        if (idx < 0 && calibration_profile_combo_->count() > 0) idx = 0;
        calibration_profile_combo_->setCurrentIndex(idx);
    }
    const gui::ManagedCalibrationProfile* p = selectedCalibrationProfile();
    if (!p) {
        refreshControlStates();
        return;
    }
    const QSignalBlocker b1(calibration_profile_name_edit_), b2(calibration_setup_id_edit_), b3(calibration_notes_edit_),
                         b4(gaze_frame_edit_), b5(target_frame_edit_), b6(stair_tx_spin_), b7(stair_ty_spin_),
                         b8(stair_tz_spin_), b9(stair_qx_spin_), b10(stair_qy_spin_), b11(stair_qz_spin_), b12(stair_qw_spin_);
    calibration_profile_name_edit_->setText(p->display_name);
    calibration_setup_id_edit_->setText(p->physical_setup_id);
    calibration_notes_edit_->setText(p->setup_notes);
    gaze_frame_edit_->setText(p->gaze_coordinate_frame);
    target_frame_edit_->setText(p->target_coordinate_frame);
    stair_tx_spin_->setValue(p->vicon_from_target.translation.x);
    stair_ty_spin_->setValue(p->vicon_from_target.translation.y);
    stair_tz_spin_->setValue(p->vicon_from_target.translation.z);
    stair_qx_spin_->setValue(p->vicon_from_target.rotation.x);
    stair_qy_spin_->setValue(p->vicon_from_target.rotation.y);
    stair_qz_spin_->setValue(p->vicon_from_target.rotation.z);
    stair_qw_spin_->setValue(p->vicon_from_target.rotation.w);
    // Selecting an entry only shows it, so while a calibration is in use the
    // quality text keeps describing that one.
    if (!calibrationInUse()) {
        calibration_quality_label_->setText(
            p->quality.sample_count > 0
                ? "Selected calibration: " + QString::number(p->quality.sample_count) +
                      " samples, " + errorText(p->quality) + " (not applied)"
                : "Selected calibration has no measured error values (not applied)");
    }
    refreshControlStates();
}

gui::ManagedCalibrationProfile* PreviewPanel::selectedCalibrationProfile() {
    return const_cast<gui::ManagedCalibrationProfile*>(
        std::as_const(*this).selectedCalibrationProfile());
}

const gui::ManagedCalibrationProfile* PreviewPanel::selectedCalibrationProfile() const {
    if (!calibration_profile_combo_) return nullptr;
    const QString id = calibration_profile_combo_->currentData().toString();
    for (const gui::ManagedCalibrationProfile& p : calibration_profiles_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

CalibrationProfile PreviewPanel::activeSolverProfile() const {
    CalibrationProfile res = defaultStairCalibrationProfile();
    if (const gui::ManagedCalibrationProfile* p = selectedCalibrationProfile()) res = p->solverProfile();
    return res;
}

void PreviewPanel::applySelectedCalibrationProfile() {
    gui::ManagedCalibrationProfile* profile = selectedCalibrationProfile();
    if (!profile) {
        setStatus("Select a saved calibration before applying one");
        return;
    }
    QString reason;
    if (!profile->complete(&reason)) {
        setStatus("Saved calibration is incomplete: " + reason);
        return;
    }
    if (!profile->hasGazeCalibration()) {
        setStatus(profile->display_name + " has no measured gaze calibration; select Calibrate to measure one");
        return;
    }
    bool metadata_matches = true;
    for (const gui::StreamIdentity& id : latest_stream_inventory_) {
        if (id.role == "gaze" && !id.coordinate_frame.isEmpty() && id.coordinate_frame != profile->gaze_coordinate_frame) metadata_matches = false;
        if (id.role == "calibration" && !id.coordinate_frame.isEmpty() && id.coordinate_frame != profile->target_coordinate_frame) metadata_matches = false;
    }
    if ((!calibration_metadata_compatible_ || !metadata_matches) && !profile->metadata_fallback_confirmed) {
        const auto ans = QMessageBox::warning(
            this, "Calibration coordinates do not match",
            "The live coordinate details are missing or differ from this saved calibration. Apply it for this session anyway?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes) {
            setStatus("Saved calibration was not applied because the coordinate details were not confirmed");
            return;
        }
        profile->metadata_fallback_confirmed = true;
        saveCalibrationProfiles();
    }
    automatic_gaze_transform_ = profile->gaze_transform;
    calibration_quality_ = profile->quality;
    calibration_state_ = gui::SessionCalibrationState::SavedProfile;
    calibration_source_id_ = profile->id;
    if (worker_) worker_->setGazeTransform(gazeTransform());
    reloadStairModel();
    widget_->requestViewRefit();
    updateCalibrationPersistentStatus(
        gui::SessionCalibrationState::SavedProfile,
        profile->quality.sample_count > 0
            ? "Quality: saved " + errorText(profile->quality)
            : "Quality: saved calibration has no measured error values",
        calibration_metadata_compatible_ && metadata_matches);
    refreshControlStates();
    setStatus("Applied saved calibration " + profile->display_name);
}

void PreviewPanel::saveSessionCalibrationProfile() {
    if (!calibrationInUse()) {
        setStatus("Complete or apply a calibration before saving it");
        return;
    }
    gui::ManagedCalibrationProfile* selected = selectedCalibrationProfile();
    const auto source = std::find_if(calibration_profiles_.cbegin(), calibration_profiles_.cend(),
        [this](const gui::ManagedCalibrationProfile& p) { return p.id == calibration_source_id_; });
    const QString display_name = calibration_profile_name_edit_->text().trimmed();
    // Only the saved calibration this one came from is updated; with another
    // one selected, saving adds a new entry instead of overwriting that one.
    const bool create_new = !selected || selected->id != calibration_source_id_ ||
                            selected->quality.sample_count == 0 || selected->retired;
    gui::ManagedCalibrationProfile profile = selected ? *selected : gui::CalibrationProfileStore::defaultProfile();
    if (create_new) {
        profile.id = gui::CalibrationProfileStore::newProfileId(display_name, calibration_profiles_);
        profile.created_at = QDateTime::currentDateTimeUtc();
        profile.retired = false;
    }
    profile.display_name = display_name;
    profile.physical_setup_id = calibration_setup_id_edit_->text().trimmed();
    profile.setup_notes = calibration_notes_edit_->text().trimmed();
    profile.stair_model_path = stair_model_edit_->text().trimmed();
    profile.stair_model_identity = gui::CalibrationProfileStore::stairModelIdentity(profile.stair_model_path);
    if (profile.stair_model_identity.isEmpty()) profile.stair_model_identity = QFileInfo(profile.stair_model_path).fileName();
    // The stair position the gaze calibration was measured against.
    profile.vicon_from_target = source != calibration_profiles_.cend()
        ? source->vicon_from_target
        : defaultStairCalibrationProfile().vicon_from_target;
    profile.gaze_transform = gazeTransform();
    profile.gaze_coordinate_frame = gaze_frame_edit_->text().trimmed();
    profile.target_coordinate_frame = target_frame_edit_->text().trimmed();
    profile.quality = calibration_quality_;
    profile.metadata_fallback_confirmed = !calibration_metadata_compatible_;
    QString reason;
    if (!profile.complete(&reason)) {
        setStatus("Cannot save calibration: " + reason);
        return;
    }
    if (create_new) calibration_profiles_.push_back(profile);
    else *selected = profile;
    saveCalibrationProfiles();
    refreshCalibrationProfileUi(profile.id);
    calibration_state_ = gui::SessionCalibrationState::SavedProfile;
    calibration_source_id_ = profile.id;
    updateCalibrationPersistentStatus(gui::SessionCalibrationState::SavedProfile, calibration_quality_label_->text(), calibration_metadata_compatible_);
    setStatus("Saved calibration " + profile.display_name);
}

void PreviewPanel::duplicateCalibrationProfile() {
    const gui::ManagedCalibrationProfile* selected = selectedCalibrationProfile();
    if (!selected) {
        setStatus("Select a saved calibration before copying one");
        return;
    }
    gui::ManagedCalibrationProfile duplicate = gui::CalibrationProfileStore::duplicate(*selected, calibration_profiles_);
    calibration_profiles_.push_back(duplicate);
    saveCalibrationProfiles();
    refreshCalibrationProfileUi(duplicate.id);
    setStatus("Copied calibration as " + duplicate.display_name);
}

void PreviewPanel::retireCalibrationProfile() {
    const gui::ManagedCalibrationProfile* selected = selectedCalibrationProfile();
    if (!selected) {
        setStatus("Select a saved calibration before hiding one");
        return;
    }
    const QString id = selected->id;
    const auto ans = QMessageBox::question(
        this, "Hide saved calibration",
        "Hide " + selected->display_name + " from the list? Past session records will keep it.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes) return;
    if (gui::CalibrationProfileStore::retire(calibration_profiles_, id)) {
        saveCalibrationProfiles();
        refreshCalibrationProfileUi();
        setStatus("Saved calibration hidden");
    }
}

void PreviewPanel::importCalibrationProfile() {
    const QString path = QFileDialog::getOpenFileName(this, "Import saved calibration", QString(), "Calibration files (*.json)");
    if (path.isEmpty()) return;
    gui::ManagedCalibrationProfile profile;
    QString err;
    if (!gui::CalibrationProfileStore::importProfile(path, profile, &err)) {
        setStatus("Could not import calibration: " + err);
        return;
    }
    if (std::any_of(calibration_profiles_.begin(), calibration_profiles_.end(), [&profile](const auto& ex) { return ex.id == profile.id; })) {
        profile.id = gui::CalibrationProfileStore::newProfileId(profile.display_name, calibration_profiles_);
    }
    calibration_profiles_.push_back(profile);
    saveCalibrationProfiles();
    refreshCalibrationProfileUi(profile.id);
    setStatus("Imported calibration " + profile.display_name);
}

void PreviewPanel::exportCalibrationProfile() {
    const gui::ManagedCalibrationProfile* profile = selectedCalibrationProfile();
    if (!profile) {
        setStatus("Select a saved calibration before exporting one");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Export saved calibration", profile->id + ".json", "Calibration files (*.json)");
    if (path.isEmpty()) return;
    QString err;
    if (!gui::CalibrationProfileStore::exportProfile(path, *profile, &err)) {
        setStatus("Could not export calibration: " + err);
        return;
    }
    setStatus("Exported calibration to " + QDir::toNativeSeparators(path));
}

void PreviewPanel::updateCalibrationPersistentStatus(gui::SessionCalibrationState state, const QString& text, bool metadata_compatible) {
    if (calibration_quality_label_) calibration_quality_label_->setText(text);
    emit calibrationStateChanged(state, text, metadata_compatible);
}

void PreviewPanel::loadSettings() {
    gui::SessionConfiguration conf = gui::SessionConfigurationStore::load(*settings_);
    applySessionConfiguration(conf);
    resetCalibrationSession();
    QString stair_model = conf.stair_model_path.trimmed();
    if (stair_model.isEmpty() || !QFileInfo::exists(stair_model)) stair_model = defaultStairModelPath();
    stair_model_edit_->setText(stair_model);
    const QStringList recent = gui::SessionConfigurationStore::loadUiState(*settings_).recent_recordings;
    recent_files_combo_->clear();
    for (const QString& p : recent) {
        if (!p.trimmed().isEmpty()) recent_files_combo_->addItem(QFileInfo(p).fileName(), p);
    }
}

void PreviewPanel::saveSettings() const {
    gui::SessionConfiguration conf = gui::SessionConfigurationStore::load(*settings_);
    updateSessionConfiguration(conf);
    gui::SessionConfigurationStore::save(*settings_, conf);
    gui::SessionUiState ui_state = gui::SessionConfigurationStore::loadUiState(*settings_);
    QStringList recent;
    for (int i = 0; i < recent_files_combo_->count(); ++i) recent.push_back(recent_files_combo_->itemData(i).toString());
    ui_state.recent_recordings = recent;
    gui::SessionConfigurationStore::saveUiState(*settings_, ui_state);
}

QString PreviewPanel::defaultStairModelPath() const {
    const QString app_dir = QCoreApplication::applicationDirPath();
    for (const QString& cand : {
        QDir(app_dir).filePath("stair_model/stair_model1.obj"),
        QDir(app_dir).filePath("../Resources/stair_model/stair_model1.obj"),
        QDir::current().filePath("stair_model/stair_model1.obj"),
        QDir::current().filePath("assets/stair_model/stair_model1.obj"),
        QDir::current().filePath("vicon-lsl-bridge/assets/stair_model/stair_model1.obj"),
    }) {
        if (QFileInfo::exists(cand)) return QDir::toNativeSeparators(cand);
    }
    return {};
}

void PreviewPanel::setStatus(const QString& status) {
    status_label_->setText(status);
}

void PreviewPanel::setFileState(const QString& text) {
    file_state_label_->setText(text);
}

} // namespace vicon_lsl
