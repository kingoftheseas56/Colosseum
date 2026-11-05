#include "account/AccountServiceEndpoint.h"

#include <QByteArray>
#include <QUrl>
#include <QtTest>

namespace {
class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char *name)
        : m_name(name),
          m_wasSet(qEnvironmentVariableIsSet(name)),
          m_value(qgetenv(name)) {}

    ~ScopedEnvironmentVariable() {
        if (m_wasSet)
            qputenv(m_name.constData(), m_value);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_value;
};
}

class tst_account_service_endpoint : public QObject {
    Q_OBJECT

private slots:
    void usesProductionDefaultAndEnvironmentOverride();
};

void tst_account_service_endpoint::
usesProductionDefaultAndEnvironmentOverride() {
    ScopedEnvironmentVariable restore(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");

    qunsetenv("COLOSSEUM_ACCOUNT_SERVICE_URL");
    QCOMPARE(
        AccountServiceEndpoint::configuredUrl(),
        QUrl(QStringLiteral(
            "https://colosseum-account-service.onrender.com")));

    qputenv(
        "COLOSSEUM_ACCOUNT_SERVICE_URL",
        QByteArrayLiteral("http://127.0.0.1:8099"));
    QCOMPARE(
        AccountServiceEndpoint::configuredUrl(),
        QUrl(QStringLiteral("http://127.0.0.1:8099")));
}

QTEST_MAIN(tst_account_service_endpoint)
#include "tst_account_service_endpoint.moc"
