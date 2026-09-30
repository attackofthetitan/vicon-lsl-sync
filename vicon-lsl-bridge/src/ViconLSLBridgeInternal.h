#pragma once

#include "StreamOutlet.h"
#include "ViconFrameTypes.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace vicon_lsl::bridge_internal {

// The Vicon reads the bridge needs, done by the Vicon SDK in the app and by a
// fake in tests.
class ViconClient {
public:
    virtual ~ViconClient() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual bool getFrame() = 0;
    // True when the last getFrame() failed only because no frame came in time.
    virtual bool frameTimedOut() const { return false; }
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

struct Dependencies {
    std::shared_ptr<ViconClient> client;
    StreamOutletFactory outlet_factory;
    std::function<double()> clock;
    std::function<void(std::chrono::milliseconds)> wait;
};

} // namespace vicon_lsl::bridge_internal
