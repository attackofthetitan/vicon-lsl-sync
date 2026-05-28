#include "CommandLine.h"
#include "HoloLensGazeReceiver.h"

#include <array>
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
    packet << "HLGAZE1,123.456";
    for (size_t i = 0; i < channel_count; ++i) {
        packet << ',' << (i + 1);
    }
    return packet.str();
}

void testGazePacketParser() {
    std::array<double, HoloLensGazeReceiver::ChannelCount> sample{};
    double timestamp = 0.0;

    expect(HoloLensGazeReceiver::parsePacket(gazePacket(21), timestamp, sample),
           "accepts 21-channel gaze packet");
    expect(timestamp == 123.456,
           "parses gaze packet timestamp");
    expect(sample[0] == 1.0 && sample[20] == 21.0,
           "maps 21 gaze values after timestamp");

    expect(!HoloLensGazeReceiver::parsePacket("BAD,123,1,2,3", timestamp, sample),
           "rejects wrong gaze prefix");
    expect(!HoloLensGazeReceiver::parsePacket(gazePacket(20), timestamp, sample),
           "rejects short gaze packet");
    expect(!HoloLensGazeReceiver::parsePacket(gazePacket(26), timestamp, sample),
           "rejects legacy 26-channel gaze packet");

    std::string malformed_timestamp = gazePacket(21);
    malformed_timestamp.replace(
        std::string("HLGAZE1,").size(),
        std::string("123.456").size(),
        "not-a-number");
    expect(!HoloLensGazeReceiver::parsePacket(malformed_timestamp, timestamp, sample),
           "rejects malformed gaze timestamp");

    std::string nan_timestamp = gazePacket(21);
    nan_timestamp.replace(
        std::string("HLGAZE1,").size(),
        std::string("123.456").size(),
        "NaN");
    expect(!HoloLensGazeReceiver::parsePacket(nan_timestamp, timestamp, sample),
           "rejects non-finite gaze timestamp");

    std::string malformed = gazePacket(21);
    size_t last_comma = malformed.rfind(',');
    malformed.replace(last_comma + 1, std::string::npos, "not-a-number");
    expect(!HoloLensGazeReceiver::parsePacket(malformed, timestamp, sample),
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
