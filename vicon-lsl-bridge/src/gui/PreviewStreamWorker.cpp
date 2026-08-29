#include "gui/PreviewStreamWorker.h"

#include "preview/PreviewCalibration.h"
#include "preview/PreviewFrameAssembler.h"
#include "preview/PreviewParsing.h"
#include "gui/PerformanceBudgets.h"

#include <lsl_cpp.h>

#include <QElapsedTimer>

#include <cmath>
#include <exception>
#include <algorithm>
#include <tuple>
#include <utility>

namespace vicon_lsl {
namespace {

constexpr int kResolveRetryMs = 1000;
constexpr int kStatusIntervalMs = 1000;
constexpr int kStaleSampleMs = 500;
constexpr double kGazeLowRateFraction = 0.8;

std::vector<std::string> channelLabels(lsl::stream_info info,
                                       PreviewStreamRole role,
                                       bool* complete) {
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
        if (complete) *complete = true;
        return labels;
    }
    auto canonical = canonicalPreviewChannelLabels(role, channel_count);
    if (!canonical.empty()) {
        if (complete) *complete = false;
        return canonical;
    }

    while (labels.size() < channel_count) {
        labels.push_back("ch_" + std::to_string(labels.size()));
    }
    labels.resize(channel_count);
    if (complete) *complete = false;
    return labels;
}

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
    QString requested_name;
    QString configured_source_id;
    QString bound_source_id;
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
    bool have_sample = false;
    qint64 last_sample_ms = -1;
    qint64 next_resolve_ms = 0;
    QString last_error;
    gui::StreamIdentity identity;

    bool connected() const { return inlet != nullptr; }
};

PreviewStreamWorker::PreviewStreamWorker(PreviewWorkerConfig config, QObject* parent)
    : QThread(parent),
      config_(std::move(config)),
      markers_(std::make_unique<StreamState>()),
      segments_(std::make_unique<StreamState>()),
      gaze_(std::make_unique<StreamState>()),
      calibration_target_(std::make_unique<StreamState>()) {
    qRegisterMetaType<gui::StreamIdentity>("vicon_lsl::gui::StreamIdentity");
    qRegisterMetaType<QVector<gui::StreamIdentity>>("QVector<vicon_lsl::gui::StreamIdentity>");
    qRegisterMetaType<ComponentLifecycleState>("ComponentLifecycleState");
    config_.vicon_transform.name = "Vicon";
    config_.vicon_transform.scale = config_.vicon_transform.scale == 0.0
        ? 0.001
        : config_.vicon_transform.scale;
    config_.gaze_transform.name = "HoloLens";
    markers_->requested_name = config_.marker_stream_name;
    markers_->configured_source_id = config_.marker_source_id;
    markers_->follow_by_name = config_.marker_follow_by_name;
    markers_->role = PreviewStreamRole::ViconMarkers;
    markers_->transform = config_.vicon_transform;
    segments_->requested_name = config_.segment_stream_name;
    segments_->configured_source_id = config_.segment_source_id;
    segments_->follow_by_name = config_.segment_follow_by_name;
    segments_->role = PreviewStreamRole::ViconSegments;
    segments_->transform = config_.vicon_transform;
    gaze_->requested_name = config_.gaze_stream_name;
    gaze_->configured_source_id = config_.gaze_source_id;
    gaze_->follow_by_name = config_.gaze_follow_by_name;
    gaze_->role = PreviewStreamRole::HoloLensGaze;
    gaze_->transform = config_.gaze_transform;
    calibration_target_->requested_name = config_.calibration_stream_name;
    calibration_target_->configured_source_id = config_.calibration_source_id;
    calibration_target_->follow_by_name = config_.calibration_follow_by_name;
    calibration_target_->role = PreviewStreamRole::HoloLensCalibrationTarget;
}

PreviewStreamWorker::~PreviewStreamWorker() {
    requestInterruption();
    wait();
}

bool PreviewStreamWorker::takeLatestFrame(PreviewFrame& frame,
                                          PreviewDeliveryMetrics& metrics) {
    return delivery_mailbox_.takeLatest(frame, metrics, steadyNowMs());
}

