#pragma once

#include "gui/SessionConfiguration.h"

#include <lsl_cpp.h>

#include <QDateTime>
#include <QString>

#include <cmath>

namespace vicon_lsl::gui {

inline QString coordinateFrameOf(lsl::stream_info info) {
    const char* frame = info.desc().child("acquisition").child_value("coordinate_frame");
    return frame ? QString::fromUtf8(frame) : QString();
}

inline StreamIdentity identityFromStreamInfo(lsl::stream_info info) {
    const double rate = info.nominal_srate();
    StreamIdentity identity;
    identity.name = QString::fromStdString(info.name());
    identity.type = QString::fromStdString(info.type());
    identity.source_id = QString::fromStdString(info.source_id());
    identity.hostname = QString::fromStdString(info.hostname());
    identity.session_id = QString::fromStdString(info.session_id());
    identity.uid = QString::fromStdString(info.uid());
    identity.publisher_created_at = info.created_at();
    identity.channel_count = info.channel_count();
    identity.nominal_rate = std::isfinite(rate) && rate > 0.0 ? rate : 0.0;
    identity.coordinate_frame = coordinateFrameOf(info);
    identity.discovered_at = QDateTime::currentDateTimeUtc();
    return identity;
}

inline bool identityDescribesItself(const StreamIdentity& identity, bool needs_coordinate_frame) {
    return !identity.source_id.isEmpty() && identity.channel_count > 0 &&
           (!needs_coordinate_frame || !identity.coordinate_frame.isEmpty());
}

} // namespace vicon_lsl::gui
