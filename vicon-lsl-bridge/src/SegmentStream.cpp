#include "SegmentStream.h"
#include "StreamSchema.h"

#include <exception>
#include <iostream>
#include <utility>

void SegmentStream::initialize(const std::vector<std::pair<std::string, std::string>>& segment_names,
                                const std::string& stream_name,
                                const std::string& source_id) {
    destroy();
    configured_ = true;
    segment_names_ = segment_names;
    if (segment_names_.empty()) {
        std::cout << "No segments discovered; segment stream not created" << std::endl;
        return;
    }

    int channel_count = static_cast<int>(segment_names_.size()) * 7;

    info_ = std::make_unique<lsl::stream_info>(
        stream_name, "MoCap", channel_count,
        lsl::IRREGULAR_RATE, lsl::cf_double64, source_id);

    lsl::xml_element channels = info_->desc().append_child("channels");
    for (const auto& spec : vicon_lsl::buildSegmentStreamSchema(segment_names_, stream_name).channels) {
        auto channel = channels.append_child("channel");
        channel.append_child_value("label", spec.label);
        channel.append_child_value("unit", spec.unit);
    }

    sample_buffer_.resize(channel_count);
    outlet_ = std::make_unique<lsl::stream_outlet>(*info_);

    std::cout << "Segment stream ready, " << segment_names_.size()
              << " segments, " << channel_count << " channels" << std::endl;
}

void SegmentStream::destroy() {
    const bool was_initialized = outlet_ != nullptr || info_ != nullptr;
    outlet_.reset();
    info_.reset();
    segment_names_.clear();
    sample_buffer_.clear();
    configured_ = false;
    if (was_initialized) {
        std::cout << "Segment stream closed" << std::endl;
    }
}

bool SegmentStream::isInitialized() const {
    return configured_ && (segment_names_.empty() || outlet_ != nullptr);
}

StreamPushResult SegmentStream::pushSample(
    const std::vector<vicon_lsl::SegmentObjectRead>& segments,
    double timestamp) {
    if (!configured_) return StreamPushResult::NotConfigured;
    if (segment_names_.empty()) return StreamPushResult::Pushed;
    if (!outlet_) return StreamPushResult::Failed;

    std::vector<vicon_lsl::SegmentSample> samples;
    samples.reserve(segments.size());
    for (const auto& segment : segments) {
        samples.push_back(
            vicon_lsl::segmentSampleForLsl(segment.translation, segment.rotation));
    }
    auto flattened = vicon_lsl::flattenSegmentSamples(samples);
    if (flattened.size() != sample_buffer_.size()) {
        std::cerr << "Segment sample channel mismatch: expected " << sample_buffer_.size()
                  << ", got " << flattened.size() << std::endl;
        return StreamPushResult::Failed;
    }
    sample_buffer_ = std::move(flattened);

    try {
        outlet_->push_sample(sample_buffer_, timestamp);
        return StreamPushResult::Pushed;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to push segment LSL sample: " << ex.what() << std::endl;
        destroy();
        return StreamPushResult::Failed;
    }
}
