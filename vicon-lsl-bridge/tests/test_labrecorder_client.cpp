#include "gui/LabRecorderClient.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>

#include <iostream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << std::endl;
        ++g_failures;
    }
}

bool waitUntil(const std::function<bool()>& condition, int timeout_ms = 1000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

QString readCommand(QTcpSocket* socket) {
    if (!waitUntil([socket]() { return socket->canReadLine(); })) {
        return {};
    }
    return QString::fromUtf8(socket->readLine()).trimmed();
}

bool writeReply(QTcpSocket* socket, const QByteArray& reply) {
    if (socket->write(reply) != reply.size()) {
        return false;
    }
    return waitUntil([socket]() { return socket->bytesToWrite() == 0; });
}

void testFilenameCommand() {
    LabRecorderFilenameFields fields;
    fields.root = "C:/Data/{bad}";
    fields.templ = "sub-%p/%b.xdf";
    fields.participant = "P001";
    fields.session = "S001";
    fields.task = "Reach\nTask";
    fields.run = "2";
    fields.acquisition = "vicon";
    fields.modality = "beh";

    QString command = LabRecorderClient::filenameCommand(fields);
    expect(command == "filename {root:C:/Data/_bad_} {template:sub-%p/%b.xdf} "
                      "{participant:P001} {session:S001} {task:Reach Task} "
                      "{run:2} {acquisition:vicon} {modality:beh}",
           "formats and sanitizes filename command");
}

void testRenderedFilenameUsesSharedSanitization() {
    LabRecorderFilenameFields fields;
    fields.templ = "sub-%p/ses-%s/task-%b/run-%r/repeat-%n/acq-%a/%m.xdf";
    fields.participant = " P{001} ";
    fields.session = "S\n001";
    fields.task = "Reach\rTask";
    fields.run = " 2 ";
    fields.acquisition = "vicon";
    fields.modality = " beh ";

    expect(LabRecorderClient::renderedFilename(fields) ==
               "sub-P_001_/ses-S 001/task-Reach Task/run-2/repeat-2/acq-vicon/beh.xdf",
           "renders filename preview with shared sanitization");
}

void testUnresolvedFilenamePlaceholders() {
    LabRecorderFilenameFields fields;
    fields.templ = "sub-%p_task-%b_run-%r.xdf";
    fields.participant = "P001";
    fields.task = "Reach";
    fields.run = "1";

    expect(!LabRecorderClient::hasUnresolvedFilenamePlaceholders(fields),
           "detects no unresolved placeholders when required values are present");

    fields.run = " \n ";
    expect(LabRecorderClient::hasUnresolvedFilenamePlaceholders(fields),
           "detects unresolved placeholder after sanitization empties value");

    fields.templ = "sub-%p.xdf";
    expect(!LabRecorderClient::hasUnresolvedFilenamePlaceholders(fields),
           "ignores missing fields not referenced by template");

    fields.templ = "sub-%p_unknown-%x.xdf";
    expect(LabRecorderClient::hasUnresolvedFilenamePlaceholders(fields),
           "detects unknown unresolved placeholders after rendering");
}

void testStartRecordingCommands() {
    LabRecorderFilenameFields fields;
    fields.root = "/tmp/data";
    fields.templ = "sub-%p_task-%b.xdf";
    fields.participant = "P002";
    fields.task = "Walk";

    QStringList without_select = LabRecorderClient::startRecordingCommands(fields, false);
    expect(without_select.size() == 2, "start command sequence without select-all has two commands");
    expect(without_select.value(0) ==
               "filename {root:/tmp/data} {template:sub-%p_task-%b.xdf} {participant:P002} {task:Walk}",
           "start command sequence includes filename command first");
    expect(without_select.value(1) == "start", "start command sequence starts recording last");

    QStringList with_select = LabRecorderClient::startRecordingCommands(fields, true);
    expect(with_select.size() == 3, "start command sequence with select-all has three commands");
    expect(with_select.value(0) == "select all", "start command sequence can select all first");
    expect(with_select.value(1) == without_select.value(0),
           "start command sequence reuses filename command after select-all");
    expect(with_select.value(2) == "start", "start command sequence with select-all starts recording last");
}

void testTcpCommandSequence() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0), "fake LabRecorder server listens");

    LabRecorderClient client;
    client.connectToServer("127.0.0.1", server.serverPort());
    expect(waitUntil([&client]() { return client.isConnected(); }), "client connects to fake server");
    expect(waitUntil([&server]() { return server.hasPendingConnections(); }), "server accepts fake client");
    std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
    expect(socket != nullptr, "server has pending socket");
    if (!socket) {
        return;
    }

    int completions = 0;
    bool last_completion_ok = false;
    QObject::connect(&client, &LabRecorderClient::commandFinished,
                     [&completions, &last_completion_ok](const QString&, bool ok, const QString&) {
                         ++completions;
                         last_completion_ok = ok;
                     });

    LabRecorderFilenameFields fields;
    fields.root = "/tmp/data";
    fields.templ = "sub-%p_task-%b_run-%r.xdf";
    fields.participant = "P002";
    fields.task = "Walk";
    fields.run = "3";

    std::vector<QString> expected = {
        "update",
        "filename {root:/tmp/data} {template:sub-%p_task-%b_run-%r.xdf} {participant:P002} {task:Walk} {run:3}",
        "start",
        "stop",
    };

    expect(client.refreshStreams(), "sends update");
    expect(readCommand(socket.get()) == expected[0], "server receives update");
    expect(writeReply(socket.get(), "OK"), "server acknowledges update");
    expect(waitUntil([&completions]() { return completions == 1; }) && last_completion_ok,
           "client completes update after acknowledgement");

    expect(client.sendCommand(LabRecorderClient::filenameCommand(fields)), "sends filename");
    expect(readCommand(socket.get()) == expected[1], "server receives filename");
    expect(writeReply(socket.get(), "OK"), "server acknowledges filename");
    expect(waitUntil([&completions]() { return completions == 2; }) && last_completion_ok,
           "client completes filename after acknowledgement");

    expect(client.sendCommand("start"), "sends start");
    expect(readCommand(socket.get()) == expected[2], "server receives start");
    expect(writeReply(socket.get(), "OK"), "server acknowledges start");
    expect(waitUntil([&completions]() { return completions == 3; }) && last_completion_ok,
           "client completes start after acknowledgement");

    expect(client.stopRecording(), "sends stop");
    expect(readCommand(socket.get()) == expected[3], "server receives stop");
    expect(writeReply(socket.get(), "OK"), "server acknowledges stop");
    expect(waitUntil([&completions]() { return completions == 4; }) && last_completion_ok,
           "client completes stop after acknowledgement");
}

