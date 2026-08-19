// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountBootstrapStore.h"
#include "account/AccountClient.h"
#include "account/AccountController.h"
#include "account/AccountDeviceIdentity.h"
#include "account/AccountHttpTransport.h"
#include "AccountFixtureTransport.h"
#include "MemoryAccountCredentialStore.h"
#include "MemoryAccountOneTimeSecretSink.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {
constexpr auto kAccountId =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceId =
    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";

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

class CapturingTransport final : public AccountTransport {
public:
    explicit CapturingTransport(QObject *parent = nullptr)
        : AccountTransport(parent) {}

    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override {
        lastRequestId = requestId;
        lastRequest = request;
        ++sendCount;
    }

    quint64 lastRequestId = 0;
    AccountTransportRequest lastRequest;
    int sendCount = 0;
};

class DeferredAccountTransport final : public AccountTransport {
public:
    struct Pending {
        quint64 requestId = 0;
        AccountTransportRequest request;
    };

    explicit DeferredAccountTransport(QObject *parent = nullptr)
        : AccountTransport(parent) {}

    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override {
        pending.append(Pending{requestId, request});
    }

    quint64 requestIdFor(
        const QByteArray &method,
        const QString &path) const {
        for (const Pending &item : pending) {
            if (item.request.method == method
                && item.request.path == path) {
                return item.requestId;
            }
        }
        return 0;
    }

    int pendingCount(
        const QByteArray &method,
        const QString &path) const {
        int count = 0;
        for (const Pending &item : pending) {
            if (item.request.method == method
                && item.request.path == path) {
                ++count;
            }
        }
        return count;
    }

    void complete(
        quint64 requestId,
        const AccountTransportReply &reply) {
        for (qsizetype i = 0; i < pending.size(); ++i) {
            if (pending.at(i).requestId != requestId)
                continue;
            pending.removeAt(i);
            emit finished(requestId, reply);
            return;
        }
    }

    QList<Pending> pending;
};

AccountTransportReply okReply(
    int statusCode,
    const QJsonObject &body = QJsonObject()) {
    AccountTransportReply reply;
    reply.statusCode = statusCode;
    reply.body = body;
    return reply;
}

AccountTransportReply errorReply(
    int statusCode,
    const QString &code,
    const QString &message) {
    AccountTransportReply reply;
    reply.statusCode = statusCode;
    reply.errorCode = code;
    reply.errorMessage = message;
    return reply;
}

QJsonObject sessionObject(
    const QString &username,
    const QString &accessToken,
    const QString &refreshToken,
    bool protection = false,
    const QString &accountId = QString::fromLatin1(kAccountId),
    const QString &deviceId = QString::fromLatin1(kDeviceId)) {
    QJsonObject account;
    account.insert(QStringLiteral("id"), accountId);
    account.insert(QStringLiteral("username"), username);
    account.insert(
        QStringLiteral("protect_new_device_signins"),
        protection);

    QJsonObject device;
    device.insert(QStringLiteral("id"), deviceId);
    device.insert(
        QStringLiteral("install_id"),
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111"));
    device.insert(
        QStringLiteral("label"),
        QStringLiteral("Windows desktop"));
    device.insert(
        QStringLiteral("platform"),
        QStringLiteral("Windows"));
    device.insert(QStringLiteral("trusted"), true);
    device.insert(
        QStringLiteral("last_seen_at"),
        QDateTime::currentDateTimeUtc()
            .toString(Qt::ISODateWithMs));

    QJsonObject session;
    session.insert(QStringLiteral("account"), account);
    session.insert(QStringLiteral("device"), device);
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

QByteArray ordinaryStateBytes(const QString &root) {
    QByteArray bytes;
    QDirIterator iterator(
        root,
        QDir::Files,
        QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        QFile file(iterator.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        bytes += file.readAll();
        bytes += '\n';
    }
    return bytes;
}

struct Fixture {
    QTemporaryDir temp;
    std::unique_ptr<AccountFixtureTransport> transport;
    MemoryAccountCredentialStore credentials;
    MemoryAccountOneTimeSecretSink secretSink;
    std::unique_ptr<AccountClient> client;
    std::unique_ptr<AccountDeviceIdentity> deviceIdentity;
    std::unique_ptr<AccountBootstrapStore> bootstrapStore;
    std::unique_ptr<AccountController> controller;

    Fixture() {
        if (!temp.isValid())
            qFatal("Could not create account identity test directory.");

        qputenv(
            "COLOSSEUM_APPDATA_TAG",
            QByteArrayLiteral("account-identity-test"));

        transport = AccountFixtureTransport::create();
        if (!transport)
            qFatal("Fixture transport refused tagged test session.");

        client = std::make_unique<AccountClient>(
            transport.get());
        deviceIdentity =
            std::make_unique<AccountDeviceIdentity>(
                temp.path()
                + QLatin1String("/device.ini"));
        bootstrapStore =
            std::make_unique<AccountBootstrapStore>(
                temp.path()
                + QLatin1String("/bootstrap.ini"));
        controller =
            std::make_unique<AccountController>(
                client.get(),
                &credentials,
                deviceIdentity.get(),
                bootstrapStore.get(),
                &secretSink);
        controller->setAutomaticPollingEnabled(false);
    }
};

void queueRestore(
    Fixture &fixture,
    const QByteArray &refreshToken,
    const QString &username = QStringLiteral("Hemanth56"),
    const QString &accessToken = QStringLiteral("access-restored")) {
    QJsonObject body;
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            username,
            accessToken,
            QString::fromLatin1(refreshToken)));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"),
        okReply(200, body));
}

void seedRememberedCredential(
    Fixture &fixture,
    const QByteArray &refreshToken) {
    StoredAccountCredential credential;
    credential.accountId = QString::fromLatin1(kAccountId);
    credential.deviceId = QString::fromLatin1(kDeviceId);
    credential.refreshToken = refreshToken;
    QVERIFY(fixture.credentials.saveActive(credential));
}

void restoreSignedIn(
    Fixture &fixture,
    const QByteArray &refreshToken =
        QByteArrayLiteral("refresh-restored")) {
    seedRememberedCredential(fixture, refreshToken);
    queueRestore(fixture, refreshToken);
    fixture.controller->restoreRememberedSession();
    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
}
}

