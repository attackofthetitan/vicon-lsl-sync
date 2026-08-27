#include "gui/PreviewFileLoader.h"

#include "gui/PerformanceBudgets.h"
#include "gui/SessionState.h"
#include "preview/PreviewXdf.h"

#include <QMutexLocker>

#include <algorithm>
#include <exception>

namespace vicon_lsl {

PreviewFileLoader::PreviewFileLoader(PreviewFileType type,
                                     QString path,
                                     PreviewTransformProfile vicon_transform,
                                     PreviewTransformProfile gaze_transform,
                                     double match_tolerance_seconds,
                                     PreviewLoadOptions options,
                                     QObject* parent)
    : QThread(parent),
      type_(type),
      path_(std::move(path)),
      vicon_transform_(std::move(vicon_transform)),
      gaze_transform_(std::move(gaze_transform)),
      match_tolerance_seconds_(match_tolerance_seconds),
      options_(std::move(options)) {
    qRegisterMetaType<XdfMappingAnalysis>("vicon_lsl::XdfMappingAnalysis");
    qRegisterMetaType<ComponentLifecycleState>("ComponentLifecycleState");
}

PreviewFileLoader::~PreviewFileLoader() {
    cancel();
    if (isRunning() && !wait(gui::PerformanceBudgets::PreviewStopDeadlineMs)) {
        terminate();
        wait(100);
    }
}

void PreviewFileLoader::cancel() {
    cancel_requested_.store(true);
    mapping_available_.wakeAll();
}

void PreviewFileLoader::provideMapping(const XdfStreamMapping& mapping) {
    QMutexLocker lock(&mutex_);
    mapping_ = mapping;
    have_mapping_ = true;
    mapping_available_.wakeAll();
}

std::optional<PreviewRecording> PreviewFileLoader::takeRecording() {
    QMutexLocker lock(&mutex_);
    std::optional<PreviewRecording> result = std::move(recording_);
    recording_.reset();
    return result;
}

XdfStreamMapping PreviewFileLoader::awaitMapping(const XdfMappingAnalysis& analysis) {
    emit mappingRequired(analysis);
    QMutexLocker lock(&mutex_);
    while (!have_mapping_ && !canceled()) {
        mapping_available_.wait(&mutex_, 100);
    }
    if (canceled()) {
        throw std::runtime_error("Preview load canceled");
    }
    return mapping_;
}

void PreviewFileLoader::run() {
    emit lifecycleChanged(ComponentLifecycleState::Starting, "Opening recording");
    try {
        PreviewLoadOptions options = options_;
        options.cancel_requested = [this]() { return canceled(); };
        options.progress = [this](const PreviewLoadProgress& progress) {
            const int percent = progress.total == 0
                ? 0
                : static_cast<int>((std::min)(100.0,
                    100.0 * static_cast<double>(progress.completed) /
                        static_cast<double>(progress.total)));
            emit progressChanged(QString::fromLatin1(previewLoadStageName(progress.stage)),
                                 percent,
                                 QString::fromStdString(progress.detail));
        };
        emit lifecycleChanged(ComponentLifecycleState::Running, "Loading recording");

        PreviewRecording loaded;
        if (type_ == PreviewFileType::Csv) {
            loaded = loadMergedPreviewCsv(path_.toStdString(),
                                          vicon_transform_, gaze_transform_, options);
        } else {
            XdfLoadResult xdf = loadXdfNumericStreams(path_.toStdString(), options);
            emit progressChanged("metadata", 100, "Inventoried all XDF streams");
            const XdfMappingAnalysis analysis = analyzeXdfStreamMapping(xdf);
            XdfStreamMapping mapping;
            if (analysis.requires_explicit_mapping) {
                mapping = awaitMapping(analysis);
            }
            emit progressChanged("calibration", 0,
                                 "Evaluating recorded stair-target calibration");
            emit progressChanged("frame preparation", 0,
                                 "Assembling the bounded playback cache");
            loaded = buildXdfPreviewRecording(xdf, vicon_transform_, gaze_transform_,
                                              match_tolerance_seconds_, mapping, options);
            emit progressChanged("calibration", 100,
                                 "Recorded calibration evaluated");
            emit progressChanged("frame preparation", 100,
                                 "Playback cache ready");
        }
        if (canceled()) {
            throw std::runtime_error("Preview load canceled");
        }
        const QString summary = QString::fromStdString(loaded.summary);
        {
            QMutexLocker lock(&mutex_);
            recording_ = std::move(loaded);
        }
        emit loadSucceeded(summary);
        emit progressChanged("complete", 100, summary);
        emit lifecycleChanged(ComponentLifecycleState::Stopped, "Recording load complete");
    } catch (const std::exception& ex) {
        const bool was_canceled = canceled() ||
            QString::fromUtf8(ex.what()).contains("canceled", Qt::CaseInsensitive);
        emit loadFailed(QString::fromUtf8(ex.what()), was_canceled);
        emit lifecycleChanged(was_canceled ? ComponentLifecycleState::Stopped
                                           : ComponentLifecycleState::Failed,
                              QString::fromUtf8(ex.what()));
    } catch (...) {
        emit loadFailed("Unknown recording-load failure", false);
        emit lifecycleChanged(ComponentLifecycleState::Failed,
                              "Unknown recording-load failure");
    }
}

} // namespace vicon_lsl
