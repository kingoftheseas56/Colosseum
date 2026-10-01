#include <QtTest>
#include <QDateTime>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QUuid>

#include "ProgressStore.h"
#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include "account/ProfilePaths.h"
#include "trackers/TraktApiClient.h"
#include "trackers/TraktCodec.h"
#include "trackers/TraktConnectionController.h"
#include "trackers/TraktSyncRuntime.h"
#include "trackers/TrackerCanonicalDeliverySource.h"
#include "trackers/TrackerHistoryEvidenceStore.h"
#include "trackers/TrackerLifecycleCoordinator.h"
#include "trackers/TrackerProgressImportOwner.h"
#include "trackers/TrackerSyncSettingsStore.h"

#include <limits>

namespace {
const QString kAccount = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
const QString kProfile = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString kOtherProfile = QStringLiteral("22222222-2222-4222-8222-222222222222");

TraktAuthConfiguration configuration()
{
    return {QStringLiteral("fixture-public-client"),
            QUrl(QStringLiteral("https://fixture.invalid/trakt")),
            QStringLiteral("ColosseumTest"), QStringLiteral("test")};
}

class FakeClock final : public TrackerClock {
public:
    qint64 nowMs() const override { return now; }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
};

class FakeVault final : public TrackerCredentialVault {
public:
    bool isAvailable() const override { return true; }
    bool hasReusableCredential(const TrackerCredentialSlot &slot, qint64 now) const override {
        const auto value = loadForProfile(slot.profileId, slot.providerId);
        return value && value->slot.remoteAccountId == slot.remoteAccountId
            && trackerCredentialIsReusable(*value, now);
    }
    bool saveAndVerify(const TrackerCredential &value) override {
        ++saves;
        if (rejectWrites) return false;
        values.insert(value.slot.profileId + trackerProviderKey(value.slot.providerId), value);
        return true;
    }
    std::optional<TrackerCredential> loadForProfile(const QString &profile, TrackerProviderId provider) const override {
        const auto found = values.constFind(profile + trackerProviderKey(provider));
        return found == values.cend() ? std::nullopt : std::optional<TrackerCredential>(*found);
    }
    bool clearForProfile(const QString &profile, TrackerProviderId provider) override {
        values.remove(profile + trackerProviderKey(provider));
        return true;
    }
    QHash<QString, TrackerCredential> values;
    bool rejectWrites = false;
    int saves = 0;
};

TrackerCredential credential(const QString &profile, qint64 now)
{
    return {{profile, TrackerProviderId::Trakt, kAccount}, QByteArrayLiteral("old-access"),
            QByteArrayLiteral("old-refresh"), now + 3600000,
            std::numeric_limits<qint64>::max(), {}};
}

class FakeAuthTransport final : public TraktAuthTransport {
public:
    void requestDeviceCode(const TraktAuthConfiguration &, DeviceCompletion done) override {
        if (holdDevice) deviceCompletion = std::move(done); else done(device);
    }
    void pollDeviceToken(const TraktAuthConfiguration &, const QByteArray &, TokenCompletion done) override {
        ++polls;
        done(token);
    }
    void fetchStableAccountId(const TraktAuthConfiguration &, const QByteArray &, IdentityCompletion done) override {
        ++identities;
        done(identity);
    }
    TraktDeviceCodeResponse device{TraktTransportError::None, QByteArrayLiteral("device-fixture"),
        QStringLiteral("ABCD1234"), QUrl(QStringLiteral("https://trakt.tv/activate")), 60000, 1000};
    TraktTokenResponse token{TraktTransportError::None, QByteArrayLiteral("access"),
        QByteArrayLiteral("refresh"), 3600000, 0};
    TraktIdentityResponse identity{TraktTransportError::None, kAccount};
    DeviceCompletion deviceCompletion;
    bool holdDevice = false;
    int polls = 0;
    int identities = 0;
};

class HeldScrobbleTransport final : public SimklScrobbleTransport {
public:
    void send(const TrackerScrobbleIntent &intent, SendCompletion done) override {
        requests.append(intent); completions.append(std::move(done));
    }
    void readback(const TrackerScrobbleIntent &, ReadbackCompletion done) override {
        done(SimklScrobbleReadbackResult::Indeterminate);
    }
    QList<TrackerScrobbleIntent> requests;
    QList<SendCompletion> completions;
};

class FakeApiTransport final : public TraktApiTransport {
public:
    struct Request { QByteArray method; QNetworkRequest request; QJsonDocument body; };
    void request(const QByteArray &method, const QNetworkRequest &request,
                 const QJsonDocument &body, Completion done) override {
        requests.append({method, request, body});
        const QString path = request.url().path();
        if (path.endsWith(QLatin1String("/refresh"))) {
            ++refreshes;
            refreshCompletion = std::move(done);
            return;
        }
        if (holdNext) { holdNext = false; heldCompletion = std::move(done); return; }
        TraktApiResult result;
        result.statusCode = 200;
        if (method == QByteArrayLiteral("GET")) {
            if (path.startsWith(QLatin1String("/sync/history"))) result.document = QJsonDocument(history);
            else if (path == QLatin1String("/sync/playback")) result.document = QJsonDocument(playback);
            else result.document = QJsonDocument(QJsonObject{{QStringLiteral("all"), activities}});
        } else {
            QJsonObject response = body.object();
            const QString action = path.section(QLatin1Char('/'), -1);
            if (path.startsWith(QLatin1String("/scrobble/")))
                response.insert(QStringLiteral("action"), action == QLatin1String("stop") ? QStringLiteral("scrobble") : action);
            if (path == QLatin1String("/sync/history"))
                response = QJsonObject{{QStringLiteral("added"), QJsonObject{{QStringLiteral("movies"), 1}, {QStringLiteral("episodes"), 0}}},
                    {QStringLiteral("not_found"), QJsonObject{{QStringLiteral("movies"), QJsonArray{}}, {QStringLiteral("episodes"), QJsonArray{}}}}};
            result.document = QJsonDocument(response);
            if (loseAcknowledgement) { result.networkFailure = true; result.statusCode = 0; }
        }
        done(result);
    }
    void completeRefresh() {
        TraktApiResult result;
        result.statusCode = 200;
        result.document = QJsonDocument(QJsonObject{{QStringLiteral("access_token"), QStringLiteral("new-access")},
            {QStringLiteral("refresh_token"), QStringLiteral("new-refresh")},
            {QStringLiteral("expires_in"), 3600}});
        auto done = std::move(refreshCompletion);
        done(result);
    }
    QList<Request> posts() const {
        QList<Request> result;
        for (const Request &request : requests)
            if (request.method == QByteArrayLiteral("POST")) result.append(request);
        return result;
    }
    QList<Request> requests;
    QJsonArray history;
    QJsonArray playback;
    QString activities = QStringLiteral("fixture-v1");
    Completion refreshCompletion;
    Completion heldCompletion;
    int refreshes = 0;
    bool holdNext = false;
    bool loseAcknowledgement = false;
};

QJsonObject movie(int id = 42)
{
    return {{QStringLiteral("title"), QStringLiteral("Fixture movie")},
            {QStringLiteral("ids"), QJsonObject{{QStringLiteral("trakt"), id},
                                               {QStringLiteral("imdb"), QStringLiteral("tt0000042")}}}};
}

QJsonObject historyRow(qint64 at, int id = 42, int eventId = 7)
{
    return {{QStringLiteral("id"), eventId}, {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("watched_at"), QDateTime::fromMSecsSinceEpoch(at, Qt::UTC).toString(Qt::ISODateWithMs)},
            {QStringLiteral("movie"), movie(id)}};
}

QJsonObject playbackRow(double percentage, qint64 at, int id = 42)
{
    return {{QStringLiteral("id"), 8}, {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("progress"), percentage},
            {QStringLiteral("paused_at"), QDateTime::fromMSecsSinceEpoch(at, Qt::UTC).toString(Qt::ISODateWithMs)},
            {QStringLiteral("movie"), movie(id)}};
}

struct Fixture {
    QTemporaryDir root;
    ProfilePaths profile = *ProfilePaths::account(kProfile, root.path());
    FakeClock clock;
    FakeVault vault;
    FakeApiTransport transport;
    TrackerConnectionStore connections{profile};
    TrackerMappingStore mappings{profile};
    TrackerImportStore imports{profile, &mappings, &connections};
    ProgressStore progress{profile.progressIniPath()};
    ActivityStore activity{profile.activityDbPath()};
    HistoryStore history{profile.historyIniPath()};
    TrackerProgressImportOwner importOwner{&progress};
    TrackerHistoryEvidenceStore evidence{profile, &mappings};
    TrackerDeliveryStore delivery{profile, &mappings, &connections};
    TrackerCanonicalDeliverySource source{&progress, &activity, &history};
    TrackerSyncSettingsStore settings{profile};
    TraktApiClient api{profile, configuration(), &vault, &transport, &clock};
    TraktSyncRuntime sync{&connections, &mappings, &imports, &importOwner, &evidence,
                          &delivery, &source, &settings, nullptr, &api};
    Fixture() {
        vault.saveAndVerify(credential(profile.profileId(), clock.now));
        connections.upsert({TrackerProviderId::Trakt, kAccount, 1, clock.now,
                            traktCapabilities(), TrackerConnectionState::Connected});
        mappings.upsert({TrackerProviderId::Trakt, kAccount, QStringLiteral("movie:42")},
                        {QStringLiteral("video:tt0000042"), QStringLiteral("video"), QStringLiteral("tt0000042"),
                         QStringLiteral("Fixture movie")}, TrackerMappingProvenance::UserConfirmed);
        settings.setGlobalSetting(QStringLiteral("checkOnLaunch"), false);
    }
    TrackerConnection connection() const { return *connections.connection(TrackerProviderId::Trakt); }
    TrackerDeliveryFact nativeProgress() {
        progress.record({{QStringLiteral("kind"), QStringLiteral("video")},
                         {QStringLiteral("id"), QStringLiteral("tt0000042")},
                         {QStringLiteral("title"), QStringLiteral("Fixture movie")},
                         {QStringLiteral("progress"), 0.41}, {QStringLiteral("resume"), 41.0}});
        progress.flush();
        QCoreApplication::processEvents();
        return *source.currentProgressFact(QStringLiteral("video"), QStringLiteral("tt0000042"));
    }
    TrackerDeliveryFact nativeCompletion() {
        const qint64 at = clock.now - 1000;
        activity.recordCompletion({{QStringLiteral("eventId"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("sessionId"), activity.newSessionId()}, {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("kind"), QStringLiteral("movie")}, {QStringLiteral("titleKey"), QStringLiteral("title:tt0000042")},
            {QStringLiteral("itemKey"), QStringLiteral("tt0000042")}, {QStringLiteral("title"), QStringLiteral("Fixture movie")},
            {QStringLiteral("itemLabel"), QString()}, {QStringLiteral("cover"), QString()},
            {QStringLiteral("utcOffsetMinutes"), 0}, {QStringLiteral("syncable"), true},
            {QStringLiteral("source"), QStringLiteral("player")}, {QStringLiteral("atMs"), at},
            {QStringLiteral("reason"), QStringLiteral("eof")}});
        const auto facts = source.currentCommittedFacts();
        for (const auto &fact : facts) if (fact.kind == TrackerDeliveryFactKind::Completion) return fact;
        return {};
    }
    std::optional<TrackerRemoteDeliverySnapshot> snapshot(const QList<TrackerDeliveryFact> &facts) {
        std::optional<TrackerRemoteDeliverySnapshot> result;
        sync.readExportSnapshotAsync(connection(), facts,
            [&result](auto value) { result = std::move(value); });
        return result;
    }
    bool consent(const TrackerDeliveryFact &fact) {
        const auto remote = snapshot({fact});
        if (!remote) return false;
        const auto preview = delivery.createExportPreview(TrackerProviderId::Trakt, kAccount, 1, {fact}, *remote, &source);
        if (!preview || preview->items.size() != 1 || !preview->items.first().eligible) return false;
        return delivery.confirmExport(preview->previewId, {preview->items.first().itemId}, *remote, &source, clock.now)
            && delivery.setProviderSendEnabled(TrackerProviderId::Trakt, kAccount, true);
    }
};
} // namespace

class TraktDirectTest final : public QObject {
    Q_OBJECT
private slots:
    void deviceCodeSuccess();
    void pendingPoll();
    void slowDownPoll();
    void expiry();
    void denial();
    void malformedAuth();
    void oneFlightRefresh();
    void replacementPersistenceFailure();
    void stableUuidRequired();
    void profileOwnershipAndMove();
    void historyImport();
    void playbackImport();
    void completionExport();
    void progressExport();
    void startPauseStopScrobble();
    void unknownReconciliation_data();
    void unknownReconciliation();
    void duplicateSuppression();
    void disconnectPendingPolicy();
    void revokeAfterLocalDisconnect();
    void unconfiguredUnavailable();
    void staleSnapshotAndPull();
    void skippedHistoryHasNoEvidence();
    void numericIdentityDoesNotAutoMatch();
    void sameAccountProvidersRemainIsolated();
    void delayedRefreshAfterDisconnectIsFenced();
    void remoteStopDoesNotCreateNativeCompletion();
    void videoImportAliases_data();
    void videoImportAliases();
    void scrobbleAliasRejectsWrongRemoteKind();
    void reconnectAllowsAnotherPull();
    void mismatchedVaultAccountIsRejected();
    void partialDisconnectStillFencesAndRevokes();
};

void TraktDirectTest::deviceCodeSuccess()
{
    FakeClock clock; FakeVault vault; FakeAuthTransport transport;
    TraktAuthSession session({kProfile, 1}, configuration(), &vault, &transport, &clock);
    QVERIFY(session.begin());
    QCOMPARE(session.snapshot().phase, TraktAuthPhase::AwaitingApproval);
    QVERIFY(!session.poll());
    clock.now = session.snapshot().nextPollAtMs;
    QVERIFY(session.poll());
    QCOMPARE(session.snapshot().phase, TraktAuthPhase::CredentialReady);
    const auto saved = vault.loadForProfile(kProfile, TrackerProviderId::Trakt);
    QVERIFY(saved);
    QCOMPARE(saved->slot.remoteAccountId, kAccount);
    QCOMPARE(saved->refreshToken, QByteArrayLiteral("refresh"));
}

void TraktDirectTest::pendingPoll()
{
    FakeClock clock; FakeVault vault; FakeAuthTransport transport;
    transport.token.error = TraktTransportError::AuthorizationPending;
    TraktAuthSession session({kProfile, 1}, configuration(), &vault, &transport, &clock);
    QVERIFY(session.begin()); clock.now = session.snapshot().nextPollAtMs; QVERIFY(session.poll());
    QCOMPARE(session.snapshot().phase, TraktAuthPhase::AwaitingApproval);
    QVERIFY(!session.poll());
    clock.now = session.snapshot().nextPollAtMs;
    QVERIFY(session.poll()); QCOMPARE(transport.polls, 2); QCOMPARE(vault.saves, 0);
}

void TraktDirectTest::slowDownPoll()
{
    FakeClock clock; FakeVault vault; FakeAuthTransport transport;
    transport.token.error = TraktTransportError::SlowDown;
    TraktAuthSession session({kProfile, 1}, configuration(), &vault, &transport, &clock);
    QVERIFY(session.begin()); clock.now = session.snapshot().nextPollAtMs; QVERIFY(session.poll());
    QCOMPARE(session.snapshot().nextPollAtMs, clock.now + 6000);
    clock.now += 5999; QVERIFY(!session.poll());
    ++clock.now; QVERIFY(session.poll()); QCOMPARE(transport.polls, 2);
}

void TraktDirectTest::expiry()
{
    FakeClock clock; FakeVault vault; FakeAuthTransport transport;
    TraktAuthSession session({kProfile, 1}, configuration(), &vault, &transport, &clock);
    QVERIFY(session.begin()); clock.now = session.snapshot().expiresAtMs;
    QVERIFY(!session.poll()); QCOMPARE(session.snapshot().phase, TraktAuthPhase::TimedOut);
    QCOMPARE(transport.polls, 0); QCOMPARE(vault.saves, 0);
}

void TraktDirectTest::denial()
{
    FakeClock clock; FakeVault vault; FakeAuthTransport transport;
    transport.token.error = TraktTransportError::AccessDenied;
    TraktAuthSession session({kProfile, 1}, configuration(), &vault, &transport, &clock);
    QVERIFY(session.begin()); clock.now = session.snapshot().nextPollAtMs; QVERIFY(session.poll());
    QCOMPARE(session.snapshot().error, TraktAuthError::AccessDenied); QCOMPARE(vault.saves, 0);
}

void TraktDirectTest::malformedAuth()
{
    FakeClock clock; FakeVault vault; FakeAuthTransport transport;
    transport.device.deviceCode.clear();
    TraktAuthSession session({kProfile, 1}, configuration(), &vault, &transport, &clock);
    QVERIFY(session.begin()); QCOMPARE(session.snapshot().phase, TraktAuthPhase::NeedsAttention);
    QVERIFY(!session.poll()); QCOMPARE(vault.saves, 0);
    transport.device.deviceCode = QByteArrayLiteral("device-fixture");
    transport.token.refreshToken.clear();
    QVERIFY(session.begin()); clock.now = session.snapshot().nextPollAtMs; QVERIFY(session.poll());
    QCOMPARE(session.snapshot().phase, TraktAuthPhase::NeedsAttention); QCOMPARE(vault.saves, 0);
}

void TraktDirectTest::oneFlightRefresh()
{
    Fixture f;
    auto expired = credential(kProfile, f.clock.now); expired.accessTokenExpiresAtMs = f.clock.now - 1;
    QVERIFY(f.vault.saveAndVerify(expired));
    int completed = 0;
    f.api.get(QStringLiteral("/sync/history"), {}, [&](const auto &r) { if (r.succeeded()) ++completed; });
    f.api.get(QStringLiteral("/sync/playback"), {}, [&](const auto &r) { if (r.succeeded()) ++completed; });
    QCOMPARE(f.transport.refreshes, 1); QCOMPARE(completed, 0);
    f.transport.completeRefresh(); QCOMPARE(completed, 2);
    QCOMPARE(f.vault.loadForProfile(kProfile, TrackerProviderId::Trakt)->refreshToken, QByteArrayLiteral("new-refresh"));
    for (const auto &request : f.transport.requests)
        if (request.method == QByteArrayLiteral("GET"))
            QCOMPARE(request.request.rawHeader(QByteArrayLiteral("Authorization")), QByteArrayLiteral("Bearer new-access"));
}

void TraktDirectTest::replacementPersistenceFailure()
{
    Fixture f;
    auto expired = credential(kProfile, f.clock.now); expired.accessTokenExpiresAtMs = f.clock.now - 1;
    QVERIFY(f.vault.saveAndVerify(expired)); f.vault.rejectWrites = true;
    int successes = 0; int completed = 0;
    f.api.get(QStringLiteral("/sync/history"), {}, [&](const auto &r) { ++completed; if (r.succeeded()) ++successes; });
    f.api.get(QStringLiteral("/sync/playback"), {}, [&](const auto &r) { ++completed; if (r.succeeded()) ++successes; });
    f.transport.completeRefresh(); QCOMPARE(successes, 0); QCOMPARE(completed, 2);
    QCOMPARE(f.transport.requests.size(), 1);
    f.api.get(QStringLiteral("/sync/history"), {}, [&](const auto &r) { if (r.succeeded()) ++successes; });
    QCOMPARE(f.transport.refreshes, 1); QCOMPARE(successes, 0);
    f.vault.rejectWrites = false;
    QVERIFY(f.vault.saveAndVerify(credential(kProfile, f.clock.now)));
    f.api.resetForConnection();
    f.api.get(QStringLiteral("/sync/history"), {}, [&](const auto &r) { if (r.succeeded()) ++successes; });
    QCOMPARE(successes, 1);
}

void TraktDirectTest::stableUuidRequired()
{
    FakeClock clock; FakeVault vault; FakeAuthTransport transport;
    transport.identity.remoteAccountId = QStringLiteral("username-is-not-uuid");
    TraktAuthSession session({kProfile, 1}, configuration(), &vault, &transport, &clock);
    QVERIFY(session.begin()); clock.now = session.snapshot().nextPollAtMs; QVERIFY(session.poll());
    QCOMPARE(session.snapshot().error, TraktAuthError::MissingStableAccount); QCOMPARE(vault.saves, 0);
    QVERIFY(traktSettingsUuid(QJsonDocument(QJsonObject{{QStringLiteral("user"), QJsonObject{
        {QStringLiteral("username"), QStringLiteral("fixture-user")}}}})).isEmpty());
    // Trakt's real "uuid" is an opaque 40-character hex identifier, not an
    // RFC 4122 UUID; it must be accepted as the stable account identity.
    const QString opaqueId = QStringLiteral("b6589fc6ab0dc82cf12099d1c2d40ab994e8410c");
    QVERIFY(traktAccountUuidIsCanonical(opaqueId));
    QCOMPARE(traktSettingsUuid(QJsonDocument(QJsonObject{{QStringLiteral("user"), QJsonObject{
        {QStringLiteral("ids"), QJsonObject{{QStringLiteral("uuid"), opaqueId}}}}}})), opaqueId);
}

void TraktDirectTest::profileOwnershipAndMove()
{
    Fixture f;
    const auto destination = ProfilePaths::account(kOtherProfile, f.root.path()); QVERIFY(destination);
    TrackerConnectionStore other(*destination);
    QString owner;
    QCOMPARE(other.externalAccountClaim(TrackerProviderId::Trakt, kAccount, &owner), TrackerConnectionStore::ExternalAccountClaim::Claimed);
    QCOMPARE(owner, kProfile); QVERIFY(!other.upsert(f.connection()));
    QVERIFY(TrackerLifecycleCoordinator::moveConnection(f.profile, *destination, TrackerProviderId::Trakt, f.vault, f.clock.now));
    QVERIFY(other.refresh()); QVERIFY(f.connections.refresh());
    QCOMPARE(f.connections.connection(TrackerProviderId::Trakt)->state, TrackerConnectionState::Disconnected);
    QCOMPARE(other.connection(TrackerProviderId::Trakt)->remoteAccountId, kAccount);
    QVERIFY(!f.vault.loadForProfile(kProfile, TrackerProviderId::Trakt));
    QVERIFY(f.vault.loadForProfile(kOtherProfile, TrackerProviderId::Trakt));
    FakeAuthTransport auth; auth.holdDevice = true;
    TraktAuthSession session({kProfile, 1}, configuration(), &f.vault, &auth, &f.clock);
    QVERIFY(session.begin()); session.invalidateProfile({kOtherProfile, 2});
    auth.deviceCompletion(auth.device); QCOMPARE(session.snapshot().phase, TraktAuthPhase::Idle);
}

void TraktDirectTest::historyImport()
{
    Fixture f; f.transport.history = {historyRow(f.clock.now - 5000)};
    f.sync.syncAll({QStringLiteral("trakt")}, 1);
    QCOMPARE(f.imports.batches().size(), 1); const auto batch = f.imports.batches().first();
    QCOMPARE(f.progress.syncEntries().size(), 0); QCOMPARE(f.evidence.contributions().size(), 0);
    QVERIFY(f.imports.resolve(batch.batchId, batch.items.first().itemId, TrackerImportResolution::UseProviderProgress));
    QVERIFY(f.imports.confirm(batch.batchId));
    bool done = false; bool ok = false;
    f.imports.applyConfirmedAsync(batch.batchId, &f.importOwner, [&](bool success, const QString &) { done = true; ok = success; });
    QTRY_VERIFY(done); QVERIFY(ok);
    f.transport.activities = QStringLiteral("fixture-v2");
    f.sync.syncAll({QStringLiteral("trakt")}, 2);
    QTRY_COMPARE(f.evidence.contributions().size(), 1);
    QCOMPARE(f.evidence.contributions().first().providerEventId, QStringLiteral("history:7"));
    QCOMPARE(f.evidence.contributions().first().occurredAtMs, f.clock.now - 5000);
    QCOMPARE(f.activity.historyProjectionFacts().size(), 0);
    QCOMPARE(f.history.trackerLocalCompletionFacts().size(), 0);
    ProgressStore reloaded(f.profile.progressIniPath());
    QCOMPARE(reloaded.trackerImportedProgressCount(QStringLiteral("trakt"), kAccount), 1);
}

void TraktDirectTest::playbackImport()
{
    Fixture f; f.transport.playback = {playbackRow(41.25, f.clock.now - 5000)};
    f.sync.syncAll({QStringLiteral("trakt")}, 1);
    const auto batch = f.imports.batches().first();
    QVERIFY(f.imports.resolve(batch.batchId, batch.items.first().itemId, TrackerImportResolution::UseProviderProgress));
    QVERIFY(f.imports.confirm(batch.batchId));
    bool done = false; bool ok = false;
    f.imports.applyConfirmedAsync(batch.batchId, &f.importOwner, [&](bool success, const QString &) { done = true; ok = success; });
    QTRY_VERIFY(done); QVERIFY(ok);
    const auto entry = f.progress.deliveryEntry(QStringLiteral("video"), QStringLiteral("tt0000042"));
    QCOMPARE(entry.value(QStringLiteral("progress")).toDouble(), 0.4125);
    QCOMPARE(f.activity.historyProjectionFacts().size(), 0); QCOMPARE(f.evidence.contributions().size(), 0);
}

void TraktDirectTest::completionExport()
{
    Fixture f; const auto fact = f.nativeCompletion(); QVERIFY(!fact.sourceEventId.isEmpty());
    QCOMPARE(f.delivery.operations().size(), 0); QVERIFY(f.consent(fact));
    f.sync.start(); QTRY_COMPARE(f.transport.posts().size(), 1);
    QCOMPARE(f.transport.posts().first().request.url().path(), QStringLiteral("/sync/history"));
    const auto item = f.transport.posts().first().body.object().value(QStringLiteral("movies")).toArray().first().toObject();
    QCOMPARE(item.value(QStringLiteral("watched_at")).toString(),
             QDateTime::fromMSecsSinceEpoch(fact.sourceRevision, Qt::UTC).toString(Qt::ISODateWithMs));
    QCOMPARE(f.delivery.operations().first().state, TrackerDeliveryState::Succeeded);
    QCOMPARE(fact.historyKind, QStringLiteral("movie"));
    QCOMPARE(f.delivery.operations().first().mapping.canonical.historyKind, QStringLiteral("video"));
    TrackerDeliveryStore reloaded(f.profile, &f.mappings, &f.connections);
    QVERIFY(reloaded.healthy()); QCOMPARE(reloaded.operations().size(), 1);
    QCOMPARE(reloaded.operations().first().state, TrackerDeliveryState::Succeeded);
}

void TraktDirectTest::progressExport()
{
    Fixture f; const auto fact = f.nativeProgress(); QVERIFY(f.source.isDurablyCurrent(fact));
    QVERIFY(f.consent(fact)); f.sync.start(); QTRY_COMPARE(f.transport.posts().size(), 1);
    QCOMPARE(f.transport.posts().first().request.url().path(), QStringLiteral("/scrobble/pause"));
    QCOMPARE(f.transport.posts().first().body.object().value(QStringLiteral("progress")).toDouble(), 41.0);
    QCOMPARE(f.delivery.operations().first().state, TrackerDeliveryState::Succeeded);
}

void TraktDirectTest::startPauseStopScrobble()
{
    Fixture f;
    TrackerScrobbleRuntime runtime(f.profile, &f.connections, &f.mappings);
    runtime.setProviderTransport(TrackerProviderId::Trakt, &f.api); QVERIFY(runtime.start());
    QVERIFY(!runtime.livePlaybackTrackingEnabled(QStringLiteral("trakt")));
    QVERIFY(runtime.setLivePlaybackTrackingEnabled(QStringLiteral("trakt"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
        {QStringLiteral("world"), QStringLiteral("theatre")}, {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("itemKey"), QStringLiteral("tt0000042")}};
    auto event = [&](const QString &action, int sequence, int position) {
        return QVariantMap{{QStringLiteral("action"), action}, {QStringLiteral("identity"), identity},
            {QStringLiteral("sessionId"), QStringLiteral("fixture-session")},
            {QStringLiteral("scopeGeneration"), runtime.playbackScopeGeneration()},
            {QStringLiteral("playbackGeneration"), 1}, {QStringLiteral("transitionSequence"), sequence},
            {QStringLiteral("positionMs"), position}, {QStringLiteral("durationMs"), 100000},
            {QStringLiteral("completedLocally"), action == QLatin1String("close")}};
    };
    runtime.observePlaybackLifecycle(event(QStringLiteral("start"), 1, 1000));
    runtime.observePlaybackLifecycle(event(QStringLiteral("pause"), 2, 41000));
    runtime.observePlaybackLifecycle(event(QStringLiteral("resume"), 3, 41000));
    runtime.observePlaybackLifecycle(event(QStringLiteral("close"), 4, 100000));
    QCOMPARE(f.transport.posts().size(), 4);
    QCOMPARE(f.transport.posts().at(0).request.url().path(), QStringLiteral("/scrobble/start"));
    QCOMPARE(f.transport.posts().at(1).request.url().path(), QStringLiteral("/scrobble/pause"));
    QCOMPARE(f.transport.posts().at(3).request.url().path(), QStringLiteral("/scrobble/stop"));
    for (const auto &intent : runtime.store()->intents()) {
        QCOMPARE(intent.providerId, TrackerProviderId::Trakt); QCOMPARE(intent.state, TrackerScrobbleState::Succeeded);
    }
}

void TraktDirectTest::unknownReconciliation_data()
{
    QTest::addColumn<bool>("completion"); QTest::addColumn<bool>("exact");
    QTest::newRow("history-exact-time") << true << true;
    QTest::newRow("history-prior-watch") << true << false;
    QTest::newRow("progress-exact") << false << true;
    QTest::newRow("progress-rounded-collision") << false << false;
}

void TraktDirectTest::unknownReconciliation()
{
    QFETCH(bool, completion); QFETCH(bool, exact);
    Fixture f; const auto fact = completion ? f.nativeCompletion() : f.nativeProgress();
    QVERIFY(f.consent(fact));
    if (completion) f.transport.history = {historyRow(fact.sourceRevision - (exact ? 0 : 86400000))};
    else f.transport.playback = {playbackRow(exact ? 41.0 : 41.49, f.clock.now - 5000)};
    f.transport.loseAcknowledgement = true;
    f.sync.start(); QTRY_COMPARE(f.transport.posts().size(), 1);
    QTRY_COMPARE(f.delivery.operations().first().state,
                 exact ? TrackerDeliveryState::Succeeded : TrackerDeliveryState::NeedsAttention);
    QCOMPARE(f.delivery.operations().first().reason,
             exact ? TrackerDeliveryReason::ReadbackPresent : TrackerDeliveryReason::ReadbackDifferent);
    QCOMPARE(f.transport.posts().size(), 1);
}

void TraktDirectTest::duplicateSuppression()
{
    Fixture f; TrackerScrobbleRuntime runtime(f.profile, &f.connections, &f.mappings);
    runtime.setProviderTransport(TrackerProviderId::Trakt, &f.api); QVERIFY(runtime.start());
    runtime.setSyncSettingsStore(&f.settings);
    auto suppress = [&] { return runtime.suppressLegacyTrackerPlaybackRelay(); };
    QVERIFY(!suppress()); QVERIFY(runtime.setLivePlaybackTrackingEnabled(QStringLiteral("trakt"), true)); QVERIFY(suppress());
    QVERIFY(runtime.setLivePlaybackTrackingEnabled(QStringLiteral("trakt"), false)); QVERIFY(!suppress());
    QVERIFY(f.connections.upsert({TrackerProviderId::Simkl, QStringLiteral("42"), 1, f.clock.now,
                                  TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}));
    QVERIFY(runtime.setLivePlaybackTrackingEnabled(QStringLiteral("trakt"), true)); QVERIFY(suppress());
    QCOMPARE(f.connections.connection(TrackerProviderId::Simkl)->state, TrackerConnectionState::Connected);
    QVERIFY(f.settings.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false)); QVERIFY(!suppress());
    QVERIFY(f.settings.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), true)); QVERIFY(suppress());
    QVERIFY(f.connections.setDisconnected(TrackerProviderId::Trakt, kAccount, 1)); QVERIFY(!suppress());
}

