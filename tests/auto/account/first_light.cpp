// First-light harness (Bundle 8C adoption, 2026-08-17): drives the EXACT
// production account chain — AccountRuntime (transport, controller, credential
// store, profile runtime, coordinators, sync engine) — against the live local
// Colosseum account service. Creates the real first account through the same
// code path the QML submit button uses (controller.createAccount), prints every
// mode transition, and exits 0 only on SignedIn.
#include "account/AccountRuntime.h"
#include "account/AccountController.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

class FirstLight : public QObject {
    Q_OBJECT
public:
    explicit FirstLight(AccountRuntime *runtime,
                        const QString &username,
                        const QString &password)
        : QObject(runtime),
          m_runtime(runtime),
          m_username(username),
          m_password(password) {
        connect(runtime->controller(),
                &AccountController::modeChanged,
                this,
                &FirstLight::onMode);
        connect(runtime->controller(),
                &AccountController::accountError,
                this,
                &FirstLight::onError);

        m_deadline.setSingleShot(true);
        connect(&m_deadline, &QTimer::timeout, this, &FirstLight::onTimeout);
    }

    void start() {
        std::printf("[first-light] creating account '%s'...\n",
                    qPrintable(m_username));
        std::fflush(stdout);
        m_deadline.start(30000);
        QTimer::singleShot(300, this, [this]() {
            m_runtime->controller()->createAccount(m_username, m_password);
        });
    }

    bool failed() const { return m_failed; }

private slots:
    void onMode() {
        const QString mode = m_runtime->controller()->mode();
        std::printf("[first-light] mode -> %s\n", qPrintable(mode));
        std::fflush(stdout);

        if (mode == QStringLiteral("signedIn")) {
            std::printf(
                "[first-light] SIGNED IN as %s (account %s, device %s)\n",
                qPrintable(m_runtime->controller()->username()),
                qPrintable(m_runtime->controller()->accountId()),
                qPrintable(m_runtime->controller()->deviceId()));
            std::fflush(stdout);
            m_deadline.stop();
            QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
        }
    }

    void onError(const QString &category,
                 const QString &code,
                 const QString &message) {
        std::printf("[first-light] error %s/%s: %s\n",
                    qPrintable(category),
                    qPrintable(code),
                    qPrintable(message));
        std::fflush(stdout);
    }

    void onTimeout() {
        std::printf("[first-light] TIMEOUT in mode '%s'\n",
                    qPrintable(m_runtime->controller()->mode()));
        std::fflush(stdout);
        m_failed = true;
        qApp->quit();
    }

private:
    AccountRuntime *m_runtime = nullptr;
    QString m_username;
    QString m_password;
    QTimer m_deadline;
    bool m_failed = false;
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("colosseum-first-light"));
    QCoreApplication::setOrganizationName(
        QStringLiteral("Brotherhood"));

    if (qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG")) {
        qWarning("Refusing first-light under COLOSSEUM_APPDATA_TAG");
        return 2;
    }

    const QString username =
        app.arguments().value(1, QStringLiteral("Hemanth56"));
    const QString password = app.arguments().value(2);
    if (password.size() < 15) {
        qWarning("password must be 15+ characters");
        return 2;
    }

    AccountRuntime runtime;
    FirstLight light(&runtime, username, password);
    light.start();

    const int result = app.exec();
    return light.failed() ? 1 : result;
}

#include "first_light.moc"
