#pragma once

// NotConfigured means the stream is not open: it was never initialized, or it
// was closed after a failed send. A stream with an empty layout reports Pushed.
enum class StreamPushResult {
    NotConfigured,
    Pushed,
    Failed,
};
