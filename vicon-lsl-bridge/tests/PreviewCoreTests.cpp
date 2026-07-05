#include "HoloLensGazePacket.h"
#include "preview/ObjMesh.h"
#include "preview/PreviewCsv.h"
#include "preview/PreviewMath.h"
#include "preview/PreviewParsing.h"
#include "preview/PreviewXdf.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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
                            const std::vector<std::string>& labels) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\"?><info>"
        << "<name>" << name << "</name>"
        << "<type>" << type << "</type>"
        << "<channel_count>" << labels.size() << "</channel_count>"
        << "<nominal_srate>0</nominal_srate>"
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
                       const std::vector<std::string>& labels) {
    writeXdfChunk(output, 2, streamHeaderXml(name, type, labels), &stream_id);
}

void writeSampleChunk(std::ostream& output,
                      std::uint32_t stream_id,
                      const std::vector<double>& timestamps,
                      const std::vector<std::vector<double>>& samples) {
    std::ostringstream content;
    writeVarlenInt(content, samples.size());
    for (std::size_t sample_index = 0; sample_index < samples.size(); ++sample_index) {
        writeTimestamp(content, timestamps[sample_index]);
        for (double value : samples[sample_index]) {
            writeLittle(content, value);
        }
    }
    writeXdfChunk(output, 3, content.str(), &stream_id);
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
    std::vector<double> sample(vicon_lsl::HoloLensGazePacket::ChannelCount, 0.0);
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

TEST_CASE("Preview merged CSV loader builds preview frames") {
    const std::string path = "preview_core_test_merged.csv";
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

TEST_CASE("Preview XDF loader decodes numeric streams and merges relative clocks") {
    const std::string path = "preview_core_test.xdf";
    const std::uint32_t marker_stream_id = 1;
    const std::uint32_t gaze_stream_id = 2;
    const std::vector<std::string> marker_labels = {
        "Subject:LASI:X", "Subject:LASI:Y", "Subject:LASI:Z", "Subject:LASI:Valid",
    };
    std::vector<double> gaze_sample(vicon_lsl::HoloLensGazePacket::ChannelCount, 0.0);
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
        writeStreamHeader(output, marker_stream_id, "ViconMarkers", "MoCap", marker_labels);
        writeStreamHeader(output, gaze_stream_id, "HoloLensGaze", "Gaze", gazeLabels());
        writeSampleChunk(output,
                         marker_stream_id,
                         {10.0, 10.1},
                         {{1000.0, 0.0, 0.0, 1.0}, {2000.0, 0.0, 0.0, 1.0}});
        writeSampleChunk(output,
                         gaze_stream_id,
                         {20.0, 20.1},
                         {gaze_sample, gaze_sample});
    }

    vicon_lsl::PreviewTransformProfile vicon_transform;
    vicon_transform.scale = 0.001;
    vicon_lsl::PreviewTransformProfile gaze_transform;
    const auto recording = vicon_lsl::loadXdfPreviewRecording(
        path,
        vicon_transform,
        gaze_transform,
        0.05);

    REQUIRE_EQ(recording.frames.size(), static_cast<std::size_t>(2));
    REQUIRE(recording.summary.find("2 stream(s)") != std::string::npos);
    REQUIRE(near(recording.frames.front().timestamp, 0.0));
    REQUIRE_EQ(recording.frames.front().markers.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(recording.frames.front().gaze_rays.size(), static_cast<std::size_t>(3));
    REQUIRE(recording.frames.front().markers.front().valid);
    REQUIRE(recording.frames.front().gaze_rays.front().valid);
    REQUIRE(near(recording.frames.front().markers.front().position.x, 1.0));
    REQUIRE(near(recording.frames.front().gaze_rays.front().origin.x, 0.25));
    REQUIRE(near(recording.frames[1].markers.front().position.x, 2.0));
}
