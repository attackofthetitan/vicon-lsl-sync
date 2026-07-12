#pragma once

#include "gui/PreviewStreamWorker.h"
#include "gui/PreviewWidget.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewPlaybackClock.h"

#include <QElapsedTimer>
#include <QWidget>

#include <vector>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace vicon_lsl {

class PreviewPanel : public QWidget {
    Q_OBJECT

public:
    explicit PreviewPanel(QWidget* parent = nullptr);
    ~PreviewPanel() override;

    bool stairModelLoaded() const { return stair_model_loaded_; }
    bool configurableTooltipsPresent() const;

private slots:
    void startPreview();
    void stopPreview();
    void browseStairModel();
    void reloadStairModel();
    void openMergedCsv();
    void openXdf();
    void toggleCsvPlayback();
    void advanceCsvPlayback();
    void beginCalibration();
    void useManualTransform();
    void handleTargetPose(vicon_lsl::CalibrationTargetPose pose);

private:
    enum class WorkerState {
        Idle,
        Running,
        Stopping,
    };

    enum class PendingRecordingOpen {
        None,
        Csv,
        Xdf,
    };

    PreviewTransformProfile manualGazeTransform() const;
    PreviewTransformProfile gazeTransform() const;
    PreviewTransformProfile stairTransform() const;
    void resetCalibrationSession();
    void loadMergedCsv(const QString& path);
    void loadXdf(const QString& path);
    void processPendingRecordingOpen();
    void loadSettings();
    void saveSettings() const;
    QString defaultStairModelPath() const;
    void setStatus(const QString& status);

    PreviewWidget* widget_ = nullptr;
    QLineEdit* marker_stream_edit_ = nullptr;
    QLineEdit* segment_stream_edit_ = nullptr;
    QLineEdit* gaze_stream_edit_ = nullptr;
    QLineEdit* calibration_stream_edit_ = nullptr;
    QLineEdit* stair_model_edit_ = nullptr;
    QDoubleSpinBox* tolerance_spin_ = nullptr;
    QDoubleSpinBox* playback_speed_spin_ = nullptr;
    QDoubleSpinBox* gaze_tx_spin_ = nullptr;
    QDoubleSpinBox* gaze_ty_spin_ = nullptr;
    QDoubleSpinBox* gaze_tz_spin_ = nullptr;
    QDoubleSpinBox* gaze_rx_spin_ = nullptr;
    QDoubleSpinBox* gaze_ry_spin_ = nullptr;
    QDoubleSpinBox* gaze_rz_spin_ = nullptr;
    QSpinBox* trail_points_spin_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* open_csv_button_ = nullptr;
    QPushButton* open_xdf_button_ = nullptr;
    QPushButton* play_csv_button_ = nullptr;
    QPushButton* calibrate_button_ = nullptr;
    QPushButton* use_manual_transform_button_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* csv_timer_ = nullptr;
    std::vector<PreviewFrame> csv_frames_;
    QElapsedTimer playback_elapsed_;
    PreviewPlaybackClock playback_clock_;
    PreviewStreamWorker* worker_ = nullptr;
    WorkerState worker_state_ = WorkerState::Idle;
    PendingRecordingOpen pending_recording_open_ = PendingRecordingOpen::None;
    QString pending_recording_path_;
    bool stair_model_loaded_ = false;
    CalibrationState calibration_state_ = CalibrationState::Manual;
    PreviewRigidTransform automatic_gaze_transform_;
    std::vector<CalibrationTargetPose> calibration_samples_;
};

} // namespace vicon_lsl
