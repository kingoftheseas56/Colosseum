#include "trackers/SimklAuth.h"

#include "account/ProfilePaths.h"
#include "trackers/TrackerConnectionStore.h"

#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <memory>

namespace {

constexpr auto kProfileId = "11111111-1111-4111-8111-111111111111";

class FakeClock final : public TrackerClock
{
public:
    qint64 nowMs() const override { return now; }

    qint64 now = 1000;
};

class FakeRandom final : public TrackerRandomSource
{
public:
    QByteArray bytes(qsizetype count) override
    {
        QByteArray result;
        result.reserve(count);
        for (qsizetype index = 0; index < count; ++index)
            result.append(static_cast<char>((cursor++ % 251) + 1));
        return result;
    }

private:
    int cursor = 0;
};

class TruncatedRandom final : public TrackerRandomSource
{
public:
    QByteArray bytes(qsizetype) override { return QByteArrayLiteral("x"); }
};

class FakeBrowser final : public TrackerSystemBrowser
{
public:
    bool open(const QUrl &url) override
    {
        openedUrls.append(url);
        return canOpen;
    }

    bool canOpen = true;
    QList<QUrl> openedUrls;
};

class FakeVault final : public TrackerCredentialVault
{
public:
    bool isAvailable() const override { return available; }

    bool hasReusableCredential(const TrackerCredentialSlot &slot, qint64 nowMs) const override
    {
        const auto credential = loadForProfile(slot.profileId, slot.providerId);
        return credential.has_value()
            && credential->slot.remoteAccountId == slot.remoteAccountId
            && trackerCredentialIsReusable(*credential, nowMs);
    }

    bool saveAndVerify(const TrackerCredential &credential) override
    {
        ++saveCount;
        if (!available || rejectWrites)
            return false;
        stored = credential;
        if (destructiveVerificationFailure) {
            destructiveVerificationFailure = false;
            stored.reset();
            rejectWrites = rejectRecoveryAfterFailure;
            return false;
        }
        return true;
    }

    std::optional<TrackerCredential> loadForProfile(
        const QString &profileId,
        TrackerProviderId providerId) const override
    {
        ++loadCount;
        if (!available || !stored.has_value()
            || stored->slot.profileId != profileId
            || stored->slot.providerId != providerId) {
            return std::nullopt;
        }
        if (readbackMismatch) {
            TrackerCredential mismatch = *stored;
            mismatch.slot.remoteAccountId = QStringLiteral("other-account");
            return mismatch;
        }
        return stored;
    }

    bool clearForProfile(const QString &profileId, TrackerProviderId providerId) override
    {
        ++clearCount;
        if (stored.has_value() && stored->slot.profileId == profileId
            && stored->slot.providerId == providerId) {
            stored.reset();
        }
        return true;
    }

    bool available = true;
    bool rejectWrites = false;
    bool destructiveVerificationFailure = false;
    bool rejectRecoveryAfterFailure = false;
    bool readbackMismatch = false;
    mutable int loadCount = 0;
    int saveCount = 0;
    int clearCount = 0;
    std::optional<TrackerCredential> stored;
};

class FakeTransport final : public SimklAuthTransport
{
public:
    void exchangeAuthorizationCode(
        const SimklAuthorizationCodeRequest &request,
        SimklTokenCompletion completion) override
    {
        ++exchangeCalls;
        lastExchange = request;
        exchangeCompletion = std::move(completion);
    }

    void requestDevicePin(
        const SimklDevicePinRequest &request,
        SimklDevicePinCompletion completion) override
    {
        ++deviceStartCalls;
        lastDeviceStart = request;
        deviceStartCompletion = std::move(completion);
    }

    void pollDevicePin(
        const SimklDevicePinPollRequest &request,
        SimklTokenCompletion completion) override
    {
        ++devicePollCalls;
        lastDevicePoll = request;
        devicePollCompletion = std::move(completion);
    }

    void fetchStableAccountId(
        const SimklIdentityRequest &request,
        SimklIdentityCompletion completion) override
    {
        ++identityCalls;
        lastIdentity = request;
        identityCompletion = std::move(completion);
    }

