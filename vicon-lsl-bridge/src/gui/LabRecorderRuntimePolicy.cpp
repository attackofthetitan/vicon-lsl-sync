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

bool LabRecorderRuntimePolicy::canLaunch(bool process_running) {
    return !process_running;
}

bool LabRecorderRuntimePolicy::shouldStopOwnedProcess(
    bool process_owned,
    bool process_running) {
    return process_owned && process_running;
}
