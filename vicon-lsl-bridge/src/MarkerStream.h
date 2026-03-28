#pragma once

#include <lsl_cpp.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

class MarkerStream {
public:
    // marker_names: vector of (subject, marker) pairs
    void initialize(const std::vector<std::pair<std::string, std::string>>& marker_names,
                    const std::string& stream_name,
                    const std::string& source_id);
    void destroy();
    bool isInitialized() const;

    // Push one sample for all markers. Each entry: {x, y, z, valid}
    void pushSample(const std::vector<std::array<double, 4>>& markers, double timestamp);

    size_t markerCount() const;

private:
    std::unique_ptr<lsl::stream_outlet> outlet_;
    std::unique_ptr<lsl::stream_info> info_;
    std::vector<std::pair<std::string, std::string>> marker_names_;
    std::vector<double> sample_buffer_;
};
