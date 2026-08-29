#pragma once

#include "gui/PreviewStreamWorker.h"
#include "gui/CalibrationProfileStore.h"
#include "gui/SessionConfiguration.h"

#include <QString>

#include <functional>
#include <memory>

class QObject;
class QWidget;

namespace vicon_lsl::gui {

class SessionSettingsService {
public:
    virtual ~SessionSettingsService() = default;
    virtual SessionConfiguration loadConfiguration() = 0;
    virtual void saveConfiguration(const SessionConfiguration& configuration) = 0;
    virtual SessionUiState loadUiState() = 0;
    virtual void saveUiState(const SessionUiState& state) = 0;
    virtual QStringList presetNames() = 0;
    virtual bool savePreset(const QString& name,
                            const SessionConfiguration& configuration,
                            QString* error) = 0;
    virtual bool loadPreset(const QString& name,
                            SessionConfiguration& configuration,
                            QString* error) = 0;
    virtual QVector<ManagedCalibrationProfile> loadCalibrationProfiles() = 0;
    virtual bool saveCalibrationProfiles(
        const QVector<ManagedCalibrationProfile>& profiles,
        QString* error) = 0;
};

struct GuiServices {
    std::shared_ptr<SessionSettingsService> settings;
    std::function<PreviewStreamWorker*(PreviewWorkerConfig, QObject*)>
        create_preview_worker;
};

GuiServices defaultGuiServices();

} // namespace vicon_lsl::gui
