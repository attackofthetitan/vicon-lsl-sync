#include "MarkerStream.h"

#include "StreamSchema.h"
#include "ViconFrameMapping.h"

#include <tuple>
#include <utility>
#include <vector>

MarkerStream::MarkerStream(StreamOutletFactory outlet_factory)
    : outlet_(std::move(outlet_factory), "Marker", "marker") {}

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
    const std::vector<vicon_lsl::MarkerTranslationRead>& markers,
    double timestamp) {
    std::vector<double> channels;
    channels.reserve(markers.size() * std::tuple_size_v<vicon_lsl::MarkerSample>);
    for (const auto& marker : markers) {
        const auto sample = vicon_lsl::markerSampleForLsl(marker);
        channels.insert(channels.end(), sample.begin(), sample.end());
    }
    return outlet_.pushSample(channels, timestamp);
}
