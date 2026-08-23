#pragma once

#include "StreamOutlet.h"
#include "StreamPushResult.h"
#include "StreamSchema.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vicon_lsl::detail {

struct ViconNumericOutletProfile {
    const char* display_name;
    const char* item_name_plural;
    const char* empty_layout_message;
    const char* null_factory_message;
    const char* push_failure_prefix;
};

// Owns the lifecycle and metadata that are identical for the fixed-shape
// marker and segment double64 outlets. Domain-specific frame conversion stays
// in MarkerStream and SegmentStream.
class ViconNumericOutlet {
public:
    ViconNumericOutlet(StreamOutletFactory outlet_factory,
                       ViconNumericOutletProfile profile);

    void initialize(const StreamSchema& schema,
                    std::size_t item_count,
                    const std::string& source_id,
                    double nominal_rate);
    void destroy();
    bool isInitialized() const;

    template <class SampleBuilder>
    StreamPushResult pushSample(SampleBuilder&& build_sample, double timestamp) {
        // Preserve the original stream facades' short-circuit order: an
        // unconfigured, empty, or failed outlet never inspects or flattens the
        // caller's domain values.
        if (!configured_) {
            return StreamPushResult::NotConfigured;
        }
        if (item_count_ == 0) {
            return StreamPushResult::Pushed;
        }
        if (!outlet_) {
            return StreamPushResult::Failed;
        }
        return pushPreparedSample(
            std::forward<SampleBuilder>(build_sample)(), timestamp);
    }

private:
    StreamPushResult pushPreparedSample(std::vector<double> sample,
                                        double timestamp);

    StreamOutletFactory outlet_factory_;
    ViconNumericOutletProfile profile_;
    std::unique_ptr<StreamOutlet> outlet_;
    std::unique_ptr<lsl::stream_info> info_;
    std::vector<double> sample_buffer_;
    std::size_t item_count_ = 0;
    bool configured_ = false;
};

} // namespace vicon_lsl::detail
