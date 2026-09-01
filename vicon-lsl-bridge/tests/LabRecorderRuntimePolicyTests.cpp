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

    const RecorderConnectionState connections[] = {
        RecorderConnectionState::Disconnected,
        RecorderConnectionState::Connecting,
        RecorderConnectionState::Connected,
        RecorderConnectionState::Error,
    };
    const RecorderRecordingState recordings[] = {
        RecorderRecordingState::Unknown,
        RecorderRecordingState::Stopped,
        RecorderRecordingState::Recording,
    };
    const RecorderOperationState operations[] = {
        RecorderOperationState::Idle,
        RecorderOperationState::Refreshing,
        RecorderOperationState::UpdatingFilename,
        RecorderOperationState::Starting,
        RecorderOperationState::Stopping,
        RecorderOperationState::ShuttingDown,
    };
    int combinations = 0;
    for (const auto connection : connections) {
        for (const auto recording : recordings) {
            for (const auto operation : operations) {
                for (const bool shutdown : {false, true}) {
                    ++combinations;
                    const bool expected_start =
                        connection == RecorderConnectionState::Connected &&
                        recording != RecorderRecordingState::Recording &&
                        operation == RecorderOperationState::Idle && !shutdown;
                    const bool expected_stop =
                        connection == RecorderConnectionState::Connected &&
                        recording != RecorderRecordingState::Stopped &&
                        operation != RecorderOperationState::Stopping &&
                        operation != RecorderOperationState::ShuttingDown &&
                        !shutdown;
                    const bool expected_refresh = expected_start;
                    expect(LabRecorderRuntimePolicy::canStartRecording(
                               connection, recording, operation, shutdown) ==
                               expected_start,
                           "Start policy matches the complete recorder state model");
                    expect(LabRecorderRuntimePolicy::canStopRecording(
                               connection, recording, operation, shutdown) ==
                               expected_stop,
                           "Stop policy matches the complete recorder state model");
                    expect(LabRecorderRuntimePolicy::canRefreshStreams(
                               connection, recording, operation, shutdown) ==
                               expected_refresh,
                           "Refresh policy matches the complete recorder state model");
                }
            }
        }
    }
    expect(combinations == 144,
           "state policy covers every connection recording operation and shutdown combination");
}

} // namespace labrecorder_client_tests
