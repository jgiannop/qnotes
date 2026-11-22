#pragma once

#include <QWidget>

class NoteManager;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

// The QNotes list window: search + cards for every note.
class NotesListWindow : public QWidget
{
    Q_OBJECT
public:
    explicit NotesListWindow(NoteManager *manager, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    void openItem(QListWidgetItem *item);
    void showContextMenu(const QPoint &pos);

    NoteManager *m_manager;
    QLineEdit *m_search;
    QListWidget *m_list;
};
