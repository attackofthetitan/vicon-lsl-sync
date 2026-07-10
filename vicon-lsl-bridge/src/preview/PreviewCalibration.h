#pragma once

#include "preview/PreviewTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace vicon_lsl {

enum class CalibrationState {
    Manual,
    Collecting,
    AutomaticSession,
};

struct PreviewRigidTransform {
    PreviewVec3 translation{};
    PreviewQuaternion rotation{};
};

struct CalibrationTargetPose {
    PreviewRigidTransform holo_from_target;
    bool tracked = false;
};

struct CalibrationProfile {
    std::string id;
    std::size_t required_samples = 20;
    double translation_tolerance_m = 0.02;
    double rotation_tolerance_degrees = 3.0;
    PreviewRigidTransform vicon_from_target;
};

struct CalibrationQuality {
    std::size_t sample_count = 0;
    double translation_rms_m = 0.0;
    double rotation_rms_degrees = 0.0;
};

struct CalibrationSolution {
    PreviewRigidTransform holo_from_target;
    CalibrationQuality quality;
};

const CalibrationProfile& defaultStairCalibrationProfile();
bool targetPoseWithinTolerance(const CalibrationTargetPose& reference,
                               const CalibrationTargetPose& candidate,
                               const CalibrationProfile& profile);
std::optional<CalibrationSolution> solveTrackedTargetCalibration(
    const std::vector<CalibrationTargetPose>& poses,
    const CalibrationProfile& profile);

PreviewRigidTransform composeRigidTransforms(const PreviewRigidTransform& left,
                                             const PreviewRigidTransform& right);
PreviewRigidTransform inverseRigidTransform(const PreviewRigidTransform& transform);
PreviewVec3 applyRigidTransformPoint(const PreviewRigidTransform& transform,
                                     const PreviewVec3& point);
PreviewVec3 applyRigidTransformDirection(const PreviewRigidTransform& transform,
                                         const PreviewVec3& direction);

std::optional<CalibrationTargetPose> parseCalibrationTargetPose(
    const std::vector<std::string>& labels,
    const std::vector<double>& sample);
std::optional<PreviewRigidTransform> averageTrackedTargetPoses(
    const std::vector<CalibrationTargetPose>& poses);
PreviewTransformProfile transformProfileFromRigid(const PreviewRigidTransform& transform,
                                                  const std::string& name = "HoloLens");

} // namespace vicon_lsl
