#include "backup.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPasswordDigestor>
#include <QRandomGenerator>

extern "C" {
#include "third_party/tweetnacl/tweetnacl.h"

// TweetNaCl needs a source of secure random bytes.
void randombytes(unsigned char *buf, unsigned long long len)
{
    QRandomGenerator *rng = QRandomGenerator::system(); // OS CSPRNG
    for (unsigned long long i = 0; i < len; ++i)
        buf[i] = static_cast<unsigned char>(rng->generate() & 0xff);
}
}

namespace {

constexpr int kIterations = 600000; // PBKDF2-HMAC-SHA256, OWASP recommendation
constexpr int kSaltBytes = 16;
const char kFormat[] = "qnotes-backup";

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    randombytes(reinterpret_cast<unsigned char *>(b.data()), static_cast<unsigned long long>(n));
    return b;
}

QByteArray deriveKey(const QString &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt, iterations,
                                              crypto_secretbox_KEYBYTES);
}

QString tr(const char *text)
{
    return QCoreApplication::translate("Backup", text);
}

} // namespace

QByteArray Backup::encrypt(const QByteArray &plain, const QString &password)
{
    const QByteArray salt = randomBytes(kSaltBytes);
    const QByteArray nonce = randomBytes(crypto_secretbox_NONCEBYTES);
    QByteArray key = deriveKey(password, salt, kIterations);

    // secretbox wants ZEROBYTES of zero padding in front of the message, and puts BOXZEROBYTES of zeros in
    // front of the ciphertext.
    QByteArray m(crypto_secretbox_ZEROBYTES, '\0');
    m += plain;
    QByteArray c(m.size(), '\0');
    crypto_secretbox(reinterpret_cast<unsigned char *>(c.data()), reinterpret_cast<const unsigned char *>(m.constData()),
                     static_cast<unsigned long long>(m.size()),
                     reinterpret_cast<const unsigned char *>(nonce.constData()),
                     reinterpret_cast<const unsigned char *>(key.constData()));
    m.fill('\0');
    key.fill('\0');

    const QJsonObject o{
        {"format", kFormat},
        {"version", 1},
        {"created", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"kdf", "pbkdf2-hmac-sha256"},
        {"iterations", kIterations},
        {"salt", QString::fromLatin1(salt.toBase64())},
        {"cipher", "xsalsa20-poly1305"},
        {"nonce", QString::fromLatin1(nonce.toBase64())},
        {"data", QString::fromLatin1(c.mid(crypto_secretbox_BOXZEROBYTES).toBase64())},
    };
    return QJsonDocument(o).toJson();
}

bool Backup::decrypt(const QByteArray &file, const QString &password, QByteArray *plain, QString *error)
{
    const QJsonObject o = QJsonDocument::fromJson(file).object();
    if (o.value("format").toString() != QLatin1String(kFormat)) {
        *error = tr("This file is not a QNotes backup.");
        return false;
    }
    if (o.value("version").toInt() != 1 || o.value("kdf").toString() != "pbkdf2-hmac-sha256"
        || o.value("cipher").toString() != "xsalsa20-poly1305") {
        *error = tr("This backup was made by a newer version of QNotes.");
        return false;
    }
    const int iterations = o.value("iterations").toInt();
    const QByteArray salt = QByteArray::fromBase64(o.value("salt").toString().toLatin1());
    const QByteArray nonce = QByteArray::fromBase64(o.value("nonce").toString().toLatin1());
    const QByteArray data = QByteArray::fromBase64(o.value("data").toString().toLatin1());
    if (iterations < 1 || iterations > 10000000 || nonce.size() != crypto_secretbox_NONCEBYTES
        || data.size() < crypto_secretbox_ZEROBYTES - crypto_secretbox_BOXZEROBYTES) {
        *error = tr("The backup file is damaged.");
        return false;
    }

    QByteArray key = deriveKey(password, salt, iterations);
    QByteArray c(crypto_secretbox_BOXZEROBYTES, '\0');
    c += data;
    QByteArray m(c.size(), '\0');
    const int rc = crypto_secretbox_open(reinterpret_cast<unsigned char *>(m.data()),
                                         reinterpret_cast<const unsigned char *>(c.constData()),
                                         static_cast<unsigned long long>(c.size()),
                                         reinterpret_cast<const unsigned char *>(nonce.constData()),
                                         reinterpret_cast<const unsigned char *>(key.constData()));
    key.fill('\0');
    if (rc != 0) {
        *error = tr("Wrong password, or the backup file was modified.");
        return false;
    }
    *plain = m.mid(crypto_secretbox_ZEROBYTES);
    m.fill('\0');
    return true;
}
