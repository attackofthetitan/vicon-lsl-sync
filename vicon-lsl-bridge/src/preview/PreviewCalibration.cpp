#include "preview/PreviewCalibration.h"

#include "preview/PreviewMath.h"
#include "preview/PreviewParsing.h"

#include <algorithm>
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

bool usablePose(const CalibrationTargetPose& pose) {
    return pose.tracked && isFinite(pose.holo_from_target.translation) &&
           usableQuaternion(pose.holo_from_target.rotation);
}

double angleBetweenDegrees(const PreviewQuaternion& left, const PreviewQuaternion& right) {
    const double orientation_dot = std::clamp(
        std::abs(left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w),
        0.0,
        1.0);
    return 2.0 * std::acos(orientation_dot) * 180.0 / 3.14159265358979323846;
}

// Undoes the published world's Z flip and Unity's X flip on model import, which
// maps (x, y, z) to (-x, y, -z) and changes the sign of a rotation's X and Z parts.
PreviewVec3 flipXZ(const PreviewVec3& value) {
    return {-value.x, value.y, -value.z};
}

PreviewQuaternion flipXZ(const PreviewQuaternion& value) {
    return {-value.x, value.y, -value.z, value.w};
}

} // namespace

const CalibrationProfile& defaultStairCalibrationProfile() {
    // The bottom front-left stair corner as measured on 2026-09-17, and the same
    // corner in the model file, which is in millimetres, both given in metres.
    // Facing up the stairs, forward is -X, left is -Y, and the floor is Z=0.
    static const PreviewVec3 measured_corner_m{-1.205, -0.213, 0.0};
    static const PreviewVec3 model_corner_m{1.677676086, -0.523499985, 0.0};
    static const CalibrationProfile profile{
        "stair-model-v1",
        20,
        0.02,
        3.0,
        {measured_corner_m - model_corner_m,
         {0.0, 0.0, 0.0, 1.0}},
    };
    return profile;
}

bool targetPoseWithinTolerance(const CalibrationTargetPose& reference,
                               const CalibrationTargetPose& candidate,
                               const CalibrationProfile& profile) {
    const PreviewVec3 delta = candidate.holo_from_target.translation -
                              reference.holo_from_target.translation;
    if (length(delta) > profile.translation_tolerance_m) {
        return false;
    }
    return angleBetweenDegrees(normalizeQuaternion(reference.holo_from_target.rotation),
                               normalizeQuaternion(candidate.holo_from_target.rotation)) <=
           profile.rotation_tolerance_degrees;
}

std::optional<CalibrationSolution> solveTrackedTargetCalibration(
    const std::vector<CalibrationTargetPose>& poses,
    const CalibrationProfile& profile) {
    const auto average = averageTrackedTargetPoses(poses);
    if (!average) {
        return std::nullopt;
    }

    double translation_squared_sum = 0.0;
    double rotation_squared_sum = 0.0;
    std::size_t count = 0;
    bool uses_frozen_reference = false;
    const PreviewQuaternion mean_rotation = normalizeQuaternion(average->rotation);
    for (const auto& pose : poses) {
        if (!usablePose(pose)) {
            continue;
        }
        const double translation_error =
            length(pose.holo_from_target.translation - average->translation);
        translation_squared_sum += translation_error * translation_error;

        const double angle_degrees = angleBetweenDegrees(
            mean_rotation, normalizeQuaternion(pose.holo_from_target.rotation));
        rotation_squared_sum += angle_degrees * angle_degrees;
        ++count;
        uses_frozen_reference = uses_frozen_reference || pose.frozen_reference;
    }
    if (count < profile.required_samples) {
        return std::nullopt;
    }

    CalibrationSolution solution;
    solution.uses_frozen_reference = uses_frozen_reference;
    solution.holo_from_target = *average;
    solution.quality.sample_count = count;
    solution.quality.translation_rms_m =
        std::sqrt(translation_squared_sum / static_cast<double>(count));
    solution.quality.rotation_rms_degrees =
        std::sqrt(rotation_squared_sum / static_cast<double>(count));
    if (solution.quality.translation_rms_m > profile.translation_tolerance_m ||
        solution.quality.rotation_rms_degrees > profile.rotation_tolerance_degrees) {
        return std::nullopt;
    }
    return solution;
}

