#pragma once

#include <string>
#include "StreamDefaults.h"

struct Config {
    std::string vicon_server = "localhost:801";
    std::string marker_stream_name = vicon_lsl::stream_defaults::ViconMarkers;
    std::string segment_stream_name = vicon_lsl::stream_defaults::ViconSegments;
    int reconnect_interval_ms = 3000;
};
