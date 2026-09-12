#include "ViconLSLBridge.h"
#include "ViconLSLBridgeInternal.h"
#include "ViconClient.h"
#include "ViconFrameMapper.h"

#include <lsl_cpp.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace {

vicon_lsl::bridge_internal::Dependencies liveDependencies(
    const Config& config) {
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
      wait_(std::move(dependencies.wait)) {
    if (!client_) {
        throw std::invalid_argument("Vicon bridge needs a client");
    }
    if (!clock_) {
        throw std::invalid_argument("Vicon bridge needs a clock");
    }
    if (!wait_) {
        throw std::invalid_argument("Vicon bridge needs a wait function");
    }
}

std::unique_ptr<ViconLSLBridge> vicon_lsl::bridge_internal::BridgeTestAccess::create(
    const Config& config,
    Dependencies dependencies) {
    return std::unique_ptr<ViconLSLBridge>(
        new ViconLSLBridge(config, std::move(dependencies)));
}

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
        status.message = message.empty() ? last_diagnostic_message_ : message;
        status_callback_(status);
    }
}

void ViconLSLBridge::stop() {
    running_ = false;
}

void ViconLSLBridge::run() {
    // LabRecorder can recover a recreated outlet by source_id and continue it
    // as the same XDF stream. Keep the timestamp guard alive across Vicon
    // reconnects so that recovered samples never move backwards in that stream.
    vicon_lsl::ViconTimestampState timestamp_state;
    bool previous_first_frame_failed = false;
    while (running_) {
        connectWithRetry();
        if (!running_) {
            if (client_->isConnected()) client_->disconnect();
            break;
        }

        if (!client_->getFrame()) {
            std::cerr << "Failed to get initial frame, reconnecting" << std::endl;
            reportStatus(BridgeState::Connecting, "Failed to get initial frame, reconnecting");
            client_->disconnect();
            // Allow one immediate retry while the server starts up.
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

    reportStatus(BridgeState::Stopped, "Stopped");
    std::cout << "Stopped" << std::endl;
}

void ViconLSLBridge::streamFrames(vicon_lsl::ViconTimestampState& timestamp_state) {
    std::cout << "Streaming started" << std::endl;
    reportStatus(BridgeState::Streaming, "Streaming started");
    while (running_ && client_->isConnected()) {
        if (!client_->getFrame()) {
            std::cerr << "Lost connection, will reconnect" << std::endl;
            reportStatus(BridgeState::Connecting, "Lost connection, will reconnect");
            return;
        }
        frame_count_ = client_->frameNumber();

        double timestamp = 0.0;
        bool timestamp_adjusted = false;
        if (!vicon_lsl::enforceViconTimestamp(
                client_->frameTimestamp(),
                clock_(),
                timestamp_state,
                timestamp,
                &timestamp_adjusted)) {
            std::cerr << "Unable to obtain a finite Vicon frame timestamp" << std::endl;
            continue;
        }
        if (timestamp_adjusted) {
            std::cerr << "Adjusted non-monotonic Vicon frame timestamp" << std::endl;
        }
        if (!streamFrame(timestamp)) {
            reportStatus(BridgeState::Connecting,
                         "LSL outlet failed; reconnecting and recreating streams");
            return;
        }

        if (++frames_since_layout_check_ >= 100) {
            frames_since_layout_check_ = 0;
            reportStatus(BridgeState::Streaming);
            if (!refreshStreams(BridgeState::Streaming)) {
                reportStatus(BridgeState::Connecting, last_diagnostic_message_);
                return;
            }
        }
    }
}

void ViconLSLBridge::resetConnectedSession() {
    marker_stream_.destroy();
    segment_stream_.destroy();
    client_->disconnect();
    frame_count_ = 0;
    frames_since_layout_check_ = 0;
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

void ViconLSLBridge::waitForRetry() {
    int remaining_ms = config_.reconnect_interval_ms;
    while (running_ && remaining_ms > 0) {
        const int sleep_ms = (std::min)(remaining_ms, 100);
        wait_(std::chrono::milliseconds(sleep_ms));
        remaining_ms -= sleep_ms;
    }
}

bool ViconLSLBridge::refreshStreams(BridgeState state) {
    const auto discovery = vicon_lsl::discoverLayout(*client_, frame_count_);
    if (!discovery.ok()) {
        handleDiagnostics(discovery.diagnostics, state);
        // A failed periodic check leaves the working streams in place.
        return state == BridgeState::Streaming;
    }
    if (state == BridgeState::Streaming) {
        if (discovery.layout == known_layout_) return true;
        std::cout << "Layout changed, reinitializing streams" << std::endl;
        marker_stream_.destroy();
        segment_stream_.destroy();
    }

    known_layout_ = discovery.layout;
    diagnostic_aggregator_.clear();
    last_diagnostic_message_.clear();

    std::cout << "Discovered " << known_layout_.markers.size() << " markers and "
              << known_layout_.segments.size() << " segments" << std::endl;

    const double nominal_rate = client_->frameRate();
    if (std::isfinite(nominal_rate) && nominal_rate > 0.0) {
        std::cout << "Vicon frame rate: " << nominal_rate << " Hz" << std::endl;
    } else {
        std::cerr << "Vicon frame rate unavailable; publishing irregular-rate streams"
                  << std::endl;
    }

    std::string hostname = "default";
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        hostname = buffer;
    } else {
        std::cerr << "Failed to resolve local hostname; using default LSL source suffix"
                  << std::endl;
    }

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
        reportStatus(BridgeState::Streaming, "Layout changed, streams reinitialized");
    }
    return true;
}

bool ViconLSLBridge::streamFrame(double timestamp) {
    const auto frame = vicon_lsl::buildViconFrame(*client_, known_layout_, frame_count_);
    const StreamPushResult marker_result = marker_stream_.pushSample(frame.markers, timestamp);
    const StreamPushResult segment_result = segment_stream_.pushSample(frame.segments, timestamp);
    handleDiagnostics(frame.diagnostics);
    return marker_result == StreamPushResult::Pushed &&
           segment_result == StreamPushResult::Pushed;
}

void ViconLSLBridge::handleDiagnostics(
    const std::vector<vicon_lsl::ViconDiagnostic>& diagnostics,
    BridgeState state) {
    if (diagnostics.empty()) {
        if (!last_diagnostic_message_.empty()) {
            last_diagnostic_message_.clear();
            diagnostic_aggregator_.clear();
            reportStatus(state, "Vicon reads recovered");
        }
        return;
    }

    const auto emission = diagnostic_aggregator_.record(diagnostics);
    for (const auto& line : emission.log_lines) {
        std::cerr << line << std::endl;
    }

    if (emission.shouldReportStatus()) {
        last_diagnostic_message_ = emission.status_message;
        reportStatus(state, last_diagnostic_message_);
    }
}
