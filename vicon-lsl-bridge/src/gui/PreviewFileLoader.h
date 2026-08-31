#pragma once

#include "preview/PreviewCsv.h"

#include <QString>
#include <QThread>

#include <optional>

namespace vicon_lsl {

enum class PreviewFileType {
    Csv,
    Xdf,
};

class PreviewFileLoader final : public QThread {
    Q_OBJECT

public:
    PreviewFileLoader(PreviewFileType type,
                      QString path,
                      PreviewTransformProfile vicon_transform,
                      PreviewTransformProfile gaze_transform,
                      double match_tolerance_seconds,
                      QObject* parent = nullptr);

    std::optional<PreviewRecording> takeRecording();
    QString error() const { return error_; }
    QString path() const { return path_; }

protected:
    void run() override;

private:
    PreviewFileType type_;
    QString path_;
    PreviewTransformProfile vicon_transform_;
    PreviewTransformProfile gaze_transform_;
    double match_tolerance_seconds_;
    std::optional<PreviewRecording> recording_;
    QString error_;
};

} // namespace vicon_lsl
