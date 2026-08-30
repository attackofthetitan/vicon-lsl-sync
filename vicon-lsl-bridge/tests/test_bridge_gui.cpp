#include "gui/BridgeWindow.h"
#include "gui/GuiServices.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QPixmap>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vicon LSL Bridge GUI Check");

    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) return 1;
    auto settings = std::make_shared<QSettings>(
        settings_directory.filePath("settings.ini"), QSettings::IniFormat);
    vicon_lsl::gui::SessionConfiguration configuration;
    configuration.recording_root = QDir::tempPath();
    configuration.recorder_host = "127.0.0.1";
    configuration.recorder_port = 1;
    configuration.recorder_automatic_launch = false;
    vicon_lsl::gui::SessionConfigurationStore::save(*settings, configuration);

    vicon_lsl::gui::GuiServices services =
        vicon_lsl::gui::defaultGuiServices();
    services.settings = std::move(settings);
    BridgeWindow window(nullptr, true, std::move(services));

    QTimer::singleShot(0, [&app, &window]() {
        const QSize target_size(800, 600);
        const QSize minimum = window.minimumSizeHint();
        const bool fits = minimum.width() <= 1280 && minimum.height() <= 720;
        const bool controls_are_described = window.passesInterfaceChecks();

        window.resize(target_size);
        window.ensurePolished();
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        const QPixmap rendered = window.grab();
        const bool rendered_at_requested_size =
            !rendered.isNull() &&
            rendered.deviceIndependentSize().toSize() == target_size;

        app.exit(fits && controls_are_described && rendered_at_requested_size
                     ? 0
                     : 1);
    });

    return app.exec();
}
