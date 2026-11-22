#include "notemanager.h"
#include "backup.h"
#include "noteslistwindow.h"
#include "preferencesdialog.h"
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
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
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
    delete m_preferences;
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
                warnPlaintextSecrets(true);
            continue; // nothing secret to protect / no keychain: keep it in the JSON
        }
        const auto stored = m_storedSecrets.constFind(id);
        if (stored == m_storedSecrets.cend() || *stored != value) {
            QString error;
            if (!SecretStore::write(id, value, &error)) {
                qWarning("QNotes: could not write secret to the keychain: %s", qPrintable(error));
                warnPlaintextSecrets(false);
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

void NoteManager::warnPlaintextSecrets(bool keychainMissing)
{
    if (!keychainMissing) {
        // The keychain exists but refused (e.g. locked and the unlock prompt was cancelled): the values stay in
        // the notes file for now and the next save tries again. Tell the user once per session.
        if (m_warnedWriteFailed)
            return;
        m_warnedWriteFailed = true;
        QMessageBox::warning(nullptr, tr("QNotes"),
                             tr("Secret values could not be saved in the system keychain, so they are kept "
                                "unencrypted in the notes file for now. QNotes will try again on the next change."));
        return;
    }
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

NoteWindow *NoteManager::loadNote(const QJsonObject &o, bool *migrate)
{
    QString id = o.value("id").toString();
    if (id.isEmpty())
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    const QString type = o.value("type").toString();
    const NoteWindow::Kind kind = type == "keyvalue" ? NoteWindow::Kind::KeyValue : NoteWindow::Kind::Text;
    NoteWindow *n = addNote(id, kind);
    n->setColorIndex(o.value("color").toInt());
    if (type == "table")
        n->setHtml(legacyTableToHtml(o.value("table").toObject()));
    else if (kind == NoteWindow::Kind::KeyValue)
        n->setKeyValueData(loadSecrets(o.value("fields").toArray(), migrate));
    else
        n->setHtml(o.value("html").toString());

    const QRect g(o.value("x").toInt(), o.value("y").toInt(), o.value("w").toInt(320), o.value("h").toInt(320));
    n->resize(g.size());
    if (QGuiApplication::screenAt(g.center()))
        n->move(g.topLeft());

    const QDateTime dt = QDateTime::fromString(o.value("modified").toString(), Qt::ISODateWithMs);
    n->setModified(dt.isValid() ? dt : QDateTime::currentDateTime());

    if (o.value("open").toBool(true))
        n->show();
    return n;
}

QJsonObject NoteManager::noteToJson(NoteWindow *n) const
{
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
    return o;
}

void NoteManager::start()
{
    QFile file(filePath());
    bool migrate = false;
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(file.readAll()).object().value("notes").toArray();
        for (const QJsonValue &v : arr)
            loadNote(v.toObject(), &migrate);
    }

    if (m_notes.isEmpty())
        createNote();
    if (migrate)
        save(); // move plaintext secrets into the keychain right away

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
    menu->addAction(tr("Preferences…"), this, &NoteManager::showPreferences);
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
        QJsonObject o = noteToJson(n);
        if (o.contains("fields"))
            o["fields"] = storeSecrets(o.value("fields").toArray(), &secretsInUse);
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

void NoteManager::showPreferences()
{
    if (!m_preferences)
        m_preferences = new PreferencesDialog(this); // no parent: its own window, not on top of a note
    m_preferences->show();
    m_preferences->raise();
    m_preferences->activateWindow();
}

void NoteManager::applyTheme()
{
    if (m_list)
        m_list->applyTheme();
}

// ---------------------------------------------------------------- backup

namespace {

// Asks for the backup password (twice when creating a backup). Empty string = cancelled.
QString askBackupPassword(QWidget *parent, bool create, const QString &error = QString())
{
    QDialog dialog(parent);
    dialog.setWindowTitle(create ? NoteManager::tr("Export notes") : NoteManager::tr("Import notes"));
    auto *text = new QLabel(create ? NoteManager::tr("Choose a password for the backup. You will need it to "
                                                     "import the backup; it can't be recovered.")
                                   : NoteManager::tr("Enter the password of the backup:"),
                            &dialog);
    text->setWordWrap(true);
    auto *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    password->setMinimumWidth(280);
    auto *confirm = new QLineEdit(&dialog);
    confirm->setEchoMode(QLineEdit::Password);
    auto *errorLabel = new QLabel(error, &dialog);
    errorLabel->setStyleSheet("color: #c42b1c;");
    errorLabel->setVisible(!error.isEmpty());
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(create ? NoteManager::tr("Export") : NoteManager::tr("Import"));

    auto *form = new QFormLayout;
    form->addRow(NoteManager::tr("Password:"), password);
    if (create)
        form->addRow(NoteManager::tr("Repeat:"), confirm);
    else
        confirm->hide();
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(text);
    layout->addLayout(form);
    layout->addWidget(errorLabel);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QString problem;
        if (password->text().isEmpty())
            problem = NoteManager::tr("Enter a password.");
        else if (create && password->text().size() < 8)
            problem = NoteManager::tr("Use at least 8 characters.");
        else if (create && password->text() != confirm->text())
            problem = NoteManager::tr("The passwords don't match.");
        if (problem.isEmpty()) {
            dialog.accept();
            return;
        }
        errorLabel->setText(problem);
        errorLabel->show();
    });
    password->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    return password->text();
}

// The password-to-key step takes ~2 s on purpose: run it off the UI thread.
template<typename F>
void runWithBusyCursor(F work)
{
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QThread *worker = QThread::create(work);
    QEventLoop loop;
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    worker->wait();
    delete worker;
    QApplication::restoreOverrideCursor();
}

} // namespace

void NoteManager::exportBackup(QWidget *parent)
{
    if (!m_unreadableSecrets.isEmpty()
        && QMessageBox::warning(parent, tr("Export notes"),
                                tr("%n secret value(s) could not be read from the keychain and will be missing "
                                   "from the backup. Export anyway?",
                                   nullptr, int(m_unreadableSecrets.size())),
                                QMessageBox::Yes | QMessageBox::Cancel)
               != QMessageBox::Yes)
        return;

    const QString suggested = QDir::home().filePath(
        QStringLiteral("qnotes-backup-%1.qnotes").arg(QDate::currentDate().toString(Qt::ISODate)));
    QString path = QFileDialog::getSaveFileName(parent, tr("Export notes"), suggested,
                                                tr("QNotes backup (*.qnotes)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".qnotes", Qt::CaseInsensitive))
        path += ".qnotes";

    const QString password = askBackupPassword(parent, true);
    if (password.isEmpty())
        return;

    QJsonArray notes;
    for (NoteWindow *n : std::as_const(m_notes))
        notes.append(noteToJson(n));
    QByteArray plain = QJsonDocument(QJsonObject{{"notes", notes}}).toJson(QJsonDocument::Compact);

    QByteArray encrypted;
    runWithBusyCursor([&] { encrypted = Backup::encrypt(plain, password); });
    plain.fill('\0');

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(encrypted) != encrypted.size() || !file.commit()) {
        QMessageBox::warning(parent, tr("Export notes"), tr("Could not write %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    QMessageBox::information(parent, tr("Export notes"),
                             tr("Exported %n note(s) to\n%1", nullptr, int(m_notes.size()))
                                 .arg(QDir::toNativeSeparators(path)));
}

void NoteManager::importBackup(QWidget *parent)
{
    const QString path = QFileDialog::getOpenFileName(parent, tr("Import notes"), QDir::homePath(),
                                                      tr("QNotes backup (*.qnotes);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(parent, tr("Import notes"), tr("Could not read %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    const QByteArray data = file.readAll();

    QByteArray plain;
    QString error;
    for (;;) {
        const QString password = askBackupPassword(parent, false, error);
        if (password.isEmpty())
            return;
        bool ok = false;
        runWithBusyCursor([&] { ok = Backup::decrypt(data, password, &plain, &error); });
        if (ok)
            break;
        if (!QJsonDocument::fromJson(data).object().value("format").isString()) { // not a backup at all
            QMessageBox::warning(parent, tr("Import notes"), error);
            return;
        }
    }

    const QJsonArray notes = QJsonDocument::fromJson(plain).object().value("notes").toArray();
    plain.fill('\0');
    QList<QJsonObject> fresh;
    for (const QJsonValue &v : notes) {
        const QJsonObject o = v.toObject();
        if (!noteById(o.value("id").toString()))
            fresh.append(o);
    }
    const int skipped = int(notes.size() - fresh.size());
    if (fresh.isEmpty()) {
        QMessageBox::information(parent, tr("Import notes"),
                                 tr("All %n note(s) in this backup are already here.", nullptr, skipped));
        return;
    }
    QString question = tr("Import %n note(s)?", nullptr, int(fresh.size()));
    if (skipped > 0)
        question += ' ' + tr("%n note(s) that are already here will be skipped.", nullptr, skipped);
    if (QMessageBox::question(parent, tr("Import notes"), question, QMessageBox::Yes | QMessageBox::Cancel)
        != QMessageBox::Yes)
        return;

    bool migrate = false;
    for (QJsonObject o : std::as_const(fresh)) {
        o["open"] = false; // don't pop up a window for every imported note
        loadNote(o, &migrate);
    }
    save(); // writes the imported secrets to the keychain
    emit notesChanged();
    QMessageBox::information(parent, tr("Import notes"),
                             tr("Imported %n note(s). They are in the notes list.", nullptr, int(fresh.size())));
}
