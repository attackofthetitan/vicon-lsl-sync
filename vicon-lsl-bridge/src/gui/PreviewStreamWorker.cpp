#include "gui/PreviewStreamWorker.h"

#include "preview/PreviewMath.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewParsing.h"

#include <lsl_cpp.h>

#include <QElapsedTimer>

#include <algorithm>
#include <exception>
#include <utility>

namespace vicon_lsl {
namespace {

constexpr int kResolveRetryMs = 1000;
constexpr int kStatusIntervalMs = 1000;
constexpr int kStaleSampleMs = 500;

std::vector<std::string> channelLabels(lsl::stream_info info,
                                       PreviewStreamRole role) {
    std::vector<std::string> labels;
    labels.reserve(static_cast<std::size_t>(info.channel_count()));
    bool metadata_complete = true;

    lsl::xml_element channel = info.desc().child("channels").child("channel");
    for (; !channel.empty(); channel = channel.next_sibling()) {
        const char* label = channel.child_value("label");
        if (!label || !*label) {
            metadata_complete = false;
            labels.push_back("ch_" + std::to_string(labels.size()));
        } else {
            labels.emplace_back(label);
        }
    }

    const std::size_t channel_count = static_cast<std::size_t>(info.channel_count());
    if (metadata_complete && labels.size() == channel_count) {
        return labels;
    }
    auto canonical = canonicalPreviewChannelLabels(role, channel_count);
    if (!canonical.empty()) {
        return canonical;
    }

    while (labels.size() < channel_count) {
        labels.push_back("ch_" + std::to_string(labels.size()));
    }
    labels.resize(channel_count);
    return labels;
}

} // namespace

struct PreviewStreamWorker::StreamState {
    QString requested_name;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    PreviewTransformProfile transform;
    std::unique_ptr<lsl::stream_inlet> inlet;
    std::vector<std::string> labels;
    std::string coordinate_frame;
    std::vector<double> latest_sample;
    double latest_timestamp = 0.0;
    bool have_sample = false;
    qint64 last_sample_ms = -1;
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
    calibration_target_->role = PreviewStreamRole::HoloLensCalibrationTarget;
}

PreviewStreamWorker::~PreviewStreamWorker() {
    requestInterruption();
    wait();
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

        const bool segments_updated = pollStream(*segments_, now);
        const bool gaze_updated = pollStream(*gaze_, now);
        if (pollStream(*calibration_target_, now)) {
            const auto pose = parseCalibrationTargetPose(calibration_target_->labels,
                                                         calibration_target_->latest_sample);
            if (pose && calibrationFramesCompatible()) {
                emit targetPoseReady(*pose);
            }
        }
        if (pollStream(*markers_, now)) {
            emitFrameFromMarker(now);
        } else if (segments_updated || gaze_updated) {
            emitFallbackFrame(now, segments_updated, gaze_updated);
        }

        if (now - last_status_ms_ >= kStatusIntervalMs) {
            updateStatus(now);
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
        auto streams = lsl::resolve_stream("name", state.requested_name.toStdString(), 1, 0.05);
        if (streams.empty()) {
            return false;
        }

        auto inlet = std::make_unique<lsl::stream_inlet>(streams.front(), 360, 0, true);
        lsl::stream_info metadata = streams.front();
        try {
            // Resolver results may contain only the short stream description.
            // The inlet supplies the full channel and coordinate metadata.
            metadata = inlet->info(1.0);
        } catch (const std::exception&) {
            // Known fixed HoloLens schemas still have a safe label fallback.
        }
        state.labels = channelLabels(metadata, state.role);
        const char* coordinate_frame = metadata
            .desc()
            .child("acquisition")
            .child_value("coordinate_frame");
        state.coordinate_frame = coordinate_frame ? coordinate_frame : "";
        state.latest_sample.assign(static_cast<std::size_t>(metadata.channel_count()), 0.0);
        state.inlet = std::move(inlet);
        // Live preview consumes timestamps in the local recorder clock. Keep
        // clock synchronization scoped to these inlets; XDF playback applies
        // its recorded offsets independently when loading the file.
        state.inlet->set_postprocessing(lsl::post_clocksync);
        state.have_sample = false;
        state.last_sample_ms = -1;
        state.last_error.clear();
        return true;
    } catch (const std::exception& ex) {
        state.inlet.reset();
        state.last_error = QString::fromStdString(ex.what());
        return false;
    }
}

bool PreviewStreamWorker::pollStream(StreamState& state, qint64 now_ms) {
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
            state.last_sample_ms = now_ms;
            updated = true;
        }
    } catch (const std::exception& ex) {
        state.last_error = QString::fromStdString(ex.what());
        state.inlet.reset();
        state.have_sample = false;
        state.last_sample_ms = -1;
    }
    return updated;
}

bool PreviewStreamWorker::streamIsFresh(const StreamState& state, qint64 now_ms) const {
    return state.have_sample && state.last_sample_ms >= 0 &&
           now_ms - state.last_sample_ms <= kStaleSampleMs;
}

bool PreviewStreamWorker::calibrationFramesCompatible() const {
    return calibrationCoordinateFramesCompatible(gaze_->coordinate_frame,
                                                  calibration_target_->coordinate_frame);
}

