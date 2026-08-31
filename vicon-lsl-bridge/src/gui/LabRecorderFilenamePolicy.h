#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct LabRecorderFilenameFields {
    QString root;
    QString templ;
    QString participant;
    QString session;
    QString task;
    QString run;
    QString acquisition;
    QString modality;
};

enum class RecordingPathIssueLevel {
    Warning,
    Error,
};

struct RecordingPathIssue {
    RecordingPathIssueLevel level = RecordingPathIssueLevel::Error;
    QString field;
    QString message;
    QString corrective_action;
};

struct RecordingPathValidationOptions {
    bool allow_outside_study_root = false;
    bool allow_overwrite = false;
    bool create_parent_directories = false;
    bool verify_write_access = true;
    bool verify_storage = true;
    qint64 storage_warning_bytes = 10LL * 1024LL * 1024LL * 1024LL;
    int practical_path_length = 240;
};

struct RecordingPathResult {
    LabRecorderFilenameFields normalized_fields;
    QString relative_path;
    QString absolute_path;
    qint64 available_storage_bytes = -1;
    QVector<RecordingPathIssue> issues;

    bool valid() const;
    bool hasWarnings() const;
    QString firstError() const;
    QString summary() const;
};

class LabRecorderFilenamePolicy {
public:
    static QString filenameCommand(const LabRecorderFilenameFields& fields);
    static QString renderedFilename(const LabRecorderFilenameFields& fields);
    static bool hasUnresolvedFilenamePlaceholders(const LabRecorderFilenameFields& fields);
    static QStringList startRecordingCommands(const LabRecorderFilenameFields& fields,
                                              bool select_all_first);
    static QString sanitizedValue(QString value);
    static QString renderedFilenamePreview(const LabRecorderFilenameFields& fields);
    static RecordingPathResult validate(
        const LabRecorderFilenameFields& fields,
        const RecordingPathValidationOptions& options = {});
    static int findNextRun(const LabRecorderFilenameFields& fields,
                           int current_run,
                           const RecordingPathValidationOptions& options = {});
};
