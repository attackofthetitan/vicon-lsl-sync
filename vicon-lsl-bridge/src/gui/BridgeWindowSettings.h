#pragma once

#include <QString>

class QSettings;

namespace vicon_lsl::gui_detail {

struct BridgeWindowSettings {
    QString server;
    QString marker_stream;
    QString segment_stream;
    QString recording_root;
    QString recording_template;
    QString participant;
    QString session;
    QString task;
    int run = 1;
    QString acquisition;
    QString modality;
    QString labrecorder_executable;
    QString labrecorder_host;
    int labrecorder_port = 22345;
};

BridgeWindowSettings loadBridgeWindowSettings();
void saveBridgeWindowSettings(const BridgeWindowSettings& settings);

// Backend-taking overloads keep the key/default mapping directly testable
// without touching a developer's native per-user settings store.
BridgeWindowSettings loadBridgeWindowSettings(QSettings& source);
void saveBridgeWindowSettings(QSettings& destination,
                              const BridgeWindowSettings& settings);

} // namespace vicon_lsl::gui_detail