    void completeExchange(const SimklTokenResponse &response)
    {
        QVERIFY(static_cast<bool>(exchangeCompletion));
        auto completion = std::move(exchangeCompletion);
        completion(response);
    }

    void completeDeviceStart(const SimklDevicePinResponse &response)
    {
        QVERIFY(static_cast<bool>(deviceStartCompletion));
        auto completion = std::move(deviceStartCompletion);
        completion(response);
    }

    void completeDevicePoll(const SimklTokenResponse &response)
    {
        QVERIFY(static_cast<bool>(devicePollCompletion));
        auto completion = std::move(devicePollCompletion);
        completion(response);
    }

    void completeIdentity(const SimklIdentityResponse &response)
    {
        QVERIFY(static_cast<bool>(identityCompletion));
        auto completion = std::move(identityCompletion);
        completion(response);
    }

    int exchangeCalls = 0;
    int deviceStartCalls = 0;
    int devicePollCalls = 0;
    int identityCalls = 0;
    SimklAuthorizationCodeRequest lastExchange;
    SimklDevicePinRequest lastDeviceStart;
    SimklDevicePinPollRequest lastDevicePoll;
    SimklIdentityRequest lastIdentity;
    SimklTokenCompletion exchangeCompletion;
    SimklDevicePinCompletion deviceStartCompletion;
    SimklTokenCompletion devicePollCompletion;
    SimklIdentityCompletion identityCompletion;
};

SimklAuthConfiguration testConfiguration()
{
    return {
        QStringLiteral("fixture-public-client"),
        QUrl(QStringLiteral("https://fixture.invalid/authorize")),
        QUrl(QStringLiteral("https://fixture.invalid/token")),
        QUrl(QStringLiteral("https://fixture.invalid/device")),
        QUrl(QStringLiteral("https://fixture.invalid/users/settings")),
        QUrl(QStringLiteral("http://127.0.0.1:49152/simkl/callback")),
        QStringLiteral("colosseum-test"),
        QStringLiteral("1.1.8-test"),
        {QStringLiteral("media:read"), QStringLiteral("media:write")}};
}

SimklTokenResponse usableToken()
{
    return {
        SimklTransportError::None,
        QByteArrayLiteral("access-token-fixture"),
        QByteArrayLiteral("refresh-token-fixture"),
        7 * 24 * 60 * 60 * 1000LL,
        180 * 24 * 60 * 60 * 1000LL,
        {QStringLiteral("media:read"), QStringLiteral("media:write")},
        0};
}

QString browserState(const FakeBrowser &browser)
{
    if (browser.openedUrls.isEmpty())
        return {};
    return QUrlQuery(browser.openedUrls.constLast()).queryItemValue(QStringLiteral("state"));
}

void completeBrowserAuth(SimklAuthSession *session,
                         FakeBrowser *browser,
                         FakeTransport *transport,
                         const SimklTokenResponse &token = usableToken(),
                         const SimklIdentityResponse &identity = {
                             SimklTransportError::None, QStringLiteral("12345"), 0})
{
    QVERIFY(session->acceptBrowserCallback(browserState(*browser), QStringLiteral("code-fixture")));
    transport->completeExchange(token);
    if (token.error == SimklTransportError::None)
        transport->completeIdentity(identity);
}

} // namespace

class TrackerAuthTest : public QObject
{
    Q_OBJECT

private slots:
    void browserPkceValidatesCallbackAndSecuresReadback();
    void forgedAndReplayedCallbacksCannotReachTheTokenEndpoint();
    void cancellationAndProfileReplacementFenceLateReplies();
    void authFailureMatrixLeavesNoCredential_data();
    void authFailureMatrixLeavesNoCredential();
    void missingIdentityOrVaultFailureLeavesNoCredential_data();
    void missingIdentityOrVaultFailureLeavesNoCredential();
    void timeoutFencesTheOutstandingAuthorization();
    void devicePinFlowRetainsOnlyTheUserCodeInSanitizedState();
    void devicePinRejectsAnUnsafeApprovalUri();
    void truncatedRandomCannotWeakenBrowserPkce();
    void destroyedSessionFencesEveryOutstandingReply();
    void durableConnectionRecordIsRequiredBeforeConnected();
    void changedIdentityPreservesCurrentCredentialUntilExplicitDisconnect();
    void changedIdentityRequiresDisconnectEvenWhenCredentialIsMissing();
    void failedSameAccountReauthorizationPreservesPreviousCredential();
    void destructiveReadbackFailureRestoresPreviousCredential_data();
    void destructiveReadbackFailureRestoresPreviousCredential();
    void expiredOrRevokedCredentialCannotBeReused();
    void staleOrDestroyedIdentityProviderCannotClearANewerCredential();
    void identityProviderRequiresAReusableBoundCredential();
    void sanitizedProjectionNeverContainsSecrets();
    void productionSimklRemainsDisabledWithoutRegistrationReceipt();
};

