#pragma once

// NotConfigured means the stream was never opened or was closed after a failed
// send, while a stream with an empty layout reports Pushed.
enum class StreamPushResult {
    NotConfigured,
    Pushed,
    Failed,
};
