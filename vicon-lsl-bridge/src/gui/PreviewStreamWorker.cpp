#include "gui/PreviewStreamWorker.h"

#include "gui/LslStreamIdentity.h"
#include "preview/PreviewFrameAssembler.h"
#include "preview/PreviewParsing.h"

#include <lsl_cpp.h>

#include <QDateTime>
#include <QElapsedTimer>
#include <QStringList>

#include <algorithm>
#include <tuple>

namespace vicon_lsl {
namespace {

constexpr int kResolveRetryMs = 1000;
constexpr int kStatusIntervalMs = 1000;
constexpr int kStaleSampleMs = 500;
constexpr double kGazeLowRateFraction = 0.8;
constexpr double kMetadataTimeoutSeconds = 0.25;
constexpr double kResolveTimeoutSeconds = 0.05;

// Channel labels from the stream details. When they are incomplete, uses the
// fixed HoloLens labels for that role, or else ch_0, ch_1, and so on.
std::vector<std::string> channelLabels(lsl::stream_info& info, PreviewStreamRole role,
                                       bool& complete) {
    const auto channel_count = static_cast<std::size_t>(info.channel_count());
    std::vector<std::string> labels;
    labels.reserve(channel_count);
    complete = true;
    for (lsl::xml_element ch = info.desc().child("channels").child("channel"); !ch.empty(); ch = ch.next_sibling()) {
        const char* label = ch.child_value("label");
        if (label && *label) {
            labels.emplace_back(label);
        } else {
            complete = false;
            labels.push_back("ch_" + std::to_string(labels.size()));
        }
    }
    if (complete && labels.size() == channel_count) {
        return labels;
    }

    complete = false;
    auto canonical = canonicalPreviewChannelLabels(role, channel_count);
    if (!canonical.empty()) {
        return canonical;
    }
    while (labels.size() < channel_count) labels.push_back("ch_" + std::to_string(labels.size()));
    labels.resize(channel_count);
    return labels;
}

// Milliseconds on a clock that never goes backward.
qint64 steadyNowMs() {
    QElapsedTimer timer;
    timer.start();
    return timer.msecsSinceReference();
}

} // namespace

struct PreviewStreamWorker::StreamState {
    gui::StreamBinding binding;
    QString bound_source_id;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    PreviewTransformProfile transform;
    std::unique_ptr<lsl::stream_inlet> inlet;
    std::vector<std::string> labels;
    std::string coordinate_frame;
    std::vector<double> latest_sample;
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

    const auto init = [](StreamState& stream, const gui::StreamBinding& binding,
                         PreviewStreamRole role, const PreviewTransformProfile& transform) {
        stream.binding = binding;
        stream.role = role;
        stream.transform = transform;
    };
    init(*markers_, config.markers, PreviewStreamRole::ViconMarkers, config.vicon_transform);
    init(*segments_, config.segments, PreviewStreamRole::ViconSegments, config.vicon_transform);
    init(*gaze_, config.gaze, PreviewStreamRole::HoloLensGaze, config.gaze_transform);
    init(*calibration_target_, config.calibration, PreviewStreamRole::HoloLensCalibrationTarget, {});
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
    if (state.binding.name.trimmed().isEmpty()) {
        state.last_error = "No stream name configured";
        return false;
    }
    try {
        auto streams = lsl::resolve_stream("name", state.binding.name.toStdString(), 0, kResolveTimeoutSeconds);
        if (streams.empty()) return false;

        // Sort so the candidates always appear in the same order.
        std::stable_sort(streams.begin(), streams.end(), [](const lsl::stream_info& left, const lsl::stream_info& right) {
            return std::make_tuple(left.source_id(), left.hostname(), left.session_id(), left.name()) <
                   std::make_tuple(right.source_id(), right.hostname(), right.session_id(), right.name());
        });
        QVector<gui::StreamIdentity> candidates;
        candidates.reserve(static_cast<qsizetype>(streams.size()));
        for (lsl::stream_info& c : streams) {
            gui::StreamIdentity id = gui::identityFromStreamInfo(c);
            id.role = previewStreamRoleName(state.role);
            candidates.push_back(std::move(id));
        }
        // A source ID from the settings wins over the one found on the last connection.
        gui::StreamBinding selection_binding = state.binding;
        selection_binding.source_id = state.binding.source_id.trimmed();
        if (selection_binding.source_id.isEmpty()) {
            selection_binding.source_id = state.bound_source_id.trimmed();
        }
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
    state.labels = channelLabels(metadata, state.role, metadata_complete);
    state.coordinate_frame = gui::coordinateFrameOf(metadata).toStdString();
    state.latest_sample.assign(static_cast<std::size_t>(metadata.channel_count()), 0.0);
    state.inlet = std::move(inlet);
    state.inlet->set_postprocessing(lsl::post_clocksync);
    state.clearSample();
    state.last_error.clear();
    state.identity = gui::identityFromStreamInfo(metadata);
    state.identity.role = previewStreamRoleName(state.role);
    const bool coordinate_required = state.role == PreviewStreamRole::HoloLensGaze ||
                                     state.role == PreviewStreamRole::HoloLensCalibrationTarget;
    state.identity.metadata_complete = metadata_complete &&
        gui::identityDescribesItself(state.identity, coordinate_required);
    // Remember the source ID so a restart reconnects to the same publisher.
    if (state.binding.reconnection == gui::StreamReconnectionMode::SourceIdentity &&
        !state.identity.source_id.isEmpty()) {
        state.bound_source_id = state.identity.source_id;
    }
    if (isInterruptionRequested()) return false;
    if (!state.identity.metadata_complete) {
        if (!warning.isEmpty()) warning += "; ";
        warning += "Some stream details were missing, so standard labels are in use";
    }
    state.identity.warning = warning;
    replaceInventory(state.role, {state.identity}, warning);
    return true;
}

// Reads up to 16 waiting samples without blocking and keeps the newest.
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
        const QString role_name = previewStreamRoleName(role);
        inventory_.erase(std::remove_if(inventory_.begin(), inventory_.end(),
            [&role_name](const gui::StreamIdentity& item) { return item.role == role_name; }),
            inventory_.end());
        for (gui::StreamIdentity stream : streams) {
            stream.warning = warning;
            inventory_.push_back(std::move(stream));
        }
    }
    // Emit after unlocking, because receivers may read the inventory.
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
    return gaze_->transform;
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
            messages.push_back(QString(previewStreamRoleName(state->role)) + " error: " + state->last_error);
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
    if (!state.connected()) status = state.binding.name + ": resolving";
    else if (!state.hasSample()) status = state.binding.name + ": connected";
    else if (!streamIsFresh(state, now_ms)) {
        status = state.binding.name + ": not recently updated (" +
                 QString::number(static_cast<double>(now_ms - state.last_sample_ms) / 1000.0, 'f', 1) + "s)";
    } else status = state.binding.name + ": " + QString::number(state.latest_sample.size()) + "ch";

    if (streamIsFresh(state, now_ms) && state.rate_tracker.hasFullWindow()) {
        status += "; rate " + QString::number(state.rate_tracker.effectiveRateHz(), 'f', 1) + "Hz";
        if (state.role == PreviewStreamRole::HoloLensGaze &&
            state.rate_tracker.belowNominalRate(state.identity.nominal_rate, kGazeLowRateFraction)) {
            status += " LOW RATE (expected " + QString::number(state.identity.nominal_rate, 'f', 1) + "Hz)";
        }
    }
    return status;
}

} // namespace vicon_lsl
