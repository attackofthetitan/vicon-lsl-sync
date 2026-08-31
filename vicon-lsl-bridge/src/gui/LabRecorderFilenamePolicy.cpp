#include "gui/LabRecorderFilenamePolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTemporaryFile>

#include <algorithm>

namespace {

constexpr int kMaxFindNextRun = 1000;

struct FilenameToken {
    const char* placeholder;
    QString LabRecorderFilenameFields::*field;
};

struct RecordingField {
    const char* command_key;
    const char* label;
    QString LabRecorderFilenameFields::*value;
    bool required;
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

const RecordingField kRecordingFields[] = {
    {"root", "study root", &LabRecorderFilenameFields::root, false},
    {"template", "filename template", &LabRecorderFilenameFields::templ, false},
    {"participant", "participant", &LabRecorderFilenameFields::participant, true},
    {"session", "session", &LabRecorderFilenameFields::session, true},
    {"task", "task", &LabRecorderFilenameFields::task, true},
    {"run", "run", &LabRecorderFilenameFields::run, false},
    {"acquisition", "acquisition", &LabRecorderFilenameFields::acquisition, true},
    {"modality", "modality", &LabRecorderFilenameFields::modality, true},
};

void appendField(QString& command, const QString& key, const QString& value) {
    const QString s = LabRecorderFilenamePolicy::sanitizedValue(value);
    if (!s.isEmpty()) command += " {" + key + ":" + s + "}";
}

bool containsProtocolBreakingChar(const QString& v) {
    return v.contains('{') || v.contains('}') || v.contains('\n') || v.contains('\r');
}

void addIssue(RecordingPathResult& res, RecordingPathIssueLevel lvl,
              const QString& field, const QString& msg, const QString& act) {
    res.issues.push_back({lvl, field, msg, act});
}

bool pathIsWithin(const QString& root, const QString& path) {
    QString norm_root = QDir::fromNativeSeparators(QDir::cleanPath(QDir(root).absolutePath()));
    QString norm_path = QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    norm_root = norm_root.toLower();
    norm_path = norm_path.toLower();
#endif
    if (norm_path == norm_root) return true;
    if (!norm_root.endsWith('/')) norm_root += '/';
    return norm_path.startsWith(norm_root);
}

QString nearestExistingDirectory(QString path) {
    QFileInfo info(path);
    if (!info.isDir()) path = info.absolutePath();
    QDir dir(path);
    while (!dir.exists()) {
        if (!dir.cdUp()) return {};
    }
    return dir.absolutePath();
}

bool isReservedWindowsName(const QString& component) {
    QString base = component;
    const int dot = base.indexOf('.');
    if (dot >= 0) base = base.left(dot);
    base = base.trimmed().toUpper();
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|CLOCK\\$|COM[1-9]|LPT[1-9])$");
    return reserved.match(base).hasMatch();
}

QStringList relativeComponents(const QString& relative_path) {
    return QDir::fromNativeSeparators(relative_path).split('/', Qt::SkipEmptyParts);
}

QString fieldNameForProtocolValue(const LabRecorderFilenameFields& fields,
                                  const QString& value) {
    for (const RecordingField& field : kRecordingFields) {
        if (value == fields.*(field.value)) return QLatin1String(field.label);
    }
    return "recording field";
}

} // namespace

bool RecordingPathResult::valid() const {
    return std::none_of(issues.begin(), issues.end(), [](const auto& i) {
        return i.level == RecordingPathIssueLevel::Error;
    });
}

bool RecordingPathResult::hasWarnings() const {
    return std::any_of(issues.begin(), issues.end(), [](const auto& i) {
        return i.level == RecordingPathIssueLevel::Warning;
    });
}

QString RecordingPathResult::firstError() const {
    for (const auto& i : issues) {
        if (i.level == RecordingPathIssueLevel::Error) {
            return i.message + (i.corrective_action.isEmpty() ? QString() : " " + i.corrective_action);
        }
    }
    return {};
}

QString RecordingPathResult::summary() const {
    QStringList lines;
    for (const auto& i : issues) {
        lines.push_back((i.level == RecordingPathIssueLevel::Error ? "Error" : "Warning") +
                        QString(" [%1]: %2").arg(i.field, i.message) +
                        (i.corrective_action.isEmpty() ? QString() : " " + i.corrective_action));
    }
    return lines.isEmpty() ? "Recording destination is valid." : lines.join('\n');
}

QString LabRecorderFilenamePolicy::filenameCommand(const LabRecorderFilenameFields& fields) {
    QString command = "filename";
    for (const RecordingField& field : kRecordingFields) {
        appendField(command, QLatin1String(field.command_key), fields.*(field.value));
    }
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
        // Refresh first so newly visible streams are selected.
        commands.append("update");
        commands.append("select all");
    }
    commands.append(filenameCommand(fields));
    commands.append("start");
    return commands;
}

QString LabRecorderFilenamePolicy::sanitizedValue(QString value) {
    value.replace('{', '_').replace('}', '_').replace('\n', ' ').replace('\r', ' ');
    return value.trimmed();
}

