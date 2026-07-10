#pragma once

#include "preview/PreviewCalibration.h"
#include "preview/PreviewTypes.h"
#include "StreamDefaults.h"

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
};

class PreviewStreamWorker : public QThread {
    Q_OBJECT

public:
    explicit PreviewStreamWorker(PreviewWorkerConfig config, QObject* parent = nullptr);
    ~PreviewStreamWorker() override;
    void setGazeTransform(PreviewTransformProfile transform);

signals:
    void frameReady(vicon_lsl::PreviewFrame frame);
    void targetPoseReady(vicon_lsl::CalibrationTargetPose pose);
    void statusChanged(QString status);

protected:
    void run() override;

private:
    struct StreamState;

    bool connectStream(StreamState& state);
    bool pollStream(StreamState& state, qint64 now_ms);
    bool streamIsFresh(const StreamState& state, qint64 now_ms) const;
    void emitFrameFromMarker(qint64 now_ms);
    void emitFallbackFrame(qint64 now_ms,
                           bool segments_updated,
                           bool gaze_updated);
    void updateStatus(qint64 now_ms);
    QString streamStatusText(const StreamState& state, qint64 now_ms) const;

    PreviewWorkerConfig config_;
    std::unique_ptr<StreamState> markers_;
    std::unique_ptr<StreamState> segments_;
    std::unique_ptr<StreamState> gaze_;
    std::unique_ptr<StreamState> calibration_target_;
    mutable std::mutex gaze_transform_mutex_;
    qint64 last_status_ms_ = 0;
};

} // namespace vicon_lsl

Q_DECLARE_METATYPE(vicon_lsl::PreviewFrame)
Q_DECLARE_METATYPE(vicon_lsl::CalibrationTargetPose)
