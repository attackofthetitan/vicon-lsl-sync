#include "preview/PreviewXdf.h"

#include "preview/PreviewCalibration.h"
#include "preview/PreviewParsing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace vicon_lsl {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool containsCaseInsensitive(const std::string& value, const std::string& needle) {
    return lower(value).find(lower(needle)) != std::string::npos;
}

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

const XdfStreamData* chooseMasterStream(const std::vector<XdfStreamData>& streams) {
    auto usable = [](const XdfStreamData& stream) {
        return stream.numeric && !stream.timestamps.empty() && !stream.samples.empty();
    };
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
        if (usable(stream) && containsCaseInsensitive(stream.name, "Vicon")) {
            return &stream;
        }
    }
    for (const auto& stream : streams) {
        if (usable(stream) && stream.role == PreviewStreamRole::HoloLensGaze) {
            return &stream;
        }
    }
    for (const auto& stream : streams) {
        if (usable(stream)) {
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

} // namespace

PreviewRecording buildXdfPreviewRecording(const XdfLoadResult& xdf,
                                          const PreviewTransformProfile& vicon_transform,
                                          const PreviewTransformProfile& gaze_transform,
                                          double match_tolerance_seconds) {
    const XdfStreamData* master = chooseMasterStream(xdf.streams);
    if (!master) {
        throw std::runtime_error("XDF contains no numeric streams with samples");
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
            resolved_gaze_transform = transformProfileFromRigid(
                composeRigidTransforms(
                    defaultStairCalibrationProfile().vicon_from_target,
                    inverseRigidTransform(solution->holo_from_target)),
                "HoloLens");
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
                appendStreamSample(frame,
                                   stream,
                                   stream.samples[*sample_index],
                                   vicon_transform,
                                   resolved_gaze_transform);
            }
        }

        recording.frames.push_back(std::move(frame));
    }

    std::ostringstream summary;
    summary << xdf.streams.size() << " stream(s), " << recording.frames.size() << " frame(s)";
    if (automatically_calibrated) {
        summary << "; stair-target calibration applied";
    }
    for (const auto& stream : xdf.streams) {
        summary << "; "
                << (stream.name.empty() ? "stream_" + std::to_string(stream.stream_id) : stream.name)
                << ": " << stream.sample_count << " sample(s), "
                << stream.channel_count << " channel(s), " << roleName(stream.role);
    }
    recording.summary = summary.str();
    return recording;
}

PreviewRecording loadXdfPreviewRecording(const std::string& path,
                                         const PreviewTransformProfile& vicon_transform,
                                         const PreviewTransformProfile& gaze_transform,
                                         double match_tolerance_seconds) {
    return buildXdfPreviewRecording(loadXdfNumericStreams(path),
                                    vicon_transform,
                                    gaze_transform,
                                    match_tolerance_seconds);
}

} // namespace vicon_lsl
