#include "notemanager.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>

static QIcon makeAppIcon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(6, 6, 52, 52), 6, 6);
    p.fillPath(path, QColor("#FFF7D1"));
    p.setClipPath(path);
    p.fillRect(QRectF(6, 6, 52, 12), QColor("#F5D94E"));
    p.setClipping(false);
    p.setPen(QPen(QColor(0, 0, 0, 60), 1.5));
    p.drawPath(path);
    p.setPen(QPen(QColor("#9A8A3A"), 2.5, Qt::SolidLine, Qt::RoundCap));
    for (int y : {28, 37, 46})
        p.drawLine(16, y, y == 46 ? 36 : 48, y);
    return QIcon(pm);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("qnotes");
    QApplication::setApplicationDisplayName("QNotes");
    QApplication::setWindowIcon(makeAppIcon());

    NoteManager manager;
    manager.start();

    return app.exec();
}
