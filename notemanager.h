#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>

#include "notewindow.h"

class NotesListWindow;
class PreferencesDialog;
class QSystemTrayIcon;
class QWidget;

// Owns all notes, the notes list, and saves/loads everything to JSON.
class NoteManager : public QObject
{
    Q_OBJECT
public:
    explicit NoteManager(QObject *parent = nullptr);
    ~NoteManager() override;

    void setupTray(); // call before start(); on GNOME/X11 while DBus is still hidden (see main.cpp)
    void start();     // load notes and show windows

    QList<NoteWindow *> notes() const { return m_notes; }
    NoteWindow *noteById(const QString &id) const;

    NoteWindow *createNote(NoteWindow *near = nullptr,
                           NoteWindow::Kind kind = NoteWindow::Kind::Text);
    void requestDelete(NoteWindow *note, QWidget *dialogParent);
    void showList();
    void toggleList(); // tray icon click
    void showPreferences();
    void applyTheme(); // light / dark preference or system setting changed

    void exportBackup(QWidget *parent);
    void importBackup(QWidget *parent);

public slots:
    void scheduleSave();
    void save();

signals:
    void notesChanged();

private:
    NoteWindow *addNote(const QString &id, NoteWindow::Kind kind);
    NoteWindow *loadNote(const QJsonObject &o, bool *migrate);
    QJsonObject noteToJson(NoteWindow *n) const; // secret values included
    QString filePath() const;
    QJsonArray loadSecrets(QJsonArray fields, bool *migrate);
    QJsonArray storeSecrets(QJsonArray fields, QSet<QString> *seen);
    void warnPlaintextSecrets(bool keychainMissing);
    NotesListWindow *listWindow();

    QList<NoteWindow *> m_notes;
    NotesListWindow *m_list = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    PreferencesDialog *m_preferences = nullptr;
    QHash<QString, QString> m_storedSecrets; // field id -> value currently in the keychain
    QSet<QString> m_unreadableSecrets;       // in the keychain but couldn't be read: never overwrite
    bool m_warnedPlaintext = false;
    bool m_warnedWriteFailed = false;
    bool m_saving = false;
    QTimer m_saveTimer;
};
