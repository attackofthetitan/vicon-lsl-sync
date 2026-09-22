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

} // namespace

void mergeStreamInventory(QVector<StreamIdentity>& inventory,
                          const QVector<StreamIdentity>& seen) {
    for (const StreamIdentity& stream : seen) {
        const auto existing = findByKey(inventory, stream);
        if (existing == inventory.end()) {
            inventory.push_back(stream);
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
            const auto configured = std::find_if(
                configuration.recording_streams.cbegin(),
                configuration.recording_streams.cend(),
                [&stream](const StreamBinding& binding) { return binding.matches(stream); });
            if (configured != configuration.recording_streams.cend()) {
                stream.selected = true;
                stream.required = configured->required;
            } else {
                stream.selected = configuration.record_every_visible_stream;
            }
        }
        reconciled.push_back(std::move(stream));
    }

    for (const StreamIdentity& previous : known) {
        if (!previous.selected && !previous.required) continue;
        if (findByKey(reconciled, previous) != reconciled.end()) continue;
        StreamIdentity missing = previous;
        missing.present = false;
        missing.freshness_ms = -1;
        missing.warning = "Previously selected stream is not currently visible";
        reconciled.push_back(std::move(missing));
    }
    return reconciled;
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
