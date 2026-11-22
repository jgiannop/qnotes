#include "notemanager.h"
#include "noteslistwindow.h"
#include "notewindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QUuid>

// Table notes were removed: convert their cells to text lines ("a | b | c") so nothing is lost.
static QString legacyTableToHtml(const QJsonObject &table)
{
    QStringList lines;
    for (const QJsonValue &row : table.value("cells").toArray()) {
        QStringList cells;
        for (const QJsonValue &cell : row.toArray())
            cells << cell.toString();
        while (!cells.isEmpty() && cells.last().isEmpty())
            cells.removeLast();
        if (!cells.isEmpty())
            lines << cells.join(" | ").toHtmlEscaped();
    }
    return "<p>" + lines.join("<br>") + "</p>";
}

NoteManager::NoteManager(QObject *parent) : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &NoteManager::save);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &NoteManager::save);
}

NoteManager::~NoteManager()
{
    for (NoteWindow *n : std::as_const(m_notes)) {
        disconnect(n, nullptr, this, nullptr);
        delete n;
    }
    delete m_list;
}

QString NoteManager::filePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notes.json";
}

NoteWindow *NoteManager::noteById(const QString &id) const
{
    for (NoteWindow *n : m_notes)
        if (n->id() == id)
            return n;
    return nullptr;
}

NoteWindow *NoteManager::addNote(const QString &id, NoteWindow::Kind kind)
{
    auto *note = new NoteWindow(id, kind);
    m_notes.append(note);

    connect(note, &NoteWindow::changed, this, [this] {
        scheduleSave();
        emit notesChanged();
    });
    connect(note, &NoteWindow::newNoteRequested, this,
            [this, note](NoteWindow::Kind kind) { createNote(note, kind); });
    connect(note, &NoteWindow::deleteRequested, this, [this, note] { requestDelete(note, note); });
    connect(note, &NoteWindow::showListRequested, this, &NoteManager::showList);
    return note;
}

void NoteManager::start()
{
    QFile file(filePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(file.readAll()).object().value("notes").toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            QString id = o.value("id").toString();
            if (id.isEmpty())
                id = QUuid::createUuid().toString(QUuid::WithoutBraces);

            const QString type = o.value("type").toString();
            const NoteWindow::Kind kind = type == "keyvalue" ? NoteWindow::Kind::KeyValue
                                                             : NoteWindow::Kind::Text;
            NoteWindow *n = addNote(id, kind);
            n->setColorIndex(o.value("color").toInt());
            if (type == "table")
                n->setHtml(legacyTableToHtml(o.value("table").toObject()));
            else if (kind == NoteWindow::Kind::KeyValue)
                n->setKeyValueData(o.value("fields").toArray());
            else
                n->setHtml(o.value("html").toString());

            const QRect g(o.value("x").toInt(), o.value("y").toInt(),
                          o.value("w").toInt(320), o.value("h").toInt(320));
            n->resize(g.size());
            if (QGuiApplication::screenAt(g.center()))
                n->move(g.topLeft());

            const QDateTime dt = QDateTime::fromString(o.value("modified").toString(), Qt::ISODateWithMs);
            n->setModified(dt.isValid() ? dt : QDateTime::currentDateTime());

            if (o.value("open").toBool(true))
                n->show();
        }
    }

    if (m_notes.isEmpty())
        createNote();

    showList();
}

NoteWindow *NoteManager::createNote(NoteWindow *near, NoteWindow::Kind kind)
{
    NoteWindow *n = addNote(QUuid::createUuid().toString(QUuid::WithoutBraces), kind);
    n->setModified(QDateTime::currentDateTime());
    if (near) {
        // Place the new note beside the one it was created from (like Windows).
        QPoint pos = near->pos() + QPoint(near->width() + 12, 0);
        QScreen *screen = near->screen();
        if (screen && !screen->availableGeometry().contains(QRect(pos, n->size())))
            pos = near->pos() + QPoint(32, 32);
        n->move(pos);
    }
    n->openNote();
    scheduleSave();
    emit notesChanged();
    return n;
}

void NoteManager::requestDelete(NoteWindow *note, QWidget *dialogParent)
{
    if (!note)
        return;
    const auto answer = QMessageBox::question(dialogParent, tr("Delete note"),
                                              tr("This will delete the note. Are you sure?"),
                                              QMessageBox::Yes | QMessageBox::Cancel,
                                              QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    m_notes.removeOne(note);
    disconnect(note, nullptr, this, nullptr);
    note->hide();
    note->deleteLater();
    save();
    emit notesChanged();
}

void NoteManager::showList()
{
    if (!m_list) {
        m_list = new NotesListWindow(this);
        connect(this, &NoteManager::notesChanged, m_list, &NotesListWindow::refresh);
    }
    m_list->refresh();
    m_list->show();
    m_list->raise();
    m_list->activateWindow();
}

void NoteManager::scheduleSave()
{
    m_saveTimer.start();
}

void NoteManager::save()
{
    m_saveTimer.stop();

    QJsonArray arr;
    for (NoteWindow *n : std::as_const(m_notes)) {
        QJsonObject o;
        o["id"] = n->id();
        if (n->kind() == NoteWindow::Kind::KeyValue) {
            o["type"] = "keyvalue";
            o["fields"] = n->keyValueData();
        } else {
            o["type"] = "text";
            o["html"] = n->html();
        }
        o["color"] = n->colorIndex();
        o["x"] = n->x();
        o["y"] = n->y();
        o["w"] = n->width();
        o["h"] = n->height();
        o["open"] = n->isVisible();
        o["modified"] = n->modified().toString(Qt::ISODateWithMs);
        arr.append(o);
    }

    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("Could not save notes to %s", qPrintable(path));
        return;
    }
    file.write(QJsonDocument(QJsonObject{{"notes", arr}}).toJson());
    file.commit();
}
