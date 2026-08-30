#include "preview/PreviewLoad.h"

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

} // namespace vicon_lsl
