#include <QApplication>
#include "BridgeWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vicon LSL Bridge");
    app.setApplicationVersion("1.0.0");

    BridgeWindow window;
    window.show();

    return app.exec();
}
