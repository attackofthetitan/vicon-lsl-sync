#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace vicon_lsl {

struct HoloLensGazeChannel {
    std::string_view label;
    std::string_view unit;
};

struct HoloLensGazePacket {
    static constexpr std::size_t ChannelCount = 21;

    std::array<double, ChannelCount> sample{};
};

const std::array<HoloLensGazeChannel, HoloLensGazePacket::ChannelCount>&
holoLensGazeChannels();

} // namespace vicon_lsl
