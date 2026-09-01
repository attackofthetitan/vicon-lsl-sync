#include "preview/PreviewPlaybackClock.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vicon_lsl {

namespace {

std::vector<double> relativeTimeline(const std::vector<double>& timestamps) {
    std::vector<double> timeline;
    if (timestamps.empty()) return timeline;
    timeline.reserve(timestamps.size());
    const double first = timestamps.front();
    for (const double timestamp : timestamps) {
        if (!std::isfinite(timestamp)) {
            throw std::invalid_argument("Playback timeline contains a non-finite timestamp");
        }
        const double relative = timestamp - first;
        if (!timeline.empty() && relative < timeline.back()) {
            throw std::invalid_argument("Playback times must always increase");
        }
        timeline.push_back(relative);
    }
    return timeline;
}

} // namespace

void PreviewPlaybackClock::setTimeline(const std::vector<double>& timestamps) {
    timeline_ = relativeTimeline(timestamps);
    reset();
}

void PreviewPlaybackClock::setFrameTimeline(const std::vector<PreviewFrame>& frames) {
    std::vector<double> timestamps;
    timestamps.reserve(frames.size());
    for (const PreviewFrame& frame : frames) timestamps.push_back(frame.timestamp);
    setTimeline(timestamps);
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

void PreviewPlaybackClock::setLooping(bool looping, double monotonic_seconds) {
    if (playing_) {
        paused_position_ = position(monotonic_seconds);
        anchor_monotonic_seconds_ = monotonic_seconds;
    }
    looping_ = looping;
}

void PreviewPlaybackClock::seek(double position_seconds, double monotonic_seconds) {
    if (!std::isfinite(position_seconds)) {
        throw std::invalid_argument("Playback position must be finite");
    }
    paused_position_ = std::clamp(position_seconds, 0.0, duration());
    anchor_monotonic_seconds_ = monotonic_seconds;
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
    if (looping_ && duration > 0.0 && current > duration) {
        current = std::fmod(current, duration);
    } else {
        current = std::clamp(current, 0.0, duration);
    }
    return current;
}

double PreviewPlaybackClock::duration() const {
    return timeline_.empty() ? 0.0 : timeline_.back();
}

bool PreviewPlaybackClock::atEnd(double monotonic_seconds) const {
    return !timeline_.empty() && !looping_ &&
           position(monotonic_seconds) >= duration();
}

std::size_t PreviewPlaybackClock::frameIndex(double monotonic_seconds) const {
    if (timeline_.empty()) {
        return 0;
    }
    const auto upper = std::upper_bound(timeline_.begin(), timeline_.end(),
                                        position(monotonic_seconds));
    return upper == timeline_.begin()
        ? 0 : static_cast<std::size_t>(std::distance(timeline_.begin(), upper) - 1);
}

} // namespace vicon_lsl
