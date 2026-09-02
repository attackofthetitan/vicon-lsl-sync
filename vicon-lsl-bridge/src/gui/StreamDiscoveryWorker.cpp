#include "gui/StreamDiscoveryWorker.h"

#include "StreamDefaults.h"
#include "gui/LslStreamIdentity.h"

#include <lsl_cpp.h>

#include <QDateTime>
#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <exception>
#include <tuple>
#include <utility>

namespace vicon_lsl {
namespace {

QString inferRole(const lsl::stream_info& stream,
                  const gui::SessionConfiguration& configuration) {
    const QString name = QString::fromStdString(stream.name());
    const QString type = QString::fromStdString(stream.type());
    if (name == configuration.marker_output_name ||
        name == configuration.preview_markers.name) return "markers";
    if (name == configuration.segment_output_name ||
        name == configuration.preview_segments.name) return "segments";
    if (name == configuration.preview_gaze.name ||
        name == stream_defaults::HoloLensGaze ||
        type.compare("Gaze", Qt::CaseInsensitive) == 0) return "gaze";
    if (name == configuration.preview_calibration.name ||
        name == stream_defaults::HoloLensModelTargetPose ||
        type.compare("Calibration", Qt::CaseInsensitive) == 0) return "calibration";
    return "other";
}

bool expectedSchema(const gui::StreamIdentity& identity) {
    if (identity.role == "gaze") return identity.channel_count == 21;
    if (identity.role == "calibration") return identity.channel_count == 8;
    if (identity.role == "markers") return identity.channel_count >= 0 && identity.channel_count % 4 == 0;
    if (identity.role == "segments") return identity.channel_count >= 0 && identity.channel_count % 7 == 0;
    return true;
}

} // namespace

StreamDiscoveryWorker::StreamDiscoveryWorker(gui::SessionConfiguration configuration,
                                             QObject* parent)
    : QThread(parent), configuration_(std::move(configuration)) {
    qRegisterMetaType<QVector<gui::StreamIdentity>>("QVector<vicon_lsl::gui::StreamIdentity>");
    qRegisterMetaType<ComponentLifecycleState>("ComponentLifecycleState");
}

StreamDiscoveryWorker::~StreamDiscoveryWorker() {
    requestInterruption();
    wait();
}

void StreamDiscoveryWorker::run() {
    emit lifecycleChanged(ComponentLifecycleState::Starting, "Discovering visible LSL streams");
    try {
        std::vector<lsl::stream_info> resolved = lsl::resolve_streams(0.5);
        std::stable_sort(resolved.begin(), resolved.end(),
            [](const lsl::stream_info& left, const lsl::stream_info& right) {
                return std::make_tuple(left.name(), left.source_id(), left.hostname(), left.uid()) <
                       std::make_tuple(right.name(), right.source_id(), right.hostname(), right.uid());
            });
        QVector<gui::StreamIdentity> result;
        QStringList warnings;
        for (lsl::stream_info& stream : resolved) {
            if (isInterruptionRequested()) {
                emit lifecycleChanged(ComponentLifecycleState::Stopped, "Stream discovery canceled");
                return;
            }
            gui::StreamIdentity identity = gui::identityFromStreamInfo(stream);
            identity.role = inferRole(stream, configuration_);
            identity.metadata_complete = gui::identityDescribesItself(
                identity, identity.role == "gaze" || identity.role == "calibration");
            identity.schema_compatible = expectedSchema(identity);
            identity.present = true;
            identity.freshness_ms = -1;
            QStringList identity_warnings;
            if (!identity.metadata_complete) {
                identity_warnings.push_back(
                    identity.name + " is missing source or channel details");
            }
            if (!identity.schema_compatible) {
                identity_warnings.push_back(
                    identity.name + " has an unexpected channel layout");
            }
            identity.warning = identity_warnings.join("; ");
            warnings.append(identity_warnings);
            result.push_back(std::move(identity));
        }
        QHash<QString, int> streams_per_name;
        for (const gui::StreamIdentity& identity : result) ++streams_per_name[identity.name];
        // Each affected stream carries its own warning, but the summary names a
        // duplicated stream once rather than once per copy of it. Walking the
        // sorted results keeps that summary in a stable order.
        QStringList reported_duplicates;
        for (gui::StreamIdentity& identity : result) {
            const int duplicates = streams_per_name.value(identity.name);
            if (duplicates <= 1) continue;
            const QString message =
                "Several streams share this name; choose one by source ID or select Follow by name";
            identity.warning = identity.warning.isEmpty() ? message
                                                          : identity.warning + "; " + message;
            if (!reported_duplicates.contains(identity.name)) {
                reported_duplicates.push_back(identity.name);
                warnings.push_back(identity.name + " has " + QString::number(duplicates) +
                                   " visible sources");
            }
        }
        emit discoveryFinished(result, warnings.join("; "));
        emit lifecycleChanged(ComponentLifecycleState::Stopped,
                              "Discovered " + QString::number(result.size()) + " stream(s)");
    } catch (const std::exception& ex) {
        emit discoveryFinished({}, QString::fromUtf8(ex.what()));
        emit lifecycleChanged(ComponentLifecycleState::Failed, QString::fromUtf8(ex.what()));
    }
}

} // namespace vicon_lsl
