#include "preview/PreviewCalibration.h"

#include "preview/PreviewMath.h"

#include <cmath>
#include <unordered_map>

namespace vicon_lsl {
namespace {

bool finiteQuaternion(const PreviewQuaternion& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

bool usableQuaternion(const PreviewQuaternion& value) {
    return finiteQuaternion(value) &&
           value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w > 1e-24;
}

} // namespace

PreviewRigidTransform composeRigidTransforms(const PreviewRigidTransform& left,
                                             const PreviewRigidTransform& right) {
    const PreviewQuaternion rotation = multiplyQuaternions(left.rotation, right.rotation);
    return {rotateByQuaternion(right.translation, left.rotation) + left.translation, rotation};
}

PreviewRigidTransform inverseRigidTransform(const PreviewRigidTransform& transform) {
    const PreviewQuaternion rotation = inverseQuaternion(transform.rotation);
    return {rotateByQuaternion(transform.translation * -1.0, rotation), rotation};
}

PreviewVec3 applyRigidTransformPoint(const PreviewRigidTransform& transform,
                                     const PreviewVec3& point) {
    return rotateByQuaternion(point, transform.rotation) + transform.translation;
}

PreviewVec3 applyRigidTransformDirection(const PreviewRigidTransform& transform,
                                         const PreviewVec3& direction) {
    return normalize(rotateByQuaternion(direction, transform.rotation));
}

std::optional<CalibrationTargetPose> parseCalibrationTargetPose(
    const std::vector<std::string>& labels,
    const std::vector<double>& sample) {
    if (labels.size() != sample.size()) {
        return std::nullopt;
    }
    std::unordered_map<std::string, double> fields;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        fields.emplace(labels[index], sample[index]);
    }
    const auto value = [&fields](const char* name) -> std::optional<double> {
        const auto found = fields.find(name);
        return found == fields.end() ? std::nullopt : std::optional<double>(found->second);
    };
    const auto x = value("PositionX"); const auto y = value("PositionY"); const auto z = value("PositionZ");
    const auto qx = value("RotationX"); const auto qy = value("RotationY");
    const auto qz = value("RotationZ"); const auto qw = value("RotationW");
    const auto tracked = value("Tracked");
    if (!x || !y || !z || !qx || !qy || !qz || !qw || !tracked ||
        !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z) ||
        !finiteQuaternion({*qx, *qy, *qz, *qw})) {
        return std::nullopt;
    }
    const PreviewQuaternion raw_rotation{*qx, *qy, *qz, *qw};
    if (!usableQuaternion(raw_rotation)) {
        return std::nullopt;
    }
    const PreviewQuaternion rotation = normalizeQuaternion(raw_rotation);
    return CalibrationTargetPose{{{*x, *y, *z}, rotation}, *tracked > 0.5};
}

std::optional<PreviewRigidTransform> averageTrackedTargetPoses(
    const std::vector<CalibrationTargetPose>& poses) {
    PreviewVec3 translation_sum{};
    PreviewQuaternion rotation_sum{0.0, 0.0, 0.0, 0.0};
    PreviewQuaternion reference{};
    std::size_t count = 0;
    for (const auto& pose : poses) {
        if (!pose.tracked || !isFinite(pose.holo_from_target.translation) ||
            !usableQuaternion(pose.holo_from_target.rotation)) {
            continue;
        }
        const PreviewQuaternion rotation = normalizeQuaternion(pose.holo_from_target.rotation);
        if (count == 0) {
            reference = rotation;
        }
        const double sign = reference.x * rotation.x + reference.y * rotation.y +
                            reference.z * rotation.z + reference.w * rotation.w < 0.0 ? -1.0 : 1.0;
        translation_sum = translation_sum + pose.holo_from_target.translation;
        rotation_sum.x += sign * rotation.x;
        rotation_sum.y += sign * rotation.y;
        rotation_sum.z += sign * rotation.z;
        rotation_sum.w += sign * rotation.w;
        ++count;
    }
    if (count == 0) {
        return std::nullopt;
    }
    return PreviewRigidTransform{translation_sum / static_cast<double>(count),
                                 normalizeQuaternion(rotation_sum)};
}

PreviewTransformProfile transformProfileFromRigid(const PreviewRigidTransform& transform,
                                                  const std::string& name) {
    PreviewTransformProfile profile;
    profile.name = name;
    profile.use_quaternion_rotation = true;
    profile.rotation = normalizeQuaternion(transform.rotation);
    profile.translation = transform.translation;
    return profile;
}

} // namespace vicon_lsl