void TrackerAuthTest::browserPkceValidatesCallbackAndSecuresReadback()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 7}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);

    QVERIFY(session.beginBrowserAuthorization());
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::AwaitingBrowserCallback);
    const QUrl url = browser.openedUrls.constLast();
    const QUrlQuery query(url);
    QCOMPARE(url.scheme(), QStringLiteral("https"));
    QCOMPARE(query.queryItemValue(QStringLiteral("client_id")),
             QStringLiteral("fixture-public-client"));
    QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
    QVERIFY(!query.queryItemValue(QStringLiteral("code_challenge")).isEmpty());
    QVERIFY(!query.queryItemValue(QStringLiteral("state")).isEmpty());
    QVERIFY(!url.toString().contains(QStringLiteral("verifier"), Qt::CaseInsensitive));

    completeBrowserAuth(&session, &browser, &transport);
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::CredentialReady);
    QCOMPARE(session.snapshot().remoteAccountId, QStringLiteral("12345"));
    QCOMPARE(vault.saveCount, 1);
    QVERIFY(vault.stored.has_value());
    QCOMPARE(vault.stored->slot.profileId, QString::fromLatin1(kProfileId));
    QCOMPARE(vault.stored->slot.providerId, TrackerProviderId::Simkl);
    QCOMPARE(vault.stored->slot.remoteAccountId, QStringLiteral("12345"));
    QVERIFY(session.snapshot().sanitizedFieldNames().contains(QStringLiteral("remoteAccountId")));
}

void TrackerAuthTest::forgedAndReplayedCallbacksCannotReachTheTokenEndpoint()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 1}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);

    QVERIFY(session.beginBrowserAuthorization());
    QVERIFY(!session.acceptBrowserCallback(QStringLiteral("forged"), QStringLiteral("code")));
    QCOMPARE(transport.exchangeCalls, 0);
    QVERIFY(session.acceptBrowserCallback(browserState(browser), QStringLiteral("code")));
    QCOMPARE(transport.exchangeCalls, 1);
    QVERIFY(!session.acceptBrowserCallback(browserState(browser), QStringLiteral("code")));
    QCOMPARE(transport.exchangeCalls, 1);
}

void TrackerAuthTest::cancellationAndProfileReplacementFenceLateReplies()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 2}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);

    QVERIFY(session.beginBrowserAuthorization());
    QVERIFY(session.acceptBrowserCallback(browserState(browser), QStringLiteral("code")));
    session.cancel();
    transport.completeExchange(usableToken());
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::Cancelled);
    QCOMPARE(transport.identityCalls, 0);
    QVERIFY(!vault.stored.has_value());

    QVERIFY(session.beginBrowserAuthorization());
    QVERIFY(session.acceptBrowserCallback(browserState(browser), QStringLiteral("code-2")));
    session.invalidateProfile({QStringLiteral("22222222-2222-4222-8222-222222222222"), 3});
    transport.completeExchange(usableToken());
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::Cancelled);
    QCOMPARE(transport.identityCalls, 0);
    QVERIFY(!vault.stored.has_value());
}

