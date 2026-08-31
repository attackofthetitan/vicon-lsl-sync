#include "preview/PreviewXdfPrivate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace vicon_lsl::preview_xdf_detail {
namespace {

struct ClockOffsetFit {
    long double stream_center = 0.0L;
    long double offset_center = 0.0L;
    long double slope = 0.0L;
};

std::optional<ClockOffsetFit> fitClockOffsets(
    const std::vector<XdfClockOffset>& offsets) {
    if (offsets.empty()) {
        return std::nullopt;
    }

    // Center both coordinates before accumulating covariance/variance. XDF
    // timestamps are often large absolute values, and an uncentered fit loses
    // the small drift term to floating-point cancellation.
    long double stream_center = 0.0L;
    long double offset_center = 0.0L;
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        const long double weight = 1.0L / static_cast<long double>(index + 1);
        stream_center +=
            (static_cast<long double>(offsets[index].stream_time) - stream_center) * weight;
        offset_center +=
            (static_cast<long double>(offsets[index].offset) - offset_center) * weight;
    }

    long double covariance = 0.0L;
    long double variance = 0.0L;
    for (const auto& measurement : offsets) {
        const long double stream_delta =
            static_cast<long double>(measurement.stream_time) - stream_center;
        const long double offset_delta =
            static_cast<long double>(measurement.offset) - offset_center;
        covariance += stream_delta * offset_delta;
        variance += stream_delta * stream_delta;
    }

    if (!std::isfinite(covariance) || !std::isfinite(variance) || variance < 0.0L) {
        throw std::runtime_error("XDF clock-offset fit is not finite");
    }

    long double slope = 0.0L;
    if (variance > 0.0L) {
        slope = covariance / variance;
    }
    if (!std::isfinite(stream_center) ||
        !std::isfinite(offset_center) ||
        !std::isfinite(slope)) {
        throw std::runtime_error("XDF clock-offset fit is not finite");
    }
    return ClockOffsetFit{stream_center, offset_center, slope};
}

} // namespace

double resolveSampleTimestamp(const std::optional<double>& encoded_timestamp,
                              const std::optional<double>& previous_timestamp,
                              double nominal_srate) {
    double timestamp = 0.0;
    if (encoded_timestamp) {
        timestamp = *encoded_timestamp;
    } else {
        if (!previous_timestamp || !std::isfinite(nominal_srate) || nominal_srate <= 0.0) {
            throw std::runtime_error(
                "Cannot restore an XDF sample time without an earlier time and a positive expected rate");
        }
        timestamp = *previous_timestamp + 1.0 / nominal_srate;
    }
    if (!std::isfinite(timestamp)) {
        throw std::runtime_error("XDF sample timestamp is not finite");
    }
    return timestamp;
}

void appendClockOffset(XdfStreamData& stream,
                       double collection_time,
                       double offset) {
    // XDF stores ClockOffset collection times in the source stream's clock
    // domain. The offset itself maps that clock into recorder time.
    const XdfClockOffset measurement{collection_time, offset};
    if (!std::isfinite(collection_time) || !std::isfinite(measurement.stream_time) ||
        !std::isfinite(measurement.offset)) {
        throw std::runtime_error("XDF clock-offset measurement is not finite");
    }
    stream.clock_offsets.push_back(measurement);
}

std::size_t correctAndRepairTimestamps(XdfStreamData& stream) {
    std::stable_sort(stream.clock_offsets.begin(), stream.clock_offsets.end(),
                     [](const XdfClockOffset& left, const XdfClockOffset& right) {
                         return left.stream_time < right.stream_time;
                     });
    if (std::adjacent_find(
            stream.clock_offsets.begin(), stream.clock_offsets.end(),
            [](const XdfClockOffset& left, const XdfClockOffset& right) {
                return left.stream_time == right.stream_time;
            }) != stream.clock_offsets.end()) {
        throw std::runtime_error("XDF clock-offset measurement times are not unique");
    }

    const auto fit = fitClockOffsets(stream.clock_offsets);
    if (fit) {
        for (double& timestamp : stream.timestamps) {
            const long double centered_timestamp =
                static_cast<long double>(timestamp) - fit->stream_center;
            const long double correction =
                fit->offset_center + fit->slope * centered_timestamp;
            const long double corrected = static_cast<long double>(timestamp) + correction;
            timestamp = static_cast<double>(corrected);
            if (!std::isfinite(timestamp)) {
                throw std::runtime_error("Corrected XDF timestamp is not finite");
            }
        }
    }
    std::size_t repaired_count = 0;
    long double accumulated_shift = 0.0L;
    std::optional<double> previous;
    for (double& timestamp : stream.timestamps) {
        const long double shifted = static_cast<long double>(timestamp) + accumulated_shift;
        if (!std::isfinite(shifted)) {
            throw std::runtime_error("Corrected XDF timestamp is not finite");
        }
        double repaired = static_cast<double>(shifted);
        if (!std::isfinite(repaired)) {
            throw std::runtime_error("Corrected XDF timestamp is not finite");
        }
        if (previous && repaired <= *previous) {
            repaired = std::nextafter(*previous, std::numeric_limits<double>::infinity());
            if (!std::isfinite(repaired)) {
                throw std::runtime_error("Could not fix an XDF timestamp that moved backward");
            }
            accumulated_shift += static_cast<long double>(repaired) - shifted;
            ++repaired_count;
        }
        timestamp = repaired;
        previous = repaired;
    }
    return repaired_count;
}

} // namespace vicon_lsl::preview_xdf_detail
