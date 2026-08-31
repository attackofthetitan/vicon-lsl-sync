#pragma once

#include <QWidget>
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

class BridgeWorker : public QThread {
    Q_OBJECT
public:
    explicit BridgeWorker(const Config& config, QObject* parent = nullptr);
    void stopBridge();

signals:
    void statusUpdate(int state, unsigned long long markers, unsigned long long segments,
                      unsigned int frames, const QString& message);
    void failed(const QString& message);

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

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStart();
    void onStop();
    void onStartSession();
    void onStopSession();
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
    void saveSettings() const;
    LabRecorderFilenameFields filenameFields() const;
    QString filenameValidationError() const;
    void setLabRecorderStatus(const QString& status);
    bool isFilenameValid() const;
    void scheduleFilenameSync();
    void updateReadiness();
    void updateRecordingButtons();
    void advanceSessionStart();
    void advanceSessionStop();
    void setSessionStatus(const QString& status);
    void updateSessionButtons();
    void beginLabRecorderStartup();
    void stopOwnedLabRecorder();
    void finishCloseIfReady();

    std::unique_ptr<vicon_lsl::gui_detail::BridgeWindowUi> ui_;

    enum class SessionState {
        Ready,
        Starting,
        Recording,
        Stopping,
        Error,
    };

    LabRecorderClient labrecorder_client_;
    std::unique_ptr<QProcess> labrecorder_process_;
    bool labrecorder_process_owned_ = false;
    QTimer* labrecorder_retry_timer_ = nullptr;
    QTimer* filename_sync_timer_ = nullptr;
    QElapsedTimer labrecorder_retry_elapsed_;
    QTimer* close_poll_timer_ = nullptr;
    QElapsedTimer close_elapsed_;
    QElapsedTimer status_timer_;
    QTimer* status_stale_timer_;
    bool have_previous_status_ = false;
    unsigned int previous_frames_ = 0;
    qint64 previous_status_ms_ = 0;
    bool bridge_streaming_ = false;
    bool bridge_status_stale_ = false;
    BridgeWorker* worker_ = nullptr;
    SessionState session_state_ = SessionState::Ready;
    bool close_pending_ = false;
    bool close_finalizing_ = false;
};
