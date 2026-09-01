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
    expect(controller.launchSelectedStreamRecorder(
               fixture, QDir::temp().filePath("process-test.xdf"), {stream}, &error),
           "selected-stream recorder launch accepts an exact stream identity");
    expect(waitUntil([&controller]() {
               return controller.state() == RecorderProcessState::OwnedRunning;
           }),
           "selected-stream recorder reports owned running state");
    expect(controller.stopSelectedStreamRecording(),
           "selected-stream Stop is accepted exactly once");
    expect(!controller.stopSelectedStreamRecording(),
           "duplicate selected-stream Stop is rejected");
    expect(waitUntil([&exited]() { return exited; }, 2000) && expected_exit,
           "requested selected-stream exit is reported as expected");

    exited = false;
    expect(controller.launchSelectedStreamRecorder(
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

void testBundledExecutableResolution() {
    using namespace vicon_lsl::gui;
    QTemporaryDir temp_dir;
    expect(temp_dir.isValid(), "temporary directory created for resolution tests");
    const QString root = temp_dir.path();

    // 1. Direct LabRecorderCLI in app directory
    QDir(root).mkdir("app1");
    const QString app1 = QDir(root).filePath("app1");
    QFile cli1(QDir(app1).filePath("LabRecorderCLI"));
    expect(cli1.open(QIODevice::WriteOnly), "create dummy flat LabRecorderCLI");
    cli1.close();
    expect(!RecorderProcessController::bundledSelectedStreamExecutable("", app1).isEmpty(),
           "resolves flat LabRecorderCLI binary");

    // 2. Subdirectory labrecorder/LabRecorderCLI
    QDir(root).mkdir("app2");
    const QString app2 = QDir(root).filePath("app2");
    QDir(app2).mkdir("labrecorder");
    QFile cli2(QDir(app2).filePath("labrecorder/LabRecorderCLI"));
    expect(cli2.open(QIODevice::WriteOnly), "create dummy nested LabRecorderCLI");
    cli2.close();
    expect(!RecorderProcessController::bundledSelectedStreamExecutable("", app2).isEmpty(),
           "resolves labrecorder/LabRecorderCLI binary in subfolder");

    // 3. macOS bundle layout: LabRecorder.app/Contents/MacOS/LabRecorder with sibling LabRecorderCLI
    QDir(root).mkdir("app3");
    const QString app3 = QDir(root).filePath("app3");
    expect(QDir(app3).mkpath("LabRecorder.app/Contents/MacOS"), "create macOS bundle directory");
    QFile macos_gui(QDir(app3).filePath("LabRecorder.app/Contents/MacOS/LabRecorder"));
    expect(macos_gui.open(QIODevice::WriteOnly), "create dummy macOS LabRecorder binary");
    macos_gui.close();
    QFile macos_cli(QDir(app3).filePath("LabRecorderCLI"));
    expect(macos_cli.open(QIODevice::WriteOnly), "create dummy sibling LabRecorderCLI");
    macos_cli.close();
    const QString resolved_cli = RecorderProcessController::bundledSelectedStreamExecutable(
        macos_gui.fileName(), app3);
    expect(!resolved_cli.isEmpty(), "resolves LabRecorderCLI alongside macOS LabRecorder.app bundle");
}

} // namespace labrecorder_client_tests
