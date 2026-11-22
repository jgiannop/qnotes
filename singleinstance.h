#pragma once

#include <QObject>

class QLocalServer;

// Makes sure only one QNotes runs per user. A second launch (e.g. from the app grid) asks the
// running instance to show its notes list and then exits.
class SingleInstance : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstance(QObject *parent = nullptr);

    // Returns false if another instance is already running (it has been asked to show itself).
    bool start();

signals:
    void showRequested();

private:
    QString serverName() const;

    QLocalServer *m_server = nullptr;
};
