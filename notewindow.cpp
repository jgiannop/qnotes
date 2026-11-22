#include "notewindow.h"
#include "notecolors.h"
#include "keyvalueeditor.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QShortcut>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWindow>

namespace {

constexpr int kResizeMargin = 6;

Qt::CursorShape cursorFor(Qt::Edges e)
{
    if (e == (Qt::LeftEdge | Qt::TopEdge) || e == (Qt::RightEdge | Qt::BottomEdge))
        return Qt::SizeFDiagCursor;
    if (e == (Qt::RightEdge | Qt::TopEdge) || e == (Qt::LeftEdge | Qt::BottomEdge))
        return Qt::SizeBDiagCursor;
    if (e & (Qt::LeftEdge | Qt::RightEdge))
        return Qt::SizeHorCursor;
    if (e & (Qt::TopEdge | Qt::BottomEdge))
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

// Starts a native move/resize so dragging works on X11, Wayland, Windows and macOS.
void startDrag(NoteWindow *note, Qt::Edges edges)
{
    QWindow *w = note->windowHandle();
    if (!w)
        return;
    if (edges)
        w->startSystemResize(edges);
    else
        w->startSystemMove();
}

// The coloured bar at the top of the note: drag to move.
class TitleBar : public QWidget
{
public:
    explicit TitleBar(NoteWindow *note) : QWidget(note), m_note(note)
    {
        setObjectName("titleBar");
        setAttribute(Qt::WA_StyledBackground);
        setMouseTracking(true);
        setFixedHeight(34);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return QWidget::mousePressEvent(e);
        startDrag(m_note, m_note->edgesAt(mapTo(m_note, e->position().toPoint())));
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        setCursor(cursorFor(m_note->edgesAt(mapTo(m_note, e->position().toPoint()))));
        QWidget::mouseMoveEvent(e);
    }

private:
    NoteWindow *m_note;
};

QToolButton *makeButton(QWidget *parent, const QString &text, const QString &tip,
                        int pointSize, bool checkable = false)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setFocusPolicy(Qt::NoFocus); // never steal focus from the editor
    b->setCursor(Qt::ArrowCursor);
    b->setAutoRaise(true);
    QFont f = b->font();
    f.setPointSize(pointSize);
    b->setFont(f);
    return b;
}

} // namespace

NoteWindow::NoteWindow(const QString &id, Kind kind, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), m_id(id), m_kind(kind),
      m_modified(QDateTime::currentDateTime())
{
    setAttribute(Qt::WA_StyledBackground);
    setMouseTracking(true);
    setWindowTitle(tr("Sticky Note"));
    setMinimumSize(220, 160);
    if (kind == Kind::KeyValue)
        resize(380, 300);
    else
        resize(320, 320);

    buildUi();
    applyColor();
    updateChrome();
}

void NoteWindow::buildUi()
{
    // --- Title bar: [+]  .........  [...] [x]
    m_titleBar = new TitleBar(this);
    auto *titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(kResizeMargin, 0, kResizeMargin, 0);
    titleLayout->setSpacing(0);

    m_newBtn = makeButton(m_titleBar, QStringLiteral("+"), tr("New note (Ctrl+N)"), 16);
    m_menuBtn = makeButton(m_titleBar, QStringLiteral("⋯"), tr("Menu"), 14);
    m_closeBtn = makeButton(m_titleBar, QStringLiteral("✕"), tr("Close note"), 11);
    titleLayout->addWidget(m_newBtn);
    titleLayout->addStretch();
    titleLayout->addWidget(m_menuBtn);
    titleLayout->addWidget(m_closeBtn);

    connect(m_newBtn, &QToolButton::clicked, this, [this] { emit newNoteRequested(Kind::Text); });
    connect(m_menuBtn, &QToolButton::clicked, this, &NoteWindow::showMenu);
    connect(m_closeBtn, &QToolButton::clicked, this, &QWidget::close);

    QWidget *content = m_kind == Kind::KeyValue ? buildKeyValueArea() : buildTextArea();

    // --- Layout. Side/bottom margins double as resize handles.
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_titleBar);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(kResizeMargin, 0, kResizeMargin, kResizeMargin);
    body->setSpacing(0);
    body->addWidget(content, 1);
    body->addWidget(m_toolbar);
    root->addLayout(body);

    // --- Window shortcuts
    auto *newSc = new QShortcut(QKeySequence::New, this);
    connect(newSc, &QShortcut::activated, this, [this] { emit newNoteRequested(Kind::Text); });
    auto *delSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
    connect(delSc, &QShortcut::activated, this, &NoteWindow::deleteRequested);
    auto *kvSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), this);
    connect(kvSc, &QShortcut::activated, this, [this] { emit newNoteRequested(Kind::KeyValue); });
}

