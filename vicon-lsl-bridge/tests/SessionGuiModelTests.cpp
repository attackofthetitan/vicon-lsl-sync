#include "LabRecorderClientTestSupport.h"

#include "gui/CalibrationProfileStore.h"
#include "gui/LabRecorderFilenamePolicy.h"
#include "gui/PerformanceBudgets.h"
#include "gui/RecorderProcessController.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionController.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>

#include <limits>

namespace labrecorder_client_tests {
namespace {

LabRecorderFilenameFields validFields(const QString& root) {
    LabRecorderFilenameFields fields;
    fields.root = root;
    fields.templ = "sub-%p/ses-%s/run-%r_%m";
    fields.participant = "P001";
    fields.session = "S001";
    fields.task = "Reach";
    fields.run = "1";
    fields.acquisition = "vicon";
    fields.modality = "beh";
    return fields;
}

bool hasIssue(const RecordingPathResult& result,
              RecordingPathIssueLevel level,
              const QString& text) {
    for (const RecordingPathIssue& issue : result.issues) {
        if (issue.level == level &&
            (issue.field.contains(text, Qt::CaseInsensitive) ||
             issue.message.contains(text, Qt::CaseInsensitive))) {
            return true;
        }
    }
    return false;
}

vicon_lsl::gui::SessionPreflightInputs readyPreflight(
    const QString& root) {
    vicon_lsl::gui::SessionPreflightInputs inputs;
    inputs.configuration = vicon_lsl::gui::SessionConfiguration();
    inputs.configuration.recording_root = root;
    inputs.configuration.recording_streams.clear();
    inputs.bridge_state = ComponentLifecycleState::Running;
    inputs.bridge_status_recent = true;
    inputs.bridge_effective_rate = 120.0;
    inputs.recorder_connection = RecorderConnectionState::Connected;
    inputs.recorder_recording = RecorderRecordingState::Stopped;
    inputs.recorder_operation = RecorderOperationState::Idle;
    inputs.allowlist_recorder_available = true;
    inputs.path = LabRecorderFilenamePolicy::validate(validFields(root));
    vicon_lsl::gui::StreamIdentity selected;
    selected.name = "Markers";
    selected.source_id = "markers-source";
    selected.present = true;
    selected.selected = true;
    inputs.streams = {selected};
    return inputs;
}

} // namespace

void testNormalizedPathPolicy() {
    QTemporaryDir root;
    expect(root.isValid(), "creates recording path test root");
    if (!root.isValid()) return;

    LabRecorderFilenameFields fields = validFields(root.path());
    RecordingPathResult valid = LabRecorderFilenamePolicy::validate(fields);
    expect(valid.valid(),
           "accepts a normalized writable destination: " +
               valid.summary().toStdString());
    expect(valid.absolute_path.endsWith(".xdf", Qt::CaseInsensitive),
           "appends the XDF extension consistently");
    expect(LabRecorderFilenamePolicy::renderedFilenamePreview(
               valid.normalized_fields) == valid.absolute_path,
           "normalized command fields render the exact displayed destination");
    expect(LabRecorderFilenamePolicy::filenameCommand(
               valid.normalized_fields).contains(
                   "{root:" + valid.normalized_fields.root + "}"),
           "filename command uses the canonical normalized root");

    fields.participant = "P{unsafe}";
    expect(!LabRecorderFilenamePolicy::validate(fields).valid(),
           "rejects protocol-breaking metadata instead of silently changing it");
    fields = validFields(root.path());
    fields.templ = "../escape/run-%r.xdf";
    RecordingPathResult traversal = LabRecorderFilenamePolicy::validate(fields);
    expect(!traversal.valid() && hasIssue(traversal, RecordingPathIssueLevel::Error,
                                          "outside"),
           "blocks traversal outside the study root");

    fields = validFields(root.path());
    fields.templ = "%b.xdf";
    fields.task = "CON";
    expect(hasIssue(LabRecorderFilenamePolicy::validate(fields),
                    RecordingPathIssueLevel::Error, "reserved"),
           "rejects Windows-reserved path names on every platform");

    fields = validFields(root.path());
    RecordingPathResult first = LabRecorderFilenamePolicy::validate(fields);
    QDir().mkpath(QFileInfo(first.absolute_path).absolutePath());
    QFile collision(first.absolute_path);
    expect(collision.open(QIODevice::WriteOnly),
           "creates a collision fixture");
    collision.write("existing");
    collision.close();
    expect(hasIssue(LabRecorderFilenamePolicy::validate(fields),
                    RecordingPathIssueLevel::Error, "already exists"),
           "blocks an existing recording by default");
    RecordingPathValidationOptions overwrite;
    overwrite.allow_overwrite = true;
    RecordingPathResult overwrite_result =
        LabRecorderFilenamePolicy::validate(fields, overwrite);
    expect(overwrite_result.valid() && overwrite_result.hasWarnings(),
           "requires an explicit overwrite policy and keeps a warning visible: " +
               overwrite_result.summary().toStdString());
    if (valid.valid()) {
        expect(LabRecorderFilenamePolicy::findNextRun(fields, 1) == 2,
               "finds the first unused normalized run");
    }

    fields = validFields(root.path());
    RecordingPathValidationOptions low_storage;
    low_storage.storage_warning_bytes =
        (std::numeric_limits<qint64>::max)();
    expect(hasIssue(LabRecorderFilenamePolicy::validate(fields, low_storage),
                    RecordingPathIssueLevel::Warning, "storage"),
           "surfaces the configurable low-storage warning");

    RecordingPathValidationOptions short_path;
    short_path.practical_path_length = 10;
    expect(hasIssue(LabRecorderFilenamePolicy::validate(fields, short_path),
                    RecordingPathIssueLevel::Error, "limit"),
           "enforces the practical path-length limit");

    QFile blocked(root.filePath("blocked"));
    expect(blocked.open(QIODevice::WriteOnly),
           "creates an intermediate-file write failure fixture");
    blocked.close();
    fields.templ = "blocked/child.xdf";
    RecordingPathValidationOptions create_parent;
    create_parent.create_parent_directories = true;
    expect(hasIssue(LabRecorderFilenamePolicy::validate(fields, create_parent),
                    RecordingPathIssueLevel::Error, "could not be created"),
           "reports a destination parent creation failure");
}

void testSessionConfiguration() {
    QTemporaryDir directory;
    expect(directory.isValid(), "creates isolated configuration store");
    if (!directory.isValid()) return;
    QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
    vicon_lsl::gui::SessionConfiguration configuration =
        vicon_lsl::gui::SessionConfigurationStore::load(settings);
    expect(configuration.version ==
               vicon_lsl::gui::SessionConfiguration::CurrentVersion,
           "uses the current configuration schema for a new settings store");

    configuration.record_every_visible_stream = false;
    configuration.recorder_automatic_launch = false;
    configuration.automatic_run_increment = true;
    configuration.allow_overwrite = true;
    configuration.calibration_required = true;
    configuration.recorder_only_mode = true;
    configuration.preview_cache_megabytes = 512;
    configuration.preview_gaze.source_id = "gaze-source";
    configuration.preview_gaze.reconnection =
        vicon_lsl::gui::StreamReconnectionMode::SourceIdentity;
    vicon_lsl::gui::SessionConfigurationStore::save(settings, configuration);
    const auto round_trip =
        vicon_lsl::gui::SessionConfigurationStore::load(settings);
    expect(round_trip.version ==
                   vicon_lsl::gui::SessionConfiguration::CurrentVersion,
           "loads the current configuration schema");
    expect(!round_trip.record_every_visible_stream &&
               !round_trip.recorder_automatic_launch &&
               round_trip.automatic_run_increment &&
               round_trip.allow_overwrite &&
               round_trip.calibration_required &&
               round_trip.recorder_only_mode &&
               round_trip.preview_cache_megabytes == 512,
           "round-trips recorder, path, calibration, and workflow policies");
    expect(round_trip.preview_gaze.source_id == "gaze-source",
           "round-trips identity-first preview binding");

    QString error;
    expect(vicon_lsl::gui::SessionConfigurationStore::savePreset(
               settings, "lab setup", configuration, &error),
           "saves a complete named preset");
    vicon_lsl::gui::SessionConfiguration preset;
    expect(vicon_lsl::gui::SessionConfigurationStore::loadPreset(
               settings, "lab setup", preset, &error) &&
               preset.toJson() == configuration.toJson(),
           "loads a preset with every reproducibility field");

    const QString export_path = directory.filePath("session.json");
    expect(vicon_lsl::gui::SessionConfigurationStore::exportConfiguration(
               export_path, configuration, &error),
           "exports a versioned configuration");
    vicon_lsl::gui::SessionConfiguration imported;
    expect(vicon_lsl::gui::SessionConfigurationStore::importConfiguration(
               export_path, imported, &error) &&
               imported.toJson() == configuration.toJson(),
           "imports the exported configuration without losing fields");

    vicon_lsl::gui::SessionUiState ui_state;
    ui_state.geometry = "geometry";
    ui_state.splitter_state = "splitter";
    ui_state.active_control_tab = 3;
    ui_state.recent_recordings = {"one.xdf", "two.csv"};
    vicon_lsl::gui::SessionConfigurationStore::saveUiState(settings, ui_state);
    const auto ui_round_trip =
        vicon_lsl::gui::SessionConfigurationStore::loadUiState(settings);
    expect(ui_round_trip.geometry == ui_state.geometry &&
               ui_round_trip.splitter_state == ui_state.splitter_state &&
               ui_round_trip.active_control_tab == 3 &&
               ui_round_trip.recent_recordings == ui_state.recent_recordings,
           "stores machine UI state separately from the session preset");

    vicon_lsl::gui::StreamIdentity first;
    first.role = "gaze";
    first.name = "DuplicateGaze";
    first.source_id = "source-b";
    vicon_lsl::gui::StreamIdentity second = first;
    second.source_id = "source-a";
    const QVector<vicon_lsl::gui::StreamIdentity> candidates{first, second};
    vicon_lsl::gui::StreamBinding binding;
    binding.name = "DuplicateGaze";
    binding.source_id = "source-b";
    binding.reconnection =
        vicon_lsl::gui::StreamReconnectionMode::SourceIdentity;
    auto selection = vicon_lsl::gui::selectStreamIdentity(candidates, binding);
    expect(selection.index == 0 && !selection.ambiguous,
           "identity-first selection recovers the exact configured source ID");
    vicon_lsl::gui::StreamIdentity recovered = first;
    recovered.publisher_created_at = 20.0;
    recovered.uid = "new-instance";
    QVector<vicon_lsl::gui::StreamIdentity> recovered_candidates{
        first, recovered, second};
    selection = vicon_lsl::gui::selectStreamIdentity(
        recovered_candidates, binding);
    expect(selection.index == 1 &&
               selection.explanation.contains("newest visible instance"),
           "source-ID recovery chooses the newest publisher instance deterministically");
    binding.source_id = "missing-source";
    selection = vicon_lsl::gui::selectStreamIdentity(candidates, binding);
    expect(selection.index < 0 && !selection.ambiguous &&
               selection.explanation.contains("no name-only fallback"),
           "identity-first reconnection refuses an unexplained name fallback");
    binding.source_id.clear();
    selection = vicon_lsl::gui::selectStreamIdentity(candidates, binding);
    expect(selection.index < 0 && selection.ambiguous,
           "duplicate live names remain visibly ambiguous without a source binding");
    binding.reconnection =
        vicon_lsl::gui::StreamReconnectionMode::FollowName;
    selection = vicon_lsl::gui::selectStreamIdentity(candidates, binding);
    expect(selection.index == 1 && selection.used_name_fallback,
           "Follow by name chooses the deterministic stable identity and reports fallback");
}

void testSessionControllerStateModel() {
    QTemporaryDir root;
    expect(root.isValid(), "creates preflight path root");
    if (!root.isValid()) return;
    vicon_lsl::gui::SessionController controller;
    const QDateTime injected_time =
        QDateTime::fromString("2026-08-24T10:11:12.345Z", Qt::ISODateWithMs);
    controller.setClock([injected_time]() { return injected_time; });
    auto inputs = readyPreflight(root.path());
    PreflightResult passed = controller.runPreflight(inputs);
    expect(!passed.hasRequiredFailures() && passed.canStart(),
           "preflight passes a ready bridge, recorder, and exact path");
    expect(passed.completed_at == injected_time &&
               !controller.eventLog().entries().isEmpty() &&
               controller.eventLog().entries().back().timestamp == injected_time,
           "preflight and state events use the injected GUI clock");

    inputs.path.issues.push_back({
        RecordingPathIssueLevel::Warning,
        "available storage",
        "Available storage is below the configured warning threshold",
        "Free storage before a long run",
    });
    const PreflightResult warning_only = controller.runPreflight(inputs);
    expect(!warning_only.hasRequiredFailures() && warning_only.hasWarnings() &&
               warning_only.canStart(),
           "warning-only preflight remains startable while preserving the warning");

    inputs.bridge_state = ComponentLifecycleState::Starting;
    inputs.bridge_status_recent = false;
    PreflightResult blocked = controller.runPreflight(inputs);
    expect(blocked.hasRequiredFailures() &&
               controller.dashboard().workflow ==
                   vicon_lsl::gui::SessionWorkflowState::PreflightBlocked,
           "preflight blocks missing bridge readiness");
    expect(!controller.overridePreflight("   "),
           "preflight override rejects an empty reason");
    expect(controller.overridePreflight("Recorder-only recovery run") &&
               controller.lastPreflight().override_used &&
               controller.lastPreflight().override_reason ==
                   "Recorder-only recovery run",
           "preflight accepts and persists a deliberate reasoned override");

    inputs.configuration.recorder_only_mode = true;
    PreflightResult recorder_only = controller.runPreflight(inputs);
    expect(!recorder_only.hasRequiredFailures(),
           "explicit recorder-only mode does not require the bridge");

    inputs = readyPreflight(root.path());
    inputs.configuration.record_every_visible_stream = false;
    inputs.streams.clear();
    PreflightResult no_allowlist = controller.runPreflight(inputs);
    expect(no_allowlist.hasRequiredFailures(),
           "exact allowlist preflight blocks an empty visible selection");

    vicon_lsl::gui::StreamBinding required;
    required.role = "gaze";
    required.name = "Gaze";
    required.source_id = "gaze-1";
    required.required = true;
    required.expected_channels = 21;
    required.expected_nominal_rate = 90.0;
    required.expected_coordinate_frame = "shared";
    inputs.configuration.recording_streams = {required};
    vicon_lsl::gui::StreamIdentity missing;
    missing.role = "gaze";
    missing.name = "Gaze";
    missing.source_id = "gaze-1";
    missing.channel_count = 21;
    missing.coordinate_frame = "shared";
    missing.present = false;
    missing.selected = true;
    missing.required = true;
    inputs.streams = {missing};
    expect(controller.runPreflight(inputs).hasRequiredFailures(),
           "a previously selected but absent identity remains a blocking failure");
    missing.present = true;
    missing.metadata_complete = true;
    missing.schema_compatible = true;
    missing.nominal_rate = 90.0;
    missing.freshness_ms = 0;
    inputs.streams = {missing};
    expect(!controller.runPreflight(inputs).hasRequiredFailures(),
           "a fresh identity with matching schema satisfies required-stream checks");

    controller.beginShutdown(1000, true, true, true, true, true);
    expect(controller.shutdownStatus().result ==
               vicon_lsl::gui::ShutdownResult::InProgress,
           "shutdown enters one explicit in-progress state");
    controller.beginShutdown(2000, false, false, false, false, false);
    expect(controller.shutdownStatus().started_at_ms == 1000,
           "repeated close cannot replace the active shutdown sequence");
    const QStringList pending_shutdown =
        controller.shutdownStatus().delayedComponents(2000);
    expect(!pending_shutdown.isEmpty() &&
               pending_shutdown.front().contains("remaining"),
           "shutdown status exposes each pending component deadline countdown");
    controller.updateShutdownDeadlines(
        1000 + vicon_lsl::gui::PerformanceBudgets::RecorderStopDeadlineMs);
    expect(controller.shutdownStatus().result ==
               vicon_lsl::gui::ShutdownResult::DeadlineExceeded &&
               controller.shutdownStatus().ownedRecorderMayBeEnded(
                   1000 + vicon_lsl::gui::PerformanceBudgets::RecorderStopDeadlineMs),
           "shutdown exposes deadline overrun and owned-recorder policy");
    controller.updateShutdownComponent(SessionComponent::Bridge, true, {}, 17000);
    controller.updateShutdownComponent(SessionComponent::Preview, true, {}, 17000);
    controller.updateShutdownComponent(SessionComponent::Recorder, true, {}, 17000);
    controller.updateShutdownComponent(SessionComponent::File, true, {}, 17000);
    controller.updateShutdownComponent(SessionComponent::Verification, true, {}, 17000);
    expect(controller.shutdownStatus().complete() &&
               controller.shutdownStatus().result ==
                   vicon_lsl::gui::ShutdownResult::Completed,
           "shutdown completes only when all required components stop");

    vicon_lsl::gui::SessionController lost_controller;
    lost_controller.beginShutdown(20000, false, false, true, false, false);
    lost_controller.markRecorderConnectionLostDuringShutdown(
        20001, "Remote-control connection unavailable");
    const qsizetype lost_event_count =
        lost_controller.eventLog().entries().size();
    lost_controller.markRecorderConnectionLostDuringShutdown(
        20002, "Remote-control connection unavailable");
    expect(lost_controller.shutdownStatus().result ==
               vicon_lsl::gui::ShutdownResult::RecorderConnectionLost &&
               lost_controller.eventLog().entries().size() == lost_event_count,
           "repeated close polling records recorder connection loss exactly once");

    SessionEventLog log(3);
    log.append(SessionComponent::Bridge, EventSeverity::Error, "persistent error");
    log.append(SessionComponent::Bridge, EventSeverity::Information, "normal one");
    log.append(SessionComponent::Preview, EventSeverity::Information, "normal two");
    log.append(SessionComponent::Recorder, EventSeverity::Information, "normal three");
    expect(log.entries().size() == 3 &&
               log.lastError() == "persistent error",
           "bounded normal events never erase the persistent last error");
    log.acknowledgeLastError();
    expect(log.lastError().isEmpty(),
           "last error clears only on explicit acknowledgement");
}

void testCalibrationProfileStore() {
    using vicon_lsl::gui::CalibrationProfileStore;
    using vicon_lsl::gui::ManagedCalibrationProfile;
    ManagedCalibrationProfile profile = CalibrationProfileStore::defaultProfile();
    profile.quality.sample_count = 60;
    profile.quality.translation_rms_m = 0.0015;
    profile.quality.rotation_rms_degrees = 0.25;
    profile.setup_notes = "Room A, stair 2, tracker origin marked";
    QString reason;
    expect(profile.complete(&reason),
           "managed calibration default contains physical and coordinate identity");
    QString error;
    const ManagedCalibrationProfile parsed =
        ManagedCalibrationProfile::fromJson(profile.toJson(), &error);
    expect(error.isEmpty() && parsed.id == profile.id &&
               parsed.physical_setup_id == profile.physical_setup_id &&
               parsed.gaze_coordinate_frame == profile.gaze_coordinate_frame &&
               parsed.target_coordinate_frame == profile.target_coordinate_frame &&
               parsed.quality.sample_count == 60 &&
               parsed.quality.translation_rms_m ==
                   profile.quality.translation_rms_m,
           "calibration profile round-trips setup identity and quality");

    QVector<ManagedCalibrationProfile> profiles{profile};
    const ManagedCalibrationProfile duplicate =
        CalibrationProfileStore::duplicate(profile, profiles);
    expect(duplicate.id != profile.id && !duplicate.retired,
           "profile duplication creates a distinct active identity");
    profiles.push_back(duplicate);
    expect(CalibrationProfileStore::retire(profiles, duplicate.id) &&
               profiles.back().retired,
           "profile retirement preserves history without deletion");

    QTemporaryDir directory;
    expect(directory.isValid(), "creates calibration profile store root");
    if (!directory.isValid()) return;
    const QString profile_path = directory.filePath("profile.json");
    expect(CalibrationProfileStore::exportProfile(
               profile_path, profile, &error),
           "exports a complete calibration profile");
    ManagedCalibrationProfile imported;
    expect(CalibrationProfileStore::importProfile(
               profile_path, imported, &error) &&
               imported.id == profile.id &&
               imported.quality.rotation_rms_degrees ==
                   profile.quality.rotation_rms_degrees,
           "imports calibration quality and setup metadata");
    QFile model(directory.filePath("stair.obj"));
    expect(model.open(QIODevice::WriteOnly), "creates stair model identity fixture");
    model.write("v 0 0 0\n");
    model.close();
    const QString first_hash =
        CalibrationProfileStore::stairModelIdentity(model.fileName());
    expect(first_hash.size() == 64,
           "stair model identity uses a stable SHA-256 digest");

    QSettings settings(directory.filePath("calibration.ini"),
                       QSettings::IniFormat);
    expect(CalibrationProfileStore::save(settings, profiles, &error),
           "persists active and retired calibration profiles");
    const auto loaded = CalibrationProfileStore::load(settings);
    expect(loaded.size() == 2 && loaded.back().retired,
           "loads retired profiles without losing audit history");

    QJsonObject unsupported = profile.toJson();
    unsupported["version"] = ManagedCalibrationProfile::CurrentVersion + 1;
    ManagedCalibrationProfile rejected =
        ManagedCalibrationProfile::fromJson(unsupported, &error);
    expect(rejected.id.isEmpty() && !error.isEmpty(),
           "rejects unsupported future calibration profile versions");
}

void testRecorderAllowlistPolicy() {
    using vicon_lsl::gui::RecorderProcessController;
    QVector<vicon_lsl::gui::StreamIdentity> streams;
    vicon_lsl::gui::StreamIdentity source_bound;
    source_bound.name = "Gaze";
    source_bound.source_id = "gaze-source";
    source_bound.hostname = "device";
    source_bound.selected = true;
    streams.push_back(source_bound);
    vicon_lsl::gui::StreamIdentity name_bound;
    name_bound.name = "Markers";
    name_bound.hostname = "capture";
    name_bound.selected = true;
    streams.push_back(name_bound);
    vicon_lsl::gui::StreamIdentity ignored;
    ignored.name = "Unrelated";
    ignored.selected = false;
    streams.push_back(ignored);
    QString error;
    const QString absolute =
        QDir::toNativeSeparators(QDir::temp().absoluteFilePath("run.xdf"));
    const QStringList arguments =
        RecorderProcessController::allowlistArguments(
            absolute, streams, &error);
    expect(error.isEmpty() && arguments.size() == 3 &&
               QFileInfo(arguments.front()).isAbsolute(),
           "allowlist arguments contain one absolute output and selected identities only");
    expect(arguments[1] == "source_id='gaze-source'",
           "source ID is the primary exact recorder predicate");
    expect(arguments[2] ==
               "name='Markers' and hostname='capture'",
           "name fallback is constrained by host identity");
    expect(!arguments.join(" ").contains("Unrelated"),
           "unselected visible streams never enter the allowlist");

    source_bound.source_id = "has'both\"quotes";
    streams = {source_bound};
    expect(RecorderProcessController::allowlistArguments(
               absolute, streams, &error).isEmpty() &&
               !error.isEmpty(),
           "unsafe stream identity cannot form a shell-like query literal");
    expect(RecorderProcessController::allowlistArguments(
               "relative.xdf", {name_bound}, &error).isEmpty(),
           "allowlist recorder rejects a relative destination");
}

} // namespace labrecorder_client_tests
