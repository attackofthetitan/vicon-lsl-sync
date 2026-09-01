#include "gui/RecorderProcessController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace vicon_lsl::gui {
namespace {
constexpr qsizetype kMaxLogLineBytes = 4096;
constexpr qsizetype kMaxOutputBytes = 64 * 1024;
} // namespace

RecorderProcessController::RecorderProcessController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<RecorderProcessKind>("vicon_lsl::gui::RecorderProcessKind");
    terminate_deadline_.setSingleShot(true);
    terminate_deadline_.setInterval(1000);
    connect(&terminate_deadline_, &QTimer::timeout, this, &RecorderProcessController::onTerminateDeadline);
}

bool RecorderProcessController::ownsRunningProcess() const {
    return !detached_ && process_ && process_->state() != QProcess::NotRunning &&
           (state_ == RecorderProcessState::Launching || state_ == RecorderProcessState::OwnedRunning);
}

bool RecorderProcessController::selectedStreamRecording() const {
    return ownsRunningProcess() && kind_ == RecorderProcessKind::SelectedStreamRecorder && !stop_requested_;
}

bool RecorderProcessController::launchGraphicalRecorder(const QString& executable, QString* error) {
    return startProcess(RecorderProcessKind::GraphicalRecorder, executable, {}, error);
}

bool RecorderProcessController::launchSelectedStreamRecorder(const QString& executable,
                                                       const QString& absolute_output_path,
                                                       const QVector<StreamIdentity>& selected_streams,
                                                       QString* error) {
    const QStringList arguments = selectedStreamArguments(absolute_output_path, selected_streams, error);
    return !arguments.isEmpty() && startProcess(RecorderProcessKind::SelectedStreamRecorder, executable, arguments, error);
}

bool RecorderProcessController::stopSelectedStreamRecording() {
    if (!process_ || process_->state() == QProcess::NotRunning ||
        kind_ != RecorderProcessKind::SelectedStreamRecorder || stop_requested_) return false;
    stop_requested_ = true;
    process_->write("\n");
    process_->closeWriteChannel();
    emit recordingStateChanged(RecorderRecordingState::Unknown);
    return true;
}

void RecorderProcessController::endOwnedProcess() {
    if (!ownsRunningProcess() || ending_owned_process_) return;
    ending_owned_process_ = true;
    if (kind_ == RecorderProcessKind::SelectedStreamRecorder && !stop_requested_) {
        stopSelectedStreamRecording();
    } else {
        process_->terminate();
    }
    terminate_deadline_.start();
}

void RecorderProcessController::detach() {
    if (!process_ || process_->state() == QProcess::NotRunning) {
        setState(RecorderProcessState::Detached, "No recorder started here is still connected to the app");
        return;
    }
    QProcess* detached_proc = process_;
    detached_ = true;
    process_ = nullptr;
    terminate_deadline_.stop();
    disconnect(detached_proc, nullptr, this, nullptr);
    detached_proc->setParent(nullptr);
    connect(detached_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            detached_proc, &QObject::deleteLater);
    setState(RecorderProcessState::Detached, "Recorder disconnected from the app and will keep running");
    kind_ = RecorderProcessKind::None;
}

QString RecorderProcessController::bundledSelectedStreamExecutable(const QString& graphical_executable,
                                                            const QString& app_dir) {
    QStringList candidates;
    if (!graphical_executable.trimmed().isEmpty()) {
        const QString dir = QFileInfo(graphical_executable).absolutePath();
        candidates.push_back(QDir(dir).filePath("LabRecorderCLI.exe"));
        candidates.push_back(QDir(dir).filePath("LabRecorderCLI"));
    }
    candidates.push_back(QDir(app_dir).filePath("labrecorder/LabRecorderCLI.exe"));
    candidates.push_back(QDir(app_dir).filePath("labrecorder/LabRecorderCLI"));
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) return QDir::toNativeSeparators(info.absoluteFilePath());
    }
    return {};
}

QStringList RecorderProcessController::selectedStreamArguments(const QString& absolute_output_path,
                                                        const QVector<StreamIdentity>& selected_streams,
                                                        QString* error) {
    const QFileInfo output(absolute_output_path);
    if (!output.isAbsolute()) {
        if (error) *error = "Exact recording requires an absolute output path";
        return {};
    }
    QStringList arguments{QDir::toNativeSeparators(output.absoluteFilePath())};
    for (const StreamIdentity& identity : selected_streams) {
        if (!identity.selected) continue;
        QString literal_error;
        auto term = [&](const QString& key, const QString& value) {
            const QString literal = queryLiteral(value, &literal_error);
            return literal_error.isEmpty() ? key + "=" + literal : QString();
        };
        QString query;
        if (!identity.source_id.trimmed().isEmpty()) {
            query = term("source_id", identity.source_id.trimmed());
        } else {
            query = term("name", identity.name);
            if (literal_error.isEmpty() && !identity.hostname.trimmed().isEmpty()) {
                query += " and " + term("hostname", identity.hostname.trimmed());
            }
        }
        if (!literal_error.isEmpty()) {
            if (error) *error = identity.displayText() + ": " + literal_error;
            return {};
        }
        arguments.push_back(query);
    }
    if (arguments.size() < 2) {
        if (error) *error = "Select at least one visible stream for exact recording";
        return {};
    }
    if (error) error->clear();
    return arguments;
}

