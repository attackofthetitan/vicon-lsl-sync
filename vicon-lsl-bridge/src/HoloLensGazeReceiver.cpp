#include "HoloLensGazeReceiver.h"

#include <lsl_cpp.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

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

    sample_count_ = 0;
    malformed_packet_count_ = 0;
    listening_ = false;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        last_error_.clear();
    }
    running_ = true;
    thread_ = std::thread(&HoloLensGazeReceiver::run, this);
    reportStatus();
}

void HoloLensGazeReceiver::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    listening_ = false;
    outlet_.reset();
    info_.reset();
    reportStatus();
}

HoloLensGazeReceiver::Status HoloLensGazeReceiver::status() const {
    Status current;
    current.enabled = running_;
    current.listening = listening_;
    current.sample_count = sample_count_;
    current.malformed_packet_count = malformed_packet_count_;
    std::lock_guard<std::mutex> lock(status_mutex_);
    current.last_error = last_error_;
    return current;
}

void HoloLensGazeReceiver::setStatusCallback(StatusCallback callback) {
    status_callback_ = std::move(callback);
}

void HoloLensGazeReceiver::reportStatus(const std::string& last_error) {
    if (!last_error.empty()) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        last_error_ = last_error;
    }
    if (status_callback_) {
        status_callback_(status());
    }
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
        const std::string error = "Failed to initialize Winsock for HoloLens gaze receiver";
        std::cerr << error << std::endl;
        running_ = false;
        reportStatus(error);
        return;
    }
#endif

    SocketHandle socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == kInvalidSocket) {
        const std::string error = "Failed to create HoloLens gaze UDP socket: " +
            std::to_string(lastSocketError());
        std::cerr << error << std::endl;
        running_ = false;
        reportStatus(error);
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
        const std::string error = "Failed to bind HoloLens gaze UDP port " +
            std::to_string(port_) + ": " + std::to_string(lastSocketError());
        std::cerr << error << std::endl;
        running_ = false;
        reportStatus(error);
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

    try {
        initializeOutlet();
    } catch (const std::exception& e) {
        const std::string error = "Failed to create HoloLens gaze LSL outlet: " + std::string(e.what());
        std::cerr << error << std::endl;
        running_ = false;
        reportStatus(error);
        closeSocket(socket_handle);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }
    listening_ = true;
    std::cout << "HoloLens gaze UDP receiver listening on port " << port_ << std::endl;
    reportStatus();

    std::array<char, 4096> buffer{};
    std::array<double, ChannelCount> sample{};
    double timestamp = 0.0;

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
        if (!parsePacket(packet, timestamp, sample)) {
            unsigned long long malformed = ++malformed_packet_count_;
            if (malformed % 100 == 0) {
                std::cerr << "Ignored " << malformed << " malformed HoloLens gaze UDP packets" << std::endl;
                reportStatus();
            }
            continue;
        }

        outlet_->push_sample(sample.data(), timestamp);
        unsigned long long count = ++sample_count_;
        if (count % 900 == 0) {
            std::cout << "Relayed " << count << " HoloLens gaze samples" << std::endl;
            reportStatus();
        }
    }

    listening_ = false;
    closeSocket(socket_handle);
#ifdef _WIN32
    WSACleanup();
#endif
    reportStatus();
}

bool HoloLensGazeReceiver::parsePacket(const std::string& packet,
                                       double& timestamp,
                                       std::array<double, ChannelCount>& sample) {
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

        if (field_index == 0) {
            if (!std::isfinite(parsed)) {
                return false;
            }
            timestamp = parsed;
        } else {
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
