#pragma once

#include "gui/LabRecorderFilenamePolicy.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"

#include <QVector>

namespace vicon_lsl::gui {

// Everything that has to be true before a recording may start, gathered from
// the session so the policy itself can be read and tested on its own.
struct SetupCheckInputs {
    bool recorder_only = false;
    bool bridge_running_with_current_data = false;
    bool record_every_visible_stream = false;
    bool recorder_connected = false;
    bool selected_stream_recorder_available = false;
    bool recorder_idle = false;
    bool calibration_required = false;
    bool stair_model_loaded = false;
    SessionCalibrationState calibration = SessionCalibrationState::Manual;
};

// A required stream is ready only when it is visible, recently updated, and
// still matches the shape the saved configuration expects.
bool requiredStreamReady(const StreamBinding& binding, const QVector<StreamIdentity>& inventory);

SetupCheckResult runSetupCheck(const SetupCheckInputs& inputs,
                               const SessionConfiguration& configuration,
                               const RecordingPathResult& path,
                               const QVector<StreamIdentity>& inventory);

} // namespace vicon_lsl::gui
