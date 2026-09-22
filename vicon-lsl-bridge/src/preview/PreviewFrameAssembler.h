#pragma once

#include "preview/PreviewTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace vicon_lsl {

// The newest sample from one live stream. The caller decides whether it is fresh
// and whether it just arrived, so this code needs no clock.
struct PreviewStreamSnapshot {
    const std::vector<std::string>& labels;
    const std::vector<double>& sample;
    const PreviewTransformProfile& transform;
    double timestamp = 0.0;
    bool connected = false;
    bool fresh = false;
    bool updated = false;
};

struct PreviewFrameSnapshot {
    const PreviewStreamSnapshot& markers;
    const PreviewStreamSnapshot& segments;
    const PreviewStreamSnapshot& gaze;
    double match_tolerance_seconds = 0.0;
};

// Builds a frame when a stream has a new sample. A new marker sample sets the
// frame time. Without one, a new segment or gaze sample sets it, using the later
// time when both arrive. Other streams join only when fresh and close in time.
std::optional<PreviewFrame> assemblePreviewFrame(const PreviewFrameSnapshot& snapshot);

} // namespace vicon_lsl
