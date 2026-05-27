#include "gui/LabRecorderClient.h"

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>

#include <iostream>
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

QString readCommand(QTcpSocket* socket) {
    if (!socket->canReadLine() && !socket->waitForReadyRead(1000)) {
        return {};
    }
    return QString::fromUtf8(socket->readLine()).trimmed();
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
    expect(client.connectToServer("127.0.0.1", server.serverPort()), "client connects to fake server");
    expect(server.waitForNewConnection(1000), "server accepts fake client");
    std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
    expect(socket != nullptr, "server has pending socket");
    if (!socket) {
        return;
    }

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

    expect(client.updateFilename(fields), "sends filename");
    expect(readCommand(socket.get()) == expected[1], "server receives filename");

    expect(client.startRecording(), "sends start");
    expect(readCommand(socket.get()) == expected[2], "server receives start");

    expect(client.stopRecording(), "sends stop");
    expect(readCommand(socket.get()) == expected[3], "server receives stop");
}

void testTcpStartRecordingSequenceWithSelectAll() {
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost, 0), "fake LabRecorder server listens for start sequence");

    LabRecorderClient client;
    expect(client.connectToServer("127.0.0.1", server.serverPort()),
           "client connects to fake server for start sequence");
    expect(server.waitForNewConnection(1000), "server accepts fake client for start sequence");
    std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
    expect(socket != nullptr, "server has pending socket for start sequence");
    if (!socket) {
        return;
    }

    LabRecorderFilenameFields fields;
    fields.root = "/tmp/data";
    fields.templ = "sub-%p_task-%b_run-%r.xdf";
    fields.participant = "P003";
    fields.task = "Jump";
    fields.run = "4";

    expect(client.startRecording(fields, true), "sends combined start sequence with select-all");
    expect(readCommand(socket.get()) == "select all",
           "server receives select-all before filename in combined start sequence");
    expect(readCommand(socket.get()) ==
               "filename {root:/tmp/data} {template:sub-%p_task-%b_run-%r.xdf} "
               "{participant:P003} {task:Jump} {run:4}",
           "server receives filename before start in combined start sequence");
    expect(readCommand(socket.get()) == "start",
           "server receives start after filename in combined start sequence");
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

    if (g_failures > 0) {
        std::cerr << g_failures << " test failure(s)" << std::endl;
        return 1;
    }

    std::cout << "All LabRecorder tests passed" << std::endl;
    return 0;
}