PreviewDeliveryMetrics PreviewStreamWorker::deliveryMetrics() const {
    return delivery_mailbox_.metrics();
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
    QElapsedTimer timer;
    timer.start();
    last_status_ms_ = 0;

    emit lifecycleChanged(ComponentLifecycleState::Running, "Live preview worker running");
    while (!isInterruptionRequested()) {
        const qint64 now = timer.msecsSinceReference();
        if (!markers_->connected() && now >= markers_->next_resolve_ms) {
            connectStream(*markers_);
            markers_->next_resolve_ms = now + kResolveRetryMs;
        }
        if (isInterruptionRequested()) break;
        if (!segments_->connected() && now >= segments_->next_resolve_ms) {
            connectStream(*segments_);
            segments_->next_resolve_ms = now + kResolveRetryMs;
        }
        if (isInterruptionRequested()) break;
        if (!gaze_->connected() && now >= gaze_->next_resolve_ms) {
            connectStream(*gaze_);
            gaze_->next_resolve_ms = now + kResolveRetryMs;
        }
        if (isInterruptionRequested()) break;
        if (!calibration_target_->connected() && now >= calibration_target_->next_resolve_ms) {
            connectStream(*calibration_target_);
            calibration_target_->next_resolve_ms = now + kResolveRetryMs;
        }
        if (isInterruptionRequested()) break;

        const bool segments_updated = pollStream(*segments_, now);
        const bool gaze_updated = pollStream(*gaze_, now);
        if (pollStream(*calibration_target_, now)) {
            const auto pose = parseCalibrationTargetPose(calibration_target_->labels,
                                                         calibration_target_->latest_sample);
            if (pose && calibrationFramesCompatible()) {
                emit targetPoseReady(*pose);
            }
        }
        const bool markers_updated = pollStream(*markers_, now);
        if (markers_updated || segments_updated || gaze_updated) {
            const PreviewTransformProfile gaze_transform = currentGazeTransform();
            const PreviewStreamSnapshot marker_snapshot{
                markers_->labels,
                markers_->latest_sample,
                markers_->transform,
                markers_->latest_timestamp,
                markers_->connected(),
                streamIsFresh(*markers_, now),
                markers_updated};
            const PreviewStreamSnapshot segment_snapshot{
                segments_->labels,
                segments_->latest_sample,
                segments_->transform,
                segments_->latest_timestamp,
                segments_->connected(),
                streamIsFresh(*segments_, now),
                segments_updated};
            const PreviewStreamSnapshot gaze_snapshot{
                gaze_->labels,
                gaze_->latest_sample,
                gaze_transform,
                gaze_->latest_timestamp,
                gaze_->connected(),
                streamIsFresh(*gaze_, now),
                gaze_updated};
            const PreviewFrameSnapshot frame_snapshot{
                marker_snapshot,
                segment_snapshot,
                gaze_snapshot,
                config_.match_tolerance_seconds};
            if (auto frame = assemblePreviewFrame(frame_snapshot)) {
                publishLatestFrame(std::move(*frame));
            }
        }

        if (now - last_status_ms_ >= kStatusIntervalMs) {
            updateStatus(now);
            last_status_ms_ = now;
        }

        msleep(4);
    }
    emit lifecycleChanged(ComponentLifecycleState::Stopping, "Closing preview inlets");
    markers_->inlet.reset();
    segments_->inlet.reset();
    gaze_->inlet.reset();
    calibration_target_->inlet.reset();
    emit lifecycleChanged(ComponentLifecycleState::Stopped, "Preview stopped");
}

