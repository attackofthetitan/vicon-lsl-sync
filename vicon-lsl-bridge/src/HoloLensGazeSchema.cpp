#include "HoloLensGazeSchema.h"

namespace vicon_lsl {
namespace {

constexpr std::array<HoloLensGazeChannel, kHoloLensGazeChannelCount> kChannels{{
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

} // namespace

const std::array<HoloLensGazeChannel, kHoloLensGazeChannelCount>&
holoLensGazeChannels() {
    return kChannels;
}

} // namespace vicon_lsl
