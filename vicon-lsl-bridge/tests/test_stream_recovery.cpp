#include "MarkerStream.h"
#include "SegmentStream.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        ++failures;
    }
}

struct OutletState {
    int created = 0;
    int pushed = 0;
    bool fail_push = false;
    std::vector<double> last_sample;
    std::vector<double> timestamps;
    std::vector<double> nominal_rates;
    std::vector<std::string> stream_xml;
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
    return [state](const lsl::stream_info& info) {
        ++state->created;
        state->nominal_rates.push_back(info.nominal_srate());
        state->stream_xml.push_back(info.as_xml());
        return std::make_unique<FakeOutlet>(state);
    };
}

void testMarkerRecovery() {
    auto state = std::make_shared<OutletState>();
    MarkerStream stream(fakeFactory(state));
    stream.initialize({{"Subject", "Marker"}}, "markers", "marker_source");
    expect(stream.isInitialized(), "marker stream initializes through injected outlet");

    vicon_lsl::MarkerObjectRead marker;
    marker.value.translation = {1000.0, 2000.0, 3000.0};
    expect(stream.pushSample({marker}, 1.0) == StreamPushResult::Pushed,
           "marker push succeeds");
    expect(state->last_sample.size() == 4, "marker push preserves channel shape");

    state->fail_push = true;
    expect(stream.pushSample({marker}, 2.0) == StreamPushResult::Failed,
           "marker push failure is reported");
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
    stream.initialize({{"Subject", "Segment"}}, "segments", "segment_source");
    expect(stream.isInitialized(), "segment stream initializes through injected outlet");

    vicon_lsl::SegmentObjectRead segment;
    segment.translation.translation = {1000.0, 2000.0, 3000.0};
    segment.rotation.quaternion = {0.0, 0.0, 0.0, 1.0};
    expect(stream.pushSample({segment}, 1.0) == StreamPushResult::Pushed,
           "segment push succeeds");
    expect(state->last_sample.size() == 7, "segment push preserves channel shape");

    state->fail_push = true;
    expect(stream.pushSample({segment}, 2.0) == StreamPushResult::Failed,
           "segment push failure is reported");
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
    markers.initialize({}, "markers", "marker_source");
    segments.initialize({}, "segments", "segment_source");

    expect(markers.isInitialized(), "empty marker layout is configured");
    expect(segments.isInitialized(), "empty segment layout is configured");
    expect(markers.pushSample({}, 1.0) == StreamPushResult::Pushed,
           "empty marker layout remains healthy");
    expect(segments.pushSample({}, 1.0) == StreamPushResult::Pushed,
           "empty segment layout remains healthy");
    expect(state->created == 0, "empty layouts do not construct outlets");
}

void testMarkerAndSegmentTimestampPropagation() {
    auto state = std::make_shared<OutletState>();
    MarkerStream markers(fakeFactory(state));
    SegmentStream segments(fakeFactory(state));
    markers.initialize({{"Subject", "Marker"}}, "markers", "marker_source", 120.0);
    segments.initialize({{"Subject", "Segment"}}, "segments", "segment_source", 120.0);

    vicon_lsl::MarkerObjectRead marker;
    vicon_lsl::SegmentObjectRead segment;
    constexpr double frame_timestamp = 42.25;
    expect(markers.pushSample({marker}, frame_timestamp) == StreamPushResult::Pushed,
           "marker outlet accepts the frame timestamp");
    expect(segments.pushSample({segment}, frame_timestamp) == StreamPushResult::Pushed,
           "segment outlet accepts the frame timestamp");
    expect(state->timestamps.size() == 2,
           "marker and segment outlets both receive a timestamp");
    if (state->timestamps.size() == 2) {
        expect(state->timestamps[0] == state->timestamps[1],
               "marker and segment outlets share the identical frame timestamp");
    }
    expect(state->nominal_rates.size() == 2 &&
               state->nominal_rates[0] == 120.0 && state->nominal_rates[1] == 120.0,
           "marker and segment metadata publish the SDK frame rate");
    expect(state->stream_xml.size() == 2 &&
               state->stream_xml[0].find("<timestamp_origin>") != std::string::npos &&
               state->stream_xml[1].find("<timestamp_origin>") != std::string::npos,
           "marker and segment headers describe timestamp synchronization");
}

} // namespace

int main() {
    testMarkerRecovery();
    testSegmentRecovery();
    testEmptyLayoutsAreHealthy();
    testMarkerAndSegmentTimestampPropagation();
    if (failures != 0) {
        std::cerr << failures << " test failure(s)" << std::endl;
        return 1;
    }
    std::cout << "All stream recovery tests passed" << std::endl;
    return 0;
}