std::optional<CalibrationSolution> solveStableTrackedTargetCalibration(
    const std::vector<CalibrationTargetPose>& poses,
    const CalibrationProfile& profile) {
    std::vector<CalibrationTargetPose> stable;
    stable.reserve(profile.required_samples);
    for (const auto& pose : poses) {
        if (!pose.tracked) {
            stable.clear();
            continue;
        }
        if (!stable.empty() &&
            !targetPoseWithinTolerance(stable.front(), pose, profile)) {
            stable.clear();
        }
        stable.push_back(pose);
        if (stable.size() < profile.required_samples) {
            continue;
        }
        const auto solution = solveTrackedTargetCalibration(stable, profile);
        if (solution) {
            return solution;
        }
        stable.erase(stable.begin());
    }
    return std::nullopt;
}

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
    const auto tracked = value("Tracked");
    if (!tracked || !std::isfinite(*tracked)) {
        return std::nullopt;
    }
    // 0 = lost, 1 = tracked live, 2 = held reference while Vuforia is paused.
    if (*tracked == 0.0) {
        return CalibrationTargetPose{{}, false};
    }
    if (*tracked != 1.0 && *tracked != 2.0) {
        return std::nullopt;
    }
    const auto x = value("PositionX");
    const auto y = value("PositionY");
    const auto z = value("PositionZ");
    const auto qx = value("RotationX");
    const auto qy = value("RotationY");
    const auto qz = value("RotationZ");
    const auto qw = value("RotationW");
    if (!x || !y || !z || !qx || !qy || !qz || !qw ||
        !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) {
        return std::nullopt;
    }
    const PreviewQuaternion raw_rotation{*qx, *qy, *qz, *qw};
    if (!usableQuaternion(raw_rotation)) {
        return std::nullopt;
    }
    return CalibrationTargetPose{
        {{*x, *y, *z}, normalizeQuaternion(raw_rotation)}, true, *tracked == 2.0};
}

// Averages positions and rotations, first giving each rotation the same sign as
// the first one, since q and -q are the same rotation.
std::optional<PreviewRigidTransform> averageTrackedTargetPoses(
    const std::vector<CalibrationTargetPose>& poses) {
    PreviewVec3 translation_sum{};
    PreviewQuaternion rotation_sum{0.0, 0.0, 0.0, 0.0};
    PreviewQuaternion reference{};
    std::size_t count = 0;
    for (const auto& pose : poses) {
        if (!usablePose(pose)) {
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

PreviewTransformProfile transformProfileFromRigid(const PreviewRigidTransform& transform) {
    PreviewTransformProfile profile;
    profile.use_quaternion_rotation = true;
    profile.rotation = normalizeQuaternion(transform.rotation);
    profile.translation = transform.translation;
    return profile;
}

PreviewTransformProfile gazeTransformFromTargetCalibration(
    const CalibrationProfile& profile,
    const PreviewRigidTransform& holo_from_target) {
    const PreviewRigidTransform target_from_holo = inverseRigidTransform(holo_from_target);

    // Move gaze into the target's coordinates, undo Unity's axis flips, and place
    // it in Vicon, flipping the target's position and rotation as well as the
    // input signs so nothing is mirrored.
    PreviewTransformProfile transform;
    transform.name = "HoloLens";
    transform.use_quaternion_rotation = true;
    transform.rotation = normalizeQuaternion(multiplyQuaternions(
        profile.vicon_from_target.rotation, flipXZ(target_from_holo.rotation)));
    transform.translation = applyRigidTransformPoint(
        profile.vicon_from_target, flipXZ(target_from_holo.translation));
    transform.input_axis_sign.x = -1.0;
    transform.input_axis_sign.z = -1.0;
    return transform;
}

bool calibrationCoordinateFramesCompatible(const std::string& gaze_frame,
                                           const std::string& target_frame) {
    const std::string gaze = lowerAscii(gaze_frame);
    if (gaze == "eye_tracker_space") {
        return false;
    }
    return gaze_frame.empty() || target_frame.empty() || gaze == lowerAscii(target_frame);
}

} // namespace vicon_lsl
