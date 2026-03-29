#pragma once

#include "Config.h"
#include "ViconClient.h"
#include "MarkerStream.h"
#include "SegmentStream.h"

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
    void initializeStreams();
    bool checkLayoutChanged();
    void streamFrame(double timestamp);
    void reportStatus(BridgeState state, const std::string& message = "");

    Config config_;
    ViconClient client_;
    MarkerStream marker_stream_;
    SegmentStream segment_stream_;
    std::atomic<bool> running_{true};
    StatusCallback status_callback_;

    // Cached layout for change detection
    std::vector<std::pair<std::string, std::string>> known_markers_;
    std::vector<std::pair<std::string, std::string>> known_segments_;
    unsigned int frame_count_ = 0;
};
