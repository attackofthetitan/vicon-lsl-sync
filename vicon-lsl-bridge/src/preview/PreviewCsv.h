#pragma once

#include "preview/PreviewTypes.h"

#include <string>
#include <vector>

namespace vicon_lsl {

struct PreviewRecording {
    std::vector<PreviewFrame> frames;
    std::string summary;
};

PreviewRecording loadMergedPreviewCsv(const std::string& path,
                                      const PreviewTransformProfile& vicon_transform,
                                      const PreviewTransformProfile& gaze_transform);

} // namespace vicon_lsl