void TraktDirectTest::disconnectPendingPolicy()
{
    for (const auto choice : {TrackerDisconnectChoice::KeepPaused, TrackerDisconnectChoice::DiscardKnownUnsent}) {
        Fixture f; const auto fact = f.nativeProgress(); QVERIFY(f.consent(fact));
        TrackerScrobbleStore scrobbles(f.profile);
        const auto local = f.progress.syncEntries();
        QVERIFY(!TrackerLifecycleCoordinator::disconnect(f.profile, TrackerProviderId::Trakt,
            TrackerDisconnectChoice::Cancel, f.vault, f.connections, f.mappings, f.delivery, scrobbles));
        QCOMPARE(f.connections.connection(TrackerProviderId::Trakt)->state, TrackerConnectionState::Connected);
        QVERIFY(TrackerLifecycleCoordinator::disconnect(f.profile, TrackerProviderId::Trakt, choice,
            f.vault, f.connections, f.mappings, f.delivery, scrobbles));
        QCOMPARE(f.progress.syncEntries(), local); QVERIFY(!f.vault.loadForProfile(kProfile, TrackerProviderId::Trakt));
        QCOMPARE(f.delivery.operations().size(), choice == TrackerDisconnectChoice::KeepPaused ? 1 : 0);
    }
    Fixture f; const auto fact = f.nativeProgress(); QVERIFY(f.consent(fact));
    const QString operation = f.delivery.operations().first().operationId;
    QVERIFY(f.delivery.markDelivering(operation, &f.source, f.clock.now));
    QVERIFY(f.delivery.recordAttemptResult(operation, TrackerDeliveryAttemptResult::UnknownOutcome, f.clock.now));
    TrackerScrobbleStore scrobbles(f.profile);
    QVERIFY(TrackerLifecycleCoordinator::disconnect(f.profile, TrackerProviderId::Trakt, TrackerDisconnectChoice::DiscardKnownUnsent,
        f.vault, f.connections, f.mappings, f.delivery, scrobbles));
    QCOMPARE(f.delivery.operation(operation)->state, TrackerDeliveryState::UnknownOutcome);
}

