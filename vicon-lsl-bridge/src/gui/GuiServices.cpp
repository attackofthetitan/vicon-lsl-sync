#include "gui/GuiServices.h"

#include "BridgeWindow.h"
#include "gui/LabRecorderClient.h"
#include "gui/RecorderProcessController.h"
#include "gui/StreamDiscoveryWorker.h"

#include <QElapsedTimer>
#include <QFileDialog>
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

class NativeFileDialogService final : public FileDialogService {
public:
    QString chooseDirectory(QWidget* parent,
                            const QString& title,
                            const QString& initial) override {
        return QFileDialog::getExistingDirectory(parent, title, initial);
    }

    QString openFile(QWidget* parent,
                     const QString& title,
                     const QString& initial,
                     const QString& filter) override {
        return QFileDialog::getOpenFileName(parent, title, initial, filter);
    }

    QString saveFile(QWidget* parent,
                     const QString& title,
                     const QString& initial,
                     const QString& filter) override {
        return QFileDialog::getSaveFileName(parent, title, initial, filter);
    }
};

class SystemGuiClock final : public GuiClock {
public:
    SystemGuiClock() { timer_.start(); }
    QDateTime nowUtc() const override { return QDateTime::currentDateTimeUtc(); }
    qint64 monotonicMilliseconds() const override {
        return timer_.msecsSinceReference();
    }

private:
    QElapsedTimer timer_;
};

} // namespace

GuiServices defaultGuiServices() {
    GuiServices result;
    result.settings = std::make_shared<NativeSessionSettingsService>();
    result.file_dialogs = std::make_shared<NativeFileDialogService>();
    result.clock = std::make_shared<SystemGuiClock>();
    result.create_bridge_worker = [](const Config& config, QObject* parent) {
        return new BridgeWorker(config, parent);
    };
    result.create_recorder_client = [](QObject* parent) {
        return new LabRecorderClient(parent);
    };
    result.create_process_controller = [](QObject* parent) {
        return new RecorderProcessController(parent);
    };
    result.create_stream_discovery = [](SessionConfiguration configuration,
                                        QObject* parent) {
        return new StreamDiscoveryWorker(std::move(configuration), parent);
    };
    result.create_preview_worker = [](PreviewWorkerConfig configuration,
                                      QObject* parent) {
        return new PreviewStreamWorker(std::move(configuration), parent);
    };
    result.create_verifier = [](RecordingVerificationRequest request,
                                QObject* parent) {
        return new RecordingVerifier(std::move(request), parent);
    };
    result.create_file_loader = [](PreviewFileType type,
                                   QString path,
                                   PreviewTransformProfile vicon_transform,
                                   PreviewTransformProfile gaze_transform,
                                   double tolerance,
                                   PreviewLoadOptions options,
                                   QObject* parent) {
        return new PreviewFileLoader(type, std::move(path),
                                     std::move(vicon_transform),
                                     std::move(gaze_transform), tolerance,
                                     std::move(options), parent);
    };
    return result;
}

} // namespace vicon_lsl::gui