void TrackerAuthTest::authFailureMatrixLeavesNoCredential_data()
{
    QTest::addColumn<SimklTransportError>("error");
    QTest::newRow("denied") << SimklTransportError::AccessDenied;
    QTest::newRow("expired") << SimklTransportError::Expired;
    QTest::newRow("revoked") << SimklTransportError::Revoked;
    QTest::newRow("rate-limited") << SimklTransportError::RateLimited;
    QTest::newRow("oversized") << SimklTransportError::PayloadTooLarge;
    QTest::newRow("network") << SimklTransportError::NetworkFailure;
    QTest::newRow("malformed-token") << SimklTransportError::ProtocolFailure;
}

void TrackerAuthTest::authFailureMatrixLeavesNoCredential()
{
    QFETCH(SimklTransportError, error);
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 4}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);
    QVERIFY(session.beginBrowserAuthorization());

    SimklTokenResponse failure = usableToken();
    failure.error = error;
    completeBrowserAuth(&session, &browser, &transport, failure);
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
    QCOMPARE(session.snapshot().error, simklAuthErrorForTransport(error));
    QVERIFY(!vault.stored.has_value());
}

void TrackerAuthTest::missingIdentityOrVaultFailureLeavesNoCredential_data()
{
    QTest::addColumn<QString>("failure");
    QTest::newRow("missing-stable-id") << QStringLiteral("missing-stable-id");
    QTest::newRow("vault-write") << QStringLiteral("vault-write");
    QTest::newRow("vault-readback") << QStringLiteral("vault-readback");
    QTest::newRow("scope") << QStringLiteral("scope");
}

void TrackerAuthTest::missingIdentityOrVaultFailureLeavesNoCredential()
{
    QFETCH(QString, failure);
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    if (failure == QLatin1String("vault-write"))
        vault.rejectWrites = true;
    if (failure == QLatin1String("vault-readback"))
        vault.readbackMismatch = true;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 8}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);
    QVERIFY(session.beginBrowserAuthorization());
    QVERIFY(session.acceptBrowserCallback(browserState(browser), QStringLiteral("code")));
    SimklTokenResponse token = usableToken();
    if (failure == QLatin1String("scope"))
        token.grantedScopes = {QStringLiteral("media:read")};
    transport.completeExchange(token);
    if (failure == QLatin1String("scope")) {
        QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
        QCOMPARE(session.snapshot().error, SimklAuthError::MissingPermission);
        QCOMPARE(transport.identityCalls, 0);
    } else {
        transport.completeIdentity({
            SimklTransportError::None,
            failure == QLatin1String("missing-stable-id") ? QString() : QStringLiteral("12345"),
            0});
        QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
        QCOMPARE(session.snapshot().error,
                 failure == QLatin1String("missing-stable-id")
                     ? SimklAuthError::MissingStableAccount
                     : SimklAuthError::CredentialStore);
    }
    QVERIFY(!vault.stored.has_value());
    if (failure == QLatin1String("vault-readback"))
        QCOMPARE(vault.clearCount, 1);
}

void TrackerAuthTest::timeoutFencesTheOutstandingAuthorization()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 9}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);
    QVERIFY(session.beginBrowserAuthorization());
    clock.now += 5 * 60 * 1000;
    session.expireIfDue();
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::TimedOut);
    QCOMPARE(session.snapshot().error, SimklAuthError::Expired);
    QVERIFY(!session.acceptBrowserCallback(browserState(browser), QStringLiteral("late-code")));
    QCOMPARE(transport.exchangeCalls, 0);
}

void TrackerAuthTest::devicePinFlowRetainsOnlyTheUserCodeInSanitizedState()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 5}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);

    QVERIFY(session.beginDevicePinAuthorization());
    QCOMPARE(transport.deviceStartCalls, 1);
    QCOMPARE(transport.lastDeviceStart.configuration.clientId,
             QStringLiteral("fixture-public-client"));
    transport.completeDeviceStart({
        SimklTransportError::None,
        QByteArrayLiteral("native-only-device-code"),
        QStringLiteral("ABCD-1234"),
        QUrl(QStringLiteral("https://simkl.com/pin")),
        QUrl(QStringLiteral("https://simkl.com/pin?user_code=ABCD-1234")),
        30 * 1000,
        5000,
        0});
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::AwaitingDeviceApproval);
    QCOMPARE(session.snapshot().userCode, QStringLiteral("ABCD-1234"));
    QCOMPARE(session.snapshot().verificationUri,
             QUrl(QStringLiteral("https://simkl.com/pin")));
    QVERIFY(!session.snapshot().sanitizedFieldNames().contains(QStringLiteral("deviceCode")));
    QVERIFY(!session.pollDevicePin());
    clock.now += 5000;
    QVERIFY(session.pollDevicePin());
    transport.completeDevicePoll({
        SimklTransportError::AuthorizationPending, {}, {}, 0, 0, {}, 0});
    QVERIFY(!session.pollDevicePin());
    clock.now += 5000;
    QVERIFY(session.pollDevicePin());
    transport.completeDevicePoll(usableToken());
    transport.completeIdentity({SimklTransportError::None, QStringLiteral("54321"), 0});
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::CredentialReady);
}