class tst_account_identity : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void fixtureTransportRefusesUntaggedUse();
    void httpTransportRejectsUnsafeBaseUrls();
    void httpTransportDoesNotFollowRedirects();
    void accountClientPreservesPasswordWhitespace();

    void controllerExposesOnlySafeStateProperties();
    void deviceIdentityIsStableAndNonSecret();
    void localOnlyChoiceSurvivesRestart();
    void returnToSignInLeavesLocalOnlyForOnboarding();
    void rememberedSessionRestartRotatesSecureCredential();
    void offlineRestoreKeepsRememberedCredential();
    void revokedRestoreClearsCredentialAndLocks();
    void protectedBearerRejectionRecoversWithoutLogout();
    void staleBearerFailureCannotTriggerAnotherRefreshAfterRotation();
    void staleGenerationListDevicesDropEmitsDeviceListRefreshFailed();
    void staleGenerationReconciledRevokeDropEmitsDeviceRevokeFailed();
    void staleGenerationSetNewDeviceProtectionDropEmitsAccountError();

    void createAccountKeepsSecretsOutOfOrdinaryState();
    void secureStoreFailureFailsClosed();
    void staleSignInReplyCannotUndoLocalOnlyChoice();
    void protectedSignInPersistsNothingBeforeApproval();
    void trustedRecoveryUsesNativeOnlySecretSink();

    void offlineLogoutQueuesDurablePendingRevocation();
    void logoutEverywhereSessionInvalidStillSignsOutExplicitly();
    void pendingRevocationFlushRemovesOnlyConfirmedToken();
    void failedCredentialClearTombstonePreventsResurrection();
    void currentDeviceRevokeTransitionsToLocked();

    void syncBlockedAndDeletionPendingStatesAreSafe();
    void stableErrorCategoryMapsRateLimit();
    void deviceListUpdatesSafeCount();

    void profileRefreshExposesPersistedAvatar();
    void avatarMutationFailurePreservesPersistedAvatar();
    void passwordChangeSuccessEmitsCompletionSignal();
    void passwordChangeFailureDoesNotEmitCompletionSignal();
    void manualRecoveryReplacementUsesOneTimeSecretSink();
    void failedManualRecoveryReplacementDoesNotPresentSecret();
    void profileMutationSignalsAreOperationSpecific();
    void recoveryReplacementSignalsAreOperationSpecific();
    void devicesExposeServerCurrentDeviceIdentityToQml();
    void malformedDeviceListIsProtocolFailureAndPreservesPriorList();
    void revokeSuccessWaitsForAuthoritativeRefresh();
};

void tst_account_identity::initTestCase() {
    qRegisterMetaType<AccountOperation>();
    qRegisterMetaType<AccountTransportReply>();
}

void tst_account_identity::fixtureTransportRefusesUntaggedUse() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qunsetenv("COLOSSEUM_APPDATA_TAG");

    QVERIFY(!AccountFixtureTransport::testModeAllowed());
    QVERIFY(AccountFixtureTransport::create() == nullptr);
}

void tst_account_identity::httpTransportRejectsUnsafeBaseUrls() {
    QVERIFY(AccountHttpTransport::isAllowedBaseUrl(
        QUrl(QStringLiteral(
            "https://accounts.example.test"))));
    QVERIFY(AccountHttpTransport::isAllowedBaseUrl(
        QUrl(QStringLiteral(
            "http://127.0.0.1:8080"))));
    QVERIFY(AccountHttpTransport::isAllowedBaseUrl(
        QUrl(QStringLiteral(
            "http://localhost:8080"))));

    QVERIFY(!AccountHttpTransport::isAllowedBaseUrl(
        QUrl(QStringLiteral(
            "http://accounts.example.test"))));
    QVERIFY(!AccountHttpTransport::isAllowedBaseUrl(
        QUrl(QStringLiteral(
            "file:///tmp/account"))));
    QVERIFY(!AccountHttpTransport::isAllowedBaseUrl(
        QUrl(QStringLiteral(
            "https://user:pass@accounts.example.test"))));
}

void tst_account_identity::httpTransportDoesNotFollowRedirects() {
    QTcpServer server;
    QVERIFY(server.listen(
        QHostAddress::LocalHost,
        0));

    int requestCount = 0;
    connect(
        &server,
        &QTcpServer::newConnection,
        &server,
        [&server, &requestCount]() {
            while (server.hasPendingConnections()) {
                QTcpSocket *socket =
                    server.nextPendingConnection();
                QObject::connect(
                    socket,
                    &QTcpSocket::readyRead,
                    socket,
                    [socket, &requestCount]() {
                        socket->readAll();
                        ++requestCount;
                        socket->write(
                            "HTTP/1.1 302 Found\r\n"
                            "Location: /v1/second\r\n"
                            "Content-Length: 0\r\n"
                            "Connection: close\r\n"
                            "\r\n");
                        socket->flush();
                        socket->disconnectFromHost();
                    });
            }
        });

    AccountHttpTransport transport(
        QUrl(
            QStringLiteral("http://127.0.0.1:%1/")
                .arg(server.serverPort())));

    QSignalSpy spy(
        &transport,
        &AccountTransport::finished);

    AccountTransportRequest request;
    request.method = QByteArrayLiteral("GET");
    request.path = QStringLiteral("/v1/first");
    request.bearerToken =
        QByteArrayLiteral("bearer-redirect-sentinel");

    transport.send(1, request);

    QTRY_COMPARE(spy.count(), 1);
    const QList<QVariant> arguments = spy.takeFirst();
    const AccountTransportReply reply =
        arguments.at(1).value<AccountTransportReply>();

    QCOMPARE(reply.statusCode, 302);
    QCOMPARE(
        reply.errorCode,
        QStringLiteral("redirect_not_allowed"));
    QCOMPARE(requestCount, 1);
}

void tst_account_identity::accountClientPreservesPasswordWhitespace() {
    CapturingTransport transport;
    AccountClient client(&transport);

    const QString password =
        QStringLiteral(
            "  leading and trailing spaces stay password data  ");

    client.signIn(
        QStringLiteral("Hemanth56"),
        password,
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111"),
        QStringLiteral("Windows desktop"),
        QStringLiteral("Windows"));

    QCOMPARE(transport.sendCount, 1);
    QCOMPARE(
        transport.lastRequest.body
            .value(QStringLiteral("password"))
            .toString(),
        password);
    QVERIFY(transport.lastRequest.bearerToken.isEmpty());
}

void tst_account_identity::controllerExposesOnlySafeStateProperties() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    QCOMPARE(
        fixture.controller->objectName(),
        QStringLiteral("accountController"));

    const QMetaObject *meta =
        fixture.controller->metaObject();

    for (int index = meta->propertyOffset();
         index < meta->propertyCount();
         ++index) {
        const QByteArray name =
            QByteArray(meta->property(index).name()).toLower();

        QVERIFY2(
            !name.contains("password"),
            name.constData());
        QVERIFY2(
            !name.contains("token"),
            name.constData());
        QVERIFY2(
            !name.contains("recovery"),
            name.constData());
        QVERIFY2(
            !name.contains("secret"),
            name.constData());
    }

    const QList<QByteArray> expectedProperties = {
        QByteArrayLiteral("mode"),
        QByteArrayLiteral("syncState"),
        QByteArrayLiteral("restoreStage"),
        QByteArrayLiteral("username"),
        QByteArrayLiteral("onboardingRequired"),
        QByteArrayLiteral("deviceCount"),
        QByteArrayLiteral("newDeviceProtection"),
        QByteArrayLiteral("pendingOutboxCount"),
        QByteArrayLiteral("deletionEffectiveAt"),
        QByteArrayLiteral("errorCategory"),
        QByteArrayLiteral("lastErrorCode"),
        QByteArrayLiteral("lastErrorMessage"),
        QByteArrayLiteral("busy")
    };

    for (const QByteArray &name : expectedProperties) {
        QVERIFY2(
            meta->indexOfProperty(name.constData()) >= 0,
            name.constData());
    }
}

void tst_account_identity::deviceIdentityIsStableAndNonSecret() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path =
        temp.path() + QLatin1String("/device.ini");

    AccountDeviceIdentity first(path);
    const QString installId = first.installId();
    QVERIFY(!installId.isEmpty());

    AccountDeviceIdentity second(path);
    QCOMPARE(second.installId(), installId);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray persisted = file.readAll();

    QVERIFY(persisted.contains(installId.toUtf8()));
    QVERIFY(!persisted.contains("password"));
    QVERIFY(!persisted.contains("refresh"));
    QVERIFY(!persisted.contains("access"));
    QVERIFY(!persisted.contains("recovery"));
    QVERIFY(!persisted.contains("token"));
}

