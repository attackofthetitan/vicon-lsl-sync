#pragma once

#include <QString>
#include <QStringList>

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

class LabRecorderFilenamePolicy {
public:
    static QString filenameCommand(const LabRecorderFilenameFields& fields);
    static QString renderedFilename(const LabRecorderFilenameFields& fields);
    static bool hasUnresolvedFilenamePlaceholders(const LabRecorderFilenameFields& fields);
    static QStringList startRecordingCommands(const LabRecorderFilenameFields& fields,
                                              bool select_all_first);
    static QString sanitizedValue(QString value);
    static QString renderedFilenamePreview(const LabRecorderFilenameFields& fields);
    static QString validationError(const LabRecorderFilenameFields& fields);
};
