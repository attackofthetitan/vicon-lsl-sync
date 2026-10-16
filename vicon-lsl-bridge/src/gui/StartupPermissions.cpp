#include "gui/StartupPermissions.h"

#include <lsl_cpp.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUuid>

#include <exception>
#include <utility>

namespace vicon_lsl::gui {

StartupPermissions::StartupPermissions(QString recording_root, QString recorder_executable,
                                       QObject* parent)
    : QThread(parent), recording_root_(std::move(recording_root)),
      recorder_executable_(std::move(recorder_executable)) {}

StartupPermissions::~StartupPermissions() {
    requestInterruption();
    wait();
}

QStringList StartupPermissions::foldersToCheck(const QString& recording_root) {
    QStringList folders;
#ifdef Q_OS_MACOS
    // macOS grants these folder categories separately, even in the same app.
    for (const auto location : {QStandardPaths::DocumentsLocation,
                               QStandardPaths::DesktopLocation,
                               QStandardPaths::DownloadLocation}) {
        const QString path = QStandardPaths::writableLocation(location);
        if (!path.isEmpty() && QFileInfo(path).isDir()) folders.push_back(path);
    }
#endif
    if (!recording_root.trimmed().isEmpty()) {
        QDir directory(QFileInfo(recording_root).absoluteFilePath());
        while (!directory.exists()) {
            const QString parent = QFileInfo(directory.absolutePath()).absolutePath();
            if (parent == directory.absolutePath()) break;
            directory.setPath(parent);
        }
        if (directory.exists()) folders.push_back(directory.absolutePath());
    }
    folders.removeDuplicates();
    return folders;
}

QStringList StartupPermissions::recorderProbeArguments(const QString& unused_output_path) {
    // LabRecorderCLI resolves before opening its output. An impossible query
    // exercises its network access and exits with code 2 without making an XDF.
    return {unused_output_path, "name='vicon-permission-probe' and name!='vicon-permission-probe'"};
}

void StartupPermissions::run() {
    for (const QString& folder : foldersToCheck(recording_root_)) {
        if (isInterruptionRequested()) return;
        emit progress("Requesting recording and preview folder access: " + folder);
        QTemporaryFile probe(QDir(folder).filePath(".vicon-lsl-access-XXXXXX"));
        if (!probe.open()) {
            emit warning("Folder access was not granted for " + folder + ": " + probe.errorString());
        }
    }
    if (isInterruptionRequested()) return;
    emit progress("Requesting local network access for the bridge and preview");
    try {
        // The outlet exercises incoming connections (including Windows Firewall),
        // while discovery exercises the multicast access used by the preview.
        const std::string id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        lsl::stream_info info("Vicon access check", "PermissionCheck", 1, 0.0,
                              lsl::cf_float32, id);
        lsl::stream_outlet outlet(info);
        lsl::resolve_streams(1.0);
    } catch (const std::exception& error) {
        emit warning("Local network access check failed: " + QString::fromUtf8(error.what()));
    }
    if (isInterruptionRequested()) return;
    if (recorder_executable_.isEmpty()) {
        emit warning("The recorder was not found, so its network access could not be requested at startup.");
        return;
    }
    emit progress("Requesting local network access for the recorder");
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        emit warning("Could not create a temporary folder for the recorder access check");
        return;
    }
    QProcess recorder;
    recorder.setWorkingDirectory(QFileInfo(recorder_executable_).absolutePath());
    recorder.start(recorder_executable_, recorderProbeArguments(temporary.filePath("unused.xdf")));
    // A custom recorder must never wait for input or leave a recording running.
    recorder.closeWriteChannel();
    QElapsedTimer elapsed;
    elapsed.start();
    while (recorder.state() != QProcess::NotRunning && !isInterruptionRequested() && elapsed.elapsed() < 30000) {
        recorder.waitForFinished(100);
        recorder.readAllStandardOutput();
        recorder.readAllStandardError();
    }
    if (recorder.state() != QProcess::NotRunning) {
        recorder.kill();
        recorder.waitForFinished(1000);
        emit warning("Recorder access check did not finish; check any pending system permission prompt");
    } else if (recorder.exitStatus() != QProcess::NormalExit || recorder.exitCode() != 2) {
        emit warning("Recorder access check could not be confirmed: " + recorder.errorString());
    }
}

} // namespace vicon_lsl::gui
