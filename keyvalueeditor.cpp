#include "keyvalueeditor.h"
#include "systemauth.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QUuid>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace {
constexpr int kRelockMs = 30 * 1000;         // a revealed secret hides itself again after 30 s
constexpr int kClipboardClearMs = 30 * 1000; // copied secrets are wiped from the clipboard
const QString kMask = QStringLiteral("••••••••");
} // namespace

// One "key | value | 👁 ⧉ 🔒 ✕" line.
class KeyValueRow : public QWidget
{
public:
    KeyValueRow(KeyValueEditor *editor, const QString &id, const QString &key, const QString &value, bool secret)
        : QWidget(editor->m_container), m_editor(editor),
          m_id(id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id), m_secret(secret)
    {
        m_key = new QLineEdit(key, this);
        m_key->setPlaceholderText(QObject::tr("Key"));
        QFont kf = m_key->font();
        kf.setBold(true);
        m_key->setFont(kf);

        m_value = new QLineEdit(value, this);
        m_value->setPlaceholderText(QObject::tr("Value"));

        m_eye = makeButton(QStringLiteral("\U0001F441"), QObject::tr("Reveal value (asks for your password)"));
        m_copy = makeButton(QStringLiteral("⧉"), QObject::tr("Copy value"));
        m_lock = makeButton(QString(), QString());
        m_lock->setCheckable(true);
        m_delete = makeButton(QStringLiteral("✕"), QObject::tr("Delete field"));

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(m_key, 2);
        layout->addWidget(m_value, 3);
        layout->addWidget(m_eye);
        layout->addWidget(m_copy);
        layout->addWidget(m_lock);
        layout->addWidget(m_delete);

        m_relock.setSingleShot(true);
        m_relock.setInterval(kRelockMs);
        QObject::connect(&m_relock, &QTimer::timeout, this, [this] { lock(); });

        QObject::connect(m_key, &QLineEdit::textEdited, this, [this] { emit m_editor->contentChanged(); });
        QObject::connect(m_value, &QLineEdit::textEdited, this, [this] {
            if (m_revealed)
                m_relock.start(); // keep it visible while the user is typing
            emit m_editor->contentChanged();
        });
        QObject::connect(m_key, &QLineEdit::returnPressed, m_value, qOverload<>(&QWidget::setFocus));
        QObject::connect(m_eye, &QToolButton::clicked, this, [this] { toggleReveal(); });
        QObject::connect(m_copy, &QToolButton::clicked, this, [this] { copyValue(); });
        QObject::connect(m_lock, &QToolButton::clicked, this, [this] { toggleSecret(); });
        QObject::connect(m_delete, &QToolButton::clicked, this, [this] { requestDelete(); });

        applyState();
    }

    QString id() const { return m_id; } // stable; the keychain entry of a secret value is stored under it
    QString key() const { return m_key->text(); }
    QString value() const { return m_value->text(); }
    bool isSecret() const { return m_secret; }
    QLineEdit *keyEdit() const { return m_key; }

    void lock()
    {
        m_revealed = false;
        m_relock.stop();
        applyState();
    }

private:
    QToolButton *makeButton(const QString &text, const QString &tip)
    {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setFixedSize(26, 26);
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoRaise(true);
        QFont f = b->font();
        f.setPointSize(12);
        b->setFont(f);
        QSizePolicy sp = b->sizePolicy();
        sp.setRetainSizeWhenHidden(true); // keep value boxes aligned across rows
        b->setSizePolicy(sp);
        return b;
    }

    bool isLocked() const { return m_secret && !m_revealed; }

    QString label() const { return m_key->text().isEmpty() ? QObject::tr("this field") : m_key->text(); }

    bool authenticate(const QString &reason)
    {
        // The row may be deleted while the modal dialog runs (e.g. the note is deleted).
        QPointer<KeyValueRow> self(this);
        const bool ok = SystemAuth::authenticate(window(), reason);
        return ok && self;
    }

    void applyState()
    {
        const bool locked = isLocked();
        m_value->setEchoMode(locked ? QLineEdit::Password : QLineEdit::Normal);
        m_value->setReadOnly(locked); // reveal first to edit a secret
        m_value->setPlaceholderText(locked ? QString() : QObject::tr("Value"));

        m_eye->setVisible(m_secret);
        m_eye->setText(m_revealed ? QStringLiteral("\U0001F648") : QStringLiteral("\U0001F441"));
        m_eye->setToolTip(m_revealed ? QObject::tr("Hide value")
                                     : QObject::tr("Reveal value (asks for your password)"));

        m_lock->setChecked(m_secret);
        m_lock->setText(m_secret ? QStringLiteral("\U0001F512") : QStringLiteral("\U0001F513"));
        m_lock->setToolTip(m_secret ? QObject::tr("Secret value - click to make it a normal value")
                                    : QObject::tr("Mark value as secret (hidden)"));
    }

