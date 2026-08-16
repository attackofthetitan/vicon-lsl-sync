#include "preview/PreviewRate.h"
#include "TestSupport.h"

#include <cmath>
#include <limits>

namespace {

bool near(double left, double right, double tolerance = 1e-9) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

TEST_CASE("Preview rate stays unavailable until the rolling window is full") {
    vicon_lsl::PreviewRateTracker tracker;
    REQUIRE(tracker.addTimestamp(10.0));
    REQUIRE(tracker.addTimestamp(11.0));
    REQUIRE(!tracker.hasFullWindow());
    REQUIRE_EQ(tracker.sampleCount(), static_cast<std::size_t>(2));
    REQUIRE(near(tracker.effectiveRateHz(), 0.0));

    REQUIRE(tracker.addTimestamp(12.0));
    REQUIRE(tracker.hasFullWindow());
    REQUIRE(near(tracker.effectiveRateHz(), 1.0));
}

TEST_CASE("Preview rate reports the effective rate over corrected timestamps") {
    vicon_lsl::PreviewRateTracker tracker;
    constexpr double interval = 1.0 / 90.0;
    for (int sample = 0; sample <= 180; ++sample) {
        REQUIRE(tracker.addTimestamp(static_cast<double>(sample) * interval));
    }

    REQUIRE(tracker.hasFullWindow());
    REQUIRE(near(tracker.effectiveRateHz(), 90.0));

    // A gap is reflected in the effective rate while the current two-second
    // window remains measurable.
    REQUIRE(tracker.addTimestamp(2.5));
    REQUIRE(tracker.hasFullWindow());
    REQUIRE(near(tracker.effectiveRateHz(), 68.0));

    // A later gap rolls the old history out while preserving the boundary
    // sample needed to measure the current window.
    REQUIRE(tracker.addTimestamp(5.0));
    REQUIRE(tracker.hasFullWindow());
    REQUIRE_EQ(tracker.sampleCount(), static_cast<std::size_t>(2));
    REQUIRE(near(tracker.effectiveRateHz(), 0.4));
}

TEST_CASE("Preview rate ignores malformed timestamps and resets on regression") {
    vicon_lsl::PreviewRateTracker tracker;
    REQUIRE(!tracker.addTimestamp(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(!tracker.addTimestamp(std::numeric_limits<double>::infinity()));
    REQUIRE(tracker.addTimestamp(1.0));
    REQUIRE(!tracker.addTimestamp(1.0));
    REQUIRE(tracker.addTimestamp(0.5));
    REQUIRE_EQ(tracker.sampleCount(), static_cast<std::size_t>(1));
    REQUIRE(!tracker.hasFullWindow());
}

TEST_CASE("Preview rate resets cleanly between live stream sessions") {
    vicon_lsl::PreviewRateTracker tracker;
    tracker.addTimestamp(0.0);
    tracker.addTimestamp(2.0);
    REQUIRE(tracker.hasFullWindow());

    tracker.reset();
    REQUIRE(!tracker.hasFullWindow());
    REQUIRE_EQ(tracker.sampleCount(), static_cast<std::size_t>(0));
    REQUIRE(tracker.addTimestamp(100.0));
    REQUIRE(!tracker.hasFullWindow());
}

TEST_CASE("Preview gaze rate warning is gated by a full window and nominal threshold") {
    vicon_lsl::PreviewRateTracker tracker;
    tracker.addTimestamp(0.0);
    tracker.addTimestamp(1.0);
    REQUIRE(!tracker.belowNominalRate(90.0, 0.8));

    tracker.addTimestamp(2.0);
    REQUIRE(!tracker.belowNominalRate(1.0, 0.8));

    vicon_lsl::PreviewRateTracker low_rate;
    low_rate.addTimestamp(0.0);
    low_rate.addTimestamp(2.0);
    REQUIRE(low_rate.belowNominalRate(90.0, 0.8));
    REQUIRE(!low_rate.belowNominalRate(0.0, 0.8));
    REQUIRE(!low_rate.belowNominalRate(90.0, 1.0));
}
