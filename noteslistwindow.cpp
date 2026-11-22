#include "noteslistwindow.h"
#include "notecolors.h"
#include "notemanager.h"
#include "notewindow.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSettings>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum Roles { IdRole = Qt::UserRole, ColorRole, TextRole, ModifiedRole, KindRole };

// Soft drop shadow: stacked rounded rects with fading alpha (cheap stand-in for a blur).
void paintShadow(QPainter *p, const QRectF &r, qreal radius, int spread, int alpha, qreal dy)
{
    p->setPen(Qt::NoPen);
    for (int i = spread; i >= 1; --i) {
        QColor c(60, 40, 20, alpha * (spread - i + 1) / (spread * spread / 2 + 1));
        p->setBrush(c);
        p->drawRoundedRect(r.translated(0, dy).adjusted(-i, -i, i, i), radius + i, radius + i);
    }
}

// Paints each note as a floating card: soft shadow, colour accent on the left, bold first line as title.
class NoteCardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize cardSize; // set by the window from the viewport width (list: one column, grid: several)

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return cardSize;
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        const NoteColor &c = noteColor(index.data(ColorRole).toInt());
        const QRectF r = QRectF(opt.rect).adjusted(7, 7, -7, -7);
        const bool hot = opt.state & (QStyle::State_MouseOver | QStyle::State_Selected);
        const qreal radius = 14;

        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        paintShadow(p, r, radius, hot ? 8 : 5, hot ? 34 : 22, hot ? 3 : 2);

        QPainterPath card;
        card.addRoundedRect(r, radius, radius);
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0, c.body.lighter(104));
        bg.setColorAt(1, c.body);
        p->fillPath(card, bg);

        // colour accent on the left
        p->save();
        p->setClipPath(card);
        QLinearGradient accent(r.topLeft(), r.bottomLeft());
        accent.setColorAt(0, c.title.darker(108));
        accent.setColorAt(1, c.title.darker(125));
        p->fillRect(QRectF(r.left(), r.top(), 6, r.height()), accent);
        p->restore();

        p->setPen(hot ? QPen(c.title.darker(135), 1.6) : QPen(QColor(0, 0, 0, 16), 1));
        p->setBrush(Qt::NoBrush);
        p->drawPath(card);

        const QRectF content = r.adjusted(22, 12, -16, -12);
        QColor dim = c.text;
        dim.setAlpha(140);

        // top row: kind chip (left), date (right)
        QFont small = opt.font;
        small.setPointSizeF(small.pointSizeF() * 0.82);
        p->setFont(small);
        const QFontMetricsF sfm(small);
        const QDateTime dt = index.data(ModifiedRole).toDateTime();
        const QString date = dt.date() == QDate::currentDate() ? dt.toString("HH:mm") : dt.toString("d MMM");
        p->setPen(dim);
        p->drawText(content, Qt::AlignRight | Qt::AlignTop, date);

        qreal textTop = content.top();
        const auto kind = NoteWindow::Kind(index.data(KindRole).toInt());
        if (kind == NoteWindow::Kind::KeyValue) {
            const QString chip = QStringLiteral("\U0001F511 ") + tr("Key-Value");
            const QRectF chipRect(content.left(), content.top() - 1, sfm.horizontalAdvance(chip) + 14,
                                  sfm.height() + 4);
            p->setPen(Qt::NoPen);
            QColor chipBg = c.text;
            chipBg.setAlpha(18);
            p->setBrush(chipBg);
            p->drawRoundedRect(chipRect, chipRect.height() / 2, chipRect.height() / 2);
            p->setPen(dim);
            p->drawText(chipRect, Qt::AlignCenter, chip);
            textTop = chipRect.bottom() + 6;
        }

        // title = first non-empty line, preview = the rest
        const QString text = index.data(TextRole).toString().left(600);
        QStringList lines;
        for (const QString &l : text.split('\n'))
            if (!l.trimmed().isEmpty())
                lines << l.trimmed();
        const QString title = lines.isEmpty() ? tr("Empty note") : lines.takeFirst();
        const QString preview = lines.join(QStringLiteral(" · "));

        QFont titleFont = opt.font;
        titleFont.setPointSizeF(opt.font.pointSizeF() * 1.08);
        titleFont.setWeight(QFont::DemiBold);
        const QFontMetricsF tfm(titleFont);
        const qreal dateWidth = kind == NoteWindow::Kind::KeyValue ? 0 : sfm.horizontalAdvance(date) + 12;
        p->setFont(titleFont);
        QColor titleColor = c.text;
        if (lines.isEmpty() && title == tr("Empty note"))
            titleColor = dim;
        p->setPen(titleColor);
        p->drawText(QRectF(content.left(), textTop, content.width() - dateWidth, tfm.height()),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    tfm.elidedText(title, Qt::ElideRight, content.width() - dateWidth));

        if (!preview.isEmpty()) {
            QFont body = opt.font;
            p->setFont(body);
            QColor pc = c.text;
            pc.setAlpha(175);
            p->setPen(pc);
            const QRectF pr(content.left(), textTop + tfm.height() + 4, content.width(),
                            content.bottom() - (textTop + tfm.height() + 4));
            p->setClipRect(pr);
            p->drawText(pr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, preview);
        }

        p->restore();
    }
};

