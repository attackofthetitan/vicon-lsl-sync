#include "LabRecorderClientTestSupport.h"
#include "gui/LabRecorderClient.h"

#include <QTcpServer>

#include <memory>
#include <vector>

namespace labrecorder_client_tests {

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

    expect(client.updateFilename(fields), "sends filename update");
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
    expect(readCommand(socket.get()) == "update",
           "server receives stream refresh before select-all in combined start sequence");
    expect(writeReply(socket.get(), "OK"), "server acknowledges stream refresh");
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

void testConnectionTimeoutDoesNotShortenCommandTimeout() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0),
           "separate timeout server listens");
    LabRecorderClient client;
    client.connectToServer("127.0.0.1", server.serverPort(), 20);
    expect(waitUntil([&client]() { return client.isConnected(); }),
           "separate timeout client connects within short connection deadline");
    expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
           "separate timeout server accepts client");
    std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
    if (!socket) return;

    int completions = 0;
    bool completion_ok = false;
    QObject::connect(&client, &LabRecorderClient::commandFinished,
                     [&completions, &completion_ok](const QString&, bool ok, const QString&) {
                         ++completions;
                         completion_ok = ok;
                     });

    expect(client.refreshStreams(), "queues refresh after short-deadline connection");
    expect(readCommand(socket.get()) == "update",
           "short-deadline connection sends refresh command");
    QElapsedTimer wait_timer;
    wait_timer.start();
    while (wait_timer.elapsed() < 100) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    expect(client.connectionState() == RecorderConnectionState::Connected && completions == 0,
           "command remains connected beyond the short connection deadline");
    expect(writeReply(socket.get(), "OK"), "separate timeout server acknowledges refresh");
    expect(waitUntil([&completions]() { return completions == 1; }) && completion_ok,
           "refresh completes within the independent command deadline");
}

void testCommandTimeoutDisconnectsAndDropsQueuedWork() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0), "timeout server listens");
    LabRecorderClient client;
    client.connectToServer("127.0.0.1", server.serverPort(), 50, 50);
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
    expect(client.sendCommand("start"), "timeout command starts");
    expect(!client.sendCommand("stop"), "concurrent command is rejected");
    expect(readCommand(socket.get()) == "start", "timeout server receives first command");
    expect(waitUntil([&client]() {
        return client.connectionState() == RecorderConnectionState::Error;
    }, 1000), "missing acknowledgement transitions connection to error");
    expect(failures == 2,
           "concurrent command rejection and timeout are reported");
    expect(client.recordingState() == RecorderRecordingState::Unknown,
           "timeout resets recording state");
    expect(!waitUntil([socket = socket.get()]() { return socket->canReadLine(); }, 100),
           "rejected command is never sent");
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
    expect(client.recordingState() == RecorderRecordingState::Unknown,
           "reconnect preserves unknown recording state until a command is acknowledged");
}

void testRecorderDuplicateAndShutdownProtocol() {
    LabRecorderFilenameFields fields;
    fields.root = "/tmp/data";
    fields.templ = "run-%r.xdf";
    fields.participant = "P001";
    fields.session = "S001";
    fields.task = "Reach";
    fields.run = "1";
    fields.acquisition = "vicon";
    fields.modality = "beh";

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "duplicate operation server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "duplicate operation client connects");
        expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
               "duplicate operation server accepts client");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;

        expect(client.startRecording(fields, true), "first Start is accepted");
        expect(!client.startRecording(fields, true),
               "rapid duplicate Start is rejected");
        expect(client.operationState() == RecorderOperationState::Starting,
               "the single active Start is visible");
        const QStringList commands = {
            "update", "select all",
            LabRecorderClient::filenameCommand(fields), "start",
        };
        for (const QString& command : commands) {
            expect(readCommand(socket.get()) == command,
                   "Start commands remain ordered");
            expect(writeReply(socket.get(), "OK"),
                   "duplicate operation server acknowledges Start command");
        }
        expect(waitUntil([&client]() {
                   return client.recordingState() ==
                          RecorderRecordingState::Recording;
               }),
               "single Start batch reaches Recording");
        expect(client.stopRecording(), "first Stop is accepted");
        expect(!client.stopRecording(), "rapid duplicate Stop is rejected");
        expect(readCommand(socket.get()) == "stop",
               "exactly one Stop reaches the server");
        expect(writeReply(socket.get(), "OK"),
               "server acknowledges the single Stop");
        expect(waitUntil([&client]() {
                   return client.recordingState() ==
                          RecorderRecordingState::Stopped;
               }) && client.activeOperation().isEmpty(),
               "Stop settles in Idle");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "shutdown server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "shutdown client connects");
        expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
               "shutdown server accepts client");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        expect(client.startRecording(fields, true), "shutdown Start begins");
        expect(readCommand(socket.get()) == "update", "Start preparation begins");
        expect(client.beginShutdown(), "shutdown waits for active Start");
        expect(!client.sendCommand("ignored"), "shutdown rejects new work");

        const QStringList remaining = {
            "select all", LabRecorderClient::filenameCommand(fields), "start",
        };
        expect(writeReply(socket.get(), "OK"), "first Start command is acknowledged");
        for (const QString& command : remaining) {
            expect(readCommand(socket.get()) == command, "Start finishes in order");
            expect(writeReply(socket.get(), "OK"), "Start command is acknowledged");
        }
        expect(readCommand(socket.get()) == "stop",
               "shutdown sends one Stop after Start completes");
        expect(writeReply(socket.get(), "OK"), "shutdown Stop is acknowledged");
        expect(waitUntil([&client]() { return client.shutdownReady(); }) &&
                   client.shutdownSettledSafely() &&
                   client.recordingState() == RecorderRecordingState::Stopped,
               "shutdown reaches a safe stopped state");
        expect(!waitUntil([socket = socket.get()]() { return socket->canReadLine(); }, 100),
               "shutdown sends no duplicate Stop");
    }
}

} // namespace labrecorder_client_tests
