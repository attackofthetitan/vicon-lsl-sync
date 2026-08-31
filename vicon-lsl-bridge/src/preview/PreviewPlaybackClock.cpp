#include "preview/PreviewPlaybackClock.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vicon_lsl {

void PreviewPlaybackClock::setTimeline(const std::vector<double>& timestamps) {
    timeline_.clear();
    if (timestamps.empty()) {
        reset();
        return;
    }

    const double first = timestamps.front();
    double previous = 0.0;
    timeline_.reserve(timestamps.size());
    for (const double timestamp : timestamps) {
        if (!std::isfinite(timestamp)) {
            throw std::invalid_argument("Playback timeline contains a non-finite timestamp");
        }
        const double relative = timestamp - first;
        if (!timeline_.empty() && relative < previous) {
            throw std::invalid_argument("Playback timeline is not monotonic");
        }
        timeline_.push_back(relative);
        previous = relative;
    }
    reset();
}

void PreviewPlaybackClock::reset() {
    playing_ = false;
    paused_position_ = 0.0;
    anchor_monotonic_seconds_ = 0.0;
}

void PreviewPlaybackClock::play(double monotonic_seconds) {
    if (playing_ || timeline_.empty()) {
        return;
    }
    anchor_monotonic_seconds_ = monotonic_seconds;
    playing_ = true;
}

void PreviewPlaybackClock::pause(double monotonic_seconds) {
    if (!playing_) {
        return;
    }
    paused_position_ = position(monotonic_seconds);
    playing_ = false;
}

void PreviewPlaybackClock::setSpeed(double speed, double monotonic_seconds) {
    if (!std::isfinite(speed) || speed <= 0.0) {
        throw std::invalid_argument("Playback speed must be positive and finite");
    }
    if (playing_) {
        paused_position_ = position(monotonic_seconds);
        anchor_monotonic_seconds_ = monotonic_seconds;
    }
    speed_ = speed;
}

double PreviewPlaybackClock::position(double monotonic_seconds) const {
    if (timeline_.empty()) {
        return 0.0;
    }
    double current = paused_position_;
    if (playing_) {
        current += (std::max)(0.0, monotonic_seconds - anchor_monotonic_seconds_) * speed_;
    }
    const double duration = timeline_.back();
    if (duration > 0.0 && current > duration) {
        current = std::fmod(current, duration);
    }
    return current;
}

std::size_t PreviewPlaybackClock::frameIndex(double monotonic_seconds) const {
    if (timeline_.empty()) {
        return 0;
    }
    const double current = position(monotonic_seconds);
    const auto upper = std::upper_bound(timeline_.begin(), timeline_.end(), current);
    return upper == timeline_.begin()
               ? 0
               : static_cast<std::size_t>(std::distance(timeline_.begin(), upper) - 1);
}

} // namespace vicon_lsl
