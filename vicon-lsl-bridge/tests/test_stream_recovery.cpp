#include "MarkerStream.h"
#include "SegmentStream.h"

#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        ++failures;
    }
}

class ScopedStreamRedirect {
public:
    ScopedStreamRedirect(std::ostream& stream, std::ostringstream& capture)
        : stream_(stream), previous_(stream.rdbuf(capture.rdbuf())) {}

    ~ScopedStreamRedirect() {
        stream_.rdbuf(previous_);
    }

private:
    std::ostream& stream_;
    std::streambuf* previous_;
};

using MetadataFields = std::vector<std::pair<std::string, std::string>>;

MetadataFields metadataFields(lsl::xml_element parent) {
    MetadataFields fields;
    for (lsl::xml_element child = parent.first_child();
         !child.empty();
         child = child.next_sibling()) {
        fields.emplace_back(child.name(), child.child_value());
    }
    return fields;
}

struct CapturedStreamInfo {
    std::string name;
    std::string type;
    int channel_count = 0;
    double nominal_rate = 0.0;
    lsl::channel_format_t channel_format = lsl::cf_undefined;
    std::string source_id;
    MetadataFields channels;
    MetadataFields acquisition;
    MetadataFields synchronization;
};

CapturedStreamInfo captureStreamInfo(const lsl::stream_info& info) {
    CapturedStreamInfo captured;
    captured.name = info.name();
    captured.type = info.type();
    captured.channel_count = info.channel_count();
    captured.nominal_rate = info.nominal_srate();
    captured.channel_format = info.channel_format();
    captured.source_id = info.source_id();

    lsl::stream_info mutable_info = info;
    const lsl::xml_element description = mutable_info.desc();
    for (lsl::xml_element channel = description.child("channels").child("channel");
         !channel.empty();
         channel = channel.next_sibling("channel")) {
        captured.channels.emplace_back(channel.child_value("label"),
                                       channel.child_value("unit"));
    }
    captured.acquisition = metadataFields(description.child("acquisition"));
    captured.synchronization = metadataFields(description.child("synchronization"));
    return captured;
}

struct OutletState {
    int created = 0;
    int pushed = 0;
    bool fail_push = false;
    bool return_null = false;
    std::vector<double> last_sample;
    std::vector<double> timestamps;
    std::vector<CapturedStreamInfo> streams;
};

class FakeOutlet final : public StreamOutlet {
public:
    explicit FakeOutlet(std::shared_ptr<OutletState> state) : state_(std::move(state)) {}

    void pushSample(const std::vector<double>& sample, double timestamp) override {
        ++state_->pushed;
        if (state_->fail_push) {
            throw std::runtime_error("injected push failure");
        }
        state_->last_sample = sample;
        state_->timestamps.push_back(timestamp);
    }

private:
    std::shared_ptr<OutletState> state_;
};

StreamOutletFactory fakeFactory(const std::shared_ptr<OutletState>& state) {
    return [state](const lsl::stream_info& info) -> std::unique_ptr<StreamOutlet> {
        ++state->created;
        state->streams.push_back(captureStreamInfo(info));
        if (state->return_null) {
            return nullptr;
        }
        return std::make_unique<FakeOutlet>(state);
    };
}

template <typename Action>
void expectException(Action&& action,
                     const std::string& expected_message,
                     const std::string& description) {
    try {
        action();
        expect(false, description + " throws");
    } catch (const std::exception& ex) {
        expect(ex.what() == expected_message,
               description + " preserves its exact exception message");
    } catch (...) {
        expect(false, description + " throws a standard exception");
    }
}

vicon_lsl::MarkerObjectRead markerRead() {
    vicon_lsl::MarkerObjectRead marker;
    marker.value.translation = {1000.0, 2000.0, 3000.0};
    return marker;
}

vicon_lsl::SegmentObjectRead segmentRead() {
    vicon_lsl::SegmentObjectRead segment;
    segment.translation.translation = {1000.0, 2000.0, 3000.0};
    segment.rotation.quaternion = {0.1, 0.2, 0.3, 0.9};
    return segment;
}

