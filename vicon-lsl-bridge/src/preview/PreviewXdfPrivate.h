#pragma once

#include "preview/PreviewCsv.h"
#include "preview/PreviewXdfReader.h"

#include <cstddef>
#include <optional>
#include <string>

namespace vicon_lsl::preview_xdf_detail {

std::string lowerAscii(std::string value);

void parseStreamHeaderMetadata(XdfStreamData& stream, const std::string& xml);
void finalizeStreamMetadata(XdfStreamData& stream);

double resolveSampleTimestamp(const std::optional<double>& encoded_timestamp,
                              const std::optional<double>& previous_timestamp,
                              double nominal_srate);
void appendClockOffset(XdfStreamData& stream,
                       double collection_time,
                       double offset);
std::size_t correctAndRepairTimestamps(XdfStreamData& stream);

PreviewRecording assembleRecording(const XdfLoadResult& xdf,
                                   const PreviewTransformProfile& vicon_transform,
                                   const PreviewTransformProfile& gaze_transform,
                                   double match_tolerance_seconds,
                                   std::uint32_t preferred_master_stream_id,
                                   const PreviewLoadOptions& options);

} // namespace vicon_lsl::preview_xdf_detail
