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

// Rebuilds the list after a stream search, keeping known streams' choices and
// measurements, selecting new streams the settings name (or every new stream
// when `record_every_visible_stream` is on), and keeping selected or required
// streams that disappeared, marked as missing.
QVector<StreamIdentity> reconcileDiscoveredStreams(
    const QVector<StreamIdentity>& known,
    QVector<StreamIdentity> discovered,
    const SessionConfiguration& configuration);

// The streams a recording should capture, where recording every visible stream
// overrides the per-stream choices.
QVector<StreamIdentity> selectedStreams(const QVector<StreamIdentity>& inventory,
                                        bool record_every_visible_stream);

int visibleStreamCount(const QVector<StreamIdentity>& inventory);

} // namespace vicon_lsl::gui
