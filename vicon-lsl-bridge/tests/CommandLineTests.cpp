#include "CommandLine.h"
#include "TestSupport.h"

TEST_CASE("Command line parser returns defaults") {
    const auto parsed = vicon_lsl::parseCommandLine({"bridge"});
    REQUIRE_EQ(parsed.action, vicon_lsl::CommandLineAction::Run);
    REQUIRE_EQ(parsed.config.vicon_server, std::string("localhost:801"));
    REQUIRE_EQ(parsed.config.marker_stream_name, std::string("ViconMarkers"));
    REQUIRE_EQ(parsed.config.segment_stream_name, std::string("ViconSegments"));
}

TEST_CASE("Command line parser handles valid options") {
    const auto parsed = vicon_lsl::parseCommandLine({
        "bridge",
        "--server", "192.168.1.10:801",
        "--marker-stream", "Markers",
        "--segment-stream", "Segments",
        "--reconnect-interval", "42",
    });

    REQUIRE_EQ(parsed.action, vicon_lsl::CommandLineAction::Run);
    REQUIRE_EQ(parsed.config.vicon_server, std::string("192.168.1.10:801"));
    REQUIRE_EQ(parsed.config.marker_stream_name, std::string("Markers"));
    REQUIRE_EQ(parsed.config.segment_stream_name, std::string("Segments"));
    REQUIRE_EQ(parsed.config.reconnect_interval_ms, 42);
}

TEST_CASE("Command line parser handles help and errors") {
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--help"}).action,
               vicon_lsl::CommandLineAction::Help);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--bad"}).action,
               vicon_lsl::CommandLineAction::Error);
    for (const std::string option : {
             "--server",
             "--marker-stream",
             "--segment-stream",
             "--reconnect-interval",
         }) {
        REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", option}).action,
                   vicon_lsl::CommandLineAction::Error);
    }
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--no-hololens-gaze"}).action,
               vicon_lsl::CommandLineAction::Error);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--gaze-port", "16000"}).action,
               vicon_lsl::CommandLineAction::Error);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--gaze-stream", "Gaze"}).action,
               vicon_lsl::CommandLineAction::Error);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--reconnect-interval", "0"}).action,
               vicon_lsl::CommandLineAction::Error);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--reconnect-interval", "-1"}).action,
               vicon_lsl::CommandLineAction::Error);
}

TEST_CASE("Startup diagnostics include configured fields") {
    Config config;
    const auto startup = vicon_lsl::formatStartupDiagnostics(config);
    REQUIRE(startup.find("Vicon-LSL Bridge") != std::string::npos);
    REQUIRE(startup.find("Marker stream: ViconMarkers") != std::string::npos);
    REQUIRE(startup.find("HoloLens gaze") == std::string::npos);
}
