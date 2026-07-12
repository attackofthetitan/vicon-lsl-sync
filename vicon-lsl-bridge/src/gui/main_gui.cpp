#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <lsl_cpp.h>

#include "BridgeWindow.h"
#include "ViconClient.h"

#ifndef VICON_LSL_BRIDGE_VERSION
#define VICON_LSL_BRIDGE_VERSION "unknown"
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vicon LSL Bridge");
    app.setApplicationVersion(VICON_LSL_BRIDGE_VERSION);

    BridgeWindow window;
    window.show();

    if (QCoreApplication::arguments().contains("--test")) {
        QTimer::singleShot(0, [&app, &window]() {
            try {
                if (!window.configurableTooltipsPresent()) {
                    app.exit(10);
                    return;
                }

                ViconClient unavailable_vicon("127.0.0.1:1");
                if (unavailable_vicon.connect()) {
                    unavailable_vicon.disconnect();
                    app.exit(4);
                    return;
                }

                const std::string source_id = "vicon-lsl-bridge-gui-test";
                const lsl::stream_info info(
                    "ViconLSLBridgeTest",
                    "Test",
                    1,
                    lsl::IRREGULAR_RATE,
                    lsl::cf_double64,
                    source_id);
                const lsl::stream_outlet outlet(info);
                const auto streams = lsl::resolve_stream("source_id", source_id, 1, 2.0);
                if (streams.empty()) {
                    app.exit(2);
                    return;
                }

                const QString app_dir = QCoreApplication::applicationDirPath();
                const bool packaged_payload = QFileInfo::exists(
                    QDir(app_dir).filePath("labrecorder/LabRecorder.exe"));
                if (!packaged_payload) {
                    app.exit(0);
                    return;
                }
                if (!QFileInfo::exists(QDir(app_dir).filePath("stair_model/stair_model1.obj")) ||
                    !QFileInfo::exists(QDir(app_dir).filePath("stair_model/stair_model1.mtl"))) {
                    app.exit(5);
                    return;
                }

                auto* poll = new QTimer(&app);
                poll->setInterval(100);
                QObject::connect(poll, &QTimer::timeout, &app, [&app, &window, poll]() {
                    if (window.labRecorderConnected() &&
                        window.labRecorderOwnedProcessRunning() &&
                        window.stairModelLoaded()) {
                        poll->stop();
                        app.exit(0);
                    }
                });
                poll->start();
                QTimer::singleShot(20000, &app, [&app, &window]() {
                    if (!window.labRecorderOwnedProcessRunning()) {
                        app.exit(7);
                    } else if (!window.labRecorderConnected()) {
                        app.exit(8);
                    } else if (!window.stairModelLoaded()) {
                        app.exit(9);
                    } else {
                        app.exit(6);
                    }
                });
            } catch (...) {
                app.exit(3);
            }
        });
    }

    return app.exec();
}
