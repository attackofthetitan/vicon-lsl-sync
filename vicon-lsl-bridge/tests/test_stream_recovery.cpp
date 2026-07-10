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
};

class FakeOutlet final : public StreamOutlet {
public:
    explicit FakeOutlet(std::shared_ptr<OutletState> state) : state_(std::move(state)) {}

    void pushSample(const std::vector<double>& sample, double) override {
        ++state_->pushed;
        if (state_->fail_push) {
            throw std::runtime_error("injected push failure");
        }
        state_->last_sample = sample;
    }

private:
    std::shared_ptr<OutletState> state_;
};

StreamOutletFactory fakeFactory(const std::shared_ptr<OutletState>& state) {
    return [state](const lsl::stream_info&) {
        ++state->created;
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

} // namespace

int main() {
    testMarkerRecovery();
    testSegmentRecovery();
    testEmptyLayoutsAreHealthy();
    if (failures != 0) {
        std::cerr << failures << " test failure(s)" << std::endl;
        return 1;
    }
    std::cout << "All stream recovery tests passed" << std::endl;
    return 0;
}
