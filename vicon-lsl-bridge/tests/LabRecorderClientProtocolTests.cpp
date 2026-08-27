#include "LabRecorderClientTestSupport.h"
#include "gui/LabRecorderClient.h"
#include "gui/PerformanceBudgets.h"

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
    expect(client.sendCommand("start"), "timeout start command queues");
    expect(client.sendCommand("stop"), "timeout follow-up command queues");
    expect(readCommand(socket.get()) == "start", "timeout server receives first command");
    expect(waitUntil([&client]() {
        return client.connectionState() == RecorderConnectionState::Error;
    }, 1000), "missing acknowledgement transitions connection to error");
    expect(failures == 2,
           "timeout deterministically fails the active and queued commands");
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
               "bounded command queue server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }) &&
                   waitUntil([&server]() { return server.hasPendingConnections(); }),
               "bounded command queue client connects");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        for (int index = 0;
             index < vicon_lsl::gui::PerformanceBudgets::MaximumRecorderQueueDepth;
             ++index) {
            expect(client.sendCommand("bounded-" + QString::number(index)),
                   "command queue accepts work through its documented bound");
        }
        expect(client.queueDepth() ==
                   vicon_lsl::gui::PerformanceBudgets::MaximumRecorderQueueDepth &&
                   !client.sendCommand("bounded-overflow"),
               "command queue rejects the first batch beyond its documented bound");
        expect(readCommand(socket.get()) == "bounded-0",
               "bounded queue keeps one active command and no hidden overflow");
    }

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

        expect(client.startRecording(fields, true),
               "first deterministic Start is accepted");
        expect(!client.startRecording(fields, true),
               "rapid duplicate Start is rejected");
        expect(client.queueDepth() == 1 &&
                   client.operationState() == RecorderOperationState::Starting,
               "starting state and bounded depth are visible");
        const QStringList commands = {
            "update", "select all",
            LabRecorderClient::filenameCommand(fields), "start",
        };
        for (const QString& command : commands) {
            expect(readCommand(socket.get()) == command,
                   "one Start batch preserves atomic command ordering");
            expect(writeReply(socket.get(), "OK"),
                   "duplicate operation server acknowledges Start command");
        }
        expect(waitUntil([&client]() {
                   return client.recordingState() ==
                          RecorderRecordingState::Recording;
               }),
               "single Start batch reaches Recording");
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(client.isConnected() &&
                   client.recordingState() == RecorderRecordingState::Recording &&
                   !server.hasPendingConnections(),
               "an active recording prevents live connection replacement");
        expect(client.stopRecording(), "first Stop is accepted");
        expect(!client.stopRecording(), "rapid duplicate Stop is rejected");
        expect(readCommand(socket.get()) == "stop",
               "exactly one Stop reaches the server");
        expect(writeReply(socket.get(), "OK"),
               "server acknowledges the single Stop");
        expect(waitUntil([&client]() {
                   return client.recordingState() ==
                          RecorderRecordingState::Stopped;
               }) &&
                   client.queueDepth() == 0,
               "Stop settles in an actionable idle state");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "filename coalescing server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "filename coalescing client connects");
        expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
               "filename coalescing server accepts client");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        expect(client.refreshStreams(), "holds an active refresh before edits");
        expect(readCommand(socket.get()) == "update",
               "refresh is active before queued filename edits");
        LabRecorderFilenameFields older = fields;
        older.run = "2";
        LabRecorderFilenameFields newest = fields;
        newest.run = "3";
        expect(client.updateFilename(older) &&
                   client.updateFilename(newest),
               "rapid unsent filename edits are accepted and coalesced");
        expect(client.queueDepth() == 2,
               "coalesced filename occupies one bounded queued batch");
        expect(writeReply(socket.get(), "OK"),
               "refresh completes before newest filename");
        expect(readCommand(socket.get()) ==
                   LabRecorderClient::filenameCommand(newest),
               "only the newest unsent filename reaches the server");
        expect(writeReply(socket.get(), "OK"),
               "newest filename is acknowledged");
        expect(waitUntil([&client]() { return client.queueDepth() == 0; }),
               "coalesced work returns to Idle");
        expect(!waitUntil([socket = socket.get()]() {
                   return socket->canReadLine();
               }, 100),
               "superseded filename is never sent");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "pre-send shutdown server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "pre-send shutdown client connects");
        expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
               "pre-send shutdown server accepts client");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        expect(client.startRecording(fields, true),
               "pre-send shutdown queues Start");
        expect(readCommand(socket.get()) == "update",
               "only Start refresh reaches server before close");
        client.beginShutdown();
        expect(client.shutdownRequested(),
               "close prevents new non-shutdown work");
        expect(!client.sendCommand("unsafe during shutdown"),
               "generic commands are also rejected after shutdown begins");
        expect(writeReply(socket.get(), "OK"),
               "in-flight refresh is acknowledged during shutdown");
        expect(waitUntil([&client]() { return client.shutdownReady(); }),
               "Start canceled before reaching server settles shutdown");
        expect(!client.startMayHaveReachedServer(),
               "canceled pre-send Start never reaches the recorder");
        expect(!waitUntil([socket = socket.get()]() {
                   return socket->canReadLine();
               }, 100),
               "pre-send close sends neither select, filename, Start, nor Stop");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "post-send shutdown server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "post-send shutdown client connects");
        expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
               "post-send shutdown server accepts client");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        expect(client.startRecording(fields, true),
               "post-send shutdown queues Start");
        const QStringList before_start = {
            "update", "select all", LabRecorderClient::filenameCommand(fields),
        };
        for (const QString& command : before_start) {
            expect(readCommand(socket.get()) == command,
                   "post-send Start preparation is ordered");
            expect(writeReply(socket.get(), "OK"),
                   "post-send server acknowledges preparation");
        }
        expect(readCommand(socket.get()) == "start",
               "Start command reaches server before close");
        expect(client.startMayHaveReachedServer(),
               "client records that Start may have reached the server");
        expect(client.beginShutdown(),
               "close arranges a final Stop after a sent Start");
        expect(writeReply(socket.get(), "OK"),
               "server acknowledges sent Start during close");
        expect(readCommand(socket.get()) == "stop",
               "Stop is the final recorder operation after sent Start");
        expect(writeReply(socket.get(), "OK"),
               "server acknowledges shutdown Stop");
        expect(waitUntil([&client]() { return client.shutdownReady(); }) &&
                   client.shutdownSettledSafely() &&
                   !client.startMayHaveReachedServer() &&
                   client.recordingState() == RecorderRecordingState::Stopped,
               "acknowledged shutdown Stop reaches a safe final state");
    }

    for (const int close_command_index : {1, 2}) {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "mid-preparation shutdown server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "mid-preparation shutdown client connects");
        expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
               "mid-preparation shutdown server accepts client");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        expect(client.startRecording(fields, true),
               "mid-preparation Start is accepted");
        const QStringList commands = {
            "update", "select all", LabRecorderClient::filenameCommand(fields),
        };
        for (int index = 0; index < close_command_index; ++index) {
            expect(readCommand(socket.get()) == commands[index],
                   "pre-close Start preparation remains ordered");
            expect(writeReply(socket.get(), "OK"),
                   "pre-close Start preparation is acknowledged");
        }
        expect(readCommand(socket.get()) == commands[close_command_index],
               "close occurs during the selected Start preparation command");
        client.beginShutdown();
        expect(writeReply(socket.get(), "OK"),
               "in-flight Start preparation command settles during close");
        expect(waitUntil([&client]() { return client.shutdownReady(); }) &&
                   !client.startMayHaveReachedServer(),
               "close during Start preparation cancels before Start reaches the recorder");
        expect(!waitUntil([socket = socket.get()]() {
                   return socket->canReadLine();
               }, 100),
               "close during Start preparation sends no later command");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "active-stop shutdown server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "active-stop shutdown client connects");
        expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
               "active-stop shutdown server accepts client");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        expect(client.startRecording(fields, true),
               "active-stop shutdown Start is accepted");
        const QStringList start_commands = {
            "update", "select all", LabRecorderClient::filenameCommand(fields), "start",
        };
        for (const QString& command : start_commands) {
            expect(readCommand(socket.get()) == command,
                   "active-stop Start sequence remains ordered");
            expect(writeReply(socket.get(), "OK"),
                   "active-stop Start sequence is acknowledged");
        }
        expect(waitUntil([&client]() {
                   return client.recordingState() == RecorderRecordingState::Recording;
               }),
               "active-stop fixture reaches Recording");
        expect(client.stopRecording(), "Stop begins before close");
        expect(readCommand(socket.get()) == "stop",
               "in-flight Stop reaches the recorder once");
        client.beginShutdown();
        expect(writeReply(socket.get(), "OK"),
               "in-flight Stop acknowledgement settles close");
        expect(waitUntil([&client]() { return client.shutdownReady(); }) &&
                   client.recordingState() == RecorderRecordingState::Stopped,
               "close during Stop waits for the existing Stop without duplicating it");
        expect(!waitUntil([socket = socket.get()]() {
                   return socket->canReadLine();
               }, 100),
               "close during Stop emits no duplicate Stop");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "recording-state shutdown server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }) &&
                   waitUntil([&server]() { return server.hasPendingConnections(); }),
               "recording-state shutdown client connects");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        expect(client.startRecording(fields, true),
               "recording-state shutdown Start is accepted");
        const QStringList start_commands = {
            "update", "select all", LabRecorderClient::filenameCommand(fields), "start",
        };
        for (const QString& command : start_commands) {
            expect(readCommand(socket.get()) == command &&
                       writeReply(socket.get(), "OK"),
                   "recording-state shutdown Start reaches the server in order");
        }
        expect(waitUntil([&client]() {
                   return client.recordingState() == RecorderRecordingState::Recording;
               }),
               "recording-state shutdown fixture reaches Recording");
        expect(client.beginShutdown(),
               "close while Recording queues one final Stop");
        expect(readCommand(socket.get()) == "stop",
               "close while Recording sends the final Stop");
        expect(writeReply(socket.get(), "OK") &&
                   waitUntil([&client]() { return client.shutdownReady(); }) &&
                   client.shutdownSettledSafely(),
               "close while Recording settles only after Stop acknowledgement");
        expect(!waitUntil([socket = socket.get()]() {
                   return socket->canReadLine();
               }, 100),
               "close while Recording emits exactly one Stop");
    }

    {
        QTcpServer first_server;
        QTcpServer second_server;
        expect(first_server.listen(QHostAddress::LocalHost, 0) &&
                   second_server.listen(QHostAddress::LocalHost, 0),
               "connection replacement servers listen");
        LabRecorderClient client;
        QString replacement_failure;
        QObject::connect(&client, &LabRecorderClient::commandFinished,
                         [&replacement_failure](const QString&, bool ok,
                                                const QString& message) {
                             if (!ok && message.contains("replaced")) {
                                 replacement_failure = message;
                             }
                         });
        client.connectToServer("127.0.0.1", first_server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "connection replacement client reaches first endpoint");
        expect(waitUntil([&first_server]() {
                   return first_server.hasPendingConnections();
               }),
               "first replacement endpoint accepts client");
        std::unique_ptr<QTcpSocket> first(first_server.nextPendingConnection());
        expect(client.refreshStreams(), "replacement fixture queues active work");
        expect(readCommand(first.get()) == "update",
               "replacement fixture observes active command");
        client.connectToServer("127.0.0.1", second_server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }) &&
                   waitUntil([&second_server]() {
                       return second_server.hasPendingConnections();
                   }),
               "connection replacement reaches the new endpoint");
        expect(!replacement_failure.isEmpty() && client.queueDepth() == 0 &&
                   client.recordingState() == RecorderRecordingState::Unknown,
               "connection replacement deterministically fails old work and resets state");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "post-disconnect shutdown server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }) &&
                   waitUntil([&server]() { return server.hasPendingConnections(); }),
               "post-disconnect shutdown client connects");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        if (!socket) return;
        expect(client.startRecording(fields, true),
               "post-disconnect shutdown Start is accepted");
        const QStringList commands = {
            "update", "select all", LabRecorderClient::filenameCommand(fields), "start",
        };
        for (const QString& command : commands) {
            expect(readCommand(socket.get()) == command &&
                       writeReply(socket.get(), "OK"),
                   "post-disconnect Start reaches Recording in order");
        }
        expect(waitUntil([&client]() {
                   return client.recordingState() == RecorderRecordingState::Recording;
               }),
               "post-disconnect fixture records the acknowledged Start");
        socket->disconnectFromHost();
        socket->close();
        expect(waitUntil([&client]() { return !client.isConnected(); }),
               "recorder disconnect is observed before application close");
        expect(!client.beginShutdown() && client.shutdownReady() &&
                   !client.shutdownSettledSafely() &&
                   client.startMayHaveReachedServer(),
               "close after disconnect settles locally while preserving evidence that remote Stop was impossible");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "uncertain-Start reconnect server listens");
        LabRecorderClient client;
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }) &&
                   waitUntil([&server]() { return server.hasPendingConnections(); }),
               "uncertain-Start reconnect client connects");
        std::unique_ptr<QTcpSocket> first(server.nextPendingConnection());
        if (!first) return;
        expect(client.startRecording(fields, true),
               "uncertain-Start reconnect fixture accepts Start");
        const QStringList commands = {
            "update", "select all", LabRecorderClient::filenameCommand(fields), "start",
        };
        for (const QString& command : commands) {
            expect(readCommand(first.get()) == command &&
                       writeReply(first.get(), "OK"),
                   "uncertain-Start reconnect fixture reaches Recording");
        }
        expect(waitUntil([&client]() {
                   return client.recordingState() == RecorderRecordingState::Recording;
               }),
               "uncertain-Start reconnect fixture observes acknowledged Start");
        first->disconnectFromHost();
        first->close();
        expect(waitUntil([&client]() { return !client.isConnected(); }) &&
                   client.startMayHaveReachedServer(),
               "connection loss retains uncertain sent-Start evidence");

        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }) &&
                   waitUntil([&server]() { return server.hasPendingConnections(); }),
               "reconnect reaches the configured endpoint");
        std::unique_ptr<QTcpSocket> second(server.nextPendingConnection());
        if (!second) return;
        expect(client.startMayHaveReachedServer() &&
                   !client.startRecording(fields, true),
               "reconnect preserves uncertainty and rejects a second Start");
        expect(client.stopRecording() && readCommand(second.get()) == "stop",
               "reconnect permits the recovery Stop");
        expect(writeReply(second.get(), "OK") &&
                   waitUntil([&client]() {
                       return client.recordingState() ==
                              RecorderRecordingState::Stopped;
                   }) &&
                   !client.startMayHaveReachedServer(),
               "acknowledged recovery Stop clears sent-Start uncertainty");
    }

    {
        QTcpServer server;
        expect(server.listen(QHostAddress::LocalHost, 0),
               "malformed reply server listens");
        LabRecorderClient client;
        QString failure;
        QObject::connect(&client, &LabRecorderClient::commandFinished,
                         [&failure](const QString&, bool ok, const QString& message) {
                             if (!ok) failure = message;
                         });
        client.connectToServer("127.0.0.1", server.serverPort());
        expect(waitUntil([&client]() { return client.isConnected(); }),
               "malformed reply client connects");
        expect(waitUntil([&server]() { return server.hasPendingConnections(); }),
               "malformed reply server accepts client");
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        expect(client.refreshStreams() && readCommand(socket.get()) == "update",
               "malformed reply fixture reaches one active command");
        expect(writeReply(socket.get(), "NO"),
               "malformed reply bytes are delivered");
        expect(waitUntil([&failure]() { return !failure.isEmpty(); }) &&
                   failure.contains("Unexpected LabRecorder reply") &&
                   client.connectionState() == RecorderConnectionState::Error &&
                   client.queueDepth() == 0,
               "malformed reply fails the connection and clears work deterministically");
    }
}

} // namespace labrecorder_client_tests
