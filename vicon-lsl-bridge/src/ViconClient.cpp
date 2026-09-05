#include "ViconClient.h"
#include "ViconTimestamp.h"

#include <lsl_cpp.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace SDK = ViconDataStreamSDK::CPP;

namespace {

// How long to spend deciding whether anything is listening. A server on the
// lab network answers a TCP handshake in a few milliseconds; this only has to
// be short enough that a stop request is not left waiting on it.
constexpr int kReachabilityTimeoutMs = 500;

// The port the Vicon DataStream server listens on when an address omits one.
constexpr const char* kDefaultViconPort = "801";

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void closeSocket(SocketHandle handle) {
#ifdef _WIN32
    closesocket(handle);
#else
    ::close(handle);
#endif
}

bool connectIsInProgress() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS;
#endif
}

bool markNonBlocking(SocketHandle handle) {
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(handle, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(handle, F_GETFL, 0);
    return flags >= 0 && fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// True once the handshake completes, false if it fails or outlasts the budget.
bool waitForConnection(SocketHandle handle, int timeout_ms) {
#ifdef _WIN32
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(handle, &writable);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    // The first argument is ignored on Windows, where a socket is not an index.
    if (select(0, nullptr, &writable, nullptr, &timeout) <= 0) {
        return false;
    }
#else
    pollfd descriptor{};
    descriptor.fd = handle;
    descriptor.events = POLLOUT;
    if (poll(&descriptor, 1, timeout_ms) <= 0) {
        return false;
    }
#endif

    // Becoming writable only means the attempt finished; it may have been
    // refused, so the pending socket error is what decides.
    int pending_error = 0;
    SocketLength length = sizeof(pending_error);
    return getsockopt(handle, SOL_SOCKET, SO_ERROR,
                      reinterpret_cast<char*>(&pending_error), &length) == 0 &&
           pending_error == 0;
}

bool addressAccepts(const addrinfo& candidate, int timeout_ms) {
    const SocketHandle handle =
        socket(candidate.ai_family, candidate.ai_socktype, candidate.ai_protocol);
    if (handle == kInvalidSocket) {
        return false;
    }

    bool accepted = false;
    if (markNonBlocking(handle)) {
        const int started = ::connect(handle, candidate.ai_addr,
                                      static_cast<SocketLength>(candidate.ai_addrlen));
        accepted = started == 0 ||
                   (connectIsInProgress() && waitForConnection(handle, timeout_ms));
    }

    closeSocket(handle);
    return accepted;
}

// Splits "host:port", leaving an address that carries no port on the default.
// An IPv6 literal is full of colons, so only a trailing all-digit field counts.
std::pair<std::string, std::string> splitServerAddress(const std::string& address) {
    const std::size_t separator = address.rfind(':');
    if (separator == std::string::npos || separator + 1 == address.size()) {
        return {address, kDefaultViconPort};
    }

    const std::string port = address.substr(separator + 1);
    if (port.find_first_not_of("0123456789") != std::string::npos) {
        return {address, kDefaultViconPort};
    }

    return {address.substr(0, separator), port};
}

// The SDK's Connect() blocks until the server answers or its own connection
// timeout expires, and nothing can cancel it in between, so a stop request
// waits behind it. Shortening that timeout would only abandon an attempt the
// SDK is still running, so ask a cheaper question first: is anything listening?
// When the answer is no, which is the case whenever a stop is left waiting,
// the SDK is never asked to try at all.
bool serverIsListening(const std::string& address) {
#ifdef _WIN32
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        // Without sockets there is nothing to check, so let the SDK decide.
        return true;
    }
    struct WinsockRelease {
        ~WinsockRelease() { WSACleanup(); }
    } winsock_release;
#endif

    const auto target = splitServerAddress(address);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* candidates = nullptr;
    if (getaddrinfo(target.first.c_str(), target.second.c_str(), &hints, &candidates) != 0) {
        return false;
    }

    bool listening = false;
    for (const addrinfo* candidate = candidates; candidate && !listening;
         candidate = candidate->ai_next) {
        listening = addressAccepts(*candidate, kReachabilityTimeoutMs);
    }

    freeaddrinfo(candidates);
    return listening;
}

const char* sdkResultName(SDK::Result::Enum result) {
    switch (result) {
        case SDK::Result::Unknown: return "Unknown";
        case SDK::Result::NotImplemented: return "NotImplemented";
        case SDK::Result::Success: return "Success";
        case SDK::Result::InvalidHostName: return "InvalidHostName";
        case SDK::Result::InvalidMulticastIP: return "InvalidMulticastIP";
        case SDK::Result::ClientAlreadyConnected: return "ClientAlreadyConnected";
        case SDK::Result::ClientConnectionFailed: return "ClientConnectionFailed";
        case SDK::Result::ServerAlreadyTransmittingMulticast:
            return "ServerAlreadyTransmittingMulticast";
        case SDK::Result::ServerNotTransmittingMulticast:
            return "ServerNotTransmittingMulticast";
        case SDK::Result::NotConnected: return "NotConnected";
        case SDK::Result::NoFrame: return "NoFrame";
        case SDK::Result::InvalidIndex: return "InvalidIndex";
        case SDK::Result::InvalidCameraName: return "InvalidCameraName";
        case SDK::Result::InvalidSubjectName: return "InvalidSubjectName";
        case SDK::Result::InvalidSegmentName: return "InvalidSegmentName";
        case SDK::Result::InvalidMarkerName: return "InvalidMarkerName";
        case SDK::Result::InvalidDeviceName: return "InvalidDeviceName";
        case SDK::Result::InvalidDeviceOutputName: return "InvalidDeviceOutputName";
        case SDK::Result::InvalidLatencySampleName: return "InvalidLatencySampleName";
        case SDK::Result::CoLinearAxes: return "CoLinearAxes";
        case SDK::Result::LeftHandedAxes: return "LeftHandedAxes";
        case SDK::Result::HapticAlreadySet: return "HapticAlreadySet";
        case SDK::Result::EarlyDataRequested: return "EarlyDataRequested";
        case SDK::Result::LateDataRequested: return "LateDataRequested";
        case SDK::Result::InvalidOperation: return "InvalidOperation";
        case SDK::Result::NotSupported: return "NotSupported";
        case SDK::Result::ConfigurationFailed: return "ConfigurationFailed";
        case SDK::Result::NotPresent: return "NotPresent";
        case SDK::Result::ArgumentOutOfRange: return "ArgumentOutOfRange";
    }
    return "UnrecognizedResult";
}

std::string describeSdkResult(SDK::Result::Enum result) {
    return std::string(sdkResultName(result)) + " (" +
           std::to_string(static_cast<int>(result)) + ")";
}

vicon_lsl::ViconReadStatus readStatus(SDK::Result::Enum result) {
    return result == SDK::Result::NotConnected
               ? vicon_lsl::ViconReadStatus::NotConnected
               : vicon_lsl::ViconReadStatus::SdkError;
}

bool checkSetupResult(const char* operation,
                      const SDK::Output_SimpleResult& output,
                      SDK::Client& client) {
    if (output.Result == SDK::Result::Success) {
        return true;
    }

    std::cerr << "Vicon setup failed: operation=" << operation
              << " sdk_result=" << describeSdkResult(output.Result) << std::endl;
    client.Disconnect();
    return false;
}

} // namespace

