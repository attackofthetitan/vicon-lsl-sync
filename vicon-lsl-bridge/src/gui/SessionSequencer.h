#pragma once

#include "gui/SessionState.h"

#include <QString>
#include <QStringList>

namespace vicon_lsl::gui {

// Tracks what still needs to stop before the window can close.
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
