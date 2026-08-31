#pragma once

#include "preview/PreviewTypes.h"
#include "preview/PreviewLoad.h"

#include <cstdint>
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
    std::string hostname;
    std::string session_id;
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
    std::size_t repaired_timestamp_count = 0;
    std::size_t stored_sample_stride = 1;
    double start_timestamp = 0.0;
    double end_timestamp = 0.0;
    double maximum_sample_gap = 0.0;
    std::size_t large_gap_count = 0;
    std::vector<double> pending_last_sample;
    double pending_last_timestamp = 0.0;
    bool have_pending_last_sample = false;
};

struct XdfLoadResult {
    std::vector<XdfStreamData> streams;
    bool truncated_tail_ignored = false;
    std::uint64_t file_size_bytes = 0;
    std::size_t estimated_memory_bytes = 0;
};

XdfLoadResult loadXdfNumericStreams(const std::string& path,
                                    const PreviewLoadOptions& options = {});

} // namespace vicon_lsl
