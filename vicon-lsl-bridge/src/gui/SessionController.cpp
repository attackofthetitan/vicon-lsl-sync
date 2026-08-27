#include "gui/SessionController.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace vicon_lsl::gui {
namespace {

constexpr qint64 kFreshStreamLimitMs = 2000;
constexpr double kMinimumRateFraction = 0.8;

bool identityMatchesBinding(const StreamIdentity& identity,
                            const StreamBinding& binding) {
    if (!binding.source_id.trimmed().isEmpty() &&
        binding.reconnection == StreamReconnectionMode::SourceIdentity) {
        return identity.source_id == binding.source_id;
    }
    return identity.name == binding.name;
}

QString bindingDescription(const StreamBinding& binding) {
    QString result = binding.role + " stream " + binding.name;
    if (!binding.source_id.isEmpty()) result += " [" + binding.source_id + "]";
    return result;
}

void addItem(PreflightResult& result,
             QString id,
             SessionComponent component,
             PreflightLevel level,
             bool passed,
             QString message,
             QString action = {}) {
    result.items.push_back({std::move(id), component, level, passed,
                            std::move(message), std::move(action)});
}

QString workflowText(SessionWorkflowState state) {
    return SessionController::workflowStateText(state);
}

} // namespace

bool SessionShutdownStatus::complete() const {
    return result == ShutdownResult::Completed ||
           std::all_of(components.begin(), components.end(),
                       [](const ComponentShutdownStatus& component) {
                           return !component.required || component.stopped;
                       });
}

bool SessionShutdownStatus::ownedRecorderMayBeEnded(qint64 now_ms) const {
    const auto found = std::find_if(components.begin(), components.end(),
        [](const ComponentShutdownStatus& component) {
            return component.component == SessionComponent::Recorder;
        });
    return found == components.end() || !found->required || found->stopped ||
           (found->deadline_ms >= 0 && now_ms >= found->deadline_ms);
}

QStringList SessionShutdownStatus::delayedComponents(qint64 now_ms) const {
    QStringList result;
    for (const ComponentShutdownStatus& component : components) {
        if (!component.required || component.stopped) continue;
        QString text = SessionEventLog::componentText(component.component);
        if (component.deadline_ms >= 0 && now_ms >= component.deadline_ms) {
            text += " (deadline exceeded)";
        } else if (component.deadline_ms >= 0) {
            const double remaining_seconds =
                static_cast<double>(component.deadline_ms - now_ms) / 1000.0;
            text += " (" + QString::number(remaining_seconds, 'f', 1) +
                    " s remaining)";
        }
        if (!component.detail.isEmpty()) text += ": " + component.detail;
        result.push_back(text);
    }
    return result;
}

QJsonObject SessionShutdownStatus::toJson(qint64 now_ms) const {
    QJsonArray serialized;
    for (const ComponentShutdownStatus& component : components) {
        serialized.push_back(QJsonObject{
            {"component", SessionEventLog::componentText(component.component)},
            {"required", component.required},
            {"stopped", component.stopped},
            {"requestedAtMs", component.requested_at_ms},
            {"deadlineMs", component.deadline_ms},
            {"deadlineExceeded", component.deadline_exceeded ||
                (component.required && !component.stopped &&
                 component.deadline_ms >= 0 && now_ms >= component.deadline_ms)},
            {"remainingMs", component.deadline_ms < 0 || component.stopped
                ? -1
                : (std::max)(qint64{0}, component.deadline_ms - now_ms)},
            {"detail", component.detail},
        });
    }
    return QJsonObject{
        {"result", SessionController::shutdownResultText(result)},
        {"startedAtMs", started_at_ms},
        {"components", serialized},
    };
}

SessionController::SessionController()
    : event_log_(PerformanceBudgets::MaximumEventLogEntries),
      clock_([]() { return QDateTime::currentDateTimeUtc(); }) {
    event_log_.setClock(clock_);
}

void SessionController::setClock(std::function<QDateTime()> clock) {
    clock_ = clock ? std::move(clock)
                   : std::function<QDateTime()>([]() {
                         return QDateTime::currentDateTimeUtc();
                     });
    event_log_.setClock(clock_);
}

