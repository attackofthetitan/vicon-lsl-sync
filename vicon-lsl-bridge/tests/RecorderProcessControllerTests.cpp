#include "gui/RecorderProcessController.h"
#include "LabRecorderClientTestSupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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

    auto createFile = [](const QString& path) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        const bool opened = file.open(QIODevice::WriteOnly);
        file.close();
        return opened;
    };
    auto sameFile = [](const QString& actual, const QString& expected) {
        return QFileInfo(actual).canonicalFilePath() ==
               QFileInfo(expected).canonicalFilePath();
    };

    const QString flat_root = QDir(root).filePath("flat");
    const QString flat_gui = QDir(flat_root).filePath("LabRecorder");
    const QString bundled_gui = QDir(flat_root).filePath(
        "LabRecorder.app/Contents/MacOS/LabRecorder");
    const QString flat_cli = QDir(flat_root).filePath("LabRecorderCLI");
    expect(createFile(flat_gui) && createFile(bundled_gui) && createFile(flat_cli),
           "create flat and app-bundle recorder fixtures");
    expect(sameFile(RecorderProcessController::bundledGraphicalRecorderExecutable(
                        flat_root), bundled_gui),
           "prefers a macOS LabRecorder app bundle over its launcher wrapper");
    expect(sameFile(RecorderProcessController::bundledSelectedStreamExecutable(
                        bundled_gui, QDir(root).filePath("missing")), flat_cli),
           "resolves LabRecorderCLI beside a macOS LabRecorder app bundle");

    const QString nested_root = QDir(root).filePath("nested");
    const QString nested_gui = QDir(nested_root).filePath(
        "labrecorder/LabRecorder.app/Contents/MacOS/LabRecorder");
    const QString nested_cli = QDir(nested_root).filePath("labrecorder/LabRecorderCLI");
    expect(createFile(nested_gui) && createFile(nested_cli),
           "create nested recorder fixtures");
    expect(sameFile(RecorderProcessController::bundledGraphicalRecorderExecutable(
                        nested_root), nested_gui),
           "resolves a nested macOS LabRecorder app bundle");
    expect(sameFile(RecorderProcessController::bundledSelectedStreamExecutable(
                        QString{}, nested_root), nested_cli),
           "resolves a nested LabRecorderCLI binary");

    const QString package_root = QDir(root).filePath("package");
    const QString bridge_app_dir = QDir(package_root).filePath(
        "vicon-lsl-bridge-gui.app/Contents/MacOS");
    const QString package_gui = QDir(package_root).filePath(
        "LabRecorder.app/Contents/MacOS/LabRecorder");
    const QString package_cli = QDir(package_root).filePath("LabRecorderCLI");
    expect(QDir().mkpath(bridge_app_dir) && createFile(package_gui) &&
               createFile(package_cli),
           "create sibling macOS application bundle fixtures");
    expect(sameFile(RecorderProcessController::bundledGraphicalRecorderExecutable(
                        bridge_app_dir), package_gui),
           "resolves LabRecorder from beside the running bridge app bundle");
    expect(sameFile(RecorderProcessController::bundledSelectedStreamExecutable(
                        package_gui, bridge_app_dir), package_cli),
           "resolves LabRecorderCLI from beside sibling macOS app bundles");

    const QString empty_root = QDir(root).filePath("empty");
    expect(QDir().mkpath(empty_root), "create recorder-free application directory");
    expect(RecorderProcessController::bundledGraphicalRecorderExecutable(
               empty_root).isEmpty(),
           "does not resolve a missing graphical recorder");
    expect(RecorderProcessController::bundledSelectedStreamExecutable(
               QString{}, empty_root).isEmpty(),
           "does not resolve a missing selected-stream recorder");
}

} // namespace labrecorder_client_tests
