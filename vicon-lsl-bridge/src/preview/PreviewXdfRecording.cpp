#include "preview/PreviewXdfPrivate.h"

#include "preview/PreviewCalibration.h"
#include "preview/PreviewParsing.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vicon_lsl::preview_xdf_detail {
namespace {

std::optional<std::size_t> nearestSampleIndex(const XdfStreamData& stream,
                                              double absolute_timestamp,
                                              double tolerance_seconds) {
    if (stream.timestamps.empty() || stream.samples.empty()) {
        return std::nullopt;
    }

    const auto it = std::lower_bound(
        stream.timestamps.begin(), stream.timestamps.end(), absolute_timestamp);
    std::optional<std::size_t> best;
    double best_delta = std::numeric_limits<double>::infinity();

    auto consider = [&](std::size_t index) {
        if (index >= stream.timestamps.size()) {
            return;
        }
        const double delta = std::abs(stream.timestamps[index] - absolute_timestamp);
        if (delta <= tolerance_seconds && delta < best_delta) {
            best = index;
            best_delta = delta;
        }
    };

    if (it != stream.timestamps.end()) {
        consider(static_cast<std::size_t>(std::distance(stream.timestamps.begin(), it)));
    }
    if (it != stream.timestamps.begin()) {
        consider(static_cast<std::size_t>(std::distance(stream.timestamps.begin(), it) - 1));
    }
    return best;
}

const XdfStreamData* chooseMasterStream(const std::vector<XdfStreamData>& streams,
                                        std::uint32_t preferred_stream_id) {
    auto usable = [](const XdfStreamData& stream) {
        return stream.numeric && !stream.timestamps.empty() && !stream.samples.empty() &&
               (stream.role == PreviewStreamRole::ViconMarkers ||
                stream.role == PreviewStreamRole::ViconSegments ||
                stream.role == PreviewStreamRole::HoloLensGaze);
    };
    if (preferred_stream_id != 0) {
        for (const auto& stream : streams) {
            if (stream.stream_id == preferred_stream_id && usable(stream)) return &stream;
        }
        throw std::runtime_error("Selected XDF master timeline is not a supported preview stream");
    }
    for (const auto& stream : streams) {
        if (usable(stream) && stream.role == PreviewStreamRole::ViconMarkers) {
            return &stream;
        }
    }
    for (const auto& stream : streams) {
        if (usable(stream) && stream.role == PreviewStreamRole::ViconSegments) {
            return &stream;
        }
    }
    for (const auto& stream : streams) {
        if (usable(stream) && stream.role == PreviewStreamRole::HoloLensGaze) {
            return &stream;
        }
    }
    return nullptr;
}

void appendStreamSample(PreviewFrame& frame,
                        const XdfStreamData& stream,
                        const std::vector<double>& sample,
                        const PreviewTransformProfile& vicon_transform,
                        const PreviewTransformProfile& gaze_transform) {
    if (stream.role == PreviewStreamRole::ViconMarkers) {
        auto markers = parseMarkerSample(stream.channel_labels, sample, vicon_transform);
        frame.markers.insert(frame.markers.end(),
                             std::make_move_iterator(markers.begin()),
                             std::make_move_iterator(markers.end()));
    } else if (stream.role == PreviewStreamRole::ViconSegments) {
        auto segments = parseSegmentSample(stream.channel_labels, sample, vicon_transform);
        frame.segments.insert(frame.segments.end(),
                              std::make_move_iterator(segments.begin()),
                              std::make_move_iterator(segments.end()));
    } else if (stream.role == PreviewStreamRole::HoloLensGaze) {
        const auto stream_transform = gazeTransformForCoordinateFrame(
            gaze_transform,
            stream.coordinate_frame);
        auto rays = parseGazeSample(stream.channel_labels, sample, stream_transform);
        frame.gaze_rays.insert(frame.gaze_rays.end(),
                               std::make_move_iterator(rays.begin()),
                               std::make_move_iterator(rays.end()));
    }
}

std::string roleName(PreviewStreamRole role) {
    switch (role) {
    case PreviewStreamRole::ViconMarkers: return "markers";
    case PreviewStreamRole::ViconSegments: return "segments";
    case PreviewStreamRole::HoloLensGaze: return "gaze";
    case PreviewStreamRole::HoloLensCalibrationTarget: return "calibration target";
    case PreviewStreamRole::Unknown: break;
    }
    return "unknown";
}

std::string buildSummary(const XdfLoadResult& xdf,
                         std::size_t frame_count,
                         bool automatically_calibrated,
                         bool tracker_local_gaze,
                         const XdfStreamData* target_stream) {
    std::ostringstream summary;
    summary << xdf.streams.size() << " stream(s), " << frame_count << " frame(s)";
    summary << "; used " << xdf.estimated_memory_bytes / (1024 * 1024)
            << " MiB while reading a " << xdf.file_size_bytes / (1024 * 1024)
            << " MiB file";
    if (xdf.truncated_tail_ignored) {
        summary << "; incomplete final chunk ignored";
    }
    std::size_t repaired_timestamps = 0;
    for (const auto& stream : xdf.streams) {
        repaired_timestamps += stream.repaired_timestamp_count;
    }
    if (repaired_timestamps > 0) {
        summary << "; " << repaired_timestamps << " timestamp(s) repaired";
    }
    if (automatically_calibrated) {
        summary << "; stair-target calibration applied";
    } else if (tracker_local_gaze && target_stream) {
        summary << "; legacy tracker-local gaze shown without stair calibration";
    }
    for (const auto& stream : xdf.streams) {
        summary << "; "
                << (stream.name.empty()
                        ? "stream_" + std::to_string(stream.stream_id)
                        : stream.name)
                << ": " << stream.sample_count << " sample(s), "
                << stream.samples.size() << " loaded, keeping every "
                << stream.stored_sample_stride << " sample(s), "
                << stream.channel_count << " channel(s), " << roleName(stream.role)
                << ", source " << (stream.source_id.empty() ? "<missing>" : stream.source_id)
                << ", range " << stream.start_timestamp << ".." << stream.end_timestamp
                << ", max gap " << stream.maximum_sample_gap
                << ", " << stream.clock_offsets.size()
                << " clock correction point(s)";
    }
    return summary.str();
}

} // namespace

