#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vicon_lsl {

// The SDK's latency does not include time in its client queue. Preserve the
// camera cadence when queued frames are drained, using frame numbers so real
// losses remain gaps. Slowly follow clock drift instead of receipt-time jitter.
class ViconFrameClock {
public:
    double timestamp(std::uint32_t frame, double rate, double estimate) {
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
            // A 1,000 ppm bound follows clock drift but keeps a short stall
            // from stretching one frame and compressing the buffered frames.
            const double correction = elapsed * 0.001;
            timestamp_ = predicted + std::clamp(estimate - predicted, -correction, correction);
        }
        initialized_ = true;
        last_frame_ = frame;
        rate_ = rate;
        return timestamp_;
    }

    void reset() { initialized_ = false; }

private:
    bool initialized_ = false;
    std::uint32_t last_frame_ = 0;
    double rate_ = 0.0;
    double timestamp_ = 0.0;
};

} // namespace vicon_lsl