QWidget *NoteWindow::buildKeyValueArea()
{
    m_kv = new KeyValueEditor(this);
    connect(m_kv, &KeyValueEditor::contentChanged, this, [this] {
        if (m_loading)
            return;
        m_modified = QDateTime::currentDateTime();
        emit changed();
    });

    m_toolbar = new QWidget(this);
    auto *tb = new QHBoxLayout(m_toolbar);
    tb->setContentsMargins(4, 2, 4, 2);
    tb->setSpacing(2);

    auto *add = makeButton(m_toolbar, tr("+ Add field"), tr("Add a key = value field (Ctrl+Enter)"), 10);
    add->setFixedHeight(28);
    add->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    connect(add, &QToolButton::clicked, m_kv, &KeyValueEditor::addField);

    auto *lockAll = makeButton(m_toolbar, tr("\U0001F512 Hide all"), tr("Hide every revealed secret value"), 10);
    lockAll->setFixedHeight(28);
    connect(lockAll, &QToolButton::clicked, m_kv, &KeyValueEditor::lockAll);

    tb->addWidget(add);
    tb->addWidget(lockAll);
    tb->addStretch();
    return m_kv;
}

QWidget *NoteWindow::buildTextArea()
{
    // --- Editor
    m_edit = new QTextEdit(this);
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setAcceptRichText(true);
    m_edit->setPlaceholderText(tr("Take a note..."));
    m_edit->document()->setDocumentMargin(10);
    QFont ef = m_edit->font();
    ef.setPointSize(11);
    m_edit->setFont(ef);

    connect(m_edit, &QTextEdit::textChanged, this, [this] {
        if (m_loading)
            return;
        m_modified = QDateTime::currentDateTime();
        emit changed();
    });
    connect(m_edit, &QTextEdit::currentCharFormatChanged, this, &NoteWindow::updateFormatButtons);
    connect(m_edit, &QTextEdit::cursorPositionChanged, this, &NoteWindow::updateFormatButtons);

    // --- Formatting toolbar: B I U S •
    m_toolbar = new QWidget(this);
    auto *tb = new QHBoxLayout(m_toolbar);
    tb->setContentsMargins(4, 2, 4, 2);
    tb->setSpacing(2);

    m_boldBtn = makeButton(m_toolbar, "B", tr("Bold (Ctrl+B)"), 11, true);
    m_italicBtn = makeButton(m_toolbar, "I", tr("Italic (Ctrl+I)"), 11, true);
    m_underlineBtn = makeButton(m_toolbar, "U", tr("Underline (Ctrl+U)"), 11, true);
    m_strikeBtn = makeButton(m_toolbar, "ab", tr("Strikethrough (Ctrl+T)"), 11, true);
    m_bulletBtn = makeButton(m_toolbar, QStringLiteral("☰"), tr("Toggle bullets (Ctrl+Shift+L)"), 11, true);

    QFont f;
    f = m_boldBtn->font(); f.setBold(true); m_boldBtn->setFont(f);
    f = m_italicBtn->font(); f.setItalic(true); m_italicBtn->setFont(f);
    f = m_underlineBtn->font(); f.setUnderline(true); m_underlineBtn->setFont(f);
    f = m_strikeBtn->font(); f.setStrikeOut(true); m_strikeBtn->setFont(f);

    m_boldBtn->setShortcut(QKeySequence::Bold);
    m_italicBtn->setShortcut(QKeySequence::Italic);
    m_underlineBtn->setShortcut(QKeySequence::Underline);
    m_strikeBtn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    m_bulletBtn->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));

    for (auto *b : {m_boldBtn, m_italicBtn, m_underlineBtn, m_strikeBtn, m_bulletBtn}) {
        b->setFixedSize(32, 30);
        tb->addWidget(b);
    }
    tb->addStretch();

    connect(m_boldBtn, &QToolButton::clicked, this, [this](bool on) {
        QTextCharFormat fmt;
        fmt.setFontWeight(on ? QFont::Bold : QFont::Normal);
        mergeFormat(fmt);
    });
    connect(m_italicBtn, &QToolButton::clicked, this, [this](bool on) {
        QTextCharFormat fmt;
        fmt.setFontItalic(on);
        mergeFormat(fmt);
    });
    connect(m_underlineBtn, &QToolButton::clicked, this, [this](bool on) {
        QTextCharFormat fmt;
        fmt.setFontUnderline(on);
        mergeFormat(fmt);
    });
    connect(m_strikeBtn, &QToolButton::clicked, this, [this](bool on) {
        QTextCharFormat fmt;
        fmt.setFontStrikeOut(on);
        mergeFormat(fmt);
    });
    connect(m_bulletBtn, &QToolButton::clicked, this, &NoteWindow::toggleBullets);

    return m_edit;
}

