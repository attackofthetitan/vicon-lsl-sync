#pragma once

#include "StreamOutlet.h"
#include "StreamPushResult.h"
#include "StreamSchema.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vicon_lsl::detail {

// Shared LSL setup and sending for marker and segment streams.
class ViconOutlet {
public:
    ViconOutlet(StreamOutletFactory outlet_factory,
                const char* display_name,
                const char* item_noun);

    void initialize(const StreamSchema& schema,
                    std::size_t item_count,
                    const std::string& source_id,
                    double nominal_rate);
    void destroy();
    bool isInitialized() const;

    StreamPushResult pushSample(const std::vector<double>& sample, double timestamp);

private:
    StreamOutletFactory outlet_factory_;
    const char* display_name_;
    const char* item_noun_;
    std::unique_ptr<StreamOutlet> outlet_;
    std::optional<lsl::stream_info> info_;
    std::size_t channel_count_ = 0;
    bool configured_ = false;
};

} // namespace vicon_lsl::detail