void TrackerAuthTest::truncatedRandomCannotWeakenBrowserPkce()
{
    FakeClock clock;
    TruncatedRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 10}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);

    QVERIFY(!session.beginBrowserAuthorization());
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
    QCOMPARE(session.snapshot().error, SimklAuthError::Configuration);
    QVERIFY(browser.openedUrls.isEmpty());
}

void TrackerAuthTest::devicePinRejectsAnUnsafeApprovalUri()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 16}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);

    QVERIFY(session.beginDevicePinAuthorization());
    transport.completeDeviceStart({
        SimklTransportError::None, QByteArrayLiteral("device"), QStringLiteral("ABCD-1234"),
        QUrl(QStringLiteral("https://evil.invalid/pin")),
        QUrl(QStringLiteral("https://evil.invalid/pin?user_code=ABCD-1234")),
        30 * 1000, 5000, 0});
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
    QCOMPARE(session.snapshot().error, SimklAuthError::TokenRejected);
    QVERIFY(!session.pollDevicePin());
}

void TrackerAuthTest::destroyedSessionFencesEveryOutstandingReply()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;

    {
        auto session = std::make_unique<SimklAuthSession>(
            SimklAuthBinding{QString::fromLatin1(kProfileId), 11}, testConfiguration(),
            &vault, &browser, &transport, &random, &clock);
        QVERIFY(session->beginBrowserAuthorization());
        QVERIFY(session->acceptBrowserCallback(browserState(browser), QStringLiteral("code")));
        session.reset();
        transport.completeExchange(usableToken());
        QCOMPARE(transport.identityCalls, 0);
    }

    {
        auto session = std::make_unique<SimklAuthSession>(
            SimklAuthBinding{QString::fromLatin1(kProfileId), 12}, testConfiguration(),
            &vault, &browser, &transport, &random, &clock);
        QVERIFY(session->beginDevicePinAuthorization());
        session.reset();
        transport.completeDeviceStart({
            SimklTransportError::None, QByteArrayLiteral("device"), QStringLiteral("ABCD-1234"),
            QUrl(QStringLiteral("https://simkl.com/pin")),
            QUrl(QStringLiteral("https://simkl.com/pin?user_code=ABCD-1234")),
            30 * 1000, 5000, 0});
    }

    {
        auto session = std::make_unique<SimklAuthSession>(
            SimklAuthBinding{QString::fromLatin1(kProfileId), 13}, testConfiguration(),
            &vault, &browser, &transport, &random, &clock);
        QVERIFY(session->beginBrowserAuthorization());
        QVERIFY(session->acceptBrowserCallback(browserState(browser), QStringLiteral("code")));
        transport.completeExchange(usableToken());
        QCOMPARE(transport.identityCalls, 1);
        session.reset();
        transport.completeIdentity({SimklTransportError::None, QStringLiteral("12345"), 0});
    }

    {
        auto session = std::make_unique<SimklAuthSession>(
            SimklAuthBinding{QString::fromLatin1(kProfileId), 14}, testConfiguration(),
            &vault, &browser, &transport, &random, &clock);
        QVERIFY(session->beginDevicePinAuthorization());
        transport.completeDeviceStart({
            SimklTransportError::None, QByteArrayLiteral("device"), QStringLiteral("ABCD-1234"),
            QUrl(QStringLiteral("https://simkl.com/pin")),
            QUrl(QStringLiteral("https://simkl.com/pin?user_code=ABCD-1234")),
            30 * 1000, 5000, 0});
        clock.now += 5000;
        QVERIFY(session->pollDevicePin());
        session.reset();
        transport.completeDevicePoll(usableToken());
        QCOMPARE(transport.identityCalls, 1);
    }

    QVERIFY(!vault.stored.has_value());
}

