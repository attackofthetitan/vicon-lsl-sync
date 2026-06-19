#include "HoloLensGazePacket.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace vicon_lsl {
namespace {

constexpr std::array<HoloLensGazeChannel, HoloLensGazePacket::ChannelCount> kChannels{{
    {"CombinedOriginX", "meters"},
    {"CombinedOriginY", "meters"},
    {"CombinedOriginZ", "meters"},
    {"CombinedDirectionX", "normalized"},
    {"CombinedDirectionY", "normalized"},
    {"CombinedDirectionZ", "normalized"},
    {"CombinedValid", "bool"},
    {"LeftEyeOriginX", "meters"},
    {"LeftEyeOriginY", "meters"},
    {"LeftEyeOriginZ", "meters"},
    {"LeftEyeDirectionX", "normalized"},
    {"LeftEyeDirectionY", "normalized"},
    {"LeftEyeDirectionZ", "normalized"},
    {"LeftEyeValid", "bool"},
    {"RightEyeOriginX", "meters"},
    {"RightEyeOriginY", "meters"},
    {"RightEyeOriginZ", "meters"},
    {"RightEyeDirectionX", "normalized"},
    {"RightEyeDirectionY", "normalized"},
    {"RightEyeDirectionZ", "normalized"},
    {"RightEyeValid", "bool"},
}};

bool parseDouble(std::string_view text, double& value) {
    if (text.empty()) {
        return false;
    }

    if (text == "NaN") {
        value = std::numeric_limits<double>::quiet_NaN();
        return true;
    }

    std::string copy(text);
    char* end = nullptr;
    errno = 0;
    value = std::strtod(copy.c_str(), &end);
    return errno == 0 && end != copy.c_str() && end == copy.c_str() + copy.size();
}

HoloLensGazeParseResult parseError(HoloLensGazeParseError error,
                                   std::size_t field_index,
                                   std::string_view field = {}) {
    HoloLensGazeParseResult result;
    result.error = error;
    result.field_index = field_index;
    result.field.assign(field.begin(), field.end());
    return result;
}

} // namespace

const std::array<HoloLensGazeChannel, HoloLensGazePacket::ChannelCount>&
holoLensGazeChannels() {
    return kChannels;
}

const char* toString(HoloLensGazeParseError error) {
    switch (error) {
        case HoloLensGazeParseError::None: return "None";
        case HoloLensGazeParseError::MissingPrefix: return "MissingPrefix";
        case HoloLensGazeParseError::WrongFieldCount: return "WrongFieldCount";
        case HoloLensGazeParseError::InvalidNumber: return "InvalidNumber";
    }
    return "Unknown";
}

HoloLensGazeParseResult parseHoloLensGazePacket(std::string_view packet) {
    constexpr std::string_view prefix_with_comma = "HLGAZE1,";
    if (packet.substr(0, prefix_with_comma.size()) != prefix_with_comma) {
        return parseError(HoloLensGazeParseError::MissingPrefix, 0);
    }

    packet.remove_prefix(prefix_with_comma.size());

    HoloLensGazeParseResult result;
    constexpr std::size_t expected_fields = HoloLensGazePacket::ChannelCount + 1;

    for (std::size_t field_index = 0; field_index < expected_fields; ++field_index) {
        const std::size_t comma = packet.find(',');
        const std::string_view field =
            comma == std::string_view::npos ? packet : packet.substr(0, comma);

        double parsed = 0.0;
        if (!parseDouble(field, parsed)) {
            return parseError(HoloLensGazeParseError::InvalidNumber, field_index, field);
        }

        if (field_index == 0) {
            if (!std::isfinite(parsed)) {
                return parseError(HoloLensGazeParseError::InvalidNumber, field_index, field);
            }
            result.packet.device_timestamp = parsed;
        } else {
            result.packet.sample[field_index - 1] = parsed;
        }

        if (field_index + 1 == expected_fields) {
            if (comma != std::string_view::npos) {
                return parseError(HoloLensGazeParseError::WrongFieldCount, field_index + 1);
            }
            result.error = HoloLensGazeParseError::None;
            return result;
        }

        if (comma == std::string_view::npos) {
            return parseError(HoloLensGazeParseError::WrongFieldCount, field_index + 1);
        }
        packet.remove_prefix(comma + 1);
    }

    return parseError(HoloLensGazeParseError::WrongFieldCount, expected_fields);
}

} // namespace vicon_lsl
