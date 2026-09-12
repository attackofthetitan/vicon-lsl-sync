#include "gui/PreviewStreamWorker.h"

#include "StreamDefaults.h"
#include "gui/LslStreamIdentity.h"
#include "gui/SessionConfiguration.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewFrameAssembler.h"
#include "preview/PreviewParsing.h"

#include <lsl_cpp.h>

#include <QDateTime>
#include <QElapsedTimer>
#include <QStringList>
#include <tuple>

namespace vicon_lsl {
namespace {

constexpr int kResolveRetryMs = 1000;
constexpr int kStatusIntervalMs = 1000;
constexpr int kStaleSampleMs = 500;
constexpr double kGazeLowRateFraction = 0.8;
constexpr double kMetadataTimeoutSeconds = 0.25;
constexpr double kResolveTimeoutSeconds = 0.05;

std::vector<std::string> channelLabels(lsl::stream_info& info, PreviewStreamRole role, bool* complete) {
    std::vector<std::string> labels;
    labels.reserve(static_cast<std::size_t>(info.channel_count()));
    bool metadata_complete = true;
    for (lsl::xml_element ch = info.desc().child("channels").child("channel"); !ch.empty(); ch = ch.next_sibling()) {
        const char* val = ch.child_value("label");
        if (val && *val) {
            labels.emplace_back(val);
        } else {
            metadata_complete = false;
            labels.push_back("ch_" + std::to_string(labels.size()));
        }
    }
    const std::size_t channel_count = static_cast<std::size_t>(info.channel_count());
    if (metadata_complete && labels.size() == channel_count) {
        if (complete) *complete = true;
        return labels;
    }

    auto canonical = canonicalPreviewChannelLabels(role, channel_count);
    if (!canonical.empty()) {
        if (complete) *complete = false;
        return canonical;
    }
    while (labels.size() < channel_count) labels.push_back("ch_" + std::to_string(labels.size()));
    labels.resize(channel_count);
    if (complete) *complete = false;
    return labels;
}

// msecsSinceReference() returns the timer's start time, so start a fresh timer.
qint64 steadyNowMs() {
    QElapsedTimer timer;
    timer.start();
    return timer.msecsSinceReference();
}

QString roleText(PreviewStreamRole role) {
    switch (role) {
        case PreviewStreamRole::ViconMarkers: return "markers";
        case PreviewStreamRole::ViconSegments: return "segments";
        case PreviewStreamRole::HoloLensGaze: return "gaze";
        case PreviewStreamRole::HoloLensCalibrationTarget: return "calibration";
        case PreviewStreamRole::Unknown: return "unknown";
    }
    return "unknown";
}

} // namespace

struct PreviewStreamWorker::StreamState {
    QString requested_name, configured_source_id, bound_source_id;
    bool follow_by_name = false;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    PreviewTransformProfile transform;
    std::unique_ptr<lsl::stream_inlet> inlet;
    std::vector<std::string> labels;
    std::string coordinate_frame;
    std::vector<double> latest_sample;
    double nominal_rate = 0.0;
    PreviewRateTracker rate_tracker;
    double latest_timestamp = 0.0;
    qint64 last_sample_ms = -1, next_resolve_ms = 0;
    QString last_error;
    gui::StreamIdentity identity;

    bool connected() const { return inlet != nullptr; }
    bool hasSample() const { return last_sample_ms >= 0; }

