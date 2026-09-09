#include "LabRecorderClientTestSupport.h"
#include "gui/LabRecorderClient.h"
#include <QRegularExpression>

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

    QString command = LabRecorderFilenamePolicy::filenameCommand(fields);
    expect(command == "filename {root:C:/Data/_bad_} {template:%b} "
                      "{participant:P001} {session:S001} {task:sub-P001/Reach Task.xdf} "
                      "{run:2} {acquisition:vicon} {modality:beh}",
           "formats and sanitizes filename command");

    // Exercise the actual recorder's legacy semantics: template/modality are
    // lowercased, %r is unavailable, and %n is padded to three digits.
    for (const QString& run : {QString("1"), QString("007")}) {
        fields.root = "/tmp/Study Root";
        fields.templ = "Upper/sub-%p/ses-%s/%m/Task-%b_Run-%r_Legacy-%n.xdf";
        fields.task = "AR Walking";
        fields.run = run;
        fields.modality = "U";
        QMap<QString, QString> options;
        auto matches = QRegularExpression("\\{(\\w+):([^}]*)\\}")
                           .globalMatch(LabRecorderFilenamePolicy::filenameCommand(fields));
        while (matches.hasNext()) {
            const auto match = matches.next();
            options[match.captured(1)] = match.captured(2);
        }
        QString actual = options["template"].toLower();
        actual.replace("%b", options["task"]);
        actual.replace("%p", options["participant"]);
        actual.replace("%s", options["session"]);
        actual.replace("%a", options["acquisition"]);
        actual.replace("%m", options["modality"].toLower());
        actual.replace("%n", QString("%1").arg(options["run"].toInt(), 3, 10, QChar('0')));
        expect(actual == "Upper/sub-P001/ses-S001/U/Task-AR Walking_Run-" + run +
                             "_Legacy-" + run + ".xdf",
               "legacy recorder preserves the bridge's exact case, directories and run format");
    }
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

    expect(LabRecorderFilenamePolicy::renderedFilename(fields) ==
               "sub-P_001_/ses-S 001/task-Reach Task/run-2/repeat-2/acq-vicon/beh.xdf",
           "renders filename preview with shared sanitization");
}

void testUnresolvedFilenamePlaceholders() {
    LabRecorderFilenameFields fields;
    fields.templ = "sub-%p_task-%b_run-%r.xdf";
    fields.participant = "P001";
    fields.task = "Reach";
    fields.run = "1";

    expect(!LabRecorderFilenamePolicy::hasUnresolvedFilenamePlaceholders(fields),
           "detects no unresolved placeholders when required values are present");

    fields.run = " \n ";
    expect(LabRecorderFilenamePolicy::hasUnresolvedFilenamePlaceholders(fields),
           "detects unresolved placeholder after sanitization empties value");

    fields.templ = "sub-%p.xdf";
    expect(!LabRecorderFilenamePolicy::hasUnresolvedFilenamePlaceholders(fields),
           "ignores missing fields not referenced by template");

    fields.templ = "sub-%p_unknown-%x.xdf";
    expect(LabRecorderFilenamePolicy::hasUnresolvedFilenamePlaceholders(fields),
           "detects unknown unresolved placeholders after rendering");
}

void testStartRecordingCommands() {
    LabRecorderFilenameFields fields;
    fields.root = "/tmp/data";
    fields.templ = "sub-%p_task-%b.xdf";
    fields.participant = "P002";
    fields.task = "Walk";

    QStringList without_select =
        LabRecorderFilenamePolicy::startRecordingCommands(fields, false);
    expect(without_select.size() == 2, "start command sequence without select-all has two commands");
    expect(without_select.value(0) ==
               "filename {root:/tmp/data} {template:%b} {participant:P002} {task:sub-P002_task-Walk.xdf}",
           "start command sequence includes filename command first");
    expect(without_select.value(1) == "start", "start command sequence starts recording last");

    QStringList with_select =
        LabRecorderFilenamePolicy::startRecordingCommands(fields, true);
    expect(with_select.size() == 4, "start command sequence with select-all has four commands");
    expect(with_select.value(0) == "update",
           "start command sequence refreshes newly available streams first");
    expect(with_select.value(1) == "select all", "start command sequence can select all first");
    expect(with_select.value(2) == without_select.value(0),
           "start command sequence reuses filename command after select-all");
    expect(with_select.value(3) == "start", "start command sequence with select-all starts recording last");
}

} // namespace labrecorder_client_tests
