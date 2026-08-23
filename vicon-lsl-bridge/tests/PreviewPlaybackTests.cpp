#include "preview/PreviewPlaybackClock.h"
#include "TestSupport.h"

#include <stdexcept>

TEST_CASE("Preview playback clock follows recorded timestamps and wraps") {
    vicon_lsl::PreviewPlaybackClock clock;
    clock.setTimeline({0.0, 1.0 / 30.0, 2.0 / 30.0});
    clock.play(10.0);
    REQUIRE_EQ(clock.frameIndex(10.04), static_cast<std::size_t>(1));
    REQUIRE_EQ(clock.frameIndex(10.06), static_cast<std::size_t>(1));
    REQUIRE_EQ(clock.frameIndex(10.07), static_cast<std::size_t>(0));

    clock.setTimeline({5.0, 5.1, 5.7, 6.0});
    clock.play(20.0);
    REQUIRE_EQ(clock.frameIndex(20.65), static_cast<std::size_t>(1));
    REQUIRE_EQ(clock.frameIndex(20.75), static_cast<std::size_t>(2));
}
TEST_CASE("Preview playback clock preserves pause position and speed changes") {
    vicon_lsl::PreviewPlaybackClock clock;
    clock.setTimeline({0.0, 1.0, 2.0, 3.0});
    clock.play(0.0);
    clock.pause(1.2);
    REQUIRE_EQ(clock.frameIndex(100.0), static_cast<std::size_t>(1));

    clock.setSpeed(2.0, 100.0);
    clock.play(100.0);
    REQUIRE_EQ(clock.frameIndex(100.2), static_cast<std::size_t>(1));
    REQUIRE_EQ(clock.frameIndex(100.45), static_cast<std::size_t>(2));
    REQUIRE_EQ(clock.frameIndex(100.95), static_cast<std::size_t>(0));

    bool rejected = false;
    try {
        clock.setTimeline({0.0, 2.0, 1.0});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
}
