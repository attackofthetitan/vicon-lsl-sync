#include "ViconLSLBridge.h"
#include "ViconClient.h"
#include "ViconLSLBridgeInternal.h"

#include <lsl_cpp.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace {

// Frames between layout checks and status reports, and how long reads must stay
// clean before they count as recovered.
constexpr unsigned int kStatusIntervalFrames = 100;
// Frame waits, about a second each, that may time out in a row before the
// bridge reconnects.
constexpr unsigned int kMaxFrameTimeouts = 5;
// Shortest time between status reports that timestamps were re-synchronized, so a
// replay that keeps drifting does not report on every frame.
constexpr double kResyncReportIntervalSeconds = 5.0;
// Longest time between status reports and layout checks while streaming, so a
// slow camera rate still reports more often than the window's three-second limit.
constexpr double kStatusIntervalSeconds = 1.0;

vicon_lsl::bridge_internal::Dependencies liveDependencies(const Config& config) {
    vicon_lsl::bridge_internal::Dependencies dependencies;
    dependencies.client = std::make_shared<::ViconClient>(config.vicon_server);
    dependencies.outlet_factory = createLslStreamOutlet;
    dependencies.clock = [] { return lsl::local_clock(); };
    dependencies.wait = [](std::chrono::milliseconds duration) {
        std::this_thread::sleep_for(duration);
    };
    return dependencies;
}

} // namespace

ViconLSLBridge::ViconLSLBridge(const Config& config)
    : ViconLSLBridge(config, liveDependencies(config)) {}

ViconLSLBridge::ViconLSLBridge(
    const Config& config,
    vicon_lsl::bridge_internal::Dependencies dependencies)
    : config_(config),
      client_(std::move(dependencies.client)),
      marker_stream_(dependencies.outlet_factory),
      segment_stream_(std::move(dependencies.outlet_factory)),
      clock_(std::move(dependencies.clock)),
      wait_(std::move(dependencies.wait)) {}

void ViconLSLBridge::setStatusCallback(StatusCallback callback) {
    status_callback_ = std::move(callback);
}

void ViconLSLBridge::reportStatus(BridgeState state, const std::string& message) {
    if (status_callback_) {
        BridgeStatus status;
        status.state = state;
        status.marker_count = known_layout_.markers.size();
        status.segment_count = known_layout_.segments.size();
        status.frame_count = frame_count_;
        status.layout_change_count = layout_change_count_;
        status.message = message.empty() ? last_diagnostic_message_ : message;
        status_callback_(status);
    }
}

void ViconLSLBridge::stop() {
    running_ = false;
}

void ViconLSLBridge::run() {
    // Keep timestamps increasing across reconnects, because LabRecorder joins a
    // reopened stream with the same source ID onto the old one.
    vicon_lsl::ViconTimestampState timestamp_state;
    bool previous_first_frame_failed = false;
    while (running_) {
        connectWithRetry();
        if (!running_) {
            if (client_->isConnected()) client_->disconnect();
            break;
        }

        if (!readFirstFrame()) {
            if (!running_) {
                client_->disconnect();
                break;
            }
            std::cerr << "Failed to get initial frame, reconnecting" << std::endl;
            reportStatus(BridgeState::Connecting, "Failed to get initial frame, reconnecting");
            client_->disconnect();
            // Retry the first failure right away, since a server that is still
            // starting usually sends a frame next time, but wait before later ones.
            if (previous_first_frame_failed) waitForRetry();
            previous_first_frame_failed = true;
            continue;
        }
        previous_first_frame_failed = false;
        frame_count_ = client_->frameNumber();

        if (refreshStreams(BridgeState::Connecting)) {
            streamFrames(timestamp_state);
            resetConnectedSession();
        } else {
            reportStatus(BridgeState::Connecting, last_diagnostic_message_);
            client_->disconnect();
        }
        waitForRetry();
    }

    // Streams kept open through a reconnect close only now.
    marker_stream_.destroy();
    segment_stream_.destroy();
    reportStatus(BridgeState::Stopped, "Stopped");
    std::cout << "Stopped" << std::endl;
}

