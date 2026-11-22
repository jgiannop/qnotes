#pragma once

#include <QString>

// Secret values in the system keychain: GNOME Keyring / KWallet (Secret Service) on Linux,
// Credential Manager on Windows. Entries are stored under the service "QNotes" with the field id as key.
// The calls are synchronous (they run a short local event loop); the keychain may show an unlock prompt.
namespace SecretStore {

bool isAvailable(); // checked once, then cached

bool read(const QString &id, QString *value, QString *error = nullptr);
bool write(const QString &id, const QString &value, QString *error = nullptr);
bool remove(const QString &id);

} // namespace SecretStore
