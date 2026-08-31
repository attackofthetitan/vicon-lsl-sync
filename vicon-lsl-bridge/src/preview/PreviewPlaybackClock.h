#pragma once

#include <cstddef>
#include <vector>

namespace vicon_lsl {

class PreviewPlaybackClock {
public:
    void setTimeline(const std::vector<double>& timestamps);
    void reset();
    void play(double monotonic_seconds);
    void pause(double monotonic_seconds);
    void setSpeed(double speed, double monotonic_seconds);

    std::size_t frameIndex(double monotonic_seconds) const;
    double position(double monotonic_seconds) const;

private:
    std::vector<double> timeline_;
    bool playing_ = false;
    double speed_ = 1.0;
    double paused_position_ = 0.0;
    double anchor_monotonic_seconds_ = 0.0;
};

} // namespace vicon_lsl
