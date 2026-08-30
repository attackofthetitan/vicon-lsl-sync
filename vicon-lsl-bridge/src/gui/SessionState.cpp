#include "gui/SessionState.h"

#include <algorithm>

namespace {

QString preflightLevelText(PreflightLevel level) {
    switch (level) {
        case PreflightLevel::Required: return "required";
        case PreflightLevel::Warning: return "warning";
        case PreflightLevel::Information: return "information";
    }
    return "information";
}

} // namespace

SessionEventLog::SessionEventLog(std::size_t maximum_entries)
    : maximum_entries_((std::max)(std::size_t{1}, maximum_entries)) {}

void SessionEventLog::append(SessionComponent component,
                             EventSeverity severity,
                             const QString& message) {
    if (message.trimmed().isEmpty()) {
        return;
    }
    const QDateTime timestamp = clock_
        ? clock_() : QDateTime::currentDateTimeUtc();
    entries_.push_back({timestamp, component, severity, message.trimmed()});
    while (static_cast<std::size_t>(entries_.size()) > maximum_entries_) {
        entries_.removeFirst();
    }
    if (severity == EventSeverity::Error) {
        last_error_ = message.trimmed();
    }
}

void SessionEventLog::acknowledgeLastError() {
    last_error_.clear();
}

QString SessionEventLog::toText(EventSeverity minimum,
                                const QVector<SessionComponent>& components) const {
    QStringList lines;
    for (const SessionEvent& event : entries_) {
        if (static_cast<int>(event.severity) < static_cast<int>(minimum) ||
            (!components.isEmpty() && !components.contains(event.component))) {
            continue;
        }
        lines.push_back(event.timestamp.toString(Qt::ISODateWithMs) + " [" +
                        severityText(event.severity) + "] [" +
                        componentText(event.component) + "] " + event.message);
    }
    return lines.join('\n');
}

QJsonArray SessionEventLog::toJson() const {
    QJsonArray result;
    for (const SessionEvent& event : entries_) {
        result.push_back(QJsonObject{
            {"timestamp", event.timestamp.toString(Qt::ISODateWithMs)},
            {"component", componentText(event.component)},
            {"severity", severityText(event.severity)},
            {"message", event.message},
        });
    }
    return result;
}

QString SessionEventLog::componentText(SessionComponent component) {
    switch (component) {
        case SessionComponent::Application: return "application";
        case SessionComponent::Bridge: return "bridge";
        case SessionComponent::Recorder: return "recorder";
        case SessionComponent::Preview: return "preview";
        case SessionComponent::Calibration: return "calibration";
        case SessionComponent::File: return "file";
        case SessionComponent::Path: return "path";
        case SessionComponent::Streams: return "streams";
        case SessionComponent::Verification: return "verification";
    }
    return "application";
}

QString SessionEventLog::severityText(EventSeverity severity) {
    switch (severity) {
        case EventSeverity::Information: return "info";
        case EventSeverity::Warning: return "warning";
        case EventSeverity::Error: return "error";
    }
    return "info";
}

bool PreflightResult::hasRequiredFailures() const {
    return std::any_of(items.begin(), items.end(), [](const PreflightItem& item) {
        return item.level == PreflightLevel::Required && !item.passed;
    });
}

bool PreflightResult::hasWarnings() const {
    return std::any_of(items.begin(), items.end(), [](const PreflightItem& item) {
        return item.level == PreflightLevel::Warning && !item.passed;
    });
}

bool PreflightResult::canStart() const {
    return !hasRequiredFailures() || (override_used && !override_reason.trimmed().isEmpty());
}

QString PreflightResult::summary() const {
    QStringList failures;
    for (const PreflightItem& item : items) {
        if (!item.passed) {
            QString text = item.message;
            if (!item.corrective_action.isEmpty()) {
                text += " — " + item.corrective_action;
            }
            failures.push_back(text);
        }
    }
    if (failures.isEmpty()) {
        return "Setup check passed";
    }
    QString result = failures.join("\n");
    if (override_used) {
        result += "\nRecorded anyway because: " + override_reason;
    }
    return result;
}

QJsonObject PreflightResult::toJson() const {
    QJsonArray serialized_items;
    for (const PreflightItem& item : items) {
        serialized_items.push_back(QJsonObject{
            {"id", item.id},
            {"component", SessionEventLog::componentText(item.component)},
            {"level", preflightLevelText(item.level)},
            {"passed", item.passed},
            {"message", item.message},
            {"correctiveAction", item.corrective_action},
        });
    }
    return QJsonObject{
        {"completedAt", completed_at.toString(Qt::ISODateWithMs)},
        {"requiredFailures", hasRequiredFailures()},
        {"warnings", hasWarnings()},
        {"overrideUsed", override_used},
        {"overrideReason", override_reason},
        {"items", serialized_items},
    };
}

QString recorderConnectionStateText(RecorderConnectionState state) {
    switch (state) {
        case RecorderConnectionState::Disconnected: return "Disconnected";
        case RecorderConnectionState::Connecting: return "Connecting";
        case RecorderConnectionState::Connected: return "Connected";
        case RecorderConnectionState::Error: return "Error";
    }
    return "Unknown";
}

QString recorderRecordingStateText(RecorderRecordingState state) {
    switch (state) {
        case RecorderRecordingState::Unknown: return "Unknown";
        case RecorderRecordingState::Stopped: return "Stopped";
        case RecorderRecordingState::Recording: return "Recording";
    }
    return "Unknown";
}

QString recorderOperationStateText(RecorderOperationState state) {
    switch (state) {
        case RecorderOperationState::Idle: return "Idle";
        case RecorderOperationState::Refreshing: return "Refreshing";
        case RecorderOperationState::UpdatingFilename: return "Updating filename";
        case RecorderOperationState::Starting: return "Starting";
        case RecorderOperationState::Stopping: return "Stopping";
        case RecorderOperationState::ShuttingDown: return "Shutting down";
    }
    return "Idle";
}

QString componentLifecycleStateText(ComponentLifecycleState state) {
    switch (state) {
        case ComponentLifecycleState::Idle: return "Idle";
        case ComponentLifecycleState::Starting: return "Starting";
        case ComponentLifecycleState::Running: return "Running";
        case ComponentLifecycleState::Stopping: return "Stopping";
        case ComponentLifecycleState::Stopped: return "Stopped";
        case ComponentLifecycleState::Failed: return "Failed";
    }
    return "Idle";
}

QString recorderProcessStateText(RecorderProcessState state) {
    switch (state) {
        case RecorderProcessState::External: return "External";
        case RecorderProcessState::Launching: return "Starting";
        case RecorderProcessState::OwnedRunning: return "Started here";
        case RecorderProcessState::OwnedExited: return "Started here, now stopped";
        case RecorderProcessState::LaunchFailed: return "Could not start";
        case RecorderProcessState::Detached: return "Disconnected from app";
    }
    return "External";
}

QString verificationStateText(RecordingVerificationState state) {
    switch (state) {
        case RecordingVerificationState::NotRun: return "Not checked";
        case RecordingVerificationState::Running: return "Checking";
        case RecordingVerificationState::Verified: return "Checked";
        case RecordingVerificationState::VerifiedWithWarnings: return "Checked with warnings";
        case RecordingVerificationState::NeedsAttention: return "Needs attention";
    }
    return "Not checked";
}