void ViconLSLBridge::streamFrames(vicon_lsl::ViconTimestampState& timestamp_state) {
    std::cout << "Streaming started" << std::endl;
    reportStatus(BridgeState::Streaming, "Streaming started");
    // Vicon pauses briefly without dropping the connection, so a few missed
    // frames keep the streams open for the recorder.
    unsigned int frame_timeouts = 0;
    double last_status_time = clock_();
    while (running_ && client_->isConnected()) {
        if (!client_->getFrame()) {
            if (client_->frameTimedOut() && ++frame_timeouts < kMaxFrameTimeouts) {
                if (frame_timeouts == 1) {
                    std::cerr << "No Vicon frame arrived in time; waiting" << std::endl;
                    reportStatus(BridgeState::Streaming, "Waiting for Vicon frames");
                }
                continue;
            }
            std::cerr << "Lost connection, will reconnect" << std::endl;
            reportStatus(BridgeState::Connecting, "Lost connection, will reconnect");
            return;
        }
        if (frame_timeouts > 0) {
            frame_timeouts = 0;
            std::cerr << "Vicon frames resumed" << std::endl;
            reportStatus(BridgeState::Streaming, "Vicon frames resumed");
        }
        frame_count_ = client_->frameNumber();

        double timestamp = 0.0;
        bool timestamp_adjusted = false;
        bool timestamp_stepped_back = false;
        if (!vicon_lsl::enforceViconTimestamp(
                client_->frameTimestamp(),
                clock_(),
                timestamp_state,
                timestamp,
                &timestamp_adjusted,
                &timestamp_stepped_back)) {
            std::cerr << "Unable to obtain a finite Vicon frame timestamp" << std::endl;
            continue;
        }
        reportTimestampCorrections(client_->frameTimestampResync(), timestamp_adjusted,
                                   timestamp_stepped_back);
        if (!streamFrame(timestamp)) {
            reportStatus(BridgeState::Connecting,
                         "LSL outlet failed; reconnecting and recreating streams");
            return;
        }

        const double now = clock_();
        if (++frames_since_layout_check_ >= kStatusIntervalFrames ||
            now - last_status_time >= kStatusIntervalSeconds) {
            frames_since_layout_check_ = 0;
            last_status_time = now;
            reportStatus(BridgeState::Streaming);
            if (!refreshStreams(BridgeState::Streaming)) {
                reportStatus(BridgeState::Connecting, last_diagnostic_message_);
                return;
            }
        }
    }
}

// Keeps the streams through a reconnect, because closing an LSL stream that a
// recorder is reading makes liblsl send it one last sample holding old values
// with a current time, and a reopened stream takes the recorder seconds to find.
// refreshStreams() reopens them only if the layout or rate changed.
void ViconLSLBridge::resetConnectedSession() {
    if (!running_) {
        marker_stream_.destroy();
        segment_stream_.destroy();
    }
    client_->disconnect();
    frame_count_ = 0;
    frames_since_layout_check_ = 0;
    clean_frames_ = 0;
    known_layout_ = {};
    diagnostic_aggregator_.clear();
    last_diagnostic_message_.clear();
    reportStatus(BridgeState::Disconnected, "Disconnected");
}

void ViconLSLBridge::connectWithRetry() {
    reportStatus(BridgeState::Connecting, "Connecting to " + config_.vicon_server);
    while (running_ && !client_->connect()) {
        reportStatus(
            BridgeState::Connecting,
            "Retrying in " + std::to_string(config_.reconnect_interval_ms) + "ms");
        waitForRetry();
    }
}

