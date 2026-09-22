#pragma once

#include "preview/PreviewCalibration.h"
#include "preview/PreviewDeliveryMailbox.h"
#include "preview/PreviewRate.h"
#include "preview/PreviewTypes.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"

#include <QThread>
#include <QMetaType>
#include <QString>

#include <memory>
#include <mutex>

namespace lsl {
class stream_inlet;
class stream_info;
} // namespace lsl

namespace vicon_lsl {

// Which streams to open. Each binding's name, source ID, and reconnection mode
// are used.
struct PreviewWorkerConfig {
    gui::StreamBinding markers;
    gui::StreamBinding segments;
    gui::StreamBinding gaze;
    gui::StreamBinding calibration;
    double match_tolerance_seconds = 0.05;
    PreviewTransformProfile vicon_transform;
    PreviewTransformProfile gaze_transform;
};

// Reads the four live preview streams on its own thread. Only the newest frame
// waits for display; stream rates and calibration poses are tracked separately.
class PreviewStreamWorker : public QThread {
    Q_OBJECT

public:
    explicit PreviewStreamWorker(PreviewWorkerConfig config, QObject* parent = nullptr);
    ~PreviewStreamWorker() override;
    void setGazeTransform(PreviewTransformProfile transform);
    bool takeLatestFrame(PreviewFrame& frame, PreviewDeliveryMetrics& metrics);
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
    bool openStream(StreamState& state, const lsl::stream_info& stream, QString warning);
    bool pollStream(StreamState& state, qint64 now_ms);
    bool streamIsFresh(const StreamState& state, qint64 now_ms) const;
    bool calibrationFramesCompatible() const;
    PreviewTransformProfile currentGazeTransform() const;
    void updateStatus(qint64 now_ms);
    void replaceInventory(PreviewStreamRole role, QVector<gui::StreamIdentity> streams,
                          const QString& warning);
    QString streamStatusText(const StreamState& state, qint64 now_ms) const;

    double match_tolerance_seconds_;
    std::unique_ptr<StreamState> markers_;
    std::unique_ptr<StreamState> segments_;
    std::unique_ptr<StreamState> gaze_;
    std::unique_ptr<StreamState> calibration_target_;
    mutable std::mutex gaze_transform_mutex_;
    mutable std::mutex inventory_mutex_;
    PreviewDeliveryMailbox delivery_mailbox_;
    QVector<vicon_lsl::gui::StreamIdentity> inventory_;
};

} // namespace vicon_lsl

Q_DECLARE_METATYPE(vicon_lsl::PreviewFrame)
Q_DECLARE_METATYPE(vicon_lsl::CalibrationTargetPose)
Q_DECLARE_METATYPE(vicon_lsl::PreviewDeliveryMetrics)