void tst_account_identity::localOnlyChoiceSurvivesRestart() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    fixture.controller->continueWithoutAccount();
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));
    QVERIFY(fixture.bootstrapStore->localOnlyChosen());

    fixture.controller.reset();
    fixture.client.reset();
    fixture.transport.reset();

    fixture.transport = AccountFixtureTransport::create();
    QVERIFY(fixture.transport);
    fixture.client = std::make_unique<AccountClient>(
        fixture.transport.get());
    fixture.controller =
        std::make_unique<AccountController>(
            fixture.client.get(),
            &fixture.credentials,
            fixture.deviceIdentity.get(),
            fixture.bootstrapStore.get(),
            &fixture.secretSink);
    fixture.controller->setAutomaticPollingEnabled(false);

    fixture.controller->restoreRememberedSession();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));
    QCOMPARE(
        fixture.controller->restoreStage(),
        QStringLiteral("none"));
}

void tst_account_identity::returnToSignInLeavesLocalOnlyForOnboarding() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    // Enter guest/local-only mode the way the Welcome screen's
    // "Continue without account" does.
    fixture.controller->continueWithoutAccount();
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));
    QVERIFY(fixture.bootstrapStore->localOnlyChosen());

    // The escape hatch: back to the sign-in choice. The account overlay is
    // mode-driven, so SignedOut is what re-shows Welcome (Create / Sign in /
    // Continue without account).
    fixture.controller->returnToSignIn();
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    // The persisted guest choice is forgotten so a restart does not silently
    // skip onboarding again.
    QVERIFY(!fixture.bootstrapStore->localOnlyChosen());

    // From SignedOut the onboarding actions are reachable again: choosing guest
    // mode a second time still works (no remembered credential blocks it).
    fixture.controller->continueWithoutAccount();
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));

    // returnToSignIn only acts from local-only; it is a no-op from signedOut.
    fixture.controller->returnToSignIn();
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    fixture.controller->returnToSignIn();
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
}

void tst_account_identity::rememberedSessionRestartRotatesSecureCredential() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    const QByteArray firstRefresh =
        QByteArrayLiteral("refresh-first-session");

    QJsonObject signInBody;
    signInBody.insert(
        QStringLiteral("status"),
        QStringLiteral("signed_in"));
    signInBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-first"),
            QString::fromLatin1(firstRefresh)));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(200, signInBody));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral(
            "A sufficiently long password 123"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));

    fixture.controller.reset();
    fixture.client.reset();
    fixture.transport.reset();

    fixture.transport = AccountFixtureTransport::create();
    QVERIFY(fixture.transport);
    fixture.client = std::make_unique<AccountClient>(
        fixture.transport.get());

    const QByteArray secondRefresh =
        QByteArrayLiteral("refresh-after-restart");

    QJsonObject refreshBody;
    refreshBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-after-restart"),
            QString::fromLatin1(secondRefresh)));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"),
        okReply(200, refreshBody));

    fixture.controller =
        std::make_unique<AccountController>(
            fixture.client.get(),
            &fixture.credentials,
            fixture.deviceIdentity.get(),
            fixture.bootstrapStore.get(),
            &fixture.secretSink);
    fixture.controller->setAutomaticPollingEnabled(false);

    fixture.controller->restoreRememberedSession();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("restoring"));
    QCOMPARE(
        fixture.controller->restoreStage(),
        QStringLiteral("sessionRefresh"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
    QCOMPARE(
        fixture.controller->restoreStage(),
        QStringLiteral("restored"));

    const auto stored = fixture.credentials.loadActive();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->refreshToken, secondRefresh);
}

void tst_account_identity::offlineRestoreKeepsRememberedCredential() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    const QByteArray refreshToken =
        QByteArrayLiteral("refresh-offline-sentinel");
    seedRememberedCredential(fixture, refreshToken);

    fixture.transport->setOnline(false);
    fixture.controller->restoreRememberedSession();

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("offline"));
    QCOMPARE(
        fixture.controller->syncState(),
        QStringLiteral("retrying"));
    QCOMPARE(
        fixture.controller->restoreStage(),
        QStringLiteral("offline"));
    QCOMPARE(
        fixture.controller->errorCategory(),
        QStringLiteral("offline"));

    const auto stored = fixture.credentials.loadActive();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->refreshToken, refreshToken);
}

void tst_account_identity::revokedRestoreClearsCredentialAndLocks() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    const QByteArray refreshToken =
        QByteArrayLiteral("refresh-revoked-sentinel");
    seedRememberedCredential(fixture, refreshToken);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"),
        errorReply(
            401,
            QStringLiteral("session_revoked"),
            QStringLiteral("This session was signed out.")));

    QSignalSpy lockedSpy(
        fixture.controller.get(),
        &AccountController::currentDeviceLocked);

    fixture.controller->restoreRememberedSession();

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("locked"));
    QCOMPARE(lockedSpy.count(), 1);

    QVERIFY(!fixture.credentials.loadActive().has_value());
    QVERIFY(fixture.client->accessToken().isEmpty());
    QVERIFY(fixture.controller->username().isEmpty());
    QCOMPARE(
        fixture.controller->syncState(),
        QStringLiteral("inactive"));
}

void tst_account_identity::protectedBearerRejectionRecoversWithoutLogout() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture, QByteArrayLiteral("refresh-before-recovery"));

    QSignalSpy lockedSpy(
        fixture.controller.get(),
        &AccountController::currentDeviceLocked);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"),
        QStringLiteral("/v1/profile"),
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("The access token is no longer current.")));

    QJsonObject refreshBody;
    refreshBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-after-recovery"),
            QStringLiteral("refresh-after-recovery")));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"),
        okReply(200, refreshBody));

    fixture.controller->refreshProfile();

    QTRY_COMPARE(
        fixture.client->accessToken(),
        QByteArrayLiteral("access-after-recovery"));
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
    QCOMPARE(lockedSpy.count(), 0);

    const auto stored = fixture.credentials.loadActive();
    QVERIFY(stored.has_value());
    QCOMPARE(
        stored->refreshToken,
        QByteArrayLiteral("refresh-after-recovery"));
    QCOMPARE(
        fixture.client->accessToken(),
        QByteArrayLiteral("access-after-recovery"));
}

void tst_account_identity::staleBearerFailureCannotTriggerAnotherRefreshAfterRotation() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("account-stale-bearer-test"));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DeferredAccountTransport transport;
    MemoryAccountCredentialStore credentials;
    MemoryAccountOneTimeSecretSink secretSink;
    AccountClient client(&transport);
    AccountDeviceIdentity deviceIdentity(temp.path() + QLatin1String("/device.ini"));
    AccountBootstrapStore bootstrapStore(temp.path() + QLatin1String("/bootstrap.ini"));
    AccountController controller(
        &client,
        &credentials,
        &deviceIdentity,
        &bootstrapStore,
        &secretSink);
    controller.setAutomaticPollingEnabled(false);

    StoredAccountCredential credential;
    credential.accountId = QString::fromLatin1(kAccountId);
    credential.deviceId = QString::fromLatin1(kDeviceId);
    credential.refreshToken = QByteArrayLiteral("refresh-a");
    QVERIFY(credentials.saveActive(credential));

    controller.restoreRememberedSession();
    QTRY_COMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        1);
    const quint64 restoreId = transport.requestIdFor(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"));

    QJsonObject restoreBody;
    restoreBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-a"),
            QStringLiteral("refresh-a")));
    transport.complete(restoreId, okReply(200, restoreBody));
    QTRY_COMPARE(client.accessToken(), QByteArrayLiteral("access-a"));

    QSignalSpy lockedSpy(&controller, &AccountController::currentDeviceLocked);
    controller.refreshProfile();
    controller.refreshDevices();
    const quint64 profileRequest = transport.requestIdFor(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/profile"));
    const quint64 devicesRequest = transport.requestIdFor(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"));
    QVERIFY(profileRequest != 0);
    QVERIFY(devicesRequest != 0);

    transport.complete(
        profileRequest,
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("stale access token")));
    QTRY_COMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        1);
    const quint64 recoveryId = transport.requestIdFor(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"));

    QJsonObject recoveryBody;
    recoveryBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-b"),
            QStringLiteral("refresh-b")));
    transport.complete(recoveryId, okReply(200, recoveryBody));
    QTRY_COMPARE(client.accessToken(), QByteArrayLiteral("access-b"));

    transport.complete(
        devicesRequest,
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("late response from access-a")));
    QTest::qWait(50);

    QCOMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        0);
    QCOMPARE(lockedSpy.count(), 0);
    QCOMPARE(client.accessToken(), QByteArrayLiteral("access-b"));
    const auto stored = credentials.loadActive();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->refreshToken, QByteArrayLiteral("refresh-b"));
}