void TraktDirectTest::revokeAfterLocalDisconnect()
{
    Fixture f; TrackerScrobbleStore scrobbles(f.profile);
    bool localWasDisconnectedAtRevoke = false;
    QVERIFY(!f.api.disconnectThenRevoke([] { return false; }));
    QCOMPARE(f.transport.posts().size(), 0);
    f.transport.loseAcknowledgement = true;
    QVERIFY(f.api.disconnectThenRevoke([&] {
        const bool ok = TrackerLifecycleCoordinator::disconnect(f.profile, TrackerProviderId::Trakt,
            TrackerDisconnectChoice::KeepPaused, f.vault, f.connections, f.mappings, f.delivery, scrobbles);
        localWasDisconnectedAtRevoke = ok && f.connections.connection(TrackerProviderId::Trakt)->state == TrackerConnectionState::Disconnected;
        return ok;
    }));
    QVERIFY(localWasDisconnectedAtRevoke); QCOMPARE(f.transport.posts().size(), 1);
    QVERIFY(f.transport.posts().first().request.url().path().endsWith(QLatin1String("/revoke")));
    QCOMPARE(f.transport.posts().first().body.object().value(QStringLiteral("token")).toString(), QStringLiteral("old-access"));
    QVERIFY(!f.vault.loadForProfile(kProfile, TrackerProviderId::Trakt));
}

