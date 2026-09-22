#include "preview/PreviewLoad.h"

#include <algorithm>
#include <stdexcept>

namespace vicon_lsl {

const char* previewLoadStageName(PreviewLoadStage stage) {
    switch (stage) {
        case PreviewLoadStage::Reading: return "reading";
        case PreviewLoadStage::Indexing: return "indexing";
        case PreviewLoadStage::StreamDetails: return "stream details";
        case PreviewLoadStage::Timestamps: return "timestamps";
        case PreviewLoadStage::Calibration: return "calibration";
        case PreviewLoadStage::FramePreparation: return "frame preparation";
        case PreviewLoadStage::Complete: return "complete";
    }
    return "reading";
}

void reportPreviewLoadProgress(const PreviewLoadOptions& options,
                               PreviewLoadStage stage,
                               std::uint64_t completed,
                               std::uint64_t total,
                               const std::string& detail) {
    if (options.cancel_requested && options.cancel_requested()) {
        throw std::runtime_error("Preview load canceled");
    }
    if (options.progress) {
        options.progress({stage, completed, total, detail});
    }
}

int previewLoadPercent(const PreviewLoadProgress& progress) {
    if (progress.total == 0) return 0;
    return static_cast<int>((std::min)(
        100.0, 100.0 * static_cast<double>(progress.completed) /
                   static_cast<double>(progress.total)));
}

} // namespace vicon_lsl
