#include "CommandLine.h"
#include "ViconLSLBridge.h"

#include <csignal>
#include <iostream>
#include <string>

static ViconLSLBridge* g_bridge = nullptr;

void signalHandler(int sig) {
    std::cout << "\nCaught signal " << sig << ", stopping" << std::endl;
    if (g_bridge) {
        g_bridge->stop();
    }
}

int main(int argc, char* argv[]) {
    const auto parsed = vicon_lsl::parseCommandLine(
        argc, const_cast<const char* const*>(argv));
    const std::string program = argc > 0 ? argv[0] : "vicon-lsl-bridge";
    if (parsed.action == vicon_lsl::CommandLineAction::Help) {
        std::cout << vicon_lsl::formatUsage(program);
        return 0;
    }
    if (parsed.action == vicon_lsl::CommandLineAction::Error) {
        std::cerr << parsed.message << std::endl;
        std::cerr << vicon_lsl::formatUsage(program);
        return 1;
    }

    const Config& config = parsed.config;
    std::cout << vicon_lsl::formatStartupDiagnostics(config);

    ViconLSLBridge bridge(config);
    g_bridge = &bridge;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    bridge.run();

    g_bridge = nullptr;
    return 0;
}
