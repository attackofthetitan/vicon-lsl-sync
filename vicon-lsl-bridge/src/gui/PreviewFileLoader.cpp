#include "gui/PreviewFileLoader.h"

#include "preview/PreviewXdf.h"

#include <exception>
#include <utility>

namespace vicon_lsl {

PreviewFileLoader::PreviewFileLoader(
    PreviewFileType type,
    QString path,
    PreviewTransformProfile vicon_transform,
    PreviewTransformProfile gaze_transform,
    double match_tolerance_seconds,
    QObject* parent)
    : QThread(parent),
      type_(type),
      path_(std::move(path)),
      vicon_transform_(std::move(vicon_transform)),
      gaze_transform_(std::move(gaze_transform)),
      match_tolerance_seconds_(match_tolerance_seconds) {}

std::optional<PreviewRecording> PreviewFileLoader::takeRecording() {
    return std::move(recording_);
}

void PreviewFileLoader::run() {
    try {
        const PreviewCancel cancel = [this]() {
            return isInterruptionRequested();
        };
        recording_ = type_ == PreviewFileType::Csv
            ? loadMergedPreviewCsv(path_.toStdString(),
                                   vicon_transform_,
                                   gaze_transform_,
                                   cancel)
            : loadXdfPreviewRecording(path_.toStdString(),
                                      vicon_transform_,
                                      gaze_transform_,
                                      match_tolerance_seconds_,
                                      cancel);
    } catch (const std::exception& exception) {
        error_ = QString::fromUtf8(exception.what());
    }
}

} // namespace vicon_lsl
