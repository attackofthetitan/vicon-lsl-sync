#pragma once

#include "preview/PreviewTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

class QSettings;

namespace vicon_lsl::gui {

enum class StreamReconnectionMode {
    SourceIdentity,
    FollowName,
};

struct StreamIdentity {
    QString role;
    QString name;
    QString type;
    QString source_id;
    QString hostname;
    QString session_id;
    QString uid;
    double publisher_created_at = 0.0;
    int channel_count = 0;
    double nominal_rate = 0.0;
    double effective_rate = 0.0;
    QString coordinate_frame;
    bool metadata_complete = false;
    bool schema_compatible = true;
    bool present = true;
    bool selected = true;
    bool required = false;
    qint64 freshness_ms = -1;
    QDateTime discovered_at;
    QString warning;

    QString stableKey() const;
    QString displayText() const;
    QJsonObject toJson() const;
    static StreamIdentity fromJson(const QJsonObject& object);
};

struct StreamBinding {
    QString role;
    QString name;
    QString source_id;
    StreamReconnectionMode reconnection = StreamReconnectionMode::SourceIdentity;
    bool required = false;
    int expected_channels = 0;
    double expected_nominal_rate = 0.0;
    QString expected_coordinate_frame;

    bool matches(const StreamIdentity& identity) const;
    QJsonObject toJson() const;
    static StreamBinding fromJson(const QJsonObject& object);
};

struct StreamIdentitySelection {
    int index = -1;
    bool ambiguous = false;
    bool used_name_fallback = false;
    bool should_warn = false;
    QString explanation;
};

StreamIdentitySelection selectStreamIdentity(
    const QVector<StreamIdentity>& candidates,
    const StreamBinding& binding);

struct SessionConfiguration {
    static constexpr int CurrentVersion = 1;

    int version = CurrentVersion;
    QString vicon_endpoint = "localhost:801";
    QString marker_output_name = "ViconMarkers";
    QString segment_output_name = "ViconSegments";

    bool preview_external_streams = false;
    StreamBinding preview_markers;
    StreamBinding preview_segments;
    StreamBinding preview_gaze;
    StreamBinding preview_calibration;
    double preview_match_tolerance = 0.05;
    int preview_render_hz = 30;
    int preview_cache_megabytes = 128;
    int preview_trail_points = 24;
    double preview_playback_speed = 1.0;
    bool preview_loop_playback = false;

    QString recorder_host = "localhost";
    int recorder_port = 22345;
    QString recorder_executable;
    bool recorder_automatic_launch = true;
    bool record_every_visible_stream = false;
    QVector<StreamBinding> recording_streams;

    QString recording_root;
    QString recording_template =
        "sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf";
    QString participant = "P001";
    QString session = "S001";
    QString task = "Task";
    int run = 1;
    QString acquisition = "vicon";
    QString modality = "beh";
    double storage_warning_gib = 10.0;
    bool automatic_run_increment = false;
    bool allow_overwrite = false;
    bool allow_outside_study_root = false;

    QString stair_model_path;
    QString calibration_profile_id;
    bool calibration_required = false;
    bool recorder_only_mode = false;

    SessionConfiguration();
    void bindPreviewOutputs();
    QJsonObject toJson() const;
    static SessionConfiguration fromJson(const QJsonObject& object, QString* error = nullptr);
};

struct SessionUiState {
    QByteArray geometry;
    QByteArray splitter_state;
    int active_control_tab = 0;
    QStringList recent_recordings;
    QString recent_preset_directory;
    QString recent_diagnostic_directory;
};

class SessionConfigurationStore {
public:
    static SessionConfiguration load(QSettings& settings);
    static void save(QSettings& settings, const SessionConfiguration& configuration);
    static SessionUiState loadUiState(QSettings& settings);
    static void saveUiState(QSettings& settings, const SessionUiState& state);
    static QStringList presetNames(QSettings& settings);
    static bool savePreset(QSettings& settings,
                           const QString& name,
                           const SessionConfiguration& configuration,
                           QString* error = nullptr);
    static bool loadPreset(QSettings& settings,
                           const QString& name,
                           SessionConfiguration& configuration,
                           QString* error = nullptr);
    static bool exportConfiguration(const QString& path,
                                    const SessionConfiguration& configuration,
                                    QString* error = nullptr);
    static bool importConfiguration(const QString& path,
                                    SessionConfiguration& configuration,
                                    QString* error = nullptr);
};

} // namespace vicon_lsl::gui

Q_DECLARE_METATYPE(vicon_lsl::gui::StreamIdentity)
Q_DECLARE_METATYPE(QVector<vicon_lsl::gui::StreamIdentity>)