void testTcpStartRecordingSequenceWithSelectAll() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0), "fake LabRecorder server listens for start sequence");

    LabRecorderClient client;
    client.connectToServer("127.0.0.1", server.serverPort());
    expect(waitUntil([&client]() { return client.isConnected(); }),
           "client connects to fake server for start sequence");
    expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
           "server accepts fake client for start sequence");
    std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
    expect(socket != nullptr, "server has pending socket for start sequence");
    if (!socket) {
        return;
    }

    int completions = 0;
    bool completion_ok = false;
    QObject::connect(&client, &LabRecorderClient::commandFinished,
                     [&completions, &completion_ok](const QString&, bool ok, const QString&) {
                         ++completions;
                         completion_ok = ok;
                     });

    LabRecorderFilenameFields fields;
    fields.root = "/tmp/data";
    fields.templ = "sub-%p_task-%b_run-%r.xdf";
    fields.participant = "P003";
    fields.task = "Jump";
    fields.run = "4";

    expect(client.startRecording(fields, true), "sends combined start sequence with select-all");
    expect(readCommand(socket.get()) == "select all",
           "server receives select-all before filename in combined start sequence");
    expect(writeReply(socket.get(), "OK"), "server acknowledges select-all");
    expect(readCommand(socket.get()) ==
               "filename {root:/tmp/data} {template:sub-%p_task-%b_run-%r.xdf} "
               "{participant:P003} {task:Jump} {run:4}",
           "server receives filename before start in combined start sequence");
    expect(writeReply(socket.get(), "OK"), "server acknowledges start filename");
    expect(readCommand(socket.get()) == "start",
           "server receives start after filename in combined start sequence");
    expect(writeReply(socket.get(), "OK"), "server acknowledges recording start");
    expect(waitUntil([&completions]() { return completions == 1; }) && completion_ok,
           "start sequence completes after every acknowledgement");
    expect(client.recordingState() == RecorderRecordingState::Recording,
           "acknowledged start updates recording state");
}