ViconClient::ViconClient(const std::string& server_address)
    : server_address_(server_address) {}

ViconClient::~ViconClient() {
    disconnect();
}

bool ViconClient::connect() {
    if (!serverIsListening(server_address_)) {
        std::cerr << "Failed to connect to " << server_address_
                  << " (nothing is listening)" << std::endl;
        return false;
    }

    auto result = client_.Connect(server_address_);
    if (result.Result != SDK::Result::Success) {
        std::cerr << "Failed to connect to " << server_address_
                  << " (" << describeSdkResult(result.Result) << ")" << std::endl;
        return false;
    }

    if (!checkSetupResult("SetStreamMode(ServerPush)",
                          client_.SetStreamMode(SDK::StreamMode::ServerPush),
                          client_) ||
        !checkSetupResult("EnableSegmentData", client_.EnableSegmentData(), client_) ||
        !checkSetupResult("EnableMarkerData", client_.EnableMarkerData(), client_)) {
        return false;
    }

    connected_ = true;
    frame_number_ = 0;
    frame_timestamp_ = 0.0;
    frame_rate_ = 0.0;
    std::cout << "Connected to " << server_address_ << std::endl;
    return true;
}

void ViconClient::disconnect() {
    if (connected_) {
        const auto result = client_.Disconnect();
        if (result.Result != SDK::Result::Success &&
            result.Result != SDK::Result::NotConnected) {
            std::cerr << "Vicon disconnect failed: sdk_result="
                      << describeSdkResult(result.Result) << std::endl;
        }
        connected_ = false;
        frame_number_ = 0;
        frame_timestamp_ = 0.0;
        frame_rate_ = 0.0;
        std::cout << "Disconnected" << std::endl;
    }
}

