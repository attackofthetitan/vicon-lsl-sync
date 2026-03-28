#include "SegmentStream.h"
#include <iostream>

void SegmentStream::initialize(const std::vector<std::pair<std::string, std::string>>& segment_names,
                                const std::string& stream_name,
                                const std::string& source_id) {
    segment_names_ = segment_names;
    int channel_count = static_cast<int>(segment_names_.size()) * 7;

    info_ = std::make_unique<lsl::stream_info>(
        stream_name, "MoCap", channel_count,
        lsl::IRREGULAR_RATE, lsl::cf_double64, source_id);

    lsl::xml_element channels = info_->desc().append_child("channels");
    for (const auto& [subject, segment] : segment_names_) {
        std::string prefix = subject + ":" + segment;

        const char* pos_labels[] = {"X", "Y", "Z"};
        for (auto label : pos_labels) {
            auto ch = channels.append_child("channel");
            ch.append_child_value("label", prefix + ":" + label);
            ch.append_child_value("unit", "mm");
        }

        const char* rot_labels[] = {"QX", "QY", "QZ", "QW"};
        for (auto label : rot_labels) {
            auto ch = channels.append_child("channel");
            ch.append_child_value("label", prefix + ":" + label);
            ch.append_child_value("unit", "quaternion");
        }
    }

    sample_buffer_.resize(channel_count);
    outlet_ = std::make_unique<lsl::stream_outlet>(*info_);

    std::cout << "Segment stream ready, " << segment_names_.size()
              << " segments, " << channel_count << " channels" << std::endl;
}

void SegmentStream::destroy() {
    outlet_.reset();
    info_.reset();
    segment_names_.clear();
    sample_buffer_.clear();
    std::cout << "Segment stream closed" << std::endl;
}

bool SegmentStream::isInitialized() const {
    return outlet_ != nullptr;
}

void SegmentStream::pushSample(const std::vector<std::array<double, 7>>& segments, double timestamp) {
    if (!outlet_) return;

    for (size_t i = 0; i < segments.size(); ++i) {
        size_t offset = i * 7;
        for (int j = 0; j < 7; ++j) {
            sample_buffer_[offset + j] = segments[i][j];
        }
    }

    outlet_->push_sample(sample_buffer_, timestamp);
}

size_t SegmentStream::segmentCount() const {
    return segment_names_.size();
}