// Allows the same run of one-second waits as streaming, because Vicon sends no
// frames while it is paused or between frames of a slow replay.
bool ViconLSLBridge::readFirstFrame() {
    unsigned int timeouts = 0;
    while (running_) {
        if (client_->getFrame()) return true;
        if (!client_->frameTimedOut() || ++timeouts >= kMaxFrameTimeouts) return false;
        if (timeouts == 1) {
            std::cerr << "No Vicon frame arrived in time; waiting for the first frame" << std::endl;
            reportStatus(BridgeState::Connecting, "Waiting for the first Vicon frame");
        }
    }
    return false;
}

// Sleeps in steps of at most 100 ms so stop() takes effect quickly.
void ViconLSLBridge::waitForRetry() {
    int remaining_ms = config_.reconnect_interval_ms;
    while (running_ && remaining_ms > 0) {
        const int sleep_ms = (std::min)(remaining_ms, 100);
        wait_(std::chrono::milliseconds(sleep_ms));
        remaining_ms -= sleep_ms;
    }
}

// Opens the streams, or keeps the ones already open when their layout and rate
// still match (while streaming, and after a reconnect), and reopens both only if
// either changed. A failed layout read keeps working streams while streaming.
bool ViconLSLBridge::refreshStreams(BridgeState state) {
    const auto discovery = vicon_lsl::discoverLayout(*client_, frame_count_);
    if (!discovery.ok()) {
        handleDiagnostics(discovery.diagnostics, state);
        return state == BridgeState::Streaming;
    }
    // The header holds the rate, so a new camera rate needs new streams too;
    // the recorder rejoins them because their channels stay the same.
    const double rate = client_->frameRate();
    const bool rate_changed =
        std::abs(rate - published_rate_) > 0.01 * (std::max)(rate, published_rate_);
    const bool streams_open = marker_stream_.isInitialized() && segment_stream_.isInitialized();
    if (streams_open && published_layout_ && discovery.layout == *published_layout_ &&
        !rate_changed) {
        if (state == BridgeState::Connecting) {
            known_layout_ = discovery.layout;
            diagnostic_aggregator_.clear();
            last_diagnostic_message_.clear();
            clean_frames_ = 0;
            std::cout << "Layout and frame rate unchanged; keeping the open streams" << std::endl;
        }
        return true;
    }
    if (streams_open) {
        std::cout << "Layout or frame rate changed, reinitializing streams" << std::endl;
    }
    marker_stream_.destroy();
    segment_stream_.destroy();

    known_layout_ = discovery.layout;
    diagnostic_aggregator_.clear();
    last_diagnostic_message_.clear();
    clean_frames_ = 0;
    // A recorder already receiving the old streams cannot follow new ones with
    // different channels, even when the change happened during a reconnect.
    if (published_layout_ && *published_layout_ != known_layout_) ++layout_change_count_;
    published_layout_ = known_layout_;

    std::cout << "Discovered " << known_layout_.markers.size() << " markers and "
              << known_layout_.segments.size() << " segments" << std::endl;

    const double nominal_rate = client_->frameRate();
    published_rate_ = nominal_rate;
    if (std::isfinite(nominal_rate) && nominal_rate > 0.0) {
        std::cout << "Vicon frame rate: " << nominal_rate << " Hz" << std::endl;
    } else {
        std::cerr << "Vicon frame rate unavailable; publishing irregular-rate streams"
                  << std::endl;
    }

    const std::string& hostname = sourceHost();
    try {
        marker_stream_.initialize(
            known_layout_.markers,
            config_.marker_stream_name,
            "vicon_markers_" + hostname,
            nominal_rate);
        segment_stream_.initialize(
            known_layout_.segments,
            config_.segment_stream_name,
            "vicon_segments_" + hostname,
            nominal_rate);
    } catch (const std::exception& ex) {
        marker_stream_.destroy();
        segment_stream_.destroy();
        last_diagnostic_message_ =
            std::string("Failed to initialize LSL streams: ") + ex.what();
        std::cerr << last_diagnostic_message_ << std::endl;
        return false;
    }
    if (state == BridgeState::Streaming) {
        reportStatus(BridgeState::Streaming, "Layout or frame rate changed, streams reinitialized");
    }
    return true;
}

