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

QJsonObject vectorToJson(const PreviewVec3& value) {
    return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

PreviewVec3 vectorFromJson(const QJsonObject& object) {
    return {object.value("x").toDouble(), object.value("y").toDouble(),
            object.value("z").toDouble()};
}

QJsonObject quaternionToJson(const PreviewQuaternion& value) {
    return {{"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w}};
}

PreviewQuaternion quaternionFromJson(const QJsonObject& object) {
    return {object.value("x").toDouble(), object.value("y").toDouble(),
            object.value("z").toDouble(), object.value("w").toDouble(1.0)};
}

QJsonObject rigidToJson(const PreviewRigidTransform& value) {
    return {{"translation", vectorToJson(value.translation)},
            {"rotation", quaternionToJson(value.rotation)}};
}

PreviewRigidTransform rigidFromJson(const QJsonObject& object) {
    return {vectorFromJson(object.value("translation").toObject()),
            quaternionFromJson(object.value("rotation").toObject())};
}

QJsonObject transformToJson(const PreviewTransformProfile& value) {
    return {
        {"name", QString::fromStdString(value.name)},
        {"scale", value.scale},
        {"translation", vectorToJson(value.translation)},
        {"rotationDegrees", vectorToJson(value.rotation_degrees)},
        {"quaternion", quaternionToJson(value.rotation)},
        {"useQuaternion", value.use_quaternion_rotation},
        {"inputAxisSign", vectorToJson(value.input_axis_sign)},
    };
}

PreviewTransformProfile transformFromJson(const QJsonObject& object) {
    PreviewTransformProfile result;
    result.name = object.value("name").toString().toStdString();
    result.scale = object.value("scale").toDouble(1.0);
    result.translation = vectorFromJson(object.value("translation").toObject());
    result.rotation_degrees = vectorFromJson(object.value("rotationDegrees").toObject());
    result.rotation = quaternionFromJson(object.value("quaternion").toObject());
    result.use_quaternion_rotation = object.value("useQuaternion").toBool(false);
    const QJsonObject signs = object.value("inputAxisSign").toObject();
    result.input_axis_sign = signs.isEmpty() ? PreviewVec3{1.0, 1.0, 1.0}
                                             : vectorFromJson(signs);
    return result;
}

bool finiteRigid(const PreviewRigidTransform& value) {
    const PreviewVec3& p = value.translation;
    const PreviewQuaternion& q = value.rotation;
    const double norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
           std::isfinite(q.w) && norm > 1e-12;
}

QString normalizedId(QString value) {
    value = value.trimmed().toLower();
    value.replace(QRegularExpression("[^a-z0-9_-]+"), "-");
    value.replace(QRegularExpression("-+"), "-");
    value.remove(QRegularExpression("^-|-$"));
    return value.isEmpty() ? QStringLiteral("calibration") : value;
}

} // namespace

bool ManagedCalibrationProfile::complete(QString* reason) const {
    const auto fail = [reason](const QString& text) {
        if (reason) *reason = text;
        return false;
    };
    if (id.trimmed().isEmpty()) return fail("Saved calibration ID is required");
    if (display_name.trimmed().isEmpty()) return fail("Calibration name is required");
    if (physical_setup_id.trimmed().isEmpty()) return fail("Setup name is required");
    if (stair_model_identity.trimmed().isEmpty()) return fail("Stair model file is required");
    if (gaze_coordinate_frame.trimmed().isEmpty() ||
        target_coordinate_frame.trimmed().isEmpty()) {
        return fail("Gaze and target coordinate names are required");
    }
    if (!finiteRigid(vicon_from_target)) return fail("Measured Vicon stair pose is invalid");
    if (!created_at.isValid()) return fail("Creation time is invalid");
    if (reason) reason->clear();
    return true;
}

CalibrationProfile ManagedCalibrationProfile::solverProfile() const {
    const CalibrationProfile& defaults = defaultStairCalibrationProfile();
    CalibrationProfile result = defaults;
    result.id = id.toStdString();
    result.vicon_from_target = vicon_from_target;
    return result;
}

QJsonObject ManagedCalibrationProfile::toJson() const {
    return {
        {"version", version},
        {"id", id},
        {"displayName", display_name},
        {"physicalSetupId", physical_setup_id},
        {"stairModelPath", stair_model_path},
        {"stairModelIdentity", stair_model_identity},
        {"viconFromTarget", rigidToJson(vicon_from_target)},
        {"gazeTransform", transformToJson(gaze_transform)},
        {"gazeCoordinateFrame", gaze_coordinate_frame},
        {"targetCoordinateFrame", target_coordinate_frame},
        {"setupNotes", setup_notes},
        {"createdAt", created_at.toString(Qt::ISODateWithMs)},
        {"quality", QJsonObject{
            {"sampleCount", static_cast<qint64>(quality.sample_count)},
            {"translationRmsM", quality.translation_rms_m},
            {"rotationRmsDegrees", quality.rotation_rms_degrees},
        }},
        {"metadataFallbackConfirmed", metadata_fallback_confirmed},
        {"retired", retired},
    };
}

ManagedCalibrationProfile ManagedCalibrationProfile::fromJson(
    const QJsonObject& object,
    QString* error) {
    ManagedCalibrationProfile result;
    const int stored_version = object.value("version").toInt(0);
    if (stored_version != CurrentVersion) {
        if (error) *error = "This saved calibration file version is not supported";
        return {};
    }
    result.version = CurrentVersion;
    result.id = object.value("id").toString();
    result.display_name = object.value("displayName").toString();
    result.physical_setup_id = object.value("physicalSetupId").toString();
    result.stair_model_path = object.value("stairModelPath").toString();
    result.stair_model_identity = object.value("stairModelIdentity").toString();
    result.vicon_from_target = rigidFromJson(object.value("viconFromTarget").toObject());
    result.gaze_transform = transformFromJson(object.value("gazeTransform").toObject());
    result.gaze_coordinate_frame = object.value("gazeCoordinateFrame").toString();
    result.target_coordinate_frame = object.value("targetCoordinateFrame").toString();
    result.setup_notes = object.value("setupNotes").toString();
    result.created_at = QDateTime::fromString(object.value("createdAt").toString(),
                                              Qt::ISODateWithMs);
    const QJsonObject quality = object.value("quality").toObject();
    result.quality.sample_count = static_cast<std::size_t>(
        (std::max)(qint64{0}, quality.value("sampleCount").toInteger()));
    result.quality.translation_rms_m = quality.value("translationRmsM").toDouble();
    result.quality.rotation_rms_degrees = quality.value("rotationRmsDegrees").toDouble();
    result.metadata_fallback_confirmed =
        object.value("metadataFallbackConfirmed").toBool(false);
    result.retired = object.value("retired").toBool(false);
    QString reason;
    if (!result.complete(&reason)) {
        if (error) *error = reason;
        return {};
    }
    if (error) error->clear();
    return result;
}

QVector<ManagedCalibrationProfile> CalibrationProfileStore::load(QSettings& settings) {
    QVector<ManagedCalibrationProfile> result;
    const QByteArray bytes = settings.value("session/calibrationProfiles").toByteArray();
    if (!bytes.isEmpty()) {
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
        if (parse_error.error == QJsonParseError::NoError && document.isArray()) {
            for (const QJsonValue& value : document.array()) {
                QString error;
                ManagedCalibrationProfile profile =
                    ManagedCalibrationProfile::fromJson(value.toObject(), &error);
                if (error.isEmpty()) result.push_back(std::move(profile));
            }
        }
    }
    if (result.isEmpty()) result.push_back(defaultProfile());
    return result;
}

bool CalibrationProfileStore::save(
    QSettings& settings,
    const QVector<ManagedCalibrationProfile>& profiles,
    QString* error) {
    QJsonArray serialized;
    for (const ManagedCalibrationProfile& profile : profiles) {
        QString reason;
        if (!profile.complete(&reason)) {
            if (error) *error = profile.display_name + ": " + reason;
            return false;
        }
        serialized.push_back(profile.toJson());
    }
    settings.setValue("session/calibrationProfiles",
                      QJsonDocument(serialized).toJson(QJsonDocument::Compact));
    if (settings.status() != QSettings::NoError) {
        if (error) *error = "Could not save calibrations";
        return false;
    }
    if (error) error->clear();
    return true;
}

ManagedCalibrationProfile CalibrationProfileStore::defaultProfile() {
    const CalibrationProfile& defaults = defaultStairCalibrationProfile();
    ManagedCalibrationProfile result;
    result.id = QString::fromStdString(defaults.id);
    result.display_name = "Default stair setup";
    result.physical_setup_id = "stair-setup-default";
    result.stair_model_identity = "stair-model-v1";
    result.vicon_from_target = defaults.vicon_from_target;
    result.gaze_transform.name = "HoloLens";
    result.gaze_coordinate_frame = "hololens_stationary_shared_with_gaze";
    result.target_coordinate_frame = "hololens_stationary_shared_with_gaze";
    result.setup_notes = "Built-in setup; confirm the measured stair pose before reuse.";
    result.created_at = QDateTime::currentDateTimeUtc();
    result.metadata_fallback_confirmed = false;
    return result;
}

QString CalibrationProfileStore::newProfileId(
    const QString& display_name,
    const QVector<ManagedCalibrationProfile>& existing) {
    const QString base = normalizedId(display_name);
    QString candidate = base;
    int suffix = 2;
    const auto used = [&existing](const QString& id) {
        return std::any_of(existing.begin(), existing.end(), [&id](const auto& profile) {
            return profile.id.compare(id, Qt::CaseInsensitive) == 0;
        });
    };
    while (used(candidate)) candidate = base + "-" + QString::number(suffix++);
    return candidate;
}

ManagedCalibrationProfile CalibrationProfileStore::duplicate(
    const ManagedCalibrationProfile& source,
    const QVector<ManagedCalibrationProfile>& existing) {
    ManagedCalibrationProfile result = source;
    result.display_name = source.display_name + " copy";
    result.id = newProfileId(result.display_name, existing);
    result.created_at = QDateTime::currentDateTimeUtc();
    result.retired = false;
    return result;
}

bool CalibrationProfileStore::retire(QVector<ManagedCalibrationProfile>& profiles,
                                     const QString& id) {
    for (ManagedCalibrationProfile& profile : profiles) {
        if (profile.id == id) {
            profile.retired = true;
            return true;
        }
    }
    return false;
}

bool CalibrationProfileStore::exportProfile(
    const QString& path,
    const ManagedCalibrationProfile& profile,
    QString* error) {
    QString reason;
    if (!profile.complete(&reason)) {
        if (error) *error = reason;
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(profile.toJson()).toJson(QJsonDocument::Indented)) < 0) {
        if (error) *error = file.errorString();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool CalibrationProfileStore::importProfile(
    const QString& path,
    ManagedCalibrationProfile& profile,
    QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = "Could not read the calibration file: " + parse_error.errorString();
        return false;
    }
    profile = ManagedCalibrationProfile::fromJson(document.object(), error);
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
