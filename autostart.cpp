#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

#ifdef Q_OS_WIN
const char kRunKey[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#else
QString desktopFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart/qnotes.desktop";
}

QString execLine()
{
    // Installed from the package: use the name, so the entry survives updates. Otherwise this exact binary.
    const QString self = QCoreApplication::applicationFilePath();
    if (self == QStandardPaths::findExecutable("qnotes"))
        return QStringLiteral("qnotes");
    QString quoted = self;
    quoted.replace('\\', "\\\\").replace('"', "\\\"");
    return '"' + quoted + '"';
}
#endif

} // namespace

bool Autostart::isSupported()
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

bool Autostart::isEnabled()
{
#ifdef Q_OS_WIN
    return QSettings(kRunKey, QSettings::NativeFormat).contains("QNotes");
#else
    return QFile::exists(desktopFile());
#endif
}

bool Autostart::setEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue("QNotes", '"' + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + '"');
    else
        run.remove("QNotes");
    run.sync();
    return run.status() == QSettings::NoError;
#else
    const QString path = desktopFile();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QStringLiteral("[Desktop Entry]\n"
                              "Type=Application\n"
                              "Name=QNotes\n"
                              "Comment=Sticky notes\n"
                              "Exec=%1\n"
                              "Icon=qnotes\n"
                              "Terminal=false\n"
                              "X-GNOME-Autostart-enabled=true\n")
                   .arg(execLine())
                   .toUtf8());
    return file.commit();
#endif
}
