#include "gui/SessionState.h"

#include <algorithm>
#include <array>

namespace {

template <typename Enum, std::size_t Size>
QString enumText(Enum value,
                 const std::array<const char*, Size>& labels,
                 const char* fallback) {
    const auto index = static_cast<std::size_t>(value);
    return QString::fromLatin1(index < Size ? labels[index] : fallback);
}

QString setupCheckLevelText(SetupCheckLevel level) {
    return enumText(level, std::array{"required", "warning", "information"},
                    "information");
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

bool SessionEventLog::matchesFilter(const SessionEvent& event,
                                    EventSeverity minimum,
                                    const QVector<SessionComponent>& components) {
    return static_cast<int>(event.severity) >= static_cast<int>(minimum) &&
           (components.isEmpty() || components.contains(event.component));
}

QString SessionEventLog::formatEvent(const SessionEvent& event) {
    return event.timestamp.toString(Qt::ISODateWithMs) + " [" +
           severityText(event.severity) + "] [" +
           componentText(event.component) + "] " + event.message;
}

QString SessionEventLog::toText(EventSeverity minimum,
                                const QVector<SessionComponent>& components) const {
    QStringList lines;
    for (const SessionEvent& event : entries_) {
        if (matchesFilter(event, minimum, components)) {
            lines.push_back(formatEvent(event));
        }
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
    return enumText(component,
                    std::array{"application", "bridge", "recorder", "preview",
                               "calibration", "file", "path", "streams",
                               "verification"},
                    "application");
}

QString SessionEventLog::severityText(EventSeverity severity) {
    return enumText(severity, std::array{"info", "warning", "error"}, "info");
}

bool SetupCheckResult::hasRequiredFailures() const {
    return std::any_of(items.begin(), items.end(), [](const SetupCheckItem& item) {
        return item.level == SetupCheckLevel::Required && !item.passed;
    });
}

bool SetupCheckResult::hasWarnings() const {
    return std::any_of(items.begin(), items.end(), [](const SetupCheckItem& item) {
        return item.level == SetupCheckLevel::Warning && !item.passed;
    });
}

QString SetupCheckResult::summary() const {
    QStringList failures;
    for (const SetupCheckItem& item : items) {
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

QJsonObject SetupCheckResult::toJson() const {
    QJsonArray serialized_items;
    for (const SetupCheckItem& item : items) {
        serialized_items.push_back(QJsonObject{
            {"component", SessionEventLog::componentText(item.component)},
            {"level", setupCheckLevelText(item.level)},
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
    return enumText(state,
                    std::array{"Disconnected", "Connecting", "Connected", "Error"},
                    "Unknown");
}

QString recorderRecordingStateText(RecorderRecordingState state) {
    return enumText(state, std::array{"Unknown", "Stopped", "Recording"},
                    "Unknown");
}

QString recorderOperationStateText(RecorderOperationState state) {
    return enumText(state,
                    std::array{"Idle", "Refreshing", "Updating filename", "Starting",
                               "Stopping", "Shutting down"},
                    "Idle");
}

QString componentLifecycleStateText(ComponentLifecycleState state) {
    return enumText(state,
                    std::array{"Idle", "Starting", "Running", "Stopping", "Stopped",
                               "Failed"},
                    "Idle");
}

QString recorderProcessStateText(RecorderProcessState state) {
    return enumText(state,
                    std::array{"External", "Starting", "Started here",
                               "Started here, now stopped", "Could not start",
                               "Disconnected from app"},
                    "External");
}

QString verificationStateText(RecordingVerificationState state) {
    return enumText(state,
                    std::array{"Not checked", "Checking", "Checked",
                               "Checked with warnings", "Needs attention"},
                    "Not checked");
}

QString vicon_lsl::gui::calibrationStateText(SessionCalibrationState state) {
    return enumText(state,
                    std::array{"Manual", "Collecting", "Current session", "Saved",
                               "Needs attention"},
                    "Manual");
}

QString vicon_lsl::gui::fileStateText(SessionFileState state) {
    return enumText(state,
                    std::array{"No file loaded", "Loading", "Loaded", "Canceled",
                               "Needs attention"},
                    "No file loaded");
}
