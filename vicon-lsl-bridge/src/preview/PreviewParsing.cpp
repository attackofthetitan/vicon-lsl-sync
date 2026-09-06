#include "preview/PreviewParsing.h"

#include "HoloLensGazeSchema.h"
#include "HoloLensModelTargetSchema.h"
#include "StreamDefaults.h"
#include "preview/PreviewMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>

namespace vicon_lsl {
namespace {

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string stripStreamPrefix(std::string label, const std::string& stream_prefix) {
    if (label.rfind(stream_prefix, 0) == 0) {
        label.erase(0, stream_prefix.size());
    }
    return label;
}

std::string displayNameForRoot(const std::string& root) {
    const std::size_t colon = root.rfind(':');
    if (colon == std::string::npos || colon + 1 >= root.size()) {
        return root;
    }
    return root.substr(colon + 1);
}

std::optional<std::size_t> findIndex(const std::vector<std::string>& labels,
                                     const std::string& exact,
                                     const std::string& suffix = {}) {
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (labels[index] == exact) {
            return index;
        }
    }
    if (!suffix.empty()) {
        for (std::size_t index = 0; index < labels.size(); ++index) {
            if (endsWith(labels[index], suffix)) {
                return index;
            }
        }
    }
    return std::nullopt;
}

bool finiteAt(const std::vector<double>& sample, std::size_t index) {
    return index < sample.size() && std::isfinite(sample[index]);
}

std::optional<PreviewVec3> parseVec3(const std::vector<double>& sample,
                                     std::size_t x,
                                     std::size_t y,
                                     std::size_t z) {
    if (!finiteAt(sample, x) || !finiteAt(sample, y) || !finiteAt(sample, z)) {
        return std::nullopt;
    }
    return PreviewVec3{sample[x], sample[y], sample[z]};
}

using LabelIndex = std::unordered_map<std::string, std::size_t>;

LabelIndex indexLabels(const std::vector<std::string>& labels) {
    LabelIndex result;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        result.emplace(labels[index], index);
    }
    return result;
}

std::optional<std::size_t> findChannel(const LabelIndex& labels,
                                      const std::string& name,
                                      const std::string& prefix) {
    auto found = labels.find(name);
    if (found == labels.end()) found = labels.find(prefix + name);
    if (found == labels.end()) return std::nullopt;
    return found->second;
}

} // namespace

PreviewStreamRole inferPreviewStreamRole(const PreviewStreamSchema& schema) {
    if (schema.name == stream_defaults::ViconMarkers) {
        return PreviewStreamRole::ViconMarkers;
    }
    if (schema.name == stream_defaults::ViconSegments) {
        return PreviewStreamRole::ViconSegments;
    }
    if (schema.name == stream_defaults::HoloLensGaze) {
        return PreviewStreamRole::HoloLensGaze;
    }
    if (schema.name == stream_defaults::HoloLensModelTargetPose) {
        return PreviewStreamRole::HoloLensCalibrationTarget;
    }

    const auto& target_channels = holoLensModelTargetChannels();
    if (std::all_of(target_channels.begin(), target_channels.end(), [&](const auto& channel) {
            return findIndex(schema.channel_labels, std::string(channel.label)).has_value();
        })) {
        return PreviewStreamRole::HoloLensCalibrationTarget;
    }

    const auto has_marker_suffix = std::any_of(schema.channel_labels.begin(),
                                               schema.channel_labels.end(),
                                               [](const std::string& label) {
                                                   return endsWith(label, ":Valid");
                                               });
    if (schema.type == "MoCap" && has_marker_suffix) {
        return PreviewStreamRole::ViconMarkers;
    }
    if (schema.type == "MoCap" && schema.channel_labels.size() % 7 == 0) {
        return PreviewStreamRole::ViconSegments;
    }

    for (const auto& channel : holoLensGazeChannels()) {
        const std::string label(channel.label);
        if (!findIndex(schema.channel_labels, label, "_" + label)) {
            return PreviewStreamRole::Unknown;
        }
    }
    return PreviewStreamRole::HoloLensGaze;
}

std::vector<std::string> canonicalPreviewChannelLabels(PreviewStreamRole role,
                                                       std::size_t channel_count) {
    if (role == PreviewStreamRole::HoloLensGaze &&
        channel_count == kHoloLensGazeChannelCount) {
        std::vector<std::string> labels;
        labels.reserve(kHoloLensGazeChannelCount);
        for (const auto& channel : holoLensGazeChannels()) {
            labels.emplace_back(channel.label);
        }
        return labels;
    }
    if (role == PreviewStreamRole::HoloLensCalibrationTarget &&
        channel_count == kHoloLensModelTargetChannelCount) {
        std::vector<std::string> labels;
        labels.reserve(kHoloLensModelTargetChannelCount);
        for (const auto& channel : holoLensModelTargetChannels()) {
            labels.emplace_back(channel.label);
        }
        return labels;
    }
    return {};
}

