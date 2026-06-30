#include "HoloLensGazeReceiver.h"

#include <lsl_cpp.h>

#include <cerrno>
#include <exception>
#include <iostream>
#include <unordered_map>
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

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

void closeSocket(SocketHandle socket) {
    closesocket(socket);
}

int lastSocketError() {
    return WSAGetLastError();
}

bool isReceiveTimeoutError(int error) {
    return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
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

bool isReceiveTimeoutError(int error) {
    return error == EAGAIN || error == EWOULDBLOCK;
}
#endif

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
    if (thread_.joinable()) {
        thread_.join();
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
    for (const auto& channel_meta : vicon_lsl::holoLensGazeChannels()) {
        lsl::xml_element channel = channels.append_child("channel");
        channel.append_child_value("label", std::string(channel_meta.label));
        channel.append_child_value("unit", std::string(channel_meta.unit));
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
    const int timeout_result =
        setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;
    const int timeout_result =
        setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    if (timeout_result != 0) {
        const std::string error = "Failed to configure HoloLens gaze socket timeout: " +
            std::to_string(lastSocketError());
        std::cerr << error << std::endl;
        running_ = false;
        reportStatus(error);
        closeSocket(socket_handle);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    try {
        initializeOutlet();
    } catch (const std::exception& ex) {
        const std::string error =
            "Failed to create HoloLens gaze LSL outlet: " + std::string(ex.what());
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
    unsigned long long receive_error_count = 0;
    std::unordered_map<std::string, unsigned long long> parse_error_counts;

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
        if (received < 0) {
            const int socket_error = lastSocketError();
            if (!isReceiveTimeoutError(socket_error)) {
                ++receive_error_count;
                if (receive_error_count == 1 || receive_error_count % 100 == 0) {
                    std::string error = "HoloLens gaze UDP receive failed: error=" +
                        std::to_string(socket_error);
                    if (receive_error_count > 1) {
                        error += " (repeated " + std::to_string(receive_error_count) + " times)";
                    }
                    std::cerr << error << std::endl;
                    reportStatus(error);
                }
            }
            continue;
        }

        const double receive_timestamp = lsl::local_clock();
        const std::string packet(buffer.data(), static_cast<std::size_t>(received));
        const auto parsed = vicon_lsl::parseHoloLensGazePacket(packet);
        if (!parsed.ok()) {
            const unsigned long long malformed = ++malformed_packet_count_;
            const std::string key =
                std::string(vicon_lsl::toString(parsed.error)) + "|" +
                std::to_string(parsed.field_index);
            const auto error_count = ++parse_error_counts[key];
            if (error_count == 1 || error_count % 100 == 0) {
                std::string error = "HoloLens gaze packet rejected: error=" +
                    std::string(vicon_lsl::toString(parsed.error)) +
                    " field_index=" + std::to_string(parsed.field_index);
                if (!parsed.field.empty()) {
                    error += " field=" + parsed.field.substr(0, 64);
                }
                if (error_count > 1) {
                    error += " (repeated " + std::to_string(error_count) + " times)";
                }
                std::cerr << error << std::endl;
                reportStatus(error);
            } else if (malformed % 100 == 0) {
                reportStatus();
            }
            continue;
        }

        try {
            outlet_->push_sample(
                parsed.packet.sample.data(), receive_timestamp);
        } catch (const std::exception& ex) {
            const std::string error =
                "Failed to push HoloLens gaze LSL sample: " + std::string(ex.what());
            std::cerr << error << std::endl;
            running_ = false;
            reportStatus(error);
            break;
        }

        const unsigned long long count = ++sample_count_;
        if (count % 900 == 0) {
            std::cout << "Relayed " << count << " HoloLens gaze samples" << std::endl;
            reportStatus();
        }
    }

    listening_ = false;
    running_ = false;
    closeSocket(socket_handle);
#ifdef _WIN32
    WSACleanup();
#endif
    reportStatus();
}
