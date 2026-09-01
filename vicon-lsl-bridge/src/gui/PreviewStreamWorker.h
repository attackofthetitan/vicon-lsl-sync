#pragma once

#include "preview/PreviewCalibration.h"
#include "preview/PreviewDeliveryMailbox.h"
#include "preview/PreviewRate.h"
#include "preview/PreviewTypes.h"
#include "StreamDefaults.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"

#include <QThread>
#include <QMetaType>
#include <QString>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lsl {
class stream_inlet;
class stream_info;
}

namespace vicon_lsl {

struct PreviewWorkerConfig {
    QString marker_stream_name = vicon_lsl::stream_defaults::ViconMarkers;
    QString segment_stream_name = vicon_lsl::stream_defaults::ViconSegments;
    QString gaze_stream_name = vicon_lsl::stream_defaults::HoloLensGaze;
    QString calibration_stream_name = vicon_lsl::stream_defaults::HoloLensModelTargetPose;
    double match_tolerance_seconds = 0.05;
    PreviewTransformProfile vicon_transform;
    PreviewTransformProfile gaze_transform;
    QString marker_source_id;
    QString segment_source_id;
    QString gaze_source_id;
    QString calibration_source_id;
    bool marker_follow_by_name = false;
    bool segment_follow_by_name = false;
    bool gaze_follow_by_name = false;
    bool calibration_follow_by_name = false;
};

class PreviewStreamWorker : public QThread {
    Q_OBJECT

public:
    explicit PreviewStreamWorker(PreviewWorkerConfig config, QObject* parent = nullptr);
    ~PreviewStreamWorker() override;
    void setGazeTransform(PreviewTransformProfile transform);
    bool takeLatestFrame(PreviewFrame& frame, PreviewDeliveryMetrics& metrics);
    PreviewDeliveryMetrics deliveryMetrics() const;
    QVector<vicon_lsl::gui::StreamIdentity> streamInventory() const;

signals:
    void targetPoseReady(vicon_lsl::CalibrationTargetPose pose);
    void statusChanged(QString status);
    void lifecycleChanged(ComponentLifecycleState state, QString detail);
    void streamIdentityChanged(vicon_lsl::gui::StreamIdentity identity, QString warning);

protected:
    void run() override;

private:
    struct StreamState;

    bool connectStream(StreamState& state);
    bool pollStream(StreamState& state, qint64 now_ms);
    bool streamIsFresh(const StreamState& state, qint64 now_ms) const;
    bool calibrationFramesCompatible() const;
    PreviewTransformProfile currentGazeTransform() const;
    void publishLatestFrame(PreviewFrame frame);
    void updateStatus(qint64 now_ms);
    QString streamStatusText(const StreamState& state, qint64 now_ms) const;

    PreviewWorkerConfig config_;
    std::unique_ptr<StreamState> markers_;
    std::unique_ptr<StreamState> segments_;
    std::unique_ptr<StreamState> gaze_;
    std::unique_ptr<StreamState> calibration_target_;
    mutable std::mutex gaze_transform_mutex_;
    mutable std::mutex inventory_mutex_;
    PreviewDeliveryMailbox delivery_mailbox_;
    QVector<vicon_lsl::gui::StreamIdentity> inventory_;
    qint64 last_status_ms_ = 0;
};

} // namespace vicon_lsl

Q_DECLARE_METATYPE(vicon_lsl::PreviewFrame)
Q_DECLARE_METATYPE(vicon_lsl::CalibrationTargetPose)
Q_DECLARE_METATYPE(vicon_lsl::PreviewDeliveryMetrics)