bool ViconClient::isConnected() const {
    // connected_ tracks what this object asked for; the SDK knows whether the
    // socket is still up. Both have to agree for a session to keep streaming.
    return connected_ && client_.IsConnected().Connected;
}

bool ViconClient::getFrame() {
    auto result = client_.GetFrame();
    if (result.Result != SDK::Result::Success) {
        std::cerr << "GetFrame failed (" << describeSdkResult(result.Result) << ")" << std::endl;
        return false;
    }

    // Capture the local clock immediately after GetFrame. GetLatencyTotal is a
    // Vicon pipeline-latency estimate, not a capture-accurate timestamp or a
    // measurement of ServerPush/network delay, so subtracting a valid value
    // produces only an estimated acquisition timestamp. Invalid values fall
    // back to this immediate receipt time.
    const double receipt_timestamp = lsl::local_clock();
    const auto latency = client_.GetLatencyTotal();
    frame_timestamp_ = vicon_lsl::viconFrameTimestamp(
        receipt_timestamp,
        latency.Total,
        latency.Result == SDK::Result::Success);

    const auto frame_number = client_.GetFrameNumber();
    if (frame_number.Result != SDK::Result::Success) {
        std::cerr << "GetFrameNumber failed ("
                  << describeSdkResult(frame_number.Result) << ")" << std::endl;
        return false;
    }
    frame_number_ = frame_number.FrameNumber;

    const auto rate = client_.GetFrameRate();
    if (rate.Result == SDK::Result::Success &&
        std::isfinite(rate.FrameRateHz) && rate.FrameRateHz > 0.0) {
        frame_rate_ = rate.FrameRateHz;
    }
    return true;
}

unsigned int ViconClient::frameNumber() const {
    return frame_number_;
}

double ViconClient::frameTimestamp() const {
    return frame_timestamp_;
}

double ViconClient::frameRate() const {
    return frame_rate_;
}

vicon_lsl::CountRead ViconClient::readSubjectCount() const {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                0,
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetSubjectCount();
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                0,
                describeSdkResult(output.Result),
                "Failed to get subject count"};
    }
    return {vicon_lsl::ViconReadStatus::Ok, output.SubjectCount, "Success", ""};
}

vicon_lsl::NameRead ViconClient::readSubjectName(unsigned int index) const {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                "",
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetSubjectName(index);
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                "",
                describeSdkResult(output.Result),
                "Failed to get subject name at index " + std::to_string(index)};
    }
    return {vicon_lsl::ViconReadStatus::Ok, output.SubjectName, "Success", ""};
}

vicon_lsl::CountRead ViconClient::readMarkerCount(const std::string& subject) const {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                0,
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetMarkerCount(subject);
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                0,
                describeSdkResult(output.Result),
                "Failed to get marker count for subject " + subject};
    }
    return {vicon_lsl::ViconReadStatus::Ok, output.MarkerCount, "Success", ""};
}

