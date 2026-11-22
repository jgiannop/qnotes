#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class KeyValueEditor;
class QTextEdit;
class QToolButton;
class QTextCharFormat;

// A single frameless sticky note window. Holds either rich text or key = value fields.
class NoteWindow : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { Text, KeyValue };

    explicit NoteWindow(const QString &id, Kind kind = Kind::Text, QWidget *parent = nullptr);

    QString id() const { return m_id; }
    Kind kind() const { return m_kind; }

    int colorIndex() const { return m_color; }
    void setColorIndex(int index);

    QString html() const;
    void setHtml(const QString &html);
    QString plainText() const;

    QJsonArray keyValueData() const;
    void setKeyValueData(const QJsonArray &data);

    QDateTime modified() const { return m_modified; }
    void setModified(const QDateTime &dt) { m_modified = dt; }

    void openNote();
    void focusEditor();

    // Which window edges are under pos (window coordinates) - used for resizing.
    Qt::Edges edgesAt(const QPoint &pos) const;

signals:
    void changed();
    void newNoteRequested(NoteWindow::Kind kind);
    void deleteRequested();
    void showListRequested();

protected:
    void changeEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void buildUi();
    QWidget *buildTextArea();
    QWidget *buildKeyValueArea();
    void applyColor();
    void updateChrome();
    void updateFormatButtons();
    void mergeFormat(const QTextCharFormat &format);
    void toggleBullets();
    void showMenu();

    QString m_id;
    Kind m_kind;
    int m_color = 0;
    QDateTime m_modified;
    bool m_loading = false;
    bool m_hover = false;

    QWidget *m_titleBar = nullptr;
    QWidget *m_toolbar = nullptr;
    QTextEdit *m_edit = nullptr;     // Text notes only
    KeyValueEditor *m_kv = nullptr;  // Key-value notes only
    QToolButton *m_newBtn = nullptr;
    QToolButton *m_menuBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
    QToolButton *m_boldBtn = nullptr;
    QToolButton *m_italicBtn = nullptr;
    QToolButton *m_underlineBtn = nullptr;
    QToolButton *m_strikeBtn = nullptr;
    QToolButton *m_bulletBtn = nullptr;
};
