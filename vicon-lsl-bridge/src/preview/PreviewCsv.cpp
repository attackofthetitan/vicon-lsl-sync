#include "preview/PreviewCsv.h"

#include "preview/PreviewParsing.h"

#include <fstream>
#include <filesystem>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <optional>

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

void reportProgress(const PreviewLoadOptions& options,
                    PreviewLoadStage stage,
                    std::uint64_t completed,
                    std::uint64_t total,
                    const std::string& detail = {}) {
    if (options.cancel_requested && options.cancel_requested()) {
        throw std::runtime_error("Preview load canceled");
    }
    if (options.progress) {
        options.progress({stage, completed, total, detail});
    }
}

std::size_t estimateFramePayloadBytes(const PreviewFrame& frame) {
    std::size_t bytes = 0;
    bytes += frame.markers.capacity() * sizeof(PreviewMarker);
    bytes += frame.segments.capacity() * sizeof(PreviewSegment);
    bytes += frame.gaze_rays.capacity() * sizeof(PreviewGazeRay);
    for (const PreviewMarker& marker : frame.markers) bytes += marker.name.capacity();
    for (const PreviewSegment& segment : frame.segments) bytes += segment.name.capacity();
    for (const PreviewGazeRay& ray : frame.gaze_rays) bytes += ray.name.capacity();
    return bytes;
}

void compactFrames(PreviewRecording& recording) {
    std::vector<PreviewFrame> compacted;
    compacted.reserve((recording.frames.size() + 1) / 2 + 1);
    for (std::size_t input = 0; input < recording.frames.size(); input += 2) {
        compacted.push_back(std::move(recording.frames[input]));
    }
    if (recording.frames.size() > 2 &&
        (recording.frames.size() - 1) % 2 != 0) {
        compacted.push_back(std::move(recording.frames.back()));
    }
    recording.frames.swap(compacted);
    if (recording.stored_frame_stride <=
        (std::numeric_limits<std::size_t>::max)() / 2) {
        recording.stored_frame_stride *= 2;
    }
}

} // namespace

std::size_t estimatePreviewRecordingBytes(const PreviewRecording& recording) {
    std::size_t bytes = sizeof(recording);
    bytes += recording.frames.capacity() * sizeof(PreviewFrame);
    for (const PreviewFrame& frame : recording.frames) {
        bytes += estimateFramePayloadBytes(frame);
    }
    return bytes;
}

void boundPreviewRecordingCache(PreviewRecording& recording,
                                std::size_t maximum_frames,
                                std::size_t maximum_memory_bytes) {
    maximum_frames = (std::max)(std::size_t{2}, maximum_frames);
    recording.estimated_memory_bytes = estimatePreviewRecordingBytes(recording);
    while (recording.frames.size() > maximum_frames ||
           (maximum_memory_bytes > 0 &&
            recording.estimated_memory_bytes > maximum_memory_bytes)) {
        if (recording.frames.size() <= 2) {
            throw std::runtime_error(
                "Preview cache budget is too small for two decoded frames");
        }
        compactFrames(recording);
        recording.estimated_memory_bytes = estimatePreviewRecordingBytes(recording);
    }
}

void appendBoundedPreviewFrame(PreviewRecording& recording,
                               PreviewFrame frame,
                               std::size_t maximum_frames,
                               std::size_t maximum_memory_bytes) {
    const std::size_t payload_bytes = estimateFramePayloadBytes(frame);
    const std::size_t previous_capacity = recording.frames.capacity();
    if (recording.frames.empty()) {
        recording.estimated_memory_bytes = sizeof(recording);
    } else if (recording.estimated_memory_bytes == 0) {
        recording.estimated_memory_bytes = estimatePreviewRecordingBytes(recording);
    }
    recording.frames.push_back(std::move(frame));
    recording.estimated_memory_bytes += payload_bytes +
        (recording.frames.capacity() - previous_capacity) * sizeof(PreviewFrame);
    if (recording.frames.size() > (std::max)(std::size_t{2}, maximum_frames) ||
        (maximum_memory_bytes > 0 &&
         recording.estimated_memory_bytes > maximum_memory_bytes)) {
        boundPreviewRecordingCache(recording, maximum_frames, maximum_memory_bytes);
    }
}