void TrackerAuthTest::durableConnectionRecordIsRequiredBeforeConnected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 15}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);

    QVERIFY(session.beginBrowserAuthorization());
    completeBrowserAuth(&session, &browser, &transport);
    QCOMPARE(session.snapshot().phase, SimklAuthPhase::CredentialReady);
    QVERIFY(!TrackerConnectionStore(*profile).connection(TrackerProviderId::Simkl).has_value());

    SimklIdentityProvider provider(testConfiguration(), &vault, &transport);
    TrackerConnectionRuntime runtime(TrackerProviderRegistry({&provider}), &vault, &clock);
    QVERIFY(runtime.activateProfile(*profile));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    transport.completeIdentity({SimklTransportError::None, QStringLiteral("12345"), 0});

    const auto connection = TrackerConnectionStore(*profile).connection(TrackerProviderId::Simkl);
    QVERIFY(connection.has_value());
    QCOMPARE(connection->remoteAccountId, QStringLiteral("12345"));
}

void TrackerAuthTest::changedIdentityPreservesCurrentCredentialUntilExplicitDisconnect()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    const TrackerCredential oldCredential{
        {QString::fromLatin1(kProfileId), TrackerProviderId::Simkl, QStringLiteral("12345")},
        QByteArrayLiteral("old-access"), QByteArrayLiteral("old-refresh"),
        5000, 10000, {QStringLiteral("media:read"), QStringLiteral("media:write")}};
    QVERIFY(vault.saveAndVerify(oldCredential));

    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 16}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);
    QVERIFY(session.beginBrowserAuthorization());
    completeBrowserAuth(
        &session, &browser, &transport, usableToken(),
        {SimklTransportError::None, QStringLiteral("54321"), 0});

    QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
    QCOMPARE(session.snapshot().error, SimklAuthError::AccountChangeRequired);
    QVERIFY(vault.stored.has_value());
    QCOMPARE(vault.stored->slot.remoteAccountId, QStringLiteral("12345"));
    QCOMPARE(vault.stored->accessToken, QByteArrayLiteral("old-access"));
    QCOMPARE(vault.saveCount, 1);
}

void TrackerAuthTest::failedSameAccountReauthorizationPreservesPreviousCredential()
{
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    const TrackerCredential oldCredential{
        {QString::fromLatin1(kProfileId), TrackerProviderId::Simkl, QStringLiteral("12345")},
        QByteArrayLiteral("old-access"), QByteArrayLiteral("old-refresh"),
        5000, 10000, {QStringLiteral("media:read"), QStringLiteral("media:write")}};
    QVERIFY(vault.saveAndVerify(oldCredential));
    vault.rejectWrites = true;

    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 17}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);
    QVERIFY(session.beginBrowserAuthorization());
    completeBrowserAuth(
        &session, &browser, &transport, usableToken(),
        {SimklTransportError::None, QStringLiteral("12345"), 0});

    QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
    QCOMPARE(session.snapshot().error, SimklAuthError::CredentialStore);
    QVERIFY(vault.stored.has_value());
    QCOMPARE(vault.stored->accessToken, QByteArrayLiteral("old-access"));
    QCOMPARE(vault.stored->refreshToken, QByteArrayLiteral("old-refresh"));
}

void TrackerAuthTest::destructiveReadbackFailureRestoresPreviousCredential_data()
{
    QTest::addColumn<bool>("rejectRecovery");
    QTest::newRow("restored") << false;
    QTest::newRow("recovery-failed") << true;
}

