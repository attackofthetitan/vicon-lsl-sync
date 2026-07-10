#pragma once

#include "preview/PreviewCsv.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vicon_lsl {

struct XdfClockOffset {
    double collection_time = 0.0;
    double offset = 0.0;
};

struct XdfStreamData {
    std::uint32_t stream_id = 0;
    std::string name;
    std::string type;
    std::string source_id;
    std::string channel_format;
    int channel_count = 0;
    double nominal_srate = 0.0;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    bool numeric = true;
    std::size_t sample_count = 0;
    std::vector<std::string> channel_labels;
    std::vector<XdfClockOffset> clock_offsets;
    std::vector<double> timestamps;
    std::vector<std::vector<double>> samples;
};

struct XdfLoadResult {
    std::vector<XdfStreamData> streams;
};

XdfLoadResult loadXdfNumericStreams(const std::string& path);

PreviewRecording loadXdfPreviewRecording(const std::string& path,
                                         const PreviewTransformProfile& vicon_transform,
                                         const PreviewTransformProfile& gaze_transform,
                                         double match_tolerance_seconds);

} // namespace vicon_lsl