const MetadataFields& expectedIrregularAcquisitionMetadata() {
    static const MetadataFields fields{
        {"device", "Vicon"},
        {"sdk", "ViconDataStreamSDK"},
        {"nominal_srate", "0.000000"},
        {"timestamp", "estimated_acquisition_time"},
        {"clock_domain", "lsl_local_clock"},
        {"timestamp_estimator", "immediate_receipt_minus_valid_pipeline_latency"},
        {"timestamp_fallback", "immediate_receipt_time"},
        {"latency_correction", "GetLatencyTotal_pipeline_estimate"},
        {"timestamp_accuracy", "acquisition_estimate_not_capture_accurate"},
    };
    return fields;
}

const MetadataFields& expectedSynchronizationMetadata() {
    static const MetadataFields fields{
        {"clock_domain", "lsl_local_clock"},
        {"timestamp_origin", "local_receipt_minus_valid_vicon_pipeline_latency"},
        {"offset_mean", "0"},
        {"can_drop_samples", "true"},
    };
    return fields;
}

void testNotConfiguredAndNullFactories() {
    auto unused_state = std::make_shared<OutletState>();
    MarkerStream markers(fakeFactory(unused_state));
    SegmentStream segments(fakeFactory(unused_state));

    std::ostringstream output;
    std::ostringstream errors;
    {
        ScopedStreamRedirect capture_output(std::cout, output);
        ScopedStreamRedirect capture_errors(std::cerr, errors);
        expect(markers.pushSample({markerRead()}, 1.0) == StreamPushResult::NotConfigured,
               "unconfigured marker push reports NotConfigured");
        expect(segments.pushSample({segmentRead()}, 1.0) == StreamPushResult::NotConfigured,
               "unconfigured segment push reports NotConfigured");
    }
    expect(unused_state->created == 0 && unused_state->pushed == 0,
           "unconfigured pushes do not create or call an outlet");
    expect(output.str().empty() && errors.str().empty(),
           "unconfigured pushes do not log");

    auto marker_state = std::make_shared<OutletState>();
    marker_state->return_null = true;
    MarkerStream null_markers(fakeFactory(marker_state));
    expectException(
        [&] {
            null_markers.initialize({{"Subject", "Marker"}},
                                    "markers",
                                    "marker_source");
        },
        "Marker outlet factory returned no outlet",
        "null marker outlet factory");
    expect(!null_markers.isInitialized(),
           "null marker outlet leaves the stream unavailable");
    expect(marker_state->created == 1,
           "marker outlet factory is called exactly once before null rejection");

    auto segment_state = std::make_shared<OutletState>();
    segment_state->return_null = true;
    SegmentStream null_segments(fakeFactory(segment_state));
    expectException(
        [&] {
            null_segments.initialize({{"Subject", "Segment"}},
                                     "segments",
                                     "segment_source");
        },
        "Segment outlet factory returned no outlet",
        "null segment outlet factory");
    expect(!null_segments.isInitialized(),
           "null segment outlet leaves the stream unavailable");
    expect(segment_state->created == 1,
           "segment outlet factory is called exactly once before null rejection");
}

