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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    testFilenameCommand();
    testTcpCommandSequence();

    if (g_failures > 0) {
        std::cerr << g_failures << " test failure(s)" << std::endl;
        return 1;
    }

    std::cout << "All LabRecorder tests passed" << std::endl;
    return 0;
}

