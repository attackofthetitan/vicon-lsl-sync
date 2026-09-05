#include "gui/SessionConfiguration.h"

#include "StreamDefaults.h"
#include "HoloLensGazeSchema.h"
#include "HoloLensModelTargetSchema.h"

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

QString jsonString(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

double readFiniteDouble(const QJsonObject& object, const char* key, double default_value = 0.0) {
    const double value = object.value(QLatin1String(key)).toDouble(default_value);
    return std::isfinite(value) ? value : default_value;
}

bool parseConfiguration(const QByteArray& bytes, const QString& bad_json,
                        SessionConfiguration& config, QString* error) {
    QJsonParseError parse_error;
    const auto doc = QJsonDocument::fromJson(bytes, &parse_error);
    QString message;
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        message = bad_json + " " + parse_error.errorString();
    } else {
        const auto loaded = SessionConfiguration::fromJson(doc.object(), &message);
        if (message.isEmpty()) config = loaded;
    }
    if (error) *error = message;
    return message.isEmpty();
}

bool readFile(const QString& path, QByteArray& bytes, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    bytes = file.readAll();
    return true;
}

StreamBinding makeBinding(const QString& role, const QString& name, bool req,
                          int ch = 0, double rate = 0.0, const QString& frame = {}) {
    StreamBinding b;
    b.role = role;
    b.name = name;
    b.required = req;
    b.expected_channels = ch;
    b.expected_nominal_rate = rate;
    b.expected_coordinate_frame = frame;
    return b;
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

StreamIdentity StreamIdentity::fromJson(const QJsonObject& o) {
    StreamIdentity id;
    id.role = o.value("role").toString();
    id.name = o.value("name").toString();
    id.type = o.value("type").toString();
    id.source_id = o.value("sourceId").toString();
    id.hostname = o.value("hostname").toString();
    id.session_id = o.value("sessionId").toString();
    id.uid = o.value("uid").toString();
    id.publisher_created_at = readFiniteDouble(o, "publisherCreatedAt");
    id.channel_count = o.value("channelCount").toInt();
    id.nominal_rate = readFiniteDouble(o, "nominalRate");
    id.effective_rate = readFiniteDouble(o, "effectiveRate");
    id.coordinate_frame = o.value("coordinateFrame").toString();
    id.metadata_complete = o.value("metadataComplete").toBool(id.metadata_complete);
    id.schema_compatible = o.value("schemaCompatible").toBool(id.schema_compatible);
    id.present = o.value("present").toBool(id.present);
    id.selected = o.value("selected").toBool(id.selected);
    id.required = o.value("required").toBool(id.required);
    id.freshness_ms = static_cast<qint64>(readFiniteDouble(o, "freshnessMs", -1));
    id.discovered_at = QDateTime::fromString(o.value("discoveredAt").toString(), Qt::ISODateWithMs);
    id.warning = o.value("warning").toString();
    return id;
}

StreamIdentitySelection selectStreamIdentity(
    const QVector<StreamIdentity>& candidates,
    const StreamBinding& binding) {
    QVector<int> matches;
    for (int i = 0; i < candidates.size(); ++i) {
        if (candidates[i].name == binding.name) matches.push_back(i);
    }
    std::stable_sort(matches.begin(), matches.end(), [&candidates](int l, int r) {
        const auto& a = candidates[l];
        const auto& b = candidates[r];
        return std::tie(a.source_id, a.hostname, a.session_id, a.uid) <
               std::tie(b.source_id, b.hostname, b.session_id, b.uid);
    });
    if (matches.isEmpty()) {
        return {-1, false, false, true, "No visible stream matches " + binding.name};
    }

    const QString req_src = binding.source_id.trimmed();
    if (!req_src.isEmpty()) {
        QVector<int> exact;
        for (int idx : matches) {
            if (candidates[idx].source_id == req_src) exact.push_back(idx);
        }
        if (!exact.isEmpty()) {
            std::stable_sort(exact.begin(), exact.end(), [&candidates](int l, int r) {
                const auto& a = candidates[l];
                const auto& b = candidates[r];
                if (a.publisher_created_at != b.publisher_created_at) {
                    return a.publisher_created_at > b.publisher_created_at;
                }
                return std::tie(a.hostname, a.session_id, a.uid) <
                       std::tie(b.hostname, b.session_id, b.uid);
            });
            const bool duplicated_source = exact.size() > 1;
            return {exact.front(), false, false, duplicated_source,
                    duplicated_source
                        ? "Selected the newest visible instance of configured source ID " +
                              req_src + " from " + QString::number(exact.size()) +
                              " matching sources"
                        : "Selected configured source ID " + req_src};
        }
        if (binding.reconnection == StreamReconnectionMode::SourceIdentity) {
            return {-1, false, false, true,
                    "Selected source ID " + req_src +
                        " is unavailable; the app did not switch to another stream with the same name"};
        }
    }

    if (matches.size() > 1 && binding.reconnection != StreamReconnectionMode::FollowName) {
        return {-1, true, false, true,
                "Multiple streams named " + binding.name +
                    " are available; select a source ID or enable Follow by name"};
    }

    const bool fallback = !req_src.isEmpty() || matches.size() > 1;
    return {matches.front(), false, fallback, fallback,
            fallback ? "Follow by name selected the first matching source"
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
        {"reconnection", reconnection == StreamReconnectionMode::FollowName ? "follow-name" : "source-id"},
        {"required", required}, {"expectedChannels", expected_channels},
        {"expectedNominalRate", expected_nominal_rate},
        {"expectedCoordinateFrame", expected_coordinate_frame},
    };
}

StreamBinding StreamBinding::fromJson(const QJsonObject& o) {
    StreamBinding b;
    b.role = o.value("role").toString();
    b.name = o.value("name").toString();
    b.source_id = o.value("sourceId").toString();
    b.reconnection = o.value("reconnection").toString().compare("follow-name", Qt::CaseInsensitive) == 0
        ? StreamReconnectionMode::FollowName : StreamReconnectionMode::SourceIdentity;
    b.required = o.value("required").toBool(b.required);
    b.expected_channels = o.value("expectedChannels").toInt();
    b.expected_nominal_rate = readFiniteDouble(o, "expectedNominalRate");
    b.expected_coordinate_frame = o.value("expectedCoordinateFrame").toString();
    return b;
}

SessionConfiguration::SessionConfiguration() {
    preview_markers = makeBinding("markers", stream_defaults::ViconMarkers, true);
    preview_segments = makeBinding("segments", stream_defaults::ViconSegments, false);
    preview_gaze = makeBinding("gaze", stream_defaults::HoloLensGaze, false,
                               static_cast<int>(kHoloLensGazeChannelCount), kHoloLensGazeNominalSrate,
                               "hololens_stationary_shared_with_gaze");
    preview_calibration = makeBinding("calibration", stream_defaults::HoloLensModelTargetPose, false,
                                      static_cast<int>(kHoloLensModelTargetChannelCount), kHoloLensModelTargetNominalSrate,
                                      "hololens_stationary_shared_with_gaze");
    recording_streams = {preview_markers, preview_segments, preview_gaze, preview_calibration};
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
    QJsonArray streams;
    for (const auto& s : recording_streams) streams.push_back(s.toJson());
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
        }},
        {"recorder", QJsonObject{
            {"host", recorder_host}, {"port", recorder_port},
            {"executable", recorder_executable}, {"automaticLaunch", recorder_automatic_launch},
            {"recordEveryVisible", record_every_visible_stream}, {"streams", streams},
        }},
        {"recording", QJsonObject{
            {"root", recording_root}, {"template", recording_template},
            {"participant", participant}, {"session", session}, {"task", task},
            {"run", run}, {"acquisition", acquisition}, {"modality", modality},
            {"storageWarningGiB", storage_warning_gib},
            {"automaticRunIncrement", automatic_run_increment},
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

SessionConfiguration SessionConfiguration::fromJson(const QJsonObject& o, QString* error) {
    SessionConfiguration res;
    const int ver = o.value("version").toInt();
    if (ver != CurrentVersion) {
        if (error) *error = "Unsupported session configuration version " + QString::number(ver);
        return res;
    }
    const auto vicon = o.value("vicon").toObject();
    res.vicon_endpoint = vicon.value("endpoint").toString(res.vicon_endpoint);
    res.marker_output_name = vicon.value("markerOutput").toString(res.marker_output_name);
    res.segment_output_name = vicon.value("segmentOutput").toString(res.segment_output_name);

    const auto prev = o.value("preview").toObject();
    res.preview_external_streams = prev.value("externalStreams").toBool(res.preview_external_streams);
    if (prev.value("markers").isObject()) res.preview_markers = StreamBinding::fromJson(prev.value("markers").toObject());
    if (prev.value("segments").isObject()) res.preview_segments = StreamBinding::fromJson(prev.value("segments").toObject());
    if (prev.value("gaze").isObject()) res.preview_gaze = StreamBinding::fromJson(prev.value("gaze").toObject());
    if (prev.value("calibration").isObject()) res.preview_calibration = StreamBinding::fromJson(prev.value("calibration").toObject());
    res.preview_match_tolerance = readFiniteDouble(prev, "matchTolerance", res.preview_match_tolerance);
    res.preview_render_hz = std::clamp(prev.value("renderHz").toInt(res.preview_render_hz), 1, 60);
    res.preview_cache_megabytes = std::clamp(prev.value("cacheMegabytes").toInt(res.preview_cache_megabytes), 16, 2048);
    res.preview_trail_points = std::clamp(prev.value("trailPoints").toInt(res.preview_trail_points), 2, 500);
    res.preview_playback_speed = std::clamp(readFiniteDouble(prev, "playbackSpeed", res.preview_playback_speed), 0.1, 4.0);
    res.preview_loop_playback = prev.value("loopPlayback").toBool(res.preview_loop_playback);

    const auto rec = o.value("recorder").toObject();
    res.recorder_host = rec.value("host").toString(res.recorder_host);
    res.recorder_port = std::clamp(rec.value("port").toInt(res.recorder_port), 1, 65535);
    res.recorder_executable = rec.value("executable").toString();
    res.recorder_automatic_launch = rec.value("automaticLaunch").toBool(res.recorder_automatic_launch);
    res.record_every_visible_stream = rec.value("recordEveryVisible").toBool(res.record_every_visible_stream);
    if (rec.value("streams").isArray()) {
        res.recording_streams.clear();
        for (const auto& val : rec.value("streams").toArray()) {
            if (val.isObject()) res.recording_streams.push_back(StreamBinding::fromJson(val.toObject()));
        }
    }

    const auto recing = o.value("recording").toObject();
    res.recording_root = recing.value("root").toString(res.recording_root);
    res.recording_template = recing.value("template").toString(res.recording_template);
    res.participant = recing.value("participant").toString(res.participant);
    res.session = recing.value("session").toString(res.session);
    res.task = recing.value("task").toString(res.task);
    res.run = std::clamp(recing.value("run").toInt(res.run), 1, 999999);
    res.acquisition = recing.value("acquisition").toString(res.acquisition);
    res.modality = recing.value("modality").toString(res.modality);
    res.storage_warning_gib = (std::max)(0.0, readFiniteDouble(recing, "storageWarningGiB", res.storage_warning_gib));
    res.automatic_run_increment = recing.value("automaticRunIncrement").toBool(res.automatic_run_increment);
    res.allow_overwrite = recing.value("allowOverwrite").toBool(res.allow_overwrite);
    res.allow_outside_study_root = recing.value("allowOutsideStudyRoot").toBool(res.allow_outside_study_root);

    const auto cal = o.value("calibration").toObject();
    res.stair_model_path = cal.value("stairModel").toString();
    res.calibration_profile_id = cal.value("profileId").toString();
    res.calibration_required = cal.value("required").toBool(res.calibration_required);
    res.recorder_only_mode = o.value("workflow").toObject().value("recorderOnly").toBool(res.recorder_only_mode);
    res.version = CurrentVersion;
    res.bindPreviewOutputs();
    if (error) error->clear();
    return res;
}

SessionConfiguration SessionConfigurationStore::load(QSettings& settings) {
    SessionConfiguration config;
    parseConfiguration(settings.value("session/configuration").toByteArray(), {}, config, nullptr);
    return config;
}

void SessionConfigurationStore::save(QSettings& settings, const SessionConfiguration& configuration) {
    SessionConfiguration norm = configuration;
    norm.version = SessionConfiguration::CurrentVersion;
    norm.bindPreviewOutputs();
    settings.setValue("session/configuration", QJsonDocument(norm.toJson()).toJson(QJsonDocument::Compact));
    settings.setValue("session/configurationVersion", SessionConfiguration::CurrentVersion);
}

SessionUiState SessionConfigurationStore::loadUiState(QSettings& settings) {
    SessionUiState s;
    s.geometry = settings.value("ui/windowGeometry").toByteArray();
    s.splitter_state = settings.value("ui/mainSplitter").toByteArray();
    s.active_control_tab = settings.value("ui/controlTab", 0).toInt();
    s.recent_recordings = settings.value("ui/recentRecordings").toStringList();
    s.recent_preset_directory = settings.value("ui/recentPresetDirectory").toString();
    s.recent_diagnostic_directory = settings.value("ui/recentDiagnosticDirectory").toString();
    return s;
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
    QStringList res = settings.childKeys();
    settings.endGroup();
    res.sort(Qt::CaseInsensitive);
    return res;
}

bool SessionConfigurationStore::savePreset(QSettings& settings, const QString& name,
                                           const SessionConfiguration& config, QString* error) {
    const QString n = name.trimmed();
    if (n.isEmpty() || n.contains('/')) {
        if (error) *error = "Preset names must be non-empty and cannot contain '/'.";
        return false;
    }
    settings.setValue("sessionPresets/" + n, jsonString(config.toJson()));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = "The preset settings store could not be written.";
        return false;
    }
    if (error) error->clear();
    return true;
}

bool SessionConfigurationStore::loadPreset(QSettings& settings, const QString& name,
                                           SessionConfiguration& config, QString* error) {
    return parseConfiguration(settings.value("sessionPresets/" + name.trimmed()).toByteArray(),
                              "Preset is missing or is not valid JSON.", config, error);
}

bool SessionConfigurationStore::exportConfiguration(const QString& path,
                                                    const SessionConfiguration& config,
                                                    QString* error) {
    QFile file(path);
    const auto bytes = QJsonDocument(config.toJson()).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(bytes) != bytes.size() || !file.flush()) {
        if (error) *error = file.errorString();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool SessionConfigurationStore::importConfiguration(const QString& path,
                                                    SessionConfiguration& config,
                                                    QString* error) {
    QByteArray bytes;
    return readFile(path, bytes, error) &&
           parseConfiguration(bytes, "The file is not valid session configuration JSON.", config, error);
}

} // namespace vicon_lsl::gui
