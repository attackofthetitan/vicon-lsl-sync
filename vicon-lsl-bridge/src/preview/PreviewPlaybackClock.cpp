#include "preview/PreviewPlaybackClock.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vicon_lsl {

void PreviewPlaybackClock::setTimeline(const std::vector<double>& timestamps) {
    frame_timeline_ = nullptr;
    frame_timeline_origin_ = 0.0;
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
            throw std::invalid_argument("Playback times must always increase");
        }
        timeline_.push_back(relative);
        previous = relative;
    }
    reset();
}

void PreviewPlaybackClock::setFrameTimeline(const std::vector<PreviewFrame>& frames) {
    timeline_.clear();
    frame_timeline_ = nullptr;
    frame_timeline_origin_ = 0.0;
    if (frames.empty()) {
        reset();
        return;
    }
    const double first = frames.front().timestamp;
    double previous = 0.0;
    for (const PreviewFrame& frame : frames) {
        if (!std::isfinite(frame.timestamp)) {
            throw std::invalid_argument("Playback timeline contains a non-finite timestamp");
        }
        const double relative = frame.timestamp - first;
        if (relative < previous) {
            throw std::invalid_argument("Playback times must always increase");
        }
        previous = relative;
    }
    frame_timeline_ = &frames;
    frame_timeline_origin_ = first;
    reset();
}

bool PreviewPlaybackClock::timelineEmpty() const {
    return frame_timeline_ ? frame_timeline_->empty() : timeline_.empty();
}

std::size_t PreviewPlaybackClock::timelineSize() const {
    return frame_timeline_ ? frame_timeline_->size() : timeline_.size();
}

double PreviewPlaybackClock::relativeTimestamp(std::size_t index) const {
    return frame_timeline_
        ? frame_timeline_->at(index).timestamp - frame_timeline_origin_
        : timeline_.at(index);
}

void PreviewPlaybackClock::reset() {
    playing_ = false;
    paused_position_ = 0.0;
    anchor_monotonic_seconds_ = 0.0;
}

void PreviewPlaybackClock::play(double monotonic_seconds) {
    if (playing_ || timelineEmpty()) {
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
    if (timelineEmpty()) {
        return 0.0;
    }
    double current = paused_position_;
    if (playing_) {
        current += (std::max)(0.0, monotonic_seconds - anchor_monotonic_seconds_) * speed_;
    }
    const double duration = relativeTimestamp(timelineSize() - 1);
    if (looping_ && duration > 0.0 && current > duration) {
        current = std::fmod(current, duration);
    } else {
        current = std::clamp(current, 0.0, duration);
    }
    return current;
}

double PreviewPlaybackClock::duration() const {
    return timelineEmpty() ? 0.0 : relativeTimestamp(timelineSize() - 1);
}

bool PreviewPlaybackClock::atEnd(double monotonic_seconds) const {
    return !timelineEmpty() && !looping_ &&
           position(monotonic_seconds) >= duration();
}

std::size_t PreviewPlaybackClock::frameIndex(double monotonic_seconds) const {
    if (timelineEmpty()) {
        return 0;
    }
    const double current = position(monotonic_seconds);
    std::size_t first = 0;
    std::size_t count = timelineSize();
    while (count > 0) {
        const std::size_t step = count / 2;
        const std::size_t candidate = first + step;
        if (relativeTimestamp(candidate) <= current) {
            first = candidate + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    return first == 0 ? 0 : first - 1;
}

} // namespace vicon_lsl
