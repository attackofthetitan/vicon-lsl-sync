#include "CommandLine.h"
#include "HoloLensGazePacket.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << std::endl;
        ++g_failures;
    }
}

std::vector<char*> argvFrom(std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    return argv;
}

ParseResult parseArgs(std::vector<std::string> args) {
    std::vector<char*> argv = argvFrom(args);
    return parseCommandLine(static_cast<int>(argv.size()), argv.data());
}

std::string gazePacket(size_t channel_count) {
    std::ostringstream packet;
    packet << "HLGAZE1";
    for (size_t i = 0; i < channel_count; ++i) {
        packet << ',' << (i + 1);
    }
    return packet.str();
}

void testGazePacketParser() {
    const auto parsed = vicon_lsl::parseHoloLensGazePacket(gazePacket(21));
    expect(parsed.ok(),
           "accepts 21-channel gaze packet");
    expect(parsed.packet.sample[0] == 1.0 && parsed.packet.sample[20] == 21.0,
           "maps 21 gaze values");

    expect(!vicon_lsl::parseHoloLensGazePacket("BAD,1,2,3").ok(),
           "rejects wrong gaze prefix");
    expect(!vicon_lsl::parseHoloLensGazePacket(gazePacket(20)).ok(),
           "rejects short gaze packet");
    expect(!vicon_lsl::parseHoloLensGazePacket(gazePacket(26)).ok(),
           "rejects legacy overlong gaze packet");

    std::string malformed = gazePacket(21);
    size_t last_comma = malformed.rfind(',');
    malformed.replace(last_comma + 1, std::string::npos, "not-a-number");
    expect(!vicon_lsl::parseHoloLensGazePacket(malformed).ok(),
           "rejects malformed gaze numeric field");
}

void testCommandLineParser() {
    ParseResult defaults = parseArgs({"bridge"});
    expect(defaults.ok && !defaults.help_requested, "accepts default CLI options");
    expect(defaults.config.vicon_server == "localhost:801", "uses default server");

    ParseResult custom = parseArgs({
        "bridge",
        "--server", "192.168.1.100:801",
        "--marker-stream", "Markers",
        "--segment-stream", "Segments",
        "--no-hololens-gaze",
        "--gaze-port", "4444",
        "--gaze-stream", "Gaze",
        "--reconnect-interval", "250"
    });
    expect(custom.ok, "accepts valid custom CLI options");
    expect(custom.config.vicon_server == "192.168.1.100:801", "parses server");
    expect(custom.config.marker_stream_name == "Markers", "parses marker stream");
    expect(custom.config.segment_stream_name == "Segments", "parses segment stream");
    expect(!custom.config.enable_hololens_gaze, "parses gaze disable flag");
    expect(custom.config.hololens_gaze_port == 4444, "parses gaze port");
    expect(custom.config.hololens_gaze_stream_name == "Gaze", "parses gaze stream");
    expect(custom.config.reconnect_interval_ms == 250, "parses reconnect interval");

    expect(parseArgs({"bridge", "--help"}).help_requested, "parses help");
    expect(!parseArgs({"bridge", "--server"}).ok, "rejects missing server value");
    expect(!parseArgs({"bridge", "--gaze-port", "0"}).ok, "rejects low gaze port");
    expect(!parseArgs({"bridge", "--gaze-port", "65536"}).ok, "rejects high gaze port");
    expect(!parseArgs({"bridge", "--gaze-port", "abc"}).ok, "rejects nonnumeric gaze port");
    expect(!parseArgs({"bridge", "--reconnect-interval", "0"}).ok, "rejects zero reconnect interval");
    expect(!parseArgs({"bridge", "--unknown"}).ok, "rejects unknown option");
}

} // namespace

int main() {
    testGazePacketParser();
    testCommandLineParser();

    if (g_failures > 0) {
        std::cerr << g_failures << " test failure(s)" << std::endl;
        return 1;
    }

    std::cout << "All tests passed" << std::endl;
    return 0;
}
