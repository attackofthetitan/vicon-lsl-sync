#include "LabRecorderClientTestSupport.h"
#include "gui/LabRecorderClient.h"

namespace labrecorder_client_tests {

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
    expect(with_select.size() == 4, "start command sequence with select-all has four commands");
    expect(with_select.value(0) == "update",
           "start command sequence refreshes newly available streams first");
    expect(with_select.value(1) == "select all", "start command sequence can select all first");
    expect(with_select.value(2) == without_select.value(0),
           "start command sequence reuses filename command after select-all");
    expect(with_select.value(3) == "start", "start command sequence with select-all starts recording last");
}

} // namespace labrecorder_client_tests
