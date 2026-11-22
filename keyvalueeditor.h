#pragma once

#include <QJsonArray>
#include <QScrollArea>

class KeyValueRow;
class QVBoxLayout;

// A list of key = value fields. Values can be marked secret: they are shown as dots and
// revealing / copying / un-marking them requires the system password.
class KeyValueEditor : public QScrollArea
{
    Q_OBJECT
public:
    explicit KeyValueEditor(QWidget *parent = nullptr);

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &fields);

    // Text for the notes list preview / search. Secret values are masked.
    QString toPlainText() const;

    void focusFirst();

public slots:
    void addField();
    void lockAll(); // hide every revealed secret

signals:
    void contentChanged();

private:
    friend class KeyValueRow;
    KeyValueRow *appendRow(const QString &key, const QString &value, bool secret, const QString &id = QString());
    void removeRow(KeyValueRow *row);
    void clearRows();

    QWidget *m_container;
    QVBoxLayout *m_layout;
    QList<KeyValueRow *> m_rows;
};
