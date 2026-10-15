#include "account/ProfilePaths.h"
#include "trackers/TrackerConnectionStore.h"
#include "trackers/TrackerRuntime.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <thread>
#include <atomic>

namespace {

constexpr auto kAccountProfileId = "11111111-1111-4111-8111-111111111111";

class FakeClock final : public TrackerClock
{
public:
    qint64 nowMs() const override { return value; }

    qint64 value = 1000;
};

class FakeCredentialVault final : public TrackerCredentialVault
{
public:
    bool hasReusableCredential(const TrackerCredentialSlot &slot, qint64 nowMs) const override
    {
        Q_UNUSED(nowMs);
        requestedSlots.append(slot);
        return acceptsCredential;
    }

    bool acceptsCredential = true;
    mutable QList<TrackerCredentialSlot> requestedSlots;
};

class FakeIdentityProvider final : public TrackerIdentityProvider
{
public:
    TrackerProviderDescriptor descriptor() const override
    {
        return {
            TrackerProviderId::Simkl,
            QStringLiteral("SIMKL"),
            TrackerProviderCapability::ReadHistory,
            true};
    }

    void requestStableAccountId(
        const TrackerConnectionAttempt &attempt,
        StableAccountCompletion completion) override
    {
        ++requestCount;
        pending.append({attempt, std::move(completion)});
    }

    void completeNext(const QString &remoteAccountId)
    {
        QVERIFY(!pending.isEmpty());
        const Pending item = pending.takeFirst();
        item.completion({remoteAccountId});
    }

    StableAccountCompletion takeNextCompletion()
    {
        if (pending.isEmpty())
            return {};
        return pending.takeFirst().completion;
    }

    int requestCount = 0;

private:
    struct Pending {
        TrackerConnectionAttempt attempt;
        StableAccountCompletion completion;
    };
    QList<Pending> pending;
};

TrackerConnectionRuntime makeRuntime(FakeIdentityProvider *provider,
                                     FakeCredentialVault *vault,
                                     FakeClock *clock)
{
    return TrackerConnectionRuntime(
        TrackerProviderRegistry({provider}), vault, clock);
}

bool writeJson(const QString &path, const QJsonObject &root)
{
    QFile file(path);
    const QFileInfo info(path);
    if (!QDir().mkpath(info.dir().absolutePath())
        || !file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) >= 0;
}

} // namespace

class TrackerConnectionFoundationTest : public QObject
{
    Q_OBJECT

private slots:
    void zeroTrackerStateCreatesNoFileOrTransportTraffic();
    void sealedProfileCannotCreateTrackerState();
    void legacyLocalDefersPrivateTrackerAdoptionToTheLifecycleSlice();
    void builtInCatalogueRemainsFailClosed();
    void connectionReopensWithoutTouchingCanonicalHistory();
    void rejectsMalformedAndForeignProfileStores();
    void keepsConnectionsProfilePrivate();
    void keepsOneConnectionPerProfileAndProvider();
    void disconnectedBindingPreservesIncarnationAndReleasesClaim();
    void concurrentExternalClaimsHaveOneWinner();
    void transferPendingBindingReservesRemoteAccount();
    void blocksAnExternalAccountClaimedByAnotherProfile();
    void rejectsAStaleConnectionGeneration();
    void rejectsAStaleProfileCallback();
    void destroyedRuntimeFencesCallback();
    void crossThreadCallbackCannotMutateTheRuntime();
    void crossThreadCallbackAfterRuntimeDestructionIsFenced();
    void refusesConnectionWhenCredentialReuseCannotBeConfirmed();
    void productionRegistryContainsNoTestTransport();
};

void TrackerConnectionFoundationTest::zeroTrackerStateCreatesNoFileOrTransportTraffic()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(runtime.activateProfile(profile));
    QVERIFY(!QFileInfo::exists(TrackerConnectionStore::storagePath(profile)));
    QCOMPARE(provider.requestCount, 0);
    QVERIFY(vault.requestedSlots.isEmpty());
}

