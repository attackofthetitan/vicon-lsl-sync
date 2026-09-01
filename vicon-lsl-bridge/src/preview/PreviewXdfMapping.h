#pragma once

#include "preview/PreviewXdfReader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vicon_lsl {

struct XdfStreamCandidate {
    std::uint32_t stream_id = 0;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    std::string group_key;
    std::string display_name;
    std::string source_id;
    std::string hostname;
    std::string session_id;
    std::size_t sample_count = 0;
    double start_timestamp = 0.0;
    double end_timestamp = 0.0;
};

struct XdfStreamMapping {
    std::uint32_t master_stream_id = 0;
    std::vector<std::uint32_t> selected_stream_ids;
};

struct XdfMappingAnalysis {
    std::vector<XdfStreamCandidate> candidates;
    XdfStreamMapping suggested_mapping;
    bool requires_explicit_mapping = false;
    std::string explanation;
};

XdfMappingAnalysis analyzeXdfStreamMapping(const XdfLoadResult& xdf);
XdfLoadResult applyXdfStreamMapping(const XdfLoadResult& xdf,
                                    const XdfStreamMapping& mapping,
                                    std::size_t maximum_samples_per_stream = 200000);

} // namespace vicon_lsl
