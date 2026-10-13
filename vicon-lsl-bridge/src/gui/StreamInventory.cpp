#include "gui/StreamInventory.h"

#include <algorithm>
#include <utility>

namespace vicon_lsl::gui {
namespace {

template <class Inventory>
auto findByKey(Inventory& inventory, const StreamIdentity& wanted) {
    return std::find_if(inventory.begin(), inventory.end(),
                        [&wanted](const StreamIdentity& candidate) {
                            return candidate.stableKey() == wanted.stableKey();
                        });
}

void applyConfiguredSelection(StreamIdentity& stream, const SessionConfiguration& configuration) {
    const auto binding = std::find_if(configuration.recording_streams.cbegin(),
        configuration.recording_streams.cend(),
        [&](const StreamBinding& candidate) { return candidate.matches(stream); });
    stream.selected = binding != configuration.recording_streams.cend() || configuration.record_every_visible_stream;
    stream.required = binding != configuration.recording_streams.cend() && binding->required;
}

} // namespace

void mergeStreamInventory(QVector<StreamIdentity>& inventory,
                          const QVector<StreamIdentity>& seen,
                          const SessionConfiguration& configuration) {
    for (StreamIdentity stream : seen) {
        auto existing = findByKey(inventory, stream);
        if (existing == inventory.end()) {
            existing = std::find_if(inventory.begin(), inventory.end(), [&](const StreamIdentity& previous) {
                return !previous.present && std::any_of(configuration.recording_streams.cbegin(),
                    configuration.recording_streams.cend(), [&](const StreamBinding& binding) {
                        return binding.matches(previous) && binding.matches(stream);
                    });
            });
        }
        if (existing == inventory.end()) {
            applyConfiguredSelection(stream, configuration);
            inventory.push_back(std::move(stream));
            continue;
        }
        // Update the stream's details but keep the user's choices.
        const bool selected = existing->selected;
        const bool required = existing->required;
        *existing = stream;
        existing->selected = selected;
        existing->required = required;
    }
}

QVector<StreamIdentity> reconcileDiscoveredStreams(
    const QVector<StreamIdentity>& known,
    QVector<StreamIdentity> discovered,
    const SessionConfiguration& configuration) {
    QVector<StreamIdentity> reconciled;
    reconciled.reserve(discovered.size() + known.size());

    for (StreamIdentity& stream : discovered) {
        const auto previous = findByKey(known, stream);
        if (previous != known.end()) {
            stream.selected = previous->selected;
            stream.required = previous->required;
            stream.freshness_ms = previous->freshness_ms;
            stream.effective_rate = previous->effective_rate;
        } else {
            applyConfiguredSelection(stream, configuration);
        }
        reconciled.push_back(std::move(stream));
    }

    for (const StreamIdentity& previous : known) {
        if (!previous.selected && !previous.required) continue;
        if (findByKey(reconciled, previous) != reconciled.end()) continue;
        // A saved name-only placeholder is replaced when its source appears.
        const bool resolved = std::any_of(configuration.recording_streams.cbegin(),
            configuration.recording_streams.cend(), [&](const StreamBinding& binding) {
                return binding.matches(previous) && std::any_of(reconciled.cbegin(), reconciled.cend(),
                    [&](const StreamIdentity& stream) { return stream.present && binding.matches(stream); });
            });
        if (resolved) continue;
        StreamIdentity missing = previous;
        missing.present = false;
        missing.freshness_ms = -1;
        missing.warning = "Previously selected stream is not currently visible";
        reconciled.push_back(std::move(missing));
    }

    for (const StreamBinding& binding : configuration.recording_streams) {
        const auto matches = [&](const StreamIdentity& stream) { return binding.matches(stream); };
        if (std::any_of(reconciled.cbegin(), reconciled.cend(), matches) ||
            std::any_of(known.cbegin(), known.cend(), matches)) continue;
        StreamIdentity missing;
        missing.role = binding.role;
        missing.name = binding.name;
        missing.source_id = binding.source_id;
        missing.required = binding.required;
        missing.channel_count = binding.expected_channels;
        missing.nominal_rate = binding.expected_nominal_rate;
        missing.coordinate_frame = binding.expected_coordinate_frame;
        missing.present = false;
        missing.warning = "Saved recording stream is not currently visible";
        reconciled.push_back(std::move(missing));
    }
    return reconciled;
}

QVector<StreamBinding> recordingBindingsForInventory(
    const QVector<StreamBinding>& saved, const QVector<StreamIdentity>& inventory) {
    QVector<StreamBinding> result;
    for (const StreamIdentity& stream : inventory) {
        if (!stream.selected && !stream.required) continue;
        const auto previous = std::find_if(saved.cbegin(), saved.cend(),
            [&](const StreamBinding& binding) { return binding.matches(stream); });
        StreamBinding binding = previous != saved.cend() ? *previous : StreamBinding{
            stream.role, stream.name, stream.source_id,
            stream.source_id.isEmpty() ? StreamReconnectionMode::FollowName : StreamReconnectionMode::SourceIdentity,
            stream.required, stream.channel_count, stream.nominal_rate, stream.coordinate_frame};
        binding.required = stream.required;
        if (stream.present) {
            if (binding.source_id.isEmpty() && binding.reconnection == StreamReconnectionMode::SourceIdentity)
                binding.source_id = stream.source_id;
            if (binding.expected_channels == 0) binding.expected_channels = stream.channel_count;
            if (binding.expected_nominal_rate == 0) binding.expected_nominal_rate = stream.nominal_rate;
            if (binding.expected_coordinate_frame.isEmpty()) binding.expected_coordinate_frame = stream.coordinate_frame;
        }
        const auto duplicate = std::find_if(result.begin(), result.end(), [&](const StreamBinding& existing) {
            return existing.name == binding.name && existing.source_id == binding.source_id &&
                   existing.reconnection == binding.reconnection;
        });
        if (duplicate == result.end()) result.push_back(std::move(binding));
        else duplicate->required = duplicate->required || binding.required;
    }
    for (const StreamBinding& binding : saved) {
        if (std::none_of(inventory.cbegin(), inventory.cend(),
            [&](const StreamIdentity& stream) { return binding.matches(stream); })) result.push_back(binding);
    }
    return result;
}

QVector<StreamIdentity> selectedStreams(const QVector<StreamIdentity>& inventory,
                                        bool record_every_visible_stream) {
    QVector<StreamIdentity> result;
    for (StreamIdentity stream : inventory) {
        if (!stream.present) continue;
        if (record_every_visible_stream) stream.selected = true;
        if (stream.selected) result.push_back(std::move(stream));
    }
    return result;
}

int visibleStreamCount(const QVector<StreamIdentity>& inventory) {
    return static_cast<int>(std::count_if(inventory.cbegin(), inventory.cend(),
                                          [](const StreamIdentity& stream) {
                                              return stream.present;
                                          }));
}

} // namespace vicon_lsl::gui
