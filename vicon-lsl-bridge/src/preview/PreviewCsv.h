#pragma once

#include "preview/PreviewTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace vicon_lsl {

struct PreviewRecording {
    std::vector<PreviewFrame> frames;
    std::string summary;
};

using PreviewCancel = std::function<bool()>;

PreviewRecording loadMergedPreviewCsv(const std::string& path,
                                      const PreviewTransformProfile& vicon_transform,
                                      const PreviewTransformProfile& gaze_transform,
                                      const PreviewCancel& cancel = {});

} // namespace vicon_lsl
