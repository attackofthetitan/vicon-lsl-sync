#pragma once

#include "gui/SessionState.h"

#include <QString>
#include <QStringList>

namespace vicon_lsl::gui {

// The order a guided session starts, stops, and closes in, as pure decisions
// over component state. BridgeWindow owns the widgets, threads, and processes;
// this owns the question of what should happen next, so the ordering rules can
// be read and tested without a running session behind them.

enum class GuidedStartStep {
    StartBridge,        // no bridge worker yet
    AwaitBridge,        // bridge is starting; nothing to do but wait
    BridgeFailed,       // give up: the bridge cannot start
    StartPreview,       // preview is idle or stopped
    AwaitPreview,       // preview is starting; nothing to do but wait
    PreviewFailed,      // give up: the preview cannot start
    StartRecording,     // every prerequisite is running
};

struct GuidedStartInputs {
    bool recorder_only = false;
    ComponentLifecycleState bridge = ComponentLifecycleState::Idle;
    bool bridge_worker_present = false;
    bool preview_available = false;
    ComponentLifecycleState preview = ComponentLifecycleState::Idle;
};

GuidedStartStep nextGuidedStartStep(const GuidedStartInputs& inputs);

enum class GuidedStopStep {
    StopRecording,      // recording is active and has not been asked to stop
    AwaitRecorder,      // a stop is already in flight
    AwaitVerification,  // the recording file is still being checked
    ShutDownPreview,    // preview still holds inlets
    StopBridge,         // bridge worker still running
    EndOwnedRecorder,   // a recorder this app started is still up
    Finished,           // every owned component has stopped
};

struct GuidedStopInputs {
    bool recording_active_or_pending = false;
    bool recorder_stopping = false;
    bool verification_active = false;
    bool preview_available = false;
    bool preview_shutdown_ready = true;
    bool bridge_worker_present = false;
    bool owns_graphical_recorder = false;
};

GuidedStopStep nextGuidedStopStep(const GuidedStopInputs& inputs);

// Closing runs the same teardown but reports progress instead of driving it,
// and it may force a recorder that will not stop on its own.
struct ShutdownInputs {
    bool bridge_done = false;
    bool preview_done = false;
    bool file_done = false;
    bool verification_done = false;
    bool recorder_settled_safely = false;
    bool recorder_shutdown_ready = false;
    RecorderConnectionState recorder_connection = RecorderConnectionState::Disconnected;
    bool selected_stream_recorder = false;
    bool owns_running_process = false;
    bool stop_deadline_reached = false;
    bool owned_process_end_requested = false;
};

// A recorder started here is closed once the recording has stopped, or once the
// deadline passes with no stop, so closing cannot wait forever on it.
struct OwnedProcessDecision {
    bool end_now = false;
    bool forced_by_deadline = false;
};

OwnedProcessDecision endOwnedProcessDecision(const ShutdownInputs& inputs);

// Whether an external recorder dropped its connection while closing, which is
// reported once but does not hold the window open.
bool recorderConnectionLostExternally(const ShutdownInputs& inputs);

// The components still being waited on, in a stable order, named as they are
// shown to the user. Empty means the window is ready to close. Call this after
// acting on endOwnedProcessDecision so `owns_running_process` is current.
QStringList shutdownWaitingOn(const ShutdownInputs& inputs);

QString shutdownStatusText(const QStringList& waiting);

} // namespace vicon_lsl::gui
