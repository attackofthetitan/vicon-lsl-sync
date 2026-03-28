#include "ViconLSLBridge.h"
#include <lsl_cpp.h>
#include <cmath>
#include <chrono>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

ViconLSLBridge::ViconLSLBridge(const Config& config)
    : config_(config), client_(config.vicon_server) {}

void ViconLSLBridge::stop() {
    running_ = false;
}

void ViconLSLBridge::run() {
    while (running_) {
        connectWithRetry();
        if (!running_) break;

        if (!client_.getFrame()) {
            std::cerr << "Failed to get initial frame, reconnecting" << std::endl;
            client_.disconnect();
            continue;
        }

        initializeStreams();

        std::cout << "Streaming started" << std::endl;
        while (running_ && client_.isConnected()) {
            if (!client_.getFrame()) {
                std::cerr << "Lost connection, will reconnect" << std::endl;
                break;
            }

            double timestamp = lsl::local_clock();
            streamFrame(timestamp);

            frame_count_++;
            if (frame_count_ % 100 == 0) {
                if (checkLayoutChanged()) {
                    std::cout << "Layout changed, reinitializing streams" << std::endl;
                    marker_stream_.destroy();
                    segment_stream_.destroy();
                    initializeStreams();
                }
            }
        }

        marker_stream_.destroy();
        segment_stream_.destroy();
        client_.disconnect();
        frame_count_ = 0;
    }

    std::cout << "Stopped" << std::endl;
}

void ViconLSLBridge::connectWithRetry() {
    while (running_ && !client_.connect()) {
        std::cerr << "Retrying in " << config_.reconnect_interval_ms << "ms" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnect_interval_ms));
    }
}

void ViconLSLBridge::initializeStreams() {
    known_markers_.clear();
    known_segments_.clear();

    unsigned int subject_count = client_.getSubjectCount();
    for (unsigned int s = 0; s < subject_count; ++s) {
        std::string subject = client_.getSubjectName(s);

        unsigned int marker_count = client_.getMarkerCount(subject);
        for (unsigned int m = 0; m < marker_count; ++m) {
            known_markers_.emplace_back(subject, client_.getMarkerName(subject, m));
        }

        unsigned int segment_count = client_.getSegmentCount(subject);
        for (unsigned int seg = 0; seg < segment_count; ++seg) {
            known_segments_.emplace_back(subject, client_.getSegmentName(subject, seg));
        }
    }

    std::string hostname = "default";
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) {
        hostname = buf;
    }

    marker_stream_.initialize(known_markers_, config_.marker_stream_name,
                               config_.source_id_prefix + "markers_" + hostname);
    segment_stream_.initialize(known_segments_, config_.segment_stream_name,
                                config_.source_id_prefix + "segments_" + hostname);
}

bool ViconLSLBridge::checkLayoutChanged() {
    std::vector<std::pair<std::string, std::string>> current_markers;
    std::vector<std::pair<std::string, std::string>> current_segments;

    unsigned int subject_count = client_.getSubjectCount();
    for (unsigned int s = 0; s < subject_count; ++s) {
        std::string subject = client_.getSubjectName(s);

        unsigned int marker_count = client_.getMarkerCount(subject);
        for (unsigned int m = 0; m < marker_count; ++m) {
            current_markers.emplace_back(subject, client_.getMarkerName(subject, m));
        }

        unsigned int segment_count = client_.getSegmentCount(subject);
        for (unsigned int seg = 0; seg < segment_count; ++seg) {
            current_segments.emplace_back(subject, client_.getSegmentName(subject, seg));
        }
    }

    return current_markers != known_markers_ || current_segments != known_segments_;
}

void ViconLSLBridge::streamFrame(double timestamp) {
    std::vector<std::array<double, 4>> marker_data(known_markers_.size());
    for (size_t i = 0; i < known_markers_.size(); ++i) {
        double x, y, z;
        bool occluded;
        if (client_.getMarkerGlobalTranslation(known_markers_[i].first, known_markers_[i].second,
                                                x, y, z, occluded)) {
            if (occluded) {
                marker_data[i] = {std::nan(""), std::nan(""), std::nan(""), 0.0};
            } else {
                marker_data[i] = {x, y, z, 1.0};
            }
        } else {
            marker_data[i] = {std::nan(""), std::nan(""), std::nan(""), 0.0};
        }
    }
    marker_stream_.pushSample(marker_data, timestamp);

    std::vector<std::array<double, 7>> segment_data(known_segments_.size());
    for (size_t i = 0; i < known_segments_.size(); ++i) {
        double x, y, z, qx, qy, qz, qw;
        bool pos_ok = client_.getSegmentGlobalTranslation(
            known_segments_[i].first, known_segments_[i].second, x, y, z);
        bool rot_ok = client_.getSegmentGlobalRotationQuaternion(
            known_segments_[i].first, known_segments_[i].second, qx, qy, qz, qw);

        if (pos_ok && rot_ok) {
            segment_data[i] = {x, y, z, qx, qy, qz, qw};
        } else {
            segment_data[i] = {std::nan(""), std::nan(""), std::nan(""),
                               std::nan(""), std::nan(""), std::nan(""), std::nan("")};
        }
    }
    segment_stream_.pushSample(segment_data, timestamp);
}
