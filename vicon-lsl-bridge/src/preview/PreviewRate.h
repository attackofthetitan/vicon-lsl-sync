#pragma once

#include <cstddef>
#include <deque>

namespace vicon_lsl {

// Measures a stream's actual sample rate over the last few seconds. No rate is
// given until the samples cover the whole window, so a burst at startup is not
// mistaken for the steady rate.
class PreviewRateTracker {
public:
    static constexpr double kDefaultWindowSeconds = 2.0;

    explicit PreviewRateTracker(double window_seconds = kDefaultWindowSeconds);

    void reset();
    // Ignores a time that is not a number or repeats the last one, and returns
    // false for it. A time that goes backward starts a new window, because
    // clock correction can move time back.
    bool addTimestamp(double corrected_timestamp);

    bool hasFullWindow() const;
    double effectiveRateHz() const;
    // True when a full window has been measured and its rate is below
    // `fraction` of the expected rate.
    bool belowNominalRate(double nominal_rate, double fraction) const;
    std::size_t sampleCount() const;

private:
    double window_seconds_;
    std::deque<double> timestamps_;
};

} // namespace vicon_lsl