void tst_account_identity::staleGenerationListDevicesDropEmitsDeviceListRefreshFailed() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("account-stale-list-devices-test"));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DeferredAccountTransport transport;
    MemoryAccountCredentialStore credentials;
    MemoryAccountOneTimeSecretSink secretSink;
    AccountClient client(&transport);
    AccountDeviceIdentity deviceIdentity(temp.path() + QLatin1String("/device.ini"));
    AccountBootstrapStore bootstrapStore(temp.path() + QLatin1String("/bootstrap.ini"));
    AccountController controller(
        &client,
        &credentials,
        &deviceIdentity,
        &bootstrapStore,
        &secretSink);
    controller.setAutomaticPollingEnabled(false);

    StoredAccountCredential credential;
    credential.accountId = QString::fromLatin1(kAccountId);
    credential.deviceId = QString::fromLatin1(kDeviceId);
    credential.refreshToken = QByteArrayLiteral("refresh-a");
    QVERIFY(credentials.saveActive(credential));

    controller.restoreRememberedSession();
    QTRY_COMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        1);
    const quint64 restoreId = transport.requestIdFor(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"));

    QJsonObject restoreBody;
    restoreBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-a"),
            QStringLiteral("refresh-a")));
    transport.complete(restoreId, okReply(200, restoreBody));
    QTRY_COMPARE(client.accessToken(), QByteArrayLiteral("access-a"));

    QSignalSpy refreshFailureSpy(
        &controller, &AccountController::deviceListRefreshFailed);
    QSignalSpy refreshSuccessSpy(
        &controller, &AccountController::deviceListRefreshSucceeded);
    QSignalSpy revokeFailureSpy(
        &controller, &AccountController::deviceRevokeFailed);
    QSignalSpy lockedSpy(&controller, &AccountController::currentDeviceLocked);

    // Two requests go out under access-a: a plain ListDevices refresh
    // (the request under test) and a profile refresh that will drive the
    // real, non-stale recovery.
    controller.refreshDevices();
    controller.refreshProfile();
    const quint64 devicesRequest = transport.requestIdFor(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"));
    const quint64 profileRequest = transport.requestIdFor(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/profile"));
    QVERIFY(devicesRequest != 0);
    QVERIFY(profileRequest != 0);

    transport.complete(
        profileRequest,
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("stale access token")));
    QTRY_COMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        1);
    const quint64 recoveryId = transport.requestIdFor(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"));

    QJsonObject recoveryBody;
    recoveryBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-b"),
            QStringLiteral("refresh-b")));
    transport.complete(recoveryId, okReply(200, recoveryBody));
    QTRY_COMPARE(client.accessToken(), QByteArrayLiteral("access-b"));

    // The ListDevices reply arrives late, tagged with the pre-rotation
    // access-token generation. It must not start a second refresh, but the
    // page waiting on it still needs its failure signal so a
    // "Refreshing..." spinner clears.
    transport.complete(
        devicesRequest,
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("late response from access-a")));

    QTRY_COMPARE(refreshFailureSpy.count(), 1);
    QCOMPARE(refreshSuccessSpy.count(), 0);
    QCOMPARE(revokeFailureSpy.count(), 0);
    const QList<QVariant> args = refreshFailureSpy.takeFirst();
    QCOMPARE(
        args.at(0).toString(),
        QStringLiteral(
            "The session was refreshed while this request was "
            "in flight. Try again."));
    QCOMPARE(args.at(1).toString(), QStringLiteral("unavailable"));
    QCOMPARE(args.at(2).toString(), QStringLiteral("stale_session_retry"));

    QCOMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        0);
    QCOMPARE(lockedSpy.count(), 0);
    QCOMPARE(controller.mode(), QStringLiteral("signedIn"));
    QCOMPARE(client.accessToken(), QByteArrayLiteral("access-b"));
    const auto stored = credentials.loadActive();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->refreshToken, QByteArrayLiteral("refresh-b"));
}

void tst_account_identity::staleGenerationReconciledRevokeDropEmitsDeviceRevokeFailed() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("account-stale-revoke-reconcile-test"));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DeferredAccountTransport transport;
    MemoryAccountCredentialStore credentials;
    MemoryAccountOneTimeSecretSink secretSink;
    AccountClient client(&transport);
    AccountDeviceIdentity deviceIdentity(temp.path() + QLatin1String("/device.ini"));
    AccountBootstrapStore bootstrapStore(temp.path() + QLatin1String("/bootstrap.ini"));
    AccountController controller(
        &client,
        &credentials,
        &deviceIdentity,
        &bootstrapStore,
        &secretSink);
    controller.setAutomaticPollingEnabled(false);

    StoredAccountCredential credential;
    credential.accountId = QString::fromLatin1(kAccountId);
    credential.deviceId = QString::fromLatin1(kDeviceId);
    credential.refreshToken = QByteArrayLiteral("refresh-a");
    QVERIFY(credentials.saveActive(credential));

    controller.restoreRememberedSession();
    QTRY_COMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        1);
    const quint64 restoreId = transport.requestIdFor(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"));

    QJsonObject restoreBody;
    restoreBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-a"),
            QStringLiteral("refresh-a")));
    transport.complete(restoreId, okReply(200, restoreBody));
    QTRY_COMPARE(client.accessToken(), QByteArrayLiteral("access-a"));

    const QString otherId =
        QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc");

    QSignalSpy revokeFailureSpy(
        &controller, &AccountController::deviceRevokeFailed);
    QSignalSpy revokeSuccessSpy(
        &controller, &AccountController::deviceRevokeSucceeded);
    QSignalSpy refreshFailureSpy(
        &controller, &AccountController::deviceListRefreshFailed);
    QSignalSpy lockedSpy(&controller, &AccountController::currentDeviceLocked);

    controller.revokeDevice(otherId);
    const quint64 revokeRequest = transport.requestIdFor(
        QByteArrayLiteral("DELETE"),
        QStringLiteral("/v1/devices/") + otherId);
    QVERIFY(revokeRequest != 0);
    transport.complete(revokeRequest, okReply(204));

    // A successful revoke chains an authoritative ListDevices reconciliation
    // request. Wait for it to be sent, then hold it open under access-a.
    QTRY_VERIFY(
        transport.requestIdFor(
            QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"))
        != 0);
    const quint64 reconcileRequest = transport.requestIdFor(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"));

    // Rotate the access token out from under the held-open reconciliation
    // request via an unrelated tracked operation's real (non-stale)
    // recovery.
    controller.refreshProfile();
    const quint64 profileRequest = transport.requestIdFor(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/profile"));
    QVERIFY(profileRequest != 0);
    transport.complete(
        profileRequest,
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("stale access token")));
    QTRY_COMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        1);
    const quint64 recoveryId = transport.requestIdFor(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"));

    QJsonObject recoveryBody;
    recoveryBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-b"),
            QStringLiteral("refresh-b")));
    transport.complete(recoveryId, okReply(200, recoveryBody));
    QTRY_COMPARE(client.accessToken(), QByteArrayLiteral("access-b"));

    // Deliver the stale reconciliation reply, tagged with the pre-rotation
    // generation. The device revoke's own completion signal must fire
    // (mirroring the success path, which also prefers deviceRevokeSucceeded
    // over the generic list-refresh signal for a reconciled device), and no
    // second refresh may start.
    transport.complete(
        reconcileRequest,
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("late reconciliation reply")));

    QTRY_COMPARE(revokeFailureSpy.count(), 1);
    QCOMPARE(revokeSuccessSpy.count(), 0);
    QCOMPARE(refreshFailureSpy.count(), 0);
    const QList<QVariant> args = revokeFailureSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), otherId);
    QCOMPARE(
        args.at(1).toString(),
        QStringLiteral(
            "The session was refreshed while this request was "
            "in flight. Try again."));
    QCOMPARE(args.at(2).toString(), QStringLiteral("unavailable"));
    QCOMPARE(args.at(3).toString(), QStringLiteral("stale_session_retry"));

    QCOMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        0);
    QCOMPARE(lockedSpy.count(), 0);
    QCOMPARE(controller.mode(), QStringLiteral("signedIn"));
    QCOMPARE(client.accessToken(), QByteArrayLiteral("access-b"));
    const auto stored = credentials.loadActive();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->refreshToken, QByteArrayLiteral("refresh-b"));
}

