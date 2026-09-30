#pragma once

#include "Config.h"
#include "MarkerStream.h"
#include "SegmentStream.h"
#include "ViconFrameMapper.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vicon_lsl::bridge_internal {
class ViconClient;
struct Dependencies;
} // namespace vicon_lsl::bridge_internal

enum class BridgeState {
    Disconnected,
    Connecting,
    Streaming,
    Stopped
};

struct BridgeStatus {
    BridgeState state = BridgeState::Disconnected;
    size_t marker_count = 0;
    size_t segment_count = 0;
    unsigned int frame_count = 0;
    std::string message;
};

// Connects to Vicon, sends the marker and segment streams, and reconnects after
// errors until stop() is called.
class ViconLSLBridge {
public:
    using StatusCallback = std::function<void(const BridgeStatus&)>;

    explicit ViconLSLBridge(const Config& config);
    // Tests use this to supply a fake Vicon client, outlets, clock, and sleep.
    ViconLSLBridge(const Config& config,
                   vicon_lsl::bridge_internal::Dependencies dependencies);

    void run();
    // Only asks run() to finish, and run() closes the streams and connection itself.
    void stop();
    void setStatusCallback(StatusCallback callback);

private:
    void connectWithRetry();
    void waitForRetry();
    void streamFrames(vicon_lsl::ViconTimestampState& timestamp_state);
    void resetConnectedSession();
    bool refreshStreams(BridgeState state);
    bool streamFrame(double timestamp);
    void reportStatus(BridgeState state, const std::string& message = "");
    void handleDiagnostics(const std::vector<vicon_lsl::ViconDiagnostic>& diagnostics,
                           BridgeState state = BridgeState::Streaming);

    Config config_;
    std::shared_ptr<vicon_lsl::bridge_internal::ViconClient> client_;
    MarkerStream marker_stream_;
    SegmentStream segment_stream_;
    std::function<double()> clock_;
    std::function<void(std::chrono::milliseconds)> wait_;
    std::atomic<bool> running_{true};
    StatusCallback status_callback_;

    vicon_lsl::ViconLayout known_layout_;
    unsigned int frame_count_ = 0;
    unsigned int frames_since_layout_check_ = 0;
    vicon_lsl::DiagnosticAggregator diagnostic_aggregator_;
    std::string last_diagnostic_message_;
};
