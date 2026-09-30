#pragma once

#include "ViconFrameTypes.h"
#include "detail/ViconOutlet.h"

#include <lsl_cpp.h>
#include <string>
#include <vector>

// An empty layout creates no LSL stream.
class MarkerStream {
public:
    explicit MarkerStream(StreamOutletFactory outlet_factory = createLslStreamOutlet);

    // Replaces any open stream with one for these (subject, marker) names.
    void initialize(const std::vector<vicon_lsl::NamedViconItem>& marker_names,
                    const std::string& stream_name,
                    const std::string& source_id,
                    double nominal_rate = lsl::IRREGULAR_RATE);
    void destroy();

    // Sends X, Y, Z, and a valid flag for each marker, with NaN for any it could not read.
    StreamPushResult pushSample(const std::vector<vicon_lsl::MarkerTranslationRead>& markers,
                                double timestamp);
    bool isInitialized() const;

private:
    vicon_lsl::detail::ViconOutlet outlet_;
};
