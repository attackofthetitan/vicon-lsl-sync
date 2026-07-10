#include "gui/PreviewStreamWorker.h"

#include "preview/PreviewMath.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewParsing.h"

#include <lsl_cpp.h>

#include <QElapsedTimer>

#include <exception>
#include <utility>

namespace vicon_lsl {
namespace {

constexpr int kResolveRetryMs = 1000;
constexpr int kStatusIntervalMs = 1000;
constexpr int kFallbackEmitIntervalMs = 33;

std::vector<std::string> channelLabels(lsl::stream_info info) {
    std::vector<std::string> labels;
    labels.reserve(static_cast<std::size_t>(info.channel_count()));

    lsl::xml_element channel = info.desc().child("channels").child("channel");
    for (; !channel.empty(); channel = channel.next_sibling()) {
        const char* label = channel.child_value("label");
        labels.emplace_back(label && *label ? label : ("ch_" + std::to_string(labels.size())));
    }

    while (labels.size() < static_cast<std::size_t>(info.channel_count())) {
        labels.push_back("ch_" + std::to_string(labels.size()));
    }
    return labels;
}

} // namespace

struct PreviewStreamWorker::StreamState {
    QString requested_name;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    PreviewTransformProfile transform;
    std::unique_ptr<lsl::stream_inlet> inlet;
    std::vector<std::string> labels;
    std::vector<double> latest_sample;
    double latest_timestamp = 0.0;
    bool have_sample = false;
    qint64 next_resolve_ms = 0;
    QString last_error;

