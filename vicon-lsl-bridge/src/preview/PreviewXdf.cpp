#include "preview/PreviewXdf.h"

#include "preview/PreviewCalibration.h"
#include "preview/PreviewParsing.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vicon_lsl {
namespace {

// The summary spells out the target role in full.
std::string summaryRoleName(PreviewStreamRole role) {
    return role == PreviewStreamRole::HoloLensCalibrationTarget ? "calibration target"
                                                                : previewStreamRoleName(role);
}

// The sample closest in time to `absolute_timestamp`, if one is within the tolerance.
std::optional<std::size_t> nearestSampleIndex(const XdfStreamData& stream,
                                              double absolute_timestamp,
                                              double tolerance_seconds) {
    if (stream.timestamps.empty() || stream.samples.empty()) {
        return std::nullopt;
    }

    auto nearest = std::lower_bound(
        stream.timestamps.begin(), stream.timestamps.end(), absolute_timestamp);
    // Only the samples on either side can be closest. Ties use the later one.
    if (nearest == stream.timestamps.end() ||
        (nearest != stream.timestamps.begin() &&
         std::abs(*std::prev(nearest) - absolute_timestamp) <
             std::abs(*nearest - absolute_timestamp))) {
        --nearest;
    }
    const double delta = std::abs(*nearest - absolute_timestamp);
    if (std::isfinite(delta) && delta <= tolerance_seconds) {
        return static_cast<std::size_t>(nearest - stream.timestamps.begin());
    }
    return std::nullopt;
}

// The stream whose samples become the playback frames. Without a choice, prefers
// markers, then segments, then gaze.
const XdfStreamData* chooseMasterStream(const std::vector<XdfStreamData>& streams,
                                        std::uint32_t preferred_stream_id) {
    const auto usable = [](const XdfStreamData& stream, PreviewStreamRole role) {
        return stream.role == role && stream.numeric &&
               !stream.timestamps.empty() && !stream.samples.empty();
    };
    const PreviewStreamRole roles[] = {PreviewStreamRole::ViconMarkers,
                                       PreviewStreamRole::ViconSegments,
                                       PreviewStreamRole::HoloLensGaze};
    if (preferred_stream_id != 0) {
        for (const auto& stream : streams) {
            if (stream.stream_id != preferred_stream_id) continue;
            for (const PreviewStreamRole role : roles) {
                if (usable(stream, role)) return &stream;
            }
        }
        throw std::runtime_error("Selected XDF master timeline is not a supported preview stream");
    }
    for (const PreviewStreamRole role : roles) {
        for (const auto& stream : streams) {
            if (usable(stream, role)) return &stream;
        }
    }
    return nullptr;
}

void setStreamSample(PreviewFrame& frame,
                     const XdfStreamData& stream,
                     const std::vector<double>& sample,
                     const PreviewTransformProfile& vicon_transform,
                     const PreviewTransformProfile& gaze_transform) {
    // Stream mapping leaves one stream per role.
    if (stream.role == PreviewStreamRole::ViconMarkers) {
        frame.markers = parseMarkerSample(stream.channel_labels, sample, vicon_transform);
    } else if (stream.role == PreviewStreamRole::ViconSegments) {
        frame.segments = parseSegmentSample(stream.channel_labels, sample, vicon_transform);
    } else if (stream.role == PreviewStreamRole::HoloLensGaze) {
        frame.gaze_rays = parseGazeSample(stream.channel_labels, sample, gaze_transform);
    }
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
                << stream.channel_count << " channel(s), " << summaryRoleName(stream.role)
                << ", source " << (stream.source_id.empty() ? "<missing>" : stream.source_id)
                << ", range " << stream.start_timestamp << ".." << stream.end_timestamp
                << ", max gap " << stream.maximum_sample_gap
                << ", " << stream.clock_offsets.size()
                << " clock correction point(s)";
    }
    return summary.str();
}

