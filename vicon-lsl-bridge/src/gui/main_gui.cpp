#include <QApplication>
#include <QScreen>
#include <QTimer>

#include "BridgeWindow.h"

#ifndef VICON_LSL_BRIDGE_VERSION
#define VICON_LSL_BRIDGE_VERSION "unknown"
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vicon LSL Bridge");
    app.setApplicationVersion(VICON_LSL_BRIDGE_VERSION);
    const bool verification_requested =
        app.arguments().contains(QStringLiteral("--test"));

    BridgeWindow window;
    if (const QScreen* screen = window.screen()) {
        const QSize available = screen->availableGeometry().size();
        const QSize usable(qMax(1, available.width() - 80),
                           qMax(1, available.height() - 80));
        window.resize(QSize(1440, 900).boundedTo(usable));
    }
    window.show();
    if (verification_requested) {
        QTimer::singleShot(0, &app, [&app, &window]() {
            window.ensurePolished();
            app.exit(window.isVisible() && window.size().isValid() ? 0 : 1);
        });
    }
    return app.exec();
}
