#include "MarkerStream.h"

#include "StreamSchema.h"
#include "ViconFrameMapping.h"

#include <utility>

namespace {

constexpr vicon_lsl::detail::ViconNumericOutletProfile kMarkerOutletProfile{
    "Marker",
    "markers",
    "No markers discovered; marker stream not created",
    "Marker outlet factory returned no outlet",
    "Failed to push marker LSL sample: ",
};

} // namespace

MarkerStream::MarkerStream(StreamOutletFactory outlet_factory)
    : outlet_(std::move(outlet_factory), kMarkerOutletProfile) {}

void MarkerStream::initialize(
    const std::vector<std::pair<std::string, std::string>>& marker_names,
    const std::string& stream_name,
    const std::string& source_id,
    double nominal_rate) {
    outlet_.initialize(
        vicon_lsl::buildMarkerStreamSchema(marker_names, stream_name),
        marker_names.size(),
        source_id,
        nominal_rate);
}

void MarkerStream::destroy() {
    outlet_.destroy();
}

bool MarkerStream::isInitialized() const {
    return outlet_.isInitialized();
}

StreamPushResult MarkerStream::pushSample(
    const std::vector<vicon_lsl::MarkerObjectRead>& markers,
    double timestamp) {
    return outlet_.pushSample([&markers] {
        std::vector<vicon_lsl::MarkerSample> samples;
        samples.reserve(markers.size());
        for (const auto& marker : markers) {
            samples.push_back(vicon_lsl::markerSampleForLsl(marker.value));
        }
        return vicon_lsl::flattenMarkerSamples(samples);
    }, timestamp);
}
