#include "SegmentStream.h"

#include "StreamSchema.h"
#include "ViconFrameMapping.h"

#include <utility>

namespace {

constexpr vicon_lsl::detail::ViconNumericOutletProfile kSegmentOutletProfile{
    "Segment",
    "segments",
    "No segments discovered; segment stream not created",
    "Segment outlet factory returned no outlet",
    "Failed to push segment LSL sample: ",
};

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
    const std::vector<vicon_lsl::SegmentObjectRead>& segments,
    double timestamp) {
    return outlet_.pushSample([&segments] {
        std::vector<vicon_lsl::SegmentSample> samples;
        samples.reserve(segments.size());
        for (const auto& segment : segments) {
            samples.push_back(
                vicon_lsl::segmentSampleForLsl(segment.translation, segment.rotation));
        }
        return vicon_lsl::flattenSegmentSamples(samples);
    }, timestamp);
}
