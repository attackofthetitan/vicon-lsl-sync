#include "gui/SessionConfiguration.h"

#include "StreamDefaults.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace vicon_lsl::gui {
namespace {

QString reconnectionText(StreamReconnectionMode mode) {
    return mode == StreamReconnectionMode::FollowName ? "follow-name" : "source-id";
}

StreamReconnectionMode parseReconnection(const QString& text) {
    return text.compare("follow-name", Qt::CaseInsensitive) == 0
        ? StreamReconnectionMode::FollowName
        : StreamReconnectionMode::SourceIdentity;
}

StreamBinding defaultBinding(const QString& role,
                             const QString& name,
                             bool required,
                             int channels = 0,
                             double rate = 0.0,
                             const QString& frame = {}) {
    StreamBinding binding;
    binding.role = role;
    binding.name = name;
    binding.required = required;
    binding.expected_channels = channels;
    binding.expected_nominal_rate = rate;
    binding.expected_coordinate_frame = frame;
    return binding;
}

QString jsonString(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool readBool(const QJsonObject& object, const char* key, bool fallback) {
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isBool() ? value.toBool() : fallback;
}

int readInt(const QJsonObject& object, const char* key, int fallback) {
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isDouble() ? value.toInt(fallback) : fallback;
}

double readDouble(const QJsonObject& object, const char* key, double fallback) {
    const QJsonValue value = object.value(QLatin1String(key));
    const double result = value.isDouble() ? value.toDouble(fallback) : fallback;
    return std::isfinite(result) ? result : fallback;
}

QJsonObject vec3ToJson(const PreviewVec3& value) {
    return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

PreviewVec3 readVec3(const QJsonObject& object, const PreviewVec3& fallback = {}) {
    return {
        readDouble(object, "x", fallback.x),
        readDouble(object, "y", fallback.y),
        readDouble(object, "z", fallback.z),
    };
}

QString readString(const QJsonObject& object, const char* key, const QString& fallback = {}) {
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isString() ? value.toString() : fallback;
}

} // namespace

QString StreamIdentity::stableKey() const {
    if (!source_id.trimmed().isEmpty()) {
        return role + "|source:" + source_id.trimmed();
    }
    return role + "|name:" + name.trimmed() + "|host:" + hostname.trimmed() +
           "|session:" + session_id.trimmed();
}

QString StreamIdentity::displayText() const {
    QString text = name;
    if (!source_id.isEmpty()) text += " [" + source_id + "]";
    if (!hostname.isEmpty()) text += " @ " + hostname;
    return text;
}

QJsonObject StreamIdentity::toJson() const {
    return {
        {"role", role}, {"name", name}, {"type", type}, {"sourceId", source_id},
        {"hostname", hostname}, {"sessionId", session_id}, {"uid", uid},
        {"publisherCreatedAt", publisher_created_at},
        {"channelCount", channel_count}, {"nominalRate", nominal_rate},
        {"effectiveRate", effective_rate},
        {"coordinateFrame", coordinate_frame}, {"metadataComplete", metadata_complete},
        {"schemaCompatible", schema_compatible}, {"present", present},
        {"selected", selected},
        {"required", required}, {"freshnessMs", static_cast<double>(freshness_ms)},
        {"discoveredAt", discovered_at.toString(Qt::ISODateWithMs)},
        {"warning", warning},
    };
}

StreamIdentity StreamIdentity::fromJson(const QJsonObject& object) {
    StreamIdentity identity;
    identity.role = readString(object, "role");
    identity.name = readString(object, "name");
    identity.type = readString(object, "type");
    identity.source_id = readString(object, "sourceId");
    identity.hostname = readString(object, "hostname");
    identity.session_id = readString(object, "sessionId");
    identity.uid = readString(object, "uid");
    identity.publisher_created_at = readDouble(object, "publisherCreatedAt", 0.0);
    identity.channel_count = readInt(object, "channelCount", 0);
    identity.nominal_rate = readDouble(object, "nominalRate", 0.0);
    identity.effective_rate = readDouble(object, "effectiveRate", 0.0);
    identity.coordinate_frame = readString(object, "coordinateFrame");
    identity.metadata_complete = readBool(
        object, "metadataComplete", identity.metadata_complete);
    identity.schema_compatible = readBool(
        object, "schemaCompatible", identity.schema_compatible);
    identity.present = readBool(object, "present", identity.present);
    identity.selected = readBool(object, "selected", identity.selected);
    identity.required = readBool(object, "required", identity.required);
    identity.freshness_ms = static_cast<qint64>(readDouble(object, "freshnessMs", -1));
    identity.discovered_at = QDateTime::fromString(readString(object, "discoveredAt"),
                                                   Qt::ISODateWithMs);
    identity.warning = readString(object, "warning");
    return identity;
}

StreamIdentitySelection selectStreamIdentity(
    const QVector<StreamIdentity>& candidates,
    const StreamBinding& binding) {
    QVector<int> matches;
    for (int index = 0; index < candidates.size(); ++index) {
        if (candidates[index].name == binding.name) matches.push_back(index);
    }
    std::stable_sort(matches.begin(), matches.end(),
        [&candidates](int left, int right) {
            const StreamIdentity& a = candidates[left];
            const StreamIdentity& b = candidates[right];
            return std::tie(a.source_id, a.hostname, a.session_id, a.uid) <
                   std::tie(b.source_id, b.hostname, b.session_id, b.uid);
        });
    if (matches.isEmpty()) {
        return {-1, false, false,
                "No visible stream matches " + binding.name};
    }

    const QString required_source = binding.source_id.trimmed();
    if (!required_source.isEmpty()) {
        QVector<int> exact;
        for (int index : matches) {
            if (candidates[index].source_id == required_source) {
                exact.push_back(index);
            }
        }
        if (!exact.isEmpty()) {
            std::stable_sort(exact.begin(), exact.end(),
                [&candidates](int left, int right) {
                    const StreamIdentity& a = candidates[left];
                    const StreamIdentity& b = candidates[right];
                    if (a.publisher_created_at != b.publisher_created_at) {
                        return a.publisher_created_at > b.publisher_created_at;
                    }
                    return std::tie(a.hostname, a.session_id, a.uid) <
                           std::tie(b.hostname, b.session_id, b.uid);
                });
            return {exact.front(), false, false,
                    exact.size() > 1
                        ? "Selected the newest visible instance of configured source ID " +
                              required_source + " from " +
                              QString::number(exact.size()) +
                              " recovered instances"
                        : "Selected configured source ID " + required_source};
        }
        if (binding.reconnection == StreamReconnectionMode::SourceIdentity) {
            return {-1, false, false,
                    "Selected source ID " + required_source +
                        " is unavailable; the app did not switch to another stream with the same name"};
        }
    }

    if (matches.size() > 1 &&
        binding.reconnection != StreamReconnectionMode::FollowName) {
        return {-1, true, false,
                "Multiple streams named " + binding.name +
                    " are available; select a source ID or enable Follow by name"};
    }

    const bool fallback = !required_source.isEmpty() || matches.size() > 1;
    return {matches.front(), false, fallback,
            fallback
                ? "Follow by name selected the first matching source"
                : "Selected the only matching stream"};
}

bool StreamBinding::matches(const StreamIdentity& identity) const {
    if (!source_id.trimmed().isEmpty() &&
        reconnection == StreamReconnectionMode::SourceIdentity) {
        return identity.source_id == source_id;
    }
    return identity.name == name;
}

QJsonObject StreamBinding::toJson() const {
    return {
        {"role", role}, {"name", name}, {"sourceId", source_id},
        {"reconnection", reconnectionText(reconnection)}, {"required", required},
        {"expectedChannels", expected_channels},
        {"expectedNominalRate", expected_nominal_rate},
        {"expectedCoordinateFrame", expected_coordinate_frame},
    };
}

StreamBinding StreamBinding::fromJson(const QJsonObject& object) {
    StreamBinding binding;
    binding.role = readString(object, "role");
    binding.name = readString(object, "name");
    binding.source_id = readString(object, "sourceId");
    binding.reconnection = parseReconnection(readString(object, "reconnection"));
    binding.required = readBool(object, "required", binding.required);
    binding.expected_channels = readInt(object, "expectedChannels", 0);
    binding.expected_nominal_rate = readDouble(object, "expectedNominalRate", 0.0);
    binding.expected_coordinate_frame = readString(object, "expectedCoordinateFrame");
    return binding;
}

SessionConfiguration::SessionConfiguration() {
    preview_markers = defaultBinding("markers", stream_defaults::ViconMarkers, true);
    preview_segments = defaultBinding("segments", stream_defaults::ViconSegments, false);
    preview_gaze = defaultBinding("gaze", stream_defaults::HoloLensGaze, false, 21, 90.0,
                                  "hololens_stationary_shared_with_gaze");
    preview_calibration = defaultBinding(
        "calibration", stream_defaults::HoloLensModelTargetPose, false, 8, 0.0,
        "hololens_stationary_shared_with_gaze");
    recording_streams = {
        preview_markers,
        preview_segments,
        preview_gaze,
        preview_calibration,
    };
    recording_root = QDir::homePath();
}

void SessionConfiguration::bindPreviewOutputs() {
    if (!preview_external_streams) {
        preview_markers.name = marker_output_name;
        preview_segments.name = segment_output_name;
        preview_markers.source_id.clear();
        preview_segments.source_id.clear();
    }
}

QJsonObject SessionConfiguration::toJson() const {
    QJsonArray selected_streams;
    for (const StreamBinding& stream : recording_streams) selected_streams.push_back(stream.toJson());
    return {
        {"version", CurrentVersion},
        {"vicon", QJsonObject{{"endpoint", vicon_endpoint},
                               {"markerOutput", marker_output_name},
                               {"segmentOutput", segment_output_name}}},
        {"preview", QJsonObject{
            {"externalStreams", preview_external_streams},
            {"markers", preview_markers.toJson()}, {"segments", preview_segments.toJson()},
            {"gaze", preview_gaze.toJson()}, {"calibration", preview_calibration.toJson()},
            {"matchTolerance", preview_match_tolerance}, {"renderHz", preview_render_hz},
            {"cacheMegabytes", preview_cache_megabytes},
            {"trailPoints", preview_trail_points},
            {"playbackSpeed", preview_playback_speed},
            {"loopPlayback", preview_loop_playback},
            {"manualGazeTranslation", vec3ToJson(preview_gaze_translation)},
            {"manualGazeRotationDegrees", vec3ToJson(preview_gaze_rotation_degrees)},
        }},
        {"recorder", QJsonObject{
            {"host", recorder_host}, {"port", recorder_port},
            {"executable", recorder_executable}, {"automaticLaunch", recorder_automatic_launch},
            {"recordEveryVisible", record_every_visible_stream}, {"streams", selected_streams},
        }},
        {"recording", QJsonObject{
            {"root", recording_root}, {"template", recording_template},
            {"participant", participant}, {"session", session}, {"task", task},
            {"run", run}, {"acquisition", acquisition}, {"modality", modality},
            {"storageWarningGiB", storage_warning_gib},
            {"automaticRunIncrement", automatic_run_increment},
            {"incrementAfterVerifiedOnly", increment_run_after_verified_only},
            {"allowOverwrite", allow_overwrite},
            {"allowOutsideStudyRoot", allow_outside_study_root},
        }},
        {"calibration", QJsonObject{
            {"stairModel", stair_model_path}, {"profileId", calibration_profile_id},
            {"required", calibration_required},
        }},
        {"workflow", QJsonObject{{"recorderOnly", recorder_only_mode}}},
    };
}

SessionConfiguration SessionConfiguration::fromJson(const QJsonObject& object, QString* error) {
    SessionConfiguration result;
    const int version = readInt(object, "version", 0);
    if (version != CurrentVersion) {
        if (error) *error = "Unsupported session configuration version " + QString::number(version);
        return result;
    }
    const QJsonObject vicon = object.value("vicon").toObject();
    result.vicon_endpoint = readString(vicon, "endpoint", result.vicon_endpoint);
    result.marker_output_name = readString(vicon, "markerOutput", result.marker_output_name);
    result.segment_output_name = readString(vicon, "segmentOutput", result.segment_output_name);

    const QJsonObject preview = object.value("preview").toObject();
    result.preview_external_streams = readBool(
        preview, "externalStreams", result.preview_external_streams);
    if (preview.value("markers").isObject()) result.preview_markers = StreamBinding::fromJson(preview.value("markers").toObject());
    if (preview.value("segments").isObject()) result.preview_segments = StreamBinding::fromJson(preview.value("segments").toObject());
    if (preview.value("gaze").isObject()) result.preview_gaze = StreamBinding::fromJson(preview.value("gaze").toObject());
    if (preview.value("calibration").isObject()) result.preview_calibration = StreamBinding::fromJson(preview.value("calibration").toObject());
    result.preview_match_tolerance = readDouble(preview, "matchTolerance", result.preview_match_tolerance);
    result.preview_render_hz = std::clamp(readInt(preview, "renderHz", result.preview_render_hz), 1, 60);
    result.preview_cache_megabytes = std::clamp(readInt(preview, "cacheMegabytes", result.preview_cache_megabytes), 16, 2048);
    result.preview_trail_points = std::clamp(
        readInt(preview, "trailPoints", result.preview_trail_points), 2, 500);
    result.preview_playback_speed = std::clamp(
        readDouble(preview, "playbackSpeed", result.preview_playback_speed),
        0.1, 4.0);
    result.preview_loop_playback = readBool(
        preview, "loopPlayback", result.preview_loop_playback);
    if (preview.value("manualGazeTranslation").isObject()) {
        result.preview_gaze_translation = readVec3(
            preview.value("manualGazeTranslation").toObject());
    }
    if (preview.value("manualGazeRotationDegrees").isObject()) {
        result.preview_gaze_rotation_degrees = readVec3(
            preview.value("manualGazeRotationDegrees").toObject());
    }

    const QJsonObject recorder = object.value("recorder").toObject();
    result.recorder_host = readString(recorder, "host", result.recorder_host);
    result.recorder_port = std::clamp(readInt(recorder, "port", result.recorder_port), 1, 65535);
    result.recorder_executable = readString(recorder, "executable");
    result.recorder_automatic_launch = readBool(
        recorder, "automaticLaunch", result.recorder_automatic_launch);
    result.record_every_visible_stream = readBool(
        recorder, "recordEveryVisible", result.record_every_visible_stream);
    if (recorder.value("streams").isArray()) {
        result.recording_streams.clear();
        for (const QJsonValue& value : recorder.value("streams").toArray()) {
            if (value.isObject()) result.recording_streams.push_back(StreamBinding::fromJson(value.toObject()));
        }
    }

    const QJsonObject recording = object.value("recording").toObject();
    result.recording_root = readString(recording, "root", result.recording_root);
    result.recording_template = readString(recording, "template", result.recording_template);
    result.participant = readString(recording, "participant", result.participant);
    result.session = readString(recording, "session", result.session);
    result.task = readString(recording, "task", result.task);
    result.run = std::clamp(readInt(recording, "run", result.run), 1, 999999);
    result.acquisition = readString(recording, "acquisition", result.acquisition);
    result.modality = readString(recording, "modality", result.modality);
    result.storage_warning_gib = (std::max)(0.0, readDouble(recording, "storageWarningGiB", result.storage_warning_gib));
    result.automatic_run_increment = readBool(
        recording, "automaticRunIncrement", result.automatic_run_increment);
    result.increment_run_after_verified_only =
        readBool(recording, "incrementAfterVerifiedOnly",
                 result.increment_run_after_verified_only);
    result.allow_overwrite = readBool(
        recording, "allowOverwrite", result.allow_overwrite);
    result.allow_outside_study_root = readBool(
        recording, "allowOutsideStudyRoot", result.allow_outside_study_root);

    const QJsonObject calibration = object.value("calibration").toObject();
    result.stair_model_path = readString(calibration, "stairModel");
    result.calibration_profile_id = readString(calibration, "profileId");
    result.calibration_required = readBool(
        calibration, "required", result.calibration_required);
    result.recorder_only_mode = readBool(
        object.value("workflow").toObject(), "recorderOnly",
        result.recorder_only_mode);
    result.version = CurrentVersion;
    result.bindPreviewOutputs();
    if (error) error->clear();
    return result;
}

SessionConfiguration SessionConfigurationStore::load(QSettings& settings) {
    const QByteArray stored = settings.value("session/configuration").toByteArray();
    if (!stored.isEmpty()) {
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(stored, &parse_error);
        QString error;
        if (parse_error.error == QJsonParseError::NoError && document.isObject()) {
            SessionConfiguration result =
                SessionConfiguration::fromJson(document.object(), &error);
            if (error.isEmpty()) return result;
        }
    }
    return {};
}

void SessionConfigurationStore::save(QSettings& settings,
                                     const SessionConfiguration& configuration) {
    SessionConfiguration normalized = configuration;
    normalized.version = SessionConfiguration::CurrentVersion;
    normalized.bindPreviewOutputs();
    settings.setValue("session/configuration", QJsonDocument(normalized.toJson()).toJson(QJsonDocument::Compact));
    settings.setValue("session/configurationVersion", SessionConfiguration::CurrentVersion);
}

SessionUiState SessionConfigurationStore::loadUiState(QSettings& settings) {
    SessionUiState result;
    result.geometry = settings.value("ui/windowGeometry").toByteArray();
    result.splitter_state = settings.value("ui/mainSplitter").toByteArray();
    result.active_control_tab = settings.value("ui/controlTab", 0).toInt();
    result.recent_recordings = settings.value("ui/recentRecordings").toStringList();
    result.recent_preset_directory = settings.value("ui/recentPresetDirectory").toString();
    result.recent_diagnostic_directory = settings.value("ui/recentDiagnosticDirectory").toString();
    return result;
}

void SessionConfigurationStore::saveUiState(QSettings& settings, const SessionUiState& state) {
    settings.setValue("ui/windowGeometry", state.geometry);
    settings.setValue("ui/mainSplitter", state.splitter_state);
    settings.setValue("ui/controlTab", state.active_control_tab);
    settings.setValue("ui/recentRecordings", state.recent_recordings.mid(0, 10));
    settings.setValue("ui/recentPresetDirectory", state.recent_preset_directory);
    settings.setValue("ui/recentDiagnosticDirectory", state.recent_diagnostic_directory);
}

QStringList SessionConfigurationStore::presetNames(QSettings& settings) {
    settings.beginGroup("sessionPresets");
    QStringList result = settings.childKeys();
    settings.endGroup();
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool SessionConfigurationStore::savePreset(QSettings& settings,
                                           const QString& name,
                                           const SessionConfiguration& configuration,
                                           QString* error) {
    const QString normalized_name = name.trimmed();
    if (normalized_name.isEmpty() || normalized_name.contains('/')) {
        if (error) *error = "Preset names must be non-empty and cannot contain '/'.";
        return false;
    }
    settings.setValue("sessionPresets/" + normalized_name, jsonString(configuration.toJson()));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = "The preset settings store could not be written.";
        return false;
    }
    if (error) error->clear();
    return true;
}

bool SessionConfigurationStore::loadPreset(QSettings& settings,
                                           const QString& name,
                                           SessionConfiguration& configuration,
                                           QString* error) {
    const QByteArray data = settings.value("sessionPresets/" + name.trimmed()).toByteArray();
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = "Preset is missing or is not valid JSON.";
        return false;
    }
    QString configuration_error;
    const SessionConfiguration loaded = SessionConfiguration::fromJson(document.object(), &configuration_error);
    if (!configuration_error.isEmpty()) {
        if (error) *error = configuration_error;
        return false;
    }
    configuration = loaded;
    if (error) error->clear();
    return true;
}

bool SessionConfigurationStore::exportConfiguration(
    const QString& path,
    const SessionConfiguration& configuration,
    QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(configuration.toJson())
                                 .toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        if (error) *error = file.errorString();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool SessionConfigurationStore::importConfiguration(
    const QString& path,
    SessionConfiguration& configuration,
    QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = "Invalid session configuration JSON: " + parse_error.errorString();
        return false;
    }
    QString configuration_error;
    const SessionConfiguration loaded =
        SessionConfiguration::fromJson(document.object(), &configuration_error);
    if (!configuration_error.isEmpty()) {
        if (error) *error = configuration_error;
        return false;
    }
    configuration = loaded;
    if (error) error->clear();
    return true;
}

} // namespace vicon_lsl::gui
