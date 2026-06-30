#include "HoloLensGazePacket.h"
#include "TestSupport.h"

#include <cmath>
#include <sstream>
#include <string>

namespace {

std::string validPacket() {
    std::ostringstream packet;
    packet << "HLGAZE1";
    for (std::size_t i = 0; i < vicon_lsl::HoloLensGazePacket::ChannelCount; ++i) {
        packet << "," << i;
    }
    return packet.str();
}

} // namespace

TEST_CASE("HoloLens parser accepts valid packets without a device timestamp") {
    const auto parsed = vicon_lsl::parseHoloLensGazePacket(validPacket());
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.packet.sample[0], 0.0);
    REQUIRE_EQ(parsed.packet.sample[20], 20.0);
}

TEST_CASE("HoloLens parser accepts NaN fields") {
    std::string packet = validPacket();
    const std::string needle = ",5,";
    const auto pos = packet.find(needle);
    REQUIRE(pos != std::string::npos);
    packet.replace(pos, needle.size(), ",NaN,");

    const auto parsed = vicon_lsl::parseHoloLensGazePacket(packet);
    REQUIRE(parsed.ok());
    REQUIRE(std::isnan(parsed.packet.sample[5]));
}

TEST_CASE("HoloLens parser rejects malformed packets") {
    REQUIRE_EQ(vicon_lsl::parseHoloLensGazePacket("BAD,123").error,
               vicon_lsl::HoloLensGazeParseError::MissingPrefix);
    REQUIRE_EQ(vicon_lsl::parseHoloLensGazePacket("HLGAZE1,1,2").error,
               vicon_lsl::HoloLensGazeParseError::WrongFieldCount);
    REQUIRE_EQ(vicon_lsl::parseHoloLensGazePacket(validPacket() + ",27").error,
               vicon_lsl::HoloLensGazeParseError::WrongFieldCount);
    REQUIRE_EQ(vicon_lsl::parseHoloLensGazePacket("HLGAZE1,,1").error,
               vicon_lsl::HoloLensGazeParseError::InvalidNumber);
    REQUIRE_EQ(vicon_lsl::parseHoloLensGazePacket("HLGAZE1,abc").error,
               vicon_lsl::HoloLensGazeParseError::InvalidNumber);
    REQUIRE_EQ(std::string(vicon_lsl::toString(
                   vicon_lsl::HoloLensGazeParseError::InvalidNumber)),
               std::string("InvalidNumber"));
}

TEST_CASE("HoloLens channel metadata remains complete") {
    const auto& metadata = vicon_lsl::holoLensGazeChannels();
    REQUIRE_EQ(metadata.size(), vicon_lsl::HoloLensGazePacket::ChannelCount);
    REQUIRE_EQ(std::string(metadata.front().label), std::string("CombinedOriginX"));
    REQUIRE_EQ(std::string(metadata.back().label), std::string("RightEyeValid"));
}
