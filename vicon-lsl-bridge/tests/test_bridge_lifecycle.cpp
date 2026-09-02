#include "ViconLSLBridge.h"
#include "ViconLSLBridgeInternal.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        ++failures;
    }
}

class FakeViconClient final : public vicon_lsl::bridge_internal::ViconClient {
public:
    std::shared_ptr<std::vector<std::string>> lifecycle_events;
    bool connect_result = true;
    int available_frames = 0;
    bool expose_marker = false;
    bool expose_segment = false;
    std::function<void(int)> on_get_frame;
    std::function<void()> on_connect;
    int connect_calls = 0;
    int disconnect_calls = 0;
    int get_frame_calls = 0;

    bool connect() override {
        ++connect_calls;
        if (on_connect) on_connect();
        connected_ = connect_result;
        return connected_;
    }

    void disconnect() override {
        ++disconnect_calls;
        if (lifecycle_events) {
            lifecycle_events->push_back("client-disconnected");
        }
        connected_ = false;
    }

    bool isConnected() const override { return connected_; }

    bool getFrame() override {
        ++get_frame_calls;
        if (on_get_frame) {
            on_get_frame(get_frame_calls);
        }
        return get_frame_calls <= available_frames;
    }

    unsigned int frameNumber() const override {
        return static_cast<unsigned int>(get_frame_calls);
    }

    double frameTimestamp() const override {
        return 100.0 + static_cast<double>(get_frame_calls);
    }

    double frameRate() const override { return 120.0; }

    vicon_lsl::CountRead readSubjectCount() const override {
        vicon_lsl::CountRead read;
        read.value = expose_marker ? 1u : 0u;
        return read;
    }

    vicon_lsl::NameRead readSubjectName(unsigned int) const override {
        vicon_lsl::NameRead read;
        read.value = "Subject";
        return read;
    }

    vicon_lsl::CountRead readMarkerCount(const std::string&) const override {
        vicon_lsl::CountRead read;
        read.value = expose_marker ? 1u : 0u;
        return read;
    }

    vicon_lsl::NameRead readMarkerName(const std::string&, unsigned int) const override {
        vicon_lsl::NameRead read;
        read.value = "Marker";
        return read;
    }

    vicon_lsl::MarkerTranslationRead readMarkerGlobalTranslation(
        const std::string&,
        const std::string&) override {
        vicon_lsl::MarkerTranslationRead read;
        read.translation = {1.0, 2.0, 3.0};
        return read;
    }

    vicon_lsl::CountRead readSegmentCount(const std::string&) const override {
        vicon_lsl::CountRead read;
        read.value = expose_segment ? 1u : 0u;
        return read;
    }

    vicon_lsl::NameRead readSegmentName(const std::string&, unsigned int) const override {
        vicon_lsl::NameRead read;
        read.value = "Segment";
        return read;
    }

    vicon_lsl::SegmentTranslationRead readSegmentGlobalTranslation(
        const std::string&,
        const std::string&) override {
        vicon_lsl::SegmentTranslationRead read;
        read.translation = {4.0, 5.0, 6.0};
        return read;
    }

    vicon_lsl::SegmentRotationRead readSegmentGlobalRotationQuaternion(
        const std::string&,
        const std::string&) override {
        vicon_lsl::SegmentRotationRead read;
        read.quaternion = {0.0, 0.0, 0.0, 1.0};
        return read;
    }

private:
    bool connected_ = false;
};

struct OutletState {
    std::shared_ptr<std::vector<std::string>> lifecycle_events;
    int created = 0;
    int pushed = 0;
    bool fail_push = false;
};

class FakeOutlet final : public StreamOutlet {
public:
    FakeOutlet(std::shared_ptr<OutletState> state, std::string stream_name)
        : state_(std::move(state)), stream_name_(std::move(stream_name)) {}

    ~FakeOutlet() override {
        if (state_->lifecycle_events) {
            state_->lifecycle_events->push_back(stream_name_ + "-outlet-destroyed");
        }
    }

    void pushSample(const std::vector<double>&, double) override {
        ++state_->pushed;
        if (state_->fail_push) {
            throw std::runtime_error("injected bridge outlet failure");
        }
    }

private:
    std::shared_ptr<OutletState> state_;
    std::string stream_name_;
};

StreamOutletFactory outletFactory(const std::shared_ptr<OutletState>& state) {
    return [state](const lsl::stream_info& info) {
        ++state->created;
        return std::make_unique<FakeOutlet>(state, info.name());
    };
}

struct StopOnWait {
    ViconLSLBridge* bridge = nullptr;
    int calls = 0;
    std::vector<std::chrono::milliseconds> durations;

    void operator()(std::chrono::milliseconds duration) {
        ++calls;
        durations.push_back(duration);
        bridge->stop();
    }
};

Config testConfig() {
    Config config;
    config.vicon_server = "test-server";
    config.marker_stream_name = "markers";
    config.segment_stream_name = "segments";
    config.reconnect_interval_ms = 250;
    return config;
}

