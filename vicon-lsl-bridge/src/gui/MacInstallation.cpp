#include "gui/MacInstallation.h"

#include "gui/RecorderProcessController.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStorageInfo>
#include <QTimer>

#include <algorithm>

#include <sys/xattr.h>

namespace vicon_lsl::gui {
namespace {

constexpr int kEjectAttempts = 3;
constexpr int kEjectRetryMs = 3000;

bool isInside(const QString& path, const QString& folder) {
    return QDir::cleanPath(path).startsWith(QDir::cleanPath(folder) + '/');
}

// macOS runs a downloaded app from a randomized read-only copy until the user
// moves it out of the disk image or folder it arrived in.
bool isTranslocated(const QString& bundle_path) {
    return bundle_path.contains("/AppTranslocation/");
}

// Only the app from the disk image carries the recorders. The one in the
// archive needs the recorders beside it, so it cannot move on its own.
bool carriesRecorders(const QString& bundle_path) {
    const QDir helpers(RecorderProcessController::embeddedRecorderDirectory(
        QDir(bundle_path).filePath("Contents/MacOS")));
    return QFileInfo(helpers.filePath("LabRecorder")).isFile() &&
           QFileInfo(helpers.filePath("LabRecorderCLI")).isFile();
}

// Runs a program to completion while a busy dialog keeps the app responsive.
bool runWithProgress(const QString& label, const QString& program,
                     const QStringList& arguments, QString* error) {
    QProgressDialog progress(label, QString(), 0, 0);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.show();
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted()) {
        *error = process.errorString();
        return false;
    }
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(50);
        QCoreApplication::processEvents();
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) return true;
    *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (error->isEmpty()) *error = program + " stopped with code " + QString::number(process.exitCode());
    return false;
}

// The user has already opened this app. Its copy would keep the mark of a
// downloaded file, and macOS would block the copy's first launch all over again.
void clearQuarantine(const QString& bundle) {
    QStringList paths{bundle};
    QDirIterator entries(bundle, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) paths.push_back(entries.next());
    for (const QString& path : paths) {
        ::removexattr(QFile::encodeName(path).constData(), "com.apple.quarantine", XATTR_NOFOLLOW);
    }
}

// Copies beside the destination first, so a failed copy never replaces a
// working app.
bool copyBundle(const QString& source, const QString& destination, QString* error) {
    const QFileInfo target(destination);
    const QString partial = target.dir().filePath("." + target.fileName() + ".partial");
    QDir(partial).removeRecursively();
    if (runWithProgress("Moving Vicon LSL Bridge to Applications...", "/usr/bin/ditto",
                        {source, partial}, error)) {
        clearQuarantine(partial);
        if (target.exists() && !QFile::moveToTrash(destination)) {
            *error = "The copy already in " + target.absolutePath() +
                     " could not be moved to the Trash.";
        } else if (!QDir().rename(partial, destination)) {
            *error = "The copy could not be named " + destination + ".";
        } else {
            return true;
        }
    }
    QDir(partial).removeRecursively();
    return false;
}

void ejectImage(const QString& mount_point, int attempt, QObject* context,
                const std::function<void(EventSeverity, const QString&)>& report) {
    auto* process = new QProcess(context);
    QObject::connect(process, &QProcess::finished, context,
                     [=](int exit_code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (status == QProcess::NormalExit && exit_code == 0) {
            report(EventSeverity::Information, "Ejected the installer disk image " + mount_point);
        } else if (attempt + 1 < kEjectAttempts) {
            // Finder, Spotlight, or the copy the app was moved from can hold the
            // image for a moment.
            QTimer::singleShot(kEjectRetryMs, context, [=]() {
                ejectImage(mount_point, attempt + 1, context, report);
            });
        } else {
            report(EventSeverity::Warning, "The installer disk image " + mount_point +
                   " is still in use. Eject it in Finder when you no longer need it.");
        }
    });
    process->start("/usr/sbin/diskutil", {"eject", mount_point});
}

} // namespace

