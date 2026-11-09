#pragma once

#include <lsl_cpp.h>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

// Sends samples to LSL, and tests swap in a fake through StreamOutletFactory.
class StreamOutlet {
public:
    virtual ~StreamOutlet() = default;
    virtual void pushSample(const std::vector<double>& sample, double timestamp) = 0;
};

using StreamOutletFactory =
    std::function<std::unique_ptr<StreamOutlet>(const lsl::stream_info&)>;

std::unique_ptr<StreamOutlet> createLslStreamOutlet(const lsl::stream_info& info);

// Closes outlets on its own thread while it exists. liblsl can take seconds to
// close an outlet that a recorder is reading, which would otherwise stop the
// bridge reading Vicon frames (the SDK queue holds about a second at 250 Hz)
// and hold up Stop. Make one at the top of main(), so it outlives every bridge
// and finishes the last closes before liblsl shuts down. Without one, outlets
// close on the thread that lets them go.
class BackgroundOutletCloser {
public:
    // How long the destructor waits for closes still queued.
    static constexpr std::chrono::seconds kExitWait{10};

    BackgroundOutletCloser();
    ~BackgroundOutletCloser();
    BackgroundOutletCloser(const BackgroundOutletCloser&) = delete;
    BackgroundOutletCloser& operator=(const BackgroundOutletCloser&) = delete;

    // True once every outlet handed over has closed, waiting up to timeout.
    bool waitUntilIdle(std::chrono::milliseconds timeout);

    struct State;

private:
    std::shared_ptr<State> state_;
    std::thread worker_;
};

// Hands the outlet to the BackgroundOutletCloser if one exists, or else closes
// it before returning.
void retireStreamOutlet(std::unique_ptr<StreamOutlet> outlet);
