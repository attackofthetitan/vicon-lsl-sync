#include "preview/ObjMesh.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewMath.h"
#include "PreviewCoreTestSupport.h"
#include "TestSupport.h"

#include <cmath>
#include <limits>
#include <vector>

using preview_core_test_support::calibrationLabels;
using preview_core_test_support::near;

TEST_CASE("Preview calibration composes and inverts rigid transforms") {
    const vicon_lsl::PreviewRigidTransform vicon_from_stair{
        {10.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 1.0}};
    const vicon_lsl::PreviewRigidTransform holo_from_stair{
        {1.0, 2.0, 3.0}, {0.0, 0.0, 0.0, 1.0}};
    const auto vicon_from_holo = vicon_lsl::composeRigidTransforms(
        vicon_from_stair, vicon_lsl::inverseRigidTransform(holo_from_stair));

    const auto vicon_stair_origin = vicon_lsl::applyRigidTransformPoint(vicon_from_holo, {1.0, 2.0, 3.0});
    REQUIRE(near(vicon_stair_origin.x, 10.0));
    REQUIRE(near(vicon_stair_origin.y, 0.0));
    REQUIRE(near(vicon_stair_origin.z, 0.0));

    const auto profile = vicon_lsl::transformProfileFromRigid(vicon_from_holo);
    REQUIRE(profile.use_quaternion_rotation);
    const auto transformed_direction = vicon_lsl::applyTransformDirection(profile, {0.0, 1.0, 0.0});
    REQUIRE(near(transformed_direction.y, 1.0));
}

TEST_CASE("Preview refaces calibrated gaze without changing the stair model") {
    const auto& calibration = vicon_lsl::defaultStairCalibrationProfile();
    const double half_sqrt_two = std::sqrt(0.5);
    const vicon_lsl::PreviewRigidTransform holo_from_target{
        {1.0, 2.0, 3.0}, {0.0, half_sqrt_two, 0.0, half_sqrt_two}};
    const vicon_lsl::PreviewVec3 target_model_point{0.25, 0.5, 1.0};
    const vicon_lsl::PreviewVec3 target_model_direction{0.0, 0.0, 1.0};
    const vicon_lsl::PreviewVec3 target_rh_point{0.25, 0.5, -1.0};
    const vicon_lsl::PreviewVec3 target_rh_direction{0.0, 0.0, -1.0};
    const auto holo_point =
        vicon_lsl::applyRigidTransformPoint(holo_from_target, target_rh_point);
    const auto holo_direction = vicon_lsl::rotateByQuaternion(
        target_rh_direction,
        holo_from_target.rotation);

    const auto transform = vicon_lsl::gazeTransformFromTargetCalibration(
        calibration,
        holo_from_target);
    const auto point = vicon_lsl::applyTransformPoint(transform, holo_point);
    const auto direction = vicon_lsl::applyTransformDirection(transform, holo_direction);
    const vicon_lsl::PreviewQuaternion stair_facing_rotation{0.0, 0.0, 1.0, 0.0};
    const auto expected_point = vicon_lsl::applyRigidTransformPoint(
        calibration.vicon_from_target,
        vicon_lsl::rotateByQuaternion(target_model_point, stair_facing_rotation));
    const auto expected_direction = vicon_lsl::rotateByQuaternion(
        vicon_lsl::rotateByQuaternion(target_model_direction, stair_facing_rotation),
        calibration.vicon_from_target.rotation);

    REQUIRE(near(transform.input_axis_sign.x, 1.0));
    REQUIRE(near(transform.input_axis_sign.y, 1.0));
    REQUIRE(near(transform.input_axis_sign.z, -1.0));
    REQUIRE(near(point.x, expected_point.x));
    REQUIRE(near(point.y, expected_point.y));
    REQUIRE(near(point.z, expected_point.z));
    REQUIRE(near(direction.x, expected_direction.x));
    REQUIRE(near(direction.y, expected_direction.y));
    REQUIRE(near(direction.z, expected_direction.z));
}

TEST_CASE("Preview calibrated gaze follows the fixed stair ascent direction") {
    const auto& calibration = vicon_lsl::defaultStairCalibrationProfile();
    const auto transform = vicon_lsl::gazeTransformFromTargetCalibration(
        calibration,
        {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 1.0}});

    const auto bottom = vicon_lsl::applyTransformPoint(transform, {-3.0, 0.0, -1.6});
    const auto upper = vicon_lsl::applyTransformPoint(transform, {0.7, 0.0, -2.4});

    REQUIRE(upper.x < bottom.x);
    REQUIRE(upper.z > bottom.z);
}

