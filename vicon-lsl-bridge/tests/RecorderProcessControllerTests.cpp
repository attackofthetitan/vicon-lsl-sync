#include "gui/RecorderProcessController.h"
#include "LabRecorderClientTestSupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace labrecorder_client_tests {

void testRecorderProcessControllerLifecycle() {
    using namespace vicon_lsl::gui;
    RecorderProcessController controller;
    controller.endOwnedProcess();
    expect(controller.state() == RecorderProcessState::External &&
               !controller.ownsRunningProcess(),
           "external recorder state cannot be ended as an owned process");

    QString error;
    expect(!controller.launchGraphicalRecorder(
               QDir::temp().filePath("missing-recorder-fixture.exe"), &error) &&
               controller.state() == RecorderProcessState::LaunchFailed &&
               !error.isEmpty(),
           "missing recorder executable produces an explicit launch-failed state");

    const QString fixture = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        "vicon-lsl-recorder-process-fixture.exe"
#else
        "vicon-lsl-recorder-process-fixture"
#endif
    );
    expect(QFileInfo::exists(fixture), "recorder process fixture is available");
    if (!QFileInfo::exists(fixture)) return;

    bool exited = false;
    bool expected_exit = true;
    QObject::connect(&controller, &RecorderProcessController::processExited,
                     [&exited, &expected_exit](int, bool expected,
                                               RecorderProcessKind) {
                         exited = true;
                         expected_exit = expected;
                     });
    expect(controller.launchGraphicalRecorder(fixture, &error),
           "custom graphical recorder launch begins asynchronously");
    expect(waitUntil([&controller]() {
               return controller.state() == RecorderProcessState::OwnedRunning ||
                      controller.state() == RecorderProcessState::OwnedExited;
           }),
           "custom recorder reaches an owned running or exited state");
    expect(waitUntil([&exited]() { return exited; }, 2000) && !expected_exit,
           "unrequested recorder process exit is reported as unexpected");
    const QByteArray output = controller.boundedOutput();
    expect(output.size() <= 64 * 1024,
           "recorder process output buffer stays within its fixed bound");
    expect(output.contains("cwd=" + QFileInfo(fixture).absolutePath().toLocal8Bit()),
           "recorder process uses the executable directory as its working directory");

    StreamIdentity stream;
    stream.name = "Gaze";
    stream.source_id = "gaze-source";
    stream.selected = true;
    exited = false;
    expected_exit = false;
    expect(controller.launchAllowlistRecorder(
               fixture, QDir::temp().filePath("process-test.xdf"), {stream}, &error),
           "allowlist recorder launch accepts an exact stream identity");
    expect(waitUntil([&controller]() {
               return controller.state() == RecorderProcessState::OwnedRunning;
           }),
           "allowlist recorder reports owned running state");
    expect(controller.stopAllowlistRecording(),
           "allowlist Stop is accepted exactly once");
    expect(!controller.stopAllowlistRecording(),
           "duplicate allowlist Stop is rejected");
    expect(waitUntil([&exited]() { return exited; }, 2000) && expected_exit,
           "requested allowlist exit is reported as expected");

    exited = false;
    expect(controller.launchAllowlistRecorder(
               fixture, QDir::temp().filePath("detached-test.xdf"), {stream}, &error),
           "second owned recorder can launch after prior exit");
    expect(waitUntil([&controller]() {
               return controller.state() == RecorderProcessState::OwnedRunning;
           }),
           "second owned recorder reaches running state");
    controller.detach();
    expect(controller.state() == RecorderProcessState::Detached &&
               !controller.ownsRunningProcess(),
           "Detach relinquishes ownership without terminating the process");
    expect(!waitUntil([&exited]() { return exited; }, 800),
           "detached process exit is no longer treated as an owned lifecycle event");
}

} // namespace labrecorder_client_tests
