#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTcpSocket>

#include <functional>
#include <iostream>
#include <string>

namespace labrecorder_client_tests {

inline int g_failures = 0;

inline void expect(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << std::endl;
        ++g_failures;
    }
}

inline bool waitUntil(const std::function<bool()>& condition, int timeout_ms = 1000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

inline QString readCommand(QTcpSocket* socket) {
    if (!waitUntil([socket]() { return socket->canReadLine(); })) {
        return {};
    }
    return QString::fromUtf8(socket->readLine()).trimmed();
}

inline bool writeReply(QTcpSocket* socket, const QByteArray& reply) {
    if (socket->write(reply) != reply.size()) {
        return false;
    }
    return waitUntil([socket]() { return socket->bytesToWrite() == 0; });
}

void testFilenameCommand();
void testRenderedFilenameUsesSharedSanitization();
void testUnresolvedFilenamePlaceholders();
void testStartRecordingCommands();
void testTcpCommandSequence();
void testTcpStartRecordingSequenceWithSelectAll();
void testFragmentedReplyControlsCommandProgress();
void testConnectionTimeoutDoesNotShortenCommandTimeout();
void testCommandTimeoutDisconnectsAndDropsQueuedWork();
void testMidCommandDisconnectReportsFailure();
void testConnectionStateTracksIdleDisconnectAndReconnect();
void testNormalizedPathPolicy();
void testSessionConfiguration();
void testSessionEventLog();
void testCalibrationProfileStore();
void testSelectedStreamRecorderPolicy();
void testRecorderDuplicateAndShutdownProtocol();
void testRecordingVerifierOutcomes();
void testRecorderProcessControllerLifecycle();
void testBundledExecutableResolution();

} // namespace labrecorder_client_tests
