#include "preview/PreviewCsv.h"
#include "PreviewCoreTestSupport.h"
#include "TestSupport.h"

#include <fstream>
#include <string>

using preview_core_test_support::TemporaryFilePath;
using preview_core_test_support::near;

TEST_CASE("Preview merged CSV loader builds preview frames") {
    const TemporaryFilePath temporary_path(".csv");
    const std::string path = temporary_path.string();
    {
        std::ofstream output(path);
        output << "relative_time,ViconMarkers_Subject:LASI:X,ViconMarkers_Subject:LASI:Y,"
                  "ViconMarkers_Subject:LASI:Z,ViconMarkers_Subject:LASI:Valid,"
                  "HoloLensGaze_CombinedOriginX,HoloLensGaze_CombinedOriginY,"
                  "HoloLensGaze_CombinedOriginZ,HoloLensGaze_CombinedDirectionX,"
                  "HoloLensGaze_CombinedDirectionY,HoloLensGaze_CombinedDirectionZ,"
                  "HoloLensGaze_CombinedValid\n";
        output << "0.5,1000,0,0,1,0,0,0,1,0,0,1\n";
    }

    vicon_lsl::PreviewTransformProfile vicon_transform;
    vicon_transform.scale = 0.001;
    vicon_lsl::PreviewTransformProfile gaze_transform;
    const auto recording = vicon_lsl::loadMergedPreviewCsv(path, vicon_transform, gaze_transform);

    REQUIRE_EQ(recording.frames.size(), static_cast<std::size_t>(1));
    REQUIRE(near(recording.frames.front().timestamp, 0.5));
    REQUIRE_EQ(recording.frames.front().markers.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(recording.frames.front().gaze_rays.size(), static_cast<std::size_t>(1));
    REQUIRE(recording.frames.front().markers.front().valid);
    REQUIRE(near(recording.frames.front().markers.front().position.x, 1.0));
}