void TraktDirectTest::unconfiguredUnavailable()
{
    Fixture f;
    TraktApiClient api(f.profile, {}, &f.vault, &f.transport, &f.clock); QVERIFY(!api.available());
    TraktConnectionController controller(f.profile, &f.connections, nullptr, std::nullopt,
        &f.vault, nullptr, nullptr, &f.clock); QVERIFY(!controller.available());
    QVERIFY(!controller.beginConnection(QStringLiteral("trakt")));
    const int requests = f.transport.requests.size();
    api.get(QStringLiteral("/sync/history"), {}, [](const auto &r) { QVERIFY(!r.succeeded()); });
    QCOMPARE(f.transport.requests.size(), requests);
#if !defined(COLOSSEUM_TRAKT_CLIENT_ID) || !defined(COLOSSEUM_TRAKT_TOKEN_BROKER_URL)
    QVERIFY(!traktProductionConfiguration());
#endif
}

void TraktDirectTest::staleSnapshotAndPull()
{
    Fixture f; const auto fact = f.nativeProgress();
    f.transport.holdNext = true;
    bool completed = false; bool accepted = false;
    f.sync.readExportSnapshotAsync(f.connection(), {fact}, [&](auto value) { completed = true; accepted = value.has_value(); });
    QVERIFY(f.connections.setDisconnected(TrackerProviderId::Trakt, kAccount, 1));
    TraktApiResult result; result.statusCode = 200; result.document = QJsonDocument(QJsonArray{});
    f.transport.heldCompletion(result); QVERIFY(completed); QVERIFY(!accepted);
    Fixture pull; pull.transport.holdNext = true; pull.sync.syncAll({QStringLiteral("trakt")}, 1);
    QVERIFY(pull.connections.setDisconnected(TrackerProviderId::Trakt, kAccount, 1));
    result.document = QJsonDocument(QJsonObject{}); pull.transport.heldCompletion(result);
    QCOMPARE(pull.imports.batches().size(), 0);
}