void testInvalidNominalRatesAndMetadata() {
    auto marker_state = std::make_shared<OutletState>();
    auto segment_state = std::make_shared<OutletState>();
    MarkerStream markers(fakeFactory(marker_state));
    SegmentStream segments(fakeFactory(segment_state));

    markers.initialize({{"Subject", "MarkerA"}, {"Subject", "MarkerB"}},
                       "marker-stream",
                       "marker-source-id",
                       std::numeric_limits<double>::quiet_NaN());
    segments.initialize({{"Subject", "Segment"}},
                        "segment-stream",
                        "segment-source-id",
                        -120.0);

    expect(marker_state->streams.size() == 1 && segment_state->streams.size() == 1,
           "both stream factories capture one stream description");
    if (marker_state->streams.size() != 1 || segment_state->streams.size() != 1) {
        return;
    }

    const CapturedStreamInfo& marker = marker_state->streams.front();
    const CapturedStreamInfo& segment = segment_state->streams.front();
    expect(marker.name == "marker-stream" && segment.name == "segment-stream",
           "stream names are forwarded exactly");
    expect(marker.type == "MoCap" && segment.type == "MoCap",
           "both streams retain the MoCap content type");
    expect(marker.source_id == "marker-source-id" &&
               segment.source_id == "segment-source-id",
           "source IDs are forwarded exactly");
    expect(marker.channel_count == 8 && segment.channel_count == 7,
           "stream channel counts retain their marker and segment widths");
    expect(marker.channel_format == lsl::cf_double64 &&
               segment.channel_format == lsl::cf_double64,
           "both streams retain double64 wire samples");
    expect(marker.nominal_rate == lsl::IRREGULAR_RATE &&
               segment.nominal_rate == lsl::IRREGULAR_RATE,
           "non-finite and non-positive nominal rates fall back to irregular");

    const MetadataFields expected_marker_channels{
        {"Subject:MarkerA:X", "mm"},
        {"Subject:MarkerA:Y", "mm"},
        {"Subject:MarkerA:Z", "mm"},
        {"Subject:MarkerA:Valid", "bool"},
        {"Subject:MarkerB:X", "mm"},
        {"Subject:MarkerB:Y", "mm"},
        {"Subject:MarkerB:Z", "mm"},
        {"Subject:MarkerB:Valid", "bool"},
    };
    const MetadataFields expected_segment_channels{
        {"Subject:Segment:X", "mm"},
        {"Subject:Segment:Y", "mm"},
        {"Subject:Segment:Z", "mm"},
        {"Subject:Segment:QX", "quaternion"},
        {"Subject:Segment:QY", "quaternion"},
        {"Subject:Segment:QZ", "quaternion"},
        {"Subject:Segment:QW", "quaternion"},
    };
    expect(marker.channels == expected_marker_channels,
           "marker channel labels, units, and order remain exact");
    expect(segment.channels == expected_segment_channels,
           "segment channel labels, units, and order remain exact");
    expect(marker.acquisition == expectedIrregularAcquisitionMetadata() &&
               segment.acquisition == expectedIrregularAcquisitionMetadata(),
           "marker and segment acquisition metadata remain exact and identical");
    expect(marker.synchronization == expectedSynchronizationMetadata() &&
               segment.synchronization == expectedSynchronizationMetadata(),
           "marker and segment synchronization metadata remain exact and identical");
}

void testWrongShapeDoesNotDestroyStreams() {
    auto marker_state = std::make_shared<OutletState>();
    MarkerStream markers(fakeFactory(marker_state));
    markers.initialize({{"Subject", "Marker"}}, "markers", "marker_source");

    std::ostringstream marker_errors;
    StreamPushResult marker_result = StreamPushResult::Pushed;
    {
        ScopedStreamRedirect capture_errors(std::cerr, marker_errors);
        marker_result = markers.pushSample({}, 1.0);
    }
    expect(marker_result == StreamPushResult::Failed,
           "marker channel mismatch is reported as Failed");
    expect(marker_errors.str() ==
               "Marker sample channel mismatch: expected 4, got 0\n",
           "marker channel mismatch preserves its exact diagnostic");
    expect(markers.isInitialized() && marker_state->pushed == 0,
           "marker channel mismatch does not call or destroy the outlet");
    expect(markers.pushSample({markerRead()}, 2.0) == StreamPushResult::Pushed,
           "marker stream remains usable after a channel mismatch");

    auto segment_state = std::make_shared<OutletState>();
    SegmentStream segments(fakeFactory(segment_state));
    segments.initialize({{"Subject", "Segment"}}, "segments", "segment_source");

    std::ostringstream segment_errors;
    StreamPushResult segment_result = StreamPushResult::Pushed;
    {
        ScopedStreamRedirect capture_errors(std::cerr, segment_errors);
        segment_result = segments.pushSample({segmentRead(), segmentRead()}, 1.0);
    }
    expect(segment_result == StreamPushResult::Failed,
           "segment channel mismatch is reported as Failed");
    expect(segment_errors.str() ==
               "Segment sample channel mismatch: expected 7, got 14\n",
           "segment channel mismatch preserves its exact diagnostic");
    expect(segments.isInitialized() && segment_state->pushed == 0,
           "segment channel mismatch does not call or destroy the outlet");
    expect(segments.pushSample({segmentRead()}, 2.0) == StreamPushResult::Pushed,
           "segment stream remains usable after a channel mismatch");
}

