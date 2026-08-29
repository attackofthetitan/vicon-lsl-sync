#pragma once

#include "gui/PreviewStreamWorker.h"
#include <functional>
#include <memory>

class QObject;
class QSettings;

namespace vicon_lsl::gui {

struct GuiServices {
    std::shared_ptr<QSettings> settings;
    std::function<PreviewStreamWorker*(PreviewWorkerConfig, QObject*)>
        create_preview_worker;
};

GuiServices defaultGuiServices();

} // namespace vicon_lsl::gui
