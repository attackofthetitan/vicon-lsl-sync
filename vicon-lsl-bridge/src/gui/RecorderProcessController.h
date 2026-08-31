#pragma once

#include "gui/PerformanceBudgets.h"
#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

namespace vicon_lsl::gui {

enum class RecorderProcessKind {
    None,
    GraphicalRecorder,
    AllowlistRecorder,
};

class RecorderProcessController : public QObject {
    Q_OBJECT

public:
    explicit RecorderProcessController(QObject* parent = nullptr);

    RecorderProcessState state() const { return state_; }
    RecorderProcessKind kind() const { return kind_; }
    bool ownsRunningProcess() const;
    bool allowlistRecording() const;
    QByteArray boundedOutput() const { return output_buffer_; }

    bool launchGraphicalRecorder(const QString& executable, QString* error = nullptr);
    bool launchAllowlistRecorder(const QString& executable,
                                 const QString& absolute_output_path,
                                 const QVector<StreamIdentity>& selected_streams,
                                 QString* error = nullptr);
    bool stopAllowlistRecording();
    void endOwnedProcess();
    void detach();

    static QString bundledAllowlistExecutable(const QString& graphical_executable,
                                              const QString& application_directory);
    static QStringList allowlistArguments(const QString& absolute_output_path,
                                          const QVector<StreamIdentity>& selected_streams,
                                          QString* error = nullptr);

signals:
    void stateChanged(RecorderProcessState state, QString detail);
    void recordingStateChanged(RecorderRecordingState state);
    void outputLine(EventSeverity severity, QString line);
    void processExited(int exit_code, bool expected, RecorderProcessKind kind);

private slots:
    void drainOutput();
    void onStarted();
    void onError(QProcess::ProcessError error);
    void onFinished(int exit_code, QProcess::ExitStatus status);
    void onTerminateDeadline();

private:
    bool startProcess(RecorderProcessKind kind,
                      const QString& executable,
                      const QStringList& arguments,
                      QString* error);
    void setState(RecorderProcessState state, const QString& detail);
    void appendOutput(const QByteArray& bytes, EventSeverity severity);
    static QString queryLiteral(const QString& value, QString* error);

    QProcess* process_ = nullptr;
    QTimer terminate_deadline_;
    RecorderProcessState state_ = RecorderProcessState::External;
    RecorderProcessKind kind_ = RecorderProcessKind::None;
    QByteArray output_buffer_;
    QByteArray partial_line_;
    bool stop_requested_ = false;
    bool ending_owned_process_ = false;
    bool detached_ = false;
};

} // namespace vicon_lsl::gui

Q_DECLARE_METATYPE(vicon_lsl::gui::RecorderProcessKind)
