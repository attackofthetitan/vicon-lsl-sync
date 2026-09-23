#include <QApplication>
#include <QScreen>
#include <QTimer>

#include "BridgeWindow.h"
#ifdef Q_OS_MACOS
#include "gui/MacInstallation.h"
#endif

#ifndef VICON_LSL_BRIDGE_VERSION
#define VICON_LSL_BRIDGE_VERSION "unknown"
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vicon LSL Bridge");
    app.setApplicationVersion(VICON_LSL_BRIDGE_VERSION);
    const bool verification_requested =
        app.arguments().contains(QStringLiteral("--test"));

#ifdef Q_OS_MACOS
    // Settle where the app runs from before the window starts a recorder.
    const QString bundle = vicon_lsl::gui::applicationBundlePath(
        QCoreApplication::applicationDirPath());
    const QStringList installer_images = vicon_lsl::gui::installerImages(
        app.applicationVersion(), vicon_lsl::gui::mountedReadOnlyVolumes());
    if (!verification_requested &&
        vicon_lsl::gui::shouldOfferMove(bundle, installer_images) &&
        vicon_lsl::gui::moveToApplications(bundle)) {
        return 0;
    }
#endif

    BridgeWindow window;
    if (const QScreen* screen = window.screen()) {
        const QSize available = screen->availableGeometry().size();
        const QSize usable(qMax(1, available.width() - 80),
                           qMax(1, available.height() - 80));
        window.resize(QSize(1440, 900).boundedTo(usable));
    }
    window.show();
#ifdef Q_OS_MACOS
    if (!verification_requested) {
        vicon_lsl::gui::ejectImages(
            vicon_lsl::gui::imagesToEject(bundle, installer_images), &window,
            [&window](EventSeverity severity, const QString& message) {
                window.reportApplicationEvent(severity, message);
            });
    }
#endif
    if (verification_requested) {
        QTimer::singleShot(0, &app, [&app, &window]() {
            window.ensurePolished();
            app.exit(window.isVisible() && window.size().isValid() ? 0 : 1);
        });
    }
    return app.exec();
}