void NoteWindow::applyColor()
{
    const NoteColor &c = noteColor(m_color);
    const bool dark = c.text.lightness() > 128;
    const QString hover = dark ? "rgba(255,255,255,40)" : "rgba(0,0,0,25)";
    const QString pressed = dark ? "rgba(255,255,255,70)" : "rgba(0,0,0,45)";
    const QString handle = dark ? "rgba(255,255,255,80)" : "rgba(0,0,0,60)";
    const QString grid = dark ? "rgba(255,255,255,50)" : "rgba(0,0,0,35)";
    const QString selection = c.title.darker(dark ? 60 : 115).name();

    setStyleSheet(QString(
        "NoteWindow { background: %1; }"
        "#titleBar { background: %2; }"
        "QToolButton { border: none; border-radius: 3px; background: transparent;"
        "              color: %3; padding: 2px 8px; }"
        "QToolButton:hover { background: %4; }"
        "QToolButton:pressed, QToolButton:checked { background: %5; }"
        "QTextEdit { background: transparent; border: none; color: %3;"
        "            selection-background-color: %6; selection-color: %3; }"
        "QScrollBar:vertical { width: 8px; background: transparent; margin: 0; }"
        "QScrollBar::handle:vertical { background: %7; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QScrollBar:horizontal { height: 8px; background: transparent; margin: 0; }"
        "QScrollBar::handle:horizontal { background: %7; border-radius: 4px; min-width: 24px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"
        // key-value fields
        "QScrollArea, QScrollArea > QWidget, #kvArea { background: transparent; border: none; }"
        "#kvArea QToolButton { padding: 0; }"
        "#kvArea QLineEdit { background: %10; color: %3; border: 1px solid %8; border-radius: 3px;"
        "                    padding: 3px 5px; selection-background-color: %6; selection-color: %3; }"
        "#kvArea QLineEdit:focus { border: 1px solid %9; }")
        .arg(c.body.name(), c.title.name(), c.text.name(), hover, pressed, selection, handle, grid,
             c.title.darker(dark ? 50 : 160).name())
        .arg(dark ? "rgba(0,0,0,60)" : "rgba(255,255,255,140)"));

    if (m_edit) {
        QPalette pal = m_edit->palette();
        QColor placeholder = c.text;
        placeholder.setAlpha(130);
        pal.setColor(QPalette::PlaceholderText, placeholder);
        pal.setColor(QPalette::Text, c.text);
        m_edit->setPalette(pal);
    }
}

void NoteWindow::setColorIndex(int index)
{
    index = qBound(0, index, int(noteColors().size()) - 1);
    if (index == m_color)
        return;
    m_color = index;
    applyColor();
    emit changed();
}

QString NoteWindow::html() const { return m_edit ? m_edit->toHtml() : QString(); }

QString NoteWindow::plainText() const
{
    if (m_edit)
        return m_edit->toPlainText();
    return m_kv->toPlainText();
}

QJsonArray NoteWindow::keyValueData() const
{
    return m_kv ? m_kv->toJson() : QJsonArray();
}

void NoteWindow::setKeyValueData(const QJsonArray &data)
{
    if (!m_kv)
        return;
    m_loading = true;
    m_kv->fromJson(data);
    m_loading = false;
}

void NoteWindow::setHtml(const QString &html)
{
    if (!m_edit)
        return;
    m_loading = true;
    m_edit->setHtml(html);
    m_loading = false;
}

void NoteWindow::openNote()
{
    show();
    raise();
    activateWindow();
    focusEditor();
}

void NoteWindow::focusEditor()
{
    if (m_kv) {
        m_kv->focusFirst();
        return;
    }
    m_edit->setFocus();
    QTextCursor cur = m_edit->textCursor();
    cur.movePosition(QTextCursor::End);
    m_edit->setTextCursor(cur);
}

void NoteWindow::mergeFormat(const QTextCharFormat &format)
{
    m_edit->mergeCurrentCharFormat(format);
    m_edit->setFocus();
}

