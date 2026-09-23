#pragma once

#include "gui/SessionConfiguration.h"
#include "gui/SessionState.h"

#include <QThread>

namespace vicon_lsl {

class StreamDiscoveryWorker : public QThread {
    Q_OBJECT

public:
    explicit StreamDiscoveryWorker(gui::SessionConfiguration configuration = {},
                                   QObject* parent = nullptr);
    ~StreamDiscoveryWorker() override;

signals:
    void discoveryFinished(QVector<vicon_lsl::gui::StreamIdentity> streams,
                           QString warning);
    void lifecycleChanged(ComponentLifecycleState state, QString detail);

protected:
    void run() override;

private:
    gui::SessionConfiguration configuration_;
};

} // namespace vicon_lsl
