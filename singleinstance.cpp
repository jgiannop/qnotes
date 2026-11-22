#include "singleinstance.h"

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

static const QByteArray kShowCommand = "show\n";

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {}

QString SingleInstance::serverName() const
{
#ifdef Q_OS_WIN
    // Named pipes are machine-wide on Windows: include the user name.
    return QStringLiteral("qnotes-") + qEnvironmentVariable("USERNAME");
#else
    // Per-user runtime dir ($XDG_RUNTIME_DIR), so other users' instances don't interfere.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + QStringLiteral("/qnotes.sock");
#endif
}

bool SingleInstance::start()
{
    const QString name = serverName();

    // Already running? Ask it to show itself.
    QLocalSocket socket;
    socket.connectToServer(name);
    if (socket.waitForConnected(500)) {
        socket.write(kShowCommand);
        socket.waitForBytesWritten(500);
        socket.disconnectFromServer();
        return false;
    }

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(name)) {
        // Nobody answered, so the socket file is left over from a crash: remove it and retry.
        QLocalServer::removeServer(name);
        if (!m_server->listen(name))
            qWarning("QNotes: could not start single-instance server: %s",
                     qPrintable(m_server->errorString()));
    }

    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server->nextPendingConnection()) {
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                if (client->readAll().contains("show"))
                    emit showRequested();
            });
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });
    return true;
}
