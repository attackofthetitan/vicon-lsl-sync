#include "gui/LabRecorderFilenamePolicy.h"

#include <QDir>
#include <QFileInfo>

namespace {

struct FilenameToken {
    const char* placeholder;
    QString LabRecorderFilenameFields::*field;
};

const FilenameToken kFilenameTokens[] = {
    {"%p", &LabRecorderFilenameFields::participant},
    {"%s", &LabRecorderFilenameFields::session},
    {"%b", &LabRecorderFilenameFields::task},
    {"%r", &LabRecorderFilenameFields::run},
    {"%n", &LabRecorderFilenameFields::run},
    {"%a", &LabRecorderFilenameFields::acquisition},
    {"%m", &LabRecorderFilenameFields::modality},
};

void appendField(QString& command, const QString& key, const QString& value) {
    const QString sanitized = LabRecorderFilenamePolicy::sanitizedValue(value);
    if (sanitized.isEmpty()) {
        return;
    }
    command += " {" + key + ":" + sanitized + "}";
}

} // namespace

QString LabRecorderFilenamePolicy::filenameCommand(const LabRecorderFilenameFields& fields) {
    QString command = "filename";
    appendField(command, "root", fields.root);
    appendField(command, "template", fields.templ);
    appendField(command, "participant", fields.participant);
    appendField(command, "session", fields.session);
    appendField(command, "task", fields.task);
    appendField(command, "run", fields.run);
    appendField(command, "acquisition", fields.acquisition);
    appendField(command, "modality", fields.modality);
    return command;
}

QString LabRecorderFilenamePolicy::renderedFilename(const LabRecorderFilenameFields& fields) {
    QString rendered = sanitizedValue(fields.templ);
    for (const FilenameToken& token : kFilenameTokens) {
        rendered.replace(QLatin1String(token.placeholder), sanitizedValue(fields.*(token.field)));
    }
    return rendered;
}

bool LabRecorderFilenamePolicy::hasUnresolvedFilenamePlaceholders(
    const LabRecorderFilenameFields& fields) {
    const QString templ = sanitizedValue(fields.templ);
    for (const FilenameToken& token : kFilenameTokens) {
        if (templ.contains(QLatin1String(token.placeholder)) &&
            sanitizedValue(fields.*(token.field)).isEmpty()) {
            return true;
        }
    }
    return renderedFilename(fields).contains('%');
}

QStringList LabRecorderFilenamePolicy::startRecordingCommands(
    const LabRecorderFilenameFields& fields,
    bool select_all_first) {
    QStringList commands;
    if (select_all_first) {
        // Discover streams that appeared since LabRecorder's last refresh
        // before selecting them. LabRecorder's start handler refreshes again,
        // but that is too late to select newly discovered streams.
        commands.append("update");
        commands.append("select all");
    }
    commands.append(filenameCommand(fields));
    commands.append("start");
    return commands;
}

QString LabRecorderFilenamePolicy::sanitizedValue(QString value) {
    value.replace('{', '_');
    value.replace('}', '_');
    value.replace('\n', ' ');
    value.replace('\r', ' ');
    return value.trimmed();
}

QString LabRecorderFilenamePolicy::renderedFilenamePreview(
    const LabRecorderFilenameFields& fields) {
    const QString rendered = renderedFilename(fields);
    const QString root = sanitizedValue(fields.root);
    if (!root.isEmpty()) {
        return QDir::toNativeSeparators(QDir(root).filePath(rendered));
    }
    return QDir::toNativeSeparators(rendered);
}

QString LabRecorderFilenamePolicy::validationError(
    const LabRecorderFilenameFields& fields) {
    const QString root = sanitizedValue(fields.root);
    if (root.isEmpty()) {
        return "Set a study root before starting recording.";
    }

    const QFileInfo root_info(fields.root);
    if (!root_info.exists() || !root_info.isDir()) {
        return "Study root does not exist or is not a directory: " + fields.root;
    }

    if (sanitizedValue(fields.templ).isEmpty()) {
        return "Set a filename template before starting recording.";
    }

    QStringList missing_fields;
    if (sanitizedValue(fields.participant).isEmpty()) {
        missing_fields.append("participant");
    }
    if (sanitizedValue(fields.session).isEmpty()) {
        missing_fields.append("session");
    }
    if (sanitizedValue(fields.task).isEmpty()) {
        missing_fields.append("task");
    }
    if (sanitizedValue(fields.acquisition).isEmpty()) {
        missing_fields.append("acquisition");
    }
    if (sanitizedValue(fields.modality).isEmpty()) {
        missing_fields.append("modality");
    }
    if (!missing_fields.isEmpty()) {
        return "Complete recording metadata before starting: " +
               missing_fields.join(", ") + ".";
    }

    if (hasUnresolvedFilenamePlaceholders(fields)) {
        return "Resolve all filename template placeholders before starting recording.";
    }

    if (renderedFilenamePreview(fields).trimmed().isEmpty()) {
        return "Filename preview is empty; check the study root and template.";
    }

    return {};
}
