#include "gui/BridgeWindow.h"
#include "gui/ElidingLabel.h"
#include "TestSupport.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
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

// Controls packed into a row that cannot shrink push their last members off the
// edge and force sideways scrolling. Nothing in this window should need it.
bool noHorizontalScrolling(const QWidget& window) {
    bool ok = true;
    for (const QScrollArea* area : window.findChildren<QScrollArea*>()) {
        const QScrollBar* bar = area->horizontalScrollBar();
        if (bar && bar->maximum() > 0) {
            std::cerr << "Horizontal scrolling required in \""
                      << area->accessibleName().toStdString() << "\": content is "
                      << bar->maximum() << " px wider than the viewport" << std::endl;
            ok = false;
        }
    }
    return ok;
}

// Two visible siblings whose rectangles intersect are painted over each other.
// The preview controls used to be laid across the drawing area this way when the
// window was short, which is what "overlapping buttons" looks like on screen.
bool noOverlappingSiblings(const QWidget& window) {
    bool ok = true;
    QList<const QWidget*> parents{&window};
    for (const QWidget* w : window.findChildren<QWidget*>()) parents << w;
    for (const QWidget* parent : parents) {
        // A crowded QTabBar overlaps its own scroll arrows. That is the style's
        // business, not this layout's, and it is not something the app can fix.
        if (qobject_cast<const QTabBar*>(parent)) continue;
        QList<QWidget*> siblings;
        for (QObject* child : parent->children()) {
            auto* w = qobject_cast<QWidget*>(child);
            if (w && w->isVisible() && !w->isWindow() && !w->geometry().isEmpty())
                siblings << w;
        }
        for (int i = 0; i < siblings.size(); ++i) {
            for (int j = i + 1; j < siblings.size(); ++j) {
                const QRect shared = siblings[i]->geometry() & siblings[j]->geometry();
                // One pixel of contact is a shared border, not an overlap.
                if (shared.width() <= 1 || shared.height() <= 1) continue;
                std::cerr << "Overlapping widgets inside "
                          << parent->metaObject()->className() << ": "
                          << siblings[i]->metaObject()->className() << " and "
                          << siblings[j]->metaObject()->className()
                          << " share " << shared.width() << "x" << shared.height()
                          << " px" << std::endl;
                ok = false;
            }
        }
    }
    return ok;
}

// A caption cut off mid-word reads as text running into the control beside it.
// Labels that shorten themselves are excluded: eliding is their whole job.
bool labelsAreNotCutOff(const QWidget& window) {
    bool ok = true;
    for (const QLabel* label : window.findChildren<QLabel*>()) {
        if (!label->isVisible() || label->wordWrap()) continue;
        if (dynamic_cast<const vicon_lsl::gui_detail::ElidingLabel*>(label)) continue;
        if (label->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored) continue;
        // "&" marks the keyboard accelerator and is never drawn.
        QString drawn = label->text();
        drawn.remove(QLatin1Char('&'));
        if (drawn.trimmed().isEmpty()) continue;
        const int needed = label->fontMetrics().horizontalAdvance(drawn);
        if (needed > label->width() + 1) {
            std::cerr << "Cut-off label \"" << drawn.toStdString() << "\": needs "
                      << needed << " px, has " << label->width() << " px" << std::endl;
            ok = false;
        }
    }
    return ok;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vicon LSL Bridge GUI Check");
    app.setQuitOnLastWindowClosed(false);

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
    // Shown, but never on a screen: the layout checks below ask each widget
    // whether it is visible, which stays false until the window is shown.
    render_host.show();
    window.show();

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
        // Every tab, at the smallest window the user can make and at the
        // reference size, because a layout only breaks once it runs out of room.
        bool laid_out_cleanly = true;
        auto* tabs = window.findChild<QTabWidget*>("sessionControlsTabs");
        for (const QSize& size : {window.minimumSize(), target_size}) {
            window.resize(size);
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            for (int tab = 0; tab < (tabs ? tabs->count() : 1); ++tab) {
                if (tabs) tabs->setCurrentIndex(tab);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                if (noOverlappingSiblings(window) && labelsAreNotCutOff(window) &&
                    noHorizontalScrolling(window)) {
                    continue;
                }
                std::cerr << "  ...at " << size.width() << 'x' << size.height()
                          << " on tab " << (tabs ? tabs->tabText(tab).toStdString()
                                                 : std::string("only"))
                          << std::endl;
                laid_out_cleanly = false;
            }
        }
        if (tabs) tabs->setCurrentIndex(0);
        window.resize(target_size);
        QApplication::processEvents(QEventLoop::AllEvents, 20);

        const bool no_sideways_scrolling = noHorizontalScrolling(window);

        if (!fits || !controls_are_described || !rendered_at_requested_size ||
            !no_sideways_scrolling || !laid_out_cleanly) {
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

        const bool session_flows_passed = test_support::runAllTests() == 0;
        app.exit(fits && controls_are_described && rendered_at_requested_size &&
                 no_sideways_scrolling && laid_out_cleanly && session_flows_passed ? 0 : 1);
    });

    return app.exec();
}