    void clearSample() {
        last_sample_ms = -1;
        latest_timestamp = 0.0;
        rate_tracker.reset();
    }
};

PreviewStreamWorker::PreviewStreamWorker(PreviewWorkerConfig config, QObject* parent)
    : QThread(parent), match_tolerance_seconds_(config.match_tolerance_seconds),
      markers_(std::make_unique<StreamState>()), segments_(std::make_unique<StreamState>()),
      gaze_(std::make_unique<StreamState>()), calibration_target_(std::make_unique<StreamState>()) {
    qRegisterMetaType<gui::StreamIdentity>("vicon_lsl::gui::StreamIdentity");
    qRegisterMetaType<QVector<gui::StreamIdentity>>("QVector<vicon_lsl::gui::StreamIdentity>");
    qRegisterMetaType<ComponentLifecycleState>("ComponentLifecycleState");
    config.vicon_transform.name = "Vicon";
    if (config.vicon_transform.scale == 0.0) config.vicon_transform.scale = 0.001;
    config.gaze_transform.name = "HoloLens";

    auto initStream = [](StreamState& stream, const QString& name, const QString& source_id, bool follow,
                         PreviewStreamRole role, const PreviewTransformProfile& transform) {
        stream.requested_name = name;
        stream.configured_source_id = source_id;
        stream.follow_by_name = follow;
        stream.role = role;
        stream.transform = transform;
    };
    initStream(*markers_, config.marker_stream_name, config.marker_source_id, config.marker_follow_by_name,
               PreviewStreamRole::ViconMarkers, config.vicon_transform);
    initStream(*segments_, config.segment_stream_name, config.segment_source_id, config.segment_follow_by_name,
               PreviewStreamRole::ViconSegments, config.vicon_transform);
    initStream(*gaze_, config.gaze_stream_name, config.gaze_source_id, config.gaze_follow_by_name,
               PreviewStreamRole::HoloLensGaze, config.gaze_transform);
    initStream(*calibration_target_, config.calibration_stream_name, config.calibration_source_id,
               config.calibration_follow_by_name, PreviewStreamRole::HoloLensCalibrationTarget, {});
}

PreviewStreamWorker::~PreviewStreamWorker() {
    requestInterruption();
    wait();
}

bool PreviewStreamWorker::takeLatestFrame(PreviewFrame& frame, PreviewDeliveryMetrics& metrics) {
    return delivery_mailbox_.takeLatest(frame, metrics, steadyNowMs());
}

QVector<gui::StreamIdentity> PreviewStreamWorker::streamInventory() const {
    std::lock_guard<std::mutex> lock(inventory_mutex_);
    return inventory_;
}

void PreviewStreamWorker::setGazeTransform(PreviewTransformProfile transform) {
    std::lock_guard<std::mutex> lock(gaze_transform_mutex_);
    transform.name = "HoloLens";
    transform.scale = 1.0;
    gaze_->transform = std::move(transform);
}

void PreviewStreamWorker::run() {
    emit lifecycleChanged(ComponentLifecycleState::Starting, "Resolving configured streams");
    qint64 last_status_ms = 0;

    emit lifecycleChanged(ComponentLifecycleState::Running, "Live preview worker running");
    StreamState* const all_streams[] = {markers_.get(), segments_.get(), gaze_.get(), calibration_target_.get()};

    while (!isInterruptionRequested()) {
        const qint64 now = steadyNowMs();
        for (StreamState* s : all_streams) {
            if (isInterruptionRequested()) break;
            if (!s->connected() && now >= s->next_resolve_ms) {
                connectStream(*s);
                s->next_resolve_ms = now + kResolveRetryMs;
            }
        }
        if (isInterruptionRequested()) break;

        const bool segments_updated = pollStream(*segments_, now);
        const bool gaze_updated = pollStream(*gaze_, now);
        if (pollStream(*calibration_target_, now)) {
            const auto pose = parseCalibrationTargetPose(calibration_target_->labels, calibration_target_->latest_sample);
            if (pose && calibrationFramesCompatible()) emit targetPoseReady(*pose);
        }
        const bool markers_updated = pollStream(*markers_, now);
        if (markers_updated || segments_updated || gaze_updated) {
            const PreviewTransformProfile gaze_transform = currentGazeTransform();
            const PreviewFrameSnapshot frame_snapshot{
                {markers_->labels, markers_->latest_sample, markers_->transform, markers_->latest_timestamp,
                 markers_->connected(), streamIsFresh(*markers_, now), markers_updated},
                {segments_->labels, segments_->latest_sample, segments_->transform, segments_->latest_timestamp,
                 segments_->connected(), streamIsFresh(*segments_, now), segments_updated},
                {gaze_->labels, gaze_->latest_sample, gaze_transform, gaze_->latest_timestamp,
                 gaze_->connected(), streamIsFresh(*gaze_, now), gaze_updated},
                match_tolerance_seconds_};
            if (auto frame = assemblePreviewFrame(frame_snapshot)) {
                delivery_mailbox_.publish(std::move(*frame), steadyNowMs());
            }
        }

        if (now - last_status_ms >= kStatusIntervalMs) {
            updateStatus(now);
            last_status_ms = now;
        }
        msleep(4);
    }
    emit lifecycleChanged(ComponentLifecycleState::Stopping, "Closing preview inlets");
    for (StreamState* s : all_streams) s->inlet.reset();
    emit lifecycleChanged(ComponentLifecycleState::Stopped, "Preview stopped");
}

bool PreviewStreamWorker::connectStream(StreamState& state) {
    if (state.requested_name.trimmed().isEmpty()) {
        state.last_error = "No stream name configured";
        return false;
    }
    try {
        auto streams = lsl::resolve_stream("name", state.requested_name.toStdString(), 0, kResolveTimeoutSeconds);
        if (streams.empty()) return false;

        // Keep the displayed candidates in a consistent order.
        std::stable_sort(streams.begin(), streams.end(), [](const lsl::stream_info& left, const lsl::stream_info& right) {
            return std::make_tuple(left.source_id(), left.hostname(), left.session_id(), left.name()) <
                   std::make_tuple(right.source_id(), right.hostname(), right.session_id(), right.name());
        });
        QVector<gui::StreamIdentity> candidates;
        candidates.reserve(static_cast<qsizetype>(streams.size()));
        for (lsl::stream_info& c : streams) {
            gui::StreamIdentity id = gui::identityFromStreamInfo(c);
            id.role = roleText(state.role);
            candidates.push_back(std::move(id));
        }
        gui::StreamBinding selection_binding;
        selection_binding.name = state.requested_name;
        selection_binding.source_id = !state.configured_source_id.trimmed().isEmpty()
            ? state.configured_source_id.trimmed() : state.bound_source_id.trimmed();
        selection_binding.reconnection = state.follow_by_name ? gui::StreamReconnectionMode::FollowName
                                                              : gui::StreamReconnectionMode::SourceIdentity;
        const gui::StreamIdentitySelection selection = gui::selectStreamIdentity(candidates, selection_binding);
        if (selection.index < 0) {
            replaceInventory(state.role, std::move(candidates), selection.explanation);
            state.last_error = selection.explanation;
            return false;
        }

        return openStream(state, streams[static_cast<std::size_t>(selection.index)],
                          selection.should_warn ? selection.explanation : QString());
    } catch (const std::exception& ex) {
        state.inlet.reset();
        state.nominal_rate = 0.0;
        state.clearSample();
        state.last_error = QString::fromStdString(ex.what());
        return false;
    }
}

bool PreviewStreamWorker::openStream(StreamState& state, const lsl::stream_info& stream,
                                     QString warning) {
    auto inlet = std::make_unique<lsl::stream_inlet>(stream, 360, 0, true);
    lsl::stream_info metadata = stream;
    try { metadata = inlet->info(kMetadataTimeoutSeconds); } catch (const std::exception&) {}

    bool metadata_complete = false;
    state.labels = channelLabels(metadata, state.role, &metadata_complete);
    state.coordinate_frame = gui::coordinateFrameOf(metadata).toStdString();
    state.latest_sample.assign(static_cast<std::size_t>(metadata.channel_count()), 0.0);
    state.nominal_rate = metadata.nominal_srate() > 0.0 && std::isfinite(metadata.nominal_srate()) ? metadata.nominal_srate() : 0.0;
    state.inlet = std::move(inlet);
    state.inlet->set_postprocessing(lsl::post_clocksync);
    state.clearSample();
    state.last_error.clear();
    state.identity = gui::identityFromStreamInfo(metadata);
    state.identity.role = roleText(state.role);
    state.identity.nominal_rate = state.nominal_rate;
    const bool coordinate_required = state.role == PreviewStreamRole::HoloLensGaze ||
                                     state.role == PreviewStreamRole::HoloLensCalibrationTarget;
    state.identity.metadata_complete = metadata_complete &&
        gui::identityDescribesItself(state.identity, coordinate_required);
    if (!state.follow_by_name && !state.identity.source_id.isEmpty()) state.bound_source_id = state.identity.source_id;
    if (isInterruptionRequested()) return false;
    if (!state.identity.metadata_complete) {
        if (!warning.isEmpty()) warning += "; ";
        warning += "Some stream details were missing, so standard labels are in use";
    }
    state.identity.warning = warning;
    replaceInventory(state.role, {state.identity}, warning);
    return true;
}

bool PreviewStreamWorker::pollStream(StreamState& state, qint64 now_ms) {
    if (!state.inlet) return false;
    int samples_in_pass = 0;
    try {
        for (int pulls = 0; pulls < 16; ++pulls) {
            std::vector<double> sample(state.latest_sample.size());
            const double timestamp = state.inlet->pull_sample(sample, 0.0);
            if (timestamp <= 0.0) break;
            state.latest_sample = std::move(sample);
            state.latest_timestamp = timestamp;
            state.rate_tracker.addTimestamp(timestamp);
            state.last_sample_ms = now_ms;
            ++samples_in_pass;
        }
        if (samples_in_pass > 1) delivery_mailbox_.addCoalescedInputSamples(static_cast<unsigned long long>(samples_in_pass - 1));
        if (samples_in_pass > 0) {
            state.identity.freshness_ms = 0;
            state.identity.effective_rate = state.rate_tracker.hasFullWindow() ? state.rate_tracker.effectiveRateHz() : 0.0;
            std::lock_guard<std::mutex> lock(inventory_mutex_);
            for (gui::StreamIdentity& item : inventory_) {
                if (item.stableKey() == state.identity.stableKey()) {
                    item.freshness_ms = 0;
                    item.effective_rate = state.identity.effective_rate;
                    item.discovered_at = QDateTime::currentDateTimeUtc();
                }
            }
        }
    } catch (const std::exception& ex) {
        state.last_error = QString::fromStdString(ex.what());
        state.inlet.reset();
        state.clearSample();
    }
    return samples_in_pass > 0;
}

void PreviewStreamWorker::replaceInventory(PreviewStreamRole role,
                                          QVector<gui::StreamIdentity> streams,
                                          const QString& warning) {
    {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        inventory_.erase(std::remove_if(inventory_.begin(), inventory_.end(),
            [role](const gui::StreamIdentity& item) { return item.role == roleText(role); }),
            inventory_.end());
        for (gui::StreamIdentity stream : streams) {
            stream.warning = warning;
            inventory_.push_back(std::move(stream));
        }
    }
    // Deliver signals after releasing the lock: receivers may read the inventory.
    for (const auto& stream : streams) emit streamIdentityChanged(stream, warning);
}

bool PreviewStreamWorker::streamIsFresh(const StreamState& state, qint64 now_ms) const {
    return state.hasSample() && now_ms - state.last_sample_ms <= kStaleSampleMs;
}

bool PreviewStreamWorker::calibrationFramesCompatible() const {
    return calibrationCoordinateFramesCompatible(gaze_->coordinate_frame, calibration_target_->coordinate_frame);
}

PreviewTransformProfile PreviewStreamWorker::currentGazeTransform() const {
    std::lock_guard<std::mutex> lock(gaze_transform_mutex_);
    return gazeTransformForCoordinateFrame(gaze_->transform, gaze_->coordinate_frame);
}

void PreviewStreamWorker::updateStatus(qint64 now_ms) {
    const StreamState* const states[] = {markers_.get(), segments_.get(), gaze_.get(), calibration_target_.get()};
    {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        for (const StreamState* state : states) {
            for (gui::StreamIdentity& identity : inventory_) {
                if (identity.stableKey() != state->identity.stableKey()) continue;
                identity.freshness_ms = state->hasSample()
                    ? (std::max)(qint64{0}, now_ms - state->last_sample_ms) : -1;
                identity.effective_rate = state->rate_tracker.hasFullWindow() ? state->rate_tracker.effectiveRateHz() : 0.0;
                identity.warning = state->identity.warning;
                if (identity.freshness_ms > kStaleSampleMs) {
                    if (!identity.warning.isEmpty()) identity.warning += "; ";
                    identity.warning += "Latest sample has not updated recently";
                }
            }
        }
    }
    QStringList messages;
    for (const StreamState* state : states) {
        messages.push_back(streamStatusText(*state, now_ms));
    }
    for (const StreamState* state : states) {
        if (!state->last_error.isEmpty()) {
            messages.push_back(roleText(state->role) + " error: " + state->last_error);
        }
    }
    if (gaze_->connected() && calibration_target_->connected() && !calibrationFramesCompatible()) {
        messages.push_back("calibration unavailable: gaze frame " + QString::fromStdString(gaze_->coordinate_frame) +
                           " differs from target frame " + QString::fromStdString(calibration_target_->coordinate_frame));
    }
    emit statusChanged(messages.join("; "));
}

QString PreviewStreamWorker::streamStatusText(const StreamState& state, qint64 now_ms) const {
    QString status;
    if (!state.connected()) status = state.requested_name + ": resolving";
    else if (!state.hasSample()) status = state.requested_name + ": connected";
    else if (!streamIsFresh(state, now_ms)) {
        status = state.requested_name + ": not recently updated (" +
                 QString::number(static_cast<double>(now_ms - state.last_sample_ms) / 1000.0, 'f', 1) + "s)";
    } else status = state.requested_name + ": " + QString::number(state.latest_sample.size()) + "ch";

    if (streamIsFresh(state, now_ms) && state.rate_tracker.hasFullWindow()) {
        status += "; rate " + QString::number(state.rate_tracker.effectiveRateHz(), 'f', 1) + "Hz";
        if (state.role == PreviewStreamRole::HoloLensGaze &&
            state.rate_tracker.belowNominalRate(state.nominal_rate, kGazeLowRateFraction)) {
            status += " LOW RATE (expected " + QString::number(state.nominal_rate, 'f', 1) + "Hz)";
        }
    }
    return status;
}

} // namespace vicon_lsl
