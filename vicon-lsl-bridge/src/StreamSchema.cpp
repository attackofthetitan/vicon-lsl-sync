#include "StreamSchema.h"

#include <initializer_list>

namespace vicon_lsl {
namespace {

StreamSchema buildViconStreamSchema(const std::vector<NamedViconItem>& names,
                                    const std::string& stream_name,
                                    std::initializer_list<StreamChannel> fields) {
    StreamSchema schema{stream_name, "MoCap", {}};
    schema.channels.reserve(names.size() * fields.size());
    for (const auto& name : names) {
        const std::string prefix = name.first + ":" + name.second + ":";
        for (const auto& field : fields) {
            schema.channels.push_back({prefix + field.label, field.unit});
        }
    }
    return schema;
}

} // namespace

StreamSchema buildMarkerStreamSchema(const std::vector<NamedViconItem>& marker_names,
                                     const std::string& stream_name) {
    return buildViconStreamSchema(marker_names, stream_name,
        {{"X", "mm"}, {"Y", "mm"}, {"Z", "mm"}, {"Valid", "bool"}});
}

StreamSchema buildSegmentStreamSchema(const std::vector<NamedViconItem>& segment_names,
                                      const std::string& stream_name) {
    return buildViconStreamSchema(segment_names, stream_name,
        {{"X", "mm"}, {"Y", "mm"}, {"Z", "mm"},
         {"QX", "quaternion"}, {"QY", "quaternion"},
         {"QZ", "quaternion"}, {"QW", "quaternion"}});
}

} // namespace vicon_lsl