void tst_account_identity::staleGenerationSetNewDeviceProtectionDropEmitsAccountError() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("account-stale-protection-test"));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DeferredAccountTransport transport;
    MemoryAccountCredentialStore credentials;
    MemoryAccountOneTimeSecretSink secretSink;
    AccountClient client(&transport);
    AccountDeviceIdentity deviceIdentity(temp.path() + QLatin1String("/device.ini"));
    AccountBootstrapStore bootstrapStore(temp.path() + QLatin1String("/bootstrap.ini"));
    AccountController controller(
        &client,
        &credentials,
        &deviceIdentity,
        &bootstrapStore,
        &secretSink);
    controller.setAutomaticPollingEnabled(false);

    StoredAccountCredential credential;
    credential.accountId = QString::fromLatin1(kAccountId);
    credential.deviceId = QString::fromLatin1(kDeviceId);
    credential.refreshToken = QByteArrayLiteral("refresh-a");
    QVERIFY(credentials.saveActive(credential));

    controller.restoreRememberedSession();
    QTRY_COMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        1);
    const quint64 restoreId = transport.requestIdFor(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"));

    QJsonObject restoreBody;
    restoreBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-a"),
            QStringLiteral("refresh-a")));
    transport.complete(restoreId, okReply(200, restoreBody));
    QTRY_COMPARE(client.accessToken(), QByteArrayLiteral("access-a"));

    const bool initialProtection = controller.newDeviceProtection();

    QSignalSpy accountErrorSpy(
        &controller, &AccountController::accountError);
    QSignalSpy protectionChangedSpy(
        &controller, &AccountController::newDeviceProtectionChanged);
    QSignalSpy lockedSpy(&controller, &AccountController::currentDeviceLocked);

    // Two requests go out under access-a: the SetNewDeviceProtection toggle
    // (the request under test) and a profile refresh that will drive the
    // real, non-stale recovery.
    controller.setNewDeviceProtection(!initialProtection);
    controller.refreshProfile();
    const quint64 protectionRequest = transport.requestIdFor(
        QByteArrayLiteral("PUT"),
        QStringLiteral("/v1/security/new-device-protection"));
    const quint64 profileRequest = transport.requestIdFor(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/profile"));
    QVERIFY(protectionRequest != 0);
    QVERIFY(profileRequest != 0);

    transport.complete(
        profileRequest,
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("stale access token")));
    QTRY_COMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        1);
    const quint64 recoveryId = transport.requestIdFor(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"));

    QJsonObject recoveryBody;
    recoveryBody.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-b"),
            QStringLiteral("refresh-b")));
    transport.complete(recoveryId, okReply(200, recoveryBody));
    QTRY_COMPARE(client.accessToken(), QByteArrayLiteral("access-b"));

    // The SetNewDeviceProtection reply arrives late, tagged with the
    // pre-rotation access-token generation. It must not start a second
    // refresh, but the security page waiting on it still needs its
    // accountError signal so protectionRequestPending clears.
    transport.complete(
        protectionRequest,
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("late response from access-a")));

    QTRY_COMPARE(accountErrorSpy.count(), 1);
    QCOMPARE(protectionChangedSpy.count(), 0);
    const QList<QVariant> args = accountErrorSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("unavailable"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("stale_session_retry"));
    QCOMPARE(
        args.at(2).toString(),
        QStringLiteral(
            "The session was refreshed while this request was "
            "in flight. Try again."));

    QCOMPARE(
        transport.pendingCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        0);
    QCOMPARE(lockedSpy.count(), 0);
    QCOMPARE(controller.mode(), QStringLiteral("signedIn"));
    QCOMPARE(client.accessToken(), QByteArrayLiteral("access-b"));
    QCOMPARE(controller.newDeviceProtection(), initialProtection);
    const auto stored = credentials.loadActive();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->refreshToken, QByteArrayLiteral("refresh-b"));
}

void tst_account_identity::createAccountKeepsSecretsOutOfOrdinaryState() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    const QString password =
        QStringLiteral(
            " exact password spaces remain valid 773 ");
    const QString recoveryKey =
        QStringLiteral("ABCDE-FGHIJ-KLMNO-PQRST-UVWXYZ");
    const QString accessToken =
        QStringLiteral("access-create-sentinel");
    const QString refreshToken =
        QStringLiteral("refresh-create-sentinel");

    QJsonObject body;
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            accessToken,
            refreshToken));
    body.insert(
        QStringLiteral("recovery_key"),
        recoveryKey);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/accounts"),
        okReply(201, body));

    fixture.controller->createAccount(
        QStringLiteral("Hemanth56"),
        password);

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
    QCOMPARE(fixture.secretSink.presentCount(), 1);
    QCOMPARE(
        fixture.secretSink.lastPurpose(),
        AccountRecoveryKeyPurpose::AccountCreated);
    QCOMPARE(
        fixture.secretSink.takeRecoveryKey(),
        recoveryKey);

    const auto stored = fixture.credentials.loadActive();
    QVERIFY(stored.has_value());
    QCOMPARE(
        stored->refreshToken,
        refreshToken.toLatin1());

    const QByteArray persisted =
        ordinaryStateBytes(fixture.temp.path());

    QVERIFY(!persisted.contains(
        password.toUtf8()));
    QVERIFY(!persisted.contains(
        recoveryKey.toUtf8()));
    QVERIFY(!persisted.contains(
        accessToken.toUtf8()));
    QVERIFY(!persisted.contains(
        refreshToken.toUtf8()));

    QVERIFY(
        persisted.contains(
            fixture.deviceIdentity
                ->installId()
                .toUtf8()));
}