PreflightResult SessionController::runPreflight(const SessionPreflightInputs& inputs) {
    PreflightResult result;
    result.completed_at = clock_();

    const bool recorder_only = inputs.configuration.recorder_only_mode;
    addItem(result, "bridge-running", SessionComponent::Bridge,
            recorder_only ? PreflightLevel::Information : PreflightLevel::Required,
            recorder_only || inputs.bridge_state == ComponentLifecycleState::Running,
            recorder_only ? "Recorder-only mode does not require the Vicon bridge"
                          : "Vicon bridge is running",
            "Start the bridge or enable Recorder-only mode with a documented reason.");
    if (!recorder_only) {
        addItem(result, "bridge-fresh", SessionComponent::Bridge,
                PreflightLevel::Required, inputs.bridge_status_recent,
                inputs.bridge_status_recent
                    ? "Vicon bridge status is recent"
                    : "Vicon bridge status is stale",
                "Restore Vicon frame delivery before recording.");
    }

    const bool recorder_ready = inputs.configuration.record_every_visible_stream
        ? inputs.recorder_connection == RecorderConnectionState::Connected
        : inputs.allowlist_recorder_available;
    addItem(result, "recorder-ready", SessionComponent::Recorder,
            PreflightLevel::Required, recorder_ready,
            inputs.configuration.record_every_visible_stream
                ? (recorder_ready ? "Remote recorder is connected"
                                  : "Remote recorder is not connected")
                : (recorder_ready ? "Allowlist recorder is available"
                                  : "Allowlist recorder executable is unavailable"),
            inputs.configuration.record_every_visible_stream
                ? "Connect the configured recorder endpoint."
                : "Install or select the bundled command-line recorder.");
    const bool recorder_idle = inputs.recorder_operation == RecorderOperationState::Idle &&
                               inputs.recorder_recording != RecorderRecordingState::Recording;
    addItem(result, "recorder-idle", SessionComponent::Recorder,
            PreflightLevel::Required, recorder_idle,
            recorder_idle ? "Recorder has no conflicting work"
                          : "Recorder is recording or has an operation in flight",
            "Stop the current recording and wait for pending commands to finish.");

    addItem(result, "recording-path", SessionComponent::Path,
            PreflightLevel::Required, inputs.path.valid(),
            inputs.path.valid() ? "Recording destination is valid"
                                : inputs.path.firstError(),
            "Correct the highlighted destination fields before recording.");
    for (const RecordingPathIssue& issue : inputs.path.issues) {
        if (issue.level == RecordingPathIssueLevel::Warning) {
            addItem(result, "path-warning-" + QString::number(result.items.size()),
                    SessionComponent::Path, PreflightLevel::Warning, false,
                    issue.message, issue.corrective_action);
        }
    }

    QVector<StreamBinding> bindings = inputs.configuration.recording_streams;
    for (const StreamBinding& binding : bindings) {
        if (!binding.required) continue;
        QVector<StreamIdentity> matches;
        for (const StreamIdentity& identity : inputs.streams) {
            if (identity.present && identityMatchesBinding(identity, binding)) {
                matches.push_back(identity);
            }
        }
        const QString prefix = "stream-" + binding.role + "-" + binding.name;
        addItem(result, prefix + "-present", SessionComponent::Streams,
                PreflightLevel::Required, !matches.isEmpty(),
                !matches.isEmpty() ? bindingDescription(binding) + " is visible"
                                   : bindingDescription(binding) + " is missing",
                "Start the publisher or select the correct stream identity.");
        if (matches.isEmpty()) continue;

        const StreamIdentity& identity = matches.front();
        if (identity.freshness_ms < 0) {
            addItem(result, prefix + "-fresh", SessionComponent::Streams,
                    PreflightLevel::Warning, false,
                    bindingDescription(binding) +
                        " sample freshness has not been measured",
                    "Start preview or confirm current source samples before recording.");
        } else {
            const bool fresh = identity.freshness_ms <= kFreshStreamLimitMs;
            addItem(result, prefix + "-fresh", SessionComponent::Streams,
                    PreflightLevel::Required, fresh,
                    fresh ? bindingDescription(binding) + " is fresh"
                          : bindingDescription(binding) + " is stale",
                    "Restore current samples before recording.");
        }
        const bool channels_ok = binding.expected_channels <= 0 ||
                                 identity.channel_count == binding.expected_channels;
        const bool frame_ok = binding.expected_coordinate_frame.isEmpty() ||
                              identity.coordinate_frame == binding.expected_coordinate_frame;
        addItem(result, prefix + "-schema", SessionComponent::Streams,
                PreflightLevel::Required,
                identity.schema_compatible && channels_ok && frame_ok,
                identity.schema_compatible && channels_ok && frame_ok
                    ? bindingDescription(binding) + " schema is compatible"
                    : bindingDescription(binding) + " schema or coordinate frame is incompatible",
                "Use a publisher matching the saved channel and coordinate contract.");
        addItem(result, prefix + "-metadata", SessionComponent::Streams,
                PreflightLevel::Warning, identity.metadata_complete,
                identity.metadata_complete
                    ? bindingDescription(binding) + " identity metadata is complete"
                    : bindingDescription(binding) + " has incomplete source, channel, or coordinate metadata",
                "Publish complete source identity, channel, and coordinate metadata or document the fallback.");
        if (binding.expected_nominal_rate > 0.0 && identity.nominal_rate > 0.0) {
            const bool rate_ok = identity.nominal_rate >=
                                 binding.expected_nominal_rate * kMinimumRateFraction;
            addItem(result, prefix + "-rate", SessionComponent::Streams,
                    PreflightLevel::Warning, rate_ok,
                    rate_ok ? bindingDescription(binding) + " nominal rate is healthy"
                            : bindingDescription(binding) + " nominal rate is below the expected range",
                    "Check the source device and network load.");
        }
        if (matches.size() > 1 && binding.source_id.isEmpty()) {
            addItem(result, prefix + "-duplicate", SessionComponent::Streams,
                    PreflightLevel::Warning, false,
                    "Multiple visible streams match " + bindingDescription(binding),
                    "Bind the role to a source ID or deliberately use Follow by name.");
        }
    }

    if (!inputs.configuration.record_every_visible_stream) {
        const bool any_selected = std::any_of(
            inputs.streams.cbegin(), inputs.streams.cend(),
            [](const StreamIdentity& identity) {
                return identity.present && identity.selected;
            });
        addItem(result, "allowlist-selection", SessionComponent::Streams,
                PreflightLevel::Required, any_selected,
                any_selected ? "At least one visible stream is selected for exact recording"
                             : "No visible stream is selected for the recording allowlist",
                "Select one or more visible identities in the Streams table.");
    }

    if (inputs.configuration.calibration_required) {
        addItem(result, "stair-model", SessionComponent::Calibration,
                PreflightLevel::Required, inputs.stair_model_loaded,
                inputs.stair_model_loaded ? "Stair model is loaded"
                                          : "Required stair model is not loaded",
                "Load the stair model selected by the calibration profile.");
        const bool calibration_ready =
            inputs.calibration_state == SessionCalibrationState::AutomaticSession ||
            inputs.calibration_state == SessionCalibrationState::SavedProfile;
        addItem(result, "calibration-ready", SessionComponent::Calibration,
                PreflightLevel::Required, calibration_ready,
                calibration_ready ? "Calibration is applied"
                                  : "The selected workflow requires calibration",
                "Collect a stable calibration or apply a compatible saved profile.");
        addItem(result, "calibration-metadata", SessionComponent::Calibration,
                PreflightLevel::Warning, inputs.calibration_metadata_compatible,
                inputs.calibration_metadata_compatible
                    ? "Calibration coordinate metadata is compatible"
                    : "Calibration is using missing or fallback coordinate metadata",
                "Confirm the coordinate setup or select a matching profile.");
    }

    addItem(result, "stream-inventory", SessionComponent::Streams,
            PreflightLevel::Information, true,
            QString::number(std::count_if(
                inputs.streams.cbegin(), inputs.streams.cend(),
                [](const StreamIdentity& identity) { return identity.present; })) +
                " visible stream(s) inventoried");
    if (inputs.bridge_effective_rate > 0.0) {
        addItem(result, "bridge-rate", SessionComponent::Bridge,
                PreflightLevel::Information, true,
                "Bridge effective rate is " +
                    QString::number(inputs.bridge_effective_rate, 'f', 1) + " Hz");
    }

    last_preflight_ = result;
    dashboard_.workflow = result.hasRequiredFailures()
        ? SessionWorkflowState::PreflightBlocked
        : SessionWorkflowState::Ready;
    event_log_.append(SessionComponent::Application,
                      result.hasRequiredFailures() ? EventSeverity::Warning
                                                   : EventSeverity::Information,
                      result.hasRequiredFailures() ? "Preflight blocked recording"
                                                   : "Preflight passed");
    return last_preflight_;
}

