#include "CommandLine.h"

#include <charconv>
#include <limits>
#include <sstream>
#include <utility>

namespace vicon_lsl {
namespace {

CommandLineResult helpResult(const Config& config) {
    CommandLineResult result;
    result.action = CommandLineAction::Help;
    result.config = config;
    return result;
}

CommandLineResult errorResult(const Config& config, std::string message) {
    CommandLineResult result;
    result.action = CommandLineAction::Error;
    result.config = config;
    result.message = std::move(message);
    return result;
}

bool parseInt(std::string_view text, int min_value, int max_value, int& value) {
    if (text.empty()) {
        return false;
    }

    int parsed = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        return false;
    }
    if (parsed < min_value || parsed > max_value) {
        return false;
    }

    value = parsed;
    return true;
}

bool needsValue(std::string_view option) {
    return option == "--server" || option == "--marker-stream" ||
           option == "--segment-stream" || option == "--gaze-port" ||
           option == "--gaze-stream" || option == "--reconnect-interval";
}

} // namespace

CommandLineResult parseCommandLine(int argc, const char* const argv[]) {
    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<std::size_t>(argc) : 0);
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i] == nullptr ? "" : argv[i]);
    }
    return parseCommandLine(args);
}

CommandLineResult parseCommandLine(const std::vector<std::string>& args) {
    Config config;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& option = args[i];

        if (option == "--help") {
            return helpResult(config);
        }

        if (needsValue(option) && i + 1 >= args.size()) {
            return errorResult(config, "Missing value for option: " + option);
        }

        if (option == "--server") {
            config.vicon_server = args[++i];
        } else if (option == "--marker-stream") {
            config.marker_stream_name = args[++i];
        } else if (option == "--segment-stream") {
            config.segment_stream_name = args[++i];
        } else if (option == "--no-hololens-gaze") {
            config.enable_hololens_gaze = false;
        } else if (option == "--gaze-port") {
            int port = 0;
            const std::string& value = args[++i];
            if (!parseInt(value, 1, std::numeric_limits<unsigned short>::max(), port)) {
                return errorResult(config, "Invalid UDP port for --gaze-port: " + value);
            }
            config.hololens_gaze_port = static_cast<unsigned short>(port);
        } else if (option == "--gaze-stream") {
            config.hololens_gaze_stream_name = args[++i];
        } else if (option == "--reconnect-interval") {
            int interval_ms = 0;
            const std::string& value = args[++i];
            if (!parseInt(value, 1, std::numeric_limits<int>::max(), interval_ms)) {
                return errorResult(config, "Invalid milliseconds for --reconnect-interval: " + value);
            }
            config.reconnect_interval_ms = interval_ms;
        } else {
            return errorResult(config, "Unknown option: " + option);
        }
    }

    CommandLineResult result;
    result.config = config;
    return result;
}

std::string formatUsage(std::string_view program) {
    std::ostringstream out;
    out << "Usage: " << program << " [options]\n"
        << "Options:\n"
        << "  --server <ip:port>          Vicon server address (default: localhost:801)\n"
        << "  --marker-stream <name>      LSL marker stream name (default: ViconMarkers)\n"
        << "  --segment-stream <name>     LSL segment stream name (default: ViconSegments)\n"
        << "  --no-hololens-gaze          Disable embedded HoloLens gaze UDP-to-LSL receiver\n"
        << "  --gaze-port <port>          HoloLens gaze UDP port (default: 16571)\n"
        << "  --gaze-stream <name>        HoloLens gaze LSL stream name (default: HoloLensGaze)\n"
        << "  --reconnect-interval <ms>   Reconnection interval in ms (default: 3000)\n"
        << "  --help                      Show this help message\n";
    return out.str();
}

std::string formatStartupDiagnostics(const Config& config) {
    std::ostringstream out;
    out << "Vicon-LSL Bridge\n"
        << "  Server: " << config.vicon_server << "\n"
        << "  Marker stream: " << config.marker_stream_name << "\n"
        << "  Segment stream: " << config.segment_stream_name << "\n";

    if (config.enable_hololens_gaze) {
        out << "  HoloLens gaze stream: " << config.hololens_gaze_stream_name
            << " on UDP port " << config.hololens_gaze_port << "\n";
    } else {
        out << "  HoloLens gaze stream: disabled\n";
    }

    out << "Press Ctrl+C to stop.\n";
    return out.str();
}

} // namespace vicon_lsl
