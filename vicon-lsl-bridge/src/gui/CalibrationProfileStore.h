#pragma once

#include "preview/PreviewCalibration.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

class QSettings;

namespace vicon_lsl::gui {

struct ManagedCalibrationProfile {
    static constexpr int CurrentVersion = 1;

    int version = CurrentVersion;
    QString id;
    QString display_name;
    QString physical_setup_id;
    QString stair_model_path;
    QString stair_model_identity;
    PreviewRigidTransform vicon_from_target;
    PreviewTransformProfile gaze_transform;
    QString gaze_coordinate_frame;
    QString target_coordinate_frame;
    QString setup_notes;
    QDateTime created_at;
    CalibrationQuality quality;
    bool metadata_fallback_confirmed = false;
    bool retired = false;

    bool complete(QString* reason = nullptr) const;
    CalibrationProfile solverProfile() const;
    QJsonObject toJson() const;
    static ManagedCalibrationProfile fromJson(const QJsonObject& object,
                                              QString* error = nullptr);
};

class CalibrationProfileStore {
public:
    static QVector<ManagedCalibrationProfile> load(QSettings& settings);
    static bool save(QSettings& settings,
                     const QVector<ManagedCalibrationProfile>& profiles,
                     QString* error = nullptr);
    static ManagedCalibrationProfile defaultProfile();
    static QString newProfileId(const QString& display_name,
                                const QVector<ManagedCalibrationProfile>& existing);
    static ManagedCalibrationProfile duplicate(
        const ManagedCalibrationProfile& source,
        const QVector<ManagedCalibrationProfile>& existing);
    static bool retire(QVector<ManagedCalibrationProfile>& profiles,
                       const QString& id);
    static bool exportProfile(const QString& path,
                              const ManagedCalibrationProfile& profile,
                              QString* error = nullptr);
    static bool importProfile(const QString& path,
                              ManagedCalibrationProfile& profile,
                              QString* error = nullptr);
    static QString stairModelIdentity(const QString& path);
};

} // namespace vicon_lsl::gui