RecordingPathResult LabRecorderFilenamePolicy::validate(
    const LabRecorderFilenameFields& fields,
    const RecordingPathValidationOptions& options) {
    RecordingPathResult result;
    result.normalized_fields = fields;
    result.normalized_fields.root = QDir::cleanPath(fields.root.trimmed());
    const QString normalized_template =
        QDir::fromNativeSeparators(fields.templ.trimmed());
    result.normalized_fields.templ = normalized_template.isEmpty()
        ? QString() : QDir::cleanPath(normalized_template);
    result.normalized_fields.participant = fields.participant.trimmed();
    result.normalized_fields.session = fields.session.trimmed();
    result.normalized_fields.task = fields.task.trimmed();
    result.normalized_fields.run = fields.run.trimmed();
    result.normalized_fields.acquisition = fields.acquisition.trimmed();
    result.normalized_fields.modality = fields.modality.trimmed();

    for (const RecordingField& recording_field : kRecordingFields) {
        const QString& value = fields.*(recording_field.value);
        if (containsProtocolBreakingChar(value)) {
            const QString field = fieldNameForProtocolValue(fields, value);
            addIssue(result, RecordingPathIssueLevel::Error, field,
                     field + " contains a brace or line break that cannot be sent safely.",
                     "Remove '{', '}', and line breaks.");
        }
    }

    if (result.normalized_fields.root.isEmpty()) {
        addIssue(result, RecordingPathIssueLevel::Error, "study root",
                 "Set a study root before starting recording.",
                 "Choose an existing writable directory.");
        return result;
    }
    const QFileInfo root_info(result.normalized_fields.root);
    if (!root_info.exists() || !root_info.isDir()) {
        addIssue(result, RecordingPathIssueLevel::Error, "study root",
                 "Study root does not exist or is not a directory: " +
                     result.normalized_fields.root,
                 "Choose an existing directory.");
        return result;
    }
    result.normalized_fields.root = QDir::toNativeSeparators(root_info.canonicalFilePath());

    if (result.normalized_fields.templ.isEmpty()) {
        addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                 "Set a filename template before starting recording.",
                 "Use a relative path ending in .xdf.");
        return result;
    }
    if (!result.normalized_fields.templ.endsWith(".xdf", Qt::CaseInsensitive)) {
        result.normalized_fields.templ += ".xdf";
    }

    for (const RecordingField& field : kRecordingFields) {
        if (!field.required) continue;
        const QString name = QLatin1String(field.label);
        const QString& value = result.normalized_fields.*(field.value);
        if (value.isEmpty()) {
            addIssue(result, RecordingPathIssueLevel::Error, name,
                     "The " + name + " value is empty.",
                     "Enter a value before recording.");
        }
        const QString& original = fields.*(field.value);
        if (original.endsWith(' ') || original.endsWith('.')) {
            addIssue(result, RecordingPathIssueLevel::Error, name,
                     "The " + name +
                         " value ends with a space or period.",
                     "Remove the trailing character.");
        }
        if (value == "." || value == ".." ||
            value.contains(QRegularExpression("[<>:\"/\\\\|?*]"))) {
            addIssue(result, RecordingPathIssueLevel::Error, name,
                     "The " + name +
                         " value contains a path separator or reserved filename character.",
                     "Use letters, numbers, spaces, hyphens, or underscores.");
        }
        if (isReservedWindowsName(value)) {
            addIssue(result, RecordingPathIssueLevel::Error, name,
                     "The " + name +
                         " value is a Windows-reserved filename name: " +
                         value,
                     "Choose a different value.");
        }
    }
    if (result.normalized_fields.run.isEmpty()) {
        addIssue(result, RecordingPathIssueLevel::Error, "run",
                 "The run value is empty.", "Enter a positive run number.");
    } else {
        bool run_ok = false;
        const int run = result.normalized_fields.run.toInt(&run_ok);
        if (!run_ok || run <= 0) {
            addIssue(result, RecordingPathIssueLevel::Error, "run",
                     "The run value is not a positive integer.",
                     "Enter a run number of 1 or greater.");
        }
    }

    if (hasUnresolvedFilenamePlaceholders(result.normalized_fields)) {
        addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                 "The filename contains an unresolved or unknown '%' placeholder.",
                 "Fill the referenced field or remove the placeholder.");
    }

    result.relative_path = QDir::cleanPath(renderedFilename(result.normalized_fields));
    if (result.relative_path == "." || result.relative_path.trimmed().isEmpty()) {
        addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                 "The resolved filename is empty.", "Use a template that produces a file name.");
        return result;
    }
    if (QDir::isAbsolutePath(result.relative_path)) {
        addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                 "The filename template resolves to an absolute path.",
                 "Use a path relative to the study root.");
    }

    for (const QString& component : relativeComponents(result.relative_path)) {
        if (component == "..") {
            addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                     "The filename traverses outside the study root.",
                     "Remove '..' path components.");
        }
        if (component.endsWith(' ') || component.endsWith('.')) {
            addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                     "A path component ends with a space or period: " + component,
                     "Remove the trailing character.");
        }
        if (component.contains(QRegularExpression("[<>:\"|?*]"))) {
            addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                     "A path component contains a Windows-reserved character: " + component,
                     "Remove < > : \" | ? and * characters.");
        }
        if (isReservedWindowsName(component)) {
            addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                     "A path component uses a Windows-reserved name: " + component,
                     "Choose a different participant, session, task, or template value.");
        }
    }

    result.absolute_path = QDir::toNativeSeparators(QDir::cleanPath(
        QDir(result.normalized_fields.root).absoluteFilePath(result.relative_path)));
    if (!options.allow_outside_study_root &&
        !pathIsWithin(result.normalized_fields.root, result.absolute_path)) {
        addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                 "The resolved destination is outside the study root.",
                 "Remove absolute or parent-directory path components.");
    }

    const QString existing_parent = nearestExistingDirectory(result.absolute_path);
    const QString canonical_parent = QFileInfo(existing_parent).canonicalFilePath();
    if (!options.allow_outside_study_root && !canonical_parent.isEmpty() &&
        !pathIsWithin(result.normalized_fields.root, canonical_parent)) {
        addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                 "A destination directory resolves through a link outside the study root.",
                 "Choose a directory physically contained by the study root.");
    }

    if (options.practical_path_length > 0 &&
        result.absolute_path.size() > options.practical_path_length) {
        addIssue(result, RecordingPathIssueLevel::Error, "filename template",
                 "The resolved path is " + QString::number(result.absolute_path.size()) +
                     " characters, above the configured practical limit of " +
                     QString::number(options.practical_path_length) + ".",
                 "Shorten the study folder, recording details, or file template.");
    }

    const QFileInfo destination(result.absolute_path);
    if (destination.exists()) {
        if (destination.isDir()) {
            addIssue(result, RecordingPathIssueLevel::Error, "destination",
                     "The recording destination already exists as a directory.",
                     "Choose a different run or filename.");
        } else if (!options.allow_overwrite) {
            addIssue(result, RecordingPathIssueLevel::Error, "destination",
                     "The recording file already exists: " + result.absolute_path,
                     "Confirm overwrite explicitly or use Find next run.");
        } else {
            addIssue(result, RecordingPathIssueLevel::Warning, "destination",
                     "The existing recording will be overwritten.",
                     "Verify that this is the intended file.");
        }
    }

    QString writable_directory = destination.absolutePath();
    if (options.create_parent_directories && !QDir(writable_directory).exists()) {
        if (!QDir().mkpath(writable_directory)) {
            addIssue(result, RecordingPathIssueLevel::Error, "destination directory",
                     "The recording directory could not be created: " + writable_directory,
                     "Check the study-root permissions and path.");
        }
    }
    if (!QDir(writable_directory).exists()) {
        writable_directory = existing_parent;
    }
    if (options.verify_write_access && QDir(writable_directory).exists()) {
        QTemporaryFile probe(QDir(writable_directory).filePath(".vicon-lsl-writecheck-XXXXXX"));
        probe.setAutoRemove(true);
        if (!probe.open()) {
            addIssue(result, RecordingPathIssueLevel::Error, "destination directory",
                     "The destination directory is not writable: " + writable_directory,
                     "Choose a writable study root or correct its permissions.");
        }
    }

    if (options.verify_storage) {
        QStorageInfo storage(writable_directory);
        if (storage.isValid() && storage.isReady()) {
            result.available_storage_bytes = storage.bytesAvailable();
            if (options.storage_warning_bytes > 0 &&
                result.available_storage_bytes >= 0 &&
                result.available_storage_bytes < options.storage_warning_bytes) {
                addIssue(result, RecordingPathIssueLevel::Warning, "available storage",
                         "Only " + QString::number(
                             static_cast<double>(result.available_storage_bytes) /
                                 (1024.0 * 1024.0 * 1024.0), 'f', 1) +
                             " GiB is available.",
                         "Free storage or lower the configured warning threshold deliberately.");
            }
        } else {
            addIssue(result, RecordingPathIssueLevel::Warning, "available storage",
                     "Available storage could not be determined.",
                     "Confirm free space before a long recording.");
        }
    }
    return result;
}

int LabRecorderFilenamePolicy::findNextRun(
    const LabRecorderFilenameFields& fields,
    int current_run,
    const RecordingPathValidationOptions& options) {
    const int first = (std::max)(1, current_run);
    RecordingPathValidationOptions search_options = options;
    search_options.allow_overwrite = true;
    search_options.verify_write_access = false;
    search_options.verify_storage = false;
    search_options.create_parent_directories = false;
    const int last = (std::min)(1000000, first + kMaxFindNextRun);
    for (int run = first; run < last; ++run) {
        LabRecorderFilenameFields candidate = fields;
        candidate.run = QString::number(run);
        const RecordingPathResult result = validate(candidate, search_options);
        if (!result.absolute_path.isEmpty() && !QFileInfo::exists(result.absolute_path) &&
            std::none_of(result.issues.begin(), result.issues.end(),
                         [](const RecordingPathIssue& issue) {
                             return issue.level == RecordingPathIssueLevel::Error &&
                                    issue.field != "destination";
                         })) {
            return run;
        }
    }
    return -1;
}