// Read once per bridge, because macOS can rename the computer while it runs (for
// example after a name clash on the network), and a source ID that changed after
// a reconnect would leave the recorder unable to rejoin the streams.
const std::string& ViconLSLBridge::sourceHost() {
    if (source_host_.empty()) {
        char buffer[256] = {};
        if (gethostname(buffer, sizeof(buffer) - 1) == 0 && buffer[0] != '\0') {
            source_host_ = buffer;
        } else {
            std::cerr << "Failed to resolve local hostname; using default LSL source suffix"
                      << std::endl;
            source_host_ = "default";
        }
    }
    return source_host_;
}

bool ViconLSLBridge::streamFrame(double timestamp) {
    const auto frame = vicon_lsl::buildViconFrame(*client_, known_layout_, frame_count_);
    const StreamPushResult marker_result = marker_stream_.pushSample(frame.markers, timestamp);
    const StreamPushResult segment_result = segment_stream_.pushSample(frame.segments, timestamp);
    handleDiagnostics(frame.diagnostics);
    return marker_result == StreamPushResult::Pushed &&
           segment_result == StreamPushResult::Pushed;
}

// Logs the first timestamp nudge and then every 100th, and reports each
// re-synchronization and step back, since both mean the frame numbers or the
// earlier times disagreed with this computer's clock.
void ViconLSLBridge::reportTimestampCorrections(double resync_seconds,
                                                bool adjusted,
                                                bool stepped_back) {
    if (adjusted && (++timestamp_adjustments_ == 1 ||
                     timestamp_adjustments_ % kStatusIntervalFrames == 0)) {
        std::cerr << "Adjusted non-monotonic Vicon frame timestamp ("
                  << timestamp_adjustments_ << " so far)" << std::endl;
    }
    if (stepped_back) {
        const std::string message =
            "Vicon timestamps had run ahead of this computer's clock and stepped back to it";
        std::cerr << message << std::endl;
        reportStatus(BridgeState::Streaming, message);
    }
    if (resync_seconds == 0.0) return;
    ++timestamp_resyncs_;
    const double now = clock_();
    if (resync_reported_ && std::isfinite(now) &&
        now - last_resync_report_ < kResyncReportIntervalSeconds) {
        return;
    }
    resync_reported_ = true;
    last_resync_report_ = now;
    std::ostringstream message;
    message << "Vicon frame numbers " << (resync_seconds > 0.0 ? "ran ahead of" : "fell behind")
            << " real time by " << std::fixed << std::setprecision(3) << std::abs(resync_seconds)
            << " s; timestamps re-synchronized to arrival time";
    if (timestamp_resyncs_ > 1) message << " (" << timestamp_resyncs_ << " times so far)";
    std::cerr << message.str() << std::endl;
    reportStatus(BridgeState::Streaming, message.str());
}

void ViconLSLBridge::handleDiagnostics(
    const std::vector<vicon_lsl::ViconDiagnostic>& diagnostics,
    BridgeState state) {
    if (diagnostics.empty()) {
        // A clean frame between occlusions is not a recovery, or a flickering
        // marker would be logged afresh every other frame.
        if (!last_diagnostic_message_.empty() && ++clean_frames_ >= kStatusIntervalFrames) {
            clean_frames_ = 0;
            last_diagnostic_message_.clear();
            diagnostic_aggregator_.clear();
            reportStatus(state, "Vicon reads recovered");
        }
        return;
    }

    clean_frames_ = 0;
    const auto emission = diagnostic_aggregator_.record(diagnostics);
    for (const auto& line : emission.log_lines) {
        std::cerr << line << std::endl;
    }

    if (emission.shouldReportStatus()) {
        last_diagnostic_message_ = emission.status_message;
        reportStatus(state, last_diagnostic_message_);
    }
}
