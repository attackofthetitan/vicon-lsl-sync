#include "preview/PreviewCsv.h"

#include "preview/PreviewParsing.h"

#include <fstream>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vicon_lsl {
namespace {

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                field.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(field);
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    fields.push_back(field);
    return fields;
}

double parseDoubleField(const std::string& text) {
    if (text.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return value;
}

std::size_t findColumn(const std::vector<std::string>& labels, const std::string& name) {
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (labels[index] == name) {
            return index;
        }
    }
    return labels.size();
}

} // namespace

PreviewRecording loadMergedPreviewCsv(const std::string& path,
                                      const PreviewTransformProfile& vicon_transform,
                                      const PreviewTransformProfile& gaze_transform) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open CSV: " + path);
    }

    std::string header;
    if (!std::getline(input, header)) {
        throw std::runtime_error("CSV has no header: " + path);
    }
    std::vector<std::string> labels = splitCsvLine(header);
    const std::size_t relative_time_index = findColumn(labels, "relative_time");
    const std::size_t lsl_time_index = findColumn(labels, "lsl_time");

    PreviewRecording recording;
    std::string line;
    double first_lsl_time = std::numeric_limits<double>::quiet_NaN();
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> fields = splitCsvLine(line);
        std::vector<double> sample(labels.size(), std::numeric_limits<double>::quiet_NaN());
        for (std::size_t index = 0; index < labels.size() && index < fields.size(); ++index) {
            sample[index] = parseDoubleField(fields[index]);
        }

        PreviewFrame frame;
        if (relative_time_index < sample.size() && std::isfinite(sample[relative_time_index])) {
            frame.timestamp = sample[relative_time_index];
        } else if (lsl_time_index < sample.size() && std::isfinite(sample[lsl_time_index])) {
            if (!std::isfinite(first_lsl_time)) {
                first_lsl_time = sample[lsl_time_index];
            }
            frame.timestamp = sample[lsl_time_index] - first_lsl_time;
        } else {
            frame.timestamp = static_cast<double>(recording.frames.size());
        }

        frame.markers = parseMarkerSample(labels, sample, vicon_transform);
        frame.segments = parseSegmentSample(labels, sample, vicon_transform);
        frame.gaze_rays = parseGazeSample(labels, sample, gaze_transform);
        frame.marker_stream_present = !frame.markers.empty();
        frame.segment_stream_present = !frame.segments.empty();
        frame.gaze_stream_present = !frame.gaze_rays.empty();
        recording.frames.push_back(std::move(frame));
    }

    std::ostringstream summary;
    summary << recording.frames.size() << " frame(s), " << labels.size() << " column(s)";
    recording.summary = summary.str();
    return recording;
}

} // namespace vicon_lsl
