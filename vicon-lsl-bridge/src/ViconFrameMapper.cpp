#include "ViconFrameMapper.h"

#include <cmath>
#include <stdexcept>

namespace vicon_lsl {
DiagnosticAggregator::DiagnosticAggregator(unsigned int repeat_interval)
    : repeat_interval_(repeat_interval) {
    if (repeat_interval_ == 0) {
        throw std::invalid_argument("Diagnostic repeat interval must be positive");
    }
}

DiagnosticEmission DiagnosticAggregator::record(
    const std::vector<ViconDiagnostic>& diagnostics) {
    DiagnosticEmission emission;

    for (const auto& diagnostic : diagnostics) {
        const std::string key = diagnosticKey(diagnostic);
        const unsigned int count = ++counts_[key];
        if (count == 1 || count % repeat_interval_ == 0) {
            std::string line = formatDiagnostic(diagnostic);
            if (count > 1) {
                line += " (repeated " + std::to_string(count) + " times)";
            }
            emission.log_lines.push_back(std::move(line));
        }
    }

    if (!emission.log_lines.empty()) {
        emission.status_message = summarizeDiagnostics(diagnostics);
    }
    return emission;
}

void DiagnosticAggregator::clear() {
    counts_.clear();
}

const char* toString(DiagnosticSeverity severity) {
    return severity == DiagnosticSeverity::Warning ? "warning" : "error";
}

const char* toString(ViconReadStatus status) {
    switch (status) {
        case ViconReadStatus::Ok: return "Ok";
        case ViconReadStatus::Occluded: return "Occluded";
        case ViconReadStatus::SdkError: return "SdkError";
        case ViconReadStatus::NotConnected: return "NotConnected";
    }
    return "Unknown";
}

double quietNaN() {
    return std::numeric_limits<double>::quiet_NaN();
}

double viconFrameTimestamp(double receipt_timestamp,
                           double latency_seconds,
                           bool latency_valid) {
    if (!std::isfinite(receipt_timestamp)) {
        return quietNaN();
    }
    if (latency_valid && std::isfinite(latency_seconds) && latency_seconds >= 0.0) {
        const double corrected = receipt_timestamp - latency_seconds;
        if (std::isfinite(corrected)) {
            return corrected;
        }
    }
    return receipt_timestamp;
}

bool enforceViconTimestamp(double candidate_timestamp,
                           double receipt_timestamp,
                           ViconTimestampState& state,
                           double& output_timestamp,
                           bool* adjusted) {
    if (adjusted) {
        *adjusted = false;
    }

    double timestamp = candidate_timestamp;
    if (!std::isfinite(timestamp)) {
        timestamp = receipt_timestamp;
    }
    if (!std::isfinite(timestamp)) {
        return false;
    }

    if (state.have_timestamp && timestamp <= state.last_timestamp) {
        timestamp = std::nextafter(
            state.last_timestamp, std::numeric_limits<double>::infinity());
        if (!std::isfinite(timestamp)) {
            return false;
        }
        if (adjusted) {
            *adjusted = true;
        }
    }

    state.last_timestamp = timestamp;
    state.have_timestamp = true;
    output_timestamp = timestamp;
    return true;
}

MarkerSample invalidMarkerSample() {
    return {quietNaN(), quietNaN(), quietNaN(), 0.0};
}

SegmentSample invalidSegmentSample() {
    return {quietNaN(), quietNaN(), quietNaN(), quietNaN(), quietNaN(), quietNaN(), quietNaN()};
}

bool isValid(const MarkerTranslationRead& read) {
    return read.status == ViconReadStatus::Ok && !read.occluded;
}

bool isValid(const SegmentTranslationRead& read) {
    return read.status == ViconReadStatus::Ok && !read.occluded;
}

bool isValid(const SegmentRotationRead& read) {
    return read.status == ViconReadStatus::Ok && !read.occluded;
}

bool isValid(const CountRead& read) {
    return read.status == ViconReadStatus::Ok;
}

bool isValid(const NameRead& read) {
    return read.status == ViconReadStatus::Ok;
}

bool layoutChanged(const ViconLayout& current, const ViconLayout& known) {
    return current != known;
}

std::string buildStreamSourceId(const std::string& prefix,
                                const std::string& kind,
                                const std::string& hostname) {
    return prefix + kind + "_" + hostname;
}

std::string formatDiagnostic(const ViconDiagnostic& diagnostic) {
    std::ostringstream out;
    out << "Vicon " << toString(diagnostic.severity)
        << " frame=" << diagnostic.frame_number
        << " operation=" << diagnostic.operation
        << " subject=" << diagnostic.subject
        << " object=" << diagnostic.object_name
        << " sdk_result=" << diagnostic.sdk_result
        << " message=" << diagnostic.message;
    return out.str();
}

std::string diagnosticKey(const ViconDiagnostic& diagnostic) {
    return diagnostic.operation + "|" + diagnostic.subject + "|" + diagnostic.object_name + "|" +
           diagnostic.sdk_result + "|" + diagnostic.message;
}

std::string summarizeDiagnostics(const std::vector<ViconDiagnostic>& diagnostics) {
    if (diagnostics.empty()) {
        return {};
    }

    std::ostringstream out;
    out << diagnostics.size() << " Vicon diagnostic";
    if (diagnostics.size() != 1) {
        out << "s";
    }
    out << "; first: " << formatDiagnostic(diagnostics.front());
    return out.str();
}

MarkerSample markerSampleForLsl(const MarkerTranslationRead& read) {
    if (!isValid(read)) {
        return invalidMarkerSample();
    }
    return {read.translation[0], read.translation[1], read.translation[2], 1.0};
}

SegmentSample segmentSampleForLsl(const SegmentTranslationRead& translation,
                                  const SegmentRotationRead& rotation) {
    if (!isValid(translation) || !isValid(rotation)) {
        return invalidSegmentSample();
    }
    return {
        translation.translation[0],
        translation.translation[1],
        translation.translation[2],
        rotation.quaternion[0],
        rotation.quaternion[1],
        rotation.quaternion[2],
        rotation.quaternion[3],
    };
}

} // namespace vicon_lsl
