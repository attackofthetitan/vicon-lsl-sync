#include "gui/LabRecorderRuntimePolicy.h"

#include <QDir>
#include <QFileInfo>

QString LabRecorderRuntimePolicy::resolveExecutable(
    const QString& configured_path,
    const QString& application_directory) {
    const QFileInfo configured(configured_path.trimmed());
    if (!configured_path.trimmed().isEmpty() && configured.exists() && configured.isFile()) {
        return QDir::toNativeSeparators(configured.absoluteFilePath());
    }

    const QFileInfo bundled(
        QDir(application_directory).filePath("labrecorder/LabRecorder.exe"));
    if (bundled.exists() && bundled.isFile()) {
        return QDir::toNativeSeparators(bundled.absoluteFilePath());
    }
    return {};
}

bool LabRecorderRuntimePolicy::retryExpired(qint64 elapsed_ms) {
    return elapsed_ms >= RetryTimeoutMs;
}

bool LabRecorderRuntimePolicy::shouldAttemptConnection(
    RecorderConnectionState state,
    qint64 elapsed_ms) {
    return elapsed_ms >= 0 && !retryExpired(elapsed_ms) &&
           state != RecorderConnectionState::Connected &&
           state != RecorderConnectionState::Connecting;
}

bool LabRecorderRuntimePolicy::canRefreshStreams(
    RecorderConnectionState connection_state,
    RecorderRecordingState recording_state,
    RecorderOperationState operation_state,
    bool shutdown_requested) {
    return connection_state == RecorderConnectionState::Connected &&
           recording_state != RecorderRecordingState::Recording &&
           operation_state == RecorderOperationState::Idle &&
           !shutdown_requested;
}

bool LabRecorderRuntimePolicy::canStartRecording(
    RecorderConnectionState connection_state,
    RecorderRecordingState recording_state,
    RecorderOperationState operation_state,
    bool shutdown_requested) {
    return connection_state == RecorderConnectionState::Connected &&
           recording_state != RecorderRecordingState::Recording &&
           operation_state == RecorderOperationState::Idle &&
           !shutdown_requested;
}

bool LabRecorderRuntimePolicy::canStopRecording(
    RecorderConnectionState connection_state,
    RecorderRecordingState recording_state,
    RecorderOperationState operation_state,
    bool shutdown_requested) {
    return connection_state == RecorderConnectionState::Connected &&
           recording_state != RecorderRecordingState::Stopped &&
           operation_state != RecorderOperationState::Stopping &&
           operation_state != RecorderOperationState::ShuttingDown &&
           !shutdown_requested;
}
