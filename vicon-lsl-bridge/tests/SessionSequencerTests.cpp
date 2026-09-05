#include "LabRecorderClientTestSupport.h"

#include "gui/SessionSequencer.h"

namespace labrecorder_client_tests {
namespace {

using vicon_lsl::gui::ShutdownInputs;
using vicon_lsl::gui::endOwnedProcessDecision;
using vicon_lsl::gui::recorderConnectionLostExternally;
using vicon_lsl::gui::shutdownStatusText;
using vicon_lsl::gui::shutdownWaitingOn;

// Closing with every component already stopped and no recorder of our own.
ShutdownInputs settledShutdown() {
    ShutdownInputs inputs;
    inputs.bridge_done = true;
    inputs.preview_done = true;
    inputs.file_done = true;
    inputs.verification_done = true;
    inputs.recorder_settled_safely = true;
    inputs.recorder_shutdown_ready = true;
    inputs.recorder_connection = RecorderConnectionState::Connected;
    inputs.selected_stream_recorder = false;
    inputs.owns_running_process = false;
    inputs.stop_deadline_reached = false;
    inputs.owned_process_end_requested = false;
    return inputs;
}

} // namespace

void testShutdownWaitsForEachComponent() {
    expect(shutdownWaitingOn(settledShutdown()).isEmpty(),
           "a settled session is ready to close");
    expect(shutdownStatusText({}) == "Ready to close",
           "an empty wait list reads as ready to close");

    ShutdownInputs inputs = settledShutdown();
    inputs.bridge_done = false;
    inputs.preview_done = false;
    inputs.file_done = false;
    inputs.verification_done = false;
    inputs.recorder_settled_safely = false;
    const QStringList waiting = shutdownWaitingOn(inputs);
    expect(waiting == QStringList({"bridge", "preview", "file loading", "file check", "recorder"}),
           "every outstanding component is named in a stable order");
    expect(shutdownStatusText(waiting) ==
               "Closing: waiting for bridge, preview, file loading, file check, recorder",
           "the wait list is reported as one readable line");

    // A recorder this app owns keeps closing open even after the connection
    // settled, because its process is still up.
    ShutdownInputs owned = settledShutdown();
    owned.owns_running_process = true;
    expect(shutdownWaitingOn(owned) == QStringList({"recorder"}),
           "an owned recorder process is waited for after the connection settles");
}

void testShutdownEndsOwnedRecorderOnceOrOnDeadline() {
    ShutdownInputs inputs = settledShutdown();
    inputs.owns_running_process = true;
    vicon_lsl::gui::OwnedProcessDecision decision = endOwnedProcessDecision(inputs);
    expect(decision.end_now && !decision.forced_by_deadline,
           "an owned recorder is closed once the recording has settled");

    inputs.owned_process_end_requested = true;
    expect(!endOwnedProcessDecision(inputs).end_now,
           "an owned recorder is never asked to close twice");

    // A recorder that never reports a clean stop must not hold closing open
    // forever; the deadline closes it and says so.
    ShutdownInputs stuck = settledShutdown();
    stuck.owns_running_process = true;
    stuck.recorder_settled_safely = false;
    expect(!endOwnedProcessDecision(stuck).end_now,
           "an unsettled recorder is given until the deadline to stop");

    stuck.stop_deadline_reached = true;
    decision = endOwnedProcessDecision(stuck);
    expect(decision.end_now && decision.forced_by_deadline,
           "the stop deadline closes a recorder that never settled");

    // A selected-stream recorder is only ever safe once its process is gone, so
    // while it is running the deadline is the only thing that closes it.
    ShutdownInputs selected = settledShutdown();
    selected.selected_stream_recorder = true;
    selected.owns_running_process = true;
    expect(!endOwnedProcessDecision(selected).end_now,
           "a running selected-stream recorder is not closed before the deadline");
    selected.stop_deadline_reached = true;
    expect(endOwnedProcessDecision(selected).forced_by_deadline,
           "a selected-stream recorder that overruns is closed by the deadline");
}

void testShutdownReportsLostExternalRecorder() {
    ShutdownInputs inputs = settledShutdown();
    inputs.recorder_settled_safely = false;
    inputs.recorder_connection = RecorderConnectionState::Disconnected;
    expect(recorderConnectionLostExternally(inputs),
           "an external recorder that drops while closing is reported");
    expect(shutdownWaitingOn(inputs).isEmpty(),
           "a lost external recorder does not hold the window open");

    inputs.recorder_connection = RecorderConnectionState::Error;
    expect(recorderConnectionLostExternally(inputs),
           "a recorder connection error while closing is reported");

    // A recorder we started is ours to close, so a dropped remote-control
    // connection is not treated as an external loss.
    inputs.owns_running_process = true;
    expect(!recorderConnectionLostExternally(inputs),
           "an owned recorder process is closed rather than reported as lost");

    ShutdownInputs unfinished = settledShutdown();
    unfinished.recorder_settled_safely = false;
    unfinished.recorder_shutdown_ready = false;
    unfinished.recorder_connection = RecorderConnectionState::Disconnected;
    expect(!recorderConnectionLostExternally(unfinished),
           "a shutdown still in flight is not reported as a lost connection");
}

} // namespace labrecorder_client_tests
