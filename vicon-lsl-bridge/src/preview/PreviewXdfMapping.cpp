#include "preview/PreviewXdfMapping.h"

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace vicon_lsl {
namespace {

bool supportedRole(PreviewStreamRole role) {
    return role == PreviewStreamRole::ViconMarkers ||
           role == PreviewStreamRole::ViconSegments ||
           role == PreviewStreamRole::HoloLensGaze ||
           role == PreviewStreamRole::HoloLensCalibrationTarget;
}

std::string roleName(PreviewStreamRole role) {
    switch (role) {
        case PreviewStreamRole::ViconMarkers: return "markers";
        case PreviewStreamRole::ViconSegments: return "segments";
        case PreviewStreamRole::HoloLensGaze: return "gaze";
        case PreviewStreamRole::HoloLensCalibrationTarget: return "calibration";
        case PreviewStreamRole::Unknown: return "unknown";
    }
    return "unknown";
}

std::string schemaKey(const XdfStreamData& stream) {
    std::ostringstream key;
    key << stream.channel_count << '|' << stream.channel_format << '|'
        << stream.coordinate_frame;
    for (const std::string& label : stream.channel_labels) key << '|' << label;
    return key.str();
}

std::string groupKey(const XdfStreamData& stream) {
    std::ostringstream key;
    key << roleName(stream.role) << '|';
    if (!stream.source_id.empty()) {
        key << "source:" << stream.source_id << "|name:" << stream.name
            << "|host:" << stream.hostname;
    } else {
        key << "name:" << stream.name << "|host:" << stream.hostname
            << "|session:" << stream.session_id;
    }
    key << "|schema:" << schemaKey(stream);
    return key.str();
}

int rolePriority(PreviewStreamRole role) {
    switch (role) {
        case PreviewStreamRole::ViconMarkers: return 0;
        case PreviewStreamRole::ViconSegments: return 1;
        case PreviewStreamRole::HoloLensGaze: return 2;
        case PreviewStreamRole::HoloLensCalibrationTarget: return 3;
        case PreviewStreamRole::Unknown: return 4;
    }
    return 4;
}

struct Group {
    std::string key;
    PreviewStreamRole role = PreviewStreamRole::Unknown;
    std::vector<const XdfStreamData*> streams;
};

std::vector<Group> groupsFor(const XdfLoadResult& xdf) {
    std::map<std::string, Group> groups;
    for (const XdfStreamData& stream : xdf.streams) {
        if (!stream.numeric || stream.samples.empty() || !supportedRole(stream.role)) continue;
        const std::string key = groupKey(stream);
        Group& group = groups[key];
        group.key = key;
        group.role = stream.role;
        group.streams.push_back(&stream);
    }
    std::vector<Group> result;
    for (auto& item : groups) {
        std::stable_sort(item.second.streams.begin(), item.second.streams.end(),
            [](const XdfStreamData* left, const XdfStreamData* right) {
                return std::tie(left->start_timestamp, left->stream_id) <
                       std::tie(right->start_timestamp, right->stream_id);
            });
        result.push_back(std::move(item.second));
    }
    std::stable_sort(result.begin(), result.end(), [](const Group& left, const Group& right) {
        return std::tie(left.role, left.key) < std::tie(right.role, right.key);
    });
    return result;
}

std::size_t groupSampleCount(const Group& group) {
    std::size_t count = 0;
    for (const XdfStreamData* stream : group.streams) count += stream->sample_count;
    return count;
}

const Group* chooseSuggestedGroup(const std::vector<Group>& groups, PreviewStreamRole role) {
    const Group* best = nullptr;
    for (const Group& group : groups) {
        if (group.role != role) continue;
        if (!best || groupSampleCount(group) > groupSampleCount(*best) ||
            (groupSampleCount(group) == groupSampleCount(*best) && group.key < best->key)) {
            best = &group;
        }
    }
    return best;
}

XdfStreamData stitchGroup(const Group& group, std::size_t maximum_samples) {
    XdfStreamData result = *group.streams.front();
    result.stream_id = group.streams.front()->stream_id;
    result.timestamps.clear();
    result.samples.clear();
    result.clock_offsets.clear();
    result.sample_count = 0;
    result.repaired_timestamp_count = 0;
    result.large_gap_count = 0;
    result.maximum_sample_gap = 0.0;
    result.stored_sample_stride = 1;

    struct Cursor {
        double timestamp = 0.0;
        std::size_t stream = 0;
        std::size_t sample = 0;
        bool operator>(const Cursor& other) const {
            return std::tie(timestamp, stream, sample) >
                   std::tie(other.timestamp, other.stream, other.sample);
        }
    };
    std::priority_queue<Cursor, std::vector<Cursor>, std::greater<Cursor>> heap;
    for (std::size_t stream_index = 0; stream_index < group.streams.size(); ++stream_index) {
        const XdfStreamData& stream = *group.streams[stream_index];
        result.sample_count += stream.sample_count;
        result.repaired_timestamp_count += stream.repaired_timestamp_count;
        result.large_gap_count += stream.large_gap_count;
        result.maximum_sample_gap = (std::max)(result.maximum_sample_gap, stream.maximum_sample_gap);
        result.clock_offsets.insert(result.clock_offsets.end(),
                                    stream.clock_offsets.begin(), stream.clock_offsets.end());
        if (!stream.timestamps.empty() && !stream.samples.empty()) {
            heap.push({stream.timestamps.front(), stream_index, 0});
        }
    }

    maximum_samples = (std::max)(std::size_t{2}, maximum_samples);
    std::size_t merged_index = 0;
    std::vector<double> pending_sample;
    double pending_timestamp = 0.0;
    bool have_pending = false;
    while (!heap.empty()) {
        const Cursor cursor = heap.top();
        heap.pop();
        const XdfStreamData& stream = *group.streams[cursor.stream];
        if (result.samples.size() >= maximum_samples) {
            std::size_t output = 0;
            for (std::size_t input = 0; input < result.samples.size(); input += 2) {
                if (output != input) {
                    result.samples[output] = std::move(result.samples[input]);
                    result.timestamps[output] = result.timestamps[input];
                }
                ++output;
            }
            result.samples.resize(output);
            result.timestamps.resize(output);
            result.stored_sample_stride *= 2;
        }
        if (merged_index % result.stored_sample_stride == 0) {
            if (result.timestamps.empty() || cursor.timestamp > result.timestamps.back()) {
                result.timestamps.push_back(cursor.timestamp);
                result.samples.push_back(stream.samples[cursor.sample]);
            }
            have_pending = false;
            pending_sample.clear();
        } else {
            pending_timestamp = cursor.timestamp;
            pending_sample = stream.samples[cursor.sample];
            have_pending = true;
        }
        ++merged_index;
        const std::size_t next = cursor.sample + 1;
        if (next < stream.timestamps.size() && next < stream.samples.size()) {
            heap.push({stream.timestamps[next], cursor.stream, next});
        }
    }
    if (have_pending) {
        if (result.samples.size() >= maximum_samples && !result.samples.empty()) {
            result.samples.back() = std::move(pending_sample);
            result.timestamps.back() = pending_timestamp;
        } else {
            result.samples.push_back(std::move(pending_sample));
            result.timestamps.push_back(pending_timestamp);
        }
    }
    if (!result.timestamps.empty()) {
        result.start_timestamp = result.timestamps.front();
        result.end_timestamp = result.timestamps.back();
    }
    return result;
}

} // namespace

XdfMappingAnalysis analyzeXdfStreamMapping(const XdfLoadResult& xdf) {
    XdfMappingAnalysis analysis;
    const std::vector<Group> groups = groupsFor(xdf);
    std::map<PreviewStreamRole, int> group_counts;
    for (const Group& group : groups) {
        ++group_counts[group.role];
        for (const XdfStreamData* stream : group.streams) {
            analysis.candidates.push_back({
                stream->stream_id, stream->role, group.key,
                stream->name.empty() ? "stream_" + std::to_string(stream->stream_id) : stream->name,
                stream->source_id, stream->hostname, stream->session_id,
                stream->sample_count, stream->start_timestamp, stream->end_timestamp,
            });
        }
    }
    for (const auto& item : group_counts) {
        if (item.second > 1) analysis.requires_explicit_mapping = true;
    }

    const PreviewStreamRole roles[] = {
        PreviewStreamRole::ViconMarkers, PreviewStreamRole::ViconSegments,
        PreviewStreamRole::HoloLensGaze, PreviewStreamRole::HoloLensCalibrationTarget,
    };
    for (PreviewStreamRole role : roles) {
        const Group* group = chooseSuggestedGroup(groups, role);
        if (!group) continue;
        analysis.suggested_mapping.selected_stream_ids.push_back(group->streams.front()->stream_id);
        if (analysis.suggested_mapping.master_stream_id == 0 && rolePriority(role) <= 2) {
            analysis.suggested_mapping.master_stream_id = group->streams.front()->stream_id;
        }
    }
    if (groups.empty()) {
        analysis.explanation = "The XDF contains no supported marker, segment, or gaze preview stream.";
    } else if (analysis.requires_explicit_mapping) {
        analysis.explanation = "Multiple incompatible candidates exist for at least one preview role; choose the intended identity and master timeline.";
    } else {
        analysis.explanation = "Compatible recovered stream instances will be stitched by source identity and schema.";
    }
    return analysis;
}

XdfLoadResult applyXdfStreamMapping(const XdfLoadResult& xdf,
                                    const XdfStreamMapping& mapping,
                                    std::size_t maximum_samples_per_stream) {
    const std::vector<Group> groups = groupsFor(xdf);
    if (groups.empty()) {
        throw std::runtime_error("XDF contains no supported preview stream");
    }
    std::set<std::uint32_t> requested(mapping.selected_stream_ids.begin(),
                                      mapping.selected_stream_ids.end());
    std::map<PreviewStreamRole, std::vector<const Group*>> by_role;
    for (const Group& group : groups) by_role[group.role].push_back(&group);

    std::vector<const Group*> selected_groups;
    for (const auto& item : by_role) {
        const Group* selected = nullptr;
        for (const Group* group : item.second) {
            const bool requested_group = std::any_of(group->streams.begin(), group->streams.end(),
                [&requested](const XdfStreamData* stream) {
                    return requested.find(stream->stream_id) != requested.end();
                });
            if (requested_group) {
                if (selected && selected != group) {
                    throw std::runtime_error("Mapping selects incompatible streams for the same preview role");
                }
                selected = group;
            }
        }
        if (!selected) {
            if (item.second.size() > 1) {
                throw std::runtime_error("Recorded-stream mapping required for " + roleName(item.first));
            }
            selected = item.second.front();
        }
        selected_groups.push_back(selected);
    }

    XdfLoadResult result;
    result.truncated_tail_ignored = xdf.truncated_tail_ignored;
    result.file_size_bytes = xdf.file_size_bytes;
    for (const Group* group : selected_groups) {
        XdfStreamData stitched = stitchGroup(*group, maximum_samples_per_stream);
        if (mapping.master_stream_id != 0 &&
            std::any_of(group->streams.begin(), group->streams.end(),
                [&mapping](const XdfStreamData* stream) {
                    return stream->stream_id == mapping.master_stream_id;
                })) {
            stitched.stream_id = mapping.master_stream_id;
        }
        result.streams.push_back(std::move(stitched));
    }
    std::stable_sort(result.streams.begin(), result.streams.end(),
        [](const XdfStreamData& left, const XdfStreamData& right) {
            return std::tie(left.role, left.stream_id) < std::tie(right.role, right.stream_id);
        });
    for (const XdfStreamData& stream : result.streams) {
        result.estimated_memory_bytes += stream.timestamps.capacity() * sizeof(double);
        for (const auto& sample : stream.samples) {
            result.estimated_memory_bytes += sample.capacity() * sizeof(double);
        }
    }
    return result;
}

} // namespace vicon_lsl
