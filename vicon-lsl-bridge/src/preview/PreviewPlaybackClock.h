#pragma once

#include "preview/PreviewTypes.h"

#include <cstddef>
#include <vector>

namespace vicon_lsl {

// Maps elapsed time to a recording position. Pass time from a steady clock.
class PreviewPlaybackClock {
public:
    // Times must not go backward. Playback starts paused at zero.
    void setTimeline(std::vector<double> timestamps);
    void setFrameTimeline(const std::vector<PreviewFrame>& frames);
    void play(double monotonic_seconds);
    void pause(double monotonic_seconds);
    void setSpeed(double speed, double monotonic_seconds);
    void setLooping(bool looping, double monotonic_seconds);
    void seek(double position_seconds, double monotonic_seconds);

    std::size_t frameIndex(double monotonic_seconds) const;
    double position(double monotonic_seconds) const;
    double duration() const;
    bool atEnd(double monotonic_seconds) const;

private:
    // Restarts the running position from `monotonic_seconds`.
    void reanchor(double monotonic_seconds);

    std::vector<double> timeline_;
    bool playing_ = false;
    double speed_ = 1.0;
    double paused_position_ = 0.0;
    double anchor_monotonic_seconds_ = 0.0;
    bool looping_ = false;
};

} // namespace vicon_lsl
