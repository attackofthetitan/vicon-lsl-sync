#include "gui/RecordingVerifier.h"

#include "preview/PreviewLoad.h"
#include "preview/PreviewXdfReader.h"

#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>

namespace vicon_lsl::gui {
namespace {

void addFinding(RecordingVerificationReport& report,
                EventSeverity severity,
                QString id,
                QString stream,
                QString message,
                QString action = {}) {
    report.findings.push_back({severity, std::move(id), std::move(stream),
                               std::move(message), std::move(action)});
}

bool streamMatches(const XdfStreamData& recorded,
                   const StreamBinding& expected) {
    if (!expected.source_id.trimmed().isEmpty() &&
        expected.reconnection == StreamReconnectionMode::SourceIdentity) {
        return QString::fromStdString(recorded.source_id) == expected.source_id;
    }
    return QString::fromStdString(recorded.name) == expected.name;
}

bool streamMatchesInventory(const XdfStreamData& recorded,
                            const StreamIdentity& expected) {
    if (!expected.source_id.trimmed().isEmpty()) {
        return QString::fromStdString(recorded.source_id) == expected.source_id;
    }
    return QString::fromStdString(recorded.name) == expected.name &&
           (expected.hostname.isEmpty() ||
            QString::fromStdString(recorded.hostname) == expected.hostname);
}

QString streamLabel(const XdfStreamData& stream) {
    QString result = QString::fromStdString(stream.name);
    if (!stream.source_id.empty()) result += " [" + QString::fromStdString(stream.source_id) + "]";
    return result;
}

} // namespace

QJsonObject RecordingVerificationFinding::toJson() const {
    return {
        {"severity", SessionEventLog::severityText(severity)},
        {"id", id},
        {"stream", stream},
        {"message", message},
        {"correctiveAction", corrective_action},
    };
}

QJsonObject RecordedStreamVerification::toJson() const {
    return {
        {"name", name}, {"type", type}, {"sourceId", source_id},
        {"hostname", hostname}, {"sessionId", session_id},
        {"coordinateFrame", coordinate_frame}, {"channelCount", channel_count},
        {"nominalRate", nominal_rate}, {"effectiveRate", effective_rate},
        {"sampleCount", sample_count}, {"startTime", start_time},
        {"endTime", end_time}, {"maximumGap", maximum_gap},
        {"largeGapCount", large_gap_count},
        {"clockCorrectionCount", clock_correction_count},
        {"repairedTimestampCount", repaired_timestamp_count},
    };
}

bool RecordingVerificationReport::hasErrors() const {
    return std::any_of(findings.begin(), findings.end(), [](const auto& finding) {
        return finding.severity == EventSeverity::Error;
    });
}

bool RecordingVerificationReport::hasWarnings() const {
    return std::any_of(findings.begin(), findings.end(), [](const auto& finding) {
        return finding.severity == EventSeverity::Warning;
    });
}

QString RecordingVerificationReport::summary() const {
    int errors = 0;
    int warnings = 0;
    for (const RecordingVerificationFinding& finding : findings) {
        if (finding.severity == EventSeverity::Error) ++errors;
        if (finding.severity == EventSeverity::Warning) ++warnings;
    }
    return verificationStateText(state) + ": " + QString::number(streams.size()) +
           " stream(s), " + QString::number(errors) + " error(s), " +
           QString::number(warnings) + " warning(s)";
}

QJsonObject RecordingVerificationReport::toJson() const {
    QJsonArray serialized_streams;
    for (const RecordedStreamVerification& stream : streams) {
        serialized_streams.push_back(stream.toJson());
    }
    QJsonArray serialized_findings;
    for (const RecordingVerificationFinding& finding : findings) {
        serialized_findings.push_back(finding.toJson());
    }
    return {
        {"state", verificationStateText(state)},
        {"path", path},
        {"startedAt", started_at.toString(Qt::ISODateWithMs)},
        {"completedAt", completed_at.toString(Qt::ISODateWithMs)},
        {"durationSeconds", duration_seconds},
        {"truncatedTailRecovered", truncated_tail_recovered},
        {"fileSizeBytes", file_size_bytes},
        {"streams", serialized_streams},
        {"findings", serialized_findings},
    };
}

RecordingVerifier::RecordingVerifier(RecordingVerificationRequest request,
                                     QObject* parent)
    : QThread(parent), request_(std::move(request)) {
    qRegisterMetaType<RecordingVerificationReport>(
        "vicon_lsl::gui::RecordingVerificationReport");
    qRegisterMetaType<ComponentLifecycleState>("ComponentLifecycleState");
}

void RecordingVerifier::cancel() {
    cancel_requested_.store(true);
    requestInterruption();
}

void RecordingVerifier::run() {
    const auto now_utc = [this]() {
        return request_.now_utc ? request_.now_utc()
                                : QDateTime::currentDateTimeUtc();
    };
    RecordingVerificationReport report;
    report.path = request_.path;
    report.started_at = now_utc();
    report.state = RecordingVerificationState::Running;
    emit lifecycleChanged(ComponentLifecycleState::Starting,
                          "Opening recorded XDF for verification");
    try {
        PreviewLoadOptions options;
        options.maximum_file_bytes = PerformanceBudgets::MaximumXdfFileBytes;
        options.maximum_samples_per_stream = PerformanceBudgets::MaximumSamplesPerXdfStream;
        options.maximum_stored_values_per_stream = 20000;
        options.maximum_preview_frames = 1;
        options.maximum_streams = PerformanceBudgets::MaximumXdfStreams;
        options.maximum_channels = PerformanceBudgets::MaximumXdfChannels;
        options.maximum_header_bytes = PerformanceBudgets::MaximumHeaderBytes;
        options.cancellation_check_sample_interval =
            PerformanceBudgets::FileCancelSampleInterval;
        options.cancel_requested = [this]() {
            return cancel_requested_.load() || isInterruptionRequested();
        };
        options.progress = [this](const PreviewLoadProgress& progress) {
            const int percent = progress.total == 0 ? 0 : static_cast<int>(
                (std::min)(100.0, 100.0 * static_cast<double>(progress.completed) /
                                         static_cast<double>(progress.total)));
            emit progressChanged(QString::fromLatin1(previewLoadStageName(progress.stage)),
                                 percent, QString::fromStdString(progress.detail));
        };
        emit lifecycleChanged(ComponentLifecycleState::Running,
                              "Verifying stream headers and timestamps");
        const XdfLoadResult loaded = loadXdfNumericStreams(request_.path.toStdString(), options);
        if (cancel_requested_.load() || isInterruptionRequested()) {
            throw std::runtime_error("Recording verification canceled");
        }

        report.file_size_bytes = static_cast<qint64>(loaded.file_size_bytes);
        report.truncated_tail_recovered = loaded.truncated_tail_ignored;
        if (loaded.truncated_tail_ignored) {
            addFinding(report, EventSeverity::Warning, "truncated-tail", {},
                       "An interrupted final XDF chunk was ignored; complete earlier chunks remain readable",
                       "Review recorder shutdown and storage health.");
        }
        double earliest = std::numeric_limits<double>::infinity();
        double latest = -std::numeric_limits<double>::infinity();
        for (const XdfStreamData& stream : loaded.streams) {
            RecordedStreamVerification item;
            item.name = QString::fromStdString(stream.name);
            item.type = QString::fromStdString(stream.type);
            item.source_id = QString::fromStdString(stream.source_id);
            item.hostname = QString::fromStdString(stream.hostname);
            item.session_id = QString::fromStdString(stream.session_id);
            item.coordinate_frame = QString::fromStdString(stream.coordinate_frame);
            item.channel_count = stream.channel_count;
            item.nominal_rate = stream.nominal_srate;
            item.sample_count = static_cast<qint64>(stream.sample_count);
            item.start_time = stream.start_timestamp;
            item.end_time = stream.end_timestamp;
            item.maximum_gap = stream.maximum_sample_gap;
            item.large_gap_count = static_cast<qint64>(stream.large_gap_count);
            item.clock_correction_count = static_cast<qint64>(stream.clock_offsets.size());
            item.repaired_timestamp_count =
                static_cast<qint64>(stream.repaired_timestamp_count);
            const double duration = stream.end_timestamp - stream.start_timestamp;
            if (duration > 0.0 && stream.sample_count > 1) {
                item.effective_rate = static_cast<double>(stream.sample_count - 1) / duration;
            }
            report.streams.push_back(item);
            if (stream.sample_count > 0) {
                earliest = (std::min)(earliest, stream.start_timestamp);
                latest = (std::max)(latest, stream.end_timestamp);
            }
            const QString label = streamLabel(stream);
            if (stream.sample_count == 0) {
                addFinding(report, EventSeverity::Error, "empty-stream", label,
                           label + " contains no samples",
                           "Confirm the publisher was producing data during the run.");
            }
            if (stream.repaired_timestamp_count > 0) {
                addFinding(report, EventSeverity::Warning, "timestamp-repair", label,
                           label + " required " +
                               QString::number(stream.repaired_timestamp_count) +
                               " timestamp repair(s)",
                           "Inspect the source clock and XDF footer diagnostics.");
            }
            if (stream.large_gap_count > 0) {
                addFinding(report, EventSeverity::Warning, "sample-gaps", label,
                           label + " contains " + QString::number(stream.large_gap_count) +
                               " large sample gap(s); maximum " +
                               QString::number(stream.maximum_sample_gap, 'f', 3) + " s",
                           "Check source connectivity and publisher health.");
            }
            if (stream.nominal_srate > 0.0 && item.effective_rate > 0.0 &&
                item.effective_rate < stream.nominal_srate * 0.8) {
                addFinding(report, EventSeverity::Warning, "effective-rate", label,
                           label + " effective rate " +
                               QString::number(item.effective_rate, 'f', 1) +
                               " Hz is below nominal " +
                               QString::number(stream.nominal_srate, 'f', 1) + " Hz",
                           "Compare with the preflight rate and source drop counters.");
            }
        }
        if (std::isfinite(earliest) && std::isfinite(latest) && latest >= earliest) {
            report.duration_seconds = latest - earliest;
        }

        for (const StreamBinding& expected : request_.expected_streams) {
            if (!expected.required) continue;
            const auto found = std::find_if(loaded.streams.begin(), loaded.streams.end(),
                [&expected](const XdfStreamData& stream) {
                    return streamMatches(stream, expected);
                });
            if (found == loaded.streams.end()) {
                addFinding(report, EventSeverity::Error, "missing-required-stream",
                           expected.name,
                           "Required stream " + expected.name + " is absent from the XDF",
                           "Review the final stream list and recorder selection.");
                continue;
            }
            if (expected.expected_channels > 0 &&
                found->channel_count != expected.expected_channels) {
                addFinding(report, EventSeverity::Error, "channel-mismatch",
                           expected.name,
                           expected.name + " recorded " +
                               QString::number(found->channel_count) + " channels; expected " +
                               QString::number(expected.expected_channels),
                           "Use the compatible publisher schema.");
            }
            if (!expected.expected_coordinate_frame.isEmpty() &&
                QString::fromStdString(found->coordinate_frame) !=
                    expected.expected_coordinate_frame) {
                addFinding(report, EventSeverity::Error, "coordinate-frame-mismatch",
                           expected.name,
                           expected.name + " coordinate frame does not match the session preset",
                           "Select a compatible calibration and publisher.");
            }
        }

        for (const StreamIdentity& expected : request_.preflight_inventory) {
            if (!expected.selected) continue;
            const bool found = std::any_of(loaded.streams.begin(), loaded.streams.end(),
                [&expected](const XdfStreamData& stream) {
                    return streamMatchesInventory(stream, expected);
                });
            if (!found) {
                addFinding(report, expected.required ? EventSeverity::Error
                                                     : EventSeverity::Warning,
                           "preflight-selection-missing", expected.name,
                           "A stream selected at preflight is absent from the recording: " +
                               expected.displayText(),
                           "Compare stream recovery identity and recorder selection.");
            }
        }

        if (loaded.streams.empty()) {
            addFinding(report, EventSeverity::Error, "no-streams", {},
                       "The XDF contains no readable numeric streams",
                       "Retain the file and inspect it with an independent XDF tool.");
        }
        report.completed_at = now_utc();
        report.state = report.hasErrors()
            ? RecordingVerificationState::NeedsAttention
            : (report.hasWarnings() ? RecordingVerificationState::VerifiedWithWarnings
                                    : RecordingVerificationState::Verified);
        emit verificationFinished(report);
        emit lifecycleChanged(ComponentLifecycleState::Stopped, report.summary());
    } catch (const std::exception& ex) {
        report.completed_at = now_utc();
        report.state = RecordingVerificationState::NeedsAttention;
        addFinding(report, EventSeverity::Error, "verification-failure", {},
                   QString::fromUtf8(ex.what()),
                   "Keep the recording unchanged and inspect the detailed error.");
        emit verificationFinished(report);
        emit lifecycleChanged(cancel_requested_.load() ? ComponentLifecycleState::Stopped
                                                       : ComponentLifecycleState::Failed,
                              QString::fromUtf8(ex.what()));
    } catch (...) {
        report.completed_at = now_utc();
        report.state = RecordingVerificationState::NeedsAttention;
        addFinding(report, EventSeverity::Error, "verification-failure", {},
                   "Unknown recording verification failure");
        emit verificationFinished(report);
        emit lifecycleChanged(ComponentLifecycleState::Failed,
                              "Unknown recording verification failure");
    }
}

} // namespace vicon_lsl::gui
