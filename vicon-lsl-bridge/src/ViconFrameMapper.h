#pragma once

// Turns Vicon reads into LSL samples: layout discovery, per-frame reads,
// timestamps, and error reporting.

#include "ViconFrameTypes.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vicon_lsl {

double quietNaN();

// Timestamps

struct ViconTimestampState {
    bool have_timestamp = false;
    double last_timestamp = 0.0;
};

// Estimates the capture time as the local receipt time minus Vicon's reported
// latency. Uses the receipt time when the latency is missing, negative, or not
// a number. This is an estimate, not the exact capture time.
double viconFrameTimestamp(double receipt_timestamp,
                           double latency_seconds,
                           bool latency_valid);

// Keeps timestamps finite and always increasing. A time that is not a number is
// replaced by the receipt time. A time that would go backward is replaced by the
// receipt time if that is later, or else by the smallest step past the last one.
// Returns false when no finite time is available.
bool enforceViconTimestamp(double candidate_timestamp,
                           double receipt_timestamp,
                           ViconTimestampState& state,
                           double& output_timestamp,
                           bool* adjusted = nullptr);

// Samples sent to LSL

MarkerSample invalidMarkerSample();
SegmentSample invalidSegmentSample();

bool isValid(const MarkerTranslationRead& read);
bool isValid(const SegmentTranslationRead& read);
bool isValid(const SegmentRotationRead& read);
bool isValid(const CountRead& read);
bool isValid(const NameRead& read);

// A marker that could not be read becomes NaN, NaN, NaN, 0.
MarkerSample markerSampleForLsl(const MarkerTranslationRead& read);
// A segment needs both reads to be good, or all seven values become NaN.
SegmentSample segmentSampleForLsl(const SegmentTranslationRead& translation,
                                  const SegmentRotationRead& rotation);

// Error reporting

struct DiagnosticEmission {
    std::vector<std::string> log_lines;
    std::string status_message;

    bool shouldReportStatus() const { return !status_message.empty(); }
};

// Logs the first copy of each error and then every Nth repeat.
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

std::string formatDiagnostic(const ViconDiagnostic& diagnostic);
std::string diagnosticKey(const ViconDiagnostic& diagnostic);
std::string summarizeDiagnostics(const std::vector<ViconDiagnostic>& diagnostics);

// Reading a frame. These are templates so tests can pass a fake client.

// Describes a failed read. Uses the SDK's own result and message when it gave
// them. Hidden items are warnings; everything else is an error.
template <class Read>
ViconDiagnostic readFailure(const Read& read,
                            unsigned int frame_number,
                            std::string subject,
                            std::string object_name,
                            std::string operation,
                            std::string fallback_message) {
    return {
        read.status == ViconReadStatus::Occluded ? DiagnosticSeverity::Warning
                                                 : DiagnosticSeverity::Error,
        frame_number,
        std::move(subject),
        std::move(object_name),
        std::move(operation),
        read.sdk_result.empty() ? toString(read.status) : read.sdk_result,
        read.message.empty() ? std::move(fallback_message) : read.message,
    };
}

// Reads every subject, marker, and segment name in Vicon's order. Any failed
// read discards the whole layout, so a partial layout is never published.
template <class Client>
ViconDiscoveryResult discoverLayout(Client& client, unsigned int frame_number) {
    ViconDiscoveryResult result;
    const auto abandon = [&result](ViconDiagnostic diagnostic) {
        result.layout = {};
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    };
    const auto at_index = [](const char* what, unsigned int index) {
        return "<" + std::string(what) + std::to_string(index) + ">";
    };

    const auto subject_count = client.readSubjectCount();
    if (!isValid(subject_count)) {
        return abandon(readFailure(subject_count, frame_number, "<all>", "<layout>",
                                   "GetSubjectCount",
                                   "Unable to discover Vicon subjects"));
    }

    for (unsigned int subject_index = 0; subject_index < subject_count.value; ++subject_index) {
        const auto subject_name = client.readSubjectName(subject_index);
        if (!isValid(subject_name)) {
            return abandon(readFailure(subject_name, frame_number,
                                       at_index("index ", subject_index), "<layout>",
                                       "GetSubjectName",
                                       "Unable to discover Vicon subject name"));
        }

        const std::string& subject = subject_name.value;
        const auto marker_count = client.readMarkerCount(subject);
        if (!isValid(marker_count)) {
            return abandon(readFailure(marker_count, frame_number, subject, "<markers>",
                                       "GetMarkerCount",
                                       "Unable to discover Vicon marker count"));
        }

        for (unsigned int marker_index = 0; marker_index < marker_count.value; ++marker_index) {
            const auto marker_name = client.readMarkerName(subject, marker_index);
            if (!isValid(marker_name)) {
                return abandon(readFailure(marker_name, frame_number, subject,
                                           at_index("marker index ", marker_index),
                                           "GetMarkerName",
                                           "Unable to discover Vicon marker name"));
            }
            result.layout.markers.emplace_back(subject, marker_name.value);
        }

        const auto segment_count = client.readSegmentCount(subject);
        if (!isValid(segment_count)) {
            return abandon(readFailure(segment_count, frame_number, subject, "<segments>",
                                       "GetSegmentCount",
                                       "Unable to discover Vicon segment count"));
        }

        for (unsigned int segment_index = 0; segment_index < segment_count.value; ++segment_index) {
            const auto segment_name = client.readSegmentName(subject, segment_index);
            if (!isValid(segment_name)) {
                return abandon(readFailure(segment_name, frame_number, subject,
                                           at_index("segment index ", segment_index),
                                           "GetSegmentName",
                                           "Unable to discover Vicon segment name"));
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
        auto read = client.readMarkerGlobalTranslation(marker.first, marker.second);
        if (!isValid(read)) {
            result.diagnostics.push_back(
                readFailure(read, frame_number, marker.first, marker.second,
                            "GetMarkerGlobalTranslation",
                            "Marker translation unavailable"));
        }
        result.reads.push_back(std::move(read));
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
        auto translation = client.readSegmentGlobalTranslation(segment.first, segment.second);
        auto rotation = client.readSegmentGlobalRotationQuaternion(segment.first, segment.second);

        if (!isValid(translation)) {
            result.diagnostics.push_back(
                readFailure(translation, frame_number, segment.first, segment.second,
                            "GetSegmentGlobalTranslation",
                            "Segment translation unavailable"));
        }
        if (!isValid(rotation)) {
            result.diagnostics.push_back(
                readFailure(rotation, frame_number, segment.first, segment.second,
                            "GetSegmentGlobalRotationQuaternion",
                            "Segment rotation unavailable"));
        }
        result.reads.push_back({std::move(translation), std::move(rotation)});
    }

    return result;
}

template <class Client>
ViconFrameResult buildViconFrame(Client& client,
                                 const ViconLayout& layout,
                                 unsigned int frame_number) {
    auto marker_frame = buildMarkerFrame(client, layout.markers, frame_number);
    auto segment_frame = buildSegmentFrame(client, layout.segments, frame_number);

    ViconFrameResult result;
    result.markers = std::move(marker_frame.reads);
    result.segments = std::move(segment_frame.reads);
    result.diagnostics = std::move(marker_frame.diagnostics);
    result.diagnostics.insert(result.diagnostics.end(),
                              segment_frame.diagnostics.begin(),
                              segment_frame.diagnostics.end());
    return result;
}

} // namespace vicon_lsl
