// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

// No-account edition focused gate (1.1.9). Registered only when the tree is
// configured with COLOSSEUM_NO_ACCOUNT_EDITION=ON. Asserts the edition's
// three load-bearing seams against the real classes:
//   1. AccountServiceEndpoint resolves to nothing even with an environment
//      override — no account-service authentication/sync traffic can be
//      constructed, including the default production fallback.
//   2. AccountController reports accountFeaturesAvailable=false, the property
//      every QML account entry surface gates on.
//   3. The startup settlement AccountRuntime::prepareForQml performs in this
//      edition — continueWithoutAccount() — lands the controller in local-only
//      mode, durably completes onboarding, prepares the local-only profile and
//      attempts zero account-service sends (counting transport).

#include "account/AccountBootstrapStore.h"
#include "account/AccountClient.h"
#include "account/AccountController.h"
#include "account/AccountDeviceIdentity.h"
#include "account/AccountProfileCoordinator.h"
#include "account/AccountServiceEndpoint.h"
#include "account/AccountTransport.h"
#include "MemoryAccountCredentialStore.h"
#include "MemoryAccountOneTimeSecretSink.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char *name)
        : m_name(name),
          m_wasSet(qEnvironmentVariableIsSet(name)),
          m_previous(qgetenv(name))
    {
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_wasSet)
            qputenv(m_name.constData(), m_previous);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previous;
};

class CountingTransport final : public AccountTransport {
public:
    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override
    {
        Q_UNUSED(requestId);
        Q_UNUSED(request);
        ++m_sends;
    }

    int sends() const { return m_sends; }

private:
    int m_sends = 0;
};

class RecordingProfileCoordinator final : public AccountProfileCoordinator {
public:
    bool prepareCreatedAccount(
        const QString &accountId,
        QString *error) override
    {
        Q_UNUSED(accountId);
        Q_UNUSED(error);
        return true;
    }

    bool prepareAccountSession(
        const QString &accountId,
        QString *error) override
    {
        Q_UNUSED(accountId);
        Q_UNUSED(error);
        return true;
    }

    bool prepareRememberedAccount(
        const QString &accountId,
        QString *error) override
    {
        Q_UNUSED(accountId);
        Q_UNUSED(error);
        return true;
    }

    bool prepareLocalOnly(QString *error) override
    {
        ++localOnlyCalls;
        if (!result && error)
            *error = QStringLiteral("fixture local-profile failure");
        return result;
    }

    bool sealAccountSession(
        const QString &accountId,
        QString *error) override
    {
        Q_UNUSED(accountId);
        Q_UNUSED(error);
        return true;
    }

    bool result = true;
    int localOnlyCalls = 0;
};

struct EditionFixture {
    QTemporaryDir temp;
    CountingTransport transport;
    MemoryAccountCredentialStore credentials;
    MemoryAccountOneTimeSecretSink secretSink;
    std::unique_ptr<AccountClient> client;
    std::unique_ptr<AccountDeviceIdentity> deviceIdentity;
    std::unique_ptr<AccountBootstrapStore> bootstrap;
    std::unique_ptr<AccountController> controller;
    RecordingProfileCoordinator profiles;

    EditionFixture()
    {
        if (!temp.isValid())
            qFatal("Could not create no-account edition test directory.");

        qputenv(
            "COLOSSEUM_APPDATA_TAG",
            QByteArrayLiteral("no-account-edition-test"));

        client = std::make_unique<AccountClient>(&transport);
        deviceIdentity =
            std::make_unique<AccountDeviceIdentity>(
                temp.path() + QLatin1String("/device.ini"));
        bootstrap =
            std::make_unique<AccountBootstrapStore>(
                temp.path() + QLatin1String("/bootstrap.ini"));
        controller =
            std::make_unique<AccountController>(
                client.get(),
                &credentials,
                deviceIdentity.get(),
                bootstrap.get(),
                &secretSink);
        controller->setAutomaticPollingEnabled(false);
        controller->setProfileCoordinator(&profiles);
    }
};

}

class tst_no_account_edition : public QObject {
    Q_OBJECT

private slots:
    void endpointSuppressedEvenWithEnvironmentOverride();
    void controllerReportsAccountFeaturesUnavailable();
    void startupSettlementLandsLocalOnlyWithZeroTraffic();
    void settlementIsIdempotentForAlreadyLocalState();
};

void tst_no_account_edition::
endpointSuppressedEvenWithEnvironmentOverride() {
    ScopedEnvironmentVariable restore(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");
    qputenv(
        "COLOSSEUM_ACCOUNT_SERVICE_URL",
        QByteArrayLiteral("https://canary.invalid"));

    const QUrl url = AccountServiceEndpoint::configuredUrl();

    QVERIFY2(
        url.isEmpty(),
        "no-account edition must resolve no endpoint — env override, compile "
        "override and the production fallback are all suppressed");
}

void tst_no_account_edition::
controllerReportsAccountFeaturesUnavailable() {
    EditionFixture fixture;

    QVERIFY(!fixture.controller->accountFeaturesAvailable());
    // The QML gate reads the property by name from the context object; prove
    // the metobject surface, not just the C++ getter.
    QVERIFY(
        fixture.controller
            ->property("accountFeaturesAvailable")
            .isValid());
    QCOMPARE(
        fixture.controller
            ->property("accountFeaturesAvailable")
            .toBool(),
        false);
}

void tst_no_account_edition::
startupSettlementLandsLocalOnlyWithZeroTraffic() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    EditionFixture fixture;

    QVERIFY(fixture.controller->onboardingRequired());
    QCOMPARE(fixture.profiles.localOnlyCalls, 0);

    // The exact call AccountRuntime::prepareForQml makes in this edition.
    fixture.controller->continueWithoutAccount();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));
    QVERIFY(!fixture.controller->onboardingRequired());
    QVERIFY(fixture.bootstrap->onboardingCompleted());
    QVERIFY(fixture.bootstrap->localOnlyChosen());
    QCOMPARE(fixture.profiles.localOnlyCalls, 1);

    AccountBootstrapStore reopened(fixture.bootstrap->settingsPath());
    QVERIFY(reopened.onboardingCompleted());
    QVERIFY(reopened.localOnlyChosen());

    QCOMPARE(
        fixture.transport.sends(),
        0);
}

void tst_no_account_edition::
settlementIsIdempotentForAlreadyLocalState() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    EditionFixture fixture;

    fixture.controller->continueWithoutAccount();
    fixture.controller->continueWithoutAccount();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));
    QVERIFY(!fixture.controller->onboardingRequired());
    QCOMPARE(
        fixture.transport.sends(),
        0);
}

QTEST_GUILESS_MAIN(tst_no_account_edition)
#include "tst_no_account_edition.moc"

