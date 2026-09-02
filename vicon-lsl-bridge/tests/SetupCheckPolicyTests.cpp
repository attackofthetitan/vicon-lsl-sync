#include "LabRecorderClientTestSupport.h"

#include "gui/SetupCheckPolicy.h"

namespace labrecorder_client_tests {

using vicon_lsl::gui::StreamBinding;
using vicon_lsl::gui::StreamIdentity;
using vicon_lsl::gui::requiredStreamReady;

namespace {

using vicon_lsl::gui::SetupCheckInputs;

// Every prerequisite satisfied: bridge streaming, recorder connected and idle.
SetupCheckInputs readyInputs() {
    SetupCheckInputs inputs;
    inputs.recorder_only = false;
    inputs.bridge_running_with_current_data = true;
    inputs.record_every_visible_stream = true;
    inputs.recorder_connected = true;
    inputs.selected_stream_recorder_available = true;
    inputs.recorder_idle = true;
    inputs.calibration_required = false;
    return inputs;
}

RecordingPathResult validPath() {
    RecordingPathResult path;
    path.absolute_path = "/tmp/session.xdf";
    return path;
}

bool failed(const SetupCheckResult& result, SessionComponent component) {
    for (const SetupCheckItem& item : result.items) {
        if (item.component == component && item.level == SetupCheckLevel::Required && !item.passed) {
            return true;
        }
    }
    return false;
}

bool mentions(const SetupCheckResult& result, const QString& text) {
    for (const SetupCheckItem& item : result.items) {
        if (item.message.contains(text)) return true;
    }
    return false;
}

} // namespace

void testSetupCheckRequiresEachComponent() {
    vicon_lsl::gui::SessionConfiguration configuration;
    QVector<StreamIdentity> inventory;

    SetupCheckResult result =
        vicon_lsl::gui::runSetupCheck(readyInputs(), configuration, validPath(), inventory);
    expect(!result.hasRequiredFailures(), "a fully prepared session passes the setup check");

    SetupCheckInputs inputs = readyInputs();
    inputs.bridge_running_with_current_data = false;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), inventory);
    expect(failed(result, SessionComponent::Bridge),
           "a bridge that is not sending current data fails the check");

    // A recorder-only session does not need the bridge, so the same state passes.
    inputs.recorder_only = true;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), inventory);
    expect(!result.hasRequiredFailures(), "a recorder-only session does not require the bridge");

    inputs = readyInputs();
    inputs.recorder_idle = false;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), inventory);
    expect(failed(result, SessionComponent::Recorder),
           "a recorder that is busy fails the check");

    inputs = readyInputs();
    inputs.recorder_connected = false;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), inventory);
    expect(failed(result, SessionComponent::Recorder),
           "recording every visible stream requires a connected recorder");

    // Selected-stream recording needs the command-line recorder instead.
    inputs.record_every_visible_stream = false;
    inputs.selected_stream_recorder_available = false;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), inventory);
    expect(failed(result, SessionComponent::Recorder),
           "selected-stream recording requires the command-line recorder");
}

void testSetupCheckReportsPathAndStreamProblems() {
    vicon_lsl::gui::SessionConfiguration configuration;
    QVector<StreamIdentity> inventory;

    RecordingPathResult blocked;
    blocked.issues.push_back({RecordingPathIssueLevel::Error, "root",
                              "The folder cannot be written", "Choose another folder"});
    SetupCheckResult result =
        vicon_lsl::gui::runSetupCheck(readyInputs(), configuration, blocked, inventory);
    expect(failed(result, SessionComponent::Path), "an unusable destination fails the check");

    // A warning about the destination is reported without blocking the start.
    RecordingPathResult warned = validPath();
    warned.issues.push_back({RecordingPathIssueLevel::Warning, "storage",
                             "Free space is low", "Free some space"});
    result = vicon_lsl::gui::runSetupCheck(readyInputs(), configuration, warned, inventory);
    expect(!result.hasRequiredFailures() && result.hasWarnings(),
           "a destination warning is reported but does not block recording");

    // Selected-stream recording needs at least one stream chosen.
    SetupCheckInputs inputs = readyInputs();
    inputs.record_every_visible_stream = false;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), inventory);
    expect(mentions(result, "No recording streams are selected"),
           "selected-stream recording requires a chosen stream");
}

void testSetupCheckRequiredStreamReadiness() {
    StreamBinding binding;
    binding.name = "Gaze";
    binding.source_id = "g";
    binding.role = "gaze";
    binding.required = true;
    binding.expected_channels = 21;

    StreamIdentity gaze;
    gaze.name = "Gaze";
    gaze.source_id = "g";
    gaze.present = true;
    gaze.schema_compatible = true;
    gaze.channel_count = 21;
    gaze.freshness_ms = 10;

    QVector<StreamIdentity> inventory{gaze};
    expect(requiredStreamReady(binding, inventory), "a visible, current, matching stream is ready");

    // A stream that has never been measured is accepted; one measured long ago
    // is not, because the source has gone quiet.
    inventory[0].freshness_ms = -1;
    expect(requiredStreamReady(binding, inventory), "an unmeasured stream is accepted");
    inventory[0].freshness_ms = 5000;
    expect(!requiredStreamReady(binding, inventory), "a stream that stopped updating is not ready");

    inventory[0].freshness_ms = 10;
    inventory[0].channel_count = 8;
    expect(!requiredStreamReady(binding, inventory),
           "a stream whose channel count changed is not ready");

    inventory[0].channel_count = 21;
    inventory[0].schema_compatible = false;
    expect(!requiredStreamReady(binding, inventory),
           "a stream with an unexpected layout is not ready");

    inventory[0].schema_compatible = true;
    inventory[0].present = false;
    expect(!requiredStreamReady(binding, inventory), "a stream that is not visible is not ready");

    expect(!requiredStreamReady(binding, {}), "a stream that is absent entirely is not ready");

    binding.expected_coordinate_frame = "rub";
    inventory[0].present = true;
    inventory[0].coordinate_frame = "rdf";
    expect(!requiredStreamReady(binding, inventory),
           "a stream in a different coordinate frame is not ready");
    inventory[0].coordinate_frame = "rub";
    expect(requiredStreamReady(binding, inventory),
           "a stream in the expected coordinate frame is ready");
}

void testSetupCheckCalibration() {
    vicon_lsl::gui::SessionConfiguration configuration;
    SetupCheckInputs inputs = readyInputs();
    inputs.calibration_required = true;
    inputs.stair_model_loaded = false;
    inputs.calibration = SessionCalibrationState::Manual;

    SetupCheckResult result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), {});
    expect(failed(result, SessionComponent::Calibration),
           "a session that requires calibration fails without one");

    inputs.stair_model_loaded = true;
    inputs.calibration = SessionCalibrationState::AutomaticSession;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), {});
    expect(!failed(result, SessionComponent::Calibration),
           "a calibrated session with the stair model loaded passes");

    inputs.calibration = SessionCalibrationState::SavedProfile;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), {});
    expect(!failed(result, SessionComponent::Calibration),
           "a saved calibration profile satisfies the check");

    // The model has to be loaded even when a calibration is applied.
    inputs.stair_model_loaded = false;
    result = vicon_lsl::gui::runSetupCheck(inputs, configuration, validPath(), {});
    expect(failed(result, SessionComponent::Calibration),
           "a calibration without the stair model is not enough");
}

} // namespace labrecorder_client_tests
