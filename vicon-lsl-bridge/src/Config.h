#pragma once

#include <string>

struct Config {
    std::string vicon_server = "localhost:801";
    std::string marker_stream_name = "ViconMarkers";
    std::string segment_stream_name = "ViconSegments";
    int reconnect_interval_ms = 3000;
};
