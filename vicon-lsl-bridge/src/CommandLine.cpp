#include "CommandLine.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace {

bool parsePositiveInt(const char* text, int min_value, int max_value, int& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    long parsed = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (parsed < min_value || parsed > max_value) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

bool hasValue(int index, int argc, char* argv[]) {
    return index + 1 < argc && argv[index + 1] != nullptr && argv[index + 1][0] != '-';
}

} // namespace

std::string usageText(const char* program) {
    std::ostringstream out;
    out << "Usage: " << program << " [options]\n"
        << "Options:\n"
        << "  --server <ip:port>          Vicon server address (default: localhost:801)\n"
        << "  --marker-stream <name>      LSL marker stream name (default: ViconMarkers)\n"
        << "  --segment-stream <name>     LSL segment stream name (default: ViconSegments)\n"
        << "  --no-hololens-gaze          Disable embedded HoloLens gaze UDP-to-LSL receiver\n"
        << "  --gaze-port <port>          HoloLens gaze UDP port, 1-65535 (default: 16571)\n"
        << "  --gaze-stream <name>        HoloLens gaze LSL stream name (default: HoloLensGaze)\n"
        << "  --reconnect-interval <ms>   Reconnection interval in ms, positive (default: 3000)\n"
        << "  --help                      Show this help message\n";
    return out.str();
}

ParseResult parseCommandLine(int argc, char* argv[]) {
    ParseResult result;
    result.config = Config{};

    for (int i = 1; i < argc; ++i) {
        const char* option = argv[i];

        if (std::strcmp(option, "--help") == 0) {
            result.ok = true;
            result.help_requested = true;
            return result;
        }

        if (std::strcmp(option, "--server") == 0) {
            if (!hasValue(i, argc, argv)) {
                result.error = "--server requires a value";
                return result;
            }
            result.config.vicon_server = argv[++i];
        } else if (std::strcmp(option, "--marker-stream") == 0) {
            if (!hasValue(i, argc, argv)) {
                result.error = "--marker-stream requires a value";
                return result;
            }
            result.config.marker_stream_name = argv[++i];
        } else if (std::strcmp(option, "--segment-stream") == 0) {
            if (!hasValue(i, argc, argv)) {
                result.error = "--segment-stream requires a value";
                return result;
            }
            result.config.segment_stream_name = argv[++i];
        } else if (std::strcmp(option, "--no-hololens-gaze") == 0) {
            result.config.enable_hololens_gaze = false;
        } else if (std::strcmp(option, "--gaze-port") == 0) {
            if (!hasValue(i, argc, argv)) {
                result.error = "--gaze-port requires a value";
                return result;
            }
            int port = 0;
            if (!parsePositiveInt(argv[++i], 1, 65535, port)) {
                result.error = "--gaze-port must be an integer from 1 to 65535";
                return result;
            }
            result.config.hololens_gaze_port = static_cast<unsigned short>(port);
        } else if (std::strcmp(option, "--gaze-stream") == 0) {
            if (!hasValue(i, argc, argv)) {
                result.error = "--gaze-stream requires a value";
                return result;
            }
            result.config.hololens_gaze_stream_name = argv[++i];
        } else if (std::strcmp(option, "--reconnect-interval") == 0) {
            if (!hasValue(i, argc, argv)) {
                result.error = "--reconnect-interval requires a value";
                return result;
            }
            int interval = 0;
            if (!parsePositiveInt(argv[++i], 1, INT_MAX, interval)) {
                result.error = "--reconnect-interval must be a positive integer";
                return result;
            }
            result.config.reconnect_interval_ms = interval;
        } else {
            result.error = std::string("Unknown option: ") + option;
            return result;
        }
    }

    result.ok = true;
    return result;
}