    void toggleReveal()
    {
        if (m_revealed) {
            lock();
            return;
        }
        if (!authenticate(QObject::tr("Reveal the value of \"%1\"").arg(label())))
            return;
        m_revealed = true;
        m_relock.start();
        applyState();
    }

    void toggleSecret()
    {
        if (!m_secret) {
            m_secret = true;
            m_revealed = false;
            m_relock.stop();
        } else {
            if (isLocked() && !authenticate(QObject::tr("Show the value of \"%1\" permanently").arg(label()))) {
                applyState(); // undo the checkbox toggle
                return;
            }
            m_secret = false;
            m_revealed = false;
            m_relock.stop();
        }
        applyState();
        emit m_editor->contentChanged();
    }

    void copyValue()
    {
        if (isLocked() && !authenticate(QObject::tr("Copy the value of \"%1\"").arg(label())))
            return;
        const QString text = m_value->text();
        QApplication::clipboard()->setText(text);
        if (m_secret) {
            // Wipe the secret from the clipboard after a while, if it is still there.
            QTimer::singleShot(kClipboardClearMs, qApp, [text] {
                if (QApplication::clipboard()->text() == text)
                    QApplication::clipboard()->clear();
            });
        }
        QToolTip::showText(m_copy->mapToGlobal(QPoint(0, m_copy->height())),
                           m_secret ? QObject::tr("Copied (clipboard clears in 30 s)") : QObject::tr("Copied"),
                           m_copy);
    }

    void requestDelete()
    {
        if (!m_key->text().isEmpty() || !m_value->text().isEmpty()) {
            const auto answer = QMessageBox::question(window(), QObject::tr("Delete field"),
                                                      QObject::tr("Delete the field \"%1\"?").arg(label()),
                                                      QMessageBox::Yes | QMessageBox::Cancel,
                                                      QMessageBox::Cancel);
            if (answer != QMessageBox::Yes)
                return;
        }
        m_editor->removeRow(this);
    }

    KeyValueEditor *m_editor;
    QString m_id;
    QLineEdit *m_key;
    QLineEdit *m_value;
    QToolButton *m_eye;
    QToolButton *m_copy;
    QToolButton *m_lock;
    QToolButton *m_delete;
    QTimer m_relock;
    bool m_secret = false;
    bool m_revealed = false;
};

KeyValueEditor::KeyValueEditor(QWidget *parent) : QScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget;
    m_container->setObjectName("kvArea");
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(6);
    m_layout->addStretch();
    setWidget(m_container);

    appendRow(QString(), QString(), false);
}

KeyValueRow *KeyValueEditor::appendRow(const QString &key, const QString &value, bool secret, const QString &id)
{
    auto *row = new KeyValueRow(this, id, key, value, secret);
    m_layout->insertWidget(m_layout->count() - 1, row); // before the stretch
    m_rows.append(row);
    return row;
}

void KeyValueEditor::addField()
{
    KeyValueRow *row = appendRow(QString(), QString(), false);
    row->keyEdit()->setFocus();
    QTimer::singleShot(0, this, [this, row] { ensureWidgetVisible(row); });
    emit contentChanged();
}

void KeyValueEditor::removeRow(KeyValueRow *row)
{
    m_rows.removeOne(row);
    row->hide();
    row->deleteLater();
    if (m_rows.isEmpty())
        appendRow(QString(), QString(), false);
    emit contentChanged();
}

void KeyValueEditor::clearRows()
{
    qDeleteAll(m_rows);
    m_rows.clear();
}

void KeyValueEditor::lockAll()
{
    for (KeyValueRow *row : std::as_const(m_rows))
        row->lock();
}

void KeyValueEditor::focusFirst()
{
    if (!m_rows.isEmpty())
        m_rows.first()->keyEdit()->setFocus();
}

QJsonArray KeyValueEditor::toJson() const
{
    QJsonArray arr;
    for (const KeyValueRow *row : m_rows) {
        if (row->key().isEmpty() && row->value().isEmpty())
            continue;
        arr.append(QJsonObject{{"id", row->id()}, {"key", row->key()}, {"value", row->value()},
                               {"secret", row->isSecret()}});
    }
    return arr;
}

void KeyValueEditor::fromJson(const QJsonArray &fields)
{
    clearRows();
    for (const QJsonValue &v : fields) {
        const QJsonObject o = v.toObject();
        appendRow(o.value("key").toString(), o.value("value").toString(), o.value("secret").toBool(),
                  o.value("id").toString());
    }
    if (m_rows.isEmpty())
        appendRow(QString(), QString(), false);
}

QString KeyValueEditor::toPlainText() const
{
    QStringList lines;
    for (const KeyValueRow *row : m_rows) {
        if (row->key().isEmpty() && row->value().isEmpty())
            continue;
        lines << QString("%1: %2").arg(row->key(), row->isSecret() ? kMask : row->value());
    }
    return lines.join('\n');
}
