#pragma once

#include <lsl_cpp.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

class SegmentStream {
public:
    // segment_names: vector of (subject, segment) pairs
    void initialize(const std::vector<std::pair<std::string, std::string>>& segment_names,
                    const std::string& stream_name,
                    const std::string& source_id);
    void destroy();
    bool isInitialized() const;

    // Push one sample for all segments. Each entry: {x, y, z, qx, qy, qz, qw}
    void pushSample(const std::vector<std::array<double, 7>>& segments, double timestamp);

    size_t segmentCount() const;

private:
    std::unique_ptr<lsl::stream_outlet> outlet_;
    std::unique_ptr<lsl::stream_info> info_;
    std::vector<std::pair<std::string, std::string>> segment_names_;
    std::vector<double> sample_buffer_;
};
