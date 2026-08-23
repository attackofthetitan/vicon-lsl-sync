#include "gui/BridgeWindowSettings.h"

#include <QDir>
#include <QSettings>

#include "StreamDefaults.h"

namespace vicon_lsl::gui_detail {

namespace {

constexpr auto kSettingsOrganization = "ViconLSL";
constexpr auto kSettingsApplication = "ViconLSLBridge";
constexpr auto kDefaultRecordingTemplate =
    "sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf";

} // namespace

BridgeWindowSettings loadBridgeWindowSettings() {
    QSettings source(kSettingsOrganization, kSettingsApplication);
    return loadBridgeWindowSettings(source);
}

BridgeWindowSettings loadBridgeWindowSettings(QSettings& source) {
    BridgeWindowSettings settings;
    settings.server = source.value("server", "localhost:801").toString();
    settings.marker_stream = source.value(
        "markerStream", vicon_lsl::stream_defaults::ViconMarkers).toString();
    settings.segment_stream = source.value(
        "segmentStream", vicon_lsl::stream_defaults::ViconSegments).toString();
    settings.recording_root = source.value("recordingRoot", QDir::homePath()).toString();
    settings.recording_template = source.value(
        "recordingTemplate", kDefaultRecordingTemplate).toString();
    settings.participant = source.value("participant", "P001").toString();
    settings.session = source.value("session", "S001").toString();
    settings.task = source.value("task", "Task").toString();
    settings.run = source.value("run", 1).toInt();
    settings.acquisition = source.value("acquisition", "vicon").toString();
    settings.modality = source.value("modality", "beh").toString();
    settings.labrecorder_executable = source.value("labRecorderExecutable", "").toString();
    settings.labrecorder_host = source.value("labRecorderHost", "localhost").toString();
    settings.labrecorder_port = source.value("labRecorderPort", 22345).toInt();
    return settings;
}

void saveBridgeWindowSettings(const BridgeWindowSettings& settings) {
    QSettings destination(kSettingsOrganization, kSettingsApplication);
    saveBridgeWindowSettings(destination, settings);
}

void saveBridgeWindowSettings(QSettings& destination,
                              const BridgeWindowSettings& settings) {
    destination.setValue("server", settings.server);
    destination.setValue("markerStream", settings.marker_stream);
    destination.setValue("segmentStream", settings.segment_stream);
    destination.setValue("recordingRoot", settings.recording_root);
    destination.setValue("recordingTemplate", settings.recording_template);
    destination.setValue("participant", settings.participant);
    destination.setValue("session", settings.session);
    destination.setValue("task", settings.task);
    destination.setValue("run", settings.run);
    destination.setValue("acquisition", settings.acquisition);
    destination.setValue("modality", settings.modality);
    destination.setValue("labRecorderExecutable", settings.labrecorder_executable);
    destination.setValue("labRecorderHost", settings.labrecorder_host);
    destination.setValue("labRecorderPort", settings.labrecorder_port);
}

} // namespace vicon_lsl::gui_detail
