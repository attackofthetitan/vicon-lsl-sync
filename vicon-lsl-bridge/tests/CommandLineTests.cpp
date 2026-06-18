#include "CommandLine.h"
#include "TestSupport.h"

TEST_CASE("Command line parser returns defaults") {
    const auto parsed = vicon_lsl::parseCommandLine({"bridge"});
    REQUIRE_EQ(parsed.action, vicon_lsl::CommandLineAction::Run);
    REQUIRE_EQ(parsed.config.vicon_server, std::string("localhost:801"));
    REQUIRE_EQ(parsed.config.marker_stream_name, std::string("ViconMarkers"));
    REQUIRE(parsed.config.enable_hololens_gaze);
}

TEST_CASE("Command line parser handles valid options") {
    const auto parsed = vicon_lsl::parseCommandLine({
        "bridge",
        "--server", "192.168.1.10:801",
        "--marker-stream", "Markers",
        "--segment-stream", "Segments",
        "--no-hololens-gaze",
        "--gaze-port", "16000",
        "--gaze-stream", "Gaze",
        "--reconnect-interval", "42",
    });

    REQUIRE_EQ(parsed.action, vicon_lsl::CommandLineAction::Run);
    REQUIRE_EQ(parsed.config.vicon_server, std::string("192.168.1.10:801"));
    REQUIRE_EQ(parsed.config.marker_stream_name, std::string("Markers"));
    REQUIRE_EQ(parsed.config.segment_stream_name, std::string("Segments"));
    REQUIRE(!parsed.config.enable_hololens_gaze);
    REQUIRE_EQ(parsed.config.hololens_gaze_port, static_cast<unsigned short>(16000));
    REQUIRE_EQ(parsed.config.hololens_gaze_stream_name, std::string("Gaze"));
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
             "--gaze-port",
             "--gaze-stream",
             "--reconnect-interval",
         }) {
        REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", option}).action,
                   vicon_lsl::CommandLineAction::Error);
    }
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--gaze-port", "0"}).action,
               vicon_lsl::CommandLineAction::Error);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--gaze-port", "abc"}).action,
               vicon_lsl::CommandLineAction::Error);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--gaze-port", "65536"}).action,
               vicon_lsl::CommandLineAction::Error);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--reconnect-interval", "0"}).action,
               vicon_lsl::CommandLineAction::Error);
    REQUIRE_EQ(vicon_lsl::parseCommandLine({"bridge", "--reconnect-interval", "-1"}).action,
               vicon_lsl::CommandLineAction::Error);
}

TEST_CASE("Startup diagnostics include configured fields") {
    Config config;
    config.enable_hololens_gaze = false;
    const auto startup = vicon_lsl::formatStartupDiagnostics(config);
    REQUIRE(startup.find("Vicon-LSL Bridge") != std::string::npos);
    REQUIRE(startup.find("HoloLens gaze stream: disabled") != std::string::npos);
}
