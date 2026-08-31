#pragma once

#include "preview/PreviewTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vicon_lsl {

struct XdfClockOffset {
    double stream_time = 0.0;
    double offset = 0.0;
};

struct XdfStreamData {
    std::uint32_t stream_id = 0;
    std::string name;
    std::string type;
    std::string source_id;
    std::string channel_format;
    std::string coordinate_frame;
    int channel_count = 0;
    double nominal_srate = 0.0;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    bool numeric = true;
    std::size_t sample_count = 0;
    std::vector<std::string> channel_labels;
    std::vector<XdfClockOffset> clock_offsets;
    std::vector<double> timestamps;
    std::vector<std::vector<double>> samples;
    std::size_t stored_sample_stride = 1;
    std::size_t repaired_timestamp_count = 0;
};

struct XdfLoadResult {
    std::vector<XdfStreamData> streams;
    bool truncated_tail_ignored = false;
};

XdfLoadResult loadXdfNumericStreams(
    const std::string& path,
    const std::function<bool()>& cancel = {});

} // namespace vicon_lsl