bool SessionController::overridePreflight(const QString& reason) {
    const QString normalized = reason.trimmed();
    if (!last_preflight_.hasRequiredFailures() || normalized.isEmpty()) return false;
    last_preflight_.override_used = true;
    last_preflight_.override_reason = normalized;
    dashboard_.workflow = SessionWorkflowState::Ready;
    event_log_.append(SessionComponent::Application, EventSeverity::Warning,
                      "Preflight override accepted: " + normalized);
    return true;
}

ComponentShutdownStatus SessionController::makeShutdownComponent(
    SessionComponent component,
    bool required,
    qint64 now_ms,
    qint64 deadline_ms) {
    ComponentShutdownStatus result;
    result.component = component;
    result.required = required;
    result.stopped = !required;
    result.requested_at_ms = required ? now_ms : -1;
    result.deadline_ms = required ? now_ms + deadline_ms : -1;
    return result;
}

void SessionController::beginShutdown(qint64 now_ms,
                                      bool bridge_required,
                                      bool preview_required,
                                      bool recorder_required,
                                      bool file_required,
                                      bool verification_required) {
    if (shutdown_.result == ShutdownResult::InProgress) return;
    shutdown_ = {};
    shutdown_.result = ShutdownResult::InProgress;
    shutdown_.started_at_ms = now_ms;
    shutdown_.components = {
        makeShutdownComponent(SessionComponent::Bridge, bridge_required, now_ms,
                              PerformanceBudgets::BridgeStopDeadlineMs),
        makeShutdownComponent(SessionComponent::Preview, preview_required, now_ms,
                              PerformanceBudgets::PreviewStopDeadlineMs),
        makeShutdownComponent(SessionComponent::Recorder, recorder_required, now_ms,
                              PerformanceBudgets::RecorderStopDeadlineMs),
        makeShutdownComponent(SessionComponent::File, file_required, now_ms,
                              PerformanceBudgets::PreviewStopDeadlineMs),
        makeShutdownComponent(SessionComponent::Verification, verification_required, now_ms,
                              PerformanceBudgets::PreviewStopDeadlineMs),
    };
    dashboard_.workflow = SessionWorkflowState::Closing;
    event_log_.append(SessionComponent::Application, EventSeverity::Information,
                      "Shutdown requested");
}

