// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountBootstrapStore.h"
#include "account/AccountClient.h"
#include "account/AccountController.h"
#include "account/AccountDeviceIdentity.h"
#include "account/AccountProfileCoordinator.h"
#include "account/AccountRecoveryKeyPresenter.h"
#include "account/AccountServiceEndpoint.h"
#include "AccountFixtureTransport.h"
#include "MemoryAccountCredentialStore.h"
#include "MemoryAccountOneTimeSecretSink.h"
#include "MemoryAccountSensitiveClipboard.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QJsonObject>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <memory>

namespace {
// Concatenated bytes of every file under root (recursive): lets the secret-
// persistence negative controls assert a recovery-key sentinel never lands on
// disk anywhere in the tagged state tree.
QByteArray ordinaryStateBytes(const QString &root) {
    QByteArray accumulated;
    QDirIterator it(
        root,
        QDir::Files,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next());
        if (file.open(QIODevice::ReadOnly))
            accumulated.append(file.readAll());
    }
    return accumulated;
}

constexpr auto kAccountId =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceId =
    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";

class RecordingProfileCoordinator final
    : public AccountProfileCoordinator {
public:
    bool prepareCreatedAccount(
        const QString &accountId,
        QString *error) override {
        ++createdCalls;
        lastAccountId = accountId;
        if (!createdResult && error)
            *error = QStringLiteral("fixture adoption failure");
        return createdResult;
    }

    bool prepareAccountSession(
        const QString &accountId,
        QString *error) override {
        ++sessionCalls;
        lastAccountId = accountId;
        if (!sessionResult && error)
            *error = QStringLiteral("fixture profile-open failure");
        return sessionResult;
    }

    bool prepareRememberedAccount(
        const QString &accountId,
        QString *error) override {
        ++rememberedCalls;
        lastAccountId = accountId;
        if (!rememberedResult && error)
            *error = QStringLiteral("fixture remembered-profile failure");
        return rememberedResult;
    }

    bool prepareLocalOnly(
        QString *error) override {
        ++localOnlyCalls;
        if (!localOnlyResult && error)
            *error = QStringLiteral("fixture local-profile failure");
        return localOnlyResult;
    }

    bool sealAccountSession(
        const QString &accountId,
        QString *error) override {
        ++sealCalls;
        lastSealedAccountId = accountId;
        if (!sealResult && error)
            *error = QStringLiteral("fixture profile-seal failure");
        return sealResult;
    }

    bool createdResult = true;
    bool sessionResult = true;
    bool rememberedResult = true;
    bool localOnlyResult = true;
    bool sealResult = true;
    int createdCalls = 0;
    int sessionCalls = 0;
    int rememberedCalls = 0;
    int localOnlyCalls = 0;
    int sealCalls = 0;
    QString lastAccountId;
    QString lastSealedAccountId;
};

class DelayedAccountTransport final : public AccountTransport {
public:
    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override {
        m_requestId = requestId;
        m_request = request;
        ++m_sendCount;
    }

    int sendCount() const {
        return m_sendCount;
    }

    AccountTransportRequest request() const {
        return m_request;
    }

    void complete(const AccountTransportReply &reply) {
        const quint64 requestId = m_requestId;
        m_requestId = 0;
        emit finished(requestId, reply);
    }

private:
    quint64 m_requestId = 0;
    AccountTransportRequest m_request;
    int m_sendCount = 0;
};

