#pragma once

#include <array>
#include <cstddef>
#include <string>
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

enum class HoloLensGazeParseError {
    None,
    MissingPrefix,
    WrongFieldCount,
    InvalidNumber
};

struct HoloLensGazeParseResult {
    HoloLensGazePacket packet{};
    HoloLensGazeParseError error = HoloLensGazeParseError::None;
    std::size_t field_index = 0;
    std::string field;

    bool ok() const { return error == HoloLensGazeParseError::None; }
    explicit operator bool() const { return ok(); }
};

constexpr std::string_view kHoloLensGazePacketPrefix = "HLGAZE1";

const std::array<HoloLensGazeChannel, HoloLensGazePacket::ChannelCount>&
holoLensGazeChannels();

const char* toString(HoloLensGazeParseError error);
HoloLensGazeParseResult parseHoloLensGazePacket(std::string_view packet);

} // namespace vicon_lsl