void testInitialFrameFailureReconnectsWithoutDelay() {
    auto client = std::make_shared<FakeViconClient>();
    client->available_frames = 0;
    auto outlets = std::make_shared<OutletState>();
    auto stop_on_wait = std::make_shared<StopOnWait>();

    vicon_lsl::bridge_internal::Collaborators collaborators;
    collaborators.client = client;
    collaborators.outlet_factory = outletFactory(outlets);
    collaborators.clock = [] { return 200.0; };
    collaborators.wait = [stop_on_wait](std::chrono::milliseconds duration) {
        (*stop_on_wait)(duration);
    };
    auto bridge = vicon_lsl::bridge_internal::BridgeTestAccess::create(
        testConfig(), std::move(collaborators));
    stop_on_wait->bridge = bridge.get();

    client->on_get_frame = [client](int call) {
        if (call == 1) {
            client->connect_result = false;
        }
    };
    bridge->run();

    expect(client->get_frame_calls == 1,
           "initial-frame failure performs one frame attempt before reconnecting");
    expect(client->connect_calls == 2,
           "initial-frame failure reconnects immediately");
    expect(client->disconnect_calls == 1,
           "initial-frame failure disconnects the failed session");
    expect(stop_on_wait->calls == 1 &&
               stop_on_wait->durations.front() == std::chrono::milliseconds(100),
           "failed reconnect uses the existing interruptible retry slice");
    expect(outlets->created == 0 && outlets->pushed == 0,
           "initial-frame failure never creates an outlet");
}

// A server that accepts connections but never produces a frame must not turn
// the immediate first retry into an unthrottled connect/getFrame/disconnect
// loop, so every failure after the first waits the reconnect interval.
void testRepeatedInitialFrameFailuresBackOff() {
    auto client = std::make_shared<FakeViconClient>();
    client->available_frames = 0;
    auto outlets = std::make_shared<OutletState>();
    auto stop_on_wait = std::make_shared<StopOnWait>();

    vicon_lsl::bridge_internal::Collaborators collaborators;
    collaborators.client = client;
    collaborators.outlet_factory = outletFactory(outlets);
    collaborators.clock = [] { return 200.0; };
    collaborators.wait = [stop_on_wait](std::chrono::milliseconds duration) {
        (*stop_on_wait)(duration);
    };
    auto bridge = vicon_lsl::bridge_internal::BridgeTestAccess::create(
        testConfig(), std::move(collaborators));
    stop_on_wait->bridge = bridge.get();

    bridge->run();

    expect(client->get_frame_calls == 2,
           "the first initial-frame failure retries at once, the second does not");
    expect(client->connect_calls == 2,
           "a reachable server is reconnected to for the immediate retry");
    expect(client->disconnect_calls == 2,
           "each failed initial-frame session disconnects");
    expect(stop_on_wait->calls == 1,
           "the second consecutive initial-frame failure waits instead of spinning");
    expect(outlets->created == 0,
           "repeated initial-frame failures never create an outlet");
}

void testOutletFailureResetsAStreamingSession() {
    auto client = std::make_shared<FakeViconClient>();
    client->available_frames = 2;
    client->expose_marker = true;
    client->expose_segment = true;
    auto outlets = std::make_shared<OutletState>();
    auto lifecycle_events = std::make_shared<std::vector<std::string>>();
    client->lifecycle_events = lifecycle_events;
    outlets->lifecycle_events = lifecycle_events;
    outlets->fail_push = true;
    auto stop_on_wait = std::make_shared<StopOnWait>();
    std::vector<BridgeStatus> statuses;

    vicon_lsl::bridge_internal::Collaborators collaborators;
    collaborators.client = client;
    collaborators.outlet_factory = outletFactory(outlets);
    collaborators.clock = [] { return 200.0; };
    collaborators.wait = [stop_on_wait](std::chrono::milliseconds duration) {
        (*stop_on_wait)(duration);
    };
    auto bridge = vicon_lsl::bridge_internal::BridgeTestAccess::create(
        testConfig(), std::move(collaborators));
    stop_on_wait->bridge = bridge.get();
    bridge->setStatusCallback([&statuses](const BridgeStatus& status) {
        statuses.push_back(status);
    });
    bridge->run();

    expect(outlets->created == 2 && outlets->pushed == 2,
           "both outlet failures are reached during one streaming frame");
    expect(client->disconnect_calls == 1,
           "outlet failure disconnects the active client");
    expect(*lifecycle_events ==
               std::vector<std::string>({"markers-outlet-destroyed",
                                         "segments-outlet-destroyed",
                                         "client-disconnected"}),
           "outlet failure destroys marker then segment outlets before disconnecting");
    expect(stop_on_wait->calls == 1,
           "outlet failure retains the configured reconnect delay");
    expect(statuses.size() >= 5,
           "outlet failure emits connecting, streaming, reset, and stopped statuses");
    if (statuses.size() >= 5) {
        expect(statuses.front().state == BridgeState::Connecting,
               "bridge starts in Connecting state");
        expect(statuses[1].state == BridgeState::Streaming,
               "initialized bridge reports Streaming");
        expect(statuses[2].state == BridgeState::Connecting,
               "outlet failure reports reconnecting");
        expect(statuses[3].state == BridgeState::Disconnected,
               "streaming-session cleanup reports Disconnected");
        expect(statuses.back().state == BridgeState::Stopped,
               "stopped retry reports Stopped");
    }
}