void TrackerAuthTest::destructiveReadbackFailureRestoresPreviousCredential()
{
    QFETCH(bool, rejectRecovery);
    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    const TrackerCredential oldCredential{
        {QString::fromLatin1(kProfileId), TrackerProviderId::Simkl, QStringLiteral("12345")},
        QByteArrayLiteral("old-access"), QByteArrayLiteral("old-refresh"),
        5000, 10000, {QStringLiteral("media:read"), QStringLiteral("media:write")}};
    QVERIFY(vault.saveAndVerify(oldCredential));
    vault.destructiveVerificationFailure = true;
    vault.rejectRecoveryAfterFailure = rejectRecovery;

    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 19}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock);
    QVERIFY(session.beginBrowserAuthorization());
    completeBrowserAuth(&session, &browser, &transport);

    QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
    QCOMPARE(session.snapshot().error, rejectRecovery
                 ? SimklAuthError::CredentialRecoveryFailed
                 : SimklAuthError::CredentialStore);
    QCOMPARE(vault.saveCount, 3);
    if (rejectRecovery) {
        QVERIFY(!vault.stored.has_value());
    } else {
        QVERIFY(vault.stored.has_value());
        QCOMPARE(vault.stored->accessToken, oldCredential.accessToken);
        QCOMPARE(vault.stored->refreshToken, oldCredential.refreshToken);
    }
}

void TrackerAuthTest::changedIdentityRequiresDisconnectEvenWhenCredentialIsMissing()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));

    FakeClock clock;
    FakeRandom random;
    FakeBrowser browser;
    FakeVault vault;
    FakeTransport transport;
    SimklAuthSession session(
        {QString::fromLatin1(kProfileId), 18}, testConfiguration(),
        &vault, &browser, &transport, &random, &clock, &connections);
    QVERIFY(session.beginBrowserAuthorization());
    completeBrowserAuth(
        &session, &browser, &transport, usableToken(),
        {SimklTransportError::None, QStringLiteral("54321"), 0});

    QCOMPARE(session.snapshot().phase, SimklAuthPhase::NeedsAttention);
    QCOMPARE(session.snapshot().error, SimklAuthError::AccountChangeRequired);
    QVERIFY(!vault.stored.has_value());
    QCOMPARE(vault.saveCount, 0);
    QCOMPARE(connections.connection(TrackerProviderId::Simkl)->remoteAccountId,
             QStringLiteral("12345"));
}

void TrackerAuthTest::expiredOrRevokedCredentialCannotBeReused()
{
    FakeClock clock;
    FakeVault vault;
    FakeTransport transport;
    TrackerCredential credential{
        {QString::fromLatin1(kProfileId), TrackerProviderId::Simkl, QStringLiteral("12345")},
        QByteArrayLiteral("access"), QByteArrayLiteral("refresh"),
        1500, 10000, {QStringLiteral("media:read")}};
    QVERIFY(vault.saveAndVerify(credential));
    QVERIFY(vault.hasReusableCredential(credential.slot, clock.now));
    clock.now = 1500;
    QVERIFY(!vault.hasReusableCredential(credential.slot, clock.now));

    credential.accessTokenExpiresAtMs = 5000;
    QVERIFY(vault.saveAndVerify(credential));
    SimklIdentityProvider provider(testConfiguration(), &vault, &transport);
    QString identity = QStringLiteral("unchanged");
    provider.requestStableAccountId(
        {QString::fromLatin1(kProfileId), 1, TrackerProviderId::Simkl, 1},
        [&identity](const TrackerStableAccountResult &result) { identity = result.remoteAccountId; });
    transport.completeIdentity({SimklTransportError::Revoked, {}, 0});
    QVERIFY(identity.isEmpty());
    QVERIFY(!vault.stored.has_value());
    QCOMPARE(vault.clearCount, 1);
}