void tst_account_identity::secureStoreFailureFailsClosed() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    fixture.credentials.setFailWrites(true);

    QJsonObject body;
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-fail-store"),
            QStringLiteral("refresh-fail-store")));
    body.insert(
        QStringLiteral("recovery_key"),
        QStringLiteral(
            "AAAAA-BBBBB-CCCCC-DDDDD-EEEEEE"));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/accounts"),
        okReply(201, body));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/revoke-refresh"),
        okReply(204));

    fixture.controller->createAccount(
        QStringLiteral("Hemanth56"),
        QStringLiteral(
            "A sufficiently long password 123"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("error"));
    QCOMPARE(
        fixture.controller->errorCategory(),
        QStringLiteral("storage"));
    QCOMPARE(fixture.secretSink.presentCount(), 1);

    QVERIFY(!fixture.credentials.loadActive().has_value());
    QVERIFY(fixture.client->accessToken().isEmpty());
    QCOMPARE(
        fixture.transport->pendingReplyCount(
            QByteArrayLiteral("POST"),
            QStringLiteral(
                "/v1/sessions/revoke-refresh")),
        0);
}

void tst_account_identity::staleSignInReplyCannotUndoLocalOnlyChoice() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    QJsonObject body;
    body.insert(
        QStringLiteral("status"),
        QStringLiteral("signed_in"));
    body.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-stale"),
            QStringLiteral("refresh-stale")));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(200, body));

    QSignalSpy clientCompletedSpy(
        fixture.client.get(),
        &AccountClient::completed);

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral(
            "A sufficiently long password 123"));

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("authenticating"));

    fixture.controller->continueWithoutAccount();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));

    QTRY_COMPARE(clientCompletedSpy.count(), 1);

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("localOnly"));
    QVERIFY(!fixture.credentials.loadActive().has_value());
    QVERIFY(fixture.client->accessToken().isEmpty());
}

void tst_account_identity::protectedSignInPersistsNothingBeforeApproval() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    QJsonObject challenge;
    challenge.insert(
        QStringLiteral("status"),
        QStringLiteral("approval_required"));
    challenge.insert(
        QStringLiteral("challenge_token"),
        QStringLiteral("challenge-sentinel"));
    challenge.insert(
        QStringLiteral("challenge_expires_at"),
        QDateTime::currentDateTimeUtc()
            .addSecs(600)
            .toString(Qt::ISODateWithMs));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        okReply(202, challenge));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral(
            "A sufficiently long password 123"));

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("authenticating"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("awaitingDeviceApproval"));
    QVERIFY(!fixture.credentials.loadActive().has_value());
    QVERIFY(fixture.client->accessToken().isEmpty());

    QJsonObject approved;
    approved.insert(
        QStringLiteral("status"),
        QStringLiteral("signed_in"));
    approved.insert(
        QStringLiteral("session"),
        sessionObject(
            QStringLiteral("Hemanth56"),
            QStringLiteral("access-approved"),
            QStringLiteral("refresh-approved")));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/challenges/device/poll"),
        okReply(200, approved));

    fixture.controller->pollPendingChallenge();

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
    QVERIFY(fixture.credentials.loadActive().has_value());
}

void tst_account_identity::trustedRecoveryUsesNativeOnlySecretSink() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    QJsonObject started;
    started.insert(
        QStringLiteral("status"),
        QStringLiteral("approval_required"));
    started.insert(
        QStringLiteral("challenge_token"),
        QStringLiteral("trusted-recovery-challenge"));
    started.insert(
        QStringLiteral("challenge_expires_at"),
        QDateTime::currentDateTimeUtc()
            .addSecs(600)
            .toString(Qt::ISODateWithMs));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/password/trusted-recovery"),
        okReply(202, started));

    fixture.controller->startTrustedRecovery(
        QStringLiteral("Hemanth56"),
        QStringLiteral(
            "A replacement password value 884"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("awaitingRecoveryApproval"));

    QJsonObject recovered;
    recovered.insert(
        QStringLiteral("status"),
        QStringLiteral("recovered"));
    recovered.insert(
        QStringLiteral("recovery_key"),
        QStringLiteral(
            "TTTTT-RRRRR-UUUUU-SSSSS-TTTTTT"));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral(
            "/v1/password/trusted-recovery/poll"),
        okReply(200, recovered));

    fixture.controller->pollPendingChallenge();

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QCOMPARE(fixture.secretSink.presentCount(), 1);
    QCOMPARE(
        fixture.secretSink.lastPurpose(),
        AccountRecoveryKeyPurpose::PasswordRecovered);
    QVERIFY(
        fixture.secretSink
            .takeRecoveryKey()
            .startsWith(QStringLiteral("TTTTT-")));

    const QByteArray persisted =
        ordinaryStateBytes(fixture.temp.path());
    QVERIFY(!persisted.contains("TTTTT-RRRRR"));
}

void tst_account_identity::offlineLogoutQueuesDurablePendingRevocation() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    const QByteArray refreshToken =
        QByteArrayLiteral("refresh-pending-revoke");
    restoreSignedIn(fixture, refreshToken);

    fixture.transport->setOnline(false);
    fixture.controller->logoutCurrent();

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QVERIFY(!fixture.credentials.loadActive().has_value());
    QCOMPARE(
        fixture.credentials.pendingRevocations(),
        QList<QByteArray>{refreshToken});
}

void tst_account_identity::logoutEverywhereSessionInvalidStillSignsOutExplicitly() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/logout-all"),
        errorReply(
            401,
            QStringLiteral("session_invalid"),
            QStringLiteral("The access session is already invalid.")));

    QSignalSpy lockedSpy(
        fixture.controller.get(),
        &AccountController::currentDeviceLocked);

    fixture.controller->logoutEverywhere();

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QCOMPARE(lockedSpy.count(), 0);
    QVERIFY(!fixture.credentials.loadActive().has_value());
    QVERIFY(fixture.client->accessToken().isEmpty());
}

void tst_account_identity::pendingRevocationFlushRemovesOnlyConfirmedToken() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    const QByteArray confirmed =
        QByteArrayLiteral("pending-confirmed");
    const QByteArray stillOffline =
        QByteArrayLiteral("pending-still-offline");

    QVERIFY(
        fixture.credentials.addPendingRevocation(
            confirmed));
    QVERIFY(
        fixture.credentials.addPendingRevocation(
            stillOffline));

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/revoke-refresh"),
        okReply(204));

    fixture.controller->flushPendingRevocations();

    QTRY_VERIFY(
        !fixture.credentials
            .pendingRevocations()
            .contains(confirmed));
    QVERIFY(
        fixture.credentials
            .pendingRevocations()
            .contains(stillOffline));
}

