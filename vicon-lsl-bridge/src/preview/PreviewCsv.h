#pragma once

#include "preview/PreviewTypes.h"
#include "preview/PreviewLoad.h"

#include <string>
#include <vector>

namespace vicon_lsl {

// Frames ready for playback, where long recordings keep every Nth frame to fit
// the memory limit but the source counts and times stay exact.
struct PreviewRecording {
    std::vector<PreviewFrame> frames;
    std::string summary;
    std::string calibration_warning;
    std::size_t source_frame_count = 0;
    std::size_t stored_frame_stride = 1;
    std::size_t estimated_memory_bytes = 0;
    double source_start_timestamp = 0.0;
    double source_end_timestamp = 0.0;
};

std::size_t estimatePreviewRecordingBytes(const PreviewRecording& recording);
// Adds a frame, then drops every other stored frame while over either limit.
void appendBoundedPreviewFrame(PreviewRecording& recording,
                               PreviewFrame frame,
                               std::size_t maximum_frames,
                               std::size_t maximum_memory_bytes);
// Drops every other stored frame until both limits are met.
void boundPreviewRecordingCache(PreviewRecording& recording,
                                std::size_t maximum_frames,
                                std::size_t maximum_memory_bytes);

PreviewRecording loadMergedPreviewCsv(const std::string& path,
                                      const PreviewTransformProfile& vicon_transform,
                                      const PreviewTransformProfile& gaze_transform,
                                      const PreviewLoadOptions& options = {});

} // namespace vicon_lsl
