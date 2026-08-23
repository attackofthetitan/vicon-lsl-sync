#pragma once

#include "ViconFrameTypes.h"

#include <string>

namespace vicon_lsl {

MarkerSample invalidMarkerSample();
SegmentSample invalidSegmentSample();

bool isValid(const MarkerTranslationRead& read);
bool isValid(const SegmentTranslationRead& read);
bool isValid(const SegmentRotationRead& read);
bool isValid(const CountRead& read);
bool isValid(const NameRead& read);

bool layoutChanged(const ViconLayout& current, const ViconLayout& known);

std::string buildStreamSourceId(const std::string& prefix,
                                const std::string& kind,
                                const std::string& hostname);

MarkerSample markerSampleForLsl(const MarkerTranslationRead& read);
SegmentSample segmentSampleForLsl(const SegmentTranslationRead& translation,
                                  const SegmentRotationRead& rotation);

} // namespace vicon_lsl
