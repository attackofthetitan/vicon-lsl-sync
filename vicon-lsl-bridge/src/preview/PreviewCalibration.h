#pragma once

#include "preview/PreviewTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace vicon_lsl {

struct PreviewRigidTransform {
    PreviewVec3 translation{};
    PreviewQuaternion rotation{};
};

struct CalibrationTargetPose {
    PreviewRigidTransform holo_from_target;
    bool tracked = false;
};

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
