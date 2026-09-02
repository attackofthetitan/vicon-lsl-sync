#pragma once

#include "ViconLSLBridgeInternal.h"

#include <DataStreamClient.h>

#include <string>

// The one real implementation of the bridge's client interface. It implements
// that interface directly so no forwarding shim has to restate every method.
class ViconClient final : public vicon_lsl::bridge_internal::ViconClient {
public:
    explicit ViconClient(const std::string& server_address);
    ~ViconClient() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    bool getFrame() override;
    unsigned int frameNumber() const override;
    double frameTimestamp() const override;
    double frameRate() const override;

    vicon_lsl::CountRead readSubjectCount() const override;
    vicon_lsl::NameRead readSubjectName(unsigned int index) const override;

    vicon_lsl::CountRead readMarkerCount(const std::string& subject) const override;
    vicon_lsl::NameRead readMarkerName(const std::string& subject,
                                       unsigned int index) const override;
    vicon_lsl::MarkerTranslationRead readMarkerGlobalTranslation(
        const std::string& subject, const std::string& marker) override;

    vicon_lsl::CountRead readSegmentCount(const std::string& subject) const override;
    vicon_lsl::NameRead readSegmentName(const std::string& subject,
                                        unsigned int index) const override;
    vicon_lsl::SegmentTranslationRead readSegmentGlobalTranslation(
        const std::string& subject, const std::string& segment) override;
    vicon_lsl::SegmentRotationRead readSegmentGlobalRotationQuaternion(
        const std::string& subject, const std::string& segment) override;

private:
    ViconDataStreamSDK::CPP::Client client_;
    std::string server_address_;
    bool connected_ = false;
    unsigned int frame_number_ = 0;
    double frame_timestamp_ = 0.0;
    double frame_rate_ = 0.0;
};