// List / grid symbols for the view toggle button.
QIcon viewIcon(bool grid)
{
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#8A7A64"));
    if (grid) {
        for (int x : {8, 22})
            for (int y : {8, 22})
                p.drawRoundedRect(QRectF(x, y, 11, 11), 3, 3);
    } else {
        for (int y : {9, 18, 27})
            p.drawRoundedRect(QRectF(8, y, 24, 5), 2.5, 2.5);
    }
    return QIcon(pm);
}

// Magnifying glass for the search field, drawn so it looks the same on every platform.
QIcon searchIcon()
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#9C8F80"), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(QRectF(6, 6, 15, 15));
    p.drawLine(QPointF(19, 19), QPointF(26, 26));
    return QIcon(pm);
}

} // namespace

NotesListWindow::NotesListWindow(NoteManager *manager, QWidget *parent)
    : QWidget(parent), m_manager(manager)
{
    setWindowTitle(tr("QNotes"));
    resize(380, 600);

    setObjectName("notesList");
    setAttribute(Qt::WA_StyledBackground);

    m_search = new QLineEdit(this);
    m_search->setObjectName("search");
    m_search->setPlaceholderText(tr("Search notes"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(40);
    m_search->addAction(searchIcon(), QLineEdit::LeadingPosition);
    connect(m_search, &QLineEdit::textChanged, this, &NotesListWindow::refresh);

    m_viewBtn = new QToolButton(this);
    m_viewBtn->setObjectName("viewButton");
    m_viewBtn->setFixedSize(40, 40);
    m_viewBtn->setIconSize(QSize(20, 20));
    m_viewBtn->setCursor(Qt::PointingHandCursor);
    connect(m_viewBtn, &QToolButton::clicked, this, [this] {
        setGridMode(!m_grid);
        QSettings("qnotes", "qnotes").setValue("list/grid", m_grid);
        if (m_grid && width() < 600) // room for at least 3 columns
            resize(620, height());
    });

    auto *kvBtn = new QToolButton(this);
    kvBtn->setObjectName("kvButton");
    kvBtn->setText(QStringLiteral("\U0001F511"));
    kvBtn->setToolTip(tr("New key-value note (Ctrl+Shift+K)"));
    kvBtn->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    kvBtn->setFixedSize(40, 40);
    kvBtn->setCursor(Qt::PointingHandCursor);
    connect(kvBtn, &QToolButton::clicked, this,
            [this] { m_manager->createNote(nullptr, NoteWindow::Kind::KeyValue); });

    auto *addBtn = new QToolButton(this);
    addBtn->setObjectName("addButton");
    addBtn->setText("+");
    addBtn->setToolTip(tr("New note (Ctrl+N)"));
    addBtn->setShortcut(QKeySequence::New);
    addBtn->setFixedSize(40, 40);
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QToolButton::clicked, this, [this] { m_manager->createNote(); });

    auto *header = new QHBoxLayout;
    header->setContentsMargins(7, 0, 7, 0);
    header->setSpacing(8);
    header->addWidget(m_search, 1);
    header->addWidget(m_viewBtn);
    header->addWidget(kvBtn);
    header->addWidget(addBtn);

    // Fixed light palette, independent of the desktop theme (the note cards have their own colours anyway).
    setStyleSheet(R"(
        #notesList {
            background: qlineargradient(x1:0, y1:0, x2:0.4, y2:1, stop:0 #FDFBF8, stop:1 #F1EBE3);
        }
        #search {
            background: #FFFFFF;
            border: 1px solid #E6DED3;
            border-radius: 20px;
            padding: 0 12px 0 6px;
            font-size: 11pt;
            color: #2B2620;
            selection-background-color: #F3D46B;
            selection-color: #2B2620;
        }
        #search:focus { border: 2px solid #E8BE45; }
        #kvButton, #addButton, #viewButton {
            border-radius: 20px;
            font-size: 15pt;
        }
        #kvButton, #viewButton {
            background: #FFFFFF;
            border: 1px solid #E6DED3;
            font-size: 13pt;
        }
        #kvButton:hover, #viewButton:hover { background: #FFF8E4; border-color: #E2B93B; }
        #addButton {
            color: #FFFFFF;
            font-weight: 600;
            border: none;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #F8D35E, stop:1 #E8A82A);
        }
        #addButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #FADD7A, stop:1 #EDB33C);
        }
        #addButton:pressed, #kvButton:pressed, #viewButton:pressed { padding-top: 2px; }
        #cards { background: transparent; }
    )");

    m_list = new QListWidget(this);
    m_list->setItemDelegate(new NoteCardDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setMovement(QListView::Static);
    m_list->setResizeMode(QListView::Adjust);
    m_list->viewport()->installEventFilter(this); // re-fit the cards when the width changes
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setObjectName("cards");
    m_list->verticalScrollBar()->setStyleSheet(R"(
        QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 2px 4px 0; }
        QScrollBar::handle:vertical { background: rgba(80, 60, 30, 60); border-radius: 3px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background: rgba(80, 60, 30, 110); }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; background: none; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
    )");
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::itemActivated, this, &NotesListWindow::openItem);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &NotesListWindow::showContextMenu);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 16, 7, 6);
    layout->setSpacing(10);
    layout->addLayout(header);
    layout->addWidget(m_list, 1);

    setGridMode(QSettings("qnotes", "qnotes").value("list/grid", false).toBool());
}

