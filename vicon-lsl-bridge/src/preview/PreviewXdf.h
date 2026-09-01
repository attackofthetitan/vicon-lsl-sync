#pragma once

#include "preview/PreviewCsv.h"
#include "preview/PreviewXdfReader.h"
#include "preview/PreviewXdfMapping.h"

namespace vicon_lsl {

PreviewRecording buildXdfPreviewRecording(const XdfLoadResult& xdf,
                                          const PreviewTransformProfile& vicon_transform,
                                          const PreviewTransformProfile& gaze_transform,
                                          double match_tolerance_seconds,
                                          const XdfStreamMapping& mapping = {},
                                          const PreviewLoadOptions& options = {});

PreviewRecording loadXdfPreviewRecording(const std::string& path,
                                         const PreviewTransformProfile& vicon_transform,
                                         const PreviewTransformProfile& gaze_transform,
                                         double match_tolerance_seconds,
                                         const PreviewLoadOptions& options = {},
                                         const XdfStreamMapping& mapping = {});

} // namespace vicon_lsl