std::vector<PreviewMarker> parseMarkerSample(const std::vector<std::string>& labels,
                                             const std::vector<double>& sample,
                                             const PreviewTransformProfile& transform) {
    const auto label_to_index = indexLabels(labels);

    std::vector<PreviewMarker> markers;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const std::string label = stripStreamPrefix(labels[index], "ViconMarkers_");
        if (!endsWith(label, ":X")) {
            continue;
        }
        const std::string root = label.substr(0, label.size() - 2);
        const auto y = findChannel(label_to_index, root + ":Y", "ViconMarkers_");
        const auto z = findChannel(label_to_index, root + ":Z", "ViconMarkers_");
        const auto valid = findChannel(label_to_index, root + ":Valid", "ViconMarkers_");
        if (!y || !z) continue;
        const auto valid_index = valid.value_or(sample.size());

        PreviewMarker marker;
        marker.name = displayNameForRoot(root);
        const auto position = parseVec3(sample, index, *y, *z);
        marker.valid = position.has_value() &&
                       (valid_index >= sample.size() || sample[valid_index] > 0.5);
        if (marker.valid) {
            marker.position = applyTransformPoint(transform, *position);
        } else {
            marker.position = {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::quiet_NaN()};
        }
        markers.push_back(std::move(marker));
    }
    return markers;
}

std::vector<PreviewSegment> parseSegmentSample(const std::vector<std::string>& labels,
                                               const std::vector<double>& sample,
                                               const PreviewTransformProfile& transform) {
    const auto label_to_index = indexLabels(labels);

    std::vector<PreviewSegment> segments;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const std::string label = stripStreamPrefix(labels[index], "ViconSegments_");
        if (!endsWith(label, ":X")) {
            continue;
        }
        const std::string root = label.substr(0, label.size() - 2);
        auto get = [&](const std::string& suffix) {
            return findChannel(label_to_index, root + suffix, "ViconSegments_");
        };

        const auto y = get(":Y");
        const auto z = get(":Z");
        const auto qx = get(":QX");
        const auto qy = get(":QY");
        const auto qz = get(":QZ");
        const auto qw = get(":QW");
        if (!y || !z || !qx || !qy || !qz || !qw) {
            continue;
        }

        PreviewSegment segment;
        segment.name = displayNameForRoot(root);
        const auto position = parseVec3(sample, index, *y, *z);
        segment.valid = position.has_value() &&
                        finiteAt(sample, *qx) && finiteAt(sample, *qy) &&
                        finiteAt(sample, *qz) && finiteAt(sample, *qw);
        if (segment.valid) {
            segment.position = applyTransformPoint(transform, *position);
            segment.rotation = {sample[*qx], sample[*qy], sample[*qz], sample[*qw]};
        }
        segments.push_back(std::move(segment));
    }
    return segments;
}

std::vector<PreviewGazeRay> parseGazeSample(const std::vector<std::string>& labels,
                                            const std::vector<double>& sample,
                                            const PreviewTransformProfile& transform) {
    const std::string names[] = {"Combined", "LeftEye", "RightEye"};
    std::vector<PreviewGazeRay> rays;

    for (const std::string& name : names) {
        const auto ox = findIndex(labels, name + "OriginX", "_" + name + "OriginX");
        const auto oy = findIndex(labels, name + "OriginY", "_" + name + "OriginY");
        const auto oz = findIndex(labels, name + "OriginZ", "_" + name + "OriginZ");
        const auto dx = findIndex(labels, name + "DirectionX", "_" + name + "DirectionX");
        const auto dy = findIndex(labels, name + "DirectionY", "_" + name + "DirectionY");
        const auto dz = findIndex(labels, name + "DirectionZ", "_" + name + "DirectionZ");
        const auto valid = findIndex(labels, name + "Valid", "_" + name + "Valid");
        if (!ox || !oy || !oz || !dx || !dy || !dz) {
            continue;
        }

        PreviewGazeRay ray;
        ray.name = name;
        const auto origin = parseVec3(sample, *ox, *oy, *oz);
        const auto direction = parseVec3(sample, *dx, *dy, *dz);
        ray.valid = origin.has_value() &&
                    direction.has_value() &&
                    (!valid || (*valid < sample.size() && sample[*valid] > 0.5)) &&
                    length(*direction) > 1e-12;
        if (ray.valid) {
            ray.origin = applyTransformPoint(transform, *origin);
            ray.direction = applyTransformDirection(transform, *direction);
        }
        rays.push_back(std::move(ray));
    }

    return rays;
}

} // namespace vicon_lsl
