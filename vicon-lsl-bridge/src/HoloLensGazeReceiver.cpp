#include "HoloLensGazeReceiver.h"

#include <lsl_cpp.h>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

struct ChannelMeta {
    const char* label;
    const char* unit;
};

constexpr std::array<ChannelMeta, HoloLensGazeReceiver::ChannelCount> kChannels{{
    {"CombinedOriginX", "meters"},
    {"CombinedOriginY", "meters"},
    {"CombinedOriginZ", "meters"},
    {"CombinedDirectionX", "normalized"},
    {"CombinedDirectionY", "normalized"},
    {"CombinedDirectionZ", "normalized"},
    {"CombinedValid", "bool"},
    {"LeftEyeOriginX", "meters"},
    {"LeftEyeOriginY", "meters"},
    {"LeftEyeOriginZ", "meters"},
    {"LeftEyeDirectionX", "normalized"},
    {"LeftEyeDirectionY", "normalized"},
    {"LeftEyeDirectionZ", "normalized"},
    {"LeftEyeValid", "bool"},
    {"RightEyeOriginX", "meters"},
    {"RightEyeOriginY", "meters"},
    {"RightEyeOriginZ", "meters"},
    {"RightEyeDirectionX", "normalized"},
    {"RightEyeDirectionY", "normalized"},
    {"RightEyeDirectionZ", "normalized"},
    {"RightEyeValid", "bool"},
    {"HitPointX", "meters"},
    {"HitPointY", "meters"},
    {"HitPointZ", "meters"},
    {"HitValid", "bool"},
    {"VergenceDistance", "meters"},
}};

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

void closeSocket(SocketHandle socket) {
    closesocket(socket);
}

int lastSocketError() {
    return WSAGetLastError();
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

void closeSocket(SocketHandle socket) {
    close(socket);
}

int lastSocketError() {
    return errno;
}
#endif

bool parseDouble(std::string_view text, double& value) {
    if (text == "NaN") {
        value = std::numeric_limits<double>::quiet_NaN();
        return true;
    }

    std::string copy(text);
    char* end = nullptr;
    errno = 0;
    value = std::strtod(copy.c_str(), &end);
    return errno == 0 && end == copy.c_str() + copy.size();
}

} // namespace

HoloLensGazeReceiver::HoloLensGazeReceiver(unsigned short port,
                                           std::string stream_name,
                                           std::string stream_type,
                                           std::string source_id)
    : port_(port),
      stream_name_(std::move(stream_name)),
      stream_type_(std::move(stream_type)),
      source_id_(std::move(source_id)) {}

HoloLensGazeReceiver::~HoloLensGazeReceiver() {
    stop();
}

void HoloLensGazeReceiver::start() {
    if (running_) {
        return;
    }

    initializeOutlet();
    running_ = true;
    thread_ = std::thread(&HoloLensGazeReceiver::run, this);
}

void HoloLensGazeReceiver::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    outlet_.reset();
    info_.reset();
}

void HoloLensGazeReceiver::initializeOutlet() {
    info_ = std::make_unique<lsl::stream_info>(
        stream_name_,
        stream_type_,
        static_cast<int>(ChannelCount),
        lsl::IRREGULAR_RATE,
        lsl::cf_double64,
        source_id_);

    lsl::xml_element channels = info_->desc().append_child("channels");
    for (const auto& channel_meta : kChannels) {
        lsl::xml_element channel = channels.append_child("channel");
        channel.append_child_value("label", channel_meta.label);
        channel.append_child_value("unit", channel_meta.unit);
    }

    lsl::xml_element acquisition = info_->desc().append_child("acquisition");
    acquisition.append_child_value("device", "HoloLens2");
    acquisition.append_child_value("sdk", "ExtendedEyeTracking");
    acquisition.append_child_value("transport", "udp-embedded-in-vicon-bridge");

    outlet_ = std::make_unique<lsl::stream_outlet>(*info_);
}

void HoloLensGazeReceiver::run() {
#ifdef _WIN32
    WSADATA wsa_data;
    const bool wsa_started = WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    if (!wsa_started) {
        std::cerr << "Failed to initialize Winsock for HoloLens gaze receiver" << std::endl;
        return;
    }
#endif

    SocketHandle socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == kInvalidSocket) {
        std::cerr << "Failed to create HoloLens gaze UDP socket: " << lastSocketError() << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);

    if (bind(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Failed to bind HoloLens gaze UDP port " << port_
                  << ": " << lastSocketError() << std::endl;
        closeSocket(socket_handle);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

#ifdef _WIN32
    DWORD timeout_ms = 250;
    setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;
    setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    std::cout << "HoloLens gaze UDP receiver listening on port " << port_ << std::endl;

    std::array<char, 4096> buffer{};
    std::array<double, ChannelCount> sample{};
    unsigned long long count = 0;

    while (running_) {
        sockaddr_in sender{};
#ifdef _WIN32
        int sender_size = sizeof(sender);
        int received = recvfrom(socket_handle, buffer.data(), static_cast<int>(buffer.size() - 1), 0,
                                reinterpret_cast<sockaddr*>(&sender), &sender_size);
#else
        socklen_t sender_size = sizeof(sender);
        ssize_t received = recvfrom(socket_handle, buffer.data(), buffer.size() - 1, 0,
                                    reinterpret_cast<sockaddr*>(&sender), &sender_size);
#endif
        if (received <= 0) {
            continue;
        }

        std::string packet(buffer.data(), static_cast<size_t>(received));
        if (!parsePacket(packet, sample)) {
            continue;
        }

        outlet_->push_sample(sample.data(), lsl::local_clock());
        ++count;
        if (count % 900 == 0) {
            std::cout << "Relayed " << count << " HoloLens gaze samples" << std::endl;
        }
    }

    closeSocket(socket_handle);
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HoloLensGazeReceiver::parsePacket(const std::string& packet,
                                       std::array<double, ChannelCount>& sample) const {
    std::string_view view(packet);
    constexpr std::string_view prefix = "HLGAZE1,";
    if (view.substr(0, prefix.size()) != prefix) {
        return false;
    }

    view.remove_prefix(prefix.size());

    size_t field_index = 0;
    bool consumed_all = false;
    while (field_index < ChannelCount + 1) {
        size_t comma = view.find(',');
        std::string_view field = comma == std::string_view::npos ? view : view.substr(0, comma);

        double parsed = 0.0;
        if (!parseDouble(field, parsed)) {
            return false;
        }

        if (field_index > 0) {
            sample[field_index - 1] = parsed;
        }

        ++field_index;
        if (comma == std::string_view::npos) {
            consumed_all = true;
            break;
        }
        view.remove_prefix(comma + 1);
    }

    return consumed_all && field_index == ChannelCount + 1;
}
