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

// One stair target position and rotation from the HoloLens target stream.
struct CalibrationTargetPose {
    PreviewRigidTransform holo_from_target;
    // True for a live tracked position or a held reference.
    bool tracked = false;
    // True when Vuforia was paused and the stream repeats a held reference.
    bool frozen_reference = false;
};

struct CalibrationProfile {
    std::string id;
    std::size_t required_samples = 20;
    double translation_tolerance_m = 0.02;
    double rotation_tolerance_degrees = 3.0;
    // Where the stair target sits in Vicon coordinates, measured once per setup.
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
    bool uses_frozen_reference = false;
};

const CalibrationProfile& defaultStairCalibrationProfile();
bool targetPoseWithinTolerance(const CalibrationTargetPose& reference,
                               const CalibrationTargetPose& candidate,
                               const CalibrationProfile& profile);
// Averages the tracked positions and rotations, and fails if there are too few
// or they spread too far.
std::optional<CalibrationSolution> solveTrackedTargetCalibration(
    const std::vector<CalibrationTargetPose>& poses,
    const CalibrationProfile& profile);
// Finds the first steady stretch of tracked samples that gives a calibration.
std::optional<CalibrationSolution> solveStableTrackedTargetCalibration(
    const std::vector<CalibrationTargetPose>& poses,
    const CalibrationProfile& profile);

PreviewRigidTransform composeRigidTransforms(const PreviewRigidTransform& left,
                                             const PreviewRigidTransform& right);
PreviewRigidTransform inverseRigidTransform(const PreviewRigidTransform& transform);
PreviewVec3 applyRigidTransformPoint(const PreviewRigidTransform& transform,
                                     const PreviewVec3& point);

// Reads one sample of the target stream, whose Tracked value must be 0, 1, or 2.
std::optional<CalibrationTargetPose> parseCalibrationTargetPose(
    const std::vector<std::string>& labels,
    const std::vector<double>& sample);
std::optional<PreviewRigidTransform> averageTrackedTargetPoses(
    const std::vector<CalibrationTargetPose>& poses);
PreviewTransformProfile transformProfileFromRigid(const PreviewRigidTransform& transform);

// The alignment that draws HoloLens gaze in Vicon coordinates, given where the
// HoloLens saw the stair target.
PreviewTransformProfile gazeTransformFromTargetCalibration(
    const CalibrationProfile& profile,
    const PreviewRigidTransform& holo_from_target);
// Ignores letter case, never matches gaze stored relative to the tracker, and
// lets an empty name match anything so older recordings still line up.
bool calibrationCoordinateFramesCompatible(const std::string& gaze_frame,
                                           const std::string& target_frame);

} // namespace vicon_lsl
