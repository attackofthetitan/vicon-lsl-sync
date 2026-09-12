#include "detail/ViconOutlet.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace vicon_lsl::detail {
namespace {

void appendTimingMetadata(lsl::stream_info& info, double stream_rate) {
    lsl::xml_element acquisition = info.desc().append_child("acquisition");
    acquisition.append_child_value("device", "Vicon");
    acquisition.append_child_value("sdk", "ViconDataStreamSDK");
    acquisition.append_child_value("nominal_srate", std::to_string(stream_rate).c_str());
    acquisition.append_child_value("timestamp", "estimated_acquisition_time");
    acquisition.append_child_value("clock_domain", "lsl_local_clock");
    acquisition.append_child_value(
        "timestamp_estimator", "immediate_receipt_minus_valid_pipeline_latency");
    acquisition.append_child_value("timestamp_fallback", "immediate_receipt_time");
    acquisition.append_child_value("latency_correction", "GetLatencyTotal_pipeline_estimate");
    acquisition.append_child_value(
        "timestamp_accuracy", "acquisition_estimate_not_capture_accurate");

    lsl::xml_element synchronization = info.desc().append_child("synchronization");
    synchronization.append_child_value("clock_domain", "lsl_local_clock");
    synchronization.append_child_value(
        "timestamp_origin", "local_receipt_minus_valid_vicon_pipeline_latency");
    synchronization.append_child_value("offset_mean", "0");
    synchronization.append_child_value("can_drop_samples", "true");
}

} // namespace

ViconOutlet::ViconOutlet(StreamOutletFactory outlet_factory,
                        const char* display_name,
                        const char* item_noun)
    : outlet_factory_(std::move(outlet_factory)),
      display_name_(display_name),
      item_noun_(item_noun) {}

void ViconOutlet::initialize(const StreamSchema& schema,
                             std::size_t item_count,
                             const std::string& source_id,
                             double nominal_rate) {
    destroy();
    configured_ = true;
    if (item_count == 0) {
        std::cout << "No " << item_noun_ << "s discovered; "
                  << item_noun_ << " stream not created" << std::endl;
        return;
    }

    const int channel_count = static_cast<int>(schema.channelCount());
    channel_count_ = static_cast<std::size_t>(channel_count);
    const double stream_rate = std::isfinite(nominal_rate) && nominal_rate > 0.0
        ? nominal_rate
        : lsl::IRREGULAR_RATE;
    info_.emplace(
        schema.name,
        schema.type,
        channel_count,
        stream_rate,
        lsl::cf_double64,
        source_id);

    lsl::xml_element channels = info_->desc().append_child("channels");
    for (const auto& spec : schema.channels) {
        lsl::xml_element channel = channels.append_child("channel");
        channel.append_child_value("label", spec.label);
        channel.append_child_value("unit", spec.unit);
    }
    appendTimingMetadata(*info_, stream_rate);

    outlet_ = outlet_factory_(*info_);
    if (!outlet_) {
        throw std::runtime_error(std::string(display_name_) +
                                 " outlet factory returned no outlet");
    }

    std::cout << display_name_ << " stream ready, " << item_count << " "
              << item_noun_ << "s, " << channel_count << " channels"
              << std::endl;
}

void ViconOutlet::destroy() {
    const bool was_initialized = outlet_ != nullptr || info_.has_value();
    outlet_.reset();
    info_.reset();
    channel_count_ = 0;
    configured_ = false;
    if (was_initialized) {
        std::cout << display_name_ << " stream closed" << std::endl;
    }
}

bool ViconOutlet::isInitialized() const {
    return configured_ && (channel_count_ == 0 || outlet_ != nullptr);
}

StreamPushResult ViconOutlet::pushSample(const std::vector<double>& sample,
                                       double timestamp) {
    if (!configured_) return StreamPushResult::NotConfigured;
    if (channel_count_ == 0) return StreamPushResult::Pushed;
    if (!outlet_) return StreamPushResult::Failed;

    if (sample.size() != channel_count_) {
        std::cerr << display_name_ << " sample channel mismatch: expected "
                  << channel_count_ << ", got " << sample.size() << std::endl;
        return StreamPushResult::Failed;
    }

    try {
        outlet_->pushSample(sample, timestamp);
        return StreamPushResult::Pushed;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to push " << item_noun_ << " LSL sample: "
                  << ex.what() << std::endl;
        destroy();
        return StreamPushResult::Failed;
    }
}

} // namespace vicon_lsl::detail
