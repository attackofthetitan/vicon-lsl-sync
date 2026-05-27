#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace lsl {
class stream_info;
class stream_outlet;
}

class HoloLensGazeReceiver {
public:
    static constexpr size_t ChannelCount = 21;

    struct Status {
        bool enabled = false;
        bool listening = false;
        unsigned long long sample_count = 0;
        unsigned long long malformed_packet_count = 0;
        std::string last_error;
    };

    using StatusCallback = std::function<void(const Status&)>;

    HoloLensGazeReceiver(unsigned short port,
                         std::string stream_name,
                         std::string stream_type,
                         std::string source_id);
    ~HoloLensGazeReceiver();

    HoloLensGazeReceiver(const HoloLensGazeReceiver&) = delete;
    HoloLensGazeReceiver& operator=(const HoloLensGazeReceiver&) = delete;

    void start();
    void stop();
    Status status() const;
    void setStatusCallback(StatusCallback callback);
    static bool parsePacket(const std::string& packet, std::array<double, ChannelCount>& sample);

private:
    void run();
    void initializeOutlet();
    void reportStatus(const std::string& last_error = "");

    unsigned short port_;
    std::string stream_name_;
    std::string stream_type_;
    std::string source_id_;

    std::atomic<bool> running_{false};
    std::atomic<bool> listening_{false};
    std::atomic<unsigned long long> sample_count_{0};
    std::atomic<unsigned long long> malformed_packet_count_{0};
    std::thread thread_;
    std::unique_ptr<lsl::stream_info> info_;
    std::unique_ptr<lsl::stream_outlet> outlet_;
    StatusCallback status_callback_;
    std::string last_error_;
    mutable std::mutex status_mutex_;
};