void PreviewStreamWorker::emitFrameFromMarker(qint64 now_ms) {
    PreviewFrame frame;
    frame.timestamp = markers_->latest_timestamp;
    frame.marker_stream_present = markers_->connected();
    frame.segment_stream_present = segments_->connected();
    frame.gaze_stream_present = gaze_->connected();
    frame.markers = parseMarkerSample(markers_->labels,
                                      markers_->latest_sample,
                                      markers_->transform);
    if (streamIsFresh(*segments_, now_ms) &&
        timestampWithinTolerance(frame.timestamp,
                                 segments_->latest_timestamp,
                                 config_.match_tolerance_seconds)) {
        frame.segments = parseSegmentSample(segments_->labels,
                                            segments_->latest_sample,
                                            segments_->transform);
    }
    if (streamIsFresh(*gaze_, now_ms) &&
        timestampWithinTolerance(frame.timestamp,
                                 gaze_->latest_timestamp,
                                 config_.match_tolerance_seconds)) {
        PreviewTransformProfile gaze_transform;
        {
            std::lock_guard<std::mutex> lock(gaze_transform_mutex_);
            gaze_transform = gazeTransformForCoordinateFrame(
                gaze_->transform,
                gaze_->coordinate_frame);
        }
        frame.gaze_rays = parseGazeSample(gaze_->labels, gaze_->latest_sample, gaze_transform);
    }
    emit frameReady(std::move(frame));
}

void PreviewStreamWorker::emitFallbackFrame(qint64 now_ms,
                                            bool segments_updated,
                                            bool gaze_updated) {
    const bool segments_fresh = streamIsFresh(*segments_, now_ms);
    const bool gaze_fresh = streamIsFresh(*gaze_, now_ms);
    if ((!segments_updated || !segments_fresh) && (!gaze_updated || !gaze_fresh)) {
        return;
    }

    PreviewFrame frame;
    if (segments_updated && segments_fresh && gaze_updated && gaze_fresh) {
        frame.timestamp = (std::max)(segments_->latest_timestamp, gaze_->latest_timestamp);
    } else if (segments_updated && segments_fresh) {
        frame.timestamp = segments_->latest_timestamp;
    } else {
        frame.timestamp = gaze_->latest_timestamp;
    }
    frame.marker_stream_present = markers_->connected();
    frame.segment_stream_present = segments_->connected();
    frame.gaze_stream_present = gaze_->connected();
    if (segments_fresh &&
        timestampWithinTolerance(frame.timestamp,
                                 segments_->latest_timestamp,
                                 config_.match_tolerance_seconds)) {
        frame.segments = parseSegmentSample(segments_->labels,
                                            segments_->latest_sample,
                                            segments_->transform);
    }
    if (gaze_fresh &&
        timestampWithinTolerance(frame.timestamp,
                                 gaze_->latest_timestamp,
                                 config_.match_tolerance_seconds)) {
        PreviewTransformProfile gaze_transform;
        {
            std::lock_guard<std::mutex> lock(gaze_transform_mutex_);
            gaze_transform = gazeTransformForCoordinateFrame(
                gaze_->transform,
                gaze_->coordinate_frame);
        }
        frame.gaze_rays = parseGazeSample(gaze_->labels, gaze_->latest_sample, gaze_transform);
    }
    emit frameReady(std::move(frame));
}

void PreviewStreamWorker::updateStatus(qint64 now_ms) {
    QString status = streamStatusText(*markers_, now_ms) + "; " +
                     streamStatusText(*segments_, now_ms) + "; " +
                     streamStatusText(*gaze_, now_ms) + "; " +
                     streamStatusText(*calibration_target_, now_ms);
    if (!markers_->last_error.isEmpty()) {
        status += "; markers error: " + markers_->last_error;
    }
    if (!segments_->last_error.isEmpty()) {
        status += "; segments error: " + segments_->last_error;
    }
    if (!gaze_->last_error.isEmpty()) {
        status += "; gaze error: " + gaze_->last_error;
    }
    if (!calibration_target_->last_error.isEmpty()) {
        status += "; calibration error: " + calibration_target_->last_error;
    }
    if (gaze_->connected() && calibration_target_->connected() &&
        !calibrationFramesCompatible()) {
        status += "; calibration unavailable: gaze frame " +
                  QString::fromStdString(gaze_->coordinate_frame) +
                  " differs from target frame " +
                  QString::fromStdString(calibration_target_->coordinate_frame);
    }
    emit statusChanged(status);
}

QString PreviewStreamWorker::streamStatusText(const StreamState& state, qint64 now_ms) const {
    if (!state.connected()) {
        return state.requested_name + ": resolving";
    }
    if (!state.have_sample) {
        return state.requested_name + ": connected";
    }
    if (!streamIsFresh(state, now_ms)) {
        return state.requested_name + ": stale (" +
               QString::number(static_cast<double>(now_ms - state.last_sample_ms) / 1000.0,
                               'f', 1) +
               "s)";
    }
    return state.requested_name + ": " + QString::number(state.latest_sample.size()) + "ch";
}

} // namespace vicon_lsl
