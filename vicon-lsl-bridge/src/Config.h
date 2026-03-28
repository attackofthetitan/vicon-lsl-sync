#pragma once

#include <string>

struct Config {
    std::string vicon_server = "localhost:801";
    std::string marker_stream_name = "ViconMarkers";
    std::string segment_stream_name = "ViconSegments";
    std::string source_id_prefix = "vicon_";
    int reconnect_interval_ms = 3000;
};
