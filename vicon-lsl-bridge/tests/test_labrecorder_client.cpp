#include "LabRecorderClientTestSupport.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    using namespace labrecorder_client_tests;
    const auto run = [](const char* name, auto function) {
        std::cout << "[run] " << name << std::endl;
        function();
    };
    run("filename command", testFilenameCommand);
    run("rendered filename", testRenderedFilenameUsesSharedSanitization);
    run("unresolved placeholders", testUnresolvedFilenamePlaceholders);
    run("start commands", testStartRecordingCommands);
    run("TCP command sequence", testTcpCommandSequence);
    run("TCP Start sequence", testTcpStartRecordingSequenceWithSelectAll);
    run("fragmented reply", testFragmentedReplyControlsCommandProgress);
    run("separate timeouts", testConnectionTimeoutDoesNotShortenCommandTimeout);
    run("command timeout", testCommandTimeoutDisconnectsAndDropsQueuedWork);
    run("mid-command disconnect", testMidCommandDisconnectReportsFailure);
    run("reconnect", testConnectionStateTracksIdleDisconnectAndReconnect);
    run("normalized path policy", testNormalizedPathPolicy);
    run("session configuration", testSessionConfiguration);
    run("session event log", testSessionEventLog);
    run("calibration profiles", testCalibrationProfileStore);
    run("selected-stream recorder", testSelectedStreamRecorderPolicy);
    run("duplicate and shutdown protocol", testRecorderDuplicateAndShutdownProtocol);
    run("recording verifier", testRecordingVerifierOutcomes);
    run("recorder process lifecycle", testRecorderProcessControllerLifecycle);
    run("bundled executable resolution", testBundledExecutableResolution);

    if (g_failures > 0) {
        std::cerr << g_failures << " test failure(s)" << std::endl;
        return 1;
    }

    std::cout << "All LabRecorder tests passed" << std::endl;
    return 0;
}
