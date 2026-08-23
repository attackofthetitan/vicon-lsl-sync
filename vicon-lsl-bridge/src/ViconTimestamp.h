#pragma once

namespace vicon_lsl {

double quietNaN();

struct ViconTimestampState {
    bool have_timestamp = false;
    double last_timestamp = 0.0;
};

// Estimate acquisition time from immediate local receipt minus Vicon's
// GetLatencyTotal pipeline-latency estimate when valid. This is not a
// capture-accurate timestamp and does not account for ServerPush or network
// delay; invalid or negative latency falls back to receipt time.
double viconFrameTimestamp(double receipt_timestamp,
                           double latency_seconds,
                           bool latency_valid);

// Keep the timestamp finite and strictly increasing. A non-finite candidate
// falls back to the supplied receipt timestamp; a regression is clamped to
// the next representable value after the previous sample.
bool enforceViconTimestamp(double candidate_timestamp,
                           double receipt_timestamp,
                           ViconTimestampState& state,
                           double& output_timestamp,
                           bool* adjusted = nullptr);

} // namespace vicon_lsl
