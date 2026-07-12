#pragma once

#include "ViconFrameMapper.h"
#include "StreamPushResult.h"
#include "StreamOutlet.h"

#include <lsl_cpp.h>
#include <memory>
#include <string>
#include <vector>

class SegmentStream {
public:
    explicit SegmentStream(StreamOutletFactory outlet_factory = createLslStreamOutlet);

    // segment_names: vector of (subject, segment) pairs
    void initialize(const std::vector<std::pair<std::string, std::string>>& segment_names,
                    const std::string& stream_name,
                    const std::string& source_id,
                    double nominal_rate = lsl::IRREGULAR_RATE);
    void destroy();

    // Converts status-bearing reads to fixed-shape LSL samples at the outlet boundary.
    StreamPushResult pushSample(const std::vector<vicon_lsl::SegmentObjectRead>& segments,
                                double timestamp);
    bool isInitialized() const;

private:
    StreamOutletFactory outlet_factory_;
    std::unique_ptr<StreamOutlet> outlet_;
    std::unique_ptr<lsl::stream_info> info_;
    std::vector<std::pair<std::string, std::string>> segment_names_;
    std::vector<double> sample_buffer_;
    bool configured_ = false;
};
