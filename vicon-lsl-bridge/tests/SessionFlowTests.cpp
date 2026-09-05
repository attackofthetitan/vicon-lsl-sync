#include "gui/BridgeWindow.h"
#include "gui/PreviewPanel.h"
#include "gui/RecorderProcessController.h"
#include "gui/StreamDiscoveryWorker.h"
#include "TestSupport.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <memory>
#include <utility>

namespace {

using namespace vicon_lsl;
using namespace vicon_lsl::gui;

template<class Predicate>
bool waitUntil(Predicate ready, int timeout_ms = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeout_ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return ready();
}

std::shared_ptr<QSettings> sessionSettings(const QTemporaryDir& directory, bool recorder_only = false) {
    auto settings = std::make_shared<QSettings>(
        directory.filePath("settings.ini"), QSettings::IniFormat);
    SessionConfiguration configuration;
    configuration.recording_root = directory.path();
    configuration.vicon_endpoint = "127.0.0.1:1";
    configuration.recorder_host = "127.0.0.1";
    configuration.recorder_port = 1;
    configuration.recorder_automatic_launch = false;
    configuration.recorder_only_mode = recorder_only;
    // These workers can run without any lab streams being available.
    configuration.preview_external_streams = true;
    configuration.preview_markers.name = "session-flow-test-markers";
    configuration.preview_segments.name = "session-flow-test-segments";
    configuration.preview_gaze.name = "session-flow-test-gaze";
    configuration.preview_calibration.name = "session-flow-test-target";
    SessionConfigurationStore::save(*settings, configuration);
    return settings;
}

bool sessionStopped(const BridgeWindow& window) {
    const auto* log = window.findChild<QPlainTextEdit*>();
    return log && log->toPlainText().contains("Session stopped");
}

void writeRecording(const QString& path) {
    QFile csv(path);
    REQUIRE(csv.open(QIODevice::WriteOnly));
    REQUIRE(csv.write("relative_time,ViconMarkers_M:X,ViconMarkers_M:Y,"
                      "ViconMarkers_M:Z,ViconMarkers_M:Valid\n0,1000,0,0,1\n") > 0);
}

} // namespace

TEST_CASE("Stop Session stops the selected recorder during startup and while recording") {
    for (const bool during_startup : {false, true}) {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        BridgeWindow window(nullptr, false, sessionSettings(directory));
        auto* recorder = window.findChild<RecorderProcessController*>();
        REQUIRE(recorder);
        const QString fixture = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
            "vicon-lsl-recorder-process-fixture.exe"
#else
            "vicon-lsl-recorder-process-fixture"
#endif
        );
        StreamIdentity stream;
        stream.name = "SessionTest";
        stream.source_id = "session-flow-test";
        REQUIRE(recorder->launchSelectedStreamRecorder(
            fixture, directory.filePath("session-stop.xdf"), {stream}));
        const bool ready = during_startup || waitUntil([&] {
            return recorder->state() == RecorderProcessState::OwnedRunning;
        });
        QMetaObject::invokeMethod(&window, "onStopSession", Qt::DirectConnection);
        QMetaObject::invokeMethod(&window, "onStopSession", Qt::DirectConnection);
        const bool stopped = waitUntil([&] {
            return !recorder->ownsRunningProcess() && sessionStopped(window);
        });
        const bool stop_received = recorder->boundedOutput().contains("Stop received");
        // Clean up the child even if the behavior under test failed.
        recorder->endOwnedProcess();
        waitUntil([&] { return !recorder->ownsRunningProcess(); });
        window.close();
        REQUIRE(ready);
        REQUIRE(stopped);
        REQUIRE(stop_received);
    }
}

TEST_CASE("Session starts preview before recording and Stop Session shuts it down") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    BridgeWindow window(nullptr, true, sessionSettings(directory));
    auto* preview = window.findChild<PreviewPanel*>();
    REQUIRE(preview);
    QMetaObject::invokeMethod(&window, "onStatusUpdate", Qt::DirectConnection,
        Q_ARG(int, static_cast<int>(BridgeState::Streaming)), Q_ARG(unsigned long long, 1ULL),
        Q_ARG(unsigned long long, 1ULL), Q_ARG(unsigned int, 1U), Q_ARG(QString, QString()));
    QMetaObject::invokeMethod(&window, "onStartSession", Qt::DirectConnection);
    const bool preview_first = preview->lifecycleState() == ComponentLifecycleState::Starting &&
                               !window.findChild<StreamDiscoveryWorker*>();
    const bool finding_streams = waitUntil([&] {
        return window.findChild<StreamDiscoveryWorker*>() != nullptr;
    });
    QMetaObject::invokeMethod(&window, "onStopSession", Qt::DirectConnection);
    const bool stopped = waitUntil([&] { return preview->shutdownReady() && sessionStopped(window); });
    preview->requestShutdown();
    waitUntil([&] { return preview->shutdownReady(); });
    window.close();
    REQUIRE(preview_first);
    REQUIRE(finding_streams);
    REQUIRE(stopped);
}

