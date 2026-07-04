#pragma once

#include "preview/PreviewTypes.h"

#include <QThread>
#include <QMetaType>
#include <QString>

#include <memory>
#include <string>
#include <vector>

namespace lsl {
class stream_inlet;
class stream_info;
}

namespace vicon_lsl {

struct PreviewWorkerConfig {
    QString marker_stream_name = "ViconMarkers";
    QString segment_stream_name = "ViconSegments";
    QString gaze_stream_name = "HoloLensGaze";
    double match_tolerance_seconds = 0.05;
    PreviewTransformProfile vicon_transform;
    PreviewTransformProfile gaze_transform;
};

class PreviewStreamWorker : public QThread {
    Q_OBJECT

public:
    explicit PreviewStreamWorker(PreviewWorkerConfig config, QObject* parent = nullptr);
    ~PreviewStreamWorker() override;

signals:
    void frameReady(vicon_lsl::PreviewFrame frame);
    void statusChanged(QString status);

protected:
    void run() override;

private:
    struct StreamState;

    bool connectStream(StreamState& state);
    bool pollStream(StreamState& state);
    void emitFrameFromMarker();
    void emitFallbackFrame();
    void updateStatus();
    QString streamStatusText(const StreamState& state) const;

    PreviewWorkerConfig config_;
    std::unique_ptr<StreamState> markers_;
    std::unique_ptr<StreamState> segments_;
    std::unique_ptr<StreamState> gaze_;
    qint64 last_status_ms_ = 0;
    qint64 last_fallback_emit_ms_ = 0;
};

} // namespace vicon_lsl

Q_DECLARE_METATYPE(vicon_lsl::PreviewFrame)
