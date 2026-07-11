#pragma once

#include <QtGlobal>
#include <QString>

#include "gui/LabRecorderClient.h"

class LabRecorderRuntimePolicy {
public:
    static constexpr qint64 RetryTimeoutMs = 15000;

    static QString resolveExecutable(const QString& configured_path,
                                     const QString& application_directory);
    static bool retryExpired(qint64 elapsed_ms);
    static bool shouldAttemptConnection(RecorderConnectionState state, qint64 elapsed_ms);
    static bool canLaunch(bool process_running);
    static bool shouldStopOwnedProcess(bool process_owned, bool process_running);
};
