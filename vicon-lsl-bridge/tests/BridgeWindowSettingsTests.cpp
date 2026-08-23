#include "LabRecorderClientTestSupport.h"
#include "gui/BridgeWindowSettings.h"

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

namespace labrecorder_client_tests {

void testBridgeWindowSettingsContract() {
    QTemporaryDir settings_root;
    expect(settings_root.isValid(), "creates isolated settings directory");
    if (!settings_root.isValid()) {
        return;
    }

    QSettings backend(settings_root.filePath("window-settings.ini"),
                      QSettings::IniFormat);

    const auto defaults = vicon_lsl::gui_detail::loadBridgeWindowSettings(backend);
    expect(defaults.server == "localhost:801", "keeps default Vicon server");
    expect(defaults.marker_stream == "ViconMarkers", "keeps default marker stream");
    expect(defaults.segment_stream == "ViconSegments", "keeps default segment stream");
    expect(defaults.recording_root == QDir::homePath(),
           "keeps default recording root");
    expect(defaults.participant == "P001" && defaults.session == "S001",
           "keeps default participant and session");
    expect(defaults.task == "Task" && defaults.run == 1,
           "keeps default task and run");
    expect(defaults.acquisition == "vicon" && defaults.modality == "beh",
           "keeps default acquisition and modality");
    expect(defaults.labrecorder_executable.isEmpty() &&
               defaults.labrecorder_host == "localhost" &&
               defaults.labrecorder_port == 22345,
           "keeps default LabRecorder settings");
    expect(defaults.recording_template ==
               "sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf",
           "keeps default recording template");

    vicon_lsl::gui_detail::BridgeWindowSettings values;
    values.server = "capture:802";
    values.marker_stream = "Markers";
    values.segment_stream = "Segments";
    values.recording_root = "C:/Study";
    values.recording_template = "sub-%p/run-%r.xdf";
    values.participant = "P042";
    values.session = "S003";
    values.task = "Reach";
    values.run = 7;
    values.acquisition = "mocap";
    values.modality = "motion";
    values.labrecorder_executable = "C:/Tools/LabRecorder.exe";
    values.labrecorder_host = "recorder";
    values.labrecorder_port = 23456;
    vicon_lsl::gui_detail::saveBridgeWindowSettings(backend, values);

    backend.sync();
    expect(backend.status() == QSettings::NoError,
           "writes extracted window settings without a backend error");
    expect(backend.value("server").toString() == values.server,
           "preserves server settings key");
    expect(backend.value("markerStream").toString() == values.marker_stream &&
               backend.value("segmentStream").toString() == values.segment_stream,
           "preserves stream settings keys");
    expect(backend.value("recordingRoot").toString() == values.recording_root &&
               backend.value("recordingTemplate").toString() == values.recording_template,
           "preserves recording path settings keys");
    expect(backend.value("participant").toString() == values.participant &&
               backend.value("session").toString() == values.session &&
               backend.value("task").toString() == values.task &&
               backend.value("run").toInt() == values.run,
           "preserves recording metadata settings keys");
    expect(backend.value("acquisition").toString() == values.acquisition &&
               backend.value("modality").toString() == values.modality,
           "preserves acquisition settings keys");
    expect(backend.value("labRecorderExecutable").toString() ==
               values.labrecorder_executable &&
               backend.value("labRecorderHost").toString() == values.labrecorder_host &&
               backend.value("labRecorderPort").toInt() == values.labrecorder_port,
           "preserves LabRecorder settings keys");

    const auto round_trip = vicon_lsl::gui_detail::loadBridgeWindowSettings(backend);
    expect(round_trip.server == values.server &&
               round_trip.marker_stream == values.marker_stream &&
               round_trip.segment_stream == values.segment_stream &&
               round_trip.recording_root == values.recording_root &&
               round_trip.recording_template == values.recording_template &&
               round_trip.participant == values.participant &&
               round_trip.session == values.session &&
               round_trip.task == values.task &&
               round_trip.run == values.run &&
               round_trip.acquisition == values.acquisition &&
               round_trip.modality == values.modality &&
               round_trip.labrecorder_executable == values.labrecorder_executable &&
               round_trip.labrecorder_host == values.labrecorder_host &&
               round_trip.labrecorder_port == values.labrecorder_port,
           "round-trips every extracted window setting");
}

} // namespace labrecorder_client_tests
