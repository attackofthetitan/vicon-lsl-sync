#pragma once

// Keep the mapper umbrella transitively available as it was before the
// implementation split; existing source consumers may include only this
// facade.
#include "ViconFrameMapper.h"
#include "detail/ViconNumericOutlet.h"

#include <lsl_cpp.h>
#include <memory>
#include <string>
#include <vector>

class MarkerStream {
public:
    explicit MarkerStream(StreamOutletFactory outlet_factory = createLslStreamOutlet);

    // marker_names: vector of (subject, marker) pairs
    void initialize(const std::vector<std::pair<std::string, std::string>>& marker_names,
                    const std::string& stream_name,
                    const std::string& source_id,
                    double nominal_rate = lsl::IRREGULAR_RATE);
    void destroy();

    // Converts status-bearing reads to fixed-shape LSL samples at the outlet boundary.
    StreamPushResult pushSample(const std::vector<vicon_lsl::MarkerObjectRead>& markers,
                                double timestamp);
    bool isInitialized() const;

private:
    vicon_lsl::detail::ViconNumericOutlet outlet_;
};