void testFragmentedReplyControlsCommandProgress() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0), "fragmented reply server listens");
    LabRecorderClient client;
    client.connectToServer("127.0.0.1", server.serverPort(), 500);
    expect(waitUntil([&client]() { return client.isConnected(); }),
           "fragmented reply client connects");
    expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
           "fragmented reply server accepts client");
    std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
    if (!socket) return;

    int completions = 0;
    bool completion_ok = false;
    QObject::connect(&client, &LabRecorderClient::commandFinished,
                     [&completions, &completion_ok](const QString&, bool ok, const QString&) {
                         ++completions;
                         completion_ok = ok;
                     });

    expect(client.sendCommand("update"), "fragmented reply command queues");
    expect(readCommand(socket.get()) == "update", "fragmented reply server receives command");
    expect(writeReply(socket.get(), "O"), "server writes first reply fragment");
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    expect(completions == 0, "partial reply does not complete command");
    expect(writeReply(socket.get(), "K"), "server writes final reply fragment");
    expect(waitUntil([&completions]() { return completions == 1; }) && completion_ok,
           "complete fragmented acknowledgement finishes command");
}

void testCommandTimeoutDisconnectsAndDropsQueuedWork() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0), "timeout server listens");
    LabRecorderClient client;
    client.connectToServer("127.0.0.1", server.serverPort(), 50);
    expect(waitUntil([&client]() { return client.isConnected(); }), "timeout client connects");
    expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
           "timeout server accepts client");
    std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
    if (!socket) return;

    int failures = 0;
    QObject::connect(&client, &LabRecorderClient::commandFinished,
                     [&failures](const QString&, bool ok, const QString&) {
                         if (!ok) ++failures;
                     });
    expect(client.sendCommand("start"), "timeout start command queues");
    expect(client.sendCommand("stop"), "timeout follow-up command queues");
    expect(readCommand(socket.get()) == "start", "timeout server receives first command");
    expect(waitUntil([&client]() {
        return client.connectionState() == RecorderConnectionState::Error;
    }, 1000), "missing acknowledgement transitions connection to error");
    expect(failures == 1, "timed-out active command reports one failure");
    expect(client.recordingState() == RecorderRecordingState::Unknown,
           "timeout resets recording state");
    expect(!waitUntil([socket = socket.get()]() { return socket->canReadLine(); }, 100),
           "queued command is dropped after timeout");
}

void testMidCommandDisconnectReportsFailure() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0), "disconnect server listens");
    LabRecorderClient client;
    client.connectToServer("127.0.0.1", server.serverPort(), 500);
    expect(waitUntil([&client]() { return client.isConnected(); }), "disconnect client connects");
    expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
           "disconnect server accepts client");
    std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
    if (!socket) return;

    int failures = 0;
    QObject::connect(&client, &LabRecorderClient::commandFinished,
                     [&failures](const QString&, bool ok, const QString&) {
                         if (!ok) ++failures;
                     });
    expect(client.sendCommand("update"), "disconnect command queues");
    expect(readCommand(socket.get()) == "update", "disconnect server receives command");
    socket->disconnectFromHost();
    expect(waitUntil([&client]() {
        return client.connectionState() == RecorderConnectionState::Disconnected;
    }), "mid-command disconnect updates connection state");
    expect(failures == 1, "mid-command disconnect reports command failure");
    expect(client.recordingState() == RecorderRecordingState::Unknown,
           "mid-command disconnect resets recording state");
}

void testConnectionStateTracksIdleDisconnectAndReconnect() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0), "state server listens");
    LabRecorderClient client;
    client.connectToServer("127.0.0.1", server.serverPort());
    expect(waitUntil([&client]() { return client.isConnected(); }),
           "state client connects");
    expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
           "state server accepts client");
    std::unique_ptr<QTcpSocket> first(server.nextPendingConnection());
    expect(first != nullptr, "state server owns first connection");
    if (!first) return;

    first->disconnectFromHost();
    expect(waitUntil([&client]() {
        return client.connectionState() == RecorderConnectionState::Disconnected;
    }), "idle remote disconnect updates client state");
    expect(client.recordingState() == RecorderRecordingState::Unknown,
           "idle disconnect resets recording state");

    client.connectToServer("127.0.0.1", server.serverPort());
    expect(waitUntil([&client]() { return client.isConnected(); }),
           "client reconnects after idle disconnect");
    expect(client.recordingState() == RecorderRecordingState::Stopped,
           "reconnect establishes stopped recording state");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    testFilenameCommand();
    testRenderedFilenameUsesSharedSanitization();
    testUnresolvedFilenamePlaceholders();
    testStartRecordingCommands();
    testTcpCommandSequence();
    testTcpStartRecordingSequenceWithSelectAll();
    testFragmentedReplyControlsCommandProgress();
    testCommandTimeoutDisconnectsAndDropsQueuedWork();
    testMidCommandDisconnectReportsFailure();
    testConnectionStateTracksIdleDisconnectAndReconnect();

    if (g_failures > 0) {
        std::cerr << g_failures << " test failure(s)" << std::endl;
        return 1;
    }

    std::cout << "All LabRecorder tests passed" << std::endl;
    return 0;
}