PreviewRecording assembleRecording(const XdfLoadResult& xdf,
                                   const PreviewTransformProfile& vicon_transform,
                                   const PreviewTransformProfile& gaze_transform,
                                   double match_tolerance_seconds,
                                   std::uint32_t preferred_master_stream_id,
                                   const PreviewLoadOptions& options) {
    const XdfStreamData* master = chooseMasterStream(xdf.streams, preferred_master_stream_id);
    if (!master) {
        throw std::runtime_error("XDF contains no supported marker, segment, or gaze preview stream");
    }

    const XdfStreamData* gaze_stream = nullptr;
    const XdfStreamData* target_stream = nullptr;
    for (const auto& stream : xdf.streams) {
        if (!gaze_stream && stream.role == PreviewStreamRole::HoloLensGaze) {
            gaze_stream = &stream;
        } else if (!target_stream &&
                   stream.role == PreviewStreamRole::HoloLensCalibrationTarget) {
            target_stream = &stream;
        }
    }

    PreviewTransformProfile resolved_gaze_transform = gaze_transform;
    bool automatically_calibrated = false;
    bool tracker_local_gaze = false;
    if (gaze_stream) {
        tracker_local_gaze = lowerAscii(gaze_stream->coordinate_frame) == "eye_tracker_space";
    }
    if (gaze_stream && target_stream &&
        calibrationCoordinateFramesCompatible(gaze_stream->coordinate_frame,
                                              target_stream->coordinate_frame)) {
        std::vector<CalibrationTargetPose> target_poses;
        target_poses.reserve(target_stream->samples.size());
        for (const auto& sample : target_stream->samples) {
            const auto pose = parseCalibrationTargetPose(target_stream->channel_labels, sample);
            if (pose) {
                target_poses.push_back(*pose);
            }
        }
        const auto solution = solveStableTrackedTargetCalibration(
            target_poses,
            defaultStairCalibrationProfile());
        if (solution) {
            resolved_gaze_transform = gazeTransformFromTargetCalibration(
                defaultStairCalibrationProfile(),
                solution->holo_from_target);
            automatically_calibrated = true;
        }
    }

    const bool has_markers = std::any_of(
        xdf.streams.begin(), xdf.streams.end(), [](const XdfStreamData& stream) {
            return stream.role == PreviewStreamRole::ViconMarkers;
        });
    const bool has_segments = std::any_of(
        xdf.streams.begin(), xdf.streams.end(), [](const XdfStreamData& stream) {
            return stream.role == PreviewStreamRole::ViconSegments;
        });
    const bool has_gaze = std::any_of(
        xdf.streams.begin(), xdf.streams.end(), [](const XdfStreamData& stream) {
            return stream.role == PreviewStreamRole::HoloLensGaze;
        });

    PreviewRecording recording;
    recording.source_frame_count = master->sample_count;
    recording.stored_frame_stride = master->stored_sample_stride;
    recording.source_start_timestamp = master->start_timestamp;
    recording.source_end_timestamp = master->end_timestamp;
    std::map<std::uint32_t, std::size_t> matched_samples;
    for (std::size_t master_index = 0; master_index < master->samples.size(); ++master_index) {
        const double absolute_timestamp = master->timestamps[master_index];
        PreviewFrame frame;
        frame.timestamp = absolute_timestamp - master->timestamps.front();
        frame.marker_stream_present = has_markers;
        frame.segment_stream_present = has_segments;
        frame.gaze_stream_present = has_gaze;

        for (const XdfStreamData& stream : xdf.streams) {
            if (!stream.numeric || stream.samples.empty()) {
                continue;
            }
            std::optional<std::size_t> sample_index;
            if (&stream == master) {
                sample_index = master_index;
            } else {
                sample_index = nearestSampleIndex(
                    stream, absolute_timestamp, match_tolerance_seconds);
            }
            if (sample_index && *sample_index < stream.samples.size()) {
                ++matched_samples[stream.stream_id];
                appendStreamSample(frame,
                                   stream,
                                   stream.samples[*sample_index],
                                   vicon_transform,
                                   resolved_gaze_transform);
            }
        }

        appendBoundedPreviewFrame(recording, std::move(frame),
                                  options.maximum_preview_frames,
                                  options.maximum_memory_bytes);
    }

    boundPreviewRecordingCache(recording, options.maximum_preview_frames,
                               options.maximum_memory_bytes);

    recording.summary = buildSummary(xdf,
                                     recording.frames.size(),
                                     automatically_calibrated,
                                     tracker_local_gaze,
                                     target_stream);
    for (const XdfStreamData& stream : xdf.streams) {
        if (&stream == master || stream.samples.empty()) continue;
        const double matched = static_cast<double>(matched_samples[stream.stream_id]);
        const double total = static_cast<double>(master->samples.size());
        const double unmatched_percent = total > 0.0 ? 100.0 * (1.0 - matched / total) : 0.0;
        std::ostringstream mapping_summary;
        mapping_summary << "; " << roleName(stream.role) << " unmatched "
                        << unmatched_percent << "%";
        recording.summary += mapping_summary.str();
    }
    recording.summary += "; " +
        std::to_string(recording.estimated_memory_bytes / (1024 * 1024)) +
        " MiB loaded, showing every " +
        std::to_string(recording.stored_frame_stride) + " frame(s)";
    return recording;
}

} // namespace vicon_lsl::preview_xdf_detail
