#include "preview/PreviewFrameAssembler.h"
#include "PreviewCoreTestSupport.h"
#include "TestSupport.h"

#include <string>
#include <vector>

namespace {

const std::vector<std::string> kMarkerLabels = {
    "Subject:Marker:X", "Subject:Marker:Y", "Subject:Marker:Z", "Subject:Marker:Valid",
};
const std::vector<double> kMarkerSample = {1000.0, 2000.0, 3000.0, 1.0};

const std::vector<std::string> kSegmentLabels = {
    "Subject:Root:X", "Subject:Root:Y", "Subject:Root:Z",
    "Subject:Root:QX", "Subject:Root:QY", "Subject:Root:QZ", "Subject:Root:QW",
};
const std::vector<double> kSegmentSample = {1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0};

std::vector<double> gazeSample() {
    std::vector<double> sample(vicon_lsl::kHoloLensGazeChannelCount, 0.0);
    sample[0] = 1.0;
    sample[1] = 2.0;
    sample[2] = 3.0;
    sample[5] = 1.0;
    sample[6] = 1.0;
    return sample;
}

} // namespace

TEST_CASE("Live preview assembly keeps marker updates as the frame clock") {
    vicon_lsl::PreviewTransformProfile marker_transform;
    marker_transform.scale = 0.001;
    const vicon_lsl::PreviewTransformProfile identity;
    const auto gaze_labels = preview_core_test_support::gazeLabels();
    const auto gaze_sample = gazeSample();

    const vicon_lsl::PreviewStreamSnapshot markers{
        kMarkerLabels, kMarkerSample, marker_transform, 10.0, true, true, true};
    const vicon_lsl::PreviewStreamSnapshot segments{
        kSegmentLabels, kSegmentSample, identity, 10.04, true, true, false};
    const vicon_lsl::PreviewStreamSnapshot gaze{
        gaze_labels, gaze_sample, identity, 10.02, true, true, false};

    const auto frame = vicon_lsl::assemblePreviewFrame({markers, segments, gaze, 0.05});
    REQUIRE(frame.has_value());
    REQUIRE(preview_core_test_support::near(frame->timestamp, 10.0));
    REQUIRE_EQ(frame->markers.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(frame->segments.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(frame->gaze_rays.size(), static_cast<std::size_t>(3));
    REQUIRE(preview_core_test_support::near(frame->markers.front().position.x, 1.0));
    REQUIRE(frame->marker_stream_present);
    REQUIRE(frame->segment_stream_present);
    REQUIRE(frame->gaze_stream_present);
}

TEST_CASE("Live preview fallback uses the newest fresh updated companion") {
    const vicon_lsl::PreviewTransformProfile identity;
    const auto gaze_labels = preview_core_test_support::gazeLabels();
    const auto gaze_sample = gazeSample();
    const std::vector<std::string> no_labels;
    const std::vector<double> no_sample;

    const vicon_lsl::PreviewStreamSnapshot markers{
        no_labels, no_sample, identity, 0.0, true, false, false};
    const vicon_lsl::PreviewStreamSnapshot segments{
        kSegmentLabels, kSegmentSample, identity, 20.0, true, true, true};
    const vicon_lsl::PreviewStreamSnapshot gaze{
        gaze_labels, gaze_sample, identity, 20.03, true, true, true};

    const auto frame = vicon_lsl::assemblePreviewFrame({markers, segments, gaze, 0.05});
    REQUIRE(frame.has_value());
    REQUIRE(preview_core_test_support::near(frame->timestamp, 20.03));
    REQUIRE(frame->markers.empty());
    REQUIRE_EQ(frame->segments.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(frame->gaze_rays.size(), static_cast<std::size_t>(3));
    REQUIRE(frame->marker_stream_present);
}

TEST_CASE("Live preview fallback rejects stale updated streams") {
    const vicon_lsl::PreviewTransformProfile identity;
    const std::vector<std::string> no_labels;
    const std::vector<double> no_sample;
    const vicon_lsl::PreviewStreamSnapshot empty{
        no_labels, no_sample, identity, 0.0, false, false, false};
    const vicon_lsl::PreviewStreamSnapshot stale_segment{
        kSegmentLabels, kSegmentSample, identity, 30.0, true, false, true};

    REQUIRE(!vicon_lsl::assemblePreviewFrame({empty, stale_segment, empty, 0.05}).has_value());
}

TEST_CASE("Live preview presence flags do not imply timestamp-matched content") {
    const vicon_lsl::PreviewTransformProfile identity;
    const auto gaze_labels = preview_core_test_support::gazeLabels();
    const auto gaze_sample = gazeSample();

    const vicon_lsl::PreviewStreamSnapshot markers{
        kMarkerLabels, kMarkerSample, identity, 40.0, true, false, true};
    const vicon_lsl::PreviewStreamSnapshot segments{
        kSegmentLabels, kSegmentSample, identity, 39.0, true, true, false};
    const vicon_lsl::PreviewStreamSnapshot gaze{
        gaze_labels, gaze_sample, identity, 40.0, true, false, false};

    const auto frame = vicon_lsl::assemblePreviewFrame({markers, segments, gaze, 0.05});
    REQUIRE(frame.has_value());
    REQUIRE_EQ(frame->markers.size(), static_cast<std::size_t>(1));
    REQUIRE(frame->segments.empty());
    REQUIRE(frame->gaze_rays.empty());
    REQUIRE(frame->marker_stream_present);
    REQUIRE(frame->segment_stream_present);
    REQUIRE(frame->gaze_stream_present);
}
