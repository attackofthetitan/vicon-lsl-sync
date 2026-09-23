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

// Close a recorder started here when it stops recording or the deadline passes.
struct OwnedProcessDecision {
    bool end_now = false;
    bool forced_by_deadline = false;
};

OwnedProcessDecision endOwnedProcessDecision(const ShutdownInputs& inputs);

// A lost external recorder connection does not prevent the window from closing.
bool recorderConnectionLostExternally(const ShutdownInputs& inputs);

// Lists what still needs to stop. Call after acting on endOwnedProcessDecision
// so owns_running_process is current. An empty list means the window can close.
QStringList shutdownWaitingOn(const ShutdownInputs& inputs);

QString shutdownStatusText(const QStringList& waiting);

} // namespace vicon_lsl::gui
