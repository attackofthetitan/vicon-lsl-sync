#pragma once

#include "ViconFrameTypes.h"
#include "detail/ViconOutlet.h"

#include <lsl_cpp.h>
#include <string>
#include <vector>

// An empty layout creates no LSL stream.
class SegmentStream {
public:
    explicit SegmentStream(StreamOutletFactory outlet_factory = createLslStreamOutlet);

    // Replaces any open stream. Each name is a (subject, segment) pair.
    void initialize(const std::vector<vicon_lsl::NamedViconItem>& segment_names,
                    const std::string& stream_name,
                    const std::string& source_id,
                    double nominal_rate = lsl::IRREGULAR_RATE);
    void destroy();

    // Sends position and rotation for each segment; unavailable poses become NaN.
    StreamPushResult pushSample(const std::vector<vicon_lsl::SegmentPoseRead>& segments,
                                double timestamp);
    bool isInitialized() const;

private:
    vicon_lsl::detail::ViconOutlet outlet_;
};
