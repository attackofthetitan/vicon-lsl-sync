#pragma once

#include <QWidget>
#include <QMetaType>
#include <QThread>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <memory>
#include "gui/LabRecorderClient.h"
#include "ViconLSLBridge.h"

class QCloseEvent;

namespace vicon_lsl::gui_detail {
struct BridgeWindowUi;
}

enum class BridgeExitResult {
    Stopped,
    Failed,
};

class BridgeWorker : public QThread {
    Q_OBJECT
public:
    explicit BridgeWorker(const Config& config, QObject* parent = nullptr);
    void stopBridge();

signals:
    void statusUpdate(int state, unsigned long long markers, unsigned long long segments,
                      unsigned int frames, const QString& message);
    void terminal(BridgeExitResult result, const QString& message);

protected:
    void run() override;

private:
    std::unique_ptr<ViconLSLBridge> bridge_;
};

class BridgeWindow : public QWidget {
    Q_OBJECT
public:
    explicit BridgeWindow(QWidget* parent = nullptr, bool enable_preview = true);
    ~BridgeWindow() override;

    // These accessors intentionally expose the small amount of state that an
    // automated GUI check needs without coupling it to widget text.
    bool labRecorderConnected() const;
    bool labRecorderOwnedProcessRunning() const;
    bool stairModelLoaded() const;
    bool configurableTooltipsPresent() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStart();
    void onStop();
    void onBrowseStudyRoot();
    void onBrowseLabRecorder();
    void onLaunchLabRecorder();
    void onConnectLabRecorder();
    void onRefreshLabRecorder();
    void onStartRecording();
    void onStopRecording();
    void updateFilenamePreview();
    void syncFilenameToLabRecorder();
    void onStatusStaleCheck();
    void onStatusUpdate(int state, unsigned long long markers, unsigned long long segments,
                        unsigned int frames, const QString& message);
    void onWorkerFinished();
    void onLabRecorderRetry();
    void onClosePoll();

private:
    void loadSettings();
    void saveSettings() const;
    void setInputsEnabled(bool enabled);
    LabRecorderFilenameFields filenameFields() const;
    QString renderedFilenamePreview() const;
    QString filenameValidationError() const;
    void setLabRecorderStatus(const QString& status);
    bool isFilenameValid() const;
    void scheduleFilenameSync();
    void updateReadiness();
    void updateRecordingButtons();
    QString resolveLabRecorderExecutable() const;
    void beginLabRecorderStartup();
    void stopOwnedLabRecorder();
    void finishCloseIfReady();

    std::unique_ptr<vicon_lsl::gui_detail::BridgeWindowUi> ui_;

    LabRecorderClient labrecorder_client_;
    std::unique_ptr<QProcess> labrecorder_process_;
    bool labrecorder_process_owned_ = false;
    QTimer* labrecorder_retry_timer_ = nullptr;
    QTimer* filename_sync_timer_ = nullptr;
    QElapsedTimer labrecorder_retry_elapsed_;
    QTimer* close_poll_timer_ = nullptr;
    QElapsedTimer close_elapsed_;
    bool close_stop_requested_ = false;
    QElapsedTimer status_timer_;
    QTimer* status_stale_timer_;
    bool have_previous_status_ = false;
    unsigned int previous_frames_ = 0;
    qint64 previous_status_ms_ = 0;
    bool bridge_streaming_ = false;
    bool bridge_status_stale_ = false;
    BridgeWorker* worker_ = nullptr;
    bool close_pending_ = false;
    bool close_finalizing_ = false;
};

Q_DECLARE_METATYPE(BridgeExitResult)
