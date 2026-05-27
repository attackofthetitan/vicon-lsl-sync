#include "CommandLine.h"
#include "ViconLSLBridge.h"

#include <csignal>
#include <iostream>

static ViconLSLBridge* g_bridge = nullptr;

void signalHandler(int sig) {
    std::cout << "\nCaught signal " << sig << ", stopping" << std::endl;
    if (g_bridge) {
        g_bridge->stop();
    }
}

int main(int argc, char* argv[]) {
    ParseResult parse_result = parseCommandLine(argc, argv);
    if (parse_result.help_requested) {
        std::cout << usageText(argv[0]);
        return 0;
    }
    if (!parse_result.ok) {
        std::cerr << parse_result.error << std::endl;
        std::cerr << usageText(argv[0]);
        return 1;
    }
    Config config = parse_result.config;

    std::cout << "Vicon-LSL Bridge" << std::endl;
    std::cout << "  Server: " << config.vicon_server << std::endl;
    std::cout << "  Marker stream: " << config.marker_stream_name << std::endl;
    std::cout << "  Segment stream: " << config.segment_stream_name << std::endl;
    if (config.enable_hololens_gaze) {
        std::cout << "  HoloLens gaze stream: " << config.hololens_gaze_stream_name
                  << " on UDP port " << config.hololens_gaze_port << std::endl;
    } else {
        std::cout << "  HoloLens gaze stream: disabled" << std::endl;
    }
    std::cout << "Press Ctrl+C to stop." << std::endl;

    ViconLSLBridge bridge(config);
    g_bridge = &bridge;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    bridge.run();

    g_bridge = nullptr;
    return 0;
}