void testMarkerRecovery() {
    auto state = std::make_shared<OutletState>();
    MarkerStream stream(fakeFactory(state));

    std::ostringstream ready_output;
    {
        ScopedStreamRedirect capture_output(std::cout, ready_output);
        stream.initialize({{"Subject", "Marker"}}, "markers", "marker_source");
    }
    expect(ready_output.str() == "Marker stream ready, 1 markers, 4 channels\n",
           "marker initialization preserves its exact log");
    expect(stream.isInitialized(), "marker stream initializes through injected outlet");

    const vicon_lsl::MarkerObjectRead marker = markerRead();
    expect(stream.pushSample({marker}, 1.0) == StreamPushResult::Pushed,
           "marker push succeeds");
    expect(state->last_sample == std::vector<double>({1000.0, 2000.0, 3000.0, 1.0}),
           "marker push preserves wire values and channel order");

    state->fail_push = true;
    std::ostringstream failure_output;
    std::ostringstream failure_errors;
    StreamPushResult failure_result = StreamPushResult::Pushed;
    {
        ScopedStreamRedirect capture_output(std::cout, failure_output);
        ScopedStreamRedirect capture_errors(std::cerr, failure_errors);
        failure_result = stream.pushSample({marker}, 2.0);
    }
    expect(failure_result == StreamPushResult::Failed,
           "marker push failure is reported");
    expect(failure_errors.str() ==
               "Failed to push marker LSL sample: injected push failure\n",
           "marker push failure preserves its exact diagnostic");
    expect(failure_output.str() == "Marker stream closed\n",
           "marker push failure preserves its close log");
    expect(!stream.isInitialized(), "marker outlet is destroyed after push failure");

    state->fail_push = false;
    stream.initialize({{"Subject", "Marker"}}, "markers", "marker_source");
    expect(state->created == 2, "marker outlet is recreated after failure");
    expect(stream.pushSample({marker}, 3.0) == StreamPushResult::Pushed,
           "recreated marker outlet publishes");
}

void testSegmentRecovery() {
    auto state = std::make_shared<OutletState>();
    SegmentStream stream(fakeFactory(state));

    std::ostringstream ready_output;
    {
        ScopedStreamRedirect capture_output(std::cout, ready_output);
        stream.initialize({{"Subject", "Segment"}}, "segments", "segment_source");
    }
    expect(ready_output.str() == "Segment stream ready, 1 segments, 7 channels\n",
           "segment initialization preserves its exact log");
    expect(stream.isInitialized(), "segment stream initializes through injected outlet");

    const vicon_lsl::SegmentObjectRead segment = segmentRead();
    expect(stream.pushSample({segment}, 1.0) == StreamPushResult::Pushed,
           "segment push succeeds");
    expect(state->last_sample ==
               std::vector<double>({1000.0, 2000.0, 3000.0, 0.1, 0.2, 0.3, 0.9}),
           "segment push preserves wire values and channel order");

    state->fail_push = true;
    std::ostringstream failure_output;
    std::ostringstream failure_errors;
    StreamPushResult failure_result = StreamPushResult::Pushed;
    {
        ScopedStreamRedirect capture_output(std::cout, failure_output);
        ScopedStreamRedirect capture_errors(std::cerr, failure_errors);
        failure_result = stream.pushSample({segment}, 2.0);
    }
    expect(failure_result == StreamPushResult::Failed,
           "segment push failure is reported");
    expect(failure_errors.str() ==
               "Failed to push segment LSL sample: injected push failure\n",
           "segment push failure preserves its exact diagnostic");
    expect(failure_output.str() == "Segment stream closed\n",
           "segment push failure preserves its close log");
    expect(!stream.isInitialized(), "segment outlet is destroyed after push failure");

    state->fail_push = false;
    stream.initialize({{"Subject", "Segment"}}, "segments", "segment_source");
    expect(state->created == 2, "segment outlet is recreated after failure");
    expect(stream.pushSample({segment}, 3.0) == StreamPushResult::Pushed,
           "recreated segment outlet publishes");
}

