#pragma once

#include "Config.h"
#include "StreamOutlet.h"
#include "ViconFrameTypes.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

class ViconLSLBridge;

namespace vicon_lsl::bridge_internal {

class ViconClient {
public:
    virtual ~ViconClient() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual bool getFrame() = 0;
    virtual unsigned int frameNumber() const = 0;
    virtual double frameTimestamp() const = 0;
    virtual double frameRate() const = 0;

    virtual CountRead readSubjectCount() const = 0;
    virtual NameRead readSubjectName(unsigned int index) const = 0;
    virtual CountRead readMarkerCount(const std::string& subject) const = 0;
    virtual NameRead readMarkerName(const std::string& subject,
                                    unsigned int index) const = 0;
    virtual MarkerTranslationRead readMarkerGlobalTranslation(
        const std::string& subject,
        const std::string& marker) = 0;
    virtual CountRead readSegmentCount(const std::string& subject) const = 0;
    virtual NameRead readSegmentName(const std::string& subject,
                                     unsigned int index) const = 0;
    virtual SegmentTranslationRead readSegmentGlobalTranslation(
        const std::string& subject,
        const std::string& segment) = 0;
    virtual SegmentRotationRead readSegmentGlobalRotationQuaternion(
        const std::string& subject,
        const std::string& segment) = 0;
};

using Clock = std::function<double()>;
using Wait = std::function<void(std::chrono::milliseconds)>;

struct Collaborators {
    std::shared_ptr<ViconClient> client;
    StreamOutletFactory outlet_factory;
    Clock clock;
    Wait wait;
};

enum class ConnectedSessionEnd {
    InitialFrameFailed,
    InitialStreamInitializationFailed,
    FrameReadFailed,
    ClientDisconnected,
    OutletFailed,
    LayoutStreamInitializationFailed,
    StopRequested,
};

class BridgeTestAccess {
public:
    static std::unique_ptr<::ViconLSLBridge> create(
        const Config& config,
        Collaborators collaborators);
};

} // namespace vicon_lsl::bridge_internal
