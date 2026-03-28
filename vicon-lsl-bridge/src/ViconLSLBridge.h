#pragma once

#include "Config.h"
#include "ViconClient.h"
#include "MarkerStream.h"
#include "SegmentStream.h"

#include <atomic>
#include <string>
#include <vector>

class ViconLSLBridge {
public:
    explicit ViconLSLBridge(const Config& config);
    void run();
    void stop();

private:
    void connectWithRetry();
    void initializeStreams();
    bool checkLayoutChanged();
    void streamFrame(double timestamp);

    Config config_;
    ViconClient client_;
    MarkerStream marker_stream_;
    SegmentStream segment_stream_;
    std::atomic<bool> running_{true};

    // Cached layout for change detection
    std::vector<std::pair<std::string, std::string>> known_markers_;
    std::vector<std::pair<std::string, std::string>> known_segments_;
    unsigned int frame_count_ = 0;
};
