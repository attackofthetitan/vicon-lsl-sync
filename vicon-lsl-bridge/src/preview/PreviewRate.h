#pragma once

#include <cstddef>
#include <deque>

namespace vicon_lsl {

// Measures a stream's real sample rate over the last few seconds, giving none
// until the samples fill the window so a burst at startup does not count.
class PreviewRateTracker {
public:
    static constexpr double kDefaultWindowSeconds = 2.0;

    explicit PreviewRateTracker(double window_seconds = kDefaultWindowSeconds);

    void reset();
    // Returns false for a time that is not a number or repeats the last one, and
    // starts a new window when time goes backward, which clock correction can cause.
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
