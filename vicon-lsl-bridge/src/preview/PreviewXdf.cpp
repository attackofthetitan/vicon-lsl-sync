#include "preview/PreviewXdf.h"

#include "preview/PreviewXdfPrivate.h"

#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

std::string mappingRoleName(vicon_lsl::PreviewStreamRole role) {
    switch (role) {
        case vicon_lsl::PreviewStreamRole::ViconMarkers: return "markers";
        case vicon_lsl::PreviewStreamRole::ViconSegments: return "segments";
        case vicon_lsl::PreviewStreamRole::HoloLensGaze: return "gaze";
        case vicon_lsl::PreviewStreamRole::HoloLensCalibrationTarget:
            return "calibration";
        case vicon_lsl::PreviewStreamRole::Unknown: return "unknown";
    }
    return "unknown";
}

std::string mappingDecisionSummary(
    const vicon_lsl::XdfMappingAnalysis& analysis,
    const vicon_lsl::XdfStreamMapping& requested) {
    const vicon_lsl::XdfStreamMapping& effective =
        requested.selected_stream_ids.empty() ? analysis.suggested_mapping : requested;
    std::set<std::uint32_t> selected_ids(
        effective.selected_stream_ids.begin(), effective.selected_stream_ids.end());
    std::set<std::string> selected_groups;
    for (const auto& candidate : analysis.candidates) {
        if (selected_ids.find(candidate.stream_id) != selected_ids.end()) {
            selected_groups.insert(candidate.group_key);
        }
    }

    const std::uint32_t master_id = requested.master_stream_id != 0
        ? requested.master_stream_id : analysis.suggested_mapping.master_stream_id;
    std::ostringstream summary;
    summary << "; mapping master stream " << master_id;

    std::map<std::string, std::vector<const vicon_lsl::XdfStreamCandidate*>> groups;
    for (const auto& candidate : analysis.candidates) {
        groups[candidate.group_key].push_back(&candidate);
    }
    for (const auto& item : groups) {
        const auto& candidates = item.second;
        if (candidates.empty()) continue;
        const bool selected = selected_groups.find(item.first) != selected_groups.end();
        summary << "; " << (selected ? "selected " : "excluded ")
                << mappingRoleName(candidates.front()->role) << " stream";
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

namespace vicon_lsl {

PreviewRecording buildXdfPreviewRecording(const XdfLoadResult& xdf,
                                          const PreviewTransformProfile& vicon_transform,
                                          const PreviewTransformProfile& gaze_transform,
                                          double match_tolerance_seconds,
                                          const XdfStreamMapping& mapping,
                                          const PreviewLoadOptions& options) {
    const XdfMappingAnalysis analysis = analyzeXdfStreamMapping(xdf);
    if (analysis.candidates.empty()) {
        throw std::runtime_error(analysis.explanation);
    }
    if (analysis.requires_explicit_mapping && mapping.selected_stream_ids.empty()) {
        throw std::runtime_error(analysis.explanation);
    }
    const XdfLoadResult selected = applyXdfStreamMapping(
        xdf, mapping, options.maximum_preview_frames);
    PreviewRecording recording = preview_xdf_detail::assembleRecording(
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