void SessionController::updateShutdownComponent(SessionComponent component,
                                                bool stopped,
                                                const QString& detail,
                                                qint64 now_ms) {
    auto found = std::find_if(shutdown_.components.begin(), shutdown_.components.end(),
        [component](const ComponentShutdownStatus& state) {
            return state.component == component;
        });
    if (found == shutdown_.components.end()) return;
    const bool changed = found->stopped != stopped || found->detail != detail;
    found->stopped = stopped;
    found->detail = detail;
    if (changed) {
        event_log_.append(component, EventSeverity::Information,
                          stopped ? "Shutdown complete" : "Shutdown pending: " + detail);
    }
    updateShutdownDeadlines(now_ms);
}

void SessionController::markRecorderConnectionLostDuringShutdown(
    qint64 now_ms,
    const QString& detail) {
    const bool first_report =
        shutdown_.result != ShutdownResult::RecorderConnectionLost;
    shutdown_.result = ShutdownResult::RecorderConnectionLost;
    updateShutdownComponent(SessionComponent::Recorder, true, detail, now_ms);
    if (first_report) {
        event_log_.append(SessionComponent::Recorder, EventSeverity::Error,
                          "Recorder connection lost during shutdown: " + detail);
    }
}

void SessionController::updateShutdownDeadlines(qint64 now_ms) {
    bool any_overrun = false;
    for (ComponentShutdownStatus& component : shutdown_.components) {
        if (component.required && !component.stopped && component.deadline_ms >= 0 &&
            now_ms >= component.deadline_ms) {
            if (!component.deadline_exceeded) {
                component.deadline_exceeded = true;
                event_log_.append(component.component, EventSeverity::Warning,
                    "Shutdown deadline exceeded; the application remains responsive and is waiting safely");
            }
            any_overrun = true;
        }
    }
    if (shutdown_.complete()) {
        if (shutdown_.result != ShutdownResult::RecorderConnectionLost) {
            shutdown_.result = ShutdownResult::Completed;
        }
    } else if (any_overrun && shutdown_.result != ShutdownResult::RecorderConnectionLost) {
        shutdown_.result = ShutdownResult::DeadlineExceeded;
    }
}

