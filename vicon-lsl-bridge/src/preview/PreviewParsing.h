#pragma once

#include "preview/PreviewTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace vicon_lsl {

PreviewStreamRole inferPreviewStreamRole(const PreviewStreamSchema& schema);
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

} // namespace vicon_lsl
