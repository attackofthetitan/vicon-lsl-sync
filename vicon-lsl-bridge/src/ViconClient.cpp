#include "ViconClient.h"

#include <iostream>
#include <string>

namespace SDK = ViconDataStreamSDK::CPP;

namespace {

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

void logLegacyDiscoveryFailure(const char* operation,
                               const std::string& context,
                               const std::string& sdk_result,
                               const std::string& message) {
    std::cerr << "Vicon discovery error: operation=" << operation
              << " context=" << context
              << " sdk_result=" << sdk_result
              << " message=" << message << std::endl;
}

} // namespace

ViconClient::ViconClient(const std::string& server_address)
    : server_address_(server_address) {}

ViconClient::~ViconClient() {
    disconnect();
}

bool ViconClient::connect() {
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
        std::cout << "Disconnected" << std::endl;
    }
}

bool ViconClient::isConnected() const {
    return connected_;
}

bool ViconClient::getFrame() {
    auto result = client_.GetFrame();
    if (result.Result != SDK::Result::Success) {
        std::cerr << "GetFrame failed (" << describeSdkResult(result.Result) << ")" << std::endl;
        return false;
    }

    const auto frame_number = client_.GetFrameNumber();
    if (frame_number.Result != SDK::Result::Success) {
        std::cerr << "GetFrameNumber failed ("
                  << describeSdkResult(frame_number.Result) << ")" << std::endl;
        return false;
    }
    frame_number_ = frame_number.FrameNumber;
    return true;
}

unsigned int ViconClient::frameNumber() const {
    return frame_number_;
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

unsigned int ViconClient::getSubjectCount() const {
    const auto read = readSubjectCount();
    if (!vicon_lsl::isValid(read)) {
        logLegacyDiscoveryFailure(
            "GetSubjectCount", "<all>", read.sdk_result, read.message);
        return 0;
    }
    return read.value;
}

std::string ViconClient::getSubjectName(unsigned int index) const {
    const auto read = readSubjectName(index);
    if (!vicon_lsl::isValid(read)) {
        logLegacyDiscoveryFailure(
            "GetSubjectName", std::to_string(index), read.sdk_result, read.message);
        return {};
    }
    return read.value;
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

unsigned int ViconClient::getMarkerCount(const std::string& subject) const {
    const auto read = readMarkerCount(subject);
    if (!vicon_lsl::isValid(read)) {
        logLegacyDiscoveryFailure(
            "GetMarkerCount", subject, read.sdk_result, read.message);
        return 0;
    }
    return read.value;
}

std::string ViconClient::getMarkerName(const std::string& subject, unsigned int index) const {
    const auto read = readMarkerName(subject, index);
    if (!vicon_lsl::isValid(read)) {
        logLegacyDiscoveryFailure(
            "GetMarkerName",
            subject + "/" + std::to_string(index),
            read.sdk_result,
            read.message);
        return {};
    }
    return read.value;
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

bool ViconClient::getMarkerGlobalTranslation(const std::string& subject,
                                             const std::string& marker,
                                             double& x,
                                             double& y,
                                             double& z,
                                             bool& occluded) {
    const auto read = readMarkerGlobalTranslation(subject, marker);
    if (read.status == vicon_lsl::ViconReadStatus::SdkError ||
        read.status == vicon_lsl::ViconReadStatus::NotConnected) {
        std::cerr << vicon_lsl::formatDiagnostic({
            vicon_lsl::DiagnosticSeverity::Error,
            frame_number_,
            subject,
            marker,
            "GetMarkerGlobalTranslation",
            read.sdk_result,
            read.message,
        }) << std::endl;
        return false;
    }
    x = read.translation[0];
    y = read.translation[1];
    z = read.translation[2];
    occluded = read.occluded;
    return true;
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

unsigned int ViconClient::getSegmentCount(const std::string& subject) const {
    const auto read = readSegmentCount(subject);
    if (!vicon_lsl::isValid(read)) {
        logLegacyDiscoveryFailure(
            "GetSegmentCount", subject, read.sdk_result, read.message);
        return 0;
    }
    return read.value;
}

std::string ViconClient::getSegmentName(const std::string& subject, unsigned int index) const {
    const auto read = readSegmentName(subject, index);
    if (!vicon_lsl::isValid(read)) {
        logLegacyDiscoveryFailure(
            "GetSegmentName",
            subject + "/" + std::to_string(index),
            read.sdk_result,
            read.message);
        return {};
    }
    return read.value;
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

bool ViconClient::getSegmentGlobalTranslation(const std::string& subject,
                                              const std::string& segment,
                                              double& x,
                                              double& y,
                                              double& z) {
    const auto read = readSegmentGlobalTranslation(subject, segment);
    if (read.status == vicon_lsl::ViconReadStatus::SdkError ||
        read.status == vicon_lsl::ViconReadStatus::NotConnected) {
        std::cerr << vicon_lsl::formatDiagnostic({
            vicon_lsl::DiagnosticSeverity::Error,
            frame_number_,
            subject,
            segment,
            "GetSegmentGlobalTranslation",
            read.sdk_result,
            read.message,
        }) << std::endl;
        return false;
    }
    x = read.translation[0];
    y = read.translation[1];
    z = read.translation[2];
    return true;
}

bool ViconClient::getSegmentGlobalRotationQuaternion(const std::string& subject,
                                                     const std::string& segment,
                                                     double& qx,
                                                     double& qy,
                                                     double& qz,
                                                     double& qw) {
    const auto read = readSegmentGlobalRotationQuaternion(subject, segment);
    if (read.status == vicon_lsl::ViconReadStatus::SdkError ||
        read.status == vicon_lsl::ViconReadStatus::NotConnected) {
        std::cerr << vicon_lsl::formatDiagnostic({
            vicon_lsl::DiagnosticSeverity::Error,
            frame_number_,
            subject,
            segment,
            "GetSegmentGlobalRotationQuaternion",
            read.sdk_result,
            read.message,
        }) << std::endl;
        return false;
    }
    qx = read.quaternion[0];
    qy = read.quaternion[1];
    qz = read.quaternion[2];
    qw = read.quaternion[3];
    return true;
}
