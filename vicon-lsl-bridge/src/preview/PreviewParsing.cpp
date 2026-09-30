#include "preview/PreviewParsing.h"

#include "HoloLensGazeSchema.h"
#include "HoloLensModelTargetSchema.h"
#include "StreamDefaults.generated.h"
#include "preview/PreviewMath.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>

#include <locale.h>
#include <stdlib.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace vicon_lsl {
namespace {

// A "C" numeric locale made once, so a number reads the same whatever locale
// the program set (Qt sets the user's own, where "0.5" can be written "0,5").
#if defined(_WIN32)
_locale_t numericCLocale() {
    static const _locale_t locale = _create_locale(LC_NUMERIC, "C");
    return locale;
}
#else
locale_t numericCLocale() {
    static const locale_t locale = newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(0));
    return locale;
}
#endif

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

// "Subject:Marker" is shown as "Marker".
std::string displayNameForRoot(const std::string& root) {
    const std::size_t colon = root.rfind(':');
    if (colon == std::string::npos || colon + 1 >= root.size()) {
        return root;
    }
    return root.substr(colon + 1);
}

// Finds the first label equal to `exact`, or else the first ending in `suffix`.
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

// Maps each label to its first position.
using LabelIndex = std::unordered_map<std::string, std::size_t>;

LabelIndex indexLabels(const std::vector<std::string>& labels) {
    LabelIndex result;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        result.emplace(labels[index], index);
    }
    return result;
}

// Looks up `name` as written, then with the stream-name prefix used in CSV files.
std::optional<std::size_t> findChannel(const LabelIndex& labels,
                                       const std::string& name,
                                       const std::string& prefix) {
    auto found = labels.find(name);
    if (found == labels.end()) found = labels.find(prefix + name);
    if (found == labels.end()) return std::nullopt;
    return found->second;
}

template <class Channels>
std::vector<std::string> labelsOf(const Channels& channels) {
    std::vector<std::string> labels;
    for (const auto& channel : channels) {
        labels.emplace_back(channel.label);
    }
    return labels;
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

const char* previewStreamRoleName(PreviewStreamRole role) {
    switch (role) {
        case PreviewStreamRole::ViconMarkers: return "markers";
        case PreviewStreamRole::ViconSegments: return "segments";
        case PreviewStreamRole::HoloLensGaze: return "gaze";
        case PreviewStreamRole::HoloLensCalibrationTarget: return "calibration";
        case PreviewStreamRole::Unknown: return "unknown";
    }
    return "unknown";
}

std::vector<std::string> canonicalPreviewChannelLabels(PreviewStreamRole role,
                                                       std::size_t channel_count) {
    if (role == PreviewStreamRole::HoloLensGaze &&
        channel_count == kHoloLensGazeChannelCount) {
        return labelsOf(holoLensGazeChannels());
    }
    if (role == PreviewStreamRole::HoloLensCalibrationTarget &&
        channel_count == kHoloLensModelTargetChannelCount) {
        return labelsOf(holoLensModelTargetChannels());
    }
    return {};
}

std::vector<PreviewMarker> parseMarkerSample(const std::vector<std::string>& labels,
                                             const std::vector<double>& sample,
                                             const PreviewTransformProfile& transform) {
    const auto label_to_index = indexLabels(labels);
    const double nan = std::numeric_limits<double>::quiet_NaN();

    std::vector<PreviewMarker> markers;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const std::string label = stripStreamPrefix(labels[index], "ViconMarkers_");
        if (!endsWith(label, ":X")) {
            continue;
        }
        const std::string root = label.substr(0, label.size() - 2);
        const auto channel = [&](const char* suffix) {
            return findChannel(label_to_index, root + suffix, "ViconMarkers_");
        };
        const auto y = channel(":Y");
        const auto z = channel(":Z");
        if (!y || !z) continue;
        // A root with rotation channels is a segment, which merged CSV files
        // list beside the markers.
        if (channel(":QX") && channel(":QY") && channel(":QZ") && channel(":QW")) continue;
        // A missing Valid channel counts as valid.
        const auto valid_index = channel(":Valid").value_or(sample.size());

        PreviewMarker marker;
        marker.name = displayNameForRoot(root);
        const auto position = parseVec3(sample, index, *y, *z);
        marker.valid = position.has_value() &&
                       (valid_index >= sample.size() || sample[valid_index] > 0.5);
        marker.position = marker.valid ? applyTransformPoint(transform, *position)
                                       : PreviewVec3{nan, nan, nan};
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
        const auto channel = [&](const char* suffix) {
            return findChannel(label_to_index, root + suffix, "ViconSegments_");
        };

        const auto y = channel(":Y");
        const auto z = channel(":Z");
        const auto qx = channel(":QX");
        const auto qy = channel(":QY");
        const auto qz = channel(":QZ");
        const auto qw = channel(":QW");
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
    std::vector<PreviewGazeRay> rays;
    for (const std::string name : {"Combined", "LeftEye", "RightEye"}) {
        // Labels may carry a stream-name prefix such as "HoloLensGaze_".
        const auto channel = [&](const char* field) {
            const std::string label = name + field;
            return findIndex(labels, label, "_" + label);
        };
        const auto ox = channel("OriginX");
        const auto oy = channel("OriginY");
        const auto oz = channel("OriginZ");
        const auto dx = channel("DirectionX");
        const auto dy = channel("DirectionY");
        const auto dz = channel("DirectionZ");
        const auto valid = channel("Valid");
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

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

double parseCNumber(const std::string& text, std::size_t* consumed) {
    const char* begin = text.c_str();
    char* end = const_cast<char*>(begin);
    double value = 0.0;
    if (const auto locale = numericCLocale()) {
#if defined(_WIN32)
        value = _strtod_l(begin, &end, locale);
#else
        value = strtod_l(begin, &end, locale);
#endif
    } else {
        value = std::strtod(begin, &end);
    }
    if (consumed) *consumed = static_cast<std::size_t>(end - begin);
    return end == begin ? std::numeric_limits<double>::quiet_NaN() : value;
}

} // namespace vicon_lsl
