#include "theme.h"

#include <QGuiApplication>
#include <QPalette>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>

namespace {

QSettings settings()
{
    return QSettings("qnotes", "qnotes");
}

#ifdef Q_OS_LINUX
QString gsettingsGet(const QString &key)
{
    const QString exe = QStandardPaths::findExecutable("gsettings");
    if (exe.isEmpty())
        return QString();
    QProcess p;
    p.start(exe, {"get", "org.gnome.desktop.interface", key});
    if (!p.waitForFinished(1000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}
#endif

} // namespace

Theme::Mode Theme::mode()
{
    const QString m = settings().value("appearance/theme", "system").toString();
    return m == "light" ? Mode::Light : m == "dark" ? Mode::Dark : Mode::System;
}

void Theme::setMode(Mode mode)
{
    settings().setValue("appearance/theme",
                        mode == Mode::Light ? "light" : mode == Mode::Dark ? "dark" : "system");
}

bool Theme::systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
#ifdef Q_OS_WIN
    const QSettings personalize(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        QSettings::NativeFormat);
    return personalize.value("AppsUseLightTheme", 1).toInt() == 0;
#endif
#ifdef Q_OS_LINUX
    // Qt 6.4 doesn't know the GNOME setting (and our session-bus workaround rules out the portal): ask gsettings.
    if (!qgetenv("XDG_CURRENT_DESKTOP").contains("KDE")) {
        const QString scheme = gsettingsGet("color-scheme");
        if (scheme.contains("prefer-dark"))
            return true;
        if (scheme.contains("prefer-light"))
            return false;
        if (!scheme.isEmpty()) // "default": older setups pick dark through the GTK theme name
            return gsettingsGet("gtk-theme").contains("dark", Qt::CaseInsensitive);
    }
#endif
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128; // e.g. KDE
}

bool Theme::isDark()
{
    switch (mode()) {
    case Mode::Light:
        return false;
    case Mode::Dark:
        return true;
    case Mode::System:
        break;
    }
    return systemPrefersDark();
}
