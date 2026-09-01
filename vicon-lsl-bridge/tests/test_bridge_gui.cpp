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
#include <QWidget>

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

    QWidget render_host;
    render_host.setAttribute(Qt::WA_DontShowOnScreen);
    BridgeWindow window(&render_host, true, std::move(settings));

    QTimer::singleShot(0, [&app, &window]() {
        const QSize target_size(800, 600);
        const QSize minimum = window.minimumSizeHint();
        const bool fits = minimum.width() <= 1280 && minimum.height() <= 720;
        const bool controls_are_described = controlsAreDescribed(window);

        window.resize(target_size);
        window.ensurePolished();
        QApplication::processEvents(QEventLoop::AllEvents, 20);

        const qreal device_pixel_ratio = window.devicePixelRatioF();
        const QSize backing_size(
            qRound(target_size.width() * device_pixel_ratio),
            qRound(target_size.height() * device_pixel_ratio));
        QPixmap rendered(backing_size);
        rendered.setDevicePixelRatio(device_pixel_ratio);
        rendered.fill(Qt::transparent);
        window.render(&rendered);
        const bool rendered_at_requested_size =
            !rendered.isNull() &&
            window.size() == target_size &&
            rendered.deviceIndependentSize() == QSizeF(target_size);

        if (!fits || !controls_are_described || !rendered_at_requested_size) {
            std::cerr << "GUI check failed: minimum="
                      << minimum.width() << 'x' << minimum.height()
                      << ", window=" << window.width() << 'x' << window.height()
                      << ", pixmap=" << rendered.width() << 'x' << rendered.height()
                      << ", device-independent pixmap="
                      << rendered.deviceIndependentSize().width() << 'x'
                      << rendered.deviceIndependentSize().height()
                      << ", device pixel ratio=" << rendered.devicePixelRatio()
                      << std::endl;
        }

        app.exit(fits && controls_are_described && rendered_at_requested_size ? 0 : 1);
    });

    return app.exec();
}
