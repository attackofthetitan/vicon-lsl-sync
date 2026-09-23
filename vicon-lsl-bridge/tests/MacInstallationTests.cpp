#include "LabRecorderClientTestSupport.h"

#include "gui/MacInstallation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace labrecorder_client_tests {
namespace {

bool writeFile(const QString& path, const QByteArray& contents = {}) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

// The disk-image app carries both recorders; the archive app does not.
QString createBundle(const QString& folder, bool carries_recorders) {
    const QString bundle = QDir(folder).filePath("vicon-lsl-bridge-gui.app");
    writeFile(QDir(bundle).filePath("Contents/MacOS/vicon-lsl-bridge-gui"));
    if (carries_recorders) {
        const QDir helpers(QDir(bundle).filePath("Contents/Helpers/LabRecorder.app/Contents/MacOS"));
        writeFile(helpers.filePath("LabRecorder"));
        writeFile(helpers.filePath("LabRecorderCLI"));
    }
    return bundle;
}

} // namespace

void testMacInstallation() {
    using namespace vicon_lsl::gui;
    QTemporaryDir temp_dir;
    expect(temp_dir.isValid(), "temporary directory created for installation tests");
    const QString root = temp_dir.path();

    expect(applicationBundlePath("/Applications/vicon-lsl-bridge-gui.app/Contents/MacOS") ==
               "/Applications/vicon-lsl-bridge-gui.app",
           "finds the app bundle from its executable folder");
    expect(applicationBundlePath("/usr/local/bin").isEmpty(),
           "finds no bundle for an executable outside one");

    const QString image = QDir(root).filePath("Vicon LSL Bridge");
    const QString older_image = QDir(root).filePath("Vicon LSL Bridge 1");
    const QString other_volume = QDir(root).filePath("Backup");
    expect(writeFile(QDir(image).filePath(kInstallerMarkerName), "1.2.3\n") &&
               writeFile(QDir(older_image).filePath(kInstallerMarkerName), "1.2.2\n") &&
               QDir().mkpath(other_volume),
           "create installer image fixtures");
    const QStringList images = installerImages("1.2.3", {image, older_image, other_volume});
    expect(images == QStringList{image},
           "recognises only the installer image of the running version");

    const QString on_image = createBundle(image, true);
    expect(shouldOfferMove(on_image, images),
           "offers to move an app running from its installer image");
    expect(imagesToEject(on_image, images).isEmpty(),
           "does not eject the image the app is running from");

    const QString translocated = createBundle(QDir(root).filePath("AppTranslocation/0A1B/d"), true);
    expect(shouldOfferMove(translocated, images),
           "offers to move the temporary copy macOS runs a downloaded app from");
    expect(imagesToEject(translocated, images).isEmpty(),
           "does not eject an image a translocated app may be running from");

    const QString archive_app = createBundle(QDir(root).filePath("AppTranslocation/2C3D/d"), false);
    expect(!shouldOfferMove(archive_app, images),
           "does not move an app that needs its recorders beside it");

    const QString installed = createBundle(QDir(root).filePath("Applications"), true);
    expect(!shouldOfferMove(installed, images),
           "does not offer to move an installed app");
    expect(imagesToEject(installed, images) == QStringList{image},
           "ejects the installer image once the app runs from elsewhere");
}

} // namespace labrecorder_client_tests
