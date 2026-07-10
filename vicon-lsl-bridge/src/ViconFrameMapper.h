#pragma once

#include "StreamSchema.h"

#include <array>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
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

struct MarkerObjectRead {
    unsigned int frame_number = 0;
    std::string subject;
    std::string object_name;
    std::string operation = "GetMarkerGlobalTranslation";
    MarkerTranslationRead value;
};

struct SegmentObjectRead {
    unsigned int frame_number = 0;
    std::string subject;
    std::string object_name;
    std::string translation_operation = "GetSegmentGlobalTranslation";
    std::string rotation_operation = "GetSegmentGlobalRotationQuaternion";
    SegmentTranslationRead translation;
    SegmentRotationRead rotation;
};

struct MarkerFrameResult {
    std::vector<MarkerObjectRead> reads;
    std::vector<ViconDiagnostic> diagnostics;
};

struct SegmentFrameResult {
    std::vector<SegmentObjectRead> reads;
    std::vector<ViconDiagnostic> diagnostics;
};

struct ViconFrameResult {
    std::vector<MarkerObjectRead> markers;
    std::vector<SegmentObjectRead> segments;
    std::vector<ViconDiagnostic> diagnostics;
};

enum class BridgeDiagnosticState {
    Disconnected,
    Connecting,
    Streaming,
    Stopped
};

struct BridgeDiagnosticStatus {
    BridgeDiagnosticState state = BridgeDiagnosticState::Disconnected;
    std::size_t marker_count = 0;
    std::size_t segment_count = 0;
    unsigned int frame_count = 0;
    std::string message;
};

struct DiagnosticEmission {
    std::vector<std::string> log_lines;
    std::string status_message;

    bool shouldReportStatus() const { return !status_message.empty(); }
};

class DiagnosticAggregator {
public:
    explicit DiagnosticAggregator(unsigned int repeat_interval = 100);

    DiagnosticEmission record(const std::vector<ViconDiagnostic>& diagnostics);
    void clear();

private:
    unsigned int repeat_interval_;
    std::unordered_map<std::string, unsigned int> counts_;
};

const char* toString(DiagnosticSeverity severity);
const char* toString(ViconReadStatus status);
const char* bridgeDiagnosticStateName(BridgeDiagnosticState state);

double quietNaN();
MarkerSample invalidMarkerSample();
SegmentSample invalidSegmentSample();

bool isValid(const MarkerTranslationRead& read);
bool isValid(const SegmentTranslationRead& read);
bool isValid(const SegmentRotationRead& read);
bool isValid(const CountRead& read);
bool isValid(const NameRead& read);

bool layoutChanged(const ViconLayout& current, const ViconLayout& known);

std::string buildStreamSourceId(const std::string& prefix,
                                const std::string& kind,
                                const std::string& hostname);
std::string formatDiagnostic(const ViconDiagnostic& diagnostic);
std::string diagnosticKey(const ViconDiagnostic& diagnostic);
std::string summarizeDiagnostics(const std::vector<ViconDiagnostic>& diagnostics);
std::string formatLayoutSummary(const ViconLayout& layout);
std::string formatBridgeDiagnostics(const BridgeDiagnosticStatus& status);

MarkerSample markerSampleForLsl(const MarkerTranslationRead& read);
SegmentSample segmentSampleForLsl(const SegmentTranslationRead& translation,
                                  const SegmentRotationRead& rotation);

template <class Client>
ViconDiscoveryResult discoverLayout(Client& client, unsigned int frame_number) {
    ViconDiscoveryResult result;

    const auto subject_count = client.readSubjectCount();
    if (!isValid(subject_count)) {
        result.layout = {};
        result.diagnostics.push_back({
            DiagnosticSeverity::Error,
            frame_number,
            "<all>",
            "<layout>",
            "GetSubjectCount",
            subject_count.sdk_result.empty() ? toString(subject_count.status)
                                             : subject_count.sdk_result,
            subject_count.message.empty() ? "Unable to discover Vicon subjects"
                                          : subject_count.message,
        });
        return result;
    }

    for (unsigned int subject_index = 0; subject_index < subject_count.value; ++subject_index) {
        const auto subject_name = client.readSubjectName(subject_index);
        if (!isValid(subject_name)) {
            result.layout = {};
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                frame_number,
                "<index " + std::to_string(subject_index) + ">",
                "<layout>",
                "GetSubjectName",
                subject_name.sdk_result.empty() ? toString(subject_name.status)
                                                : subject_name.sdk_result,
                subject_name.message.empty() ? "Unable to discover Vicon subject name"
                                             : subject_name.message,
            });
            return result;
        }

        const std::string& subject = subject_name.value;
        const auto marker_count = client.readMarkerCount(subject);
        if (!isValid(marker_count)) {
            result.layout = {};
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                frame_number,
                subject,
                "<markers>",
                "GetMarkerCount",
                marker_count.sdk_result.empty() ? toString(marker_count.status)
                                                : marker_count.sdk_result,
                marker_count.message.empty() ? "Unable to discover Vicon marker count"
                                             : marker_count.message,
            });
            return result;
        }

        for (unsigned int marker_index = 0; marker_index < marker_count.value; ++marker_index) {
            const auto marker_name = client.readMarkerName(subject, marker_index);
            if (!isValid(marker_name)) {
                result.layout = {};
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    frame_number,
                    subject,
                    "<marker index " + std::to_string(marker_index) + ">",
                    "GetMarkerName",
                    marker_name.sdk_result.empty() ? toString(marker_name.status)
                                                   : marker_name.sdk_result,
                    marker_name.message.empty() ? "Unable to discover Vicon marker name"
                                                : marker_name.message,
                });
                return result;
            }
            result.layout.markers.emplace_back(subject, marker_name.value);
        }

        const auto segment_count = client.readSegmentCount(subject);
        if (!isValid(segment_count)) {
            result.layout = {};
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                frame_number,
                subject,
                "<segments>",
                "GetSegmentCount",
                segment_count.sdk_result.empty() ? toString(segment_count.status)
                                                 : segment_count.sdk_result,
                segment_count.message.empty() ? "Unable to discover Vicon segment count"
                                              : segment_count.message,
            });
            return result;
        }

        for (unsigned int segment_index = 0; segment_index < segment_count.value; ++segment_index) {
            const auto segment_name = client.readSegmentName(subject, segment_index);
            if (!isValid(segment_name)) {
                result.layout = {};
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    frame_number,
                    subject,
                    "<segment index " + std::to_string(segment_index) + ">",
                    "GetSegmentName",
                    segment_name.sdk_result.empty() ? toString(segment_name.status)
                                                    : segment_name.sdk_result,
                    segment_name.message.empty() ? "Unable to discover Vicon segment name"
                                                 : segment_name.message,
                });
                return result;
            }
            result.layout.segments.emplace_back(subject, segment_name.value);
        }
    }

    return result;
}