void TrackerConnectionFoundationTest::sealedProfileCannotCreateTrackerState()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::sealed(root.path());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(!runtime.activateProfile(profile));
    QVERIFY(TrackerConnectionStore::storagePath(profile).isEmpty());
    QCOMPARE(provider.requestCount, 0);
    QVERIFY(vault.requestedSlots.isEmpty());
}

void TrackerConnectionFoundationTest::legacyLocalDefersPrivateTrackerAdoptionToTheLifecycleSlice()
{
    const ProfilePaths profile = ProfilePaths::legacyLocal();
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(!runtime.activateProfile(profile));
    QVERIFY(TrackerConnectionStore::storagePath(profile).isEmpty());
    QCOMPARE(provider.requestCount, 0);
    QVERIFY(vault.requestedSlots.isEmpty());
}

void TrackerConnectionFoundationTest::builtInCatalogueRemainsFailClosed()
{
    const QList<TrackerProviderDescriptor> catalogue = trackerBuiltInProviderCatalog();
    QCOMPARE(catalogue.size(), 4);
    QCOMPARE(catalogue.at(0).providerId, TrackerProviderId::Simkl);
    QCOMPARE(catalogue.at(1).providerId, TrackerProviderId::Mal);
    QCOMPARE(catalogue.at(2).providerId, TrackerProviderId::Trakt);
    QCOMPARE(catalogue.at(3).providerId, TrackerProviderId::AniList);
    for (const TrackerProviderDescriptor &descriptor : catalogue) {
        QVERIFY(!descriptor.available);
        QVERIFY(descriptor.capabilities.testFlag(TrackerProviderCapability::None));
    }
}

void TrackerConnectionFoundationTest::connectionReopensWithoutTouchingCanonicalHistory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    const QString historyPath = profile.historyIniPath();
    QVERIFY(writeJson(historyPath, {{QStringLiteral("native"), true}}));
    QFile originalHistory(historyPath);
    QVERIFY(originalHistory.open(QIODevice::ReadOnly));
    const QByteArray before = originalHistory.readAll();

    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    clock.value = 4242;
    auto runtime = makeRuntime(&provider, &vault, &clock);
    QVERIFY(runtime.activateProfile(profile));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("simkl-42"));

    TrackerConnectionStore reopened(profile);
    QVERIFY(reopened.healthy());
    const auto connection = reopened.connection(TrackerProviderId::Simkl);
    QVERIFY(connection.has_value());
    QCOMPARE(connection->remoteAccountId, QStringLiteral("simkl-42"));
    QCOMPARE(connection->connectionGeneration, quint64(1));
    QCOMPARE(connection->verifiedAtMs, qint64(4242));

    QFile unchangedHistory(historyPath);
    QVERIFY(unchangedHistory.open(QIODevice::ReadOnly));
    QCOMPARE(unchangedHistory.readAll(), before);
}

void TrackerConnectionFoundationTest::rejectsMalformedAndForeignProfileStores()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    const QString path = TrackerConnectionStore::storagePath(profile);

    QVERIFY(writeJson(path, {{QStringLiteral("version"), 99}}));
    TrackerConnectionStore malformed(profile);
    QVERIFY(!malformed.healthy());

    QVERIFY(writeJson(path, {
        {QStringLiteral("version"), 1},
        {QStringLiteral("profileId"), QStringLiteral("someone-else")},
        {QStringLiteral("connections"), QJsonArray()}}));
    TrackerConnectionStore foreign(profile);
    QVERIFY(!foreign.healthy());

    QVERIFY(writeJson(path, {
        {QStringLiteral("version"), 1},
        {QStringLiteral("profileId"), profile.profileId()},
        {QStringLiteral("connections"), QJsonArray({QJsonObject{
            {QStringLiteral("providerId"), QStringLiteral("simkl")},
            {QStringLiteral("remoteAccountId"), QStringLiteral("remote")},
            {QStringLiteral("connectionGeneration"), QStringLiteral("1")},
            {QStringLiteral("verifiedAtMs"), QStringLiteral("1")},
            {QStringLiteral("capabilities"), 1073741824},
            {QStringLiteral("state"), QStringLiteral("connected")}}})}}));
    TrackerConnectionStore unsupportedCapability(profile);
    QVERIFY(!unsupportedCapability.healthy());
}

