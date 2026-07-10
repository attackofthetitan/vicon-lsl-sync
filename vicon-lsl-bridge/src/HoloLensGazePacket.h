#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace vicon_lsl {

struct HoloLensGazeChannel {
    std::string_view label;
    std::string_view unit;
};

inline constexpr std::size_t kHoloLensGazeChannelCount = 21;

const std::array<HoloLensGazeChannel, kHoloLensGazeChannelCount>&
holoLensGazeChannels();

} // namespace vicon_lsl