TEST_CASE("Preview calibration parser rejects invalid and averages tracked poses") {
    const auto labels = calibrationLabels();
    const auto invalid = vicon_lsl::parseCalibrationTargetPose(
        labels, {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0});
    REQUIRE(!invalid.has_value());

    const auto tracked = vicon_lsl::parseCalibrationTargetPose(
        labels, {1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 2.0, 1.0});
    const auto untracked = vicon_lsl::parseCalibrationTargetPose(
        labels, {9.0, 9.0, 9.0, 0.0, 0.0, 0.0, 1.0, 0.0});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto publisher_untracked = vicon_lsl::parseCalibrationTargetPose(
        labels, {nan, nan, nan, nan, nan, nan, nan, 0.0});
    REQUIRE(tracked.has_value());
    REQUIRE(untracked.has_value());
    REQUIRE(publisher_untracked.has_value());
    REQUIRE(!publisher_untracked->tracked);
    const auto average = vicon_lsl::averageTrackedTargetPoses({*tracked, *untracked});
    REQUIRE(average.has_value());
    REQUIRE(near(average->translation.x, 1.0));
    REQUIRE(near(average->translation.y, 2.0));
    REQUIRE(near(average->translation.z, 3.0));
    REQUIRE(near(average->rotation.w, 1.0));
}

TEST_CASE("Preview calibration reports quality and rejects unstable target motion") {
    const auto profile = vicon_lsl::defaultStairCalibrationProfile();
    std::vector<vicon_lsl::CalibrationTargetPose> stable(
        profile.required_samples,
        {{{1.0, 2.0, 3.0}, {0.0, 0.0, 0.0, 1.0}}, true});
    for (std::size_t index = 0; index < stable.size(); ++index) {
        stable[index].holo_from_target.translation.x +=
            static_cast<double>(index % 3) * 0.001;
    }

    const auto solution = vicon_lsl::solveTrackedTargetCalibration(stable, profile);
    REQUIRE(solution.has_value());
    REQUIRE_EQ(solution->quality.sample_count, profile.required_samples);
    REQUIRE(solution->quality.translation_rms_m < profile.translation_tolerance_m);
    REQUIRE(solution->quality.rotation_rms_degrees < 1e-9);

    auto unstable = stable;
    unstable.back().holo_from_target.translation.x += 0.1;
    REQUIRE(!vicon_lsl::targetPoseWithinTolerance(unstable.front(), unstable.back(), profile));
    REQUIRE(!vicon_lsl::solveTrackedTargetCalibration(unstable, profile).has_value());
}

TEST_CASE("Preview calibration finds a stable target window in a recording") {
    const auto profile = vicon_lsl::defaultStairCalibrationProfile();
    std::vector<vicon_lsl::CalibrationTargetPose> poses = {
        {{{9.0, 9.0, 9.0}, {0.0, 0.0, 0.0, 1.0}}, false},
        {{{5.0, 5.0, 5.0}, {0.0, 0.0, 0.0, 1.0}}, true},
    };
    for (std::size_t index = 0; index < profile.required_samples; ++index) {
        poses.push_back({{{1.0 + static_cast<double>(index) * 0.0001, 2.0, 3.0},
                          {0.0, 0.0, 0.0, 1.0}}, true});
    }

    const auto solution = vicon_lsl::solveStableTrackedTargetCalibration(poses, profile);
    REQUIRE(solution.has_value());
    REQUIRE(near(solution->holo_from_target.translation.x, 1.00095, 1e-9));
}

TEST_CASE("Preview calibration aligns a synthetic gaze ray with the stair frame") {
    const vicon_lsl::PreviewRigidTransform vicon_from_stair{
        {2.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 1.0}};
    const vicon_lsl::PreviewRigidTransform holo_from_stair{
        {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 1.0}};
    const auto profile = vicon_lsl::transformProfileFromRigid(
        vicon_lsl::composeRigidTransforms(
            vicon_from_stair, vicon_lsl::inverseRigidTransform(holo_from_stair)));

    const vicon_lsl::PreviewMesh mesh{
        {{2.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {3.0, 1.0, 0.0}, {2.0, 1.0, 0.0}},
        {{0, 1, 2, 3}},
    };
    const auto triangles = vicon_lsl::triangulateMesh(mesh, {});
    const auto origin = vicon_lsl::applyTransformPoint(profile, {1.5, 0.5, 1.0});
    const auto direction = vicon_lsl::applyTransformDirection(profile, {0.0, 0.0, -1.0});
    const auto distance = vicon_lsl::rayTriangleDistance(origin, direction, triangles, 10.0);
    REQUIRE(distance.has_value());
    REQUIRE(near(*distance, 1.0));
}