void TrackerConnectionFoundationTest::keepsConnectionsProfilePrivate()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths local = ProfilePaths::localOnly(root.path());
    const auto account = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(account.has_value());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(runtime.activateProfile(local));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("simkl-local"));
    QVERIFY(runtime.activateProfile(*account));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("simkl-account"));

    TrackerConnectionStore localStore(local);
    TrackerConnectionStore accountStore(*account);
    QCOMPARE(localStore.connection(TrackerProviderId::Simkl)->remoteAccountId,
             QStringLiteral("simkl-local"));
    QCOMPARE(accountStore.connection(TrackerProviderId::Simkl)->remoteAccountId,
             QStringLiteral("simkl-account"));
}

void TrackerConnectionFoundationTest::keepsOneConnectionPerProfileAndProvider()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(runtime.activateProfile(profile));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("simkl-first"));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("simkl-second"));

    TrackerConnectionStore store(profile);
    QCOMPARE(store.connections().size(), 1);
    const auto connection = store.connection(TrackerProviderId::Simkl);
    QVERIFY(connection.has_value());
    QCOMPARE(connection->remoteAccountId, QStringLiteral("simkl-first"));
    QCOMPARE(connection->connectionGeneration, quint64(1));
    QCOMPARE(runtime.lastError(), QStringLiteral(
        "Changing tracker accounts requires disconnecting the current account first."));

    QVERIFY(store.setDisconnected(TrackerProviderId::Simkl,
                                 QStringLiteral("simkl-first"), 1));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("simkl-second"));
    TrackerConnectionStore changed(profile);
    const auto changedConnection = changed.connection(TrackerProviderId::Simkl);
    QVERIFY(changedConnection.has_value());
    QCOMPARE(changedConnection->remoteAccountId, QStringLiteral("simkl-second"));
    QCOMPARE(changedConnection->connectionGeneration, quint64(3));
    QCOMPARE(changedConnection->state, TrackerConnectionState::Connected);
}

void TrackerConnectionFoundationTest::disconnectedBindingPreservesIncarnationAndReleasesClaim()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths local = ProfilePaths::localOnly(root.path());
    const auto account = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(account.has_value());

    TrackerConnectionStore localStore(local);
    QVERIFY(localStore.upsert({TrackerProviderId::Simkl, QStringLiteral("simkl-shared"),
                               7, 10, {}, TrackerConnectionState::Connected}));
    QVERIFY(localStore.setDisconnected(TrackerProviderId::Simkl,
                                       QStringLiteral("simkl-shared"), 7));
    QCOMPARE(localStore.nextConnectionGeneration(TrackerProviderId::Simkl), quint64(8));
    QCOMPARE(localStore.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);

    TrackerConnectionStore accountStore(*account);
    QCOMPARE(accountStore.externalAccountClaim(TrackerProviderId::Simkl,
                                               QStringLiteral("simkl-shared")),
             TrackerConnectionStore::ExternalAccountClaim::Unclaimed);
    QVERIFY(accountStore.upsert({TrackerProviderId::Simkl, QStringLiteral("simkl-shared"),
                                 1, 11, {}, TrackerConnectionState::Connected}));
    TrackerConnectionStore reopenedLocal(local);
    QVERIFY(reopenedLocal.healthy());
    QCOMPARE(reopenedLocal.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);
}

