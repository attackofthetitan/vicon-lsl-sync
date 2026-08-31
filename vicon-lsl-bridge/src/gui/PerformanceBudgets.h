#pragma once

#include <QtGlobal>

#include <cstddef>
#include <cstdint>

namespace vicon_lsl::gui {

// Keep the app's time, memory, and size limits in one place.
struct PerformanceBudgets {
    static constexpr int MaximumGuiThreadStallMs = 50;
    static constexpr int BridgeStopDeadlineMs = 4000;
    static constexpr int RecorderStopDeadlineMs = 15000;
    static constexpr int PreviewStopDeadlineMs = 2000;
    static constexpr int FileCancelLatencyMs = 250;
    static constexpr std::size_t FileCancelSampleInterval = 1024;
    static constexpr double PreviewResolveTimeoutSeconds = 0.05;
    static constexpr double PreviewMetadataTimeoutSeconds = 0.25;
    static constexpr int MaximumLivePreviewLatencyMs = 100;
    static constexpr int DefaultRenderHz = 30;
    static constexpr int MaximumRenderHz = 60;
    static constexpr std::size_t MaximumQueuedLiveFrames = 1;
    static constexpr std::size_t MaximumEventLogEntries = 1000;
    static constexpr qsizetype MaximumProcessOutputBytes = 64 * 1024;
    static constexpr std::size_t MaximumPreviewFrames = 200000;
    static constexpr std::size_t MaximumStoredValuesPerXdfStream = 2000000;
    static constexpr std::uint64_t MaximumXdfFileBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
    static constexpr std::uint64_t MaximumSamplesPerXdfStream = 100000000ULL;
    static constexpr int MaximumXdfChannels = 65536;
    static constexpr int MaximumXdfStreams = 4096;
    static constexpr int MaximumHeaderBytes = 4 * 1024 * 1024;
    static constexpr int MaximumFindNextRunAttempts = 1000;
};

} // namespace vicon_lsl::gui
