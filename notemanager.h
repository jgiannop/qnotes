#pragma once

#include <QList>
#include <QObject>
#include <QTimer>

#include "notewindow.h"

class NotesListWindow;
class QSystemTrayIcon;
class QWidget;

// Owns all notes, the notes list, and saves/loads everything to JSON.
class NoteManager : public QObject
{
    Q_OBJECT
public:
    explicit NoteManager(QObject *parent = nullptr);
    ~NoteManager() override;

    void start(); // load notes and show windows

    QList<NoteWindow *> notes() const { return m_notes; }
    NoteWindow *noteById(const QString &id) const;

    NoteWindow *createNote(NoteWindow *near = nullptr,
                           NoteWindow::Kind kind = NoteWindow::Kind::Text);
    void requestDelete(NoteWindow *note, QWidget *dialogParent);
    void showList();
    void toggleList(); // tray icon click

public slots:
    void scheduleSave();
    void save();

signals:
    void notesChanged();

private:
    NoteWindow *addNote(const QString &id, NoteWindow::Kind kind);
    QString filePath() const;
    void setupTray();
    NotesListWindow *listWindow();

    QList<NoteWindow *> m_notes;
    NotesListWindow *m_list = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QTimer m_saveTimer;
};
