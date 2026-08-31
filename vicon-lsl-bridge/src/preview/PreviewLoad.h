#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace vicon_lsl {

enum class PreviewLoadStage {
    Reading,
    Indexing,
    StreamDetails,
    Timestamps,
    Calibration,
    FramePreparation,
    Complete,
};

struct PreviewLoadProgress {
    PreviewLoadStage stage = PreviewLoadStage::Reading;
    std::uint64_t completed = 0;
    std::uint64_t total = 0;
    std::string detail;
};

struct PreviewLoadOptions {
    std::uint64_t maximum_file_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
    std::uint64_t maximum_samples_per_stream = 100000000ULL;
    std::size_t maximum_memory_bytes = 128ULL * 1024ULL * 1024ULL;
    std::size_t maximum_preview_frames = 200000;
    std::size_t maximum_stored_values_per_stream = 2000000;
    std::size_t maximum_columns = 65536;
    std::size_t maximum_line_bytes = 16ULL * 1024ULL * 1024ULL;
    std::size_t cancellation_check_sample_interval = 1024;
    int maximum_streams = 4096;
    int maximum_channels = 65536;
    int maximum_header_bytes = 4 * 1024 * 1024;
    std::function<bool()> cancel_requested;
    std::function<void(const PreviewLoadProgress&)> progress;
};

const char* previewLoadStageName(PreviewLoadStage stage);

} // namespace vicon_lsl
