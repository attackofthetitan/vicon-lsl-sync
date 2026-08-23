#pragma once

#include "preview/PreviewTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace vicon_lsl {

// Immutable view of the latest sample held by a live stream worker. Freshness
// and update state are supplied by the caller so this assembly policy remains
// independent of clocks, inlets, and thread ownership.
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

// Marker updates are the master frame clock. When no marker arrives, a fresh
// segment or gaze update anchors a fallback frame, with simultaneous updates
// anchored to the newer timestamp.
std::optional<PreviewFrame> assemblePreviewFrame(const PreviewFrameSnapshot& snapshot);

} // namespace vicon_lsl
