#pragma once

#include "ViconFrameTypes.h"
#include "detail/ViconOutlet.h"

#include <lsl_cpp.h>
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

    // Sends X, Y, Z and validity for each marker; unavailable positions become NaN.
    StreamPushResult pushSample(const std::vector<vicon_lsl::MarkerTranslationRead>& markers,
                                double timestamp);
    bool isInitialized() const;

private:
    vicon_lsl::detail::ViconOutlet outlet_;
};