QJsonObject SessionController::toJson(qint64 now_ms) const {
    QJsonArray streams;
    for (const StreamIdentity& identity : dashboard_.selected_streams) {
        streams.push_back(identity.toJson());
    }
    return QJsonObject{
        {"workflow", workflowText(dashboard_.workflow)},
        {"bridge", componentLifecycleStateText(dashboard_.bridge)},
        {"preview", componentLifecycleStateText(dashboard_.preview)},
        {"recorderConnection", recorderConnectionStateText(dashboard_.recorder_connection)},
        {"recorderRecording", recorderRecordingStateText(dashboard_.recorder_recording)},
        {"recorderOperation", recorderOperationStateText(dashboard_.recorder_operation)},
        {"recorderProcess", recorderProcessStateText(dashboard_.recorder_process)},
        {"calibration", calibrationStateText(dashboard_.calibration)},
        {"file", fileStateText(dashboard_.file)},
        {"verification", verificationStateText(dashboard_.verification)},
        {"recordingPath", dashboard_.recording_path},
        {"runIdentifier", dashboard_.run_identifier},
        {"recordingStartedAt", dashboard_.recording_started_at.toString(Qt::ISODateWithMs)},
        {"availableStorageBytes", dashboard_.available_storage_bytes},
        {"previewReplacedFrames", static_cast<qint64>(dashboard_.preview_replaced_frames)},
        {"previewCoalescedInputSamples",
         static_cast<qint64>(dashboard_.preview_coalesced_input_samples)},
        {"previewLatencyMs", dashboard_.preview_latency_ms},
        {"selectedStreams", streams},
        {"lastPreflight", last_preflight_.toJson()},
        {"shutdown", shutdown_.toJson(now_ms)},
        {"events", event_log_.toJson()},
        {"lastError", event_log_.lastError()},
    };
}

QString SessionController::workflowStateText(SessionWorkflowState state) {
    switch (state) {
        case SessionWorkflowState::Idle: return "Idle";
        case SessionWorkflowState::Preparing: return "Preparing";
        case SessionWorkflowState::PreflightBlocked: return "Preflight blocked";
        case SessionWorkflowState::Ready: return "Ready";
        case SessionWorkflowState::Starting: return "Starting";
        case SessionWorkflowState::Recording: return "Recording";
        case SessionWorkflowState::Stopping: return "Stopping";
        case SessionWorkflowState::Verifying: return "Verifying";
        case SessionWorkflowState::Complete: return "Complete";
        case SessionWorkflowState::Failed: return "Failed";
        case SessionWorkflowState::Closing: return "Closing";
    }
    return "Idle";
}

QString SessionController::calibrationStateText(SessionCalibrationState state) {
    switch (state) {
        case SessionCalibrationState::Manual: return "Manual";
        case SessionCalibrationState::Collecting: return "Collecting";
        case SessionCalibrationState::AutomaticSession: return "Automatic (session only)";
        case SessionCalibrationState::SavedProfile: return "Saved profile";
        case SessionCalibrationState::Failed: return "Failed";
    }
    return "Manual";
}

QString SessionController::fileStateText(SessionFileState state) {
    switch (state) {
        case SessionFileState::None: return "No recording loaded";
        case SessionFileState::Loading: return "Loading";
        case SessionFileState::Loaded: return "Loaded";
        case SessionFileState::Canceled: return "Canceled";
        case SessionFileState::Failed: return "Failed";
    }
    return "No recording loaded";
}

QString SessionController::shutdownResultText(ShutdownResult result) {
    switch (result) {
        case ShutdownResult::NotStarted: return "Not started";
        case ShutdownResult::InProgress: return "In progress";
        case ShutdownResult::Completed: return "Completed";
        case ShutdownResult::DeadlineExceeded: return "Deadline exceeded";
        case ShutdownResult::RecorderConnectionLost: return "Recorder connection lost";
    }
    return "Not started";
}

} // namespace vicon_lsl::gui
