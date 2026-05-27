#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QThread>
#include <memory>
#include "ViconLSLBridge.h"

class BridgeWorker : public QThread {
    Q_OBJECT
public:
    explicit BridgeWorker(const Config& config, QObject* parent = nullptr);
    void stopBridge();

signals:
    void statusUpdate(int state, unsigned long long markers, unsigned long long segments,
                      unsigned int frames, bool gaze_enabled, bool gaze_listening,
                      unsigned long long gaze_samples,
                      unsigned long long gaze_malformed_packets,
                      const QString& gaze_last_error,
                      const QString& message);

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
    void onStatusUpdate(int state, unsigned long long markers, unsigned long long segments,
                        unsigned int frames, bool gaze_enabled, bool gaze_listening,
                        unsigned long long gaze_samples,
                        unsigned long long gaze_malformed_packets,
                        const QString& gaze_last_error,
                        const QString& message);
    void onWorkerFinished();

private:
    void loadSettings();
    void saveSettings() const;
    void setInputsEnabled(bool enabled);

    QLineEdit* server_edit_;
    QLineEdit* marker_stream_edit_;
    QLineEdit* segment_stream_edit_;
    QCheckBox* gaze_enabled_check_;
    QSpinBox* gaze_port_spin_;
    QLineEdit* gaze_stream_edit_;
    QPushButton* start_button_;
    QPushButton* stop_button_;
    QLabel* status_label_;
    QLabel* gaze_status_label_;
    QLabel* markers_label_;
    QLabel* segments_label_;
    QLabel* frames_label_;
    QLabel* gaze_samples_label_;
    QLabel* malformed_packets_label_;
    QLabel* last_error_label_;

    BridgeWorker* worker_ = nullptr;
};