void testStopDuringFrameReadStillCleansUp() {
    auto client = std::make_shared<FakeViconClient>();
    client->available_frames = 2;
    client->expose_marker = true;
    client->expose_segment = true;
    auto outlets = std::make_shared<OutletState>();
    auto lifecycle_events = std::make_shared<std::vector<std::string>>();
    client->lifecycle_events = lifecycle_events;
    outlets->lifecycle_events = lifecycle_events;

    vicon_lsl::bridge_internal::Collaborators collaborators;
    collaborators.client = client;
    collaborators.outlet_factory = outletFactory(outlets);
    collaborators.clock = [] { return 200.0; };
    collaborators.wait = [](std::chrono::milliseconds) {};
    auto bridge = vicon_lsl::bridge_internal::BridgeTestAccess::create(
        testConfig(), std::move(collaborators));
    client->on_get_frame = [raw_bridge = bridge.get()](int call) {
        if (call == 2) {
            raw_bridge->stop();
        }
    };
    bridge->run();

    expect(outlets->created == 2 && outlets->pushed == 2,
           "a frame already returned during stop reaches both outlets once");
    expect(client->disconnect_calls == 1,
           "stop request disconnects the streaming client");
    expect(*lifecycle_events ==
               std::vector<std::string>({"markers-outlet-destroyed",
                                         "segments-outlet-destroyed",
                                         "client-disconnected"}),
           "stop destroys marker then segment outlets before disconnecting");
}

void testStopDuringConnectionSkipsRetryAndFrameWork() {
    auto client = std::make_shared<FakeViconClient>();
    client->connect_result = false;
    auto outlets = std::make_shared<OutletState>();
    int waits = 0;

    vicon_lsl::bridge_internal::Collaborators collaborators;
    collaborators.client = client;
    collaborators.outlet_factory = outletFactory(outlets);
    collaborators.clock = [] { return 200.0; };
    collaborators.wait = [&waits](std::chrono::milliseconds) { ++waits; };
    auto bridge = vicon_lsl::bridge_internal::BridgeTestAccess::create(
        testConfig(), std::move(collaborators));
    client->on_connect = [raw_bridge = bridge.get()]() { raw_bridge->stop(); };
    bridge->run();

    expect(client->connect_calls == 1 && client->get_frame_calls == 0,
           "stop during connection prevents frame and stream initialization work");
    expect(waits == 0,
           "stop during connection prevents a new reconnect wait");
    expect(outlets->created == 0,
           "stop during connection creates no LSL outlets");
}

void testStopDuringNonCancellableSdkDelayKeepsCallerResponsive() {
    auto client = std::make_shared<FakeViconClient>();
    client->available_frames = 0;
    auto outlets = std::make_shared<OutletState>();
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    client->on_get_frame = [&](int) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&release]() { return release; });
    };

    vicon_lsl::bridge_internal::Collaborators collaborators;
    collaborators.client = client;
    collaborators.outlet_factory = outletFactory(outlets);
    collaborators.clock = [] { return 200.0; };
    collaborators.wait = [](std::chrono::milliseconds) {};
    auto bridge = vicon_lsl::bridge_internal::BridgeTestAccess::create(
        testConfig(), std::move(collaborators));
    std::thread worker([&bridge]() { bridge->run(); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&entered]() { return entered; });
    }
    const auto started = std::chrono::steady_clock::now();
    bridge->stop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - started;
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    worker.join();

    expect(stop_elapsed < std::chrono::milliseconds(50),
           "stop request returns promptly while the SDK call remains isolated on its worker thread");
    expect(client->get_frame_calls == 1 && outlets->created == 0,
           "worker settles after the delayed SDK call without starting a new session");
}

} // namespace

int main() {
    testInitialFrameFailureReconnectsWithoutDelay();
    testRepeatedInitialFrameFailuresBackOff();
    testOutletFailureResetsAStreamingSession();
    testStopDuringFrameReadStillCleansUp();
    testStopDuringConnectionSkipsRetryAndFrameWork();
    testStopDuringNonCancellableSdkDelayKeepsCallerResponsive();
    if (failures != 0) {
        std::cerr << failures << " test failure(s)" << std::endl;
        return 1;
    }
    std::cout << "Bridge lifecycle tests passed" << std::endl;
    return 0;
}
