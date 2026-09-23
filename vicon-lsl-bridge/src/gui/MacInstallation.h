#pragma once

#include "gui/SessionState.h"

#include <QString>
#include <QStringList>

#include <functional>

class QObject;

// Settles where the macOS app runs from. macOS ties the permissions a user
// grants to where an app is, and an app opened from its disk image moves on
// every mount, so the app offers to move to Applications and then ejects the
// image it came on.
namespace vicon_lsl::gui {

// The file at the root of the disk image. It holds the version the image
// installs, so the app can recognise its own installer.
inline constexpr char kInstallerMarkerName[] = ".vicon-lsl-bridge-installer";

// Root folders of the read-only disk volumes mounted where Finder shows them.
QStringList mountedReadOnlyVolumes();

// The app bundle for an executable folder ending in Contents/MacOS, or empty.
QString applicationBundlePath(const QString& executable_directory);

// The volumes among these that are the installer for this version.
QStringList installerImages(const QString& version, const QStringList& volume_roots);

// True when the app carries its recorders, so it can move on its own, and runs
// from its installer or from the temporary copy macOS makes of a downloaded app
// that has not been moved yet.
bool shouldOfferMove(const QString& bundle_path, const QStringList& installer_images);

// The installer images the app can eject, leaving any it may be running from.
QStringList imagesToEject(const QString& bundle_path, const QStringList& installer_images);

// Asks to copy the app to Applications, and reopens it there once this process
// exits. Returns true when the caller should quit.
bool moveToApplications(const QString& bundle_path);

// Ejects each image in the background, and reports what happened.
void ejectImages(const QStringList& mount_points, QObject* context,
                 std::function<void(EventSeverity, const QString&)> report);

} // namespace vicon_lsl::gui
