#include "preview/PreviewParsing.h"

#include "HoloLensGazePacket.h"
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

PreviewVec3 sampleVec3(const std::vector<double>& sample,
                       std::size_t x,
                       std::size_t y,
                       std::size_t z) {
    return {sample[x], sample[y], sample[z]};
}

std::optional<PreviewVec3> parseVec3(const std::vector<double>& sample,
                                     std::size_t x,
                                     std::size_t y,
                                     std::size_t z) {
    if (!finiteAt(sample, x) || !finiteAt(sample, y) || !finiteAt(sample, z)) {
        return std::nullopt;
    }
    return sampleVec3(sample, x, y, z);
}

} // namespace

PreviewStreamRole inferPreviewStreamRole(const PreviewStreamSchema& schema) {
    if (schema.name == "ViconMarkers") {
        return PreviewStreamRole::ViconMarkers;
    }
    if (schema.name == "ViconSegments") {
        return PreviewStreamRole::ViconSegments;
    }
    if (schema.name == "HoloLensGaze") {
        return PreviewStreamRole::HoloLensGaze;
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

    bool has_gaze = true;
    for (const auto& channel : holoLensGazeChannels()) {
        const std::string label(channel.label);
        if (!findIndex(schema.channel_labels, label, "_" + label)) {
            has_gaze = false;
            break;
        }
    }
    return has_gaze ? PreviewStreamRole::HoloLensGaze : PreviewStreamRole::Unknown;
}

std::vector<PreviewMarker> parseMarkerSample(const std::vector<std::string>& labels,
                                             const std::vector<double>& sample,
                                             const PreviewTransformProfile& transform) {
    std::unordered_map<std::string, std::size_t> label_to_index;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        label_to_index.emplace(labels[index], index);
    }

    std::vector<PreviewMarker> markers;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const std::string label = stripStreamPrefix(labels[index], "ViconMarkers_");
        if (!endsWith(label, ":X")) {
            continue;
        }
        const std::string root = label.substr(0, label.size() - 2);
        const auto y = label_to_index.find(root + ":Y");
        const auto z = label_to_index.find(root + ":Z");
        const auto valid = label_to_index.find(root + ":Valid");

        auto find_prefixed = [&](const std::string& suffix) {
            return label_to_index.find("ViconMarkers_" + root + suffix);
        };
        const auto y_prefixed = find_prefixed(":Y");
        const auto z_prefixed = find_prefixed(":Z");
        if (y == label_to_index.end() && y_prefixed == label_to_index.end()) {
            continue;
        }
        if (z == label_to_index.end() && z_prefixed == label_to_index.end()) {
            continue;
        }

        const std::size_t x_index = index;
        const std::size_t y_index = y != label_to_index.end() ? y->second : y_prefixed->second;
        const std::size_t z_index = z != label_to_index.end() ? z->second : z_prefixed->second;
        const auto valid_prefixed = find_prefixed(":Valid");
        const std::size_t valid_index = valid != label_to_index.end()
            ? valid->second
            : (valid_prefixed != label_to_index.end() ? valid_prefixed->second : sample.size());

        PreviewMarker marker;
        marker.name = displayNameForRoot(root);
        const auto position = parseVec3(sample, x_index, y_index, z_index);
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
    std::unordered_map<std::string, std::size_t> label_to_index;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        label_to_index.emplace(labels[index], index);
    }

    std::vector<PreviewSegment> segments;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const std::string label = stripStreamPrefix(labels[index], "ViconSegments_");
        if (!endsWith(label, ":X")) {
            continue;
        }
        const std::string root = label.substr(0, label.size() - 2);
        auto get = [&](const std::string& suffix) -> std::optional<std::size_t> {
            const auto direct = label_to_index.find(root + suffix);
            if (direct != label_to_index.end()) {
                return direct->second;
            }
            const auto prefixed = label_to_index.find("ViconSegments_" + root + suffix);
            if (prefixed != label_to_index.end()) {
                return prefixed->second;
            }
            return std::nullopt;
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
