#include "SegmentStream.h"

#include "StreamSchema.h"
#include "ViconFrameMapping.h"

#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr vicon_lsl::detail::ViconNumericOutletProfile kSegmentOutletProfile{"Segment", "segment"};

} // namespace

SegmentStream::SegmentStream(StreamOutletFactory outlet_factory)
    : outlet_(std::move(outlet_factory), kSegmentOutletProfile) {}

void SegmentStream::initialize(
    const std::vector<std::pair<std::string, std::string>>& segment_names,
    const std::string& stream_name,
    const std::string& source_id,
    double nominal_rate) {
    outlet_.initialize(
        vicon_lsl::buildSegmentStreamSchema(segment_names, stream_name),
        segment_names.size(),
        source_id,
        nominal_rate);
}

void SegmentStream::destroy() {
    outlet_.destroy();
}

bool SegmentStream::isInitialized() const {
    return outlet_.isInitialized();
}

StreamPushResult SegmentStream::pushSample(
    const std::vector<vicon_lsl::SegmentPoseRead>& segments,
    double timestamp) {
    return outlet_.pushSample([&segments] {
        std::vector<double> channels;
        channels.reserve(segments.size() * std::tuple_size_v<vicon_lsl::SegmentSample>);
        for (const auto& segment : segments) {
            const vicon_lsl::SegmentSample sample =
                vicon_lsl::segmentSampleForLsl(segment.translation, segment.rotation);
            channels.insert(channels.end(), sample.begin(), sample.end());
        }
        return channels;
    }, timestamp);
}