class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char *name)
        : m_name(name),
          m_wasSet(qEnvironmentVariableIsSet(name)),
          m_previous(qgetenv(name)) {}

    ~ScopedEnvironmentVariable() {
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

AccountTransportReply okReply(
    int statusCode,
    const QJsonObject &body = QJsonObject()) {
    AccountTransportReply reply;
    reply.statusCode = statusCode;
    reply.body = body;
    return reply;
}

QJsonObject sessionObject(
    const QString &username,
    const QString &accessToken,
    const QString &refreshToken) {
    QJsonObject account;
    account.insert(
        QStringLiteral("id"),
        QString::fromLatin1(kAccountId));
    account.insert(
        QStringLiteral("username"),
        username);
    account.insert(
        QStringLiteral("protect_new_device_signins"),
        false);

    QJsonObject device;
    device.insert(
        QStringLiteral("id"),
        QString::fromLatin1(kDeviceId));

    QJsonObject session;
    session.insert(
        QStringLiteral("account"),
        account);
    session.insert(
        QStringLiteral("device"),
        device);
    session.insert(
        QStringLiteral("access_token"),
        accessToken);
    session.insert(
        QStringLiteral("access_expires_at"),
        QDateTime::currentDateTimeUtc()
            .addSecs(15 * 60)
            .toString(Qt::ISODateWithMs));
    session.insert(
        QStringLiteral("refresh_token"),
        refreshToken);
    return session;
}

struct ControllerFixture {
    QTemporaryDir temp;
    std::unique_ptr<AccountFixtureTransport> transport;
    MemoryAccountCredentialStore credentials;
    MemoryAccountOneTimeSecretSink secretSink;
    std::unique_ptr<AccountClient> client;
    std::unique_ptr<AccountDeviceIdentity> deviceIdentity;
    std::unique_ptr<AccountBootstrapStore> bootstrap;
    std::unique_ptr<AccountController> controller;

    ControllerFixture() {
        if (!temp.isValid())
            qFatal("Could not create account onboarding test directory.");

        qputenv(
            "COLOSSEUM_APPDATA_TAG",
            QByteArrayLiteral("account-onboarding-test"));

        transport = AccountFixtureTransport::create();
        if (!transport)
            qFatal("Fixture transport refused tagged test session.");

        client = std::make_unique<AccountClient>(
            transport.get());
        deviceIdentity =
            std::make_unique<AccountDeviceIdentity>(
                temp.path()
                + QLatin1String("/device.ini"));
        bootstrap =
            std::make_unique<AccountBootstrapStore>(
                temp.path()
                + QLatin1String("/bootstrap.ini"));
        controller =
            std::make_unique<AccountController>(
                client.get(),
                &credentials,
                deviceIdentity.get(),
                bootstrap.get(),
                &secretSink);
        controller->setAutomaticPollingEnabled(false);
    }
};
}

class tst_account_onboarding : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void freshInstallRequiresOnboarding();
    void continueLocalCompletesOnboardingDurably();
    void rememberedLocalOnlyPreparesProfileOwnership();
    void accountCreationCompletesOnboardingAfterSecretHandoff();
    void createdSessionIsNotAdoptedWhenProfilePreparationFails();
    void ordinarySignInPreparesAccountProfileBeforeCredentialAdoption();
    void rememberedAccountOpensProfileBeforeOfflineRefresh();
    void offlineLogoutQueuesRevocationAndSealsProfile();
    void remoteRevocationSealsProfileAndLocksDevice();
    void sealFailureFailsClosedAfterServerLogout();
    void protectedDeviceChallengeCanBeCancelled();
    void cancelledChallengeIgnoresLateApproval();
    void deviceRecoveryConsumesKeyAndPresentsReplacement();
    void recoveryKeyNeverAppearsInControllerProperties();

    void presenterExposesPurposeOnlyBesideTransientKey();
    void presenterReportsClipboardFailure();
    void presenterClearsOnlyUnchangedClipboard();
    void presenterPreservesNewerClipboardContent();

    void serviceEndpointHasNoInventedProductionDefault();
};

void tst_account_onboarding::initTestCase() {
    qRegisterMetaType<AccountOperation>();
    qRegisterMetaType<AccountTransportReply>();
}

void tst_account_onboarding::freshInstallRequiresOnboarding() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;

    QVERIFY(fixture.controller->onboardingRequired());
    QVERIFY(!fixture.bootstrap->onboardingCompleted());

    fixture.controller->restoreRememberedSession();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QVERIFY(fixture.controller->onboardingRequired());
}

void tst_account_onboarding::continueLocalCompletesOnboardingDurably() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;

    QSignalSpy onboardingSpy(
        fixture.controller.get(),
        &AccountController::onboardingRequiredChanged);

    fixture.controller->continueWithoutAccount();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));
    QVERIFY(!fixture.controller->onboardingRequired());
    QVERIFY(fixture.bootstrap->onboardingCompleted());
    QCOMPARE(onboardingSpy.count(), 1);

    AccountBootstrapStore reopened(
        fixture.bootstrap->settingsPath());
    QVERIFY(reopened.onboardingCompleted());
    QVERIFY(reopened.localOnlyChosen());
}

void tst_account_onboarding::
rememberedLocalOnlyPreparesProfileOwnership() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;
    RecordingProfileCoordinator profiles;
    fixture.controller->setProfileCoordinator(
        &profiles);

    QVERIFY(
        fixture.bootstrap->setLocalOnlyChosen(
            true));
    QVERIFY(
        fixture.bootstrap->setOnboardingCompleted(
            true));

    fixture.controller->restoreRememberedSession();

    QCOMPARE(profiles.localOnlyCalls, 1);
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));
}

