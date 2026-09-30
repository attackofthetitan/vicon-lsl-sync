#pragma once

#include "gui/SessionState.h"

#include <QString>
#include <QStringList>

#include <functional>

class QObject;

// Settles where the macOS app runs from, because macOS ties the permissions a
// user grants to where the app is.
namespace vicon_lsl::gui {

// The file at the top of the disk image that names the version it installs, so
// the app can recognise its own installer.
inline constexpr char kInstallerMarkerName[] = ".vicon-lsl-bridge-installer";

// The top folders of the read-only local disks under /Volumes, such as open disk images.
QStringList mountedReadOnlyVolumes();

// The .app folder around a program folder ending in Contents/MacOS, or empty.
QString applicationBundlePath(const QString& executable_directory);

// The disks among these that are this version's installer.
QStringList installerImages(const QString& version, const QStringList& volume_roots);

// True when the app can move on its own because the recorders are inside it,
// and it is running from its installer or from macOS's temporary copy.
bool shouldOfferMove(const QString& bundle_path, const QStringList& installer_images);

// The installer images the app can eject, leaving out any it may be running from.
QStringList imagesToEject(const QString& bundle_path, const QStringList& installer_images);

// Asks to copy the app to Applications and reopens it there once this process
// exits, returning true when the caller should quit.
bool moveToApplications(const QString& bundle_path);

// Ejects each image in the background, and reports what happened.
void ejectImages(const QStringList& mount_points, QObject* context,
                 std::function<void(EventSeverity, const QString&)> report);

} // namespace vicon_lsl::gui
