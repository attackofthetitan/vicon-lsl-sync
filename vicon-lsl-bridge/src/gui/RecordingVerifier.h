#pragma once

#include "gui/PerformanceBudgets.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"

#include <QJsonObject>
#include <QThread>
#include <QVector>

#include <atomic>
#include <functional>

namespace vicon_lsl::gui {

struct RecordingVerificationFinding {
    EventSeverity severity = EventSeverity::Information;
    QString id;
    QString stream;
    QString message;
    QString corrective_action;

    QJsonObject toJson() const;
};

struct RecordedStreamVerification {
    QString name;
    QString type;
    QString source_id;
    QString hostname;
    QString session_id;
    QString coordinate_frame;
    int channel_count = 0;
    double nominal_rate = 0.0;
    double effective_rate = 0.0;
    qint64 sample_count = 0;
    double start_time = 0.0;
    double end_time = 0.0;
    double maximum_gap = 0.0;
    qint64 large_gap_count = 0;
    qint64 clock_correction_count = 0;
    qint64 repaired_timestamp_count = 0;

    QJsonObject toJson() const;
};

struct RecordingVerificationReport {
    RecordingVerificationState state = RecordingVerificationState::NotRun;
    QString path;
    QDateTime started_at;
    QDateTime completed_at;
    double duration_seconds = 0.0;
    QVector<RecordedStreamVerification> streams;
    QVector<RecordingVerificationFinding> findings;
    bool truncated_tail_recovered = false;
    qint64 file_size_bytes = 0;

    bool hasErrors() const;
    bool hasWarnings() const;
    QString summary() const;
    QJsonObject toJson() const;
};

struct RecordingVerificationRequest {
    QString path;
    QVector<StreamIdentity> preflight_inventory;
    QVector<StreamBinding> expected_streams;
    bool record_every_visible_stream = true;
    std::function<QDateTime()> now_utc;
};

class RecordingVerifier : public QThread {
    Q_OBJECT

public:
    explicit RecordingVerifier(RecordingVerificationRequest request,
                               QObject* parent = nullptr);
    void cancel();

signals:
    void progressChanged(QString stage, int percent, QString detail);
    void verificationFinished(vicon_lsl::gui::RecordingVerificationReport report);
    void lifecycleChanged(ComponentLifecycleState state, QString detail);

protected:
    void run() override;

private:
    RecordingVerificationRequest request_;
    std::atomic<bool> cancel_requested_{false};
};

} // namespace vicon_lsl::gui

Q_DECLARE_METATYPE(vicon_lsl::gui::RecordingVerificationReport)
