#pragma once

#include "Config.h"
#include "HoloLensGazeReceiver.h"
#include "MarkerStream.h"
#include "SegmentStream.h"
#include "ViconClient.h"
#include "ViconFrameMapper.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

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
    bool gaze_enabled = false;
    bool gaze_listening = false;
    unsigned long long gaze_sample_count = 0;
    unsigned long long gaze_malformed_packet_count = 0;
    std::string gaze_last_error;
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
    void connectWithRetry();
    bool initializeStreams();
    bool checkLayoutChanged();
    void streamFrame(double timestamp);
    void reportStatus(BridgeState state, const std::string& message = "");
    void handleDiagnostics(const std::vector<vicon_lsl::ViconDiagnostic>& diagnostics,
                           BridgeState state = BridgeState::Streaming);

    Config config_;
    ViconClient client_;
    MarkerStream marker_stream_;
    SegmentStream segment_stream_;
    HoloLensGazeReceiver hololens_gaze_receiver_;
    std::atomic<bool> running_{true};
    std::atomic<BridgeState> current_state_{BridgeState::Disconnected};
    StatusCallback status_callback_;

    vicon_lsl::ViconLayout known_layout_;
    unsigned int frame_count_ = 0;
    unsigned int frames_since_layout_check_ = 0;
    vicon_lsl::DiagnosticAggregator diagnostic_aggregator_;
    std::string last_diagnostic_message_;
};
