#pragma once

#include "gui/LabRecorderFilenamePolicy.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"

#include <QVector>

namespace vicon_lsl::gui {

// The facts the setup check needs, gathered in one place so the check can be
// tested on its own.
struct SetupCheckInputs {
    bool recorder_only = false;
    bool bridge_running_with_current_data = false;
    bool record_every_visible_stream = false;
    bool recorder_connected = false;
    bool selected_stream_recorder_available = false;
    bool recorder_idle = false;
    bool calibration_required = false;
    bool stair_model_loaded = false;
    SessionCalibrationState calibration = SessionCalibrationState::Uncalibrated;
};

// A required stream is ready when it is visible, recently updated, and still has
// the channel count and coordinate name the settings expect.
bool requiredStreamReady(const StreamBinding& binding, const QVector<StreamIdentity>& inventory);

SetupCheckResult runSetupCheck(const SetupCheckInputs& inputs,
                               const SessionConfiguration& configuration,
                               const RecordingPathResult& path,
                               const QVector<StreamIdentity>& inventory);

} // namespace vicon_lsl::gui
