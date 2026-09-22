#pragma once

#include "gui/SessionConfiguration.h"

#include <QVector>

namespace vicon_lsl::gui {

// Rules for keeping the list of known streams up to date as streams appear,
// restart, and disappear.

// Adds newly seen streams and updates known ones, keeping the user's Record and
// Required choices.
void mergeStreamInventory(QVector<StreamIdentity>& inventory,
                          const QVector<StreamIdentity>& seen);

// Rebuilds the list after a stream search. Known streams keep the user's choices
// and last measurements. Streams named in the saved settings are selected, and
// the rest follow `record_every_visible_stream`. A selected or required stream
// that is no longer visible stays in the list, marked as missing.
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