void TrackerConnectionFoundationTest::concurrentExternalClaimsHaveOneWinner()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths local = ProfilePaths::localOnly(root.path());
    const auto account = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(account.has_value());
    TrackerConnectionStore localStore(local);
    TrackerConnectionStore accountStore(*account);

    std::atomic<int> successes{0};
    std::thread localClaim([&] {
        if (localStore.upsert({TrackerProviderId::Simkl, QStringLiteral("same-account"),
                               1, 1, {}, TrackerConnectionState::Connected})) {
            ++successes;
        }
    });
    std::thread accountClaim([&] {
        if (accountStore.upsert({TrackerProviderId::Simkl, QStringLiteral("same-account"),
                                 1, 1, {}, TrackerConnectionState::Connected})) {
            ++successes;
        }
    });
    localClaim.join();
    accountClaim.join();

    QCOMPARE(successes.load(), 1);
    TrackerConnectionStore reopenedLocal(local);
    TrackerConnectionStore reopenedAccount(*account);
    const auto localConnection = reopenedLocal.connection(TrackerProviderId::Simkl);
    const auto accountConnection = reopenedAccount.connection(TrackerProviderId::Simkl);
    const int activeClaims =
        (localConnection.has_value()
         && localConnection->state == TrackerConnectionState::Connected ? 1 : 0)
        + (accountConnection.has_value()
           && accountConnection->state == TrackerConnectionState::Connected ? 1 : 0);
    QCOMPARE(activeClaims, 1);
}

void TrackerConnectionFoundationTest::transferPendingBindingReservesRemoteAccount()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto pendingProfile = ProfilePaths::account(
        QStringLiteral("22222222-2222-4222-8222-222222222222"), root.path());
    const auto competingProfile = ProfilePaths::account(
        QStringLiteral("33333333-3333-4333-8333-333333333333"), root.path());
    QVERIFY(pendingProfile.has_value());
    QVERIFY(competingProfile.has_value());

    const QJsonObject pendingConnection{
        {QStringLiteral("providerId"), QStringLiteral("simkl")},
        {QStringLiteral("remoteAccountId"), QStringLiteral("same-account")},
        {QStringLiteral("connectionGeneration"), QStringLiteral("2")},
        {QStringLiteral("verifiedAtMs"), QStringLiteral("2")},
        {QStringLiteral("capabilities"), 0},
        {QStringLiteral("state"), QStringLiteral("transfer_pending")}};
    QVERIFY(writeJson(TrackerConnectionStore::storagePath(*pendingProfile), {
        {QStringLiteral("version"), 2},
        {QStringLiteral("profileId"), pendingProfile->profileId()},
        {QStringLiteral("connections"), QJsonArray{pendingConnection}}}));

    TrackerConnectionStore competing(*competingProfile);
    QCOMPARE(competing.externalAccountClaim(TrackerProviderId::Simkl,
                                            QStringLiteral("same-account")),
             TrackerConnectionStore::ExternalAccountClaim::Claimed);
    QVERIFY(!competing.upsert({TrackerProviderId::Simkl, QStringLiteral("same-account"),
                               1, 3, {}, TrackerConnectionState::Connected}));
    QVERIFY(!competing.connection(TrackerProviderId::Simkl).has_value());
}

void TrackerConnectionFoundationTest::blocksAnExternalAccountClaimedByAnotherProfile()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths local = ProfilePaths::localOnly(root.path());
    const auto account = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(account.has_value());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(runtime.activateProfile(local));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("same-simkl-account"));
    QVERIFY(runtime.activateProfile(*account));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("same-simkl-account"));

    TrackerConnectionStore accountStore(*account);
    QVERIFY(!accountStore.connection(TrackerProviderId::Simkl).has_value());
    QCOMPARE(runtime.lastError(),
             QStringLiteral("This SIMKL account is already connected to another profile."));
}

void TrackerConnectionFoundationTest::rejectsAStaleConnectionGeneration()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(runtime.activateProfile(profile));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("stale"));
    QVERIFY(!QFileInfo::exists(TrackerConnectionStore::storagePath(profile)));
    provider.completeNext(QStringLiteral("current"));

    TrackerConnectionStore store(profile);
    const auto connection = store.connection(TrackerProviderId::Simkl);
    QVERIFY(connection.has_value());
    QCOMPARE(connection->remoteAccountId, QStringLiteral("current"));
    QCOMPARE(connection->connectionGeneration, quint64(2));
}

