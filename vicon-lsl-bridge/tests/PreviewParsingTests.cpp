#include "preview/ObjMesh.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewMath.h"
#include "preview/PreviewParsing.h"
#include "PreviewCoreTestSupport.h"
#include "TestSupport.h"

#include <sstream>
#include <string>
#include <vector>

using preview_core_test_support::calibrationLabels;
using preview_core_test_support::gazeLabels;
using preview_core_test_support::near;

TEST_CASE("Preview parser converts Vicon marker samples from millimetres to metres") {
    const std::vector<std::string> labels = {
        "Subject:LASI:X", "Subject:LASI:Y", "Subject:LASI:Z", "Subject:LASI:Valid",
    };
    const std::vector<double> sample = {1000.0, 2000.0, 3000.0, 1.0};
    vicon_lsl::PreviewTransformProfile transform;
    transform.scale = 0.001;

    const auto markers = vicon_lsl::parseMarkerSample(labels, sample, transform);
    REQUIRE_EQ(markers.size(), static_cast<std::size_t>(1));
    REQUIRE(markers.front().valid);
    REQUIRE_EQ(markers.front().name, std::string("LASI"));
    REQUIRE(near(markers.front().position.x, 1.0));
    REQUIRE(near(markers.front().position.y, 2.0));
    REQUIRE(near(markers.front().position.z, 3.0));
}

TEST_CASE("Preview parser extracts HoloLens gaze rays from native LSL labels") {
    std::vector<double> sample(vicon_lsl::kHoloLensGazeChannelCount, 0.0);
    sample[0] = 1.0;
    sample[1] = 2.0;
    sample[2] = 3.0;
    sample[3] = 10.0;
    sample[4] = 0.0;
    sample[5] = 0.0;
    sample[6] = 1.0;

    vicon_lsl::PreviewTransformProfile transform;
    transform.translation = {1.0, 0.0, 0.0};
    const auto rays = vicon_lsl::parseGazeSample(gazeLabels(), sample, transform);

    REQUIRE_EQ(rays.size(), static_cast<std::size_t>(3));
    REQUIRE(rays.front().valid);
    REQUIRE_EQ(rays.front().name, std::string("Combined"));
    REQUIRE(near(rays.front().origin.x, 2.0));
    REQUIRE(near(rays.front().origin.y, 2.0));
    REQUIRE(near(rays.front().origin.z, 3.0));
    REQUIRE(near(vicon_lsl::length(rays.front().direction), 1.0));
}

TEST_CASE("Preview preserves raw eye-tracker basis and rejects world calibration") {
    std::vector<double> sample(vicon_lsl::kHoloLensGazeChannelCount, 0.0);
    sample[2] = -0.25;
    sample[5] = 1.0;
    sample[6] = 1.0;

    const auto transform = vicon_lsl::gazeTransformForCoordinateFrame(
        {}, "eye_tracker_space");
    const auto rays = vicon_lsl::parseGazeSample(gazeLabels(), sample, transform);

    REQUIRE(rays.front().valid);
    REQUIRE(near(rays.front().origin.z, -0.25));
    REQUIRE(near(rays.front().direction.z, 1.0));
    REQUIRE(!vicon_lsl::calibrationCoordinateFramesCompatible(
        "eye_tracker_space", "hololens_stationary_shared_with_gaze"));
    REQUIRE(vicon_lsl::calibrationCoordinateFramesCompatible(
        "", "hololens_stationary_shared_with_gaze"));
    REQUIRE(vicon_lsl::calibrationCoordinateFramesCompatible(
        "hololens_stationary_shared_with_gaze", ""));
}

TEST_CASE("Preview recognizes the HoloLens stair target stream") {
    vicon_lsl::PreviewStreamSchema schema;
    schema.name = "HoloLensModelTargetPose";
    schema.type = "Calibration";
    schema.channel_labels = calibrationLabels();
    REQUIRE_EQ(vicon_lsl::inferPreviewStreamRole(schema),
               vicon_lsl::PreviewStreamRole::HoloLensCalibrationTarget);
}

TEST_CASE("Preview restores fixed HoloLens labels when live LSL metadata is short") {
    const auto gaze = vicon_lsl::canonicalPreviewChannelLabels(
        vicon_lsl::PreviewStreamRole::HoloLensGaze,
        vicon_lsl::kHoloLensGazeChannelCount);
    const auto target = vicon_lsl::canonicalPreviewChannelLabels(
        vicon_lsl::PreviewStreamRole::HoloLensCalibrationTarget,
        8);

    REQUIRE_EQ(gaze, gazeLabels());
    REQUIRE_EQ(target, calibrationLabels());
    REQUIRE(vicon_lsl::canonicalPreviewChannelLabels(
        vicon_lsl::PreviewStreamRole::HoloLensGaze,
        vicon_lsl::kHoloLensGazeChannelCount - 1).empty());
}

TEST_CASE("Preview timestamp tolerance accepts only nearby samples") {
    REQUIRE(vicon_lsl::timestampWithinTolerance(10.0, 10.04, 0.05));
    REQUIRE(!vicon_lsl::timestampWithinTolerance(10.0, 10.06, 0.05));
}

TEST_CASE("Preview OBJ parser triangulates faces and ray intersection hits mesh") {
    std::istringstream obj(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "f 1 2 3 4\n");
    const auto mesh = vicon_lsl::parseObjMesh(obj);
    vicon_lsl::PreviewTransformProfile transform;
    const auto triangles = vicon_lsl::triangulateMesh(mesh, transform);

    REQUIRE_EQ(mesh.vertices.size(), static_cast<std::size_t>(4));
    REQUIRE_EQ(mesh.faces.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(triangles.size(), static_cast<std::size_t>(2));

    const auto distance = vicon_lsl::rayTriangleDistance(
        {0.5, 0.5, 1.0},
        {0.0, 0.0, -1.0},
        triangles,
        10.0);
    REQUIRE(distance.has_value());
    REQUIRE(near(*distance, 1.0));
}

TEST_CASE("Preview segment axes preserve identity quaternion orientation") {
    const auto axes = vicon_lsl::segmentAxes({0.0, 0.0, 0.0, 1.0});
    REQUIRE(near(axes[0].x, 1.0));
    REQUIRE(near(axes[1].y, 1.0));
    REQUIRE(near(axes[2].z, 1.0));
}
