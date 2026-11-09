#include "StreamOutlet.h"

#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace {

class LslStreamOutlet final : public StreamOutlet {
public:
    explicit LslStreamOutlet(const lsl::stream_info& info) : outlet_(info) {}

    void pushSample(const std::vector<double>& sample, double timestamp) override {
        outlet_.push_sample(sample, timestamp);
    }

private:
    lsl::stream_outlet outlet_;
};

// Guards which closer is active, and is held while handing it an outlet, so the
// closer cannot finish while an outlet is on its way to it.
std::mutex g_active_mutex;
std::shared_ptr<BackgroundOutletCloser::State> g_active;

} // namespace

// Shared with the worker thread, so a worker left running after a stuck close
// never touches a destroyed closer.
struct BackgroundOutletCloser::State {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::unique_ptr<StreamOutlet>> queue;
    bool closing = false;
    bool stopping = false;
};

std::unique_ptr<StreamOutlet> createLslStreamOutlet(const lsl::stream_info& info) {
    return std::make_unique<LslStreamOutlet>(info);
}

BackgroundOutletCloser::BackgroundOutletCloser() : state_(std::make_shared<State>()) {
    {
        std::lock_guard<std::mutex> lock(g_active_mutex);
        if (g_active) {
            throw std::logic_error("Only one BackgroundOutletCloser may exist at a time");
        }
        g_active = state_;
    }
    worker_ = std::thread([state = state_]() {
        std::unique_lock<std::mutex> lock(state->mutex);
        for (;;) {
            state->changed.wait(lock, [&state] { return state->stopping || !state->queue.empty(); });
            if (state->queue.empty()) return;
            std::unique_ptr<StreamOutlet> outlet = std::move(state->queue.front());
            state->queue.pop_front();
            state->closing = true;
            lock.unlock();
            outlet.reset();
            lock.lock();
            state->closing = false;
            state->changed.notify_all();
        }
    });
}

BackgroundOutletCloser::~BackgroundOutletCloser() {
    {
        std::lock_guard<std::mutex> lock(g_active_mutex);
        if (g_active == state_) g_active.reset();
    }
    // Nothing new can arrive now, so finish what is queued.
    const bool idle = waitUntilIdle(kExitWait);
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stopping = true;
    }
    state_->changed.notify_all();
    if (idle) {
        worker_.join();
    } else {
        std::cerr << "An LSL stream is still closing after "
                  << kExitWait.count() << " s; exiting without waiting for it" << std::endl;
        worker_.detach();
    }
}

bool BackgroundOutletCloser::waitUntilIdle(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(state_->mutex);
    return state_->changed.wait_for(lock, timeout, [this] {
        return state_->queue.empty() && !state_->closing;
    });
}

void retireStreamOutlet(std::unique_ptr<StreamOutlet> outlet) {
    if (!outlet) return;
    {
        std::lock_guard<std::mutex> active_lock(g_active_mutex);
        if (g_active) {
            {
                std::lock_guard<std::mutex> lock(g_active->mutex);
                g_active->queue.push_back(std::move(outlet));
            }
            g_active->changed.notify_all();
            return;
        }
    }
    outlet.reset();
}