void NotesListWindow::setGridMode(bool grid)
{
    m_grid = grid;
    m_list->setFlow(grid ? QListView::LeftToRight : QListView::TopToBottom);
    m_list->setWrapping(grid);
    m_viewBtn->setIcon(viewIcon(!grid)); // shows what a click switches to
    m_viewBtn->setToolTip(grid ? tr("Show as list") : tr("Show as grid"));
    updateCardSize();
}

void NotesListWindow::updateCardSize()
{
    auto *delegate = static_cast<NoteCardDelegate *>(m_list->itemDelegate());
    const int width = m_list->viewport()->width();
    QSize size(width, 116);
    if (m_grid) {
        const int columns = qMax(1, width / 190); // cards at least ~190 px wide
        size = QSize((width - 4) / columns, 180); // QListView wraps a few px before the full width
    }
    if (size == delegate->cardSize)
        return;
    delegate->cardSize = size;
    m_list->doItemsLayout();
}

bool NotesListWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list->viewport() && event->type() == QEvent::Resize)
        updateCardSize();
    return QWidget::eventFilter(watched, event);
}

void NotesListWindow::refresh()
{
    const int scroll = m_list->verticalScrollBar()->value();
    const QString filter = m_search->text().trimmed();
    const QString current = m_list->currentItem() ? m_list->currentItem()->data(IdRole).toString() : QString();

    QList<NoteWindow *> notes = m_manager->notes();
    std::sort(notes.begin(), notes.end(),
              [](NoteWindow *a, NoteWindow *b) { return a->modified() > b->modified(); });

    m_list->clear();
    for (NoteWindow *n : std::as_const(notes)) {
        const QString text = n->plainText();
        if (!filter.isEmpty() && !text.contains(filter, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(m_list);
        item->setData(IdRole, n->id());
        item->setData(ColorRole, n->colorIndex());
        item->setData(TextRole, text);
        item->setData(ModifiedRole, n->modified());
        item->setData(KindRole, int(n->kind()));
        if (n->id() == current)
            m_list->setCurrentItem(item);
    }
    m_list->verticalScrollBar()->setValue(scroll);
}

void NotesListWindow::openItem(QListWidgetItem *item)
{
    if (!item)
        return;
    if (NoteWindow *n = m_manager->noteById(item->data(IdRole).toString()))
        n->openNote();
}

void NotesListWindow::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        return;
    const QString id = item->data(IdRole).toString();

    QMenu menu(this);
    menu.addAction(tr("Open note"), this, [this, id] {
        if (NoteWindow *n = m_manager->noteById(id))
            n->openNote();
    });
    menu.addAction(tr("Delete note"), this, [this, id] {
        m_manager->requestDelete(m_manager->noteById(id), this);
    });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void NotesListWindow::showNear(const QPoint &anchor)
{
    if (!m_placed && !anchor.isNull()) {
        m_placed = true;
        QScreen *screen = QGuiApplication::screenAt(anchor);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const QRect avail = screen->availableGeometry();
        const QSize frame = frameGeometry().size().expandedTo(size()); // title bar not known before first show

        // Open towards the middle of the screen from the anchor (below a top panel, above a bottom one).
        QPoint pos(anchor.x() - frame.width() / 2, anchor.y() + 8);
        if (anchor.y() > avail.center().y())
            pos.setY(anchor.y() - frame.height() - 8);
        pos.setX(qBound(avail.left() + 8, pos.x(), avail.right() - frame.width() - 8));
        pos.setY(qBound(avail.top() + 8, pos.y(), avail.bottom() - frame.height() - 8));
        move(pos);
    }

    refresh();
    if (isMinimized())
        showNormal();
    show();
    raise();
    activateWindow();
    m_search->setFocus();
}
