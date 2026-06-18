#include "MarkerStream.h"
#include "StreamSchema.h"

#include <exception>
#include <iostream>
#include <utility>

void MarkerStream::initialize(const std::vector<std::pair<std::string, std::string>>& marker_names,
                               const std::string& stream_name,
                               const std::string& source_id) {
    destroy();
    marker_names_ = marker_names;
    if (marker_names_.empty()) {
        std::cout << "No markers discovered; marker stream not created" << std::endl;
        return;
    }

    int channel_count = static_cast<int>(marker_names_.size()) * 4;

    info_ = std::make_unique<lsl::stream_info>(
        stream_name, "MoCap", channel_count,
        lsl::IRREGULAR_RATE, lsl::cf_double64, source_id);

    lsl::xml_element channels = info_->desc().append_child("channels");
    for (const auto& spec : vicon_lsl::buildMarkerStreamSchema(marker_names_, stream_name).channels) {
        auto channel = channels.append_child("channel");
        channel.append_child_value("label", spec.label);
        channel.append_child_value("unit", spec.unit);
    }

    sample_buffer_.resize(channel_count);
    outlet_ = std::make_unique<lsl::stream_outlet>(*info_);

    std::cout << "Marker stream ready, " << marker_names_.size()
              << " markers, " << channel_count << " channels" << std::endl;
}

void MarkerStream::destroy() {
    const bool was_initialized = outlet_ != nullptr || info_ != nullptr;
    outlet_.reset();
    info_.reset();
    marker_names_.clear();
    sample_buffer_.clear();
    if (was_initialized) {
        std::cout << "Marker stream closed" << std::endl;
    }
}

void MarkerStream::pushSample(const std::vector<vicon_lsl::MarkerObjectRead>& markers,
                              double timestamp) {
    if (!outlet_) return;

    std::vector<vicon_lsl::MarkerSample> samples;
    samples.reserve(markers.size());
    for (const auto& marker : markers) {
        samples.push_back(vicon_lsl::markerSampleForLsl(marker.value));
    }
    auto flattened = vicon_lsl::flattenMarkerSamples(samples);
    if (flattened.size() != sample_buffer_.size()) {
        std::cerr << "Marker sample channel mismatch: expected " << sample_buffer_.size()
                  << ", got " << flattened.size() << std::endl;
        return;
    }
    sample_buffer_ = std::move(flattened);

    try {
        outlet_->push_sample(sample_buffer_, timestamp);
    } catch (const std::exception& ex) {
        std::cerr << "Failed to push marker LSL sample: " << ex.what() << std::endl;
        destroy();
    }
}
