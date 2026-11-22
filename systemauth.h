#pragma once

class QString;
class QWidget;

// Asks for the current user's system (login / sudo) password and verifies it with PAM.
// After a successful authentication no prompt is shown again for a short grace period,
// similar to sudo's timestamp.
namespace SystemAuth {

bool authenticate(QWidget *parent, const QString &reason);

} // namespace SystemAuth
