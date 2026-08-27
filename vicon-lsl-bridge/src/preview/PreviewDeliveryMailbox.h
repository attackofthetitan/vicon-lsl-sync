#pragma once

#include "preview/PreviewTypes.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace vicon_lsl {

struct PreviewDeliveryMetrics {
    unsigned long long produced_frames = 0;
    unsigned long long displayed_frames = 0;
    unsigned long long replaced_before_display = 0;
    unsigned long long coalesced_input_samples = 0;
    std::int64_t display_latency_ms = 0;
};

// Single-slot display delivery. Publishing can never grow an event queue:
// an undisplayed frame is replaced, while the replacement remains observable.
class PreviewDeliveryMailbox {
public:
    void publish(PreviewFrame frame, std::int64_t created_ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (latest_frame_) ++metrics_.replaced_before_display;
        latest_frame_ = std::move(frame);
        latest_frame_created_ms_ = created_ms;
        ++metrics_.produced_frames;
    }

    bool takeLatest(PreviewFrame& frame,
                    PreviewDeliveryMetrics& metrics,
                    std::int64_t now_ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!latest_frame_) {
            metrics = metrics_;
            return false;
        }
        frame = std::move(*latest_frame_);
        latest_frame_.reset();
        ++metrics_.displayed_frames;
        metrics_.display_latency_ms =
            (std::max)(std::int64_t{0}, now_ms - latest_frame_created_ms_);
        metrics = metrics_;
        return true;
    }

    void addCoalescedInputSamples(unsigned long long count) {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.coalesced_input_samples += count;
    }

    PreviewDeliveryMetrics metrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return metrics_;
    }

    std::size_t queuedFrameCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_frame_ ? 1U : 0U;
    }

private:
    mutable std::mutex mutex_;
    std::optional<PreviewFrame> latest_frame_;
    PreviewDeliveryMetrics metrics_;
    std::int64_t latest_frame_created_ms_ = 0;
};

} // namespace vicon_lsl
