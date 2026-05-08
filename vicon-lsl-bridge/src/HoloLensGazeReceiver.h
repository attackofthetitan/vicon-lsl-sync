#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace lsl {
class stream_info;
class stream_outlet;
}

class HoloLensGazeReceiver {
public:
    static constexpr size_t ChannelCount = 26;

    HoloLensGazeReceiver(unsigned short port,
                         std::string stream_name,
                         std::string stream_type,
                         std::string source_id);
    ~HoloLensGazeReceiver();

    HoloLensGazeReceiver(const HoloLensGazeReceiver&) = delete;
    HoloLensGazeReceiver& operator=(const HoloLensGazeReceiver&) = delete;

    void start();
    void stop();

private:
    void run();
    void initializeOutlet();
    bool parsePacket(const std::string& packet, std::array<double, ChannelCount>& sample) const;

    unsigned short port_;
    std::string stream_name_;
    std::string stream_type_;
    std::string source_id_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::unique_ptr<lsl::stream_info> info_;
    std::unique_ptr<lsl::stream_outlet> outlet_;
};
