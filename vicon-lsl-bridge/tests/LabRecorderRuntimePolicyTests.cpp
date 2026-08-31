#include "LabRecorderClientTestSupport.h"
#include "gui/LabRecorderRuntimePolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace labrecorder_client_tests {

void testRuntimePolicy() {
    QTemporaryDir app_dir;
    expect(app_dir.isValid(), "creates temporary application directory");
    if (!app_dir.isValid()) return;

    const QString recorder_dir = QDir(app_dir.path()).filePath("labrecorder");
    expect(QDir().mkpath(recorder_dir), "creates bundled recorder directory");
    const QString bundled = QDir(recorder_dir).filePath("LabRecorder.exe");
    QFile bundled_file(bundled);
    expect(bundled_file.open(QIODevice::WriteOnly), "creates bundled recorder fixture");
    bundled_file.close();

    const QString custom = QDir(app_dir.path()).filePath("CustomRecorder.exe");
    QFile custom_file(custom);
    expect(custom_file.open(QIODevice::WriteOnly), "creates custom recorder fixture");
    custom_file.close();

    expect(LabRecorderRuntimePolicy::resolveExecutable(custom, app_dir.path()) ==
               QDir::toNativeSeparators(QFileInfo(custom).absoluteFilePath()),
           "valid saved recorder path takes precedence");
    expect(QFile::remove(custom), "removes custom recorder fixture");
    expect(LabRecorderRuntimePolicy::resolveExecutable(custom, app_dir.path()) ==
               QDir::toNativeSeparators(QFileInfo(bundled).absoluteFilePath()),
           "missing saved recorder path falls back to bundled recorder");
    expect(QFile::remove(bundled), "removes bundled recorder fixture");
    expect(LabRecorderRuntimePolicy::resolveExecutable(custom, app_dir.path()).isEmpty(),
           "missing saved and bundled recorder paths resolve empty");

    expect(LabRecorderRuntimePolicy::shouldAttemptConnection(
               RecorderConnectionState::Disconnected, 0),
           "disconnected recorder retries immediately");
    expect(!LabRecorderRuntimePolicy::shouldAttemptConnection(
               RecorderConnectionState::Connecting, 250),
           "connection in progress suppresses duplicate retry");
    expect(!LabRecorderRuntimePolicy::shouldAttemptConnection(
               RecorderConnectionState::Connected, 250),
           "connected recorder suppresses retry");
    expect(LabRecorderRuntimePolicy::retryExpired(15000),
           "retry window expires at fifteen seconds");
    expect(!LabRecorderRuntimePolicy::shouldAttemptConnection(
               RecorderConnectionState::Disconnected, 15000),
           "expired retry window suppresses connection attempts");

    expect(LabRecorderRuntimePolicy::canRefreshStreams(
               RecorderConnectionState::Connected, RecorderRecordingState::Unknown),
           "connected recorder with unknown state allows stream refresh");
    expect(LabRecorderRuntimePolicy::canStartRecording(
               RecorderConnectionState::Connected, RecorderRecordingState::Unknown),
           "connected recorder with unknown state allows recording start");
    expect(LabRecorderRuntimePolicy::canStopRecording(
               RecorderConnectionState::Connected, RecorderRecordingState::Unknown),
           "connected recorder with unknown state allows recording stop recovery");
    expect(!LabRecorderRuntimePolicy::canRefreshStreams(
               RecorderConnectionState::Connected, RecorderRecordingState::Recording),
           "known recording state blocks stream refresh");
    expect(!LabRecorderRuntimePolicy::canStartRecording(
               RecorderConnectionState::Connected, RecorderRecordingState::Recording),
           "known recording state blocks duplicate start");
    expect(!LabRecorderRuntimePolicy::canStopRecording(
               RecorderConnectionState::Connected, RecorderRecordingState::Stopped),
           "known stopped state blocks duplicate stop");
    expect(!LabRecorderRuntimePolicy::canStartRecording(
               RecorderConnectionState::Disconnected, RecorderRecordingState::Stopped),
           "disconnected recorder blocks recording controls");
}

} // namespace labrecorder_client_tests