void TrackerConnectionFoundationTest::rejectsAStaleProfileCallback()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths local = ProfilePaths::localOnly(root.path());
    const auto account = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(account.has_value());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(runtime.activateProfile(local));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    QVERIFY(runtime.activateProfile(*account));
    provider.completeNext(QStringLiteral("late-local"));

    QVERIFY(!QFileInfo::exists(TrackerConnectionStore::storagePath(local)));
    QVERIFY(!QFileInfo::exists(TrackerConnectionStore::storagePath(*account)));
}

void TrackerConnectionFoundationTest::destroyedRuntimeFencesCallback()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    {
        auto runtime = std::make_unique<TrackerConnectionRuntime>(
            TrackerProviderRegistry({&provider}), &vault, &clock);
        QVERIFY(runtime->activateProfile(profile));
        QVERIFY(runtime->beginConnection(TrackerProviderId::Simkl));
    }

    provider.completeNext(QStringLiteral("late-after-destruction"));
    QVERIFY(!QFileInfo::exists(TrackerConnectionStore::storagePath(profile)));
    QVERIFY(vault.requestedSlots.isEmpty());
}

void TrackerConnectionFoundationTest::crossThreadCallbackCannotMutateTheRuntime()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(runtime.activateProfile(profile));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    StableAccountCompletion completion = provider.takeNextCompletion();
    QVERIFY(static_cast<bool>(completion));
    std::thread delivery([completion = std::move(completion)]() mutable {
        completion({QStringLiteral("cross-thread")});
    });
    delivery.join();

    QVERIFY(!QFileInfo::exists(TrackerConnectionStore::storagePath(profile)));
    QVERIFY(vault.requestedSlots.isEmpty());
}

void TrackerConnectionFoundationTest::crossThreadCallbackAfterRuntimeDestructionIsFenced()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    FakeClock clock;
    StableAccountCompletion completion;
    {
        auto runtime = std::make_unique<TrackerConnectionRuntime>(
            TrackerProviderRegistry({&provider}), &vault, &clock);
        QVERIFY(runtime->activateProfile(profile));
        QVERIFY(runtime->beginConnection(TrackerProviderId::Simkl));
        completion = provider.takeNextCompletion();
        QVERIFY(static_cast<bool>(completion));
    }

    std::thread delivery([completion = std::move(completion)]() mutable {
        completion({QStringLiteral("late-cross-thread")});
    });
    delivery.join();

    QVERIFY(!QFileInfo::exists(TrackerConnectionStore::storagePath(profile)));
    QVERIFY(vault.requestedSlots.isEmpty());
}

void TrackerConnectionFoundationTest::refusesConnectionWhenCredentialReuseCannotBeConfirmed()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeIdentityProvider provider;
    FakeCredentialVault vault;
    vault.acceptsCredential = false;
    FakeClock clock;
    auto runtime = makeRuntime(&provider, &vault, &clock);

    QVERIFY(runtime.activateProfile(profile));
    QVERIFY(runtime.beginConnection(TrackerProviderId::Simkl));
    provider.completeNext(QStringLiteral("simkl-without-vault"));

    QVERIFY(!QFileInfo::exists(TrackerConnectionStore::storagePath(profile)));
    QCOMPARE(runtime.lastError(),
             QStringLiteral("SIMKL credentials could not be verified for this profile."));
}

void TrackerConnectionFoundationTest::productionRegistryContainsNoTestTransport()
{
    const TrackerProviderRegistry production = TrackerProviderRegistry::production();
    QVERIFY(production.provider(TrackerProviderId::Simkl) == nullptr);
    QVERIFY(production.provider(TrackerProviderId::Mal) == nullptr);
    QVERIFY(production.provider(TrackerProviderId::Trakt) == nullptr);
    QVERIFY(production.provider(TrackerProviderId::AniList) == nullptr);
}

QTEST_APPLESS_MAIN(TrackerConnectionFoundationTest)

#include "tst_tracker_connection_foundation.moc"