template <class Client>
MarkerFrameResult buildMarkerFrame(Client& client,
                                   const std::vector<NamedViconItem>& markers,
                                   unsigned int frame_number) {
    MarkerFrameResult result;
    result.reads.reserve(markers.size());

    for (const auto& marker : markers) {
        const auto read = client.readMarkerGlobalTranslation(marker.first, marker.second);
        result.reads.push_back({
            frame_number,
            marker.first,
            marker.second,
            "GetMarkerGlobalTranslation",
            read,
        });

        if (!isValid(read)) {
            result.diagnostics.push_back({
                read.status == ViconReadStatus::Occluded ? DiagnosticSeverity::Warning
                                                         : DiagnosticSeverity::Error,
                frame_number,
                marker.first,
                marker.second,
                "GetMarkerGlobalTranslation",
                read.sdk_result.empty() ? toString(read.status) : read.sdk_result,
                read.message.empty() ? "Marker translation unavailable" : read.message,
            });
        }
    }

    return result;
}

template <class Client>
SegmentFrameResult buildSegmentFrame(Client& client,
                                     const std::vector<NamedViconItem>& segments,
                                     unsigned int frame_number) {
    SegmentFrameResult result;
    result.reads.reserve(segments.size());

    for (const auto& segment : segments) {
        const auto translation = client.readSegmentGlobalTranslation(segment.first, segment.second);
        const auto rotation = client.readSegmentGlobalRotationQuaternion(segment.first, segment.second);

        result.reads.push_back({
            frame_number,
            segment.first,
            segment.second,
            "GetSegmentGlobalTranslation",
            "GetSegmentGlobalRotationQuaternion",
            translation,
            rotation,
        });
        if (!isValid(translation)) {
            result.diagnostics.push_back({
                translation.status == ViconReadStatus::Occluded ? DiagnosticSeverity::Warning
                                                                : DiagnosticSeverity::Error,
                frame_number,
                segment.first,
                segment.second,
                "GetSegmentGlobalTranslation",
                translation.sdk_result.empty() ? toString(translation.status) : translation.sdk_result,
                translation.message.empty() ? "Segment translation unavailable" : translation.message,
            });
        }
        if (!isValid(rotation)) {
            result.diagnostics.push_back({
                rotation.status == ViconReadStatus::Occluded ? DiagnosticSeverity::Warning
                                                             : DiagnosticSeverity::Error,
                frame_number,
                segment.first,
                segment.second,
                "GetSegmentGlobalRotationQuaternion",
                rotation.sdk_result.empty() ? toString(rotation.status) : rotation.sdk_result,
                rotation.message.empty() ? "Segment rotation unavailable" : rotation.message,
            });
        }
    }

    return result;
}

template <class Client>
ViconFrameResult buildViconFrame(Client& client,
                                 const ViconLayout& layout,
                                 unsigned int frame_number) {
    ViconFrameResult result;
    auto marker_frame = buildMarkerFrame(client, layout.markers, frame_number);
    auto segment_frame = buildSegmentFrame(client, layout.segments, frame_number);

    result.markers = std::move(marker_frame.reads);
    result.segments = std::move(segment_frame.reads);
    result.diagnostics = std::move(marker_frame.diagnostics);
    result.diagnostics.insert(result.diagnostics.end(),
                              segment_frame.diagnostics.begin(),
                              segment_frame.diagnostics.end());
    return result;
}

} // namespace vicon_lsl
