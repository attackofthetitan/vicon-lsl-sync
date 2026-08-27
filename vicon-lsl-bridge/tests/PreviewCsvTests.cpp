#include "preview/PreviewCsv.h"
#include "preview/PreviewPlaybackClock.h"
#include "PreviewCoreTestSupport.h"
#include "TestSupport.h"

#include <chrono>
#include <fstream>
#include <stdexcept>
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

TEST_CASE("Preview CSV loader decimates predictably and honors cancellation") {
    const TemporaryFilePath temporary_path("_bounded.csv");
    const std::string path = temporary_path.string();
    {
        std::ofstream output(path);
        output << "relative_time,ViconMarkers_M:X,ViconMarkers_M:Y,"
                  "ViconMarkers_M:Z,ViconMarkers_M:Valid\n";
        for (int index = 0; index < 100; ++index) {
            output << static_cast<double>(index) * 0.01 << ','
                   << index << ",0,0,1\n";
        }
    }
    vicon_lsl::PreviewLoadOptions options;
    options.maximum_preview_frames = 8;
    options.maximum_memory_bytes = 4096;
    int progress_updates = 0;
    options.progress =
        [&progress_updates](const vicon_lsl::PreviewLoadProgress&) {
            ++progress_updates;
        };
    vicon_lsl::PreviewTransformProfile vicon;
    vicon.scale = 0.001;
    const auto bounded =
        vicon_lsl::loadMergedPreviewCsv(path, vicon, {}, options);
    REQUIRE_EQ(bounded.source_frame_count, static_cast<std::size_t>(100));
    REQUIRE(bounded.frames.size() <= static_cast<std::size_t>(8));
    REQUIRE(bounded.stored_frame_stride > static_cast<std::size_t>(1));
    REQUIRE(near(bounded.source_start_timestamp, 0.0));
    REQUIRE(near(bounded.source_end_timestamp, 0.99));
    REQUIRE(progress_updates > 0);
    REQUIRE(bounded.estimated_memory_bytes > 0);
    REQUIRE(bounded.estimated_memory_bytes <= options.maximum_memory_bytes);

    vicon_lsl::PreviewLoadOptions canceled_options;
    int checks = 0;
    canceled_options.cancel_requested = [&checks]() {
        return ++checks > 1;
    };
    bool canceled = false;
    const auto cancel_started = std::chrono::steady_clock::now();
    try {
        (void)vicon_lsl::loadMergedPreviewCsv(
            path, vicon, {}, canceled_options);
    } catch (const std::runtime_error& error) {
        canceled = std::string(error.what()).find("canceled") !=
                   std::string::npos;
    }
    REQUIRE(canceled);
    REQUIRE(std::chrono::steady_clock::now() - cancel_started <
            std::chrono::milliseconds(250));
}

TEST_CASE("Preview CSV cache stays bounded for short one-hour and multi-hour recordings") {
    struct DurationCase {
        int rows;
        double interval;
    };
    const DurationCase durations[] = {
        {601, 0.1},
        {36001, 0.1},
        {108001, 0.1},
    };

    for (const DurationCase duration : durations) {
        const TemporaryFilePath temporary_path("_duration.csv");
        const std::string path = temporary_path.string();
        {
            std::ofstream output(path);
            output << "relative_time,ViconMarkers_M:X,ViconMarkers_M:Y,"
                      "ViconMarkers_M:Z,ViconMarkers_M:Valid\n";
            for (int index = 0; index < duration.rows; ++index) {
                output << static_cast<double>(index) * duration.interval
                       << ',' << index << ",0,0,1\n";
            }
        }

        vicon_lsl::PreviewLoadOptions options;
        options.maximum_preview_frames = 128;
        options.maximum_memory_bytes = 128 * 1024;
        vicon_lsl::PreviewTransformProfile vicon;
        vicon.scale = 0.001;
        const auto started = std::chrono::steady_clock::now();
        const auto recording =
            vicon_lsl::loadMergedPreviewCsv(path, vicon, {}, options);
        const auto elapsed = std::chrono::steady_clock::now() - started;

        REQUIRE_EQ(recording.source_frame_count,
                   static_cast<std::size_t>(duration.rows));
        REQUIRE(recording.frames.size() <= options.maximum_preview_frames);
        REQUIRE(recording.estimated_memory_bytes <= options.maximum_memory_bytes);
        REQUIRE(near(recording.source_start_timestamp, 0.0));
        const double expected_end =
            static_cast<double>(duration.rows - 1) * duration.interval;
        REQUIRE(near(recording.source_end_timestamp, expected_end, 1e-6));
        REQUIRE(near(recording.frames.front().timestamp, 0.0));
        REQUIRE(near(recording.frames.back().timestamp, expected_end, 1e-6));
        REQUIRE(elapsed < std::chrono::seconds(15));
        if (expected_end >= 3.0 * 60.0 * 60.0) {
            vicon_lsl::PreviewPlaybackClock clock;
            clock.setFrameTimeline(recording.frames);
            clock.seek(2.0 * 60.0 * 60.0, 0.0);
            const std::size_t index = clock.frameIndex(0.0);
            REQUIRE(index > 0);
            REQUIRE(index + 1 < recording.frames.size());
            REQUIRE(std::abs(recording.frames[index].timestamp -
                             2.0 * 60.0 * 60.0) < 120.0);
        }
    }
}

TEST_CASE("Preview CSV loader rejects configured row and column limits") {
    const TemporaryFilePath temporary_path("_limits.csv");
    const std::string path = temporary_path.string();
    {
        std::ofstream output(path);
        output << "relative_time,a,b,c\n0,1,2,3\n";
    }
    vicon_lsl::PreviewLoadOptions columns;
    columns.maximum_columns = 3;
    bool excessive_columns = false;
    try {
        (void)vicon_lsl::loadMergedPreviewCsv(path, {}, {}, columns);
    } catch (const std::runtime_error& error) {
        excessive_columns = std::string(error.what()).find("column-count") !=
                            std::string::npos;
    }
    REQUIRE(excessive_columns);

    vicon_lsl::PreviewLoadOptions line;
    line.maximum_line_bytes = 8;
    bool excessive_line = false;
    try {
        (void)vicon_lsl::loadMergedPreviewCsv(path, {}, {}, line);
    } catch (const std::runtime_error& error) {
        excessive_line = std::string(error.what()).find("line-size") !=
                         std::string::npos;
    }
    REQUIRE(excessive_line);
}
