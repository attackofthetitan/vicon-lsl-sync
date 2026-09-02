#pragma once

#include "gui/PreviewStreamWorker.h"
#include "gui/PreviewWidget.h"
#include "gui/PreviewFileLoader.h"
#include "gui/CalibrationProfileStore.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewPlaybackClock.h"

#include <QElapsedTimer>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QProgressBar;
class QSettings;
class QSlider;
class QCheckBox;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QResizeEvent;
class QScrollArea;

namespace vicon_lsl {

class PreviewPanel : public QWidget {
    Q_OBJECT

public:
    PreviewPanel(QWidget* parent, std::shared_ptr<QSettings> settings);
    ~PreviewPanel() override;

    bool stairModelLoaded() const { return stair_model_loaded_; }
    ComponentLifecycleState lifecycleState() const { return lifecycle_state_; }
    QVector<vicon_lsl::gui::StreamIdentity> streamInventory() const;
    void applySessionConfiguration(const vicon_lsl::gui::SessionConfiguration& configuration);
    void updateSessionConfiguration(vicon_lsl::gui::SessionConfiguration& configuration) const;
    void requestShutdown();
    bool shutdownReady() const;
    bool fileLoadActive() const { return file_loader_ != nullptr; }
    vicon_lsl::gui::SessionCalibrationState sessionCalibrationState() const {
        return calibration_state_;
    }
    void openRecording(const QString& path);

public slots:
    void startPreview();
    void stopPreview();
    void fitView();
    void resetCamera();
    void exportPreviewImage();

signals:
    void lifecycleChanged(ComponentLifecycleState state, QString detail);
    void streamInventoryChanged(QVector<vicon_lsl::gui::StreamIdentity> streams);
    void calibrationStateChanged(vicon_lsl::gui::SessionCalibrationState state,
                                 QString quality,
                                 bool metadata_compatible);
    void fileStateChanged(vicon_lsl::gui::SessionFileState state, QString detail);
    void deliveryMetricsChanged(vicon_lsl::PreviewDeliveryMetrics metrics);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void displayLatestLiveFrame();
    void browseStairModel();
    void reloadStairModel();
    void openMergedCsv();
    void openXdf();
    void toggleCsvPlayback();
    void advanceCsvPlayback();
    void cancelFileLoad();
    void seekPlaybackFromSlider(int value);
    void openRecentRecording();
    void beginCalibration();
    void clearCalibration();
    void handleTargetPose(vicon_lsl::CalibrationTargetPose pose);
    void applySelectedCalibrationProfile();
    void saveSessionCalibrationProfile();
    void duplicateCalibrationProfile();
    void retireCalibrationProfile();
    void importCalibrationProfile();
    void exportCalibrationProfile();
    void updateMeasuredStairPose();

private:
    PreviewTransformProfile gazeTransform() const;
    PreviewTransformProfile stairTransform() const;
    void resetCalibrationSession();
    void openRecording(PreviewFileType type,
                       const QString& title,
                       const QString& filter);
    void startFileLoad(PreviewFileType type, const QString& path);
    void applyLoadedRecording(PreviewFileLoader* loader, const QString& summary);
    void requestRecordedStreamMapping(PreviewFileLoader* loader,
                                      const XdfMappingAnalysis& analysis);
    void seekToFrame(std::size_t frame_index);
    void seekBySeconds(double seconds);
    void updatePlaybackDisplay();
    void rememberRecentFile(const QString& path);
    void processPendingRecordingOpen();
    void loadSettings();
    void saveSettings() const;
    QString defaultStairModelPath() const;
    void setStatus(const QString& status);
    void loadCalibrationProfiles();
    void saveCalibrationProfiles();
    void refreshCalibrationProfileUi(const QString& select_id = {});
    gui::ManagedCalibrationProfile* selectedCalibrationProfile();
    const gui::ManagedCalibrationProfile* selectedCalibrationProfile() const;
    CalibrationProfile activeSolverProfile() const;
    void updateCalibrationPersistentStatus(gui::SessionCalibrationState state,
                                           const QString& text,
                                           bool metadata_compatible);
    // Keeps every control that only works in some states enabled exactly when it
    // does work, so no button is live while its action would do nothing.
    void refreshControlStates();

