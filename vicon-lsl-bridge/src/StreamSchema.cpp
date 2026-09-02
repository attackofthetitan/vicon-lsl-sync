#include "StreamSchema.h"

namespace vicon_lsl {
namespace {

void appendChannel(StreamSchema& schema, std::string label, std::string unit) {
    schema.channels.push_back(StreamChannel{std::move(label), std::move(unit)});
}

} // namespace

StreamSchema buildMarkerStreamSchema(const std::vector<NamedViconItem>& marker_names,
                                     const std::string& stream_name) {
    StreamSchema schema{stream_name, "MoCap", {}};
    schema.channels.reserve(marker_names.size() * 4);

    for (const auto& marker_name : marker_names) {
        const std::string prefix = marker_name.first + ":" + marker_name.second;
        appendChannel(schema, prefix + ":X", "mm");
        appendChannel(schema, prefix + ":Y", "mm");
        appendChannel(schema, prefix + ":Z", "mm");
        appendChannel(schema, prefix + ":Valid", "bool");
    }

    return schema;
}

StreamSchema buildSegmentStreamSchema(const std::vector<NamedViconItem>& segment_names,
                                      const std::string& stream_name) {
    StreamSchema schema{stream_name, "MoCap", {}};
    schema.channels.reserve(segment_names.size() * 7);

    for (const auto& segment_name : segment_names) {
        const std::string prefix = segment_name.first + ":" + segment_name.second;
        appendChannel(schema, prefix + ":X", "mm");
        appendChannel(schema, prefix + ":Y", "mm");
        appendChannel(schema, prefix + ":Z", "mm");
        appendChannel(schema, prefix + ":QX", "quaternion");
        appendChannel(schema, prefix + ":QY", "quaternion");
        appendChannel(schema, prefix + ":QZ", "quaternion");
        appendChannel(schema, prefix + ":QW", "quaternion");
    }

    return schema;
}

} // namespace vicon_lsl