vicon_lsl::NameRead ViconClient::readMarkerName(const std::string& subject,
                                                unsigned int index) const {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                "",
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetMarkerName(subject, index);
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                "",
                describeSdkResult(output.Result),
                "Failed to get marker name for subject " + subject +
                    " at index " + std::to_string(index)};
    }
    return {vicon_lsl::ViconReadStatus::Ok, output.MarkerName, "Success", ""};
}

vicon_lsl::MarkerTranslationRead ViconClient::readMarkerGlobalTranslation(
    const std::string& subject, const std::string& marker) {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                {0.0, 0.0, 0.0},
                false,
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetMarkerGlobalTranslation(subject, marker);
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                {0.0, 0.0, 0.0},
                false,
                describeSdkResult(output.Result),
                "Failed to read marker global translation"};
    }

    vicon_lsl::MarkerTranslationRead read;
    read.translation = {output.Translation[0], output.Translation[1], output.Translation[2]};
    read.occluded = output.Occluded;
    read.status = output.Occluded ? vicon_lsl::ViconReadStatus::Occluded
                                  : vicon_lsl::ViconReadStatus::Ok;
    read.sdk_result = "Success";
    read.message = output.Occluded ? "Marker is occluded" : "";
    return read;
}

vicon_lsl::CountRead ViconClient::readSegmentCount(const std::string& subject) const {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                0,
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetSegmentCount(subject);
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                0,
                describeSdkResult(output.Result),
                "Failed to get segment count for subject " + subject};
    }
    return {vicon_lsl::ViconReadStatus::Ok, output.SegmentCount, "Success", ""};
}

vicon_lsl::NameRead ViconClient::readSegmentName(const std::string& subject,
                                                 unsigned int index) const {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                "",
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetSegmentName(subject, index);
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                "",
                describeSdkResult(output.Result),
                "Failed to get segment name for subject " + subject +
                    " at index " + std::to_string(index)};
    }
    return {vicon_lsl::ViconReadStatus::Ok, output.SegmentName, "Success", ""};
}

vicon_lsl::SegmentTranslationRead ViconClient::readSegmentGlobalTranslation(
    const std::string& subject, const std::string& segment) {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                {0.0, 0.0, 0.0},
                false,
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetSegmentGlobalTranslation(subject, segment);
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                {0.0, 0.0, 0.0},
                false,
                describeSdkResult(output.Result),
                "Failed to read segment global translation"};
    }

    vicon_lsl::SegmentTranslationRead read;
    read.translation = {output.Translation[0], output.Translation[1], output.Translation[2]};
    read.occluded = output.Occluded;
    read.status = output.Occluded ? vicon_lsl::ViconReadStatus::Occluded
                                  : vicon_lsl::ViconReadStatus::Ok;
    read.sdk_result = "Success";
    read.message = output.Occluded ? "Segment translation is occluded" : "";
    return read;
}

vicon_lsl::SegmentRotationRead ViconClient::readSegmentGlobalRotationQuaternion(
    const std::string& subject, const std::string& segment) {
    if (!connected_) {
        return {vicon_lsl::ViconReadStatus::NotConnected,
                {0.0, 0.0, 0.0, 1.0},
                false,
                "NotConnected",
                "Vicon client is not connected"};
    }

    const auto output = client_.GetSegmentGlobalRotationQuaternion(subject, segment);
    if (output.Result != SDK::Result::Success) {
        return {readStatus(output.Result),
                {0.0, 0.0, 0.0, 1.0},
                false,
                describeSdkResult(output.Result),
                "Failed to read segment global rotation quaternion"};
    }

    vicon_lsl::SegmentRotationRead read;
    read.quaternion = {output.Rotation[0], output.Rotation[1], output.Rotation[2], output.Rotation[3]};
    read.occluded = output.Occluded;
    read.status = output.Occluded ? vicon_lsl::ViconReadStatus::Occluded
                                  : vicon_lsl::ViconReadStatus::Ok;
    read.sdk_result = "Success";
    read.message = output.Occluded ? "Segment rotation is occluded" : "";
    return read;
}
