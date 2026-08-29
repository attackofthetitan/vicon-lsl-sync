#include "gui/StreamDiscoveryWorker.h"

#include "StreamDefaults.h"

#include <lsl_cpp.h>

#include <QDateTime>
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
            gui::StreamIdentity identity;
            identity.role = inferRole(stream, configuration_);
            identity.name = QString::fromStdString(stream.name());
            identity.type = QString::fromStdString(stream.type());
            identity.source_id = QString::fromStdString(stream.source_id());
            identity.hostname = QString::fromStdString(stream.hostname());
            identity.session_id = QString::fromStdString(stream.session_id());
            identity.uid = QString::fromStdString(stream.uid());
            identity.publisher_created_at = stream.created_at();
            identity.channel_count = stream.channel_count();
            identity.nominal_rate = std::isfinite(stream.nominal_srate())
                ? stream.nominal_srate() : 0.0;
            const char* coordinate_frame = stream.desc()
                .child("acquisition")
                .child_value("coordinate_frame");
            identity.coordinate_frame = coordinate_frame
                ? QString::fromUtf8(coordinate_frame) : QString();
            identity.metadata_complete = !identity.source_id.isEmpty() &&
                                         identity.channel_count > 0 &&
                                         (identity.role != "gaze" && identity.role != "calibration" ||
                                          !identity.coordinate_frame.isEmpty());
            identity.schema_compatible = expectedSchema(identity);
            identity.present = true;
            identity.freshness_ms = -1;
            identity.discovered_at = QDateTime::currentDateTimeUtc();
            QStringList identity_warnings;
            if (!identity.metadata_complete) {
                identity_warnings.push_back(
                    identity.name + " has incomplete identity or channel metadata");
            }
            if (!identity.schema_compatible) {
                identity_warnings.push_back(
                    identity.name + " has an incompatible channel schema");
            }
            identity_warnings.push_back(
                identity.name + " sample freshness has not yet been measured");
            identity.warning = identity_warnings.join("; ");
            warnings.append(identity_warnings);
            result.push_back(std::move(identity));
        }
        for (int left = 0; left < result.size(); ++left) {
            int duplicate_count = 0;
            for (const gui::StreamIdentity& candidate : result) {
                if (candidate.name == result[left].name) ++duplicate_count;
            }
            if (duplicate_count > 1) {
                result[left].warning = "Duplicate stream name; bind by source ID or enable Follow by name";
                warnings.push_back(result[left].name + " has " +
                                   QString::number(duplicate_count) + " visible identities");
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
