#include "gui/BridgeWindow.h"
#include "gui/GuiServices.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QPixmap>
#include <QTimer>

#include <memory>

namespace {

class TestSettings final : public vicon_lsl::gui::SessionSettingsService {
public:
    TestSettings() {
        configuration_.recording_root = QDir::tempPath();
        configuration_.recorder_host = "127.0.0.1";
        configuration_.recorder_port = 1;
        configuration_.recorder_automatic_launch = false;
    }

    vicon_lsl::gui::SessionConfiguration loadConfiguration() override {
        return configuration_;
    }

    void saveConfiguration(
        const vicon_lsl::gui::SessionConfiguration& configuration) override {
        configuration_ = configuration;
    }

    vicon_lsl::gui::SessionUiState loadUiState() override { return {}; }
    void saveUiState(const vicon_lsl::gui::SessionUiState&) override {}
    QStringList presetNames() override { return {}; }

    bool savePreset(const QString&,
                    const vicon_lsl::gui::SessionConfiguration&,
                    QString* error) override {
        if (error) *error = "Not available in this check";
        return false;
    }

    bool loadPreset(const QString&,
                    vicon_lsl::gui::SessionConfiguration&,
                    QString* error) override {
        if (error) *error = "Not available in this check";
        return false;
    }

    QVector<vicon_lsl::gui::ManagedCalibrationProfile>
    loadCalibrationProfiles() override {
        return {};
    }

    bool saveCalibrationProfiles(
        const QVector<vicon_lsl::gui::ManagedCalibrationProfile>&,
        QString* error) override {
        if (error) error->clear();
        return true;
    }

private:
    vicon_lsl::gui::SessionConfiguration configuration_;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vicon LSL Bridge GUI Check");

    vicon_lsl::gui::GuiServices services =
        vicon_lsl::gui::defaultGuiServices();
    services.settings = std::make_shared<TestSettings>();
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
