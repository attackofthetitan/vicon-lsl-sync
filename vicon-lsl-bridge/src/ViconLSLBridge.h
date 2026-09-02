#pragma once

#include "Config.h"
#include "MarkerStream.h"
#include "SegmentStream.h"
#include "ViconDiagnostics.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vicon_lsl {

struct ViconTimestampState;

namespace bridge_internal {

class BridgeTestAccess;
class ViconClient;
struct Collaborators;
enum class ConnectedSessionEnd;

} // namespace bridge_internal
} // namespace vicon_lsl

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

class ViconLSLBridge {
public:
    using StatusCallback = std::function<void(const BridgeStatus&)>;

    explicit ViconLSLBridge(const Config& config);
    void run();
    void stop();
    void setStatusCallback(StatusCallback callback);

private:
    friend class vicon_lsl::bridge_internal::BridgeTestAccess;

    ViconLSLBridge(const Config& config,
                   vicon_lsl::bridge_internal::Collaborators collaborators);
    void connectWithRetry();
    void waitForRetry();
    vicon_lsl::bridge_internal::ConnectedSessionEnd runConnectedSession(
        vicon_lsl::ViconTimestampState& timestamp_state);
    void resetConnectedSession(
        vicon_lsl::bridge_internal::ConnectedSessionEnd end_reason);
    bool initializeStreams();
    bool checkLayoutChanged();
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
    unsigned int consecutive_initial_frame_failures_ = 0;
    vicon_lsl::DiagnosticAggregator diagnostic_aggregator_;
    std::string last_diagnostic_message_;
};
