#include "preview/PreviewCalibration.h"
#include "preview/PreviewMath.h"
#include "preview/PreviewPlaybackClock.h"
#include "preview/PreviewXdf.h"
#include "preview/PreviewXdfMapping.h"
#include "PreviewCoreTestSupport.h"
#include "TestSupport.h"

#include <cmath>
#include <chrono>
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

TEST_CASE("Preview XDF playback keeps legacy tracker-local gaze uncalibrated") {
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
    const auto recording = vicon_lsl::buildXdfPreviewRecording(xdf, {}, {}, 0.05);

    REQUIRE_EQ(recording.frames.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(recording.frames.front().gaze_rays.size(), static_cast<std::size_t>(3));
    const auto& ray = recording.frames.front().gaze_rays.front();
    REQUIRE(ray.valid);
    REQUIRE(near(ray.origin.x, 1.0));
    REQUIRE(near(ray.origin.y, 2.0));
    REQUIRE(near(ray.origin.z, 3.0));
    REQUIRE(near(ray.direction.z, 1.0));
    REQUIRE(recording.summary.find("legacy tracker-local gaze shown without stair calibration") !=
            std::string::npos);
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

TEST_CASE("Recorded stream mapping stitches compatible recovered identities") {
    const auto make_stream = [](std::uint32_t id,
                                const std::string& source,
                                std::vector<double> timestamps) {
        vicon_lsl::XdfStreamData stream;
        stream.stream_id = id;
        stream.name = "ViconMarkers";
        stream.type = "Markers";
        stream.source_id = source;
        stream.hostname = "capture";
        stream.session_id = "session";
        stream.channel_format = "double64";
        stream.coordinate_frame = "vicon";
        stream.channel_count = 4;
        stream.channel_labels = {"x", "y", "z", "valid"};
        stream.role = vicon_lsl::PreviewStreamRole::ViconMarkers;
        stream.timestamps = std::move(timestamps);
        for (double timestamp : stream.timestamps) {
            stream.samples.push_back({timestamp, 0.0, 0.0, 1.0});
        }
        stream.sample_count = stream.samples.size();
        stream.start_timestamp = stream.timestamps.front();
        stream.end_timestamp = stream.timestamps.back();
        return stream;
    };

    vicon_lsl::XdfLoadResult xdf;
    xdf.streams.push_back(make_stream(1, "markers-stable", {1.0, 2.0}));
    xdf.streams.push_back(make_stream(7, "markers-stable", {3.0, 4.0}));
    const auto analysis = vicon_lsl::analyzeXdfStreamMapping(xdf);
    REQUIRE(!analysis.requires_explicit_mapping);
    REQUIRE(analysis.explanation.find("stitched") != std::string::npos);
    REQUIRE_EQ(analysis.candidates.size(), static_cast<std::size_t>(2));
    const auto stitched = vicon_lsl::applyXdfStreamMapping(
        xdf, analysis.suggested_mapping);
    REQUIRE_EQ(stitched.streams.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(stitched.streams.front().sample_count,
               static_cast<std::size_t>(4));
    REQUIRE_EQ(stitched.streams.front().timestamps.size(),
               static_cast<std::size_t>(4));
    REQUIRE(near(stitched.streams.front().timestamps.front(), 1.0));
    REQUIRE(near(stitched.streams.front().timestamps.back(), 4.0));

    const auto bounded = vicon_lsl::applyXdfStreamMapping(
        xdf, analysis.suggested_mapping, 2);
    REQUIRE_EQ(bounded.streams.front().sample_count,
               static_cast<std::size_t>(4));
    REQUIRE(bounded.streams.front().samples.size() <=
            static_cast<std::size_t>(2));
    REQUIRE(bounded.streams.front().stored_sample_stride >=
            static_cast<std::size_t>(2));
    REQUIRE(near(bounded.streams.front().timestamps.back(), 4.0));

    vicon_lsl::XdfLoadResult source_id_collision = xdf;
    source_id_collision.streams.back().hostname = "different-capture-host";
    const auto collision_analysis =
        vicon_lsl::analyzeXdfStreamMapping(source_id_collision);
    REQUIRE(collision_analysis.requires_explicit_mapping);
    REQUIRE_EQ(collision_analysis.candidates.size(),
               static_cast<std::size_t>(2));
}

TEST_CASE("Recorded stream mapping requires an explicit incompatible identity choice") {
    const auto make_gaze = [](std::uint32_t id,
                              const std::string& source,
                              std::size_t count) {
        vicon_lsl::XdfStreamData stream;
        stream.stream_id = id;
        stream.name = "HoloLensGaze";
        stream.type = "Gaze";
        stream.source_id = source;
        stream.hostname = "headset";
        stream.channel_format = "double64";
        stream.coordinate_frame = "shared";
        stream.channel_count = static_cast<int>(
            vicon_lsl::kHoloLensGazeChannelCount);
        stream.channel_labels = gazeLabels();
        stream.role = vicon_lsl::PreviewStreamRole::HoloLensGaze;
        for (std::size_t index = 0; index < count; ++index) {
            stream.timestamps.push_back(10.0 +
                static_cast<double>(index) * 0.01);
            stream.samples.push_back(std::vector<double>(
                vicon_lsl::kHoloLensGazeChannelCount, 0.0));
        }
        stream.sample_count = count;
        stream.start_timestamp = stream.timestamps.front();
        stream.end_timestamp = stream.timestamps.back();
        return stream;
    };
    vicon_lsl::XdfLoadResult xdf;
    xdf.streams.push_back(make_gaze(2, "headset-old", 2));
    xdf.streams.push_back(make_gaze(9, "headset-current", 4));
    const auto analysis = vicon_lsl::analyzeXdfStreamMapping(xdf);
    REQUIRE(analysis.requires_explicit_mapping);
    REQUIRE(analysis.explanation.find("choose") != std::string::npos);
    REQUIRE_EQ(analysis.suggested_mapping.master_stream_id,
               static_cast<std::uint32_t>(9));

    bool required = false;
    try {
        (void)vicon_lsl::applyXdfStreamMapping(xdf, {});
    } catch (const std::runtime_error&) {
        required = true;
    }
    REQUIRE(required);

    vicon_lsl::XdfStreamMapping selected;
    selected.master_stream_id = 2;
    selected.selected_stream_ids = {2};
    const auto mapped = vicon_lsl::applyXdfStreamMapping(xdf, selected);
    REQUIRE_EQ(mapped.streams.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(mapped.streams.front().stream_id,
               static_cast<std::uint32_t>(2));
    REQUIRE_EQ(mapped.streams.front().source_id,
               std::string("headset-old"));

    const auto recording = vicon_lsl::buildXdfPreviewRecording(
        xdf, {}, {}, 0.05, selected);
    REQUIRE(recording.summary.find("mapping master stream 2") !=
            std::string::npos);
    REQUIRE(recording.summary.find(
                "selected gaze stream 2:HoloLensGaze[headset-old]") !=
            std::string::npos);
    REQUIRE(recording.summary.find(
                "excluded gaze stream 9:HoloLensGaze[headset-current]") !=
            std::string::npos);

    selected.selected_stream_ids = {2, 9};
    bool conflicting = false;
    try {
        (void)vicon_lsl::applyXdfStreamMapping(xdf, selected);
    } catch (const std::runtime_error&) {
        conflicting = true;
    }
    REQUIRE(conflicting);
}

TEST_CASE("Preview XDF loader enforces progress cancellation and file bounds") {
    const TemporaryFilePath temporary_path("_bounded.xdf");
    const std::string path = temporary_path.string();
    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, 1, "ViconMarkers", "Markers",
                          {"x", "y", "z", "valid"}, 100.0);
        writeSampleChunk(output, 1, {1.0, 1.01},
                         {{1.0, 2.0, 3.0, 1.0},
                          {2.0, 3.0, 4.0, 1.0}});
    }

    vicon_lsl::PreviewLoadOptions canceled_options;
    int progress_updates = 0;
    canceled_options.progress =
        [&progress_updates](const vicon_lsl::PreviewLoadProgress&) {
            ++progress_updates;
        };
    int cancellation_checks = 0;
    canceled_options.cancel_requested = [&cancellation_checks]() {
        return ++cancellation_checks > 1;
    };
    bool canceled = false;
    const auto cancel_started = std::chrono::steady_clock::now();
    try {
        (void)vicon_lsl::loadXdfNumericStreams(path, canceled_options);
    } catch (const std::runtime_error& error) {
        canceled = std::string(error.what()).find("canceled") !=
                   std::string::npos;
    }
    REQUIRE(canceled);
    REQUIRE(progress_updates > 0);
    REQUIRE(std::chrono::steady_clock::now() - cancel_started <
            std::chrono::milliseconds(250));

    vicon_lsl::PreviewLoadOptions file_limit;
    file_limit.maximum_file_bytes = 3;
    bool bounded = false;
    try {
        (void)vicon_lsl::loadXdfNumericStreams(path, file_limit);
    } catch (const std::runtime_error&) {
        bounded = true;
    }
    REQUIRE(bounded);
}

TEST_CASE("Preview XDF index and playback cache preserve timing under a memory bound") {
    const TemporaryFilePath temporary_path("_memory.xdf");
    const std::string path = temporary_path.string();
    constexpr std::size_t sample_count = 20000;
    std::vector<double> timestamps;
    std::vector<std::vector<double>> samples;
    timestamps.reserve(sample_count);
    samples.reserve(sample_count);
    for (std::size_t index = 0; index < sample_count; ++index) {
        timestamps.push_back(static_cast<double>(index) * 0.01);
        samples.push_back({static_cast<double>(index), 0.0, 0.0, 1.0});
    }
    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, 1, "ViconMarkers", "Markers",
            {"Subject:LASI:X", "Subject:LASI:Y", "Subject:LASI:Z",
             "Subject:LASI:Valid"}, 100.0);
        writeSampleChunk(output, 1, timestamps, samples);
    }

    vicon_lsl::PreviewLoadOptions options;
    options.maximum_preview_frames = sample_count;
    options.maximum_stored_values_per_stream = sample_count * 4;
    options.maximum_memory_bytes = 64 * 1024;
    const auto indexed = vicon_lsl::loadXdfNumericStreams(path, options);
    REQUIRE_EQ(indexed.streams.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(indexed.streams.front().sample_count, sample_count);
    REQUIRE(indexed.streams.front().samples.size() < sample_count);
    REQUIRE(near(indexed.streams.front().start_timestamp, 0.0));
    REQUIRE(near(indexed.streams.front().end_timestamp, 199.99, 1e-6));
    REQUIRE(near(indexed.streams.front().timestamps.back(), 199.99, 1e-6));
    REQUIRE(indexed.estimated_memory_bytes <= options.maximum_memory_bytes);

    vicon_lsl::PreviewTransformProfile vicon;
    vicon.scale = 0.001;
    const auto recording =
        vicon_lsl::loadXdfPreviewRecording(path, vicon, {}, 0.05, options);
    REQUIRE_EQ(recording.source_frame_count, sample_count);
    REQUIRE(recording.frames.size() < sample_count);
    REQUIRE(recording.estimated_memory_bytes <= options.maximum_memory_bytes);
    REQUIRE(near(recording.source_start_timestamp, 0.0));
    REQUIRE(near(recording.source_end_timestamp, 199.99, 1e-6));
    REQUIRE(near(recording.frames.front().timestamp, 0.0));
    REQUIRE(near(recording.frames.back().timestamp, 199.99, 1e-6));
}

TEST_CASE("Preview XDF cache stays bounded for one-hour and multi-hour recordings") {
    struct DurationCase {
        std::size_t samples;
        double interval;
    };
    const DurationCase durations[] = {
        {361, 10.0},
        {1081, 10.0},
    };
    for (const DurationCase duration : durations) {
        const TemporaryFilePath temporary_path("_duration.xdf");
        const std::string path = temporary_path.string();
        std::vector<double> timestamps;
        std::vector<std::vector<double>> samples;
        timestamps.reserve(duration.samples);
        samples.reserve(duration.samples);
        for (std::size_t index = 0; index < duration.samples; ++index) {
            timestamps.push_back(static_cast<double>(index) * duration.interval);
            samples.push_back({static_cast<double>(index), 0.0, 0.0, 1.0});
        }
        {
            std::ofstream output(path, std::ios::binary);
            output << "XDF:";
            writeStreamHeader(output, 1, "ViconMarkers", "Markers",
                {"Subject:LASI:X", "Subject:LASI:Y", "Subject:LASI:Z",
                 "Subject:LASI:Valid"},
                1.0 / duration.interval);
            writeSampleChunk(output, 1, timestamps, samples);
        }

        vicon_lsl::PreviewLoadOptions options;
        options.maximum_preview_frames = 128;
        options.maximum_stored_values_per_stream = 512;
        options.maximum_memory_bytes = 128 * 1024;
        vicon_lsl::PreviewTransformProfile vicon;
        vicon.scale = 0.001;
        const auto recording =
            vicon_lsl::loadXdfPreviewRecording(path, vicon, {}, 0.05, options);
        const double expected_end =
            static_cast<double>(duration.samples - 1) * duration.interval;
        REQUIRE_EQ(recording.source_frame_count, duration.samples);
        REQUIRE(recording.frames.size() <= options.maximum_preview_frames);
        REQUIRE(recording.estimated_memory_bytes <= options.maximum_memory_bytes);
        REQUIRE(near(recording.source_start_timestamp, 0.0));
        REQUIRE(near(recording.source_end_timestamp, expected_end, 1e-6));
        REQUIRE(near(recording.frames.front().timestamp, 0.0));
        REQUIRE(near(recording.frames.back().timestamp, expected_end, 1e-6));
        if (expected_end > 2.0 * 60.0 * 60.0) {
            vicon_lsl::PreviewPlaybackClock clock;
            clock.setFrameTimeline(recording.frames);
            clock.seek(2.0 * 60.0 * 60.0, 0.0);
            const std::size_t index = clock.frameIndex(0.0);
            REQUIRE(index > 0);
            REQUIRE(index + 1 < recording.frames.size());
            REQUIRE(std::abs(recording.frames[index].timestamp -
                             2.0 * 60.0 * 60.0) < 180.0);
        }
    }
}

TEST_CASE("Preview XDF loader rejects configured sample channel and header limits") {
    const TemporaryFilePath temporary_path("_declared_limits.xdf");
    const std::string path = temporary_path.string();
    {
        std::ofstream output(path, std::ios::binary);
        output << "XDF:";
        writeStreamHeader(output, 1, "ViconMarkers", "Markers",
                          {"x", "y", "z", "valid"}, 100.0);
        writeSampleChunk(output, 1, {1.0, 1.01},
                         {{1.0, 2.0, 3.0, 1.0},
                          {2.0, 3.0, 4.0, 1.0}});
    }

    vicon_lsl::PreviewLoadOptions samples;
    samples.maximum_samples_per_stream = 1;
    bool sample_limit = false;
    try {
        (void)vicon_lsl::loadXdfNumericStreams(path, samples);
    } catch (const std::runtime_error& error) {
        sample_limit = std::string(error.what()).find("sample-count") !=
                       std::string::npos;
    }
    REQUIRE(sample_limit);

    vicon_lsl::PreviewLoadOptions channels;
    channels.maximum_channels = 3;
    bool channel_limit = false;
    try {
        (void)vicon_lsl::loadXdfNumericStreams(path, channels);
    } catch (const std::runtime_error& error) {
        channel_limit = std::string(error.what()).find("channel count") !=
                        std::string::npos;
    }
    REQUIRE(channel_limit);

    vicon_lsl::PreviewLoadOptions header;
    header.maximum_header_bytes = 16;
    bool header_limit = false;
    try {
        (void)vicon_lsl::loadXdfNumericStreams(path, header);
    } catch (const std::runtime_error& error) {
        header_limit = std::string(error.what()).find("header") !=
                       std::string::npos;
    }
    REQUIRE(header_limit);
}
