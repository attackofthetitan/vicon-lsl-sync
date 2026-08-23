#include "detail/ViconNumericOutlet.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
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

ViconNumericOutlet::ViconNumericOutlet(StreamOutletFactory outlet_factory,
                                       ViconNumericOutletProfile profile)
    : outlet_factory_(std::move(outlet_factory)), profile_(profile) {}

void ViconNumericOutlet::initialize(const StreamSchema& schema,
                                    std::size_t item_count,
                                    const std::string& source_id,
                                    double nominal_rate) {
    destroy();
    configured_ = true;
    item_count_ = item_count;
    if (item_count_ == 0) {
        std::cout << profile_.empty_layout_message << std::endl;
        return;
    }

    const int channel_count = static_cast<int>(schema.channelCount());
    const double stream_rate = std::isfinite(nominal_rate) && nominal_rate > 0.0
        ? nominal_rate
        : lsl::IRREGULAR_RATE;
    info_ = std::make_unique<lsl::stream_info>(
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

    sample_buffer_.resize(static_cast<std::size_t>(channel_count));
    outlet_ = outlet_factory_(*info_);
    if (!outlet_) {
        throw std::runtime_error(profile_.null_factory_message);
    }

    std::cout << profile_.display_name << " stream ready, " << item_count_ << " "
              << profile_.item_name_plural << ", " << channel_count << " channels"
              << std::endl;
}

void ViconNumericOutlet::destroy() {
    const bool was_initialized = outlet_ != nullptr || info_ != nullptr;
    outlet_.reset();
    info_.reset();
    sample_buffer_.clear();
    item_count_ = 0;
    configured_ = false;
    if (was_initialized) {
        std::cout << profile_.display_name << " stream closed" << std::endl;
    }
}

bool ViconNumericOutlet::isInitialized() const {
    return configured_ && (item_count_ == 0 || outlet_ != nullptr);
}

StreamPushResult ViconNumericOutlet::pushPreparedSample(std::vector<double> sample,
                                                        double timestamp) {
    if (sample.size() != sample_buffer_.size()) {
        std::cerr << profile_.display_name << " sample channel mismatch: expected "
                  << sample_buffer_.size() << ", got " << sample.size() << std::endl;
        return StreamPushResult::Failed;
    }
    sample_buffer_ = std::move(sample);

    try {
        outlet_->pushSample(sample_buffer_, timestamp);
        return StreamPushResult::Pushed;
    } catch (const std::exception& ex) {
        std::cerr << profile_.push_failure_prefix << ex.what() << std::endl;
        destroy();
        return StreamPushResult::Failed;
    }
}

} // namespace vicon_lsl::detail
