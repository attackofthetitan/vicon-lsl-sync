#include "gui/SessionSequencer.h"

namespace vicon_lsl::gui {

namespace {

// A selected-stream recorder is this app's own process, so it is safe once that
// process is gone. A LabRecorder connection is safe once its shutdown settled
// without a Start that may still be running on the server.
bool recorderSafeToClose(const ShutdownInputs& inputs) {
    return inputs.selected_stream_recorder ? !inputs.owns_running_process
                                           : inputs.recorder_settled_safely;
}

} // namespace

OwnedProcessDecision endOwnedProcessDecision(const ShutdownInputs& inputs) {
    if (!inputs.owns_running_process || inputs.owned_process_end_requested) {
        return {};
    }
    const bool safe = recorderSafeToClose(inputs);
    if (!safe && !inputs.stop_deadline_reached) {
        return {};
    }
    return {true, inputs.stop_deadline_reached && !safe};
}

bool recorderConnectionLostExternally(const ShutdownInputs& inputs) {
    const bool lost = !inputs.recorder_settled_safely && inputs.recorder_shutdown_ready &&
                      (inputs.recorder_connection == RecorderConnectionState::Disconnected ||
                       inputs.recorder_connection == RecorderConnectionState::Error);
    return lost && !inputs.owns_running_process;
}

QStringList shutdownWaitingOn(const ShutdownInputs& inputs) {
    QStringList waiting;
    if (!inputs.bridge_done) waiting.push_back("bridge");
    if (!inputs.preview_done) waiting.push_back("preview");
    if (!inputs.file_done) waiting.push_back("file loading");
    if (!inputs.verification_done) waiting.push_back("file check");

    const bool recorder_done = recorderSafeToClose(inputs) && !inputs.owns_running_process;
    if (!recorder_done && !recorderConnectionLostExternally(inputs)) {
        waiting.push_back("recorder");
    }
    return waiting;
}

QString shutdownStatusText(const QStringList& waiting) {
    return waiting.isEmpty() ? QStringLiteral("Ready to close")
                             : "Closing: waiting for " + waiting.join(", ");
}

} // namespace vicon_lsl::gui
