#include <QApplication>
#include "BridgeWindow.h"

#ifndef VICON_LSL_BRIDGE_VERSION
#define VICON_LSL_BRIDGE_VERSION "unknown"
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vicon LSL Bridge");
    app.setApplicationVersion(VICON_LSL_BRIDGE_VERSION);

    BridgeWindow window;
    window.show();

    return app.exec();
}
