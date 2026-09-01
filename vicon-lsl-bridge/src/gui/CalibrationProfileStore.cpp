#include "gui/CalibrationProfileStore.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace vicon_lsl::gui {
namespace {

QJsonObject vec3ToJson(const PreviewVec3& v) { return {{"x", v.x}, {"y", v.y}, {"z", v.z}}; }
PreviewVec3 vec3FromJson(const QJsonObject& o) { return {o.value("x").toDouble(), o.value("y").toDouble(), o.value("z").toDouble()}; }

QJsonObject quatToJson(const PreviewQuaternion& q) { return {{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}}; }
PreviewQuaternion quatFromJson(const QJsonObject& o) { return {o.value("x").toDouble(), o.value("y").toDouble(), o.value("z").toDouble(), o.value("w").toDouble(1.0)}; }

QJsonObject rigidToJson(const PreviewRigidTransform& r) {
    return {{"translation", vec3ToJson(r.translation)}, {"rotation", quatToJson(r.rotation)}};
}
PreviewRigidTransform rigidFromJson(const QJsonObject& o) {
    return {vec3FromJson(o.value("translation").toObject()), quatFromJson(o.value("rotation").toObject())};
}

QJsonObject transformToJson(const PreviewTransformProfile& p) {
    return {
        {"name", QString::fromStdString(p.name)}, {"scale", p.scale},
        {"translation", vec3ToJson(p.translation)}, {"rotationDegrees", vec3ToJson(p.rotation_degrees)},
        {"quaternion", quatToJson(p.rotation)}, {"useQuaternion", p.use_quaternion_rotation},
        {"inputAxisSign", vec3ToJson(p.input_axis_sign)},
    };
}
PreviewTransformProfile transformFromJson(const QJsonObject& o) {
    PreviewTransformProfile p;
    p.name = o.value("name").toString().toStdString();
    p.scale = o.value("scale").toDouble(1.0);
    p.translation = vec3FromJson(o.value("translation").toObject());
    p.rotation_degrees = vec3FromJson(o.value("rotationDegrees").toObject());
    p.rotation = quatFromJson(o.value("quaternion").toObject());
    p.use_quaternion_rotation = o.value("useQuaternion").toBool(false);
    const QJsonObject signs = o.value("inputAxisSign").toObject();
    p.input_axis_sign = signs.isEmpty() ? PreviewVec3{1.0, 1.0, 1.0} : vec3FromJson(signs);
    return p;
}

bool finiteRigid(const PreviewRigidTransform& r) {
    const auto& p = r.translation;
    const auto& q = r.rotation;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
           std::isfinite(q.w) && (q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w) > 1e-12;
}

QString normalizedId(QString v) {
    v = v.trimmed().toLower();
    v.replace(QRegularExpression("[^a-z0-9_-]+"), "-");
    v.replace(QRegularExpression("-+"), "-");
    v.remove(QRegularExpression("^-|-$"));
    return v.isEmpty() ? QStringLiteral("calibration") : v;
}

} // namespace

bool ManagedCalibrationProfile::complete(QString* reason) const {
    const auto fail = [reason](const QString& text) { if (reason) *reason = text; return false; };
    if (id.trimmed().isEmpty()) return fail("Saved calibration ID is required");
    if (display_name.trimmed().isEmpty()) return fail("Calibration name is required");
    if (physical_setup_id.trimmed().isEmpty()) return fail("Setup name is required");
    if (stair_model_identity.trimmed().isEmpty()) return fail("Stair model file is required");
    if (gaze_coordinate_frame.trimmed().isEmpty() || target_coordinate_frame.trimmed().isEmpty()) {
        return fail("Gaze and target coordinate names are required");
    }
    if (!finiteRigid(vicon_from_target)) return fail("Measured Vicon stair pose is invalid");
    if (!created_at.isValid()) return fail("Creation time is invalid");
    if (reason) reason->clear();
    return true;
}

CalibrationProfile ManagedCalibrationProfile::solverProfile() const {
    CalibrationProfile res = defaultStairCalibrationProfile();
    res.id = id.toStdString();
    res.vicon_from_target = vicon_from_target;
    return res;
}

QJsonObject ManagedCalibrationProfile::toJson() const {
    return {
        {"version", version}, {"id", id}, {"displayName", display_name},
        {"physicalSetupId", physical_setup_id}, {"stairModelPath", stair_model_path},
        {"stairModelIdentity", stair_model_identity},
        {"viconFromTarget", rigidToJson(vicon_from_target)},
        {"gazeTransform", transformToJson(gaze_transform)},
        {"gazeCoordinateFrame", gaze_coordinate_frame},
        {"targetCoordinateFrame", target_coordinate_frame},
        {"setupNotes", setup_notes}, {"createdAt", created_at.toString(Qt::ISODateWithMs)},
        {"quality", QJsonObject{
            {"sampleCount", static_cast<qint64>(quality.sample_count)},
            {"translationRmsM", quality.translation_rms_m},
            {"rotationRmsDegrees", quality.rotation_rms_degrees},
        }},
        {"metadataFallbackConfirmed", metadata_fallback_confirmed},
        {"retired", retired},
    };
}

