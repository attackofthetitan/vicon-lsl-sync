#include "gui/BridgeWindow.h"
#include "gui/LabRecorderClient.h"
#include "gui/PreviewPanel.h"
#include "gui/RecorderProcessController.h"
#include "gui/StreamDiscoveryWorker.h"
#include "TestSupport.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <iostream>
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

QString bridgeStateText(const BridgeWindow& window) {
    for (const auto* label : window.findChildren<QLabel*>()) {
        if (label->accessibleName() == "Detailed bridge state") return label->text();
    }
    return {};
}

bool eventLogContains(const BridgeWindow& window, const QString& text) {
    const auto* log = window.findChild<QPlainTextEdit*>();
    return log && log->toPlainText().contains(text);
}

bool sessionStopped(const BridgeWindow& window) {
    return eventLogContains(window, "Session stopped");
}

// Stands in for LabRecorder and answers each command only when told to, so a
// test can hold a group of commands part way through.
class FakeLabRecorder {
public:
    FakeLabRecorder() { REQUIRE(server_.listen(QHostAddress::LocalHost, 0)); }

    quint16 port() const { return server_.serverPort(); }

    // Takes the next connection the app makes.
    bool accept() {
        if (!waitUntil([this] { return server_.hasPendingConnections(); })) return false;
        socket_.reset(server_.nextPendingConnection());
        return socket_ != nullptr;
    }

    QString nextCommand() {
        if (!hasCommand(3000)) return {};
        return QString::fromUtf8(socket_->readLine()).trimmed();
    }

    bool hasCommand(int timeout_ms) {
        return waitUntil([this] { return socket_ && socket_->canReadLine(); }, timeout_ms);
    }

    void reply() {
        socket_->write("OK");
        socket_->flush();
    }

    void drop() { socket_->abort(); }

private:
    QTcpServer server_;
    std::unique_ptr<QTcpSocket> socket_;
};

// Records with the LabRecorder window, which the app drives over its remote port.
std::shared_ptr<QSettings> remoteRecorderSettings(const QTemporaryDir& directory, quint16 port) {
    auto settings = sessionSettings(directory, true);
    SessionConfiguration configuration = SessionConfigurationStore::load(*settings);
    configuration.recorder_port = port;
    configuration.record_every_visible_stream = true;
    configuration.recording_template = "run-%r.xdf";
    SessionConfigurationStore::save(*settings, configuration);
    return settings;
}

// The test computer has no lab streams, so the setup check fails and the
// recording is started with Record Anyway.
bool startRecordingAnyway(BridgeWindow& window) {
    QMetaObject::invokeMethod(&window, "onStartRecording", Qt::DirectConnection);
    QLineEdit* reason = nullptr;
    for (auto* edit : window.findChildren<QLineEdit*>()) {
        if (edit->placeholderText().startsWith("Required reason")) reason = edit;
    }
    if (!reason || !waitUntil([&] { return !window.findChild<StreamDiscoveryWorker*>(); })) {
        return false;
    }
    reason->setText("No lab streams on the test computer");
    return QMetaObject::invokeMethod(&window, "onOverrideSetupCheck", Qt::DirectConnection);
}

// A measured calibration for its own stair setup.
ManagedCalibrationProfile measuredCalibration(const QString& id, double gaze_x) {
    ManagedCalibrationProfile profile = CalibrationProfileStore::defaultProfile();
    profile.id = id;
    profile.display_name = id;
    profile.physical_setup_id = id;
    profile.gaze_transform.translation = {gaze_x, 0.0, 0.0};
    profile.quality.sample_count = 30;
    profile.metadata_fallback_confirmed = true;
    return profile;
}

QComboBox* calibrationList(PreviewPanel& panel) {
    for (auto* combo : panel.findChildren<QComboBox*>()) {
        if (combo->findText("Default stair setup") >= 0) return combo;
    }
    return nullptr;
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
        // Clean up the child process even if the behavior under test failed.
        recorder->endOwnedProcess();
        waitUntil([&] { return !recorder->ownsRunningProcess(); });
        window.close();
        REQUIRE(ready);
        REQUIRE(stopped);
        REQUIRE(stop_received);
    }
}

