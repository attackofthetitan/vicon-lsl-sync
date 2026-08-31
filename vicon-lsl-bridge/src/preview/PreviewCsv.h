#pragma once

#include "preview/PreviewTypes.h"
#include "preview/PreviewLoad.h"

#include <string>
#include <vector>

namespace vicon_lsl {

struct PreviewRecording {
    std::vector<PreviewFrame> frames;
    std::string summary;
    std::size_t source_frame_count = 0;
    std::size_t stored_frame_stride = 1;
    std::size_t estimated_memory_bytes = 0;
    double source_start_timestamp = 0.0;
    double source_end_timestamp = 0.0;
};

std::size_t estimatePreviewRecordingBytes(const PreviewRecording& recording);
void appendBoundedPreviewFrame(PreviewRecording& recording,
                               PreviewFrame frame,
                               std::size_t maximum_frames,
                               std::size_t maximum_memory_bytes);
void boundPreviewRecordingCache(PreviewRecording& recording,
                                std::size_t maximum_frames,
                                std::size_t maximum_memory_bytes);

PreviewRecording loadMergedPreviewCsv(const std::string& path,
                                      const PreviewTransformProfile& vicon_transform,
                                      const PreviewTransformProfile& gaze_transform,
                                      const PreviewLoadOptions& options = {});

} // namespace vicon_lsl