PreviewRecording loadMergedPreviewCsv(const std::string& path,
                                      const PreviewTransformProfile& vicon_transform,
                                      const PreviewTransformProfile& gaze_transform,
                                      const PreviewLoadOptions& options) {
    std::error_code size_error;
    const std::uint64_t file_size = std::filesystem::file_size(path, size_error);
    if (!size_error && options.maximum_file_bytes > 0 &&
        file_size > options.maximum_file_bytes) {
        throw std::runtime_error("CSV exceeds the configured file-size limit");
    }
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open CSV: " + path);
    }

    std::string header;
    if (!std::getline(input, header)) {
        throw std::runtime_error("CSV has no header: " + path);
    }
    if (header.size() > options.maximum_line_bytes) {
        throw std::runtime_error("CSV header exceeds the configured line-size limit");
    }
    std::vector<std::string> labels = splitCsvLine(header);
    if (labels.size() > options.maximum_columns) {
        throw std::runtime_error("CSV exceeds the configured column-count limit");
    }
    const std::size_t relative_time_index = findColumn(labels, "relative_time");
    const std::size_t lsl_time_index = findColumn(labels, "lsl_time");

    PreviewRecording recording;
    std::string line;
    double first_lsl_time = std::numeric_limits<double>::quiet_NaN();
    std::optional<PreviewFrame> pending_last_frame;
    reportProgress(options, PreviewLoadStage::Reading, 0, file_size, "CSV header");
    while (std::getline(input, line)) {
        if (line.size() > options.maximum_line_bytes) {
            throw std::runtime_error("CSV row exceeds the configured line-size limit");
        }
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
            frame.timestamp = static_cast<double>(recording.source_frame_count);
        }

        frame.markers = parseMarkerSample(labels, sample, vicon_transform);
        frame.segments = parseSegmentSample(labels, sample, vicon_transform);
        frame.gaze_rays = parseGazeSample(labels, sample, gaze_transform);
        frame.marker_stream_present = !frame.markers.empty();
        frame.segment_stream_present = !frame.segments.empty();
        frame.gaze_stream_present = !frame.gaze_rays.empty();
        if (recording.source_frame_count == 0) {
            recording.source_start_timestamp = frame.timestamp;
        }
        recording.source_end_timestamp = frame.timestamp;
        const std::size_t source_index = recording.source_frame_count++;
        if (source_index % recording.stored_frame_stride == 0) {
            appendBoundedPreviewFrame(recording, std::move(frame),
                                      options.maximum_preview_frames,
                                      options.maximum_memory_bytes);
            pending_last_frame.reset();
        } else {
            pending_last_frame = std::move(frame);
        }
        if (recording.source_frame_count % (std::max)(
                std::size_t{1}, options.cancellation_check_sample_interval) == 0) {
            const auto position = input.tellg();
            reportProgress(options, PreviewLoadStage::Reading,
                           position < 0 ? 0 : static_cast<std::uint64_t>(position),
                           file_size,
                           std::to_string(recording.source_frame_count) + " rows");
        }
    }

    if (pending_last_frame) {
        appendBoundedPreviewFrame(recording, std::move(*pending_last_frame),
                                  options.maximum_preview_frames,
                                  options.maximum_memory_bytes);
    }

    reportProgress(options, PreviewLoadStage::FramePreparation,
                   recording.frames.size(), recording.frames.size(), "CSV preview frames");
    boundPreviewRecordingCache(recording, options.maximum_preview_frames,
                               options.maximum_memory_bytes);

    std::ostringstream summary;
    summary << recording.source_frame_count << " source frame(s), "
            << recording.frames.size() << " cached frame(s) at stride "
            << recording.stored_frame_stride << ", " << labels.size() << " column(s), "
            << recording.estimated_memory_bytes / (1024 * 1024) << " MiB estimated cache";
    recording.summary = summary.str();
    reportProgress(options, PreviewLoadStage::Complete, file_size, file_size, recording.summary);
    return recording;
}

} // namespace vicon_lsl
