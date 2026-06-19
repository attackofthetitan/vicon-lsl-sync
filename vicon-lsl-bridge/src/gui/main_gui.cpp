#include <QApplication>
#include <QCoreApplication>
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

    if (QCoreApplication::arguments().contains("--smoke-test")) {
        QTimer::singleShot(0, [&app]() {
            try {
                ViconClient unavailable_vicon("127.0.0.1:1");
                if (unavailable_vicon.connect()) {
                    unavailable_vicon.disconnect();
                    app.exit(4);
                    return;
                }

                const std::string source_id = "vicon-lsl-bridge-gui-smoke";
                const lsl::stream_info info(
                    "ViconLSLBridgeSmoke",
                    "Test",
                    1,
                    lsl::IRREGULAR_RATE,
                    lsl::cf_double64,
                    source_id);
                const lsl::stream_outlet outlet(info);
                const auto streams = lsl::resolve_stream("source_id", source_id, 1, 2.0);
                app.exit(streams.empty() ? 2 : 0);
            } catch (...) {
                app.exit(3);
            }
        });
    }

    return app.exec();
}