    bool connected() const { return inlet != nullptr; }
};

PreviewStreamWorker::PreviewStreamWorker(PreviewWorkerConfig config, QObject* parent)
    : QThread(parent),
      config_(std::move(config)),
      markers_(std::make_unique<StreamState>()),
      segments_(std::make_unique<StreamState>()),
      gaze_(std::make_unique<StreamState>()),
      calibration_target_(std::make_unique<StreamState>()) {
    config_.vicon_transform.name = "Vicon";
    config_.vicon_transform.scale = config_.vicon_transform.scale == 0.0
        ? 0.001
        : config_.vicon_transform.scale;
    config_.gaze_transform.name = "HoloLens";
    markers_->requested_name = config_.marker_stream_name;
    markers_->role = PreviewStreamRole::ViconMarkers;
    markers_->transform = config_.vicon_transform;
    segments_->requested_name = config_.segment_stream_name;
    segments_->role = PreviewStreamRole::ViconSegments;
    segments_->transform = config_.vicon_transform;
    gaze_->requested_name = config_.gaze_stream_name;
    gaze_->role = PreviewStreamRole::HoloLensGaze;
    gaze_->transform = config_.gaze_transform;
    calibration_target_->requested_name = config_.calibration_stream_name;
}

PreviewStreamWorker::~PreviewStreamWorker() {
    requestInterruption();
    wait(1000);
}

void PreviewStreamWorker::setGazeTransform(PreviewTransformProfile transform) {
    std::lock_guard<std::mutex> lock(gaze_transform_mutex_);
    transform.name = "HoloLens";
    transform.scale = 1.0;
    gaze_->transform = std::move(transform);
}

void PreviewStreamWorker::run() {
    QElapsedTimer timer;
    timer.start();
    last_status_ms_ = 0;
    last_fallback_emit_ms_ = 0;

    while (!isInterruptionRequested()) {
        const qint64 now = timer.elapsed();
        if (!markers_->connected() && now >= markers_->next_resolve_ms) {
            connectStream(*markers_);
            markers_->next_resolve_ms = now + kResolveRetryMs;
        }
        if (!segments_->connected() && now >= segments_->next_resolve_ms) {
            connectStream(*segments_);
            segments_->next_resolve_ms = now + kResolveRetryMs;
        }
        if (!gaze_->connected() && now >= gaze_->next_resolve_ms) {
            connectStream(*gaze_);
            gaze_->next_resolve_ms = now + kResolveRetryMs;
        }
        if (!calibration_target_->connected() && now >= calibration_target_->next_resolve_ms) {
            connectStream(*calibration_target_);
            calibration_target_->next_resolve_ms = now + kResolveRetryMs;
        }

        pollStream(*segments_);
        pollStream(*gaze_);
        if (pollStream(*calibration_target_)) {
            const auto pose = parseCalibrationTargetPose(calibration_target_->labels,
                                                         calibration_target_->latest_sample);
            if (pose) {
                emit targetPoseReady(*pose);
            }
        }
        if (pollStream(*markers_)) {
            emitFrameFromMarker();
        } else if (now - last_fallback_emit_ms_ >= kFallbackEmitIntervalMs) {
            emitFallbackFrame();
            last_fallback_emit_ms_ = now;
        }

        if (now - last_status_ms_ >= kStatusIntervalMs) {
            updateStatus();
            last_status_ms_ = now;
        }

        msleep(4);
    }
}

bool PreviewStreamWorker::connectStream(StreamState& state) {
    if (state.requested_name.trimmed().isEmpty()) {
        state.last_error = "No stream name configured";
        return false;
    }

    try {
        const auto streams = lsl::resolve_stream("name", state.requested_name.toStdString(), 1, 0.05);
        if (streams.empty()) {
            return false;
        }

        state.labels = channelLabels(streams.front());
        state.latest_sample.assign(static_cast<std::size_t>(streams.front().channel_count()), 0.0);
        state.inlet = std::make_unique<lsl::stream_inlet>(streams.front(), 360, 0, true);
        state.have_sample = false;
        state.last_error.clear();
        return true;
    } catch (const std::exception& ex) {
        state.inlet.reset();
        state.last_error = QString::fromStdString(ex.what());
        return false;
    }
}

bool PreviewStreamWorker::pollStream(StreamState& state) {
    if (!state.inlet) {
        return false;
    }

    bool updated = false;
    try {
        for (int pulls = 0; pulls < 16; ++pulls) {
            std::vector<double> sample(state.latest_sample.size());
            const double timestamp = state.inlet->pull_sample(sample, 0.0);
            if (timestamp <= 0.0) {
                break;
            }
            state.latest_sample = std::move(sample);
            state.latest_timestamp = timestamp;
            state.have_sample = true;
            updated = true;
        }
    } catch (const std::exception& ex) {
        state.last_error = QString::fromStdString(ex.what());
        state.inlet.reset();
        state.have_sample = false;
    }
    return updated;
}

void PreviewStreamWorker::emitFrameFromMarker() {
    PreviewFrame frame;
    frame.timestamp = markers_->latest_timestamp;
    frame.marker_stream_present = markers_->connected();
    frame.segment_stream_present = segments_->connected();
    frame.gaze_stream_present = gaze_->connected();
    frame.markers = parseMarkerSample(markers_->labels,
                                      markers_->latest_sample,
                                      markers_->transform);
    if (segments_->have_sample &&
        timestampWithinTolerance(frame.timestamp,
                                 segments_->latest_timestamp,
                                 config_.match_tolerance_seconds)) {
        frame.segments = parseSegmentSample(segments_->labels,
                                            segments_->latest_sample,
                                            segments_->transform);
    }
    if (gaze_->have_sample &&
        timestampWithinTolerance(frame.timestamp,
                                 gaze_->latest_timestamp,
                                 config_.match_tolerance_seconds)) {
        PreviewTransformProfile gaze_transform;
        {
            std::lock_guard<std::mutex> lock(gaze_transform_mutex_);
            gaze_transform = gaze_->transform;
        }
        frame.gaze_rays = parseGazeSample(gaze_->labels, gaze_->latest_sample, gaze_transform);
    }
    emit frameReady(std::move(frame));
}

void PreviewStreamWorker::emitFallbackFrame() {
    if (!segments_->have_sample && !gaze_->have_sample) {
        return;
    }

    PreviewFrame frame;
    frame.timestamp = segments_->have_sample ? segments_->latest_timestamp : gaze_->latest_timestamp;
    frame.marker_stream_present = markers_->connected();
    frame.segment_stream_present = segments_->connected();
    frame.gaze_stream_present = gaze_->connected();
    if (segments_->have_sample) {
        frame.segments = parseSegmentSample(segments_->labels,
                                            segments_->latest_sample,
                                            segments_->transform);
    }
    if (gaze_->have_sample) {
        PreviewTransformProfile gaze_transform;
        {
            std::lock_guard<std::mutex> lock(gaze_transform_mutex_);
            gaze_transform = gaze_->transform;
        }
        frame.gaze_rays = parseGazeSample(gaze_->labels, gaze_->latest_sample, gaze_transform);
    }
    emit frameReady(std::move(frame));
}

void PreviewStreamWorker::updateStatus() {
    QString status = streamStatusText(*markers_) + "; " +
                     streamStatusText(*segments_) + "; " +
                     streamStatusText(*gaze_) + "; " +
                     streamStatusText(*calibration_target_);
    if (!markers_->last_error.isEmpty()) {
        status += "; markers error: " + markers_->last_error;
    }
    if (!segments_->last_error.isEmpty()) {
        status += "; segments error: " + segments_->last_error;
    }
    if (!gaze_->last_error.isEmpty()) {
        status += "; gaze error: " + gaze_->last_error;
    }
    emit statusChanged(status);
}

QString PreviewStreamWorker::streamStatusText(const StreamState& state) const {
    if (!state.connected()) {
        return state.requested_name + ": resolving";
    }
    if (!state.have_sample) {
        return state.requested_name + ": connected";
    }
    return state.requested_name + ": " + QString::number(state.latest_sample.size()) + "ch";
}

} // namespace vicon_lsl
