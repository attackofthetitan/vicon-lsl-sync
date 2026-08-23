#include "preview/PreviewFrameAssembler.h"

#include "preview/PreviewMath.h"
#include "preview/PreviewParsing.h"

#include <algorithm>

namespace vicon_lsl {
namespace {

bool matchesFrame(const PreviewStreamSnapshot& stream,
                  double frame_timestamp,
                  double tolerance_seconds) {
    return stream.fresh &&
           timestampWithinTolerance(frame_timestamp, stream.timestamp, tolerance_seconds);
}

} // namespace

std::optional<PreviewFrame> assemblePreviewFrame(const PreviewFrameSnapshot& snapshot) {
    const bool marker_master = snapshot.markers.updated;
    const bool segment_fallback = snapshot.segments.updated && snapshot.segments.fresh;
    const bool gaze_fallback = snapshot.gaze.updated && snapshot.gaze.fresh;

    if (!marker_master && !segment_fallback && !gaze_fallback) {
        return std::nullopt;
    }

    PreviewFrame frame;
    if (marker_master) {
        frame.timestamp = snapshot.markers.timestamp;
        frame.markers = parseMarkerSample(snapshot.markers.labels,
                                          snapshot.markers.sample,
                                          snapshot.markers.transform);
    } else if (segment_fallback && gaze_fallback) {
        frame.timestamp = (std::max)(snapshot.segments.timestamp, snapshot.gaze.timestamp);
    } else if (segment_fallback) {
        frame.timestamp = snapshot.segments.timestamp;
    } else {
        frame.timestamp = snapshot.gaze.timestamp;
    }

    frame.marker_stream_present = snapshot.markers.connected;
    frame.segment_stream_present = snapshot.segments.connected;
    frame.gaze_stream_present = snapshot.gaze.connected;

    if (matchesFrame(snapshot.segments,
                     frame.timestamp,
                     snapshot.match_tolerance_seconds)) {
        frame.segments = parseSegmentSample(snapshot.segments.labels,
                                            snapshot.segments.sample,
                                            snapshot.segments.transform);
    }
    if (matchesFrame(snapshot.gaze,
                     frame.timestamp,
                     snapshot.match_tolerance_seconds)) {
        frame.gaze_rays = parseGazeSample(snapshot.gaze.labels,
                                          snapshot.gaze.sample,
                                          snapshot.gaze.transform);
    }

    return frame;
}

} // namespace vicon_lsl