void tst_account_onboarding::
accountCreationCompletesOnboardingAfterSecretHandoff() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;

    QJsonObject body;
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-create"),
            QStringLiteral("refresh-create")));
    body.insert(
        QStringLiteral("recovery_key"),
        QStringLiteral(
            "CLSM-AAAA-BBBB-CCCC-DDDD-EEEE-FFFF"));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/accounts"),
        okReply(201, body));

    fixture.controller->createAccount(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
    QVERIFY(!fixture.controller->onboardingRequired());
    QVERIFY(fixture.bootstrap->onboardingCompleted());
    QCOMPARE(fixture.secretSink.presentCount(), 1);
    QCOMPARE(
        fixture.secretSink.lastPurpose(),
        AccountRecoveryKeyPurpose::AccountCreated);
}


void tst_account_onboarding::
createdSessionIsNotAdoptedWhenProfilePreparationFails() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;
    RecordingProfileCoordinator profiles;
    profiles.createdResult = false;
    fixture.controller->setProfileCoordinator(
        &profiles);

    QJsonObject body;
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-create-profile-fail"),
            QStringLiteral("refresh-create-profile-fail")));
    body.insert(
        QStringLiteral("recovery_key"),
        QStringLiteral(
            "CLSM-PROFILE-FAIL-RECOVERY"));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/accounts"),
        okReply(201, body));

    fixture.controller->createAccount(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("error"));
    QCOMPARE(profiles.createdCalls, 1);
    QCOMPARE(profiles.sessionCalls, 0);
    QCOMPARE(
        profiles.lastAccountId,
        QString::fromLatin1(kAccountId));

    // The account exists server-side, so its one-time key is still presented
    // before local profile preparation. The session itself must not persist.
    QCOMPARE(fixture.secretSink.presentCount(), 1);
    QVERIFY(
        !fixture.credentials
             .loadActive()
             .has_value());
    QCOMPARE(
        fixture.controller->lastErrorCode(),
        QStringLiteral("profile_prepare_failed"));
}

void tst_account_onboarding::
ordinarySignInPreparesAccountProfileBeforeCredentialAdoption() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;
    RecordingProfileCoordinator profiles;
    fixture.controller->setProfileCoordinator(
        &profiles);

    QJsonObject body;
    body.insert(
        QStringLiteral("status"),
        QStringLiteral("signed_in"));
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-signin-profile"),
            QStringLiteral("refresh-signin-profile")));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(200, body));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
    QCOMPARE(profiles.createdCalls, 0);
    QCOMPARE(profiles.rememberedCalls, 0);
    QCOMPARE(profiles.sessionCalls, 1);
    QCOMPARE(
        profiles.lastAccountId,
        QString::fromLatin1(kAccountId));
    QVERIFY(
        fixture.credentials
            .loadActive()
            .has_value());
}

void tst_account_onboarding::
rememberedAccountOpensProfileBeforeOfflineRefresh() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;
    RecordingProfileCoordinator profiles;
    fixture.controller->setProfileCoordinator(
        &profiles);

    StoredAccountCredential credential;
    credential.accountId =
        QString::fromLatin1(kAccountId);
    credential.deviceId =
        QString::fromLatin1(kDeviceId);
    credential.refreshToken =
        QByteArrayLiteral("remembered-refresh");
    QVERIFY(
        fixture.credentials.saveActive(
            credential));

    fixture.transport->setOnline(false);

    fixture.controller->restoreRememberedSession();

    QCOMPARE(profiles.rememberedCalls, 1);
    QCOMPARE(profiles.sessionCalls, 0);
    QCOMPARE(
        profiles.lastAccountId,
        QString::fromLatin1(kAccountId));
    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("offline"));
    QVERIFY(
        fixture.credentials
            .loadActive()
            .has_value());
}

void tst_account_onboarding::
offlineLogoutQueuesRevocationAndSealsProfile() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;
    RecordingProfileCoordinator profiles;
    fixture.controller->setProfileCoordinator(
        &profiles);

    QJsonObject body;
    body.insert(
        QStringLiteral("status"),
        QStringLiteral("signed_in"));
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("offline-logout-access"),
            QStringLiteral("offline-logout-refresh")));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(200, body));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));

    fixture.transport->setOnline(false);
    fixture.controller->logoutCurrent();

    QTRY_COMPARE(profiles.sealCalls, 1);
    QCOMPARE(
        profiles.lastSealedAccountId,
        QString::fromLatin1(kAccountId));
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QVERIFY(
        !fixture.credentials
             .loadActive()
             .has_value());
    QVERIFY(
        fixture.credentials
            .pendingRevocations()
            .contains(
                QByteArrayLiteral(
                    "offline-logout-refresh")));
}