    PreviewWidget* widget_ = nullptr;
    QScrollArea* controls_scroll_ = nullptr;
    QLineEdit* marker_stream_edit_ = nullptr;
    QLineEdit* segment_stream_edit_ = nullptr;
    QLineEdit* gaze_stream_edit_ = nullptr;
    QLineEdit* calibration_stream_edit_ = nullptr;
    QLineEdit* stair_model_edit_ = nullptr;
    QDoubleSpinBox* tolerance_spin_ = nullptr;
    QSpinBox* cache_megabytes_spin_ = nullptr;
    QDoubleSpinBox* playback_speed_spin_ = nullptr;
    QSpinBox* trail_points_spin_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* open_csv_button_ = nullptr;
    QPushButton* open_xdf_button_ = nullptr;
    QPushButton* play_csv_button_ = nullptr;
    QPushButton* calibrate_button_ = nullptr;
    QPushButton* clear_calibration_button_ = nullptr;
    QPushButton* save_calibration_button_ = nullptr;
    QPushButton* open_recent_button_ = nullptr;
    // Controls that need a selected saved calibration, and controls that need a
    // loaded recording.
    QVector<QWidget*> profile_selection_controls_;
    QVector<QWidget*> playback_controls_;
    QComboBox* calibration_profile_combo_ = nullptr;
    QLineEdit* calibration_profile_name_edit_ = nullptr;
    QLineEdit* calibration_setup_id_edit_ = nullptr;
    QLineEdit* calibration_notes_edit_ = nullptr;
    QLineEdit* gaze_frame_edit_ = nullptr;
    QLineEdit* target_frame_edit_ = nullptr;
    QDoubleSpinBox* stair_tx_spin_ = nullptr;
    QDoubleSpinBox* stair_ty_spin_ = nullptr;
    QDoubleSpinBox* stair_tz_spin_ = nullptr;
    QDoubleSpinBox* stair_qx_spin_ = nullptr;
    QDoubleSpinBox* stair_qy_spin_ = nullptr;
    QDoubleSpinBox* stair_qz_spin_ = nullptr;
    QDoubleSpinBox* stair_qw_spin_ = nullptr;
    QLabel* calibration_quality_label_ = nullptr;
    QLabel* calibration_metadata_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* delivery_metrics_label_ = nullptr;
    QLabel* playback_position_label_ = nullptr;
    QLabel* file_state_label_ = nullptr;
    QLabel* memory_label_ = nullptr;
    QProgressBar* load_progress_ = nullptr;
    QSlider* timeline_slider_ = nullptr;
    QCheckBox* loop_playback_check_ = nullptr;
    QDoubleSpinBox* jump_seconds_spin_ = nullptr;
    QComboBox* recent_files_combo_ = nullptr;
    QPushButton* cancel_load_button_ = nullptr;
    QTimer* csv_timer_ = nullptr;
    QTimer* live_render_timer_ = nullptr;
    std::vector<PreviewFrame> csv_frames_;
    QElapsedTimer playback_elapsed_;
    PreviewPlaybackClock playback_clock_;
    PreviewStreamWorker* worker_ = nullptr;
    PreviewFileLoader* file_loader_ = nullptr;
    bool worker_stopping_ = false;
    std::optional<PreviewFileType> pending_recording_type_;
    QString pending_recording_path_;
    bool stair_model_loaded_ = false;
    ComponentLifecycleState lifecycle_state_ = ComponentLifecycleState::Idle;
    gui::SessionCalibrationState calibration_state_ =
        gui::SessionCalibrationState::Uncalibrated;
    PreviewTransformProfile automatic_gaze_transform_;
    std::vector<CalibrationTargetPose> calibration_samples_;
    gui::StreamBinding marker_binding_;
    gui::StreamBinding segment_binding_;
    gui::StreamBinding gaze_binding_;
    gui::StreamBinding calibration_binding_;
    QVector<gui::StreamIdentity> latest_stream_inventory_;
    QVector<gui::ManagedCalibrationProfile> calibration_profiles_;
    CalibrationQuality calibration_quality_;
    QString calibration_rejection_reason_;
    bool calibration_metadata_compatible_ = true;
    QElapsedTimer calibration_progress_throttle_;
    PreviewDeliveryMetrics last_delivery_metrics_;
    std::shared_ptr<QSettings> settings_;
};

} // namespace vicon_lsl