void tst_account_identity::failedCredentialClearTombstonePreventsResurrection() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("DELETE"),
        QStringLiteral("/v1/sessions/current"),
        okReply(204));

    fixture.credentials.setAvailable(false);
    fixture.controller->logoutCurrent();

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QVERIFY(fixture.bootstrapStore->credentialClearPending());

    fixture.controller.reset();
    fixture.client.reset();
    fixture.transport.reset();

    fixture.credentials.setAvailable(true);
    QVERIFY(fixture.credentials.loadActive().has_value());

    fixture.transport = AccountFixtureTransport::create();
    QVERIFY(fixture.transport);
    fixture.client = std::make_unique<AccountClient>(
        fixture.transport.get());
    fixture.controller =
        std::make_unique<AccountController>(
            fixture.client.get(),
            &fixture.credentials,
            fixture.deviceIdentity.get(),
            fixture.bootstrapStore.get(),
            &fixture.secretSink);
    fixture.controller->setAutomaticPollingEnabled(false);

    fixture.controller->restoreRememberedSession();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QVERIFY(!fixture.credentials.loadActive().has_value());
    QVERIFY(!fixture.bootstrapStore->credentialClearPending());
    QCOMPARE(
        fixture.transport->pendingReplyCount(
            QByteArrayLiteral("POST"),
            QStringLiteral("/v1/sessions/refresh")),
        0);
}

void tst_account_identity::currentDeviceRevokeTransitionsToLocked() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("DELETE"),
        QStringLiteral("/v1/devices/")
            + QString::fromLatin1(kDeviceId),
        okReply(204));

    fixture.controller->revokeDevice(
        QString::fromLatin1(kDeviceId));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("locked"));
    QVERIFY(!fixture.credentials.loadActive().has_value());
    QVERIFY(fixture.client->accessToken().isEmpty());
}

void tst_account_identity::syncBlockedAndDeletionPendingStatesAreSafe() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    QCOMPARE(
        fixture.controller->syncState(),
        QStringLiteral("idle"));

    fixture.controller->setSyncObservation(
        AccountController::SyncState::Retrying,
        4);
    QCOMPARE(
        fixture.controller->syncState(),
        QStringLiteral("retrying"));
    QCOMPARE(
        fixture.controller->pendingOutboxCount(),
        4);

    fixture.controller->setSyncObservation(
        AccountController::SyncState::Blocked,
        7);
    QCOMPARE(
        fixture.controller->syncState(),
        QStringLiteral("blocked"));
    QCOMPARE(
        fixture.controller->pendingOutboxCount(),
        7);
    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));

    fixture.controller->setDeletionPending(
        QDateTime::currentDateTimeUtc().addDays(7));

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("deletionPending"));
    QCOMPARE(
        fixture.controller->syncState(),
        QStringLiteral("inactive"));
    QCOMPARE(
        fixture.controller->pendingOutboxCount(),
        0);
    QVERIFY(
        !fixture.controller
            ->deletionEffectiveAt()
            .isEmpty());

    fixture.controller->clearDeletionPending();

    QCOMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedIn"));
    QCOMPARE(
        fixture.controller->syncState(),
        QStringLiteral("idle"));
}

void tst_account_identity::stableErrorCategoryMapsRateLimit() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        errorReply(
            429,
            QStringLiteral("rate_limited"),
            QStringLiteral(
                "Too many attempts. Try again later.")));

    fixture.controller->signIn(
        QStringLiteral("Hemanth56"),
        QStringLiteral(
            "A sufficiently long password 123"));

    QTRY_COMPARE(
        fixture.controller->mode(),
        QStringLiteral("signedOut"));
    QCOMPARE(
        fixture.controller->errorCategory(),
        QStringLiteral("rateLimited"));
    QCOMPARE(
        fixture.controller->lastErrorCode(),
        QStringLiteral("rate_limited"));
}

void tst_account_identity::deviceListUpdatesSafeCount() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    QJsonArray devices;
    devices.append(
        QJsonObject{
            {
                QStringLiteral("id"),
                QString::fromLatin1(kDeviceId)
            }
        });
    devices.append(
        QJsonObject{
            {
                QStringLiteral("id"),
                QStringLiteral(
                    "cccccccc-cccc-4ccc-8ccc-cccccccccccc")
            }
        });

    QJsonObject body;
    body.insert(
        QStringLiteral("devices"),
        devices);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"),
        QStringLiteral("/v1/devices"),
        okReply(200, body));

    fixture.controller->refreshDevices();

    QTRY_COMPARE(
        fixture.controller->deviceCount(),
        2);
}


void tst_account_identity::profileRefreshExposesPersistedAvatar() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    QJsonObject profile;
    profile.insert(QStringLiteral("username"), QStringLiteral("Hemanth56"));
    profile.insert(QStringLiteral("avatar_id"), QStringLiteral("laurel"));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"),
        QStringLiteral("/v1/profile"),
        okReply(200, profile));

    QSignalSpy avatarSpy(
        fixture.controller.get(),
        &AccountController::avatarIdChanged);
    fixture.controller->refreshProfile();

    QTRY_COMPARE(fixture.controller->avatarId(), QStringLiteral("laurel"));
    QCOMPARE(avatarSpy.count(), 1);
}

void tst_account_identity::avatarMutationFailurePreservesPersistedAvatar() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    QJsonObject profile;
    profile.insert(QStringLiteral("username"), QStringLiteral("Hemanth56"));
    profile.insert(QStringLiteral("avatar_id"), QStringLiteral("column"));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"),
        QStringLiteral("/v1/profile"),
        okReply(200, profile));
    fixture.controller->refreshProfile();
    QTRY_COMPARE(fixture.controller->avatarId(), QStringLiteral("column"));

    QSignalSpy failureSpy(
        fixture.controller.get(),
        &AccountController::builtinAvatarChangeFailed);
    fixture.transport->enqueueReply(
        QByteArrayLiteral("PUT"),
        QStringLiteral("/v1/profile/avatar/builtin"),
        errorReply(
            503,
            QStringLiteral("service_unavailable"),
            QStringLiteral("Profile service unavailable.")));
    fixture.controller->setBuiltinAvatar(QStringLiteral("panels"));

    QTRY_COMPARE(failureSpy.count(), 1);
    QCOMPARE(fixture.controller->avatarId(), QStringLiteral("column"));
    const QList<QVariant> args = failureSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("Profile service unavailable."));
    QCOMPARE(args.at(1).toString(), QStringLiteral("unavailable"));
    QCOMPARE(args.at(2).toString(), QStringLiteral("service_unavailable"));
}

void tst_account_identity::passwordChangeSuccessEmitsCompletionSignal() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);
    QSignalSpy successSpy(
        fixture.controller.get(),
        &AccountController::passwordChangeSucceeded);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/password/change"),
        okReply(200));
    fixture.controller->changePassword(
        QStringLiteral("current password sentinel"),
        QStringLiteral("new password sentinel"));

    QTRY_COMPARE(successSpy.count(), 1);
    QCOMPARE(fixture.controller->mode(), QStringLiteral("signedIn"));
}

void tst_account_identity::passwordChangeFailureDoesNotEmitCompletionSignal() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);
    QSignalSpy successSpy(
        fixture.controller.get(),
        &AccountController::passwordChangeSucceeded);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/password/change"),
        errorReply(
            401,
            QStringLiteral("invalid_password"),
            QStringLiteral("The current password is incorrect.")));
    fixture.controller->changePassword(
        QStringLiteral("wrong current password sentinel"),
        QStringLiteral("new password sentinel"));

    QTRY_COMPARE(fixture.controller->lastErrorCode(), QStringLiteral("invalid_password"));
    QCOMPARE(successSpy.count(), 0);
}

void tst_account_identity::manualRecoveryReplacementUsesOneTimeSecretSink() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    const QString sentinel = QStringLiteral("CLSM-MANUAL-REPLACEMENT-TEST-ONLY");
    QJsonObject body;
    body.insert(QStringLiteral("recovery_key"), sentinel);
    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/recovery-key/replace"),
        okReply(200, body));

    fixture.controller->replaceRecoveryKey(QStringLiteral("fixture current password"));
    QTRY_COMPARE(fixture.secretSink.presentCount(), 1);
    QCOMPARE(fixture.secretSink.lastPurpose(), AccountRecoveryKeyPurpose::ManualReplacement);
    QCOMPARE(fixture.secretSink.recoveryKey(), sentinel);
    QVERIFY(!ordinaryStateBytes(fixture.temp.path()).contains(sentinel.toUtf8()));
}