void tst_account_onboarding::
remoteRevocationSealsProfileAndLocksDevice() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;
    RecordingProfileCoordinator profiles;
    fixture.controller->setProfileCoordinator(
        &profiles);

    QJsonObject body;
    body.insert(
        QStringLiteral("status"),
        QStringLiteral("signed_in"));
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("revoked-access"),
            QStringLiteral("revoked-refresh")));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(200, body));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));
    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));

    AccountTransportReply revoked;
    revoked.statusCode = 401;
    revoked.errorCode =
        QStringLiteral("session_revoked");
    revoked.errorMessage =
        QStringLiteral("The session was revoked.");

    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"),
        QStringLiteral("/v1/profile"),
        revoked);

    fixture.controller->refreshProfile();

    QTRY_COMPARE(profiles.sealCalls, 1);
    QCOMPARE(
        profiles.lastSealedAccountId,
        QString::fromLatin1(kAccountId));
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("locked"));
    QCOMPARE(
        fixture.controller->lastErrorCode(),
        QStringLiteral("session_revoked"));
    QVERIFY(
        !fixture.credentials
             .loadActive()
             .has_value());
    QVERIFY(fixture.controller->username().isEmpty());
}

void tst_account_onboarding::
sealFailureFailsClosedAfterServerLogout() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;
    RecordingProfileCoordinator profiles;
    profiles.sealResult = false;
    fixture.controller->setProfileCoordinator(
        &profiles);

    QJsonObject body;
    body.insert(
        QStringLiteral("status"),
        QStringLiteral("signed_in"));
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("seal-fail-access"),
            QStringLiteral("seal-fail-refresh")));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(200, body));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("DELETE"),
        QStringLiteral("/v1/sessions/current"),
        okReply(204));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));
    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));

    fixture.controller->logoutCurrent();

    QTRY_COMPARE(profiles.sealCalls, 1);
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("error"));
    QCOMPARE(
        fixture.controller->lastErrorCode(),
        QStringLiteral("profile_seal_failed"));
    QVERIFY(
        !fixture.credentials
             .loadActive()
             .has_value());
    QVERIFY(fixture.controller->username().isEmpty());
}

void tst_account_onboarding::
protectedDeviceChallengeCanBeCancelled() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;

    QJsonObject approval;
    approval.insert(
        QStringLiteral("status"),
        QStringLiteral("approval_required"));
    approval.insert(
        QStringLiteral("challenge_token"),
        QStringLiteral("pending-device-challenge"));
    approval.insert(
        QStringLiteral("challenge_expires_at"),
        QDateTime::currentDateTimeUtc()
            .addSecs(600)
            .toString(Qt::ISODateWithMs));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(202, approval));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("awaitingDeviceApproval"));
    QVERIFY(!fixture.credentials.loadActive().has_value());

    fixture.controller->cancelPendingAuthentication();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QVERIFY(!fixture.credentials.loadActive().has_value());
    QVERIFY(fixture.controller->username().isEmpty());
}

