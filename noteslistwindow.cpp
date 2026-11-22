#include "noteslistwindow.h"
#include "notecolors.h"
#include "notemanager.h"
#include "notewindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum Roles { IdRole = Qt::UserRole, ColorRole, TextRole, ModifiedRole, KindRole };

// Paints each note as a small coloured card, like the Windows notes list.
class NoteCardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &) const override
    {
        return QSize(opt.rect.width(), 118);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        const NoteColor &c = noteColor(index.data(ColorRole).toInt());
        const QRectF r = QRectF(opt.rect).adjusted(10, 5, -10, -5);

        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        QPainterPath card;
        card.addRoundedRect(r, 4, 4);
        p->fillPath(card, c.body);

        // coloured strip at the top
        p->save();
        p->setClipPath(card);
        p->fillRect(QRectF(r.left(), r.top(), r.width(), 5), c.title);
        p->restore();

        const bool hot = opt.state & (QStyle::State_MouseOver | QStyle::State_Selected);
        p->setPen(QPen(hot ? c.title.darker(140) : QColor(0, 0, 0, 30), hot ? 1.5 : 1));
        p->drawPath(card);

        // date, top right
        const QDateTime dt = index.data(ModifiedRole).toDateTime();
        const QString date = dt.date() == QDate::currentDate() ? dt.toString("HH:mm")
                                                               : dt.toString("d MMM");
        QColor dim = c.text;
        dim.setAlpha(150);
        QFont small = opt.font;
        small.setPointSizeF(small.pointSizeF() * 0.85);
        p->setFont(small);
        p->setPen(dim);
        p->drawText(r.adjusted(12, 10, -12, 0), Qt::AlignRight | Qt::AlignTop, date);
        const auto kind = NoteWindow::Kind(index.data(KindRole).toInt());
        if (kind == NoteWindow::Kind::KeyValue)
            p->drawText(r.adjusted(12, 10, -12, 0), Qt::AlignLeft | Qt::AlignTop,
                        QStringLiteral("\U0001F511 Key-Value"));

        // preview text
        QString text = index.data(TextRole).toString().left(400).trimmed();
        QFont body = opt.font;
        body.setPointSizeF(body.pointSizeF() * 1.05);
        p->setFont(body);
        p->setPen(c.text);
        p->setClipRect(r.adjusted(12, 30, -12, -10));
        p->drawText(r.adjusted(12, 30, -12, -10), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);

        p->restore();
    }
};

} // namespace

NotesListWindow::NotesListWindow(NoteManager *manager, QWidget *parent)
    : QWidget(parent), m_manager(manager)
{
    setWindowTitle(tr("QNotes"));
    resize(360, 560);

    auto *title = new QLabel(tr("QNotes"), this);
    QFont tf = title->font();
    tf.setPointSize(16);
    tf.setBold(true);
    title->setFont(tf);

    auto *addBtn = new QToolButton(this);
    addBtn->setText("+");
    addBtn->setToolTip(tr("New note (Ctrl+N)"));
    addBtn->setAutoRaise(true);
    addBtn->setShortcut(QKeySequence::New);
    QFont af = addBtn->font();
    af.setPointSize(18);
    addBtn->setFont(af);
    connect(addBtn, &QToolButton::clicked, this, [this] { m_manager->createNote(); });


    auto *header = new QHBoxLayout;
    header->addWidget(title);
    header->addStretch();
    auto *kvBtn = new QToolButton(this);
    kvBtn->setText(QStringLiteral("\U0001F511"));
    kvBtn->setToolTip(tr("New key-value note (Ctrl+Shift+K)"));
    kvBtn->setAutoRaise(true);
    kvBtn->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    QFont kf = af;
    kf.setPointSize(14);
    kvBtn->setFont(kf);
    connect(kvBtn, &QToolButton::clicked, this,
            [this] { m_manager->createNote(nullptr, NoteWindow::Kind::KeyValue); });

    header->addWidget(kvBtn);
    header->addWidget(addBtn);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search..."));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(32);
    connect(m_search, &QLineEdit::textChanged, this, &NotesListWindow::refresh);

    m_list = new QListWidget(this);
    m_list->setItemDelegate(new NoteCardDelegate(m_list));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setStyleSheet("QListWidget { background: transparent; }");
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::itemActivated, this, &NotesListWindow::openItem);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &NotesListWindow::showContextMenu);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->addLayout(header);
    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
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