// Builds one frame per master-stream sample and adds the nearest sample from
// every other stream within the time tolerance. Gaze is aligned from the
// recorded stair target when a stable pose is found.
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
    bool frozen_reference_calibration = false;
    const bool tracker_local_gaze =
        gaze_stream && lowerAscii(gaze_stream->coordinate_frame) == "eye_tracker_space";
    if (gaze_stream && target_stream &&
        calibrationCoordinateFramesCompatible(gaze_stream->coordinate_frame,
                                              target_stream->coordinate_frame)) {
        std::vector<CalibrationTargetPose> target_poses;
        target_poses.reserve(target_stream->samples.size());
        for (const auto& sample : target_stream->samples) {
            if (const auto pose = parseCalibrationTargetPose(target_stream->channel_labels, sample)) {
                target_poses.push_back(*pose);
            }
        }
        const auto solution = solveStableTrackedTargetCalibration(
            target_poses, defaultStairCalibrationProfile());
        if (solution) {
            resolved_gaze_transform = gazeTransformFromTargetCalibration(
                defaultStairCalibrationProfile(), solution->holo_from_target);
            automatically_calibrated = true;
            frozen_reference_calibration = solution->uses_frozen_reference;
        }
    }

    const auto has_role = [&xdf](PreviewStreamRole role) {
        return std::any_of(xdf.streams.begin(), xdf.streams.end(),
                           [role](const XdfStreamData& stream) { return stream.role == role; });
    };
    const bool has_markers = has_role(PreviewStreamRole::ViconMarkers);
    const bool has_segments = has_role(PreviewStreamRole::ViconSegments);
    const bool has_gaze = has_role(PreviewStreamRole::HoloLensGaze);

    PreviewRecording recording;
    if (gaze_stream && !automatically_calibrated) {
        recording.calibration_warning = tracker_local_gaze
            ? "Gaze is in legacy tracker-local coordinates and cannot be aligned from the stair target."
            : "No usable stair calibration in this recording. Gaze alignment with Vicon is unverified; record a stable stair reference or apply a calibration saved for this HoloLens session before opening the file.";
    }
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
            const std::optional<std::size_t> sample_index = &stream == master
                ? std::optional<std::size_t>(master_index)
                : nearestSampleIndex(stream, absolute_timestamp, match_tolerance_seconds);
            if (sample_index && *sample_index < stream.samples.size()) {
                ++matched_samples[stream.stream_id];
                setStreamSample(frame, stream, stream.samples[*sample_index],
                                vicon_transform, resolved_gaze_transform);
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
    if (frozen_reference_calibration) {
        recording.summary += "; using frozen stair reference (Vuforia paused)";
    }
    if (!recording.calibration_warning.empty()) {
        recording.summary += "; WARNING: " + recording.calibration_warning;
    }
    for (const XdfStreamData& stream : xdf.streams) {
        if (&stream == master || stream.samples.empty()) continue;
        const double matched = static_cast<double>(matched_samples[stream.stream_id]);
        const double total = static_cast<double>(master->samples.size());
        const double unmatched_percent = total > 0.0 ? 100.0 * (1.0 - matched / total) : 0.0;
        std::ostringstream mapping_summary;
        mapping_summary << "; " << summaryRoleName(stream.role) << " unmatched "
                        << unmatched_percent << "%";
        recording.summary += mapping_summary.str();
    }
    recording.summary += "; " +
        std::to_string(recording.estimated_memory_bytes / (1024 * 1024)) +
        " MiB loaded, showing every " +
        std::to_string(recording.stored_frame_stride) + " frame(s)";
    return recording;
}

// Lists which stream groups were selected or left out, for the load summary.
std::string mappingDecisionSummary(const XdfMappingAnalysis& analysis,
                                   const XdfStreamMapping& requested) {
    const XdfStreamMapping& effective =
        requested.selected_stream_ids.empty() ? analysis.suggested_mapping : requested;
    const std::set<std::uint32_t> selected_ids(
        effective.selected_stream_ids.begin(), effective.selected_stream_ids.end());
    std::set<std::string> selected_groups;
    std::map<std::string, std::vector<const XdfStreamCandidate*>> groups;
    for (const auto& candidate : analysis.candidates) {
        groups[candidate.group_key].push_back(&candidate);
        if (selected_ids.count(candidate.stream_id) != 0) {
            selected_groups.insert(candidate.group_key);
        }
    }

    const std::uint32_t master_id = requested.master_stream_id != 0
        ? requested.master_stream_id : analysis.suggested_mapping.master_stream_id;
    std::ostringstream summary;
    summary << "; mapping master stream " << master_id;
    for (const auto& [key, candidates] : groups) {
        const bool selected = selected_groups.count(key) != 0;
        summary << "; " << (selected ? "selected " : "excluded ")
                << previewStreamRoleName(candidates.front()->role) << " stream";
        if (candidates.size() > 1) summary << " instances";
        summary << " ";
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (index != 0) summary << ',';
            summary << candidates[index]->stream_id << ':'
                    << candidates[index]->display_name << '['
                    << (candidates[index]->source_id.empty()
                            ? "source-missing" : candidates[index]->source_id)
                    << ']';
        }
        if (selected && candidates.size() > 1) summary << " stitched";
    }
    return summary.str();
}

} // namespace

PreviewRecording buildXdfPreviewRecording(const XdfLoadResult& xdf,
                                          const PreviewTransformProfile& vicon_transform,
                                          const PreviewTransformProfile& gaze_transform,
                                          double match_tolerance_seconds,
                                          const XdfStreamMapping& mapping,
                                          const PreviewLoadOptions& options) {
    const XdfMappingAnalysis analysis = analyzeXdfStreamMapping(xdf);
    if (analysis.candidates.empty() ||
        (analysis.requires_explicit_mapping && mapping.selected_stream_ids.empty())) {
        throw std::runtime_error(analysis.explanation);
    }
    const XdfLoadResult selected = applyXdfStreamMapping(
        xdf, mapping, options.maximum_preview_frames);
    PreviewRecording recording = assembleRecording(
        selected, vicon_transform, gaze_transform, match_tolerance_seconds,
        mapping.master_stream_id, options);
    recording.summary += "; " + analysis.explanation;
    recording.summary += mappingDecisionSummary(analysis, mapping);
    return recording;
}

PreviewRecording loadXdfPreviewRecording(const std::string& path,
                                         const PreviewTransformProfile& vicon_transform,
                                         const PreviewTransformProfile& gaze_transform,
                                         double match_tolerance_seconds,
                                         const PreviewLoadOptions& options,
                                         const XdfStreamMapping& mapping) {
    return buildXdfPreviewRecording(loadXdfNumericStreams(path, options),
                                    vicon_transform,
                                    gaze_transform,
                                    match_tolerance_seconds,
                                    mapping,
                                    options);
}

} // namespace vicon_lsl
