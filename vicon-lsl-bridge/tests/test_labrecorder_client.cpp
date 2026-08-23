#include "LabRecorderClientTestSupport.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    using namespace labrecorder_client_tests;
    testFilenameCommand();
    testRenderedFilenameUsesSharedSanitization();
    testUnresolvedFilenamePlaceholders();
    testStartRecordingCommands();
    testBridgeWindowSettingsContract();
    testRuntimePolicy();
    testTcpCommandSequence();
    testTcpStartRecordingSequenceWithSelectAll();
    testFragmentedReplyControlsCommandProgress();
    testConnectionTimeoutDoesNotShortenCommandTimeout();
    testCommandTimeoutDisconnectsAndDropsQueuedWork();
    testMidCommandDisconnectReportsFailure();
    testConnectionStateTracksIdleDisconnectAndReconnect();

    if (g_failures > 0) {
        std::cerr << g_failures << " test failure(s)" << std::endl;
        return 1;
    }

    std::cout << "All LabRecorder tests passed" << std::endl;
    return 0;
}