void testEmptyLayoutsAreHealthy() {
    auto state = std::make_shared<OutletState>();
    MarkerStream markers(fakeFactory(state));
    SegmentStream segments(fakeFactory(state));

    std::ostringstream output;
    {
        ScopedStreamRedirect capture_output(std::cout, output);
        markers.initialize({}, "markers", "marker_source");
        segments.initialize({}, "segments", "segment_source");
    }

    expect(output.str() ==
               "No markers discovered; marker stream not created\n"
               "No segments discovered; segment stream not created\n",
           "empty layouts preserve their exact diagnostics");
    expect(markers.isInitialized(), "empty marker layout is configured");
    expect(segments.isInitialized(), "empty segment layout is configured");
    expect(markers.pushSample({markerRead()}, 1.0) == StreamPushResult::Pushed,
           "empty marker layout remains healthy without inspecting input shape");
    expect(segments.pushSample({segmentRead()}, 1.0) == StreamPushResult::Pushed,
           "empty segment layout remains healthy without inspecting input shape");
    expect(state->created == 0 && state->pushed == 0,
           "empty layouts do not construct or call outlets");
}

void testReinitializeClosesBeforeCreatingReplacement() {
    auto marker_state = std::make_shared<OutletState>();
    MarkerStream markers(fakeFactory(marker_state));
    markers.initialize({{"Subject", "Marker"}}, "markers", "marker_source");
    std::ostringstream marker_output;
    {
        ScopedStreamRedirect capture_output(std::cout, marker_output);
        markers.initialize({{"Subject", "Marker"}}, "markers", "marker_source");
    }
    expect(marker_output.str() ==
               "Marker stream closed\n"
               "Marker stream ready, 1 markers, 4 channels\n",
           "marker reinitialization preserves close-before-ready logging");

    auto segment_state = std::make_shared<OutletState>();
    SegmentStream segments(fakeFactory(segment_state));
    segments.initialize({{"Subject", "Segment"}}, "segments", "segment_source");
    std::ostringstream segment_output;
    {
        ScopedStreamRedirect capture_output(std::cout, segment_output);
        segments.initialize({{"Subject", "Segment"}}, "segments", "segment_source");
    }
    expect(segment_output.str() ==
               "Segment stream closed\n"
               "Segment stream ready, 1 segments, 7 channels\n",
           "segment reinitialization preserves close-before-ready logging");
}

void testMarkerAndSegmentTimestampPropagation() {
    auto state = std::make_shared<OutletState>();
    MarkerStream markers(fakeFactory(state));
    SegmentStream segments(fakeFactory(state));
    markers.initialize({{"Subject", "Marker"}}, "markers", "marker_source", 120.0);
    segments.initialize({{"Subject", "Segment"}}, "segments", "segment_source", 120.0);

    constexpr double frame_timestamp = 42.25;
    expect(markers.pushSample({markerRead()}, frame_timestamp) == StreamPushResult::Pushed,
           "marker outlet accepts the frame timestamp");
    expect(segments.pushSample({segmentRead()}, frame_timestamp) == StreamPushResult::Pushed,
           "segment outlet accepts the frame timestamp");
    expect(state->timestamps == std::vector<double>({frame_timestamp, frame_timestamp}),
           "marker and segment outlets receive identical frame timestamps");
    expect(state->streams.size() == 2 &&
               state->streams[0].nominal_rate == 120.0 &&
               state->streams[1].nominal_rate == 120.0,
           "marker and segment metadata publish the SDK frame rate");
    if (state->streams.size() == 2) {
        MetadataFields expected_acquisition = expectedIrregularAcquisitionMetadata();
        expected_acquisition[2].second = "120.000000";
        expect(state->streams[0].acquisition == expected_acquisition &&
                   state->streams[1].acquisition == expected_acquisition,
               "valid nominal rates are serialized identically in both metadata headers");
    }
}

} // namespace

int main() {
    testNotConfiguredAndNullFactories();
    testInvalidNominalRatesAndMetadata();
    testWrongShapeDoesNotDestroyStreams();
    testMarkerRecovery();
    testSegmentRecovery();
    testEmptyLayoutsAreHealthy();
    testReinitializeClosesBeforeCreatingReplacement();
    testMarkerAndSegmentTimestampPropagation();
    if (failures != 0) {
        std::cerr << failures << " test failure(s)" << std::endl;
        return 1;
    }
    std::cout << "All stream recovery tests passed" << std::endl;
    return 0;
}
