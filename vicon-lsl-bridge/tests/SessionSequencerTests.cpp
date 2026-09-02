#include "LabRecorderClientTestSupport.h"

#include "gui/SessionSequencer.h"

namespace labrecorder_client_tests {
namespace {

using vicon_lsl::gui::GuidedStartInputs;
using vicon_lsl::gui::GuidedStartStep;
using vicon_lsl::gui::GuidedStopInputs;
using vicon_lsl::gui::GuidedStopStep;
using vicon_lsl::gui::ShutdownInputs;
using vicon_lsl::gui::endOwnedProcessDecision;
using vicon_lsl::gui::nextGuidedStartStep;
using vicon_lsl::gui::nextGuidedStopStep;
using vicon_lsl::gui::recorderConnectionLostExternally;
using vicon_lsl::gui::shutdownStatusText;
using vicon_lsl::gui::shutdownWaitingOn;

// A session with every component available and nothing started yet.
GuidedStartInputs startInputs() {
    GuidedStartInputs inputs;
    inputs.recorder_only = false;
    inputs.bridge = ComponentLifecycleState::Idle;
    inputs.bridge_worker_present = false;
    inputs.preview_available = true;
    inputs.preview = ComponentLifecycleState::Idle;
    return inputs;
}

GuidedStopInputs stopInputs() {
    GuidedStopInputs inputs;
    inputs.recording_active_or_pending = false;
    inputs.recorder_stopping = false;
    inputs.verification_active = false;
    inputs.preview_available = true;
    inputs.preview_shutdown_ready = true;
    inputs.bridge_worker_present = false;
    inputs.owns_graphical_recorder = false;
    return inputs;
}

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

void testGuidedStartOrder() {
    GuidedStartInputs inputs = startInputs();
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::StartBridge,
           "a guided start begins with the bridge");

    inputs.bridge_worker_present = true;
    inputs.bridge = ComponentLifecycleState::Starting;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::AwaitBridge,
           "a bridge that is already starting is waited for, not started twice");

    inputs.bridge = ComponentLifecycleState::Running;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::StartPreview,
           "the preview starts only once the bridge is running");

    inputs.preview = ComponentLifecycleState::Starting;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::AwaitPreview,
           "a preview that is already starting is waited for, not started twice");

    inputs.preview = ComponentLifecycleState::Running;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::StartRecording,
           "recording starts once the bridge and preview are running");

    // A preview that stopped on its own is started again rather than waited on.
    inputs.preview = ComponentLifecycleState::Stopped;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::StartPreview,
           "a stopped preview is started again");

    inputs.preview_available = false;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::StartRecording,
           "a session without a preview goes straight to recording");
}

void testGuidedStartFailuresAndRecorderOnly() {
    GuidedStartInputs inputs = startInputs();
    inputs.bridge = ComponentLifecycleState::Failed;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::BridgeFailed,
           "a failed bridge stops the guided start");

    // Recorder-only sessions do not need the bridge at all, so its state is
    // not allowed to block or fail the start.
    inputs.recorder_only = true;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::StartPreview,
           "a recorder-only session ignores a failed bridge");

    inputs.preview = ComponentLifecycleState::Failed;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::PreviewFailed,
           "a failed preview stops the guided start");

    inputs.preview_available = false;
    expect(nextGuidedStartStep(inputs) == GuidedStartStep::StartRecording,
           "a recorder-only session with no preview records immediately");
}

void testGuidedStopOrder() {
    GuidedStopInputs inputs = stopInputs();
    inputs.recording_active_or_pending = true;
    expect(nextGuidedStopStep(inputs) == GuidedStopStep::StopRecording,
           "a guided stop stops the recording first");

    inputs.recorder_stopping = true;
    expect(nextGuidedStopStep(inputs) == GuidedStopStep::AwaitRecorder,
           "a stop already in flight is not requested again");

    inputs.recording_active_or_pending = false;
    inputs.recorder_stopping = false;
    inputs.verification_active = true;
    expect(nextGuidedStopStep(inputs) == GuidedStopStep::AwaitVerification,
           "the file check finishes before anything else is torn down");

    inputs.verification_active = false;
    inputs.preview_shutdown_ready = false;
    expect(nextGuidedStopStep(inputs) == GuidedStopStep::ShutDownPreview,
           "the preview releases its inlets before the bridge stops");

    inputs.preview_shutdown_ready = true;
    inputs.bridge_worker_present = true;
    expect(nextGuidedStopStep(inputs) == GuidedStopStep::StopBridge,
           "the bridge stops after the preview");

    inputs.bridge_worker_present = false;
    inputs.owns_graphical_recorder = true;
    expect(nextGuidedStopStep(inputs) == GuidedStopStep::EndOwnedRecorder,
           "a recorder started here is closed last");

    inputs.owns_graphical_recorder = false;
    expect(nextGuidedStopStep(inputs) == GuidedStopStep::Finished,
           "the guided stop finishes once nothing is left running");
}

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
