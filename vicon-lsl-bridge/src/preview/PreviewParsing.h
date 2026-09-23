#pragma once

#include "preview/PreviewTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace vicon_lsl {

PreviewStreamRole inferPreviewStreamRole(const PreviewStreamSchema& schema);
const char* previewStreamRoleName(PreviewStreamRole role);
// The fixed HoloLens labels for a gaze or target stream of the expected size,
// or nothing for any other stream.
std::vector<std::string> canonicalPreviewChannelLabels(PreviewStreamRole role,
                                                       std::size_t channel_count);
std::vector<PreviewMarker> parseMarkerSample(const std::vector<std::string>& labels,
                                             const std::vector<double>& sample,
                                             const PreviewTransformProfile& transform);
std::vector<PreviewSegment> parseSegmentSample(const std::vector<std::string>& labels,
                                               const std::vector<double>& sample,
                                               const PreviewTransformProfile& transform);
std::vector<PreviewGazeRay> parseGazeSample(const std::vector<std::string>& labels,
                                            const std::vector<double>& sample,
                                            const PreviewTransformProfile& transform);

std::string lowerAscii(std::string value);

} // namespace vicon_lsl
