#pragma once

#include "ViconFrameTypes.h"
#include "detail/ViconNumericOutlet.h"

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
    StreamPushResult pushSample(const std::vector<vicon_lsl::SegmentPoseRead>& segments,
                                double timestamp);
    bool isInitialized() const;

private:
    vicon_lsl::detail::ViconNumericOutlet outlet_;
};
