#include "gui/SetupCheckPolicy.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vicon_lsl::gui {

namespace {

// A stream that has never been measured passes, but one that has must have been
// updated within this many milliseconds.
constexpr qint64 kStreamFreshnessLimitMs = 2000;

QString rateText(double rate) {
    return rate > 0.0 ? QString::number(rate, 'f', 1) + " Hz" : QStringLiteral("an irregular rate");
}

} // namespace

bool requiredStreamReady(const StreamBinding& binding, const QVector<StreamIdentity>& inventory) {
    const auto found = std::find_if(inventory.cbegin(), inventory.cend(),
                                    [&binding](const StreamIdentity& stream) {
                                        return stream.present && binding.matches(stream);
                                    });
    if (found == inventory.cend()) {
        return false;
    }
    return (found->freshness_ms < 0 || found->freshness_ms <= kStreamFreshnessLimitMs) &&
           found->schema_compatible &&
           (binding.expected_channels <= 0 || found->channel_count == binding.expected_channels) &&
           (binding.expected_coordinate_frame.isEmpty() ||
            found->coordinate_frame == binding.expected_coordinate_frame);
}

SetupCheckResult runSetupCheck(const SetupCheckInputs& inputs,
                               const SessionConfiguration& configuration,
                               const RecordingPathResult& path,
                               const QVector<StreamIdentity>& inventory) {
    SetupCheckResult result;
    result.completed_at = QDateTime::currentDateTimeUtc();
    const auto add = [&result](SessionComponent component, SetupCheckLevel level, bool passed,
                               QString message, QString action = {}) {
        result.items.push_back({component, level, passed, std::move(message), std::move(action)});
    };

    if (!inputs.recorder_only) {
        add(SessionComponent::Bridge, SetupCheckLevel::Required,
            inputs.bridge_running_with_current_data,
            inputs.bridge_running_with_current_data ? "Vicon bridge is ready"
                                                    : "Vicon bridge is not sending current data",
            "Start the bridge and wait for data.");
    }

    const bool recorder_ready = inputs.record_every_visible_stream
                                    ? inputs.recorder_connected
                                    : inputs.selected_stream_recorder_available;
    const bool recorder_ok = recorder_ready && inputs.recorder_idle;
    add(SessionComponent::Recorder, SetupCheckLevel::Required, recorder_ok,
        recorder_ok ? "Recorder is ready" : "Recorder is not ready",
        "Connect the recorder and stop any current recording.");

    add(SessionComponent::Path, SetupCheckLevel::Required, path.valid(),
        path.valid() ? "Recording folder is ready" : path.firstError(),
        "Choose a writable recording folder.");
    for (const RecordingPathIssue& issue : path.issues) {
        if (issue.level == RecordingPathIssueLevel::Warning) {
            add(SessionComponent::Path, SetupCheckLevel::Warning, false, issue.message,
                issue.corrective_action);
        }
    }

    for (const StreamBinding& binding : configuration.recording_streams) {
        if (!binding.required) continue;
        const bool ready = requiredStreamReady(binding, inventory);
        const QString name = binding.role.isEmpty() ? binding.name : binding.role;
        add(SessionComponent::Streams, SetupCheckLevel::Required, ready,
            ready ? name + " stream is ready" : name + " stream is not ready",
            "Start the saved stream source or choose the current one.");
    }

    // Rates only warn, since a slow stream can still be recorded.
    for (const StreamBinding& binding : configuration.recording_streams) {
        const auto found = std::find_if(inventory.cbegin(), inventory.cend(),
                                        [&binding](const StreamIdentity& stream) {
                                            return stream.present && binding.matches(stream);
                                        });
        if (found == inventory.cend()) continue;
        const QString name = binding.role.isEmpty() ? binding.name : binding.role;
        const double expected = binding.expected_nominal_rate;
        if (expected > 0.0 && std::abs(found->nominal_rate - expected) > 0.01 * expected) {
            add(SessionComponent::Streams, SetupCheckLevel::Warning, false,
                name + " stream declares " + rateText(found->nominal_rate) + "; the settings expect " +
                    rateText(expected),
                "Check the source's frame rate, or choose the stream again to save its rate.");
        }
        if (found->nominal_rate > 0.0 && found->effective_rate > 0.0 &&
            found->effective_rate < 0.8 * found->nominal_rate) {
            add(SessionComponent::Streams, SetupCheckLevel::Warning, false,
                name + " stream arrives at " + rateText(found->effective_rate) + ", below 80% of its " +
                    rateText(found->nominal_rate),
                "Check the stream source and its network connection.");
        }
    }

    if (!inputs.record_every_visible_stream) {
        const bool selected = std::any_of(inventory.cbegin(), inventory.cend(),
                                          [](const StreamIdentity& stream) {
                                              return stream.present && stream.selected;
                                          });
        add(SessionComponent::Streams, SetupCheckLevel::Required, selected,
            selected ? "Recording streams are selected" : "No recording streams are selected",
            "Select at least one visible stream.");
    }

    if (inputs.calibration_required) {
        const bool ready = inputs.stair_model_loaded &&
                           (inputs.calibration == SessionCalibrationState::AutomaticSession ||
                            inputs.calibration == SessionCalibrationState::SavedProfile);
        add(SessionComponent::Calibration, SetupCheckLevel::Required, ready,
            ready ? "Calibration is ready" : "Calibration is not ready",
            "Load the stair model and apply a calibration.");
    }
    return result;
}

} // namespace vicon_lsl::gui
