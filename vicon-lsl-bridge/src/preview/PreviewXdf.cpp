#include "preview/PreviewXdf.h"

#include "preview/PreviewXdfPrivate.h"

namespace vicon_lsl {

PreviewRecording buildXdfPreviewRecording(const XdfLoadResult& xdf,
                                          const PreviewTransformProfile& vicon_transform,
                                          const PreviewTransformProfile& gaze_transform,
                                          double match_tolerance_seconds) {
    return preview_xdf_detail::assembleRecording(xdf,
                                                 vicon_transform,
                                                 gaze_transform,
                                                 match_tolerance_seconds);
}

PreviewRecording loadXdfPreviewRecording(const std::string& path,
                                         const PreviewTransformProfile& vicon_transform,
                                         const PreviewTransformProfile& gaze_transform,
                                         double match_tolerance_seconds) {
    return buildXdfPreviewRecording(loadXdfNumericStreams(path),
                                    vicon_transform,
                                    gaze_transform,
                                    match_tolerance_seconds);
}

} // namespace vicon_lsl
