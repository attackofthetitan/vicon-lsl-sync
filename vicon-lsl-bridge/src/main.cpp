#include "Config.h"
#include "ViconLSLBridge.h"

#include <csignal>
#include <cstring>
#include <iostream>
#include <string>

static ViconLSLBridge* g_bridge = nullptr;

void signalHandler(int sig) {
    std::cout << "\nCaught signal " << sig << ", stopping" << std::endl;
    if (g_bridge) {
        g_bridge->stop();
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --server <ip:port>          Vicon server address (default: localhost:801)\n"
              << "  --marker-stream <name>      LSL marker stream name (default: ViconMarkers)\n"
              << "  --segment-stream <name>     LSL segment stream name (default: ViconSegments)\n"
              << "  --reconnect-interval <ms>   Reconnection interval in ms (default: 3000)\n"
              << "  --help                      Show this help message\n";
}

int main(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            config.vicon_server = argv[++i];
        } else if (std::strcmp(argv[i], "--marker-stream") == 0 && i + 1 < argc) {
            config.marker_stream_name = argv[++i];
        } else if (std::strcmp(argv[i], "--segment-stream") == 0 && i + 1 < argc) {
            config.segment_stream_name = argv[++i];
        } else if (std::strcmp(argv[i], "--reconnect-interval") == 0 && i + 1 < argc) {
            config.reconnect_interval_ms = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    std::cout << "Vicon-LSL Bridge" << std::endl;
    std::cout << "  Server: " << config.vicon_server << std::endl;
    std::cout << "  Marker stream: " << config.marker_stream_name << std::endl;
    std::cout << "  Segment stream: " << config.segment_stream_name << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    ViconLSLBridge bridge(config);
    g_bridge = &bridge;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    bridge.run();

    g_bridge = nullptr;
    return 0;
}