void RecorderProcessController::drainOutput() {
    if (!process_) return;
    appendOutput(process_->readAllStandardOutput(), EventSeverity::Information);
    appendOutput(process_->readAllStandardError(), EventSeverity::Warning);
}

void RecorderProcessController::onStarted() {
    setState(RecorderProcessState::OwnedRunning,
             kind_ == RecorderProcessKind::SelectedStreamRecorder ? "Selected-stream recorder started" : "Recorder started");
    if (kind_ == RecorderProcessKind::SelectedStreamRecorder) emit recordingStateChanged(RecorderRecordingState::Recording);
}

void RecorderProcessController::onError(QProcess::ProcessError) {
    if (!process_) return;
    const QString message = process_->errorString();
    if (process_->state() == QProcess::NotRunning && state_ == RecorderProcessState::Launching) {
        setState(RecorderProcessState::LaunchFailed, message);
    }
    emit outputLine(EventSeverity::Error, message);
}

void RecorderProcessController::onFinished(int exit_code, QProcess::ExitStatus status) {
    drainOutput();
    terminate_deadline_.stop();
    const RecorderProcessKind finished_kind = kind_;
    const bool expected = stop_requested_ || ending_owned_process_;
    if (finished_kind == RecorderProcessKind::SelectedStreamRecorder) {
        emit recordingStateChanged(RecorderRecordingState::Stopped);
    }
    setState(RecorderProcessState::OwnedExited,
             QString("Recorder stopped with code %1%2").arg(exit_code).arg(status == QProcess::CrashExit ? " after a crash" : ""));
    emit processExited(exit_code, expected, finished_kind);
    if (process_) {
        process_->deleteLater();
        process_ = nullptr;
    }
    kind_ = RecorderProcessKind::None;
    stop_requested_ = ending_owned_process_ = detached_ = false;
}

void RecorderProcessController::onTerminateDeadline() {
    if (!process_ || process_->state() == QProcess::NotRunning) return;
    process_->kill();
    emit outputLine(EventSeverity::Warning, "Recorder did not stop in time and was forced to close");
}

bool RecorderProcessController::startProcess(RecorderProcessKind kind, const QString& executable,
                                             const QStringList& arguments, QString* error) {
    const QFileInfo info(executable);
    if (!info.exists() || !info.isFile()) {
        const QString message = "Recorder program was not found: " + executable;
        if (error) *error = message;
        setState(RecorderProcessState::LaunchFailed, message);
        return false;
    }
    if (process_ && process_->state() != QProcess::NotRunning) {
        if (error) *error = "A recorder started by this app is already running";
        return false;
    }
    if (process_) {
        process_->deleteLater();
        process_ = nullptr;
    }
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setWorkingDirectory(info.absolutePath());
    connect(process_, &QProcess::readyReadStandardOutput, this, &RecorderProcessController::drainOutput);
    connect(process_, &QProcess::readyReadStandardError, this, &RecorderProcessController::drainOutput);
    connect(process_, &QProcess::started, this, &RecorderProcessController::onStarted);
    connect(process_, &QProcess::errorOccurred, this, &RecorderProcessController::onError);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RecorderProcessController::onFinished);
    kind_ = kind;
    stop_requested_ = ending_owned_process_ = detached_ = false;
    output_buffer_.clear();
    partial_line_.clear();
    setState(RecorderProcessState::Launching,
             kind == RecorderProcessKind::SelectedStreamRecorder ? "Starting selected-stream recorder" : "Starting recorder");
    process_->start(QDir::toNativeSeparators(info.absoluteFilePath()), arguments, QIODevice::ReadWrite);
    if (error) error->clear();
    return true;
}

void RecorderProcessController::setState(RecorderProcessState state, const QString& detail) {
    state_ = state;
    emit stateChanged(state_, detail);
}

void RecorderProcessController::appendOutput(const QByteArray& bytes, EventSeverity severity) {
    if (bytes.isEmpty()) return;
    output_buffer_.append(bytes);
    if (output_buffer_.size() > kMaxOutputBytes) output_buffer_.remove(0, output_buffer_.size() - kMaxOutputBytes);
    partial_line_.append(bytes);
    while (true) {
        const qsizetype newline = partial_line_.indexOf('\n');
        if (newline < 0) break;
        QByteArray line = partial_line_.left(newline);
        partial_line_.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.size() > kMaxLogLineBytes) line = line.left(kMaxLogLineBytes) + "...";
        if (!line.trimmed().isEmpty()) emit outputLine(severity, QString::fromLocal8Bit(line));
    }
    if (partial_line_.size() > kMaxLogLineBytes) {
        emit outputLine(severity, QString::fromLocal8Bit(partial_line_.left(kMaxLogLineBytes)) + "...");
        partial_line_.clear();
    }
}

QString RecorderProcessController::queryLiteral(const QString& value, QString* error) {
    if (value.contains('\n') || value.contains('\r')) {
        if (error) *error = "Stream source contains a line break";
        return {};
    }
    if (!value.contains('\'')) {
        if (error) error->clear();
        return "'" + value + "'";
    }
    if (!value.contains('"')) {
        if (error) error->clear();
        return "\"" + value + "\"";
    }
    if (error) *error = "Stream source contains both quote styles and cannot be selected safely";
    return {};
}

} // namespace vicon_lsl::gui
