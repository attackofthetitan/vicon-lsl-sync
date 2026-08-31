#pragma once

#include "HoloLensGazeSchema.h"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace preview_core_test_support {

class TemporaryFilePath {
public:
    explicit TemporaryFilePath(const char* suffix) {
        static std::atomic<unsigned long long> sequence{0};
        path_ = std::filesystem::temp_directory_path() /
                ("vicon_lsl_preview_test_" + std::to_string(++sequence) + suffix);
    }

    ~TemporaryFilePath() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    std::string string() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

inline bool near(double left, double right, double tolerance = 1e-9) {
    return std::abs(left - right) <= tolerance;
}

inline std::vector<std::string> gazeLabels() {
    std::vector<std::string> labels;
    for (const auto& channel : vicon_lsl::holoLensGazeChannels()) {
        labels.emplace_back(channel.label);
    }
    return labels;
}

inline std::vector<std::string> calibrationLabels() {
    return {
        "PositionX", "PositionY", "PositionZ",
        "RotationX", "RotationY", "RotationZ", "RotationW", "Tracked",
    };
}

} // namespace preview_core_test_support
