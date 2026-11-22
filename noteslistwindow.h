#pragma once

#include <QColor>
#include <QWidget>

class NoteManager;
class QLineEdit;
class QListWidget;
class QAction;
class QListWidgetItem;
class QToolButton;

// The QNotes list window: search + cards for every note, as a list or a grid.
class NotesListWindow : public QWidget
{
    Q_OBJECT
public:
    explicit NotesListWindow(NoteManager *manager, QWidget *parent = nullptr);
    ~NotesListWindow() override;

    // Shows the window and gives it focus. The first time, it's placed next to anchor (a global position,
    // e.g. the tray icon; a null anchor lets the window manager place it); after that it stays where the user put it.
    void showNear(const QPoint &anchor);

    void applyTheme(); // light / dark from the preference or the system setting

public slots:
    void refresh();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setGridMode(bool grid);
    void saveWindowGeometry();
    void updateCardSize();

    void openItem(QListWidgetItem *item);
    void showContextMenu(const QPoint &pos);
    void copyField(const QString &noteId, const QString &fieldId);

    NoteManager *m_manager;
    QLineEdit *m_search;
    QListWidget *m_list;
    QToolButton *m_viewBtn;
    QAction *m_searchIcon;
    QColor m_iconColor;
    bool m_dark = false;
    bool m_themeApplied = false;
    bool m_grid = false;
    bool m_placed = false;
};
