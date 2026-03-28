#include "MarkerStream.h"
#include <iostream>

void MarkerStream::initialize(const std::vector<std::pair<std::string, std::string>>& marker_names,
                               const std::string& stream_name,
                               const std::string& source_id) {
    marker_names_ = marker_names;
    int channel_count = static_cast<int>(marker_names_.size()) * 4;

    info_ = std::make_unique<lsl::stream_info>(
        stream_name, "MoCap", channel_count,
        lsl::IRREGULAR_RATE, lsl::cf_double64, source_id);

    lsl::xml_element channels = info_->desc().append_child("channels");
    for (const auto& [subject, marker] : marker_names_) {
        std::string prefix = subject + ":" + marker;

        auto chX = channels.append_child("channel");
        chX.append_child_value("label", prefix + ":X");
        chX.append_child_value("unit", "mm");

        auto chY = channels.append_child("channel");
        chY.append_child_value("label", prefix + ":Y");
        chY.append_child_value("unit", "mm");

        auto chZ = channels.append_child("channel");
        chZ.append_child_value("label", prefix + ":Z");
        chZ.append_child_value("unit", "mm");

        auto chV = channels.append_child("channel");
        chV.append_child_value("label", prefix + ":Valid");
        chV.append_child_value("unit", "bool");
    }

    sample_buffer_.resize(channel_count);
    outlet_ = std::make_unique<lsl::stream_outlet>(*info_);

    std::cout << "Marker stream ready, " << marker_names_.size()
              << " markers, " << channel_count << " channels" << std::endl;
}

void MarkerStream::destroy() {
    outlet_.reset();
    info_.reset();
    marker_names_.clear();
    sample_buffer_.clear();
    std::cout << "Marker stream closed" << std::endl;
}

bool MarkerStream::isInitialized() const {
    return outlet_ != nullptr;
}

void MarkerStream::pushSample(const std::vector<std::array<double, 4>>& markers, double timestamp) {
    if (!outlet_) return;

    for (size_t i = 0; i < markers.size(); ++i) {
        size_t offset = i * 4;
        sample_buffer_[offset + 0] = markers[i][0];
        sample_buffer_[offset + 1] = markers[i][1];
        sample_buffer_[offset + 2] = markers[i][2];
        sample_buffer_[offset + 3] = markers[i][3];
    }

    outlet_->push_sample(sample_buffer_, timestamp);
}

size_t MarkerStream::markerCount() const {
    return marker_names_.size();
}
