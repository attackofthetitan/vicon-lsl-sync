#include "preview/PreviewCalibration.h"
#include "preview/PreviewMath.h"
#include "preview/PreviewXdf.h"
#include "PreviewCoreTestSupport.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using preview_core_test_support::TemporaryFilePath;
using preview_core_test_support::calibrationLabels;
using preview_core_test_support::gazeLabels;
using preview_core_test_support::near;

namespace {

template <typename T>
void writeLittle(std::ostream& output, T value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void writeVarlenInt(std::ostream& output, std::uint64_t value) {
    if (value < 256) {
        output.put(1);
        output.put(static_cast<char>(value));
    } else if (value <= 0xffffffffu) {
        output.put(4);
        writeLittle(output, static_cast<std::uint32_t>(value));
    } else {
        output.put(8);
        writeLittle(output, value);
    }
}

void writeTimestamp(std::ostream& output, double timestamp) {
    output.put(8);
    writeLittle(output, timestamp);
}

void writeXdfChunk(std::ostream& output,
                   std::uint16_t tag,
                   const std::string& content,
                   const std::uint32_t* stream_id = nullptr) {
    std::uint64_t length = content.size() + sizeof(tag);
    if (stream_id) {
        length += sizeof(*stream_id);
    }
    writeVarlenInt(output, length);
    writeLittle(output, tag);
    if (stream_id) {
        writeLittle(output, *stream_id);
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string streamHeaderXml(const std::string& name,
                            const std::string& type,
                            const std::vector<std::string>& labels,
                            double nominal_srate = 0.0) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\"?><info>"
        << "<name>" << name << "</name>"
        << "<type>" << type << "</type>"
        << "<channel_count>" << labels.size() << "</channel_count>"
        << "<nominal_srate>" << nominal_srate << "</nominal_srate>"
        << "<channel_format>double64</channel_format>"
        << "<source_id>" << name << "-test</source_id>"
        << "<desc><channels>";
    for (const auto& label : labels) {
        xml << "<channel><label>" << label << "</label></channel>";
    }
    xml << "</channels></desc></info>";
    return xml.str();
}

void writeStreamHeader(std::ostream& output,
                       std::uint32_t stream_id,
                       const std::string& name,
                       const std::string& type,
                       const std::vector<std::string>& labels,
                       double nominal_srate = 0.0) {
    writeXdfChunk(output, 2, streamHeaderXml(name, type, labels, nominal_srate), &stream_id);
}

void writeEncodedSampleChunk(std::ostream& output,
                             std::uint32_t stream_id,
                             const std::vector<std::optional<double>>& timestamps,
                             const std::vector<std::vector<double>>& samples) {
    std::ostringstream content;
    writeVarlenInt(content, samples.size());
    for (std::size_t sample_index = 0; sample_index < samples.size(); ++sample_index) {
        if (timestamps[sample_index]) {
            writeTimestamp(content, *timestamps[sample_index]);
        } else {
            content.put(0);
        }
        for (double value : samples[sample_index]) {
            writeLittle(content, value);
        }
    }
    writeXdfChunk(output, 3, content.str(), &stream_id);
}

void writeSampleChunk(std::ostream& output,
                      std::uint32_t stream_id,
                      const std::vector<double>& timestamps,
                      const std::vector<std::vector<double>>& samples) {
    std::vector<std::optional<double>> encoded_timestamps;
    encoded_timestamps.reserve(timestamps.size());
    for (double timestamp : timestamps) {
        encoded_timestamps.emplace_back(timestamp);
    }
    writeEncodedSampleChunk(output, stream_id, encoded_timestamps, samples);
}

void writeClockOffsetChunk(std::ostream& output,
                           std::uint32_t stream_id,
                           double collection_time,
                           double offset) {
    std::ostringstream content;
    writeLittle(content, collection_time);
    writeLittle(content, offset);
    writeXdfChunk(output, 4, content.str(), &stream_id);
}


} // namespace

TEST_CASE("Preview XDF playback rejects mismatched coordinate systems") {
    const auto profile = vicon_lsl::defaultStairCalibrationProfile();

    vicon_lsl::XdfStreamData gaze;
    gaze.stream_id = 1;
    gaze.name = "HoloLensGaze";
    gaze.role = vicon_lsl::PreviewStreamRole::HoloLensGaze;
    gaze.coordinate_frame = "eye_tracker_space";
    gaze.channel_labels = gazeLabels();
    gaze.timestamps = {10.0};
    gaze.samples = {std::vector<double>(vicon_lsl::kHoloLensGazeChannelCount, 0.0)};
    gaze.samples.front()[0] = 1.0;
    gaze.samples.front()[1] = 2.0;
    gaze.samples.front()[2] = 3.0;
    gaze.samples.front()[5] = 1.0;
    gaze.samples.front()[6] = 1.0;

    vicon_lsl::XdfStreamData target;
    target.stream_id = 2;
    target.name = "HoloLensModelTargetPose";
    target.role = vicon_lsl::PreviewStreamRole::HoloLensCalibrationTarget;
    target.coordinate_frame = "hololens_stationary_shared_with_gaze";
    target.channel_labels = calibrationLabels();
    for (std::size_t index = 0; index < profile.required_samples; ++index) {
        target.timestamps.push_back(9.9 + static_cast<double>(index) * 0.005);
        target.samples.push_back({1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0, 1.0});
    }

    vicon_lsl::XdfLoadResult xdf;
    xdf.streams = {std::move(target), std::move(gaze)};
    bool rejected = false;
    try {
        vicon_lsl::buildXdfPreviewRecording(xdf, {}, {}, 0.05);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    REQUIRE(rejected);
}

TEST_CASE("Preview XDF playback calibrates shared Unity-world gaze from the stair target") {
    const auto profile = vicon_lsl::defaultStairCalibrationProfile();
    const double half_sqrt_two = std::sqrt(0.5);
    const vicon_lsl::PreviewRigidTransform holo_from_target{
        {1.0, 2.0, 3.0}, {0.0, half_sqrt_two, 0.0, half_sqrt_two}};
    const vicon_lsl::PreviewVec3 target_space_origin{0.25, 0.5, 1.0};
    const vicon_lsl::PreviewVec3 target_space_direction{0.0, 0.0, 1.0};
    const vicon_lsl::PreviewVec3 target_rh_origin{
        target_space_origin.x, target_space_origin.y, -target_space_origin.z};
    const vicon_lsl::PreviewVec3 target_rh_direction{
        target_space_direction.x, target_space_direction.y, -target_space_direction.z};
    const auto holo_space_origin =
        vicon_lsl::applyRigidTransformPoint(holo_from_target, target_rh_origin);
    const auto holo_space_direction =
        vicon_lsl::rotateByQuaternion(target_rh_direction, holo_from_target.rotation);

    vicon_lsl::XdfStreamData gaze;
    gaze.stream_id = 1;
    gaze.name = "HoloLensGaze";
    gaze.role = vicon_lsl::PreviewStreamRole::HoloLensGaze;
    gaze.coordinate_frame = "hololens_stationary_shared_with_gaze";
    gaze.channel_labels = gazeLabels();
    gaze.timestamps = {10.0};
    gaze.samples = {std::vector<double>(vicon_lsl::kHoloLensGazeChannelCount, 0.0)};
    gaze.samples.front()[0] = holo_space_origin.x;
    gaze.samples.front()[1] = holo_space_origin.y;
    gaze.samples.front()[2] = holo_space_origin.z;
    gaze.samples.front()[3] = holo_space_direction.x;
    gaze.samples.front()[4] = holo_space_direction.y;
    gaze.samples.front()[5] = holo_space_direction.z;
    gaze.samples.front()[6] = 1.0;

    vicon_lsl::XdfStreamData target;
    target.stream_id = 2;
    target.name = "HoloLensModelTargetPose";
    target.role = vicon_lsl::PreviewStreamRole::HoloLensCalibrationTarget;
    target.coordinate_frame = "hololens_stationary_shared_with_gaze";
    target.channel_labels = calibrationLabels();
    for (std::size_t index = 0; index < profile.required_samples; ++index) {
        target.timestamps.push_back(9.9 + static_cast<double>(index) * 0.005);
        target.samples.push_back({holo_from_target.translation.x,
                                  holo_from_target.translation.y,
                                  holo_from_target.translation.z,
                                  holo_from_target.rotation.x,
                                  holo_from_target.rotation.y,
                                  holo_from_target.rotation.z,
                                  holo_from_target.rotation.w,
                                  1.0});
    }

    vicon_lsl::XdfLoadResult xdf;
    xdf.streams = {std::move(target), std::move(gaze)};
    const auto recording = vicon_lsl::buildXdfPreviewRecording(xdf, {}, {}, 0.05);

    REQUIRE_EQ(recording.frames.size(), static_cast<std::size_t>(1));
    const auto& ray = recording.frames.front().gaze_rays.front();
    REQUIRE(ray.valid);
    const vicon_lsl::PreviewQuaternion stair_facing_rotation{0.0, 0.0, 1.0, 0.0};
    const auto expected_origin =
        vicon_lsl::applyRigidTransformPoint(
            profile.vicon_from_target,
            vicon_lsl::rotateByQuaternion(target_space_origin, stair_facing_rotation));
    const auto expected_direction =
        vicon_lsl::rotateByQuaternion(
            vicon_lsl::rotateByQuaternion(target_space_direction,
                                          stair_facing_rotation),
            profile.vicon_from_target.rotation);
    REQUIRE(near(ray.origin.x, expected_origin.x));
    REQUIRE(near(ray.origin.y, expected_origin.y));
    REQUIRE(near(ray.origin.z, expected_origin.z));
    REQUIRE(near(ray.direction.x, expected_direction.x));
    REQUIRE(near(ray.direction.y, expected_direction.y));
    REQUIRE(near(ray.direction.z, expected_direction.z));
    REQUIRE(recording.summary.find("stair-target calibration applied") != std::string::npos);
}

TEST_CASE("Preview XDF loader reconstructs timestamps and applies fitted clock offsets") {
    const TemporaryFilePath temporary_path(".xdf");
    const std::string path = temporary_path.string();
    const std::uint32_t marker_stream_id = 1;
    const std::uint32_t gaze_stream_id = 2;
    const std::vector<std::string> marker_labels = {
        "Subject:LASI:X", "Subject:LASI:Y", "Subject:LASI:Z", "Subject:LASI:Valid",
    };
    std::vector<double> gaze_sample(vicon_lsl::kHoloLensGazeChannelCount, 0.0);
    gaze_sample[0] = 0.25;
    gaze_sample[1] = 0.5;
    gaze_sample[2] = 0.75;
    gaze_sample[3] = 1.0;
    gaze_sample[4] = 0.0;
    gaze_sample[5] = 0.0;
    gaze_sample[6] = 1.0;

    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeXdfChunk(output, 1, "<?xml version=\"1.0\"?><info><version>1.0</version></info>");
        writeStreamHeader(output, marker_stream_id, "ViconMarkers", "MoCap", marker_labels, 10.0);
        writeStreamHeader(output, gaze_stream_id, "HoloLensGaze", "Gaze", gazeLabels());
        writeEncodedSampleChunk(output,
                                marker_stream_id,
                                {10.0, std::nullopt, std::nullopt},
                                {{1000.0, 0.0, 0.0, 1.0},
                                 {2000.0, 0.0, 0.0, 1.0},
                                 {3000.0, 0.0, 0.0, 1.0}});
        writeSampleChunk(output,
                         gaze_stream_id,
                         {20.0, 20.1, 20.2},
                         {gaze_sample, gaze_sample, gaze_sample});
        writeClockOffsetChunk(output, gaze_stream_id, 20.0, -10.0);
        writeClockOffsetChunk(output, gaze_stream_id, 20.2, -10.0);
    }

    vicon_lsl::PreviewTransformProfile vicon_transform;
    vicon_transform.scale = 0.001;
    vicon_lsl::PreviewTransformProfile gaze_transform;
    const auto recording = vicon_lsl::loadXdfPreviewRecording(
        path,
        vicon_transform,
        gaze_transform,
        0.05);

    REQUIRE_EQ(recording.frames.size(), static_cast<std::size_t>(3));
    REQUIRE(recording.summary.find("2 stream(s)") != std::string::npos);
    REQUIRE(near(recording.frames.front().timestamp, 0.0));
    REQUIRE_EQ(recording.frames.front().markers.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(recording.frames.front().gaze_rays.size(), static_cast<std::size_t>(3));
    REQUIRE(recording.frames.front().markers.front().valid);
    REQUIRE(recording.frames.front().gaze_rays.front().valid);
    REQUIRE(near(recording.frames.front().markers.front().position.x, 1.0));
    REQUIRE(near(recording.frames.front().gaze_rays.front().origin.x, 0.25));
    REQUIRE(near(recording.frames[1].timestamp, 0.1));
    REQUIRE(near(recording.frames[1].markers.front().position.x, 2.0));
    REQUIRE(near(recording.frames[2].markers.front().position.x, 3.0));
}

TEST_CASE("Preview XDF loader fits changing clock offsets in source stream time") {
    const TemporaryFilePath temporary_path(".xdf");
    const std::string path = temporary_path.string();
    const std::uint32_t stream_id = 1;
    const std::vector<std::string> labels = {
        "Subject:LASI:X", "Subject:LASI:Y", "Subject:LASI:Z", "Subject:LASI:Valid",
    };
    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, stream_id, "ViconMarkers", "MoCap", labels);
        writeSampleChunk(output, stream_id, {20.0, 20.1, 20.2},
                         {{1.0, 0.0, 0.0, 1.0},
                          {2.0, 0.0, 0.0, 1.0},
                          {3.0, 0.0, 0.0, 1.0}});
        // XDF ClockOffset CollectionTime is already in the source stream's
        // clock domain, matching the raw sample timestamps.
        writeClockOffsetChunk(output, stream_id, 20.0, -10.0);
        writeClockOffsetChunk(output, stream_id, 20.2, -10.1);
    }

    const auto xdf = vicon_lsl::loadXdfNumericStreams(path);
    REQUIRE_EQ(xdf.streams.size(), static_cast<std::size_t>(1));
    REQUIRE(near(xdf.streams.front().timestamps[0], 10.0));
    REQUIRE(near(xdf.streams.front().timestamps[1], 10.05));
    REQUIRE(near(xdf.streams.front().timestamps[2], 10.1));
}

TEST_CASE("Preview XDF loader falls back to one constant clock offset") {
    const TemporaryFilePath temporary_path(".xdf");
    const std::string path = temporary_path.string();
    const std::uint32_t stream_id = 1;
    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, stream_id, "numeric", "Unknown", {"value"});
        writeSampleChunk(output, stream_id, {50.0, 51.0}, {{1.0}, {2.0}});
        writeClockOffsetChunk(output, stream_id, 50.0, -3.0);
    }

    const auto xdf = vicon_lsl::loadXdfNumericStreams(path);
    REQUIRE_EQ(xdf.streams.size(), static_cast<std::size_t>(1));
    REQUIRE(near(xdf.streams.front().timestamps[0], 47.0));
    REQUIRE(near(xdf.streams.front().timestamps[1], 48.0));
}