TEST_CASE("Session waits for the bridge unless recorder-only mode is enabled, and can cancel either start") {
    for (const bool recorder_only : {false, true}) {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        BridgeWindow window(nullptr, false, sessionSettings(directory, recorder_only));
        QMetaObject::invokeMethod(&window, "onStartSession", Qt::DirectConnection);
        const bool bridge_started = window.findChild<BridgeWorker*>() != nullptr;
        const bool discovery_started = window.findChild<StreamDiscoveryWorker*>() != nullptr;
        QMetaObject::invokeMethod(&window, "onStopSession", Qt::DirectConnection);
        const bool canceled = waitUntil([&] { return sessionStopped(window); });
        window.close();
        waitUntil([&] {
            for (auto* thread : window.findChildren<QThread*>()) if (thread->isRunning()) return false;
            return true;
        });
        REQUIRE(bridge_started == !recorder_only);
        REQUIRE(discovery_started == recorder_only);
        REQUIRE(canceled);
    }
}

TEST_CASE("The bridge dashboard follows disconnect and reconnect updates") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    BridgeWindow window(nullptr, false, sessionSettings(directory));
    QLabel* status = nullptr;
    for (auto* label : window.findChildren<QLabel*>()) {
        if (label->accessibleName() == "Bridge status") status = label;
    }
    REQUIRE(status);
    const std::pair<BridgeState, QString> updates[] = {
        {BridgeState::Streaming, "Running"},
        {BridgeState::Disconnected, "Starting"},
        {BridgeState::Connecting, "Starting"},
        {BridgeState::Streaming, "Running"},
        {BridgeState::Stopped, "Stopped"},
    };
    for (const auto& update : updates) {
        REQUIRE(QMetaObject::invokeMethod(&window, "onStatusUpdate", Qt::DirectConnection,
            Q_ARG(int, static_cast<int>(update.first)), Q_ARG(unsigned long long, 1ULL),
            Q_ARG(unsigned long long, 1ULL), Q_ARG(unsigned int, 1U),
            Q_ARG(QString, QString())));
        REQUIRE_EQ(status->text(), update.second);
    }
    window.close();
}

TEST_CASE("Opening recordings stops live preview, and shutdown cancels a queued open") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath("preview.csv");
    writeRecording(path);
    PreviewPanel preview(nullptr, sessionSettings(directory));
    SessionFileState file_state = SessionFileState::None;
    bool loaded_while_live = false;
    QObject::connect(&preview, &PreviewPanel::fileStateChanged, &preview,
        [&](SessionFileState state, const QString&) {
            if (state == SessionFileState::Loading || state == SessionFileState::Loaded) {
                loaded_while_live |= preview.lifecycleState() != ComponentLifecycleState::Stopped;
                preview.startPreview();
                loaded_while_live |= preview.lifecycleState() != ComponentLifecycleState::Stopped;
            }
            file_state = state;
        });
    for (int route = 0; route < 4; ++route) {
        file_state = SessionFileState::None;
        loaded_while_live = false;
        preview.startPreview();
        REQUIRE(waitUntil([&] { return preview.lifecycleState() == ComponentLifecycleState::Running; }));
        if (route == 1) {
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(path)});
            QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&preview, &enter);
            QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&preview, &drop);
            REQUIRE(drop.isAccepted());
        } else if (route == 2) {
            REQUIRE(QMetaObject::invokeMethod(&preview, "openRecentRecording", Qt::DirectConnection));
        } else {
            preview.openRecording(path);
            if (route == 3) preview.requestShutdown();
        }
        REQUIRE(waitUntil([&] { return preview.shutdownReady(); }));
        REQUIRE_EQ(file_state, route == 3 ? SessionFileState::None : SessionFileState::Loaded);
        REQUIRE(!loaded_while_live);
    }
}