TEST_CASE("Stop Session during a LabRecorder Start waits for it and then sends one Stop") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    FakeLabRecorder recorder;
    BridgeWindow window(nullptr, false, remoteRecorderSettings(directory, recorder.port()));
    REQUIRE(recorder.accept());
    REQUIRE(startRecordingAnyway(window));
    // Hold the Start at its first command, as LabRecorder does while it looks
    // for streams, and ask the session to stop meanwhile.
    REQUIRE_EQ(recorder.nextCommand(), QString("update"));
    QMetaObject::invokeMethod(&window, "onStopSession", Qt::DirectConnection);
    const bool stop_held_back = !recorder.hasCommand(100);
    recorder.reply();
    QStringList commands;
    for (int command = 0; command < 3; ++command) {
        commands.push_back(recorder.nextCommand());
        recorder.reply();
    }
    const QString stop = recorder.nextCommand();
    recorder.reply();
    const bool one_stop = !recorder.hasCommand(200);
    window.close();
    REQUIRE(stop_held_back);
    REQUIRE_EQ(commands.value(2), QString("start"));
    REQUIRE_EQ(stop, QString("stop"));
    REQUIRE(one_stop);
}

TEST_CASE("Stop Session waits for a lost recorder to be reconnected and then stops it") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    FakeLabRecorder recorder;
    BridgeWindow window(nullptr, false, remoteRecorderSettings(directory, recorder.port()));
    auto* client = window.findChild<LabRecorderClient*>();
    REQUIRE(client);
    REQUIRE(recorder.accept());
    REQUIRE(startRecordingAnyway(window));
    for (int command = 0; command < 4; ++command) {
        REQUIRE(!recorder.nextCommand().isEmpty());
        recorder.reply();
    }
    REQUIRE(waitUntil([&] { return client->recordingState() == RecorderRecordingState::Recording; }));
    recorder.drop();
    REQUIRE(waitUntil([&] { return !client->isConnected(); }));

    QMetaObject::invokeMethod(&window, "onStopSession", Qt::DirectConnection);
    const bool waiting = eventLogContains(window, "Select Connect to reconnect");
    QMetaObject::invokeMethod(&window, "onConnectLabRecorder", Qt::DirectConnection);
    const bool reconnected = recorder.accept();
    const QString stop = reconnected ? recorder.nextCommand() : QString();
    if (reconnected) recorder.reply();
    const bool stopped = waitUntil([&] {
        return client->recordingState() == RecorderRecordingState::Stopped;
    });
    window.close();
    REQUIRE(waiting);
    REQUIRE(reconnected);
    REQUIRE_EQ(stop, QString("stop"));
    REQUIRE(stopped);
}

TEST_CASE("Closing while recording stops the recorder without waiting to check the file") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    FakeLabRecorder recorder;
    BridgeWindow window(nullptr, false, remoteRecorderSettings(directory, recorder.port()));
    auto* client = window.findChild<LabRecorderClient*>();
    REQUIRE(client);
    window.show();
    REQUIRE(recorder.accept());
    REQUIRE(startRecordingAnyway(window));
    for (int command = 0; command < 4; ++command) {
        REQUIRE(!recorder.nextCommand().isEmpty());
        recorder.reply();
    }
    REQUIRE(waitUntil([&] { return client->recordingState() == RecorderRecordingState::Recording; }));

    QElapsedTimer close_timer;
    close_timer.start();
    window.close();
    const QString stop = recorder.nextCommand();
    recorder.reply();
    // The recorder here writes no file, which a file check would wait 15 s for.
    const bool closed = waitUntil([&] { return !window.isVisible(); }, 10000);
    REQUIRE_EQ(stop, QString("stop"));
    REQUIRE(closed);
    REQUIRE(close_timer.elapsed() < 5000);
    REQUIRE(!eventLogContains(window, "Waiting for the recorder to finish writing"));
}

TEST_CASE("A Vicon layout change is an error while recording and a warning otherwise") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    FakeLabRecorder recorder;
    BridgeWindow window(nullptr, false, remoteRecorderSettings(directory, recorder.port()));
    auto* client = window.findChild<LabRecorderClient*>();
    REQUIRE(client);
    REQUIRE(recorder.accept());
    REQUIRE(QMetaObject::invokeMethod(&window, "onBridgeLayoutChanged", Qt::DirectConnection));
    const bool warned = eventLogContains(window, "[warning] [bridge] The Vicon subjects or markers changed");

    REQUIRE(startRecordingAnyway(window));
    for (int command = 0; command < 4; ++command) {
        REQUIRE(!recorder.nextCommand().isEmpty());
        recorder.reply();
    }
    REQUIRE(waitUntil([&] { return client->recordingState() == RecorderRecordingState::Recording; }));
    REQUIRE(QMetaObject::invokeMethod(&window, "onBridgeLayoutChanged", Qt::DirectConnection));
    const bool failed = eventLogContains(window, "[error] [bridge] The Vicon subjects or markers changed");
    window.close();
    REQUIRE(warned);
    REQUIRE(failed);
}