void tst_account_onboarding::
cancelledChallengeIgnoresLateApproval() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    DelayedAccountTransport transport;
    MemoryAccountCredentialStore credentials;
    MemoryAccountOneTimeSecretSink secretSink;
    AccountClient client(&transport);
    AccountDeviceIdentity deviceIdentity(
        temp.path() + QLatin1String("/device.ini"));
    AccountBootstrapStore bootstrap(
        temp.path() + QLatin1String("/bootstrap.ini"));
    AccountController controller(
        &client,
        &credentials,
        &deviceIdentity,
        &bootstrap,
        &secretSink);
    controller.setAutomaticPollingEnabled(false);

    controller.signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));

    QCOMPARE(transport.sendCount(), 1);
    QCOMPARE(
        transport.request().path,
        QStringLiteral("/v1/sessions"));

    QJsonObject approval;
    approval.insert(
        QStringLiteral("status"),
        QStringLiteral("approval_required"));
    approval.insert(
        QStringLiteral("challenge_token"),
        QStringLiteral("late-device-challenge"));
    approval.insert(
        QStringLiteral("challenge_expires_at"),
        QDateTime::currentDateTimeUtc()
            .addSecs(600)
            .toString(Qt::ISODateWithMs));
    transport.complete(okReply(202, approval));

    QTRY_COMPARE(
        controller.mode(),
        QStringLiteral("awaitingDeviceApproval"));

    controller.pollPendingChallenge();
    QCOMPARE(transport.sendCount(), 2);
    QCOMPARE(
        transport.request().path,
        QStringLiteral("/v1/challenges/device/poll"));

    controller.cancelPendingAuthentication();
    QCOMPARE(
        controller.mode(),
        QStringLiteral("signedOut"));

    QJsonObject approved;
    approved.insert(
        QStringLiteral("status"),
        QStringLiteral("signed_in"));
    approved.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("late-access"),
            QStringLiteral("late-refresh")));

    transport.complete(okReply(200, approved));

    // Let the deferred completion signal land so the stale-generation
    // rejection is actually exercised, then prove the late approval was
    // ignored.
    QTest::qWait(50);
    QCOMPARE(
        controller.mode(),
        QStringLiteral("signedOut"));
    QVERIFY(!credentials.loadActive().has_value());
    QVERIFY(client.accessToken().isEmpty());
}

void tst_account_onboarding::
deviceRecoveryConsumesKeyAndPresentsReplacement() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;

    QJsonObject approval;
    approval.insert(
        QStringLiteral("status"),
        QStringLiteral("approval_required"));
    approval.insert(
        QStringLiteral("challenge_token"),
        QStringLiteral("device-recovery-challenge"));
    approval.insert(
        QStringLiteral("challenge_expires_at"),
        QDateTime::currentDateTimeUtc()
            .addSecs(600)
            .toString(Qt::ISODateWithMs));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(202, approval));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("awaitingDeviceApproval"));

    QJsonObject recovered;
    recovered.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("device-access"),
            QStringLiteral("device-refresh")));
    recovered.insert(
        QStringLiteral("recovery_key"),
        QStringLiteral("CLSM-DEVICE-REPLACEMENT-SENTINEL"));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/challenges/device/recovery-key"),
        okReply(200, recovered));

    fixture.controller->useRecoveryKeyForPendingDevice(
        QStringLiteral("CLSM-OLD-DEVICE-RECOVERY"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
    QVERIFY(fixture.credentials.loadActive().has_value());

    QCOMPARE(fixture.secretSink.presentCount(), 1);
    QCOMPARE(
        fixture.secretSink.lastPurpose(),
        AccountRecoveryKeyPurpose::DeviceChallengeRecovered);
    QCOMPARE(
        fixture.secretSink.recoveryKey(),
        QStringLiteral("CLSM-DEVICE-REPLACEMENT-SENTINEL"));

    const QByteArray persisted =
        ordinaryStateBytes(fixture.temp.path());
    QVERIFY(!persisted.contains(
        "CLSM-DEVICE-REPLACEMENT-SENTINEL"));
}

void tst_account_onboarding::
recoveryKeyNeverAppearsInControllerProperties() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    ControllerFixture fixture;

    const QString sentinel =
        QStringLiteral("CLSM-DIAGNOSTIC-LEAK-SENTINEL");

    QJsonObject body;
    body.insert(
        QStringLiteral("recovery_key"),
        sentinel);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/password/recover"),
        okReply(200, body));

    fixture.controller->recoverPassword(
        QStringLiteral("Hemanth56"),
        QStringLiteral("CLSM-OLD-RECOVERY"),
        QStringLiteral("another correct password 884"));

    QTRY_COMPARE(fixture.secretSink.presentCount(), 1);
    QCOMPARE(
        fixture.secretSink.lastPurpose(),
        AccountRecoveryKeyPurpose::PasswordRecovered);
    QCOMPARE(
        fixture.secretSink.recoveryKey(),
        sentinel);

    const QMetaObject *meta =
        fixture.controller->metaObject();
    for (int index = meta->propertyOffset();
         index < meta->propertyCount();
         ++index) {
        const QMetaProperty property = meta->property(index);
        const QString propertyName =
            QString::fromLatin1(property.name()).toLower();

        QVERIFY2(
            !propertyName.contains(QLatin1String("recoverykey"))
                && !propertyName.contains(QLatin1String("password"))
                && !propertyName.contains(QLatin1String("accesstoken"))
                && !propertyName.contains(QLatin1String("refreshtoken")),
            qPrintable(
                QStringLiteral(
                    "Secret-bearing controller property exposed: %1")
                    .arg(QString::fromLatin1(property.name()))));

        const QVariant value =
            property.read(fixture.controller.get());
        QVERIFY2(
            !value.toString().contains(sentinel),
            qPrintable(
                QStringLiteral(
                    "Recovery-key sentinel leaked through controller property %1")
                    .arg(QString::fromLatin1(property.name()))));
    }
}

