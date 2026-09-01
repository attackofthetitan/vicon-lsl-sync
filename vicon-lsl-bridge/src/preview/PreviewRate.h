#pragma once

#include <cstddef>
#include <deque>

namespace vicon_lsl {

// Estimates the effective rate of a stream from its corrected sample
// timestamps.  A rate is only available after the timestamps span the full
// rolling window, so short startup bursts cannot be mistaken for a steady
// stream rate.
class PreviewRateTracker {
public:
    static constexpr double kDefaultWindowSeconds = 2.0;

    explicit PreviewRateTracker(double window_seconds = kDefaultWindowSeconds);

    void reset();
    // Non-finite and duplicate timestamps are ignored. A regression starts a
    // new measurement window because clock correction need not be monotonic.
    // Returning false distinguishes ignored timestamps without changing the
    // live stream's sample handling.
    bool addTimestamp(double corrected_timestamp);

    bool hasFullWindow() const;
    double effectiveRateHz() const;
    // Returns true only when a complete window has been measured and the
    // effective rate is below the requested fraction of a valid nominal rate.
    bool belowNominalRate(double nominal_rate, double fraction) const;
    std::size_t sampleCount() const;

private:
    double window_seconds_;
    std::deque<double> timestamps_;
};

} // namespace vicon_lsl