TEST_CASE("Preview XDF loader uses a centered offset fit without zeroing absolute time") {
    const TemporaryFilePath temporary_path(".xdf");
    const std::string path = temporary_path.string();
    const std::uint32_t stream_id = 1;
    const std::vector<std::string> labels = {"value"};
    const double source_start = 1000000000.0;
    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, stream_id, "numeric", "Unknown", labels);
        writeSampleChunk(output,
                         stream_id,
                         {source_start + 5.0, source_start + 15.0},
                         {{1.0}, {2.0}});
        // Small measurement noise exercises the full-history least-squares fit.
        writeClockOffsetChunk(output, stream_id, source_start,
                              100.2);
        writeClockOffsetChunk(output, stream_id, source_start + 10.0,
                              100.8);
        writeClockOffsetChunk(output, stream_id, source_start + 20.0,
                              102.1);
    }

    const auto xdf = vicon_lsl::loadXdfNumericStreams(path);
    REQUIRE_EQ(xdf.streams.size(), static_cast<std::size_t>(1));
    REQUIRE(near(xdf.streams.front().timestamps[0], source_start + 105.5583333333));
    REQUIRE(near(xdf.streams.front().timestamps[1], source_start + 116.5083333333));
}

TEST_CASE("Preview XDF timeline matches streams by corrected absolute timestamp") {
    vicon_lsl::XdfStreamData markers;
    markers.stream_id = 1;
    markers.name = "ViconMarkers";
    markers.role = vicon_lsl::PreviewStreamRole::ViconMarkers;
    markers.channel_labels = {
        "Subject:LASI:X", "Subject:LASI:Y", "Subject:LASI:Z", "Subject:LASI:Valid",
    };
    markers.timestamps = {10.0};
    markers.samples = {{1000.0, 0.0, 0.0, 1.0}};

    vicon_lsl::XdfStreamData gaze;
    gaze.stream_id = 2;
    gaze.name = "HoloLensGaze";
    gaze.role = vicon_lsl::PreviewStreamRole::HoloLensGaze;
    gaze.channel_labels = gazeLabels();
    gaze.timestamps = {20.0};
    gaze.samples = {std::vector<double>(vicon_lsl::kHoloLensGazeChannelCount, 0.0)};

    vicon_lsl::XdfLoadResult xdf;
    xdf.streams = {std::move(markers), std::move(gaze)};

    vicon_lsl::PreviewTransformProfile vicon_transform;
    vicon_transform.scale = 0.001;
    const auto recording = vicon_lsl::buildXdfPreviewRecording(
        xdf, vicon_transform, {}, 0.05);
    REQUIRE_EQ(recording.frames.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(recording.frames.front().markers.size(), static_cast<std::size_t>(1));
    REQUIRE(recording.frames.front().gaze_rays.empty());
}

TEST_CASE("Preview XDF loader rejects impossible implicit timestamps and repairs regressions") {
    const TemporaryFilePath implicit_file("_invalid_implicit.xdf");
    const TemporaryFilePath non_monotonic_file("_non_monotonic.xdf");
    const std::string implicit_path = implicit_file.string();
    const std::string non_monotonic_path = non_monotonic_file.string();
    const std::uint32_t stream_id = 1;
    const std::vector<std::string> labels = {"value"};
    {
        std::ofstream output(implicit_path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, stream_id, "numeric", "Unknown", labels, 10.0);
        writeEncodedSampleChunk(output, stream_id, {std::nullopt}, {{1.0}});
    }
    {
        std::ofstream output(non_monotonic_path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, stream_id, "numeric", "Unknown", labels);
        writeSampleChunk(output, stream_id, {2.0, 1.0}, {{1.0}, {2.0}});
    }

    bool rejected_implicit = false;
    try {
        (void)vicon_lsl::loadXdfNumericStreams(implicit_path);
    } catch (const std::runtime_error&) {
        rejected_implicit = true;
    }
    REQUIRE(rejected_implicit);
    const auto repaired = vicon_lsl::loadXdfNumericStreams(non_monotonic_path);
    REQUIRE_EQ(repaired.streams.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(repaired.streams.front().repaired_timestamp_count,
               static_cast<std::size_t>(1));
    REQUIRE(repaired.streams.front().timestamps[1] >
            repaired.streams.front().timestamps[0]);
}

TEST_CASE("Preview XDF loader preserves complete chunks before an interrupted tail") {
    const TemporaryFilePath temporary_path("_truncated_tail.xdf");
    const std::string path = temporary_path.string();
    const std::uint32_t stream_id = 1;
    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, stream_id, "numeric", "Unknown", {"value"});
        writeSampleChunk(output, stream_id, {1.0}, {{42.0}});
        writeVarlenInt(output, 1000);
        writeLittle(output, static_cast<std::uint16_t>(3));
    }

    const auto recovered = vicon_lsl::loadXdfNumericStreams(path);
    REQUIRE(recovered.truncated_tail_ignored);
    REQUIRE_EQ(recovered.streams.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(recovered.streams.front().samples.size(), static_cast<std::size_t>(1));
    REQUIRE(near(recovered.streams.front().samples.front().front(), 42.0));
}

TEST_CASE("Preview XDF loader rejects malformed clock-offset chunks") {
    const TemporaryFilePath temporary_path(".xdf");
    const std::string path = temporary_path.string();
    const std::uint32_t stream_id = 1;
    std::ostringstream malformed_offset;
    writeLittle(malformed_offset, 10.0);
    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, stream_id, "numeric", "Unknown", {"value"});
        writeXdfChunk(output, 4, malformed_offset.str(), &stream_id);
    }

    bool rejected = false;
    try {
        (void)vicon_lsl::loadXdfNumericStreams(path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    REQUIRE(rejected);
}
