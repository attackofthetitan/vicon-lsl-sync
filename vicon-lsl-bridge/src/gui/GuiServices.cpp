#include "gui/GuiServices.h"

#include <QSettings>

namespace vicon_lsl::gui {
GuiServices defaultGuiServices() {
    GuiServices result;
    result.settings = std::make_shared<QSettings>("ViconLSL", "ViconLSLBridge");
    result.create_preview_worker = [](PreviewWorkerConfig configuration,
                                      QObject* parent) {
        return new PreviewStreamWorker(std::move(configuration), parent);
    };
    return result;
}

} // namespace vicon_lsl::gui
