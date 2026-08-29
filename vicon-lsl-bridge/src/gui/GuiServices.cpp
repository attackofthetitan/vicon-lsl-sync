#include "gui/GuiServices.h"

#include <QSettings>

namespace vicon_lsl::gui {
namespace {

class NativeSessionSettingsService final : public SessionSettingsService {
public:
    SessionConfiguration loadConfiguration() override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        return SessionConfigurationStore::load(settings);
    }

    void saveConfiguration(const SessionConfiguration& configuration) override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        SessionConfigurationStore::save(settings, configuration);
    }

    SessionUiState loadUiState() override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        return SessionConfigurationStore::loadUiState(settings);
    }

    void saveUiState(const SessionUiState& state) override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        SessionConfigurationStore::saveUiState(settings, state);
    }

    QStringList presetNames() override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        return SessionConfigurationStore::presetNames(settings);
    }

    bool savePreset(const QString& name,
                    const SessionConfiguration& configuration,
                    QString* error) override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        return SessionConfigurationStore::savePreset(settings, name, configuration, error);
    }

    bool loadPreset(const QString& name,
                    SessionConfiguration& configuration,
                    QString* error) override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        return SessionConfigurationStore::loadPreset(settings, name, configuration, error);
    }

    QVector<ManagedCalibrationProfile> loadCalibrationProfiles() override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        return CalibrationProfileStore::load(settings);
    }

    bool saveCalibrationProfiles(
        const QVector<ManagedCalibrationProfile>& profiles,
        QString* error) override {
        QSettings settings("ViconLSL", "ViconLSLBridge");
        return CalibrationProfileStore::save(settings, profiles, error);
    }
};

} // namespace

GuiServices defaultGuiServices() {
    GuiServices result;
    result.settings = std::make_shared<NativeSessionSettingsService>();
    result.create_preview_worker = [](PreviewWorkerConfig configuration,
                                      QObject* parent) {
        return new PreviewStreamWorker(std::move(configuration), parent);
    };
    return result;
}

} // namespace vicon_lsl::gui