void NoteWindow::toggleBullets()
{
    QTextCursor cur = m_edit->textCursor();
    cur.beginEditBlock();
    if (QTextList *list = cur.currentList()) {
        QTextDocument *doc = m_edit->document();
        QTextBlock block = doc->findBlock(cur.selectionStart());
        const QTextBlock last = doc->findBlock(cur.selectionEnd());
        while (block.isValid()) {
            list->remove(block);
            QTextCursor bc(block);
            QTextBlockFormat bf = bc.blockFormat();
            bf.setIndent(0);
            bc.setBlockFormat(bf);
            if (block == last)
                break;
            block = block.next();
        }
    } else {
        QTextListFormat lf;
        lf.setStyle(QTextListFormat::ListDisc);
        cur.createList(lf);
    }
    cur.endEditBlock();
    m_edit->setFocus();
    updateFormatButtons();
}

void NoteWindow::updateFormatButtons()
{
    const QTextCharFormat f = m_edit->currentCharFormat();
    m_boldBtn->setChecked(f.fontWeight() >= QFont::Bold);
    m_italicBtn->setChecked(f.fontItalic());
    m_underlineBtn->setChecked(f.fontUnderline());
    m_strikeBtn->setChecked(f.fontStrikeOut());
    m_bulletBtn->setChecked(m_edit->textCursor().currentList() != nullptr);
}

void NoteWindow::showMenu()
{
    QMenu menu; // no parent: don't inherit the note's style sheet

    // Row of colour swatches, like Windows Sticky Notes.
    auto *swatches = new QWidget;
    auto *row = new QHBoxLayout(swatches);
    row->setContentsMargins(8, 8, 8, 8);
    row->setSpacing(4);
    const auto &colors = noteColors();
    for (int i = 0; i < colors.size(); ++i) {
        const NoteColor &c = colors.at(i);
        auto *b = new QToolButton(swatches);
        b->setFixedSize(36, 36);
        b->setToolTip(c.name);
        b->setFocusPolicy(Qt::NoFocus);
        if (i == m_color)
            b->setText(QStringLiteral("✓"));
        b->setStyleSheet(QString("QToolButton { background: %1; color: %2; border: %3; font-size: 16px; }"
                                 "QToolButton:hover { border: 2px solid #777; }")
                             .arg(c.title.name(), c.text.name(),
                                  i == m_color ? "2px solid #444" : "1px solid rgba(0,0,0,40)"));
        connect(b, &QToolButton::clicked, &menu, [this, i, &menu] {
            setColorIndex(i);
            menu.close();
        });
        row->addWidget(b);
    }
    auto *swatchAction = new QWidgetAction(&menu);
    swatchAction->setDefaultWidget(swatches);
    menu.addAction(swatchAction);
    menu.addSeparator();
    menu.addAction(tr("New note"), this, [this] { emit newNoteRequested(Kind::Text); })
        ->setShortcut(QKeySequence::New);
    menu.addAction(tr("New key-value note"), this, [this] { emit newNoteRequested(Kind::KeyValue); })
        ->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    menu.addSeparator();
    menu.addAction(tr("Notes list"), this, &NoteWindow::showListRequested);
    menu.addAction(tr("Delete note"), this, &NoteWindow::deleteRequested);

    menu.exec(m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height())));
}

void NoteWindow::updateChrome()
{
    const bool active = isActiveWindow();
    const bool showButtons = active || m_hover;
    m_newBtn->setVisible(showButtons);
    m_menuBtn->setVisible(showButtons);
    m_closeBtn->setVisible(showButtons);
    m_toolbar->setVisible(active);
}

Qt::Edges NoteWindow::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    if (pos.x() < kResizeMargin)
        edges |= Qt::LeftEdge;
    if (pos.x() >= width() - kResizeMargin)
        edges |= Qt::RightEdge;
    if (pos.y() < kResizeMargin)
        edges |= Qt::TopEdge;
    if (pos.y() >= height() - kResizeMargin)
        edges |= Qt::BottomEdge;
    return edges;
}

void NoteWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        startDrag(this, edgesAt(event->position().toPoint()));
    else
        QWidget::mousePressEvent(event);
}

void NoteWindow::mouseMoveEvent(QMouseEvent *event)
{
    setCursor(cursorFor(edgesAt(event->position().toPoint())));
    QWidget::mouseMoveEvent(event);
}

void NoteWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange)
        updateChrome();
    QWidget::changeEvent(event);
}

void NoteWindow::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    updateChrome();
    QWidget::enterEvent(event);
}

void NoteWindow::leaveEvent(QEvent *event)
{
    m_hover = false;
    unsetCursor();
    updateChrome();
    QWidget::leaveEvent(event);
}

void NoteWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    emit changed();
}

void NoteWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    emit changed();
}

void NoteWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    emit changed();
}

void NoteWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_kv)
        m_kv->lockAll(); // closing a note always hides its secrets again
    emit changed();
}
