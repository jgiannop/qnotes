#include "secretstore.h"

#include <QEventLoop>

#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <qtkeychain/keychain.h> // built from source (FetchContent), e.g. on Windows
#endif

namespace {

// Test harnesses set QNOTES_KEYCHAIN_SERVICE so their entries never mix with the user's real ones.
const QString kService = qEnvironmentVariable("QNOTES_KEYCHAIN_SERVICE", QStringLiteral("QNotes"));

// Runs a keychain job to completion.
bool runJob(QKeychain::Job *job, QString *error)
{
    job->setAutoDelete(false);
    QEventLoop loop;
    QObject::connect(job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job->start();
    loop.exec();
    const bool ok = job->error() == QKeychain::NoError;
    if (!ok && error)
        *error = job->errorString();
    return ok;
}

} // namespace

bool SecretStore::isAvailable()
{
    static const bool available = QKeychain::isAvailable();
    return available;
}

bool SecretStore::read(const QString &id, QString *value, QString *error)
{
    QKeychain::ReadPasswordJob job(kService);
    job.setKey(id);
    if (!runJob(&job, error))
        return false;
    *value = job.textData();
    return true;
}

bool SecretStore::write(const QString &id, const QString &value, QString *error)
{
    QKeychain::WritePasswordJob job(kService);
    job.setKey(id);
    job.setTextData(value);
    return runJob(&job, error);
}

bool SecretStore::remove(const QString &id)
{
    QKeychain::DeletePasswordJob job(kService);
    job.setKey(id);
    QString error;
    return runJob(&job, &error);
}
