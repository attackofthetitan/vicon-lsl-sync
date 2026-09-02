#pragma once

#include "StreamSchema.h"

#include <array>
#include <string>
#include <vector>

namespace vicon_lsl {

enum class DiagnosticSeverity {
    Warning,
    Error
};

enum class ViconReadStatus {
    Ok,
    Occluded,
    SdkError,
    NotConnected
};

struct ViconDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    unsigned int frame_number = 0;
    std::string subject;
    std::string object_name;
    std::string operation;
    std::string sdk_result;
    std::string message;
};

struct ViconLayout {
    std::vector<NamedViconItem> markers;
    std::vector<NamedViconItem> segments;

    bool operator==(const ViconLayout& other) const {
        return markers == other.markers && segments == other.segments;
    }

    bool operator!=(const ViconLayout& other) const {
        return !(*this == other);
    }
};

struct MarkerTranslationRead {
    ViconReadStatus status = ViconReadStatus::Ok;
    std::array<double, 3> translation{0.0, 0.0, 0.0};
    bool occluded = false;
    std::string sdk_result = "Success";
    std::string message;
};

struct SegmentTranslationRead {
    ViconReadStatus status = ViconReadStatus::Ok;
    std::array<double, 3> translation{0.0, 0.0, 0.0};
    bool occluded = false;
    std::string sdk_result = "Success";
    std::string message;
};

struct SegmentRotationRead {
    ViconReadStatus status = ViconReadStatus::Ok;
    std::array<double, 4> quaternion{0.0, 0.0, 0.0, 1.0};
    bool occluded = false;
    std::string sdk_result = "Success";
    std::string message;
};

struct CountRead {
    ViconReadStatus status = ViconReadStatus::Ok;
    unsigned int value = 0;
    std::string sdk_result = "Success";
    std::string message;
};

struct NameRead {
    ViconReadStatus status = ViconReadStatus::Ok;
    std::string value;
    std::string sdk_result = "Success";
    std::string message;
};

struct ViconDiscoveryResult {
    ViconLayout layout;
    std::vector<ViconDiagnostic> diagnostics;

    bool ok() const { return diagnostics.empty(); }
};

// A segment's pose needs both reads to become one LSL sample.
struct SegmentPoseRead {
    SegmentTranslationRead translation;
    SegmentRotationRead rotation;
};

// Frame results carry only the values the outlets convert. The subject, object,
// and operation that identify a read belong to the diagnostic raised for it, so
// repeating them per item per frame would only copy strings nobody reads.
struct MarkerFrameResult {
    std::vector<MarkerTranslationRead> reads;
    std::vector<ViconDiagnostic> diagnostics;
};

struct SegmentFrameResult {
    std::vector<SegmentPoseRead> reads;
    std::vector<ViconDiagnostic> diagnostics;
};

struct ViconFrameResult {
    std::vector<MarkerTranslationRead> markers;
    std::vector<SegmentPoseRead> segments;
    std::vector<ViconDiagnostic> diagnostics;
};

} // namespace vicon_lsl
