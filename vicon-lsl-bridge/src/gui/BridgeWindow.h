#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QThread>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <memory>
#include "gui/LabRecorderClient.h"
#include "gui/PreviewPanel.h"
#include "ViconLSLBridge.h"

class BridgeWorker : public QThread {
    Q_OBJECT
public:
    explicit BridgeWorker(const Config& config, QObject* parent = nullptr);
    void stopBridge();

signals:
    void statusUpdate(int state, unsigned long long markers, unsigned long long segments,
                      unsigned int frames, const QString& message);

protected:
    void run() override;

private:
    Config config_;
    std::unique_ptr<ViconLSLBridge> bridge_;
};

class BridgeWindow : public QWidget {
    Q_OBJECT
public:
    explicit BridgeWindow(QWidget* parent = nullptr);
    ~BridgeWindow() override;

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
    void onStatusStaleCheck();
    void onStatusUpdate(int state, unsigned long long markers, unsigned long long segments,
                        unsigned int frames, const QString& message);
    void onWorkerFinished();

private:
    void loadSettings();
    void saveSettings() const;
    void setInputsEnabled(bool enabled);
    LabRecorderFilenameFields filenameFields() const;
    QString renderedFilenamePreview() const;
    QString filenameValidationError() const;
    void setLabRecorderStatus(const QString& status);
    bool sendLabRecorderCommand(bool ok, const QString& success_message);
    bool isFilenameValid() const;
    void updateReadiness();
    void updateRecordingButtons();

    QLineEdit* server_edit_;
    QLineEdit* marker_stream_edit_;
    QLineEdit* segment_stream_edit_;
    QPushButton* start_button_;
    QPushButton* stop_button_;
    QLabel* status_label_;
    QLabel* markers_label_;
    QLabel* segments_label_;
    QLabel* frames_label_;
    QLabel* frame_rate_label_;
    QLabel* last_error_label_;

    QLineEdit* study_root_edit_;
    QLineEdit* filename_template_edit_;
    QLineEdit* participant_edit_;
    QLineEdit* session_edit_;
    QLineEdit* task_edit_;
    QSpinBox* run_spin_;
    QLineEdit* acquisition_edit_;
    QLineEdit* modality_edit_;
    QLineEdit* filename_preview_label_;
    QCheckBox* select_all_before_start_check_;
    QLineEdit* labrecorder_executable_edit_;
    QLineEdit* labrecorder_host_edit_;
    QSpinBox* labrecorder_port_spin_;
    QPushButton* launch_labrecorder_button_;
    QPushButton* connect_labrecorder_button_;
    QPushButton* refresh_streams_button_;
    QPushButton* start_recording_button_;
    QPushButton* stop_recording_button_;
    QLabel* labrecorder_status_label_;
    QLabel* readiness_label_;
    vicon_lsl::PreviewPanel* preview_panel_;

    LabRecorderClient labrecorder_client_;
    std::unique_ptr<QProcess> labrecorder_process_;
    QElapsedTimer status_timer_;
    QTimer* status_stale_timer_;
    bool have_previous_status_ = false;
    unsigned int previous_frames_ = 0;
    qint64 previous_status_ms_ = 0;
    bool bridge_streaming_ = false;
    bool bridge_status_stale_ = false;
    bool labrecorder_connected_ = false;
    bool recording_requested_ = false;

    BridgeWorker* worker_ = nullptr;
};
