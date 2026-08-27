#pragma once

#include "preview/PreviewCsv.h"
#include "preview/PreviewXdfMapping.h"
#include "gui/SessionState.h"

#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <optional>

namespace vicon_lsl {

enum class PreviewFileType {
    Csv,
    Xdf,
};

class PreviewFileLoader : public QThread {
    Q_OBJECT

public:
    PreviewFileLoader(PreviewFileType type,
                      QString path,
                      PreviewTransformProfile vicon_transform,
                      PreviewTransformProfile gaze_transform,
                      double match_tolerance_seconds,
                      PreviewLoadOptions options,
                      QObject* parent = nullptr);
    ~PreviewFileLoader() override;

    void cancel();
    void provideMapping(const XdfStreamMapping& mapping);
    std::optional<PreviewRecording> takeRecording();
    QString path() const { return path_; }

signals:
    void progressChanged(QString stage, int percent, QString detail);
    void mappingRequired(vicon_lsl::XdfMappingAnalysis analysis);
    void loadSucceeded(QString summary);
    void loadFailed(QString error, bool canceled);
    void lifecycleChanged(ComponentLifecycleState state, QString detail);

protected:
    void run() override;

private:
    bool canceled() const { return cancel_requested_.load(); }
    XdfStreamMapping awaitMapping(const XdfMappingAnalysis& analysis);

    PreviewFileType type_;
    QString path_;
    PreviewTransformProfile vicon_transform_;
    PreviewTransformProfile gaze_transform_;
    double match_tolerance_seconds_ = 0.05;
    PreviewLoadOptions options_;
    std::atomic<bool> cancel_requested_{false};
    QMutex mutex_;
    QWaitCondition mapping_available_;
    bool have_mapping_ = false;
    XdfStreamMapping mapping_;
    std::optional<PreviewRecording> recording_;
};

} // namespace vicon_lsl

Q_DECLARE_METATYPE(vicon_lsl::XdfMappingAnalysis)
