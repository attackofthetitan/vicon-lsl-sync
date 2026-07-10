#pragma once

#include <array>
#include <string>
#include <vector>

namespace vicon_lsl {

struct PreviewVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct PreviewQuaternion {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

struct PreviewTransformProfile {
    std::string name;
    bool enabled = true;
    double scale = 1.0;
    PreviewVec3 rotation_degrees{};
    // Automatic calibration uses a quaternion so a solved rigid transform is not
    // degraded by an Euler-angle round trip. Manual controls keep using Euler.
    bool use_quaternion_rotation = false;
    PreviewQuaternion rotation{};
    PreviewVec3 translation{};
};

struct PreviewMarker {
    std::string name;
    PreviewVec3 position{};
    bool valid = false;
};

struct PreviewSegment {
    std::string name;
    PreviewVec3 position{};
    PreviewQuaternion rotation{};
    bool valid = false;
};

struct PreviewGazeRay {
    std::string name;
    PreviewVec3 origin{};
    PreviewVec3 direction{};
    bool valid = false;
};

struct PreviewFrame {
    double timestamp = 0.0;
    std::vector<PreviewMarker> markers;
    std::vector<PreviewSegment> segments;
    std::vector<PreviewGazeRay> gaze_rays;
    bool marker_stream_present = false;
    bool segment_stream_present = false;
    bool gaze_stream_present = false;
};

enum class PreviewStreamRole {
    Unknown,
    ViconMarkers,
    ViconSegments,
    HoloLensGaze,
};

struct PreviewStreamSchema {
    std::string name;
    std::string type;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    std::vector<std::string> channel_labels;
};

struct PreviewTriangle {
    PreviewVec3 a;
    PreviewVec3 b;
    PreviewVec3 c;
};

struct PreviewMesh {
    std::vector<PreviewVec3> vertices;
    std::vector<std::vector<unsigned int>> faces;
};

} // namespace vicon_lsl