void tst_account_identity::failedManualRecoveryReplacementDoesNotPresentSecret() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    QSignalSpy failureSpy(
        fixture.controller.get(),
        &AccountController::recoveryKeyReplacementFailed);
    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/recovery-key/replace"),
        errorReply(
            401,
            QStringLiteral("invalid_password"),
            QStringLiteral("The current password is incorrect.")));
    fixture.controller->replaceRecoveryKey(QStringLiteral("wrong fixture password"));

    QTRY_COMPARE(failureSpy.count(), 1);
    QCOMPARE(fixture.secretSink.presentCount(), 0);
    const QList<QVariant> args = failureSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("The current password is incorrect."));
    QCOMPARE(args.at(2).toString(), QStringLiteral("invalid_password"));
}

void tst_account_identity::profileMutationSignalsAreOperationSpecific() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    QSignalSpy renameSuccess(fixture.controller.get(), &AccountController::usernameRenameSucceeded);
    QSignalSpy renameFailure(fixture.controller.get(), &AccountController::usernameRenameFailed);
    QSignalSpy avatarSuccess(fixture.controller.get(), &AccountController::builtinAvatarChangeSucceeded);
    QSignalSpy avatarFailure(fixture.controller.get(), &AccountController::builtinAvatarChangeFailed);

    QJsonObject renamed;
    renamed.insert(QStringLiteral("username"), QStringLiteral("Hemanth57"));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("PATCH"),
        QStringLiteral("/v1/profile/username"),
        okReply(200, renamed));
    fixture.controller->renameUsername(QStringLiteral("Hemanth57"));
    QTRY_COMPARE(renameSuccess.count(), 1);
    QCOMPARE(renameFailure.count(), 0);
    QCOMPARE(avatarSuccess.count(), 0);
    QCOMPARE(avatarFailure.count(), 0);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("PUT"),
        QStringLiteral("/v1/profile/avatar/builtin"),
        errorReply(503, QStringLiteral("service_unavailable"), QStringLiteral("Profile service unavailable.")));
    fixture.controller->setBuiltinAvatar(QStringLiteral("book"));
    QTRY_COMPARE(avatarFailure.count(), 1);
    QCOMPARE(renameFailure.count(), 0);
}

void tst_account_identity::recoveryReplacementSignalsAreOperationSpecific() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    QSignalSpy successSpy(fixture.controller.get(), &AccountController::recoveryKeyReplacementSucceeded);
    QSignalSpy failureSpy(fixture.controller.get(), &AccountController::recoveryKeyReplacementFailed);

    QJsonObject body;
    body.insert(QStringLiteral("recovery_key"), QStringLiteral("CLSM-OP-SIGNAL-TEST-ONLY"));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/recovery-key/replace"),
        okReply(200, body));
    fixture.controller->replaceRecoveryKey(QStringLiteral("fixture current password"));
    QTRY_COMPARE(successSpy.count(), 1);
    QCOMPARE(failureSpy.count(), 0);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/recovery-key/replace"),
        errorReply(401, QStringLiteral("invalid_password"), QStringLiteral("The current password is incorrect.")));
    fixture.controller->replaceRecoveryKey(QStringLiteral("wrong fixture password"));
    QTRY_COMPARE(failureSpy.count(), 1);
    QCOMPARE(successSpy.count(), 1);
}

void tst_account_identity::devicesExposeServerCurrentDeviceIdentityToQml() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);
    QCOMPARE(fixture.controller->deviceId(), QString::fromLatin1(kDeviceId));
    QVERIFY(fixture.controller->metaObject()->indexOfMethod("deviceId()") >= 0);
}

void tst_account_identity::malformedDeviceListIsProtocolFailureAndPreservesPriorList() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    QJsonObject current{{QStringLiteral("id"), QString::fromLatin1(kDeviceId)}};
    QJsonObject other{{QStringLiteral("id"), QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc")}};
    QJsonObject valid;
    valid.insert(QStringLiteral("devices"), QJsonArray{current, other});
    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"), okReply(200, valid));
    fixture.controller->refreshDevices();
    QTRY_COMPARE(fixture.controller->deviceCount(), 2);

    QSignalSpy successSpy(fixture.controller.get(), &AccountController::deviceListRefreshSucceeded);
    QSignalSpy failureSpy(fixture.controller.get(), &AccountController::deviceListRefreshFailed);
    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"), okReply(200, QJsonObject()));
    fixture.controller->refreshDevices();

    QTRY_COMPARE(failureSpy.count(), 1);
    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(fixture.controller->deviceCount(), 2);
    QCOMPARE(fixture.controller->devices().size(), 2);
    const QList<QVariant> args = failureSpy.takeFirst();
    QCOMPARE(args.at(1).toString(), QStringLiteral("protocol"));
    QCOMPARE(args.at(2).toString(), QStringLiteral("invalid_devices_payload"));
}

void tst_account_identity::revokeSuccessWaitsForAuthoritativeRefresh() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    Fixture fixture;
    restoreSignedIn(fixture);

    const QString otherId = QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc");
    QJsonObject current{{QStringLiteral("id"), QString::fromLatin1(kDeviceId)}};
    QJsonObject other{{QStringLiteral("id"), otherId}};
    QJsonObject initial;
    initial.insert(QStringLiteral("devices"), QJsonArray{current, other});
    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"), okReply(200, initial));
    fixture.controller->refreshDevices();
    QTRY_COMPARE(fixture.controller->deviceCount(), 2);

    QSignalSpy revokeSuccess(fixture.controller.get(), &AccountController::deviceRevokeSucceeded);
    QSignalSpy revokeFailure(fixture.controller.get(), &AccountController::deviceRevokeFailed);
    QSignalSpy refreshFailure(fixture.controller.get(), &AccountController::deviceListRefreshFailed);

    fixture.transport->enqueueReply(
        QByteArrayLiteral("DELETE"), QStringLiteral("/v1/devices/") + otherId, okReply(204));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"),
        errorReply(503, QStringLiteral("service_unavailable"), QStringLiteral("Device refresh unavailable.")));
    fixture.controller->revokeDevice(otherId);

    QTRY_COMPARE(refreshFailure.count(), 1);
    QCOMPARE(revokeSuccess.count(), 0);
    QCOMPARE(revokeFailure.count(), 0);
    QCOMPARE(fixture.controller->devices().size(), 2);

    QJsonObject currentOnly;
    currentOnly.insert(QStringLiteral("devices"), QJsonArray{current});
    fixture.transport->enqueueReply(
        QByteArrayLiteral("DELETE"), QStringLiteral("/v1/devices/") + otherId, okReply(204));
    fixture.transport->enqueueReply(
        QByteArrayLiteral("GET"), QStringLiteral("/v1/devices"), okReply(200, currentOnly));
    fixture.controller->revokeDevice(otherId);

    QTRY_COMPARE(revokeSuccess.count(), 1);
    QCOMPARE(revokeFailure.count(), 0);
    QCOMPARE(fixture.controller->devices().size(), 1);
}

QTEST_GUILESS_MAIN(tst_account_identity)

#include "tst_account_identity.moc"
