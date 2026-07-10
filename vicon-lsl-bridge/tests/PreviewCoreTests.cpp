#include "HoloLensGazeSchema.h"
#include "preview/ObjMesh.h"
#include "preview/PreviewCsv.h"
#include "preview/PreviewCalibration.h"
#include "preview/PreviewMath.h"
#include "preview/PreviewPlaybackClock.h"
#include "preview/PreviewParsing.h"
#include "preview/PreviewXdf.h"
#include "TestSupport.h"

#include <cmath>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class TemporaryFilePath {
public:
    explicit TemporaryFilePath(const char* suffix) {
        static std::atomic<unsigned long long> sequence{0};
        path_ = std::filesystem::temp_directory_path() /
                ("vicon_lsl_preview_test_" + std::to_string(++sequence) + suffix);
    }

    ~TemporaryFilePath() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    std::string string() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

bool near(double left, double right, double tolerance = 1e-9) {
    return std::abs(left - right) <= tolerance;
}

std::vector<std::string> gazeLabels() {
    std::vector<std::string> labels;
    for (const auto& channel : vicon_lsl::holoLensGazeChannels()) {
        labels.emplace_back(channel.label);
    }
    return labels;
}

std::vector<std::string> calibrationLabels() {
    return {
        "PositionX", "PositionY", "PositionZ",
        "RotationX", "RotationY", "RotationZ", "RotationW", "Tracked",
    };
}

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

TEST_CASE("Preview calibration parser rejects invalid and averages tracked poses") {
    const auto labels = calibrationLabels();
    const auto invalid = vicon_lsl::parseCalibrationTargetPose(
        labels, {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0});
    REQUIRE(!invalid.has_value());

    const auto tracked = vicon_lsl::parseCalibrationTargetPose(
        labels, {1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 2.0, 1.0});
    const auto untracked = vicon_lsl::parseCalibrationTargetPose(
        labels, {9.0, 9.0, 9.0, 0.0, 0.0, 0.0, 1.0, 0.0});
    REQUIRE(tracked.has_value());
    REQUIRE(untracked.has_value());
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

TEST_CASE("Preview playback clock follows recorded timestamps and wraps") {
    vicon_lsl::PreviewPlaybackClock clock;
    clock.setTimeline({0.0, 1.0 / 30.0, 2.0 / 30.0});
    clock.play(10.0);
    REQUIRE_EQ(clock.frameIndex(10.04), static_cast<std::size_t>(1));
    REQUIRE_EQ(clock.frameIndex(10.06), static_cast<std::size_t>(1));
    REQUIRE_EQ(clock.frameIndex(10.07), static_cast<std::size_t>(0));

    clock.setTimeline({5.0, 5.1, 5.7, 6.0});
    clock.play(20.0);
    REQUIRE_EQ(clock.frameIndex(20.65), static_cast<std::size_t>(1));
    REQUIRE_EQ(clock.frameIndex(20.75), static_cast<std::size_t>(2));
}

TEST_CASE("Preview playback clock preserves pause position and speed changes") {
    vicon_lsl::PreviewPlaybackClock clock;
    clock.setTimeline({0.0, 1.0, 2.0, 3.0});
    clock.play(0.0);
    clock.pause(1.2);
    REQUIRE_EQ(clock.frameIndex(100.0), static_cast<std::size_t>(1));

    clock.setSpeed(2.0, 100.0);
    clock.play(100.0);
    REQUIRE_EQ(clock.frameIndex(100.2), static_cast<std::size_t>(1));
    REQUIRE_EQ(clock.frameIndex(100.45), static_cast<std::size_t>(2));
    REQUIRE_EQ(clock.frameIndex(100.95), static_cast<std::size_t>(0));

    bool rejected = false;
    try {
        clock.setTimeline({0.0, 2.0, 1.0});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
}

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

TEST_CASE("Preview XDF loader reconstructs timestamps and applies interpolated clock offsets") {
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

TEST_CASE("Preview XDF loader interpolates changing clock offsets") {
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
        writeClockOffsetChunk(output, stream_id, 20.0, -10.0);
        writeClockOffsetChunk(output, stream_id, 20.2, -10.1);
    }

    const auto xdf = vicon_lsl::loadXdfNumericStreams(path);
    REQUIRE_EQ(xdf.streams.size(), static_cast<std::size_t>(1));
    REQUIRE(near(xdf.streams.front().timestamps[0], 10.0));
    REQUIRE(near(xdf.streams.front().timestamps[1], 10.05));
    REQUIRE(near(xdf.streams.front().timestamps[2], 10.1));
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

TEST_CASE("Preview XDF loader rejects impossible implicit and non-monotonic timestamps") {
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
    bool rejected_non_monotonic = false;
    try {
        (void)vicon_lsl::loadXdfNumericStreams(implicit_path);
    } catch (const std::runtime_error&) {
        rejected_implicit = true;
    }
    try {
        (void)vicon_lsl::loadXdfNumericStreams(non_monotonic_path);
    } catch (const std::runtime_error&) {
        rejected_non_monotonic = true;
    }
    REQUIRE(rejected_implicit);
    REQUIRE(rejected_non_monotonic);
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
