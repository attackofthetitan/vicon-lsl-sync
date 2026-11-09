#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vicon_lsl {

// Frames the Vicon SDK may hold waiting for the bridge to read them.
constexpr unsigned int kViconClientBufferFrames = 256;

// The SDK's latency does not include time in its client queue. Preserve the
// camera cadence when queued frames are drained, using frame numbers so real
// losses remain gaps. Slowly follow clock drift instead of receipt-time jitter.
//
// The cadence is trusted only while it agrees with when frames arrive:
// - A frame cannot be captured after it arrived. A cadence time later than the
//   arrival estimate by more than kEarlyToleranceSeconds means the frame number
//   jumped ahead without that time passing. Vicon does this when a client
//   connects, by sending frames left from the last client first, and when it
//   switches between reviewing a trial and Live.
// - A drained queue makes a frame arrive late by at most the time the queue
//   holds. Arriving later than that means frame numbers stopped keeping up with
//   time, as in a paused or slowed replay.
// Either way the clock starts again from the arrival estimate and says so.
class ViconFrameClock {
public:
    static constexpr double kEarlyToleranceSeconds = 0.05;
    static constexpr double kLateMarginSeconds = 0.5;

    double timestamp(std::uint32_t frame, double rate, double estimate) {
        resync_seconds_ = 0.0;
        if (!std::isfinite(estimate) || !std::isfinite(rate) || rate <= 0.0) {
            reset();
            return estimate;
        }
        const std::uint32_t advance = frame - last_frame_;
        if (!initialized_ || rate != rate_ || advance == 0 || advance >= 0x80000000U) {
            timestamp_ = estimate;
        } else {
            const double elapsed = static_cast<double>(advance) / rate;
            const double predicted = timestamp_ + elapsed;
            // Positive when the frame arrived after the time the cadence gives it.
            const double arrival_lag = estimate - predicted;
            const double maximum_lag =
                static_cast<double>(kViconClientBufferFrames) / rate + kLateMarginSeconds;
            if (arrival_lag < -kEarlyToleranceSeconds || arrival_lag > maximum_lag) {
                timestamp_ = estimate;
                resync_seconds_ = predicted - estimate;
            } else {
                // A 1,000 ppm bound follows clock drift but keeps a short stall
                // from stretching one frame and compressing the buffered frames.
                const double correction = elapsed * 0.001;
                timestamp_ = predicted + std::clamp(arrival_lag, -correction, correction);
            }
        }
        initialized_ = true;
        last_frame_ = frame;
        rate_ = rate;
        return timestamp_;
    }

    // How far the cadence was moved to match arrival time on the last frame:
    // positive when it had run ahead, negative when behind, 0 when not moved.
    double lastResyncSeconds() const { return resync_seconds_; }

    void reset() {
        initialized_ = false;
        resync_seconds_ = 0.0;
    }

private:
    bool initialized_ = false;
    std::uint32_t last_frame_ = 0;
    double rate_ = 0.0;
    double timestamp_ = 0.0;
    double resync_seconds_ = 0.0;
};

} // namespace vicon_lsl
