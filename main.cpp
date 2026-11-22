#include "notemanager.h"

#include <QApplication>
#include <QIcon>

static QIcon makeAppIcon()
{
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/qnotes-%1.png").arg(size), QSize(size, size));
    return icon;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("qnotes");
    QApplication::setApplicationDisplayName("QNotes");
    QApplication::setApplicationVersion(QStringLiteral(QNOTES_VERSION));
    // Matches qnotes.desktop, so GNOME's dock shows our icon and groups the note windows.
    QGuiApplication::setDesktopFileName("qnotes");
    QApplication::setWindowIcon(makeAppIcon());

    NoteManager manager;
    manager.start();

    return app.exec();
}