bool PreviewStreamWorker::connectStream(StreamState& state) {
    if (state.requested_name.trimmed().isEmpty()) {
        state.last_error = "No stream name configured";
        return false;
    }

    try {
        auto streams = lsl::resolve_stream(
            "name", state.requested_name.toStdString(), 0,
            gui::PerformanceBudgets::PreviewResolveTimeoutSeconds);
        if (streams.empty()) {
            return false;
        }

        std::stable_sort(streams.begin(), streams.end(), [](const lsl::stream_info& left,
                                                            const lsl::stream_info& right) {
            return std::make_tuple(left.source_id(), left.hostname(), left.session_id(), left.name()) <
                   std::make_tuple(right.source_id(), right.hostname(), right.session_id(), right.name());
        });
        QVector<gui::StreamIdentity> candidates;
        candidates.reserve(static_cast<qsizetype>(streams.size()));
        for (const lsl::stream_info& candidate : streams) {
            gui::StreamIdentity identity;
            identity.role = roleText(state.role);
            identity.name = QString::fromStdString(candidate.name());
            identity.type = QString::fromStdString(candidate.type());
            identity.source_id = QString::fromStdString(candidate.source_id());
            identity.hostname = QString::fromStdString(candidate.hostname());
            identity.session_id = QString::fromStdString(candidate.session_id());
            identity.uid = QString::fromStdString(candidate.uid());
            identity.publisher_created_at = candidate.created_at();
            identity.channel_count = candidate.channel_count();
            identity.nominal_rate = candidate.nominal_srate();
            identity.discovered_at = QDateTime::currentDateTimeUtc();
            candidates.push_back(identity);
        }
        gui::StreamBinding selection_binding;
        selection_binding.name = state.requested_name;
        selection_binding.source_id = !state.configured_source_id.trimmed().isEmpty()
            ? state.configured_source_id.trimmed()
            : state.bound_source_id.trimmed();
        selection_binding.reconnection = state.follow_by_name
            ? gui::StreamReconnectionMode::FollowName
            : gui::StreamReconnectionMode::SourceIdentity;
        const gui::StreamIdentitySelection selection =
            gui::selectStreamIdentity(candidates, selection_binding);
        if (selection.index < 0) {
            {
                std::lock_guard<std::mutex> lock(inventory_mutex_);
                inventory_.erase(std::remove_if(inventory_.begin(), inventory_.end(),
                    [&state](const gui::StreamIdentity& item) {
                        return item.role == roleText(state.role);
                    }), inventory_.end());
                for (gui::StreamIdentity candidate : candidates) {
                    candidate.warning = selection.explanation;
                    inventory_.push_back(std::move(candidate));
                }
            }
            for (const gui::StreamIdentity& candidate : candidates) {
                emit streamIdentityChanged(candidate, selection.explanation);
            }
            state.last_error = selection.explanation;
            return false;
        }
        const std::size_t selected_index = static_cast<std::size_t>(selection.index);
        QString selection_warning =
            selection.used_name_fallback ||
                    selection.explanation.contains("recovered instances")
                ? selection.explanation : QString();

        auto inlet = std::make_unique<lsl::stream_inlet>(streams[selected_index], 360, 0, true);
        lsl::stream_info metadata = streams[selected_index];
        try {
            // Resolver results may contain only the short stream description.
            // The inlet supplies the full channel and coordinate metadata.
            metadata = inlet->info(
                gui::PerformanceBudgets::PreviewMetadataTimeoutSeconds);
        } catch (const std::exception&) {
            // Known fixed HoloLens schemas still have a safe label fallback.
        }
        bool metadata_complete = false;
        state.labels = channelLabels(metadata, state.role, &metadata_complete);
        const char* coordinate_frame = metadata
            .desc()
            .child("acquisition")
            .child_value("coordinate_frame");
        state.coordinate_frame = coordinate_frame ? coordinate_frame : "";
        state.latest_sample.assign(static_cast<std::size_t>(metadata.channel_count()), 0.0);
        state.nominal_rate = metadata.nominal_srate();
        if (!std::isfinite(state.nominal_rate) || state.nominal_rate <= 0.0) {
            state.nominal_rate = 0.0;
        }
        state.inlet = std::move(inlet);
        // Live preview consumes timestamps in the local recorder clock. Keep
        // clock synchronization scoped to these inlets; XDF playback applies
        // its recorded offsets independently when loading the file.
        state.inlet->set_postprocessing(lsl::post_clocksync);
        state.have_sample = false;
        state.last_sample_ms = -1;
        state.latest_timestamp = 0.0;
        state.rate_tracker.reset();
        state.last_error.clear();
        state.identity.role = roleText(state.role);
        state.identity.name = QString::fromStdString(metadata.name());
        state.identity.type = QString::fromStdString(metadata.type());
        state.identity.source_id = QString::fromStdString(metadata.source_id());
        state.identity.hostname = QString::fromStdString(metadata.hostname());
        state.identity.session_id = QString::fromStdString(metadata.session_id());
        state.identity.uid = QString::fromStdString(metadata.uid());
        state.identity.publisher_created_at = metadata.created_at();
        state.identity.channel_count = metadata.channel_count();
        state.identity.nominal_rate = state.nominal_rate;
        state.identity.coordinate_frame = QString::fromStdString(state.coordinate_frame);
        const bool coordinate_required =
            state.role == PreviewStreamRole::HoloLensGaze ||
            state.role == PreviewStreamRole::HoloLensCalibrationTarget;
        state.identity.metadata_complete = metadata_complete &&
            !state.identity.source_id.isEmpty() &&
            (!coordinate_required || !state.identity.coordinate_frame.isEmpty());
        state.identity.discovered_at = QDateTime::currentDateTimeUtc();
        if (!state.follow_by_name && !state.identity.source_id.isEmpty()) {
            state.bound_source_id = state.identity.source_id;
        }
        if (isInterruptionRequested()) return false;
        if (!state.identity.metadata_complete) {
            if (!selection_warning.isEmpty()) selection_warning += "; ";
            selection_warning += "Channel or coordinate metadata was incomplete; canonical fallback labels are in use";
        }
        state.identity.warning = selection_warning;
        {
            std::lock_guard<std::mutex> lock(inventory_mutex_);
            inventory_.erase(std::remove_if(inventory_.begin(), inventory_.end(),
                [&state](const gui::StreamIdentity& item) {
                    return item.role == roleText(state.role);
                }), inventory_.end());
            inventory_.push_back(state.identity);
        }
        emit streamIdentityChanged(state.identity, selection_warning);
        return true;
    } catch (const std::exception& ex) {
        state.inlet.reset();
        state.nominal_rate = 0.0;
        state.have_sample = false;
        state.last_sample_ms = -1;
        state.latest_timestamp = 0.0;
        state.rate_tracker.reset();
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
        int samples_in_pass = 0;
        for (int pulls = 0; pulls < 16; ++pulls) {
            std::vector<double> sample(state.latest_sample.size());
            const double timestamp = state.inlet->pull_sample(sample, 0.0);
            if (timestamp <= 0.0) {
                break;
            }
            state.latest_sample = std::move(sample);
            state.latest_timestamp = timestamp;
            state.rate_tracker.addTimestamp(timestamp);
            state.have_sample = true;
            state.last_sample_ms = now_ms;
            updated = true;
            ++samples_in_pass;
        }
        if (samples_in_pass > 1) {
            delivery_mailbox_.addCoalescedInputSamples(
                static_cast<unsigned long long>(samples_in_pass - 1));
        }
        if (updated) {
            state.identity.freshness_ms = 0;
            state.identity.effective_rate = state.rate_tracker.hasFullWindow()
                ? state.rate_tracker.effectiveRateHz() : 0.0;
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
        state.have_sample = false;
        state.last_sample_ms = -1;
        state.latest_timestamp = 0.0;
        state.rate_tracker.reset();
    }
    return updated;
}

void PreviewStreamWorker::publishLatestFrame(PreviewFrame frame) {
    delivery_mailbox_.publish(std::move(frame), steadyNowMs());
}

bool PreviewStreamWorker::streamIsFresh(const StreamState& state, qint64 now_ms) const {
    return state.have_sample && state.last_sample_ms >= 0 &&
           now_ms - state.last_sample_ms <= kStaleSampleMs;
}

bool PreviewStreamWorker::calibrationFramesCompatible() const {
    return calibrationCoordinateFramesCompatible(gaze_->coordinate_frame,
                                                  calibration_target_->coordinate_frame);
}

PreviewTransformProfile PreviewStreamWorker::currentGazeTransform() const {
    std::lock_guard<std::mutex> lock(gaze_transform_mutex_);
    return gazeTransformForCoordinateFrame(gaze_->transform, gaze_->coordinate_frame);
}

void PreviewStreamWorker::updateStatus(qint64 now_ms) {
    {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        const StreamState* const states[] = {
            markers_.get(), segments_.get(), gaze_.get(), calibration_target_.get(),
        };
        for (const StreamState* state : states) {
            for (gui::StreamIdentity& identity : inventory_) {
                if (identity.stableKey() != state->identity.stableKey()) continue;
                identity.freshness_ms = state->have_sample && state->last_sample_ms >= 0
                    ? (std::max)(qint64{0}, now_ms - state->last_sample_ms)
                    : -1;
                identity.effective_rate = state->rate_tracker.hasFullWindow()
                    ? state->rate_tracker.effectiveRateHz() : 0.0;
                identity.warning = state->identity.warning;
                if (identity.freshness_ms > kStaleSampleMs) {
                    if (!identity.warning.isEmpty()) identity.warning += "; ";
                    identity.warning += "Latest sample is stale";
                }
            }
        }
    }
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
    QString status;
    if (!state.connected()) {
        status = state.requested_name + ": resolving";
    } else if (!state.have_sample) {
        status = state.requested_name + ": connected";
    } else if (!streamIsFresh(state, now_ms)) {
        status = state.requested_name + ": stale (" +
               QString::number(static_cast<double>(now_ms - state.last_sample_ms) / 1000.0,
                               'f', 1) +
               "s)";
    } else {
        status = state.requested_name + ": " + QString::number(state.latest_sample.size()) + "ch";
    }

    // A stopped stream is already reported as stale. Do not keep showing its
    // last measured rate as if the old window were still current.
    if (streamIsFresh(state, now_ms) && state.rate_tracker.hasFullWindow()) {
        status += "; rate " +
                  QString::number(state.rate_tracker.effectiveRateHz(), 'f', 1) + "Hz";
        const bool low_gaze_rate = state.role == PreviewStreamRole::HoloLensGaze &&
                                   state.rate_tracker.belowNominalRate(
                                       state.nominal_rate, kGazeLowRateFraction);
        if (low_gaze_rate) {
            status += " LOW RATE (nominal " +
                      QString::number(state.nominal_rate, 'f', 1) + "Hz)";
        }
    }
    return status;
}

} // namespace vicon_lsl
