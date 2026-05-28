#pragma once

#include <string>

struct Config {
    std::string vicon_server = "localhost:801";
    std::string marker_stream_name = "ViconMarkers";
    std::string segment_stream_name = "ViconSegments";
    int reconnect_interval_ms = 3000;

    bool enable_hololens_gaze = true;
    unsigned short hololens_gaze_port = 16571;
    std::string hololens_gaze_stream_name = "HoloLensGaze";
};
