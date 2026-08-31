#include "gui/BridgeWindow.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QLineEdit>
#include <QPixmap>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <memory>

namespace {

bool hasTooltip(const QWidget* widget) {
    for (; widget; widget = qobject_cast<const QWidget*>(widget->parent())) {
        if (!widget->toolTip().trimmed().isEmpty()) return true;
    }
    return false;
}

bool controlsAreDescribed(const QWidget& window) {
    for (const QWidget* control : window.findChildren<QWidget*>()) {
        const bool needs_tooltip = qobject_cast<const QLineEdit*>(control) ||
            qobject_cast<const QComboBox*>(control) ||
            qobject_cast<const QAbstractSpinBox*>(control);
        if (needs_tooltip && control->focusPolicy() != Qt::NoFocus &&
            !hasTooltip(control)) {
            std::cerr << "Missing control description: "
                      << control->metaObject()->className() << std::endl;
            return false;
        }
    }
    for (const QSlider* slider : window.findChildren<QSlider*>()) {
        if (slider->accessibleName().trimmed().isEmpty()) return false;
    }
    for (const QAbstractButton* button : window.findChildren<QAbstractButton*>()) {
        if (!button->shortcut().isEmpty() &&
            (button->focusPolicy() == Qt::NoFocus ||
             button->accessibleName().trimmed().isEmpty())) {
            std::cerr << "Missing shortcut description: "
                      << button->text().toStdString() << std::endl;
            return false;
        }
    }
    return true;
}

} // namespace

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

    BridgeWindow window(nullptr, true, std::move(settings));

    QTimer::singleShot(0, [&app, &window]() {
        const QSize target_size(800, 600);
        const QSize minimum = window.minimumSizeHint();
        const bool fits = minimum.width() <= 1280 && minimum.height() <= 720;
        const bool controls_are_described = controlsAreDescribed(window);

        window.resize(target_size);
        window.ensurePolished();
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        const QPixmap rendered = window.grab();
        const bool rendered_at_requested_size =
            !rendered.isNull() &&
            rendered.deviceIndependentSize().toSize() == target_size;

        app.exit(fits && controls_are_described && rendered_at_requested_size ? 0 : 1);
    });

    return app.exec();
}
