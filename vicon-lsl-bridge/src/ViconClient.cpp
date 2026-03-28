#include "ViconClient.h"
#include <iostream>

namespace SDK = ViconDataStreamSDK::CPP;

ViconClient::ViconClient(const std::string& server_address)
    : server_address_(server_address) {}

ViconClient::~ViconClient() {
    disconnect();
}

bool ViconClient::connect() {
    auto result = client_.Connect(server_address_);
    if (result.Result != SDK::Result::Success) {
        std::cerr << "Failed to connect to " << server_address_ << std::endl;
        return false;
    }

    client_.SetStreamMode(SDK::StreamMode::ServerPush);
    client_.EnableSegmentData();
    client_.EnableMarkerData();

    connected_ = true;
    std::cout << "Connected to " << server_address_ << std::endl;
    return true;
}

void ViconClient::disconnect() {
    if (connected_) {
        client_.Disconnect();
        connected_ = false;
        std::cout << "Disconnected" << std::endl;
    }
}

bool ViconClient::isConnected() const {
    return connected_;
}

bool ViconClient::getFrame() {
    auto result = client_.GetFrame();
    if (result.Result != SDK::Result::Success) {
        std::cerr << "GetFrame failed" << std::endl;
        connected_ = false;
        return false;
    }
    return true;
}

unsigned int ViconClient::getSubjectCount() const {
    return client_.GetSubjectCount().SubjectCount;
}

std::string ViconClient::getSubjectName(unsigned int index) const {
    return client_.GetSubjectName(index).SubjectName;
}

unsigned int ViconClient::getMarkerCount(const std::string& subject) const {
    return client_.GetMarkerCount(subject).MarkerCount;
}

std::string ViconClient::getMarkerName(const std::string& subject, unsigned int index) const {
    return client_.GetMarkerName(subject, index).MarkerName;
}

bool ViconClient::getMarkerGlobalTranslation(const std::string& subject, const std::string& marker,
                                               double& x, double& y, double& z, bool& occluded) {
    auto output = client_.GetMarkerGlobalTranslation(subject, marker);
    if (output.Result != SDK::Result::Success) {
        return false;
    }
    x = output.Translation[0];
    y = output.Translation[1];
    z = output.Translation[2];
    occluded = output.Occluded;
    return true;
}

unsigned int ViconClient::getSegmentCount(const std::string& subject) const {
    return client_.GetSegmentCount(subject).SegmentCount;
}

std::string ViconClient::getSegmentName(const std::string& subject, unsigned int index) const {
    return client_.GetSegmentName(subject, index).SegmentName;
}

bool ViconClient::getSegmentGlobalTranslation(const std::string& subject, const std::string& segment,
                                                double& x, double& y, double& z) {
    auto output = client_.GetSegmentGlobalTranslation(subject, segment);
    if (output.Result != SDK::Result::Success) {
        return false;
    }
    x = output.Translation[0];
    y = output.Translation[1];
    z = output.Translation[2];
    return true;
}

bool ViconClient::getSegmentGlobalRotationQuaternion(const std::string& subject, const std::string& segment,
                                                       double& qx, double& qy, double& qz, double& qw) {
    auto output = client_.GetSegmentGlobalRotationQuaternion(subject, segment);
    if (output.Result != SDK::Result::Success) {
        return false;
    }
    qx = output.Rotation[0];
    qy = output.Rotation[1];
    qz = output.Rotation[2];
    qw = output.Rotation[3];
    return true;
}
