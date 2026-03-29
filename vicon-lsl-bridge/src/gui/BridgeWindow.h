#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QThread>
#include <memory>
#include "ViconLSLBridge.h"

class BridgeWorker : public QThread {
    Q_OBJECT
public:
    explicit BridgeWorker(const Config& config, QObject* parent = nullptr);
    void stopBridge();

signals:
    void statusUpdate(int state, size_t markers, size_t segments,
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
    void onStatusUpdate(int state, size_t markers, size_t segments,
                        unsigned int frames, const QString& message);
    void onWorkerFinished();

private:
    QLineEdit* server_edit_;
    QLineEdit* marker_stream_edit_;
    QLineEdit* segment_stream_edit_;
    QPushButton* start_button_;
    QPushButton* stop_button_;
    QLabel* status_label_;
    QLabel* markers_label_;
    QLabel* segments_label_;
    QLabel* frames_label_;

    BridgeWorker* worker_ = nullptr;
};
