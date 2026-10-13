#include "LabRecorderClientTestSupport.h"

#include "gui/StreamInventory.h"

namespace labrecorder_client_tests {
namespace {

using vicon_lsl::gui::SessionConfiguration;
using vicon_lsl::gui::StreamBinding;
using vicon_lsl::gui::StreamIdentity;
using vicon_lsl::gui::mergeStreamInventory;
using vicon_lsl::gui::reconcileDiscoveredStreams;
using vicon_lsl::gui::selectedStreams;
using vicon_lsl::gui::visibleStreamCount;

// StreamIdentity::selected defaults to true, so this helper clears it and each
// test says which streams the user picked.
StreamIdentity stream(const QString& name, const QString& source_id) {
    StreamIdentity identity;
    identity.name = name;
    identity.source_id = source_id;
    identity.role = "markers";
    identity.channel_count = 4;
    identity.present = true;
    identity.selected = false;
    return identity;
}

// A default SessionConfiguration already asks for the four standard streams, so
// tests that are about one binding start from a configuration with none.
SessionConfiguration configurationWithoutBindings() {
    SessionConfiguration configuration;
    configuration.recording_streams.clear();
    return configuration;
}

const StreamIdentity* findStream(const QVector<StreamIdentity>& inventory, const QString& name) {
    for (const StreamIdentity& candidate : inventory) {
        if (candidate.name == name) return &candidate;
    }
    return nullptr;
}

} // namespace

void testStreamInventoryMerge() {
    const SessionConfiguration configuration = configurationWithoutBindings();
    QVector<StreamIdentity> inventory;
    mergeStreamInventory(inventory, {stream("Markers", "a"), stream("Gaze", "b")}, configuration);
    expect(inventory.size() == 2, "unseen streams are added to the inventory");

    inventory[0].selected = true;
    inventory[0].required = true;

    // The same stream seen again brings fresh details, but the user's Record
    // and Required choices must be kept.
    StreamIdentity refreshed = stream("Markers", "a");
    refreshed.channel_count = 12;
    mergeStreamInventory(inventory, {refreshed}, configuration);
    expect(inventory.size() == 2, "a stream already listed is not duplicated");
    const StreamIdentity* markers = findStream(inventory, "Markers");
    expect(markers != nullptr && markers->channel_count == 12,
           "a rediscovered stream refreshes its details");
    expect(markers != nullptr && markers->selected && markers->required,
           "a rediscovered stream keeps the operator's choices");
}

void testReconcileKeepsChoicesAndFlagsMissingStreams() {
    SessionConfiguration configuration = configurationWithoutBindings();
    configuration.record_every_visible_stream = false;

    QVector<StreamIdentity> known;
    known.push_back(stream("Markers", "a"));
    known[0].selected = true;
    known[0].required = true;
    known[0].effective_rate = 100.0;
    known[0].freshness_ms = 12;
    known.push_back(stream("Unwanted", "z"));

    QVector<StreamIdentity> discovered;
    discovered.push_back(stream("Markers", "a"));
    discovered.push_back(stream("Segments", "c"));

    const QVector<StreamIdentity> reconciled =
        reconcileDiscoveredStreams(known, discovered, configuration);

    const StreamIdentity* markers = findStream(reconciled, "Markers");
    expect(markers != nullptr && markers->selected && markers->required,
           "a rediscovered stream keeps the operator's choices");
    expect(markers != nullptr && markers->effective_rate == 100.0 && markers->freshness_ms == 12,
           "a rediscovered stream keeps its last measurements");

    const StreamIdentity* segments = findStream(reconciled, "Segments");
    expect(segments != nullptr && !segments->selected,
           "a newly seen stream is not recorded unless something asks for it");

    // "Unwanted" was known but neither selected nor required, so a stream
    // search that no longer sees it simply drops it.
    expect(findStream(reconciled, "Unwanted") == nullptr,
           "an unselected stream that disappeared is dropped");
}

void testReconcileHonoursConfiguredAndEveryVisibleStreams() {
    SessionConfiguration configuration = configurationWithoutBindings();
    configuration.record_every_visible_stream = false;
    StreamBinding binding;
    binding.name = "Gaze";
    binding.source_id = "g";
    binding.required = true;
    configuration.recording_streams.push_back(binding);

    QVector<StreamIdentity> discovered;
    discovered.push_back(stream("Gaze", "g"));
    discovered.push_back(stream("Other", "o"));

    QVector<StreamIdentity> reconciled = reconcileDiscoveredStreams({}, discovered, configuration);
    const StreamIdentity* gaze = findStream(reconciled, "Gaze");
    expect(gaze != nullptr && gaze->selected && gaze->required,
           "a stream the saved configuration requires is selected and required");
    const StreamIdentity* other = findStream(reconciled, "Other");
    expect(other != nullptr && !other->selected,
           "a stream nothing asked for is left unselected");

    configuration.record_every_visible_stream = true;
    reconciled = reconcileDiscoveredStreams({}, discovered, configuration);
    other = findStream(reconciled, "Other");
    expect(other != nullptr && other->selected,
           "recording every visible stream selects streams nothing asked for");

    configuration.record_every_visible_stream = false;
    configuration.recording_streams[0].source_id.clear();
    const auto missing = reconcileDiscoveredStreams({}, {}, configuration);
    expect(missing.size() == 1 && !missing[0].present && missing[0].selected && missing[0].required,
           "an unseen saved requirement appears as a missing stream on the first search");
    reconciled = reconcileDiscoveredStreams(missing, {stream("Gaze", "g")}, configuration);
    expect(reconciled.size() == 1 && reconciled[0].present && reconciled[0].required,
           "a real source replaces its saved name-only placeholder without a duplicate");
    auto merged = missing;
    mergeStreamInventory(merged, {stream("Gaze", "g")}, configuration);
    expect(merged.size() == 1 && merged[0].present && merged[0].required,
           "preview updates also replace missing placeholders and keep saved requirements");

    const auto saved = vicon_lsl::gui::recordingBindingsForInventory(configuration.recording_streams, missing);
    expect(saved.size() == 1 && saved[0].required,
           "a missing saved stream remains required after saving the inventory");
    auto deselected = missing;
    deselected[0].selected = deselected[0].required = false;
    expect(vicon_lsl::gui::recordingBindingsForInventory(saved, deselected).isEmpty(),
           "explicitly clearing Record and Required removes a saved missing stream");

    reconciled = reconcileDiscoveredStreams({}, {stream("Gaze", "g1"), stream("Gaze", "g2")}, configuration);
    const auto separate = vicon_lsl::gui::recordingBindingsForInventory(configuration.recording_streams, reconciled);
    expect(separate.size() == 2 && separate[0].source_id != separate[1].source_id,
           "each selected source is saved when several sources match an initially name-only binding");
}

void testReconcileRetainsSelectedStreamThatVanished() {
    SessionConfiguration configuration = configurationWithoutBindings();
    QVector<StreamIdentity> known;
    known.push_back(stream("Markers", "a"));
    known[0].selected = true;

    const QVector<StreamIdentity> reconciled =
        reconcileDiscoveredStreams(known, {stream("Segments", "c")}, configuration);

    const StreamIdentity* missing = findStream(reconciled, "Markers");
    expect(missing != nullptr, "a selected stream that vanished is retained, not dropped");
    expect(missing != nullptr && !missing->present,
           "a vanished stream is marked as no longer visible");
    expect(missing != nullptr && missing->freshness_ms == -1,
           "a vanished stream reports no freshness measurement");
    expect(missing != nullptr && !missing->warning.isEmpty(),
           "a vanished stream explains why it is still listed");
    expect(visibleStreamCount(reconciled) == 1,
           "a vanished stream is not counted as visible");
}

void testSelectedStreamsForRecording() {
    QVector<StreamIdentity> inventory;
    inventory.push_back(stream("Markers", "a"));
    inventory[0].selected = true;
    inventory.push_back(stream("Segments", "c"));
    inventory.push_back(stream("Gone", "x"));
    inventory[2].selected = true;
    inventory[2].present = false;

    QVector<StreamIdentity> chosen = selectedStreams(inventory, false);
    expect(chosen.size() == 1 && chosen[0].name == "Markers",
           "only visible selected streams are recorded");

    chosen = selectedStreams(inventory, true);
    expect(chosen.size() == 2, "recording every visible stream overrides the per-stream choices");
    expect(visibleStreamCount(inventory) == 2, "a stream that is gone is not counted as visible");
}

} // namespace labrecorder_client_tests
