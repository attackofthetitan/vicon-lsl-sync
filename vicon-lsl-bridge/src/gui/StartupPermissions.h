#pragma once

#include <QThread>
#include <QStringList>

namespace vicon_lsl::gui {

// Exercises the same network and folder access as a session before controls
// become available. OS permission decisions remain with the user.
class StartupPermissions : public QThread {
    Q_OBJECT
public:
    StartupPermissions(QString recording_root, QString recorder_executable,
                       QObject* parent = nullptr);
    ~StartupPermissions() override;

    static QStringList foldersToCheck(const QString& recording_root);
    static QStringList recorderProbeArguments(const QString& unused_output_path);

signals:
    void progress(QString message);
    void warning(QString message);

protected:
    void run() override;

private:
    QString recording_root_;
    QString recorder_executable_;
};

} // namespace vicon_lsl::gui
