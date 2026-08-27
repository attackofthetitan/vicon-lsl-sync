#pragma once

#include "Config.h"
#include "gui/PreviewFileLoader.h"
#include "gui/PreviewStreamWorker.h"
#include "gui/CalibrationProfileStore.h"
#include "gui/RecordingVerifier.h"
#include "gui/SessionConfiguration.h"

#include <QDateTime>
#include <QString>

#include <functional>
#include <memory>

class QObject;
class QWidget;
class BridgeWorker;
class LabRecorderClient;

namespace vicon_lsl {
class StreamDiscoveryWorker;
}

namespace vicon_lsl::gui {

class RecorderProcessController;

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

class FileDialogService {
public:
    virtual ~FileDialogService() = default;
    virtual QString chooseDirectory(QWidget* parent,
                                    const QString& title,
                                    const QString& initial) = 0;
    virtual QString openFile(QWidget* parent,
                             const QString& title,
                             const QString& initial,
                             const QString& filter) = 0;
    virtual QString saveFile(QWidget* parent,
                             const QString& title,
                             const QString& initial,
                             const QString& filter) = 0;
};

class GuiClock {
public:
    virtual ~GuiClock() = default;
    virtual QDateTime nowUtc() const = 0;
    virtual qint64 monotonicMilliseconds() const = 0;
};

struct GuiServices {
    std::shared_ptr<SessionSettingsService> settings;
    std::shared_ptr<FileDialogService> file_dialogs;
    std::shared_ptr<GuiClock> clock;
    std::function<BridgeWorker*(const Config&, QObject*)> create_bridge_worker;
    std::function<LabRecorderClient*(QObject*)> create_recorder_client;
    std::function<RecorderProcessController*(QObject*)> create_process_controller;
    std::function<StreamDiscoveryWorker*(SessionConfiguration, QObject*)>
        create_stream_discovery;
    std::function<PreviewStreamWorker*(PreviewWorkerConfig, QObject*)>
        create_preview_worker;
    std::function<RecordingVerifier*(RecordingVerificationRequest, QObject*)>
        create_verifier;
    std::function<PreviewFileLoader*(PreviewFileType,
                                     QString,
                                     PreviewTransformProfile,
                                     PreviewTransformProfile,
                                     double,
                                     PreviewLoadOptions,
                                     QObject*)> create_file_loader;
};

GuiServices defaultGuiServices();

} // namespace vicon_lsl::gui
