#pragma once

#include "ViconFrameMapper.h"

#include <DataStreamClient.h>

#include <string>

class ViconClient {
public:
    explicit ViconClient(const std::string& server_address);
    ~ViconClient();

    bool connect();
    void disconnect();
    bool isConnected() const;
    bool getFrame();
    unsigned int frameNumber() const;
    double frameTimestamp() const;
    double frameRate() const;

    vicon_lsl::CountRead readSubjectCount() const;
    vicon_lsl::NameRead readSubjectName(unsigned int index) const;

    vicon_lsl::CountRead readMarkerCount(const std::string& subject) const;
    vicon_lsl::NameRead readMarkerName(const std::string& subject, unsigned int index) const;
    vicon_lsl::MarkerTranslationRead readMarkerGlobalTranslation(
        const std::string& subject, const std::string& marker);

    vicon_lsl::CountRead readSegmentCount(const std::string& subject) const;
    vicon_lsl::NameRead readSegmentName(const std::string& subject, unsigned int index) const;
    vicon_lsl::SegmentTranslationRead readSegmentGlobalTranslation(
        const std::string& subject, const std::string& segment);
    vicon_lsl::SegmentRotationRead readSegmentGlobalRotationQuaternion(
        const std::string& subject, const std::string& segment);

private:
    ViconDataStreamSDK::CPP::Client client_;
    std::string server_address_;
    bool connected_ = false;
    unsigned int frame_number_ = 0;
    double frame_timestamp_ = 0.0;
    double frame_rate_ = 0.0;
};
