#include "preview/PreviewRate.h"

#include <cmath>

namespace vicon_lsl {

PreviewRateTracker::PreviewRateTracker(double window_seconds)
    : window_seconds_(std::isfinite(window_seconds) && window_seconds > 0.0
                          ? window_seconds
                          : kDefaultWindowSeconds) {}

void PreviewRateTracker::reset() {
    timestamps_.clear();
}

bool PreviewRateTracker::addTimestamp(double corrected_timestamp) {
    if (!std::isfinite(corrected_timestamp)) {
        return false;
    }
    if (!timestamps_.empty() && corrected_timestamp == timestamps_.back()) {
        return false;
    }
    if (!timestamps_.empty() && corrected_timestamp < timestamps_.back()) {
        timestamps_.clear();
    }

    timestamps_.push_back(corrected_timestamp);
    const double cutoff = corrected_timestamp - window_seconds_;
    // Keep the most recent sample at or before the cutoff.  Retaining that
    // boundary sample makes a full window measurable even when timestamps
    // are jittered and no sample lands exactly on cutoff.
    while (timestamps_.size() > 2 && timestamps_[1] <= cutoff) {
        timestamps_.pop_front();
    }
    return true;
}

bool PreviewRateTracker::hasFullWindow() const {
    return timestamps_.size() >= 2 &&
           timestamps_.back() - timestamps_.front() >= window_seconds_;
}

double PreviewRateTracker::effectiveRateHz() const {
    if (!hasFullWindow()) {
        return 0.0;
    }
    const double elapsed = timestamps_.back() - timestamps_.front();
    if (!std::isfinite(elapsed) || elapsed <= 0.0) {
        return 0.0;
    }
    // There are N-1 intervals between N timestamped samples.  This keeps a
    // regular stream at its advertised rate when the window endpoints are
    // both included (for example, 181 samples over exactly two seconds at
    // 90 Hz).
    return static_cast<double>(timestamps_.size() - 1) / elapsed;
}

bool PreviewRateTracker::belowNominalRate(double nominal_rate, double fraction) const {
    return std::isfinite(nominal_rate) && nominal_rate > 0.0 &&
           std::isfinite(fraction) && fraction > 0.0 && fraction < 1.0 &&
           hasFullWindow() && effectiveRateHz() < fraction * nominal_rate;
}

std::size_t PreviewRateTracker::sampleCount() const {
    return timestamps_.size();
}

} // namespace vicon_lsl
