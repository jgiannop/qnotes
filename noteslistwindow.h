#pragma once

#include <QWidget>

class NoteManager;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;

// The QNotes list window: search + cards for every note, as a list or a grid.
class NotesListWindow : public QWidget
{
    Q_OBJECT
public:
    explicit NotesListWindow(NoteManager *manager, QWidget *parent = nullptr);

    // Shows the window and gives it focus. The first time, it's placed next to anchor (a global position,
    // e.g. the tray icon; a null anchor lets the window manager place it); after that it stays where the user put it.
    void showNear(const QPoint &anchor);

public slots:
    void refresh();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setGridMode(bool grid);
    void updateCardSize();

    void openItem(QListWidgetItem *item);
    void showContextMenu(const QPoint &pos);

    NoteManager *m_manager;
    QLineEdit *m_search;
    QListWidget *m_list;
    QToolButton *m_viewBtn;
    bool m_grid = false;
    bool m_placed = false;
};