void TraktDirectTest::skippedHistoryHasNoEvidence()
{
    Fixture f; f.nativeProgress(); f.transport.history = {historyRow(f.clock.now - 5000)};
    f.sync.syncAll({QStringLiteral("trakt")}, 1);
    const auto batch = f.imports.batches().first();
    QVERIFY(f.imports.resolve(batch.batchId, batch.items.first().itemId, TrackerImportResolution::LeaveUnresolved));
    QVERIFY(f.imports.confirm(batch.batchId));
    bool done = false;
    f.imports.applyConfirmedAsync(batch.batchId, &f.importOwner, [&](bool success, const QString &) { QVERIFY(success); done = true; });
    QTRY_VERIFY(done);
    f.sync.syncAll({QStringLiteral("trakt")}, 2);
    QCoreApplication::processEvents(); QCOMPARE(f.evidence.contributions().size(), 0);
}

void TraktDirectTest::numericIdentityDoesNotAutoMatch()
{
    Fixture f;
    f.progress.record({{QStringLiteral("kind"), QStringLiteral("video")}, {QStringLiteral("id"), QStringLiteral("123")},
        {QStringLiteral("title"), QStringLiteral("Different identity namespace")}, {QStringLiteral("progress"), 0.1}});
    f.progress.flush();
    auto remote = movie(43); remote.insert(QStringLiteral("ids"), QJsonObject{{QStringLiteral("trakt"), 43}, {QStringLiteral("tmdb"), 123}});
    auto row = historyRow(f.clock.now - 5000, 43); row.insert(QStringLiteral("movie"), remote);
    f.transport.history = {row}; f.sync.syncAll({QStringLiteral("trakt")}, 1);
    QVERIFY(!f.mappings.mapping({TrackerProviderId::Trakt, kAccount, QStringLiteral("movie:43")}));
    QCOMPARE(f.imports.batches().first().items.first().classification, TrackerImportClassification::NeedsMatching);
}

