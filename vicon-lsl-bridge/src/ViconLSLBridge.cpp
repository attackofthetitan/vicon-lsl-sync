#include "ViconLSLBridge.h"

#include <lsl_cpp.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <iostream>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr const char* kSourceIdPrefix = "vicon_";

} // namespace

ViconLSLBridge::ViconLSLBridge(const Config& config)
    : config_(config),
      client_(config.vicon_server) {}

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
    while (running_) {
        connectWithRetry();
        if (!running_) {
            break;
        }

        if (!client_.getFrame()) {
            std::cerr << "Failed to get initial frame, reconnecting" << std::endl;
            reportStatus(BridgeState::Connecting, "Failed to get initial frame, reconnecting");
            client_.disconnect();
            continue;
        }
        frame_count_ = client_.frameNumber();

        if (!initializeStreams()) {
            reportStatus(BridgeState::Connecting, last_diagnostic_message_);
            client_.disconnect();
            waitForRetry();
            continue;
        }

        std::cout << "Streaming started" << std::endl;
        reportStatus(BridgeState::Streaming, "Streaming started");
        vicon_lsl::ViconTimestampState timestamp_state;
        while (running_ && client_.isConnected()) {
            if (!client_.getFrame()) {
                std::cerr << "Lost connection, will reconnect" << std::endl;
                reportStatus(BridgeState::Connecting, "Lost connection, will reconnect");
                break;
            }
            frame_count_ = client_.frameNumber();

            double timestamp = 0.0;
            bool timestamp_adjusted = false;
            if (!vicon_lsl::enforceViconTimestamp(
                    client_.frameTimestamp(),
                    lsl::local_clock(),
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
                break;
            }

            if (++frames_since_layout_check_ >= 100) {
                frames_since_layout_check_ = 0;
                reportStatus(BridgeState::Streaming);
                if (checkLayoutChanged()) {
                    std::cout << "Layout changed, reinitializing streams" << std::endl;
                    marker_stream_.destroy();
                    segment_stream_.destroy();
                    if (!initializeStreams()) {
                        reportStatus(BridgeState::Connecting, last_diagnostic_message_);
                        break;
                    }
                    reportStatus(BridgeState::Streaming, "Layout changed, streams reinitialized");
                }
            }
        }

        marker_stream_.destroy();
        segment_stream_.destroy();
        client_.disconnect();
        frame_count_ = 0;
        frames_since_layout_check_ = 0;
        known_layout_ = {};
        diagnostic_aggregator_.clear();
        last_diagnostic_message_.clear();
        reportStatus(BridgeState::Disconnected, "Disconnected");
        if (running_) {
            waitForRetry();
        }
    }

    reportStatus(BridgeState::Stopped, "Stopped");
    std::cout << "Stopped" << std::endl;
}

void ViconLSLBridge::connectWithRetry() {
    reportStatus(BridgeState::Connecting, "Connecting to " + config_.vicon_server);
    while (running_ && !client_.connect()) {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        remaining_ms -= sleep_ms;
    }
}

bool ViconLSLBridge::initializeStreams() {
    const auto discovery = vicon_lsl::discoverLayout(client_, frame_count_);
    if (!discovery.ok()) {
        handleDiagnostics(discovery.diagnostics, BridgeState::Connecting);
        return false;
    }

    known_layout_ = discovery.layout;
    diagnostic_aggregator_.clear();
    last_diagnostic_message_.clear();

    std::cout << "Discovered " << known_layout_.markers.size() << " markers and "
              << known_layout_.segments.size() << " segments" << std::endl;

    const double nominal_rate = client_.frameRate();
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
            vicon_lsl::buildStreamSourceId(kSourceIdPrefix, "markers", hostname),
            nominal_rate);
        segment_stream_.initialize(
            known_layout_.segments,
            config_.segment_stream_name,
            vicon_lsl::buildStreamSourceId(kSourceIdPrefix, "segments", hostname),
            nominal_rate);
    } catch (const std::exception& ex) {
        marker_stream_.destroy();
        segment_stream_.destroy();
        last_diagnostic_message_ =
            std::string("Failed to initialize LSL streams: ") + ex.what();
        std::cerr << last_diagnostic_message_ << std::endl;
        return false;
    }
    return true;
}

bool ViconLSLBridge::checkLayoutChanged() {
    const auto discovery = vicon_lsl::discoverLayout(client_, frame_count_);
    if (!discovery.ok()) {
        handleDiagnostics(discovery.diagnostics);
        return false;
    }
    return vicon_lsl::layoutChanged(discovery.layout, known_layout_);
}

bool ViconLSLBridge::streamFrame(double timestamp) {
    const auto frame = vicon_lsl::buildViconFrame(client_, known_layout_, frame_count_);
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
