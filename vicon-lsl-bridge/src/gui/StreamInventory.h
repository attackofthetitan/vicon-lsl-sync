#pragma once

#include "gui/SessionConfiguration.h"

#include <QVector>

namespace vicon_lsl::gui {

// How the list of streams the session knows about changes as they are
// discovered, rediscovered, and lost. These are the rules that decide what
// stays selected across a rediscovery, so they are kept apart from the widgets
// that display the result.

// Folds newly seen streams into the known list, keeping the operator's record
// and required choices for a stream that is already listed.
void mergeStreamInventory(QVector<StreamIdentity>& inventory,
                          const QVector<StreamIdentity>& seen);

// Rebuilds the list from a completed discovery pass. A stream that was already
// known keeps the operator's choices and its last measurements; a stream the
// saved configuration asked for is selected; anything else follows
// `record_every_visible_stream`. A stream that was selected or required but did
// not appear is retained and flagged, so it is visible as missing rather than
// silently dropped.
QVector<StreamIdentity> reconcileDiscoveredStreams(
    const QVector<StreamIdentity>& known,
    QVector<StreamIdentity> discovered,
    const SessionConfiguration& configuration);

// The streams a recording should capture. Recording every visible stream
// overrides the per-stream choices.
QVector<StreamIdentity> selectedStreams(const QVector<StreamIdentity>& inventory,
                                        bool record_every_visible_stream);

int visibleStreamCount(const QVector<StreamIdentity>& inventory);

} // namespace vicon_lsl::gui
