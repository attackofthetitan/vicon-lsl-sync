#pragma once

#include <string>
#include <DataStreamClient.h>

class ViconClient {
public:
    explicit ViconClient(const std::string& server_address);
    ~ViconClient();

    bool connect();
    void disconnect();
    bool isConnected() const;
    bool getFrame();

    unsigned int getSubjectCount() const;
    std::string getSubjectName(unsigned int index) const;

    unsigned int getMarkerCount(const std::string& subject) const;
    std::string getMarkerName(const std::string& subject, unsigned int index) const;
    bool getMarkerGlobalTranslation(const std::string& subject, const std::string& marker,
                                     double& x, double& y, double& z, bool& occluded);

    unsigned int getSegmentCount(const std::string& subject) const;
    std::string getSegmentName(const std::string& subject, unsigned int index) const;
    bool getSegmentGlobalTranslation(const std::string& subject, const std::string& segment,
                                      double& x, double& y, double& z);
    bool getSegmentGlobalRotationQuaternion(const std::string& subject, const std::string& segment,
                                             double& qx, double& qy, double& qz, double& qw);

private:
    ViconDataStreamSDK::CPP::Client client_;
    std::string server_address_;
    bool connected_ = false;
};
