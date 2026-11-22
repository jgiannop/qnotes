#include "notemanager.h"
#include "singleinstance.h"

#include <QApplication>
#include <QIcon>

#ifdef HAVE_XCB
#include <xcb/xcb.h>
#include <cstdlib>
#endif

static QIcon makeAppIcon()
{
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/qnotes-%1.png").arg(size), QSize(size, size));
    return icon;
}

// Is there an X11 (XEmbed) system tray, e.g. GNOME's AppIndicator extension with its legacy tray enabled?
// Must work before QApplication exists, so it opens its own short X connection.
static bool xembedTrayAvailable()
{
#ifdef HAVE_XCB
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    if (!platform.isEmpty() ? !platform.startsWith("xcb") : qgetenv("XDG_SESSION_TYPE") == "wayland")
        return false; // Qt won't run on X11
    int screen = 0;
    xcb_connection_t *c = xcb_connect(nullptr, &screen);
    bool available = false;
    if (!xcb_connection_has_error(c)) {
        const QByteArray name = "_NET_SYSTEM_TRAY_S" + QByteArray::number(screen);
        if (auto *atom = xcb_intern_atom_reply(c, xcb_intern_atom(c, 1, name.size(), name.constData()), nullptr)) {
            if (atom->atom != XCB_ATOM_NONE) {
                if (auto *owner = xcb_get_selection_owner_reply(c, xcb_get_selection_owner(c, atom->atom), nullptr)) {
                    available = owner->owner != XCB_WINDOW_NONE;
                    free(owner);
                }
            }
            free(atom);
        }
    }
    xcb_disconnect(c);
    return available;
#else
    return false;
#endif
}

int main(int argc, char *argv[])
{
    // Ubuntu's AppIndicator extension opens a DBus (StatusNotifierItem) tray item's menu on a single left click
    // and only reports double-clicks to the app. Qt always prefers that DBus tray and only falls back to an
    // XEmbed tray icon (which gets the real left/right clicks) when it can't reach the session bus. So with an
    // X11 tray available, hide the session bus from Qt until the tray icon exists.
    // Side effect: Qt caches the failed connection, so this process has no session bus. QNotes doesn't use
    // DBus itself; only accessibility (screen readers) is lost.
    // GNOME only: KDE's tray handles single clicks fine, and KWallet (the keychain) needs Qt's DBus there.
    const QByteArray sessionBus = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    const bool forceXEmbedTray = qgetenv("XDG_CURRENT_DESKTOP").contains("GNOME") && xembedTrayAvailable();
    if (forceXEmbedTray)
        qputenv("DBUS_SESSION_BUS_ADDRESS", "disabled:");

    QApplication app(argc, argv);
    QApplication::setApplicationName("qnotes");
    QApplication::setApplicationDisplayName("QNotes");
    QApplication::setApplicationVersion(QStringLiteral(QNOTES_VERSION));
    // Matches qnotes.desktop, so GNOME's dock shows our icon and groups the note windows.
    QGuiApplication::setDesktopFileName("qnotes");
    QApplication::setWindowIcon(makeAppIcon());

    SingleInstance instance;
    if (!instance.start())
        return 0; // the running QNotes shows its notes list instead

    NoteManager manager;
    QObject::connect(&instance, &SingleInstance::showRequested, &manager, &NoteManager::showList);

    // Tray first: it must be created while the session bus is still hidden. Restore the bus before
    // anything touches the keychain (loading notes reads secrets, migration writes them).
    manager.setupTray();
    if (forceXEmbedTray)
        qputenv("DBUS_SESSION_BUS_ADDRESS", sessionBus);

    manager.start();

    return app.exec();
}
