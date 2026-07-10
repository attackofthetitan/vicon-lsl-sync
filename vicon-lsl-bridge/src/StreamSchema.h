#pragma once


#include <array>
#include <string>
#include <utility>
#include <vector>

namespace vicon_lsl {

struct StreamChannel {
    std::string label;
    std::string unit;
};

struct StreamSchema {
    std::string name;
    std::string type;
    std::vector<StreamChannel> channels;

    std::size_t channelCount() const { return channels.size(); }
};

using NamedViconItem = std::pair<std::string, std::string>;
using MarkerSample = std::array<double, 4>;
using SegmentSample = std::array<double, 7>;

StreamSchema buildMarkerStreamSchema(const std::vector<NamedViconItem>& marker_names,
                                     const std::string& stream_name);
StreamSchema buildSegmentStreamSchema(const std::vector<NamedViconItem>& segment_names,
                                      const std::string& stream_name);

std::vector<double> flattenMarkerSamples(const std::vector<MarkerSample>& markers);
std::vector<double> flattenSegmentSamples(const std::vector<SegmentSample>& segments);

} // namespace vicon_lsl