QStringList mountedReadOnlyVolumes() {
    QStringList roots;
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        // A disk image attaches as a local disk. Leaving out network shares
        // avoids waiting on a server that has gone away.
        if (volume.isValid() && volume.isReady() && volume.isReadOnly() &&
            volume.rootPath().startsWith("/Volumes/") &&
            volume.device().startsWith("/dev/disk")) {
            roots.push_back(volume.rootPath());
        }
    }
    return roots;
}

QString applicationBundlePath(const QString& executable_directory) {
    const QString directory = QDir::cleanPath(executable_directory);
    if (!directory.endsWith(".app/Contents/MacOS")) return {};
    return QDir::cleanPath(directory + "/../..");
}

QStringList installerImages(const QString& version, const QStringList& volume_roots) {
    QStringList images;
    for (const QString& root : volume_roots) {
        QFile marker(QDir(root).filePath(kInstallerMarkerName));
        if (marker.open(QIODevice::ReadOnly) &&
            QString::fromUtf8(marker.readAll()).trimmed() == version) {
            images.push_back(root);
        }
    }
    return images;
}

bool shouldOfferMove(const QString& bundle_path, const QStringList& installer_images) {
    if (bundle_path.isEmpty() || !carriesRecorders(bundle_path)) return false;
    if (isTranslocated(bundle_path)) return true;
    return std::any_of(installer_images.begin(), installer_images.end(),
                       [&](const QString& image) { return isInside(bundle_path, image); });
}

QStringList imagesToEject(const QString& bundle_path, const QStringList& installer_images) {
    // A translocated app does not show which image it came from.
    if (isTranslocated(bundle_path)) return {};
    QStringList images;
    for (const QString& image : installer_images) {
        if (!isInside(bundle_path, image)) images.push_back(image);
    }
    return images;
}

bool moveToApplications(const QString& bundle_path) {
    // A standard account cannot write to the shared Applications folder.
    const QString folder = QFileInfo(QStringLiteral("/Applications")).isWritable()
        ? QStringLiteral("/Applications") : QDir::home().filePath("Applications");
    const QString destination = QDir(folder).filePath(QFileInfo(bundle_path).fileName());

    QMessageBox question(QMessageBox::Question, "Move to Applications",
                         "Move Vicon LSL Bridge to the Applications folder?");
    QString detail = "It is running from its disk image, where macOS asks for its permissions "
                     "again each time it opens. The disk image is ejected after the move.";
    if (QFileInfo::exists(destination)) {
        detail += "\n\nThe copy already in " + folder + " goes to the Trash.";
    }
    question.setInformativeText(detail);
    QPushButton* move = question.addButton("Move to Applications", QMessageBox::AcceptRole);
    question.addButton("Not Now", QMessageBox::RejectRole);
    question.setDefaultButton(move);
    question.exec();
    if (question.clickedButton() != move) return false;

    QString error;
    if (!QDir().mkpath(folder)) {
        error = "The folder " + folder + " could not be created.";
    } else if (copyBundle(bundle_path, destination, &error)) {
        // Wait for this process to exit, so macOS opens the new copy instead of
        // returning to this one.
        const QString reopen = "while /bin/kill -0 \"$1\" 2>/dev/null; do /bin/sleep 0.2; done; "
                               "exec /usr/bin/open \"$2\"";
        if (QProcess::startDetached("/bin/sh", {"-c", reopen, "sh",
                QString::number(QCoreApplication::applicationPid()), destination})) {
            return true;
        }
        QMessageBox::information(nullptr, "Move to Applications",
                                 "Vicon LSL Bridge was copied to " + folder +
                                 " but could not reopen from there. Quit it, then open it from " +
                                 folder + ".");
        return false;
    }
    QMessageBox::warning(nullptr, "Move to Applications",
                         "Vicon LSL Bridge could not be moved. " + error +
                         "\n\nDrag it from the disk image to the Applications folder instead.");
    return false;
}

void ejectImages(const QStringList& mount_points, QObject* context,
                 std::function<void(EventSeverity, const QString&)> report) {
    for (const QString& mount_point : mount_points) ejectImage(mount_point, 0, context, report);
}

} // namespace vicon_lsl::gui