ManagedCalibrationProfile ManagedCalibrationProfile::fromJson(const QJsonObject& o, QString* error) {
    if (o.value("version").toInt(0) != CurrentVersion) {
        if (error) *error = "This saved calibration file version is not supported";
        return {};
    }
    ManagedCalibrationProfile res;
    res.version = CurrentVersion;
    res.id = o.value("id").toString();
    res.display_name = o.value("displayName").toString();
    res.physical_setup_id = o.value("physicalSetupId").toString();
    res.stair_model_path = o.value("stairModelPath").toString();
    res.stair_model_identity = o.value("stairModelIdentity").toString();
    res.vicon_from_target = rigidFromJson(o.value("viconFromTarget").toObject());
    res.gaze_transform = transformFromJson(o.value("gazeTransform").toObject());
    res.gaze_coordinate_frame = o.value("gazeCoordinateFrame").toString();
    res.target_coordinate_frame = o.value("targetCoordinateFrame").toString();
    res.setup_notes = o.value("setupNotes").toString();
    res.created_at = QDateTime::fromString(o.value("createdAt").toString(), Qt::ISODateWithMs);
    const auto q = o.value("quality").toObject();
    res.quality.sample_count = static_cast<std::size_t>((std::max)(qint64{0}, q.value("sampleCount").toInteger()));
    res.quality.translation_rms_m = q.value("translationRmsM").toDouble();
    res.quality.rotation_rms_degrees = q.value("rotationRmsDegrees").toDouble();
    res.metadata_fallback_confirmed = o.value("metadataFallbackConfirmed").toBool(false);
    res.retired = o.value("retired").toBool(false);
    QString reason;
    if (!res.complete(&reason)) {
        if (error) *error = reason;
        return {};
    }
    if (error) error->clear();
    return res;
}

QVector<ManagedCalibrationProfile> CalibrationProfileStore::load(QSettings& settings) {
    QVector<ManagedCalibrationProfile> res;
    const auto bytes = settings.value("session/calibrationProfiles").toByteArray();
    if (!bytes.isEmpty()) {
        QJsonParseError parse_error;
        const auto doc = QJsonDocument::fromJson(bytes, &parse_error);
        if (parse_error.error == QJsonParseError::NoError && doc.isArray()) {
            for (const auto& val : doc.array()) {
                QString err;
                auto p = ManagedCalibrationProfile::fromJson(val.toObject(), &err);
                if (err.isEmpty()) res.push_back(std::move(p));
            }
        }
    }
    if (res.isEmpty()) res.push_back(defaultProfile());
    return res;
}

bool CalibrationProfileStore::save(QSettings& settings, const QVector<ManagedCalibrationProfile>& profiles, QString* error) {
    QJsonArray arr;
    for (const auto& p : profiles) {
        QString reason;
        if (!p.complete(&reason)) {
            if (error) *error = p.display_name + ": " + reason;
            return false;
        }
        arr.push_back(p.toJson());
    }
    settings.setValue("session/calibrationProfiles", QJsonDocument(arr).toJson(QJsonDocument::Compact));
    if (settings.status() != QSettings::NoError) {
        if (error) *error = "Could not save calibrations";
        return false;
    }
    if (error) error->clear();
    return true;
}

ManagedCalibrationProfile CalibrationProfileStore::defaultProfile() {
    const auto& defaults = defaultStairCalibrationProfile();
    ManagedCalibrationProfile res;
    res.id = QString::fromStdString(defaults.id);
    res.display_name = "Default stair setup";
    res.physical_setup_id = "stair-setup-default";
    res.stair_model_identity = "stair-model-v1";
    res.vicon_from_target = defaults.vicon_from_target;
    res.gaze_transform.name = "HoloLens";
    res.gaze_coordinate_frame = "hololens_stationary_shared_with_gaze";
    res.target_coordinate_frame = "hololens_stationary_shared_with_gaze";
    res.setup_notes = "Built-in setup; confirm the measured stair pose before reuse.";
    res.created_at = QDateTime::currentDateTimeUtc();
    res.metadata_fallback_confirmed = false;
    return res;
}

QString CalibrationProfileStore::newProfileId(const QString& display_name,
                                             const QVector<ManagedCalibrationProfile>& existing) {
    const QString base = normalizedId(display_name);
    QString candidate = base;
    int suffix = 2;
    const auto used = [&existing](const QString& id) {
        return std::any_of(existing.begin(), existing.end(), [&id](const auto& p) {
            return p.id.compare(id, Qt::CaseInsensitive) == 0;
        });
    };
    while (used(candidate)) candidate = base + "-" + QString::number(suffix++);
    return candidate;
}

ManagedCalibrationProfile CalibrationProfileStore::duplicate(const ManagedCalibrationProfile& source,
                                                             const QVector<ManagedCalibrationProfile>& existing) {
    ManagedCalibrationProfile res = source;
    res.display_name = source.display_name + " copy";
    res.id = newProfileId(res.display_name, existing);
    res.created_at = QDateTime::currentDateTimeUtc();
    res.retired = false;
    return res;
}

bool CalibrationProfileStore::retire(QVector<ManagedCalibrationProfile>& profiles, const QString& id) {
    for (auto& p : profiles) {
        if (p.id == id) {
            p.retired = true;
            return true;
        }
    }
    return false;
}

bool CalibrationProfileStore::exportProfile(const QString& path, const ManagedCalibrationProfile& profile, QString* error) {
    QString reason;
    if (!profile.complete(&reason)) {
        if (error) *error = reason;
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(QJsonDocument(profile.toJson()).toJson(QJsonDocument::Indented)) < 0) {
        if (error) *error = file.errorString();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool CalibrationProfileStore::importProfile(const QString& path, ManagedCalibrationProfile& profile, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parse_error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = "Could not read the calibration file: " + parse_error.errorString();
        return false;
    }
    profile = ManagedCalibrationProfile::fromJson(doc.object(), error);
    return error ? error->isEmpty() : !profile.id.isEmpty();
}

QString CalibrationProfileStore::stairModelIdentity(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace vicon_lsl::gui