void tst_account_onboarding::
presenterExposesPurposeOnlyBesideTransientKey() {
    MemoryAccountSensitiveClipboard clipboard;
    AccountRecoveryKeyPresenter presenter(&clipboard);

    const QString sentinel =
        QStringLiteral(
            "CLSM-TRANSIENT-RECOVERY-SENTINEL");

    QVERIFY(presenter.presentRecoveryKey(
        sentinel,
        AccountRecoveryKeyPurpose::PasswordRecovered));

    QVERIFY(presenter.active());
    QCOMPARE(
        presenter.purpose(),
        QStringLiteral("passwordRecovered"));
    QCOMPARE(presenter.recoveryKey(), sentinel);
    QCOMPARE(
        presenter.copyState(),
        QStringLiteral("idle"));

    presenter.dismiss();

    QVERIFY(!presenter.active());
    QVERIFY(presenter.recoveryKey().isEmpty());
    QCOMPARE(
        presenter.copyState(),
        QStringLiteral("idle"));
}

void tst_account_onboarding::presenterReportsClipboardFailure() {
    MemoryAccountSensitiveClipboard clipboard;
    clipboard.setFailCopy(true);

    AccountRecoveryKeyPresenter presenter(&clipboard);
    QVERIFY(presenter.presentRecoveryKey(
        QStringLiteral("CLSM-COPY-FAIL-SENTINEL"),
        AccountRecoveryKeyPurpose::AccountCreated));

    QVERIFY(!presenter.copyRecoveryKey());
    QCOMPARE(
        presenter.copyState(),
        QStringLiteral("failed"));
    QCOMPARE(clipboard.copyCount(), 1);
}

void tst_account_onboarding::presenterClearsOnlyUnchangedClipboard() {
    MemoryAccountSensitiveClipboard clipboard;
    AccountRecoveryKeyPresenter presenter(&clipboard);
    presenter.setClipboardClearDelayForTests(0);

    const QString sentinel =
        QStringLiteral("CLSM-CLEAR-ME-SENTINEL");
    QVERIFY(presenter.presentRecoveryKey(
        sentinel,
        AccountRecoveryKeyPurpose::AccountCreated));
    QVERIFY(presenter.copyRecoveryKey());

    QTRY_COMPARE(clipboard.clearAttemptCount(), 1);
    QCOMPARE(clipboard.clearCount(), 1);
    QVERIFY(clipboard.currentText().isEmpty());
}

void tst_account_onboarding::
presenterPreservesNewerClipboardContent() {
    MemoryAccountSensitiveClipboard clipboard;
    AccountRecoveryKeyPresenter presenter(&clipboard);
    presenter.setClipboardClearDelayForTests(1);

    QVERIFY(presenter.presentRecoveryKey(
        QStringLiteral("CLSM-OLD-RECOVERY-SENTINEL"),
        AccountRecoveryKeyPurpose::AccountCreated));
    QVERIFY(presenter.copyRecoveryKey());

    clipboard.replaceCurrentText(
        QStringLiteral("newer user clipboard content"));

    QTRY_COMPARE(clipboard.clearAttemptCount(), 1);
    QCOMPARE(clipboard.clearCount(), 0);
    QCOMPARE(
        clipboard.currentText(),
        QStringLiteral("newer user clipboard content"));
}

void tst_account_onboarding::
serviceEndpointHasNoInventedProductionDefault() {
    ScopedEnvironmentVariable restore(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");

    qunsetenv("COLOSSEUM_ACCOUNT_SERVICE_URL");
#ifndef COLOSSEUM_ACCOUNT_SERVICE_URL
    QVERIFY(AccountServiceEndpoint::configuredUrl().isEmpty());
#endif

    qputenv(
        "COLOSSEUM_ACCOUNT_SERVICE_URL",
        QByteArrayLiteral("http://127.0.0.1:8099"));

    QCOMPARE(
        AccountServiceEndpoint::configuredUrl(),
        QUrl(QStringLiteral("http://127.0.0.1:8099")));
}

QTEST_MAIN(tst_account_onboarding)
#include "tst_account_onboarding.moc"
