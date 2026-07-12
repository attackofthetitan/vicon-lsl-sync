#include "MarkerStream.h"
#include "StreamSchema.h"

#include <exception>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

MarkerStream::MarkerStream(StreamOutletFactory outlet_factory)
    : outlet_factory_(std::move(outlet_factory)) {}

void MarkerStream::initialize(const std::vector<std::pair<std::string, std::string>>& marker_names,
                               const std::string& stream_name,
                               const std::string& source_id,
                               double nominal_rate) {
    destroy();
    configured_ = true;
    marker_names_ = marker_names;
    if (marker_names_.empty()) {
        std::cout << "No markers discovered; marker stream not created" << std::endl;
        return;
    }

    int channel_count = static_cast<int>(marker_names_.size()) * 4;

    const double stream_rate = std::isfinite(nominal_rate) && nominal_rate > 0.0
        ? nominal_rate
        : lsl::IRREGULAR_RATE;
    info_ = std::make_unique<lsl::stream_info>(
        stream_name, "MoCap", channel_count,
        stream_rate, lsl::cf_double64, source_id);

    lsl::xml_element channels = info_->desc().append_child("channels");
    for (const auto& spec : vicon_lsl::buildMarkerStreamSchema(marker_names_, stream_name).channels) {
        auto channel = channels.append_child("channel");
        channel.append_child_value("label", spec.label);
        channel.append_child_value("unit", spec.unit);
    }

    lsl::xml_element acquisition = info_->desc().append_child("acquisition");
    acquisition.append_child_value("device", "Vicon");
    acquisition.append_child_value("sdk", "ViconDataStreamSDK");
    acquisition.append_child_value("nominal_srate", std::to_string(stream_rate).c_str());
    acquisition.append_child_value("timestamp", "estimated_acquisition_time");
    acquisition.append_child_value("clock_domain", "lsl_local_clock");
    acquisition.append_child_value(
        "timestamp_estimator", "immediate_receipt_minus_valid_pipeline_latency");
    acquisition.append_child_value("timestamp_fallback", "immediate_receipt_time");
    acquisition.append_child_value("latency_correction", "GetLatencyTotal_pipeline_estimate");
    acquisition.append_child_value("timestamp_accuracy", "acquisition_estimate_not_capture_accurate");
    lsl::xml_element synchronization = info_->desc().append_child("synchronization");
    synchronization.append_child_value("clock_domain", "lsl_local_clock");
    synchronization.append_child_value(
        "timestamp_origin", "local_receipt_minus_valid_vicon_pipeline_latency");
    synchronization.append_child_value("offset_mean", "0");
    synchronization.append_child_value("can_drop_samples", "true");

    sample_buffer_.resize(channel_count);
    outlet_ = outlet_factory_(*info_);
    if (!outlet_) {
        throw std::runtime_error("Marker outlet factory returned no outlet");
    }

    std::cout << "Marker stream ready, " << marker_names_.size()
              << " markers, " << channel_count << " channels" << std::endl;
}

void MarkerStream::destroy() {
    const bool was_initialized = outlet_ != nullptr || info_ != nullptr;
    outlet_.reset();
    info_.reset();
    marker_names_.clear();
    sample_buffer_.clear();
    configured_ = false;
    if (was_initialized) {
        std::cout << "Marker stream closed" << std::endl;
    }
}

bool MarkerStream::isInitialized() const {
    return configured_ && (marker_names_.empty() || outlet_ != nullptr);
}

StreamPushResult MarkerStream::pushSample(
    const std::vector<vicon_lsl::MarkerObjectRead>& markers,
    double timestamp) {
    if (!configured_) return StreamPushResult::NotConfigured;
    if (marker_names_.empty()) return StreamPushResult::Pushed;
    if (!outlet_) return StreamPushResult::Failed;

    std::vector<vicon_lsl::MarkerSample> samples;
    samples.reserve(markers.size());
    for (const auto& marker : markers) {
        samples.push_back(vicon_lsl::markerSampleForLsl(marker.value));
    }
    auto flattened = vicon_lsl::flattenMarkerSamples(samples);
    if (flattened.size() != sample_buffer_.size()) {
        std::cerr << "Marker sample channel mismatch: expected " << sample_buffer_.size()
                  << ", got " << flattened.size() << std::endl;
        return StreamPushResult::Failed;
    }
    sample_buffer_ = std::move(flattened);

    try {
        outlet_->pushSample(sample_buffer_, timestamp);
        return StreamPushResult::Pushed;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to push marker LSL sample: " << ex.what() << std::endl;
        destroy();
        return StreamPushResult::Failed;
    }
}
