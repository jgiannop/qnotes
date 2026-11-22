#include "notemanager.h"
#include "noteslistwindow.h"
#include "secretstore.h"
#include "notewindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCursor>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSettings>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>
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
    if (m_tray)
        delete m_tray->contextMenu();
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

// Fills in secret values from the keychain. Secrets still stored in plaintext (older versions, or no
// keychain at the time) are kept and *migrate is set, so the next save moves them into the keychain.
QJsonArray NoteManager::loadSecrets(QJsonArray fields, bool *migrate)
{
    for (int i = 0; i < fields.size(); ++i) {
        QJsonObject o = fields.at(i).toObject();
        if (!o.value("secret").toBool())
            continue;
        const QString id = o.value("id").toString();
        if (o.contains("value")) {
            if (!o.value("value").toString().isEmpty() && SecretStore::isAvailable())
                *migrate = true;
            continue;
        }
        QString value, error;
        if (SecretStore::isAvailable() && SecretStore::read(id, &value, &error)) {
            m_storedSecrets.insert(id, value);
            o["value"] = value;
            fields[i] = o;
        } else {
            qWarning("QNotes: could not read secret %s from the keychain: %s", qPrintable(id),
                     SecretStore::isAvailable() ? qPrintable(error) : "no keychain available");
            m_unreadableSecrets.insert(id);
        }
    }
    return fields;
}

// Moves secret values into the keychain (writing only changed ones) and strips them from the JSON.
// Without a keychain, or if writing fails, the value stays in the JSON (plaintext) as before.
QJsonArray NoteManager::storeSecrets(QJsonArray fields, QSet<QString> *seen)
{
    for (int i = 0; i < fields.size(); ++i) {
        QJsonObject o = fields.at(i).toObject();
        if (!o.value("secret").toBool())
            continue;
        const QString id = o.value("id").toString();
        const QString value = o.value("value").toString();
        seen->insert(id);

        if (m_unreadableSecrets.contains(id) && value.isEmpty()) {
            o.remove("value"); // keep the keychain entry untouched; we never saw its value
            fields[i] = o;
            continue;
        }
        if (value.isEmpty() || !SecretStore::isAvailable()) {
            if (!value.isEmpty())
                warnPlaintextSecrets();
            continue; // nothing secret to protect / no keychain: keep it in the JSON
        }
        const auto stored = m_storedSecrets.constFind(id);
        if (stored == m_storedSecrets.cend() || *stored != value) {
            QString error;
            if (!SecretStore::write(id, value, &error)) {
                qWarning("QNotes: could not write secret to the keychain: %s", qPrintable(error));
                warnPlaintextSecrets();
                continue;
            }
            m_storedSecrets.insert(id, value);
            m_unreadableSecrets.remove(id);
        }
        o.remove("value");
        fields[i] = o;
    }
    return fields;
}

void NoteManager::warnPlaintextSecrets()
{
    if (m_warnedPlaintext)
        return;
    m_warnedPlaintext = true;
    QSettings settings("qnotes", "qnotes");
    if (settings.value("warnedPlaintextSecrets").toBool())
        return; // tell the user once, not on every start
    settings.setValue("warnedPlaintextSecrets", true);
    QMessageBox::warning(nullptr, tr("QNotes"),
                         tr("No system keychain was found (such as GNOME Keyring or KWallet), so secret values "
                            "are saved unencrypted in the notes file. They are still hidden in QNotes."));
}

void NoteManager::start()
{
    QFile file(filePath());
    bool migrate = false;
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
                n->setKeyValueData(loadSecrets(o.value("fields").toArray(), &migrate));
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
    if (migrate)
        save(); // move plaintext secrets into the keychain right away

    setupTray();
    if (!m_tray)
        showList(); // no tray: the list window is the app's main window, as before
}

void NoteManager::setupTray()
{
    // On GNOME/X11, main() makes Qt pick an XEmbed tray icon here instead of a DBus one (see main.cpp).
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
    m_tray->setToolTip(QApplication::applicationDisplayName());

    auto *menu = new QMenu; // no parent: popup menus must not inherit a note's style sheet
    menu->addAction(tr("Show notes"), this, &NoteManager::showList);
    menu->addAction(tr("New note"), this, [this] { createNote(); });
    menu->addAction(tr("New key-value note"), this,
                    [this] { createNote(nullptr, NoteWindow::Kind::KeyValue); });
    menu->addSeparator();
    menu->addAction(tr("Quit QNotes"), qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        // Trigger = left click (on GNOME's DBus tray: double-click). Right click shows the context menu.
        if (reason == QSystemTrayIcon::Trigger)
            toggleList();
    });
    m_tray->show();

    // The app keeps running in the tray with every window closed.
    QApplication::setQuitOnLastWindowClosed(false);
}

NotesListWindow *NoteManager::listWindow()
{
    if (!m_list) {
        m_list = new NotesListWindow(this);
        connect(this, &NoteManager::notesChanged, m_list, &NotesListWindow::refresh);
    }
    return m_list;
}

void NoteManager::toggleList()
{
    NotesListWindow *list = listWindow();
    if (list->isVisible() && !list->isMinimized() && list->isActiveWindow())
        list->hide();
    else
        showList(); // also raises it if it's behind other windows
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
    // First time: open next to the tray icon. Its position isn't known for DBus tray items: use the mouse.
    QPoint anchor;
    if (m_tray)
        anchor = m_tray->geometry().isEmpty() ? QCursor::pos() : m_tray->geometry().center();
    listWindow()->showNear(anchor);
}

void NoteManager::scheduleSave()
{
    m_saveTimer.start();
}

void NoteManager::save()
{
    // Keychain calls run a local event loop: don't start a second save from inside it.
    if (m_saving) {
        scheduleSave();
        return;
    }
    m_saving = true;
    const auto done = qScopeGuard([this] { m_saving = false; });
    m_saveTimer.stop();

    QJsonArray arr;
    QSet<QString> secretsInUse;
    for (NoteWindow *n : std::as_const(m_notes)) {
        QJsonObject o;
        o["id"] = n->id();
        if (n->kind() == NoteWindow::Kind::KeyValue) {
            o["type"] = "keyvalue";
            o["fields"] = storeSecrets(n->keyValueData(), &secretsInUse);
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
    const bool written = [&] {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning("Could not save notes to %s", qPrintable(path));
            return false;
        }
        file.write(QJsonDocument(QJsonObject{{"notes", arr}}).toJson());
        return file.commit();
    }();

    // Delete keychain entries of secrets that are gone (field/note deleted or no longer secret), but only
    // once the notes file no longer refers to them.
    if (!written)
        return;
    for (auto it = m_storedSecrets.begin(); it != m_storedSecrets.end();) {
        if (secretsInUse.contains(it.key())) {
            ++it;
            continue;
        }
        SecretStore::remove(it.key());
        it = m_storedSecrets.erase(it);
    }
    for (const QString &id : std::as_const(m_unreadableSecrets))
        if (!secretsInUse.contains(id))
            SecretStore::remove(id);
    m_unreadableSecrets.intersect(secretsInUse);
}
