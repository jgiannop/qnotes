#pragma once

#include <QByteArray>
#include <QString>

// Password-protected backup files. The whole payload is encrypted with XSalsa20-Poly1305 (TweetNaCl
// crypto_secretbox); the key comes from the password via PBKDF2-HMAC-SHA256 with a random salt.
// File format: JSON {format: "qnotes-backup", version, kdf, iterations, salt, cipher, nonce, data} (base64).
namespace Backup {

QByteArray encrypt(const QByteArray &plain, const QString &password);

// False with *error set if the file isn't a QNotes backup, or the password is wrong / the file was modified.
bool decrypt(const QByteArray &file, const QString &password, QByteArray *plain, QString *error);

} // namespace Backup