void TraktDirectTest::sameAccountProvidersRemainIsolated()
{
    Fixture f;
    QVERIFY(f.connections.upsert({TrackerProviderId::Simkl, kAccount, 1, f.clock.now,
        TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}));
    QVERIFY(f.mappings.upsert({TrackerProviderId::Simkl, kAccount, QStringLiteral("42")},
        {QStringLiteral("movie:tt0000042"), QStringLiteral("movie"), QStringLiteral("tt0000042"),
         QStringLiteral("Fixture movie")}, TrackerMappingProvenance::UserConfirmed));
    HeldScrobbleTransport simkl; HeldScrobbleTransport trakt;
    TrackerScrobbleRuntime runtime(f.profile, &f.connections, &f.mappings, &simkl);
    runtime.setProviderTransport(TrackerProviderId::Trakt, &trakt); QVERIFY(runtime.start());
    QVERIFY(runtime.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    QVERIFY(runtime.setLivePlaybackTrackingEnabled(QStringLiteral("trakt"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
        {QStringLiteral("world"), QStringLiteral("theatre")}, {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("itemKey"), QStringLiteral("tt0000042")}};
    auto event = [&](const QString &action, int sequence) {
        return QVariantMap{{QStringLiteral("action"), action}, {QStringLiteral("identity"), identity},
            {QStringLiteral("sessionId"), QStringLiteral("both-providers")},
            {QStringLiteral("scopeGeneration"), runtime.playbackScopeGeneration()},
            {QStringLiteral("playbackGeneration"), 1}, {QStringLiteral("transitionSequence"), sequence},
            {QStringLiteral("positionMs"), 41000}, {QStringLiteral("durationMs"), 100000}};
    };
    runtime.observePlaybackLifecycle(event(QStringLiteral("start"), 1));
    QCOMPARE(simkl.requests.size(), 1); QCOMPARE(trakt.requests.size(), 1);
    QCOMPARE(simkl.requests.first().remoteAccountId, trakt.requests.first().remoteAccountId);
    QCOMPARE(simkl.requests.first().providerId, TrackerProviderId::Simkl);
    QCOMPARE(trakt.requests.first().providerId, TrackerProviderId::Trakt);
    QVERIFY(simkl.requests.first().operationId != trakt.requests.first().operationId);
    simkl.completions.takeFirst()(SimklScrobbleSendResult::Succeeded);
    trakt.completions.takeFirst()(SimklScrobbleSendResult::Succeeded);
    runtime.observePlaybackLifecycle(event(QStringLiteral("pause"), 2));
    QCOMPARE(simkl.requests.size(), 2); QCOMPARE(trakt.requests.size(), 2);
    simkl.completions.takeFirst()(SimklScrobbleSendResult::Succeeded);
    trakt.completions.takeFirst()(SimklScrobbleSendResult::Succeeded);
    QCOMPARE(runtime.store()->intents().size(), 4);
}

void TraktDirectTest::delayedRefreshAfterDisconnectIsFenced()
{
    Fixture f;
    auto expired = credential(kProfile, f.clock.now); expired.accessTokenExpiresAtMs = f.clock.now - 1;
    QVERIFY(f.vault.saveAndVerify(expired));
    int successes = 0;
    f.api.get(QStringLiteral("/sync/history"), {}, [&](const auto &result) { if (result.succeeded()) ++successes; });
    QCOMPARE(f.transport.refreshes, 1);
    TrackerScrobbleStore scrobbles(f.profile);
    QVERIFY(f.api.disconnectThenRevoke([&] {
        return TrackerLifecycleCoordinator::disconnect(f.profile, TrackerProviderId::Trakt,
            TrackerDisconnectChoice::KeepPaused, f.vault, f.connections, f.mappings, f.delivery, scrobbles);
    }));
    f.transport.completeRefresh();
    QCOMPARE(successes, 0); QVERIFY(!f.vault.loadForProfile(kProfile, TrackerProviderId::Trakt));
    QCOMPARE(f.connections.connection(TrackerProviderId::Trakt)->state, TrackerConnectionState::Disconnected);
    f.api.deactivate(); f.api.resetForConnection(); QVERIFY(!f.api.available());
}

void TraktDirectTest::remoteStopDoesNotCreateNativeCompletion()
{
    Fixture f; TrackerScrobbleRuntime runtime(f.profile, &f.connections, &f.mappings);
    runtime.setProviderTransport(TrackerProviderId::Trakt, &f.api); QVERIFY(runtime.start());
    QVERIFY(runtime.setLivePlaybackTrackingEnabled(QStringLiteral("trakt"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
        {QStringLiteral("world"), QStringLiteral("theatre")}, {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("itemKey"), QStringLiteral("tt0000042")}};
    auto event = [&](const QString &action, int sequence, int position) {
        return QVariantMap{{QStringLiteral("action"), action}, {QStringLiteral("identity"), identity},
            {QStringLiteral("sessionId"), QStringLiteral("remote-only-stop")},
            {QStringLiteral("scopeGeneration"), runtime.playbackScopeGeneration()},
            {QStringLiteral("playbackGeneration"), 1}, {QStringLiteral("transitionSequence"), sequence},
            {QStringLiteral("positionMs"), position}, {QStringLiteral("durationMs"), 100000},
            {QStringLiteral("completedLocally"), false}};
    };
    runtime.observePlaybackLifecycle(event(QStringLiteral("start"), 1, 1000));
    f.transport.loseAcknowledgement = true;
    f.transport.history = {historyRow(QDateTime::currentMSecsSinceEpoch())};
    runtime.observePlaybackLifecycle(event(QStringLiteral("close"), 2, 71000));
    QCOMPARE(f.transport.posts().size(), 2);
    QCOMPARE(f.transport.posts().last().request.url().path(), QStringLiteral("/scrobble/stop"));
    QCOMPARE(f.transport.posts().last().body.object().value(QStringLiteral("progress")).toDouble(), 100.0);
    QCOMPARE(runtime.store()->intents().last().state, TrackerScrobbleState::Succeeded);
    QVERIFY(!runtime.store()->intents().last().completedLocally);
    QCOMPARE(f.activity.historyProjectionFacts().size(), 0);
    QCOMPARE(f.history.trackerLocalCompletionFacts().size(), 0);
    QCOMPARE(f.progress.syncEntries().size(), 0);
}

void TraktDirectTest::videoImportAliases_data()
{
    QTest::addColumn<QString>("historyKind");
    QTest::newRow("movie-to-video") << QStringLiteral("movie");
    QTest::newRow("episode-to-video") << QStringLiteral("episode");
}

void TraktDirectTest::videoImportAliases()
{
    QFETCH(QString, historyKind);
    Fixture f;
    const QString localId = historyKind == QLatin1String("movie")
        ? QStringLiteral("tt0000042") : QStringLiteral("tt0000042:1:2");
    const QString remoteId = historyKind + QStringLiteral(":42");
    QVERIFY(f.mappings.upsert({TrackerProviderId::Trakt, kAccount, remoteId},
        {historyKind + QLatin1Char(':') + localId, historyKind, localId, QStringLiteral("Fixture title")},
        TrackerMappingProvenance::UserConfirmed));
    auto row = historyRow(f.clock.now - 5000);
    if (historyKind == QLatin1String("episode")) {
        row.remove(QStringLiteral("movie"));
        row.insert(QStringLiteral("type"), historyKind);
        QJsonObject episode = movie(); episode.insert(QStringLiteral("season"), 1); episode.insert(QStringLiteral("number"), 2);
        row.insert(QStringLiteral("episode"), episode); row.insert(QStringLiteral("show"), movie(99));
    }
    f.transport.history = {row}; f.sync.syncAll({QStringLiteral("trakt")}, 1);
    const auto batch = f.imports.batches().first(); QCOMPARE(batch.items.size(), 1);
    QVERIFY(batch.items.first().remote.exactProgressTarget);
    QCOMPARE(batch.items.first().remote.exactProgressTarget->kind, QStringLiteral("video"));
    QVERIFY(f.imports.resolve(batch.batchId, batch.items.first().itemId, TrackerImportResolution::UseProviderProgress));
    QVERIFY(f.imports.confirm(batch.batchId));
    bool done = false; bool ok = false;
    f.imports.applyConfirmedAsync(batch.batchId, &f.importOwner,
        [&](bool success, const QString &) { done = true; ok = success; });
    QTRY_VERIFY(done); QVERIFY(ok);
    QCOMPARE(f.progress.deliveryEntry(QStringLiteral("video"), localId).value(QStringLiteral("progress")).toDouble(), 1.0);
    QVERIFY(f.progress.deliveryEntry(historyKind, localId).isEmpty());
}

void TraktDirectTest::scrobbleAliasRejectsWrongRemoteKind()
{
    Fixture f;
    const TrackerRemoteMediaKey movieRemote{TrackerProviderId::Trakt, kAccount, QStringLiteral("movie:42")};
    const auto canonical = f.mappings.mapping(movieRemote)->canonical;
    QVERIFY(f.mappings.remove(movieRemote));
    QVERIFY(f.mappings.upsert({TrackerProviderId::Trakt, kAccount, QStringLiteral("episode:42")},
        canonical, TrackerMappingProvenance::UserConfirmed));
    TrackerScrobbleRuntime runtime(f.profile, &f.connections, &f.mappings);
    runtime.setProviderTransport(TrackerProviderId::Trakt, &f.api); QVERIFY(runtime.start());
    QVERIFY(runtime.setLivePlaybackTrackingEnabled(QStringLiteral("trakt"), true));
    QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
        {QStringLiteral("world"), QStringLiteral("theatre")}, {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("itemKey"), QStringLiteral("tt0000042")}};
    QVariantMap event{{QStringLiteral("action"), QStringLiteral("start")}, {QStringLiteral("identity"), identity},
        {QStringLiteral("sessionId"), QStringLiteral("mapping-kind-test")},
        {QStringLiteral("scopeGeneration"), runtime.playbackScopeGeneration()},
        {QStringLiteral("playbackGeneration"), 1}, {QStringLiteral("transitionSequence"), 1},
        {QStringLiteral("positionMs"), 1000}, {QStringLiteral("durationMs"), 100000}};
    runtime.observePlaybackLifecycle(event);
    QCOMPARE(f.transport.posts().size(), 0); QCOMPARE(runtime.store()->intents().size(), 0);
    identity.insert(QStringLiteral("kind"), QStringLiteral("episode"));
    event.insert(QStringLiteral("identity"), identity); event.insert(QStringLiteral("playbackGeneration"), 2);
    runtime.observePlaybackLifecycle(event);
    QCOMPARE(f.transport.posts().size(), 1);
    QCOMPARE(f.transport.posts().first().body.object().value(QStringLiteral("episode")).toObject()
                 .value(QStringLiteral("ids")).toObject().value(QStringLiteral("trakt")).toInt(), 42);
}

void TraktDirectTest::reconnectAllowsAnotherPull()
{
    Fixture f; f.transport.holdNext = true;
    f.sync.syncAll({QStringLiteral("trakt")}, 1);
    QCOMPARE(f.transport.requests.size(), 1); QCOMPARE(f.imports.batches().size(), 0);
    QVERIFY(f.connections.setDisconnected(TrackerProviderId::Trakt, kAccount, 1));
    f.api.resetForConnection();
    auto next = f.connection(); next.state = TrackerConnectionState::Connected;
    next.connectionGeneration = f.connections.nextConnectionGeneration(TrackerProviderId::Trakt);
    QVERIFY(f.connections.upsert(next));
    f.sync.connectionEstablished(QStringLiteral("trakt"));
    QCOMPARE(f.imports.batches().size(), 1);
    QCOMPARE(f.imports.batches().first().connectionGeneration, next.connectionGeneration);
    const int completedRequests = f.transport.requests.size();
    TraktApiResult old; old.statusCode = 200; old.document = QJsonDocument(QJsonObject{});
    f.transport.heldCompletion(old);
    QCOMPARE(f.transport.requests.size(), completedRequests); QCOMPARE(f.imports.batches().size(), 1);
}

void TraktDirectTest::mismatchedVaultAccountIsRejected()
{
    Fixture f;
    auto other = credential(kProfile, f.clock.now);
    other.slot.remoteAccountId = QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    QVERIFY(f.vault.saveAndVerify(other));
    bool completed = false; bool accepted = false;
    f.sync.readExportSnapshotAsync(f.connection(), {}, [&](auto result) { completed = true; accepted = result.has_value(); });
    QVERIFY(completed); QVERIFY(!accepted);
    f.sync.syncAll({QStringLiteral("trakt")}, 1);
    QCOMPARE(f.imports.batches().size(), 0); QCOMPARE(f.transport.requests.size(), 0);
}

void TraktDirectTest::partialDisconnectStillFencesAndRevokes()
{
    Fixture f;
    auto expired = credential(kProfile, f.clock.now); expired.accessTokenExpiresAtMs = f.clock.now - 1;
    QVERIFY(f.vault.saveAndVerify(expired));
    int successes = 0;
    f.api.get(QStringLiteral("/sync/history"), {}, [&](const auto &result) { if (result.succeeded()) ++successes; });
    QCOMPARE(f.transport.refreshes, 1);
    QVERIFY(!f.api.disconnectThenRevoke([&] {
        f.vault.clearForProfile(kProfile, TrackerProviderId::Trakt);
        return false;
    }));
    QCOMPARE(f.transport.posts().size(), 2);
    QVERIFY(f.transport.posts().last().request.url().path().endsWith(QLatin1String("/revoke")));
    f.transport.completeRefresh();
    QCOMPARE(successes, 0); QVERIFY(!f.vault.loadForProfile(kProfile, TrackerProviderId::Trakt));
}

QTEST_GUILESS_MAIN(TraktDirectTest)
#include "tst_trakt_direct.moc"