void TrackerAuthTest::staleOrDestroyedIdentityProviderCannotClearANewerCredential()
{
    FakeVault vault;
    FakeTransport transport;
    const TrackerCredential oldCredential{
        {QString::fromLatin1(kProfileId), TrackerProviderId::Simkl, QStringLiteral("12345")},
        QByteArrayLiteral("old-access"), QByteArrayLiteral("old-refresh"),
        5000, 10000, {QStringLiteral("media:read")}};
    const TrackerCredential newerCredential{
        {QString::fromLatin1(kProfileId), TrackerProviderId::Simkl, QStringLiteral("12345")},
        QByteArrayLiteral("new-access"), QByteArrayLiteral("new-refresh"),
        6000, 11000, {QStringLiteral("media:read")}};
    QVERIFY(vault.saveAndVerify(oldCredential));

    SimklIdentityProvider provider(testConfiguration(), &vault, &transport);
    provider.requestStableAccountId(
        {QString::fromLatin1(kProfileId), 1, TrackerProviderId::Simkl, 1},
        [](const TrackerStableAccountResult &) {});
    QVERIFY(vault.saveAndVerify(newerCredential));
    transport.completeIdentity({SimklTransportError::Revoked, {}, 0});
    QVERIFY(vault.stored.has_value());
    QCOMPARE(vault.stored->accessToken, QByteArrayLiteral("new-access"));
    QCOMPARE(vault.clearCount, 0);

    QString completion = QStringLiteral("not-called");
    auto destroyedProvider = std::make_unique<SimklIdentityProvider>(
        testConfiguration(), &vault, &transport);
    destroyedProvider->requestStableAccountId(
        {QString::fromLatin1(kProfileId), 1, TrackerProviderId::Simkl, 2},
        [&completion](const TrackerStableAccountResult &result) {
            completion = result.remoteAccountId;
        });
    destroyedProvider.reset();
    transport.completeIdentity({SimklTransportError::Revoked, {}, 0});
    QCOMPARE(completion, QStringLiteral("not-called"));
    QVERIFY(vault.stored.has_value());
    QCOMPARE(vault.stored->accessToken, QByteArrayLiteral("new-access"));
}

void TrackerAuthTest::identityProviderRequiresAReusableBoundCredential()
{
    FakeVault vault;
    FakeTransport transport;
    const TrackerCredential credential{
        {QString::fromLatin1(kProfileId), TrackerProviderId::Simkl, QStringLiteral("12345")},
        QByteArrayLiteral("access"), QByteArrayLiteral("refresh"),
        5000, 10000, {QStringLiteral("media:read")}};
    QVERIFY(vault.saveAndVerify(credential));
    SimklIdentityProvider provider(testConfiguration(), &vault, &transport);
    TrackerConnectionAttempt attempt{
        QString::fromLatin1(kProfileId), 1, TrackerProviderId::Simkl, 1};
    QString identity;
    provider.requestStableAccountId(attempt, [&identity](const TrackerStableAccountResult &result) {
        identity = result.remoteAccountId;
    });
    QCOMPARE(transport.identityCalls, 1);
    transport.completeIdentity({SimklTransportError::None, QStringLiteral("12345"), 0});
    QCOMPARE(identity, QStringLiteral("12345"));

    provider.requestStableAccountId(
        {QString::fromLatin1(kProfileId), 1, TrackerProviderId::Simkl, 2},
        [&identity](const TrackerStableAccountResult &result) { identity = result.remoteAccountId; });
    transport.completeIdentity({SimklTransportError::None, QStringLiteral("54321"), 0});
    QVERIFY(identity.isEmpty());
}

void TrackerAuthTest::sanitizedProjectionNeverContainsSecrets()
{
    const QStringList names = SimklAuthSnapshot::sanitizedFieldNames();
    for (const QString &forbidden : {
             QStringLiteral("accessToken"), QStringLiteral("refreshToken"),
             QStringLiteral("authorizationCode"), QStringLiteral("codeVerifier"),
             QStringLiteral("state"), QStringLiteral("cookie"),
             QStringLiteral("callbackUrl"), QStringLiteral("rawResponse")}) {
        QVERIFY2(!names.contains(forbidden), qPrintable(forbidden));
    }
}

void TrackerAuthTest::productionSimklRemainsDisabledWithoutRegistrationReceipt()
{
    QVERIFY(!simklProductionConfiguration().has_value());
    const TrackerProviderRegistry registry = TrackerProviderRegistry::production();
    QVERIFY(registry.provider(TrackerProviderId::Simkl) == nullptr);
}

QTEST_APPLESS_MAIN(TrackerAuthTest)

#include "tst_tracker_auth.moc"
