#pragma once

#include "preview/PreviewCsv.h"
#include "preview/PreviewXdfReader.h"

namespace vicon_lsl {

PreviewRecording buildXdfPreviewRecording(const XdfLoadResult& xdf,
                                          const PreviewTransformProfile& vicon_transform,
                                          const PreviewTransformProfile& gaze_transform,
                                          double match_tolerance_seconds,
                                          const PreviewCancel& cancel = {});

PreviewRecording loadXdfPreviewRecording(const std::string& path,
                                         const PreviewTransformProfile& vicon_transform,
                                         const PreviewTransformProfile& gaze_transform,
                                         double match_tolerance_seconds,
                                         const PreviewCancel& cancel = {});

} // namespace vicon_lsl