TEST_CASE("Find Next Run says when the current run is still unused") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    BridgeWindow window(nullptr, false, remoteRecorderSettings(directory, 1));
    REQUIRE(QMetaObject::invokeMethod(&window, "onFindNextRun", Qt::DirectConnection));
    const bool unused = eventLogContains(window, "Run 1 is not used yet");
    const bool no_false_warning = !eventLogContains(window, "No unused run was found");
    window.close();
    REQUIRE(unused);
    REQUIRE(no_false_warning);
}

TEST_CASE("Only measured calibrations apply, and saving never overwrites another setup") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    auto settings = sessionSettings(directory);
    SessionConfiguration configuration = SessionConfigurationStore::load(*settings);
    configuration.stair_model_path = directory.filePath("stair.obj");
    SessionConfigurationStore::save(*settings, configuration);
    {
        QFile model(configuration.stair_model_path);
        REQUIRE(model.open(QIODevice::WriteOnly));
        REQUIRE(model.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n") > 0);
    }
    const ManagedCalibrationProfile built_in = CalibrationProfileStore::defaultProfile();
    REQUIRE(CalibrationProfileStore::save(
        *settings, {built_in, measuredCalibration("setup-a", 1.0), measuredCalibration("setup-b", 2.0)}));

    PreviewPanel panel(nullptr, settings);
    QComboBox* list = calibrationList(panel);
    REQUIRE(list);
    list->setCurrentIndex(list->findData(built_in.id));
    REQUIRE(QMetaObject::invokeMethod(&panel, "applySelectedCalibrationProfile", Qt::DirectConnection));
    const bool built_in_refused =
        panel.sessionCalibrationState() == SessionCalibrationState::Uncalibrated;

    list->setCurrentIndex(list->findData("setup-a"));
    REQUIRE(QMetaObject::invokeMethod(&panel, "applySelectedCalibrationProfile", Qt::DirectConnection));
    const bool applied = panel.sessionCalibrationState() == SessionCalibrationState::SavedProfile;

    // Setup B is only being looked at, so saving setup A's calibration adds an entry.
    list->setCurrentIndex(list->findData("setup-b"));
    REQUIRE(QMetaObject::invokeMethod(&panel, "saveSessionCalibrationProfile", Qt::DirectConnection));
    const auto saved = CalibrationProfileStore::load(*settings);
    const auto setup_b = std::find_if(saved.begin(), saved.end(),
                                      [](const auto& profile) { return profile.id == "setup-b"; });
    REQUIRE(built_in_refused);
    REQUIRE(applied);
    REQUIRE_EQ(saved.size(), 4);
    REQUIRE(setup_b != saved.end());
    REQUIRE(setup_b->gaze_transform.translation.x == 2.0);
    REQUIRE(saved.back().gaze_transform.translation.x == 1.0);
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
        // Cancel the bridge from inside its connection attempt, the one part of a
        // start that a stop cannot interrupt directly, so the slow path is tested.
        const bool connecting = !bridge_started ||
            waitUntil([&] { return bridgeStateText(window).startsWith("Connecting"); });
        QMetaObject::invokeMethod(&window, "onStopSession", Qt::DirectConnection);
        const bool canceled = waitUntil([&] { return sessionStopped(window); });
        window.close();
        // Wait generously and name any thread still running while the window is
        // alive, because destroying a running QThread kills the process before
        // any check below can report.
        const bool threads_stopped = waitUntil([&] {
            for (auto* thread : window.findChildren<QThread*>()) if (thread->isRunning()) return false;
            return true;
        }, 15000);
        if (!threads_stopped) {
            for (auto* thread : window.findChildren<QThread*>()) {
                if (!thread->isRunning()) continue;
                std::cerr << "Still running at teardown: "
                          << thread->metaObject()->className() << std::endl;
            }
        }
        REQUIRE(bridge_started == !recorder_only);
        REQUIRE(connecting);
        REQUIRE(discovery_started == recorder_only);
        REQUIRE(canceled);
        REQUIRE(threads_stopped);
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
