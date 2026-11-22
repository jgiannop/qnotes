#include "systemauth.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#include <cstdlib>
#include <cstring>
#endif

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

namespace {

constexpr qint64 kGracePeriodMs = 60 * 1000; // don't ask again for 1 minute
constexpr int kMaxAttempts = 3;              // like sudo

QElapsedTimer g_lastSuccess;

QString currentUser()
{
#ifdef Q_OS_UNIX
    if (passwd *pw = getpwuid(getuid()))
        return QString::fromLocal8Bit(pw->pw_name);
#endif
    return qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
}

#ifdef HAVE_PAM
int pamConversation(int count, const pam_message **messages, pam_response **responses, void *data)
{
    const char *password = static_cast<const char *>(data);
    auto *replies = static_cast<pam_response *>(calloc(size_t(count), sizeof(pam_response)));
    if (!replies)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        if (messages[i]->msg_style == PAM_PROMPT_ECHO_OFF || messages[i]->msg_style == PAM_PROMPT_ECHO_ON)
            replies[i].resp = strdup(password);
    }
    *responses = replies; // PAM frees these
    return PAM_SUCCESS;
}

// Verifies the password through the same PAM stack sudo uses.
bool checkPassword(const QByteArray &user, const QByteArray &password)
{
    pam_conv conv{pamConversation, const_cast<char *>(password.constData())};
    pam_handle_t *handle = nullptr;
    if (pam_start("sudo", user.constData(), &conv, &handle) != PAM_SUCCESS)
        return false;
    const int rc = pam_authenticate(handle, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
    pam_end(handle, rc);
    return rc == PAM_SUCCESS;
}
#endif

class AuthDialog : public QDialog
{
public:
    AuthDialog(QWidget *parent, const QString &reason) : QDialog(parent), m_user(currentUser())
    {
        setWindowTitle(tr("Authentication required"));
        setModal(true);

        auto *icon = new QLabel(this);
        icon->setPixmap(QIcon::fromTheme("dialog-password",
                                         style()->standardIcon(QStyle::SP_MessageBoxWarning))
                            .pixmap(48, 48));
        icon->setAlignment(Qt::AlignTop);

        auto *text = new QLabel(QString("<b>%1</b><br><br>%2")
                                    .arg(reason.toHtmlEscaped(),
                                         tr("Enter the password for <b>%1</b>:")
                                             .arg(m_user.toHtmlEscaped())),
                                this);
        text->setWordWrap(true);

        m_password = new QLineEdit(this);
        m_password->setEchoMode(QLineEdit::Password);
        m_password->setMinimumWidth(260);

        m_error = new QLabel(this);
        m_error->setStyleSheet("color: #c42b1c;");
        m_error->hide();

        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Authenticate"));
        connect(m_buttons, &QDialogButtonBox::accepted, this, &AuthDialog::tryAuthenticate);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *right = new QVBoxLayout;
        right->addWidget(text);
        right->addWidget(m_password);
        right->addWidget(m_error);

        auto *top = new QHBoxLayout;
        top->addWidget(icon);
        top->addLayout(right, 1);

        auto *layout = new QVBoxLayout(this);
        layout->addLayout(top);
        layout->addWidget(m_buttons);

        m_password->setFocus();
    }

private:
    void tryAuthenticate()
    {
        QByteArray password = m_password->text().toUtf8();
        m_password->clear();
        if (password.isEmpty())
            return;

        setEnabled(false);
        setCursor(Qt::BusyCursor);

        // Run PAM off the UI thread: a wrong password takes ~2 s (pam_faildelay).
        bool ok = false;
#ifdef HAVE_PAM
        const QByteArray user = m_user.toLocal8Bit();
        QThread *worker = QThread::create([&] { ok = checkPassword(user, password); });
        QEventLoop loop;
        connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
        worker->start();
        loop.exec();
        worker->wait();
        delete worker;
#endif
        password.fill('\0');

        unsetCursor();
        setEnabled(true);

        if (ok) {
            accept();
            return;
        }
        if (++m_attempts >= kMaxAttempts) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("%1 incorrect password attempts.").arg(kMaxAttempts));
            reject();
            return;
        }
        m_error->setText(tr("Sorry, try again."));
        m_error->show();
        m_password->setFocus();
    }

    QString m_user;
    QLineEdit *m_password;
    QLabel *m_error;
    QDialogButtonBox *m_buttons;
    int m_attempts = 0;
};

} // namespace

bool SystemAuth::authenticate(QWidget *parent, const QString &reason)
{
    if (g_lastSuccess.isValid() && g_lastSuccess.elapsed() < kGracePeriodMs)
        return true;

#ifndef HAVE_PAM
    QMessageBox::warning(parent, QObject::tr("Authentication"),
                         QObject::tr("System password verification is not supported on this platform."));
    return false;
#else
    AuthDialog dialog(parent, reason);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    g_lastSuccess.start();
    return true;
#endif
}
