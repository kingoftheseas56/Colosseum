// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/ProfilePaths.h"
#include "account/SyncAdapter.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncHybridClock.h"
#include "account/SyncProtocol.h"
#include "account/SyncStateStore.h"

#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto kAccountA =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kAccountB =
    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
constexpr auto kDeviceA =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kDeviceB =
    "22222222-2222-4222-8222-222222222222";

struct FixtureJournalEntry {
    quint64 serverSeq = 0;
    bool won = false;
    bool canonical = false;
    SyncWireMutation mutation;
};

struct FixtureMutationResult {
    quint64 serverSeq = 0;
    bool won = false;
};

struct FixtureSnapshotPage {
    quint64 cursor = 0;
    bool hasMore = false;
    QString nextPageToken;
    QList<SyncWirePullEntry> entries;
};

QString recordIdentity(
    const QString &category,
    const QString &recordKey) {
    return category
        + QChar(0x1f)
        + recordKey;
}

class FixtureSyncService {
public:
    qint64 serverTimeMs = 2000000;
    qint64 maxFutureSkewMs =
        10 * 60 * 1000;

    int acceptedMutationCount() const {
        return m_idempotency.size();
    }

    const QList<FixtureJournalEntry> &
    journal() const {
        return m_journal;
    }

    void setCanonicalCurrentPullMode(bool enabled) {
        m_canonicalCurrentPullMode = enabled;
    }

    void setSnapshotPages(
        const QList<FixtureSnapshotPage> &pages) {
        m_snapshotPages = pages;
        m_snapshotServed = 0;
    }

    // Makes the next snapshot requests fail with a network error once
    // the given number of pages has already been served.
    void setSnapshotNetworkFailuresAfterPages(
        int servedPages,
        int failures) {
        m_snapshotFailAfterServed = servedPages;
        m_snapshotNetworkFailures = failures;
    }

    int snapshotRequestCount() const {
        return m_snapshotRequestTokens.size();
    }

    QStringList snapshotRequestTokens() const {
        return m_snapshotRequestTokens;
    }

    QStringList pushAttachmentIds() const {
        return m_pushAttachmentIds;
    }

    QStringList pullRequestAfters() const {
        return m_pullRequestAfters;
    }

    void appendRemote(
        const SyncWireMutation &mutation,
        bool won,
        bool canonical = false) {
        FixtureJournalEntry entry;
        entry.serverSeq =
            m_nextServerSeq++;
        entry.won = won;
        entry.canonical = canonical;
        entry.mutation =
            mutation;
        m_journal.append(entry);

        if (won) {
            m_current.insert(
                recordIdentity(
                    mutation.category,
                    mutation.recordKey),
                mutation);
        }
    }

    AccountTransportReply push(
        const QJsonArray &mutations,
        const QString &attachmentId) {
        m_pushAttachmentIds.append(
            attachmentId);

        AccountTransportReply reply;
        reply.statusCode = 200;

        QJsonArray results;

        for (const QJsonValue &value :
             mutations) {
            if (!value.isObject()) {
                continue;
            }

            const auto parsed =
                syncWireMutationFromJson(
                    value.toObject());
            if (!parsed.has_value()) {
                QJsonObject result;
                result.insert(
                    QStringLiteral(
                        "mutation_id"),
                    value.toObject()
                        .value(
                            QStringLiteral(
                                "mutation_id"))
                        .toString());
                result.insert(
                    QStringLiteral(
                        "accepted"),
                    false);
                result.insert(
                    QStringLiteral("code"),
                    QStringLiteral(
                        "invalid_mutation"));
                results.append(result);
                continue;
            }

            const SyncWireMutation mutation =
                *parsed;

            const auto duplicate =
                m_idempotency.constFind(
                    mutation.mutationId);
            if (duplicate
                != m_idempotency.constEnd()) {
                QJsonObject result;
                result.insert(
                    QStringLiteral(
                        "mutation_id"),
                    mutation.mutationId);
                result.insert(
                    QStringLiteral(
                        "accepted"),
                    true);
                result.insert(
                    QStringLiteral(
                        "server_seq"),
                    QString::number(
                        duplicate->serverSeq));
                result.insert(
                    QStringLiteral("won"),
                    duplicate->won);
                results.append(result);
                continue;
            }

            if (mutation.hlc.physicalMs
                > serverTimeMs
                    + maxFutureSkewMs) {
                QJsonObject result;
                result.insert(
                    QStringLiteral(
                        "mutation_id"),
                    mutation.mutationId);
                result.insert(
                    QStringLiteral(
                        "accepted"),
                    false);
                result.insert(
                    QStringLiteral("code"),
                    QStringLiteral(
                        "clock_skew"));
                result.insert(
                    QStringLiteral("message"),
                    QStringLiteral(
                        "fixture future clock"));

                const auto current =
                    m_current.constFind(
                        recordIdentity(
                            mutation.category,
                            mutation.recordKey));
                if (current
                    != m_current.constEnd()) {
                    QJsonObject metadata;
                    metadata.insert(
                        QStringLiteral(
                            "mutation_id"),
                        current->mutationId);
                    metadata.insert(
                        QStringLiteral(
                            "device_id"),
                        current->deviceId);
                    metadata.insert(
                        QStringLiteral(
                            "schema_version"),
                        current->schemaVersion);
                    metadata.insert(
                        QStringLiteral(
                            "hlc_physical_ms"),
                        QString::number(
                            current->hlc
                                .physicalMs));
                    metadata.insert(
                        QStringLiteral(
                            "hlc_counter"),
                        QString::number(
                            current->hlc
                                .counter));
                    metadata.insert(
                        QStringLiteral(
                            "operation"),
                        syncWireOperationName(
                            current->operation));

                    quint64 currentSeq = 0;
                    for (const FixtureJournalEntry &entry :
                         m_journal) {
                        if (entry.mutation.mutationId
                            == current->mutationId) {
                            currentSeq =
                                entry.serverSeq;
                        }
                    }
                    metadata.insert(
                        QStringLiteral(
                            "server_seq"),
                        QString::number(
                            currentSeq));
                    result.insert(
                        QStringLiteral(
                            "current"),
                        metadata);
                }

                results.append(result);
                continue;
            }

            const QString identity =
                recordIdentity(
                    mutation.category,
                    mutation.recordKey);

            const auto current =
                m_current.constFind(
                    identity);

            const bool won =
                current
                    == m_current.constEnd()
                || syncWireHlcGreater(
                    mutation.hlc,
                    current->hlc);

            FixtureJournalEntry entry;
            entry.serverSeq =
                m_nextServerSeq++;
            entry.won = won;
            entry.mutation =
                mutation;
            m_journal.append(entry);

            if (won)
                m_current.insert(
                    identity,
                    mutation);

            m_idempotency.insert(
                mutation.mutationId,
                FixtureMutationResult{
                    entry.serverSeq,
                    won});

            QJsonObject result;
            result.insert(
                QStringLiteral(
                    "mutation_id"),
                mutation.mutationId);
            result.insert(
                QStringLiteral("accepted"),
                true);
            result.insert(
                QStringLiteral(
                    "server_seq"),
                QString::number(
                    entry.serverSeq));
            result.insert(
                QStringLiteral("won"),
                won);
            results.append(result);
        }

        reply.body.insert(
            QStringLiteral(
                "server_time_ms"),
            QString::number(
                serverTimeMs));
        reply.body.insert(
            QStringLiteral("results"),
            results);
        return reply;
    }

    AccountTransportReply pull(
        quint64 after) const {
        AccountTransportReply reply;
        reply.statusCode = 200;

        QJsonArray entries;
        bool hasMore = false;

        for (const FixtureJournalEntry &entry :
             m_journal) {
            if (entry.serverSeq <= after)
                continue;

            if (m_canonicalCurrentPullMode
                && !entry.won) {
                continue;
            }

            if (entries.size() >= 200) {
                hasMore = true;
                break;
            }

            QJsonObject object;
            object.insert(
                QStringLiteral(
                    "server_seq"),
                QString::number(
                    entry.serverSeq));
            object.insert(
                QStringLiteral("won"),
                entry.won);
            if (m_canonicalCurrentPullMode
                || entry.canonical) {
                object.insert(
                    QStringLiteral("canonical"),
                    true);
            }
            object.insert(
                QStringLiteral("mutation"),
                syncWireMutationToJson(
                    entry.mutation));
            entries.append(object);
        }

        reply.body.insert(
            QStringLiteral(
                "server_time_ms"),
            QString::number(
                serverTimeMs));
        reply.body.insert(
            QStringLiteral("entries"),
            entries);
        reply.body.insert(
            QStringLiteral("has_more"),
            hasMore);
        return reply;
    }

    AccountTransportReply snapshot(
        const QString &pageToken) {
        m_snapshotRequestTokens.append(
            pageToken);

        AccountTransportReply reply;

        if (m_snapshotServed
                >= m_snapshotFailAfterServed
            && m_snapshotNetworkFailures
                > 0) {
            --m_snapshotNetworkFailures;
            reply.networkError = true;
            reply.errorCode =
                QStringLiteral("offline");
            reply.errorMessage =
                QStringLiteral(
                    "fixture snapshot offline");
            return reply;
        }

        if (m_snapshotServed
            >= m_snapshotPages.size()) {
            reply.statusCode = 400;
            reply.errorCode =
                QStringLiteral(
                    "fixture_snapshot_exhausted");
            return reply;
        }

        const FixtureSnapshotPage &page =
            m_snapshotPages.at(
                m_snapshotServed++);

        reply.statusCode = 200;

        QJsonArray entries;
        for (const SyncWirePullEntry &entry :
             page.entries) {
            QJsonObject object;
            object.insert(
                QStringLiteral(
                    "server_seq"),
                QString::number(
                    entry.serverSeq));
            object.insert(
                QStringLiteral("won"),
                entry.won);
            if (entry.canonical) {
                object.insert(
                    QStringLiteral(
                        "canonical"),
                    true);
            }
            object.insert(
                QStringLiteral(
                    "mutation"),
                syncWireMutationToJson(
                    entry.mutation));
            entries.append(object);
        }

        reply.body.insert(
            QStringLiteral(
                "server_time_ms"),
            QString::number(
                serverTimeMs));
        reply.body.insert(
            QStringLiteral("cursor"),
            QString::number(
                page.cursor));
        reply.body.insert(
            QStringLiteral("entries"),
            entries);
        reply.body.insert(
            QStringLiteral("has_more"),
            page.hasMore);
        if (!page.nextPageToken
                 .isEmpty()) {
            reply.body.insert(
                QStringLiteral(
                    "next_page_token"),
                page.nextPageToken);
        }
        return reply;
    }

    void notePullAfter(quint64 after) {
        m_pullRequestAfters.append(
            QString::number(after));
    }

private:
    quint64 m_nextServerSeq = 1;
    QList<FixtureJournalEntry> m_journal;
    QHash<QString, FixtureMutationResult>
        m_idempotency;
    QHash<QString, SyncWireMutation>
        m_current;
    bool m_canonicalCurrentPullMode = false;

    QList<FixtureSnapshotPage> m_snapshotPages;
    int m_snapshotServed = 0;
    int m_snapshotNetworkFailures = 0;
    int m_snapshotFailAfterServed = 0;
    QStringList m_snapshotRequestTokens;
    QStringList m_pushAttachmentIds;
    QStringList m_pullRequestAfters;
};

class FixtureSyncTransport final
    : public AccountTransport {
    Q_OBJECT

public:
    explicit FixtureSyncTransport(
        FixtureSyncService *service,
        QObject *parent = nullptr)
        : AccountTransport(parent),
          m_service(service) {}

    void setOnline(bool online) {
        m_online = online;
    }

    void setPushOnline(bool online) {
        m_pushOnline = online;
    }

    void dropNextPushResponseAfterCommit() {
        m_dropNextPush = true;
    }

    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override {
        AccountTransportReply reply;

        if (!m_online) {
            reply.networkError = true;
            reply.errorCode =
                QStringLiteral("offline");
            reply.errorMessage =
                QStringLiteral(
                    "fixture offline");
            emit finished(
                requestId,
                reply);
            return;
        }

        if (request.bearerToken.isEmpty()) {
            reply.statusCode = 401;
            reply.errorCode =
                QStringLiteral(
                    "session_invalid");
            emit finished(
                requestId,
                reply);
            return;
        }

        if (request.method
                == QByteArrayLiteral("POST")
            && request.path
                == QLatin1String(
                    "/v1/sync/push")) {
            if (!m_pushOnline) {
                reply.networkError = true;
                reply.errorCode =
                    QStringLiteral("offline");
                reply.errorMessage =
                    QStringLiteral(
                        "fixture push offline");
                emit finished(
                    requestId,
                    reply);
                return;
            }

            reply =
                m_service->push(
                    request.body
                        .value(
                            QStringLiteral(
                                "mutations"))
                        .toArray(),
                    request.body
                        .value(
                            QStringLiteral(
                                "attachment_id"))
                        .toString());

            if (m_dropNextPush) {
                m_dropNextPush = false;
                AccountTransportReply dropped;
                dropped.networkError = true;
                dropped.errorCode =
                    QStringLiteral("offline");
                dropped.errorMessage =
                    QStringLiteral(
                        "fixture lost response");
                emit finished(
                    requestId,
                    dropped);
                return;
            }

            emit finished(
                requestId,
                reply);
            return;
        }

        if (request.method
                == QByteArrayLiteral("GET")
            && request.path.startsWith(
                QStringLiteral(
                    "/v1/sync/pull?after="))) {
            bool ok = false;
            const quint64 after =
                request.path
                    .mid(
                        QStringLiteral(
                            "/v1/sync/pull?after=")
                            .size())
                    .toULongLong(
                        &ok);

            if (!ok) {
                reply.statusCode = 400;
                reply.errorCode =
                    QStringLiteral(
                        "invalid_cursor");
            } else {
                m_service->notePullAfter(
                    after);
                reply =
                    m_service->pull(
                        after);
            }

            emit finished(
                requestId,
                reply);
            return;
        }

        if (request.method
                == QByteArrayLiteral("GET")
            && request.path
                == QLatin1String(
                    "/v1/sync/snapshot")) {
            emit finished(
                requestId,
                m_service->snapshot(
                    QString()));
            return;
        }

        if (request.method
                == QByteArrayLiteral("GET")
            && request.path.startsWith(
                QStringLiteral(
                    "/v1/sync/"
                    "snapshot?after_"
                    "key="))) {
            const QString token =
                request.path.mid(
                    QStringLiteral(
                        "/v1/sync/"
                        "snapshot?"
                        "after_key=")
                        .size());
            emit finished(
                requestId,
                m_service->snapshot(
                    token));
            return;
        }

        reply.statusCode = 404;
        reply.errorCode =
            QStringLiteral(
                "fixture_route_missing");
        emit finished(
            requestId,
            reply);
    }

private:
    FixtureSyncService *m_service = nullptr;
    bool m_online = true;
    bool m_pushOnline = true;
    bool m_dropNextPush = false;
};

class SyntheticAdapter final
    : public SyncAdapter {
    Q_OBJECT

public:
    explicit SyntheticAdapter(
        QObject *parent = nullptr)
        : SyncAdapter(parent) {}

    QString categoryId() const override {
        return QStringLiteral(
            "collection");
    }

    int schemaVersion() const override {
        return 1;
    }

    quint64 revision() const override {
        return m_revision;
    }

    bool missingRecordsAreDeletes() const override {
        return m_missingRecordsAreDeletes;
    }

    bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error) const override {
        if (!snapshot) {
            if (error) {
                *error = QStringLiteral(
                    "fixture snapshot missing");
            }
            return false;
        }

        snapshot->revision =
            m_revision;
        snapshot->records.clear();

        QStringList keys =
            m_records.keys();
        keys.sort();

        for (const QString &key : keys) {
            snapshot->records.append(
                SyncAdapterRecord{
                    key,
                    m_records.value(key)});
        }

        return true;
    }

    bool applyRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        QString *error) override {
        if (schemaVersion != 1) {
            if (error) {
                *error = QStringLiteral(
                    "fixture schema mismatch");
            }
            return false;
        }

        if (operation
            == SyncWireOperation::Put) {
            m_records.insert(
                recordKey,
                payload);
        } else {
            m_records.remove(
                recordKey);
        }

        ++m_revision;
        ++m_remoteApplyCount;

        if (m_emitDuringRemoteApply) {
            emit localMutationAvailable(
                m_revision);
        }

        return true;
    }

    void seedLocalWithoutSignal(
        const QString &recordKey,
        const QString &value) {
        m_records.insert(
            recordKey,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    value
                }
            });
        ++m_revision;
    }

    // Simulates the local profile being replaced by a legacy import
    // that lacks an account record, without emitting a mutation signal.
    void removeLocalWithoutSignal(
        const QString &recordKey) {
        m_records.remove(recordKey);
        ++m_revision;
    }

    void putLocal(
        const QString &recordKey,
        const QString &value) {
        m_records.insert(
            recordKey,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    value
                }
            });

        ++m_revision;
        emit localMutationAvailable(
            m_revision);
    }

    void deleteLocal(
        const QString &recordKey) {
        m_records.remove(
            recordKey);

        ++m_revision;
        emit localMutationAvailable(
            m_revision);
    }

    QString value(
        const QString &recordKey) const {
        return m_records
            .value(recordKey)
            .toObject()
            .value(
                QStringLiteral("value"))
            .toString();
    }

    bool contains(
        const QString &recordKey) const {
        return m_records.contains(
            recordKey);
    }

    void setEmitDuringRemoteApply(
        bool enabled) {
        m_emitDuringRemoteApply =
            enabled;
    }

    void setMissingRecordsAreDeletes(
        bool enabled) {
        m_missingRecordsAreDeletes = enabled;
    }

    int remoteApplyCount() const {
        return m_remoteApplyCount;
    }

private:
    QHash<QString, QJsonValue>
        m_records;
    quint64 m_revision = 0;
    bool m_emitDuringRemoteApply = false;
    bool m_missingRecordsAreDeletes = true;
    int m_remoteApplyCount = 0;
};

ProfilePaths accountProfile(
    QTemporaryDir *temp,
    const QString &accountId =
        QString::fromLatin1(kAccountA)) {
    const auto profile =
        ProfilePaths::account(
            accountId,
            temp->path());

    if (!profile.has_value())
        qFatal(
            "fixture account profile invalid");

    QDir().mkpath(
        profile->profileRoot());
    return *profile;
}

struct Replica {
    FixtureSyncTransport transport;
    AccountClient client;
    SyncAdapterRegistry registry;
    SyntheticAdapter adapter;
    SyncEngine engine;
    ProfilePaths profile;

    Replica(
        FixtureSyncService *service,
        const ProfilePaths &profileValue,
        const QString &deviceId,
        qint64 *now,
        bool missingRecordsAreDeletes = true)
        : transport(service),
          client(&transport),
          engine(
              &client,
              &registry,
              [now]() {
                  return *now;
              }),
          profile(profileValue) {
        client.setAccessToken(
            QByteArrayLiteral(
                "fixture-access"));
        adapter.setMissingRecordsAreDeletes(
            missingRecordsAreDeletes);

        if (!registry.registerAdapter(
                &adapter)) {
            qFatal(
                "fixture adapter registration failed");
        }

        engine.setAutomaticSchedulingEnabled(
            false);
        engine.setNetworkEnabled(
            false);

        QString error;
        if (!engine.start(
                profile,
                deviceId,
                &error)) {
            qFatal(
                "fixture engine start failed");
        }
    }
};

SyncWireMutation remoteMutation(
    const QString &mutationId,
    const QString &category,
    const QString &recordKey,
    const QString &deviceId,
    qint64 physicalMs,
    quint64 counter,
    SyncWireOperation operation,
    const QJsonValue &payload =
        QJsonValue()) {
    SyncWireMutation mutation;
    mutation.mutationId =
        mutationId;
    mutation.deviceId =
        deviceId;
    mutation.category =
        category;
    mutation.recordKey =
        recordKey;
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{
        physicalMs,
        counter,
        deviceId};
    mutation.operation =
        operation;
    mutation.payload =
        operation
                == SyncWireOperation::Put
            ? payload
            : QJsonValue();
    return mutation;
}

SyncWirePullEntry canonicalSnapshotEntry(
    quint64 serverSeq,
    const SyncWireMutation &mutation) {
    SyncWirePullEntry entry;
    entry.serverSeq = serverSeq;
    entry.won = true;
    entry.canonical = true;
    entry.mutation = mutation;
    return entry;
}
}

class tst_sync_engine final
    : public QObject {
    Q_OBJECT

private slots:
    void hlcNeverGoesBackwards();
    void trustedLocalOrderingHintsBecomeOrderedHLCs();
    void rejectedFutureCanRebaseToServiceTime();
    void stateStoreRoundTripPreservesCheckpoint();
    void offlineMutationIsDurableAcrossRestart();
    void bearerRejectionPausesForAuthenticationRecoveryWithoutDroppingOutbox();
    void adapterRegisteredAfterStartSnapshotsExistingState();
    void duplicatePushAfterLostResponseIsIdempotent();
    void twoReplicasConvergeByHLCTuple();
    void tombstoneBeatsOlderOfflinePut();
    void immutableSnapshotNeverInfersDelete();
    void immutablePausedReplayNeverSynthesizesDelete();
    void immutablePausedReplayRequeuesUnsyncedBaselineFact();
    void remoteImportDoesNotEchoIntoOutbox();
    void futureClockIsRebasedAndRetried();
    void canonicalOlderHlcAppliesByServerSeqAndPreservesPendingOutbox();
    void canonicalPullThenPendingPushLossKeepsServerCanonical();
    void canonicalPullThenPendingPushWinRepullsLocalWinner();
    void legacyNonCanonicalOlderHlcRemainsSuppressed();
    void unknownCanonicalCategoryDoesNotAdvanceCursor();
    void unknownWinningCategoryDoesNotAdvanceCursor();
    void bannedRemotePayloadDoesNotAdvanceCursor();
    void signOutFlushWarnsWhenNetworkUnavailable();
    void signOutFlushSucceedsAfterDrain();
    void accountSwitchUsesSeparateProfileState();
    void attachmentModeStampsPushesWithEnvelopeId();
    void attachmentSnapshotPagesApplyThenLoopStops();
    void attachmentReplayDoesNotInferDeletesForDeleteCapableAdapter();
    void attachmentCursorFreezesAheadThenPullResumesStrictlyAfter();
    void attachmentCursorNeverRegressesWhenAlreadyAhead();
    void attachmentModeRestoresAcrossRestartAndResumesPerPhase();
};

void tst_sync_engine::
hlcNeverGoesBackwards() {
    SyncHybridClock clock(
        QString::fromLatin1(
            kDeviceA));

    const SyncWireHlc first =
        clock.next(1000);
    const SyncWireHlc second =
        clock.next(900);

    QCOMPARE(
        first.physicalMs,
        qint64(1000));
    QCOMPARE(
        second.physicalMs,
        qint64(1000));
    QCOMPARE(
        second.counter,
        quint64(1));

    const SyncWireHlc remote{
        5000,
        7,
        QString::fromLatin1(
            kDeviceB)};
    clock.observe(
        remote,
        800);

    const SyncWireHlc after =
        clock.next(700);
    QCOMPARE(
        after.physicalMs,
        qint64(5000));
    QVERIFY(after.counter > 7);
}

void tst_sync_engine::
trustedLocalOrderingHintsBecomeOrderedHLCs() {
    SyncHybridClock clock(
        QString::fromLatin1(
            kDeviceA));

    const SyncWireHlc oldest =
        clock.nextFromLocalOrder(
            1000,
            9000);
    const SyncWireHlc newer =
        clock.nextFromLocalOrder(
            2000,
            9000);
    const SyncWireHlc sameTimestamp =
        clock.nextFromLocalOrder(
            2000,
            9000);
    const SyncWireHlc legacyFallback =
        clock.nextFromLocalOrder(
            -1,
            9000);

    QCOMPARE(
        oldest.physicalMs,
        qint64(1000));
    QCOMPARE(
        newer.physicalMs,
        qint64(2000));
    QCOMPARE(
        newer.counter,
        quint64(0));
    QCOMPARE(
        sameTimestamp.physicalMs,
        qint64(2000));
    QCOMPARE(
        sameTimestamp.counter,
        quint64(1));
    QCOMPARE(
        legacyFallback.physicalMs,
        qint64(9000));

    QVERIFY(
        syncWireHlcGreater(
            newer,
            oldest));
    QVERIFY(
        syncWireHlcGreater(
            sameTimestamp,
            newer));
    QVERIFY(
        syncWireHlcGreater(
            legacyFallback,
            sameTimestamp));
}

void tst_sync_engine::
rejectedFutureCanRebaseToServiceTime() {
    SyncHybridClock clock(
        QString::fromLatin1(
            kDeviceA));

    const SyncWireHlc future =
        clock.next(5000000);
    QCOMPARE(
        future.physicalMs,
        qint64(5000000));

    clock.observeServiceTime(
        2000000,
        5000000,
        5000000);
    clock.rebaseRejectedFuture(
        5000000);

    const SyncWireHlc rebased =
        clock.next(5000000);
    QCOMPARE(
        rebased.physicalMs,
        qint64(2000000));
    QCOMPARE(
        rebased.counter,
        quint64(1));
}

void tst_sync_engine::
stateStoreRoundTripPreservesCheckpoint() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path =
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "state.json"));

    SyncPersistentState source;
    source.cursor = 42;
    source.hlcPhysicalMs = 1000;
    source.hlcCounter = 3;
    source.serverOffsetMs = -12;

    SyncWireMutation mutation =
        remoteMutation(
            QStringLiteral(
                "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
            QStringLiteral("collection"),
            QStringLiteral(
                "manga/item"),
            QString::fromLatin1(
                kDeviceA),
            1000,
            3,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    QStringLiteral(
                        "fixture")
                }
            });

    source.outbox.append(
        mutation);

    source.mirrors[
        QStringLiteral("collection")]
        .insert(
            QStringLiteral(
                "manga/item"),
            SyncMirrorRecord{
                1,
                mutation.payload});

    source.winners[
        QStringLiteral("collection")]
        .insert(
            QStringLiteral(
                "manga/item"),
            SyncWinner{
                mutation.hlc,
                1,
                SyncWireOperation::Put});

    SyncStateStore store;
    QSignalSpy committed(
        &store,
        &SyncStateStore::
            persistenceCommitted);

    store.saveAsync(
        path,
        source);

    QTRY_COMPARE(
        committed.count(),
        1);
    QVERIFY(store.flush());

    QString error;
    const auto loaded =
        store.load(
            path,
            &error);
    QVERIFY2(
        loaded.has_value(),
        qPrintable(error));

    QCOMPARE(
        loaded->cursor,
        quint64(42));
    QCOMPARE(
        loaded->outbox.size(),
        1);
    QCOMPARE(
        loaded->mirrors
            .value(
                QStringLiteral(
                    "collection"))
            .value(
                QStringLiteral(
                    "manga/item"))
            .payload,
        mutation.payload);
    QCOMPARE(
        loaded->winners
            .value(
                QStringLiteral(
                    "collection"))
            .value(
                QStringLiteral(
                    "manga/item"))
            .hlc.counter,
        quint64(3));
}

void tst_sync_engine::
offlineMutationIsDurableAcrossRestart() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs;
    const ProfilePaths profile =
        accountProfile(&temp);

    {
        Replica first(
            &service,
            profile,
            QString::fromLatin1(
                kDeviceA),
            &now);

        first.adapter.putLocal(
            QStringLiteral(
                "manga/item"),
            QStringLiteral(
                "offline"));

        QTRY_COMPARE(
            first.engine
                .pendingOutboxCount(),
            1);

        QVERIFY(
            first.engine
                .stopPreservingOutbox());
    }

    FixtureSyncTransport transport(
        &service);
    AccountClient client(
        &transport);
    client.setAccessToken(
        QByteArrayLiteral(
            "fixture-access"));

    SyncAdapterRegistry registry;
    SyntheticAdapter adapter;
    QVERIFY(
        registry.registerAdapter(
            &adapter));

    SyncEngine restarted(
        &client,
        &registry,
        [&now]() {
            return now;
        });
    restarted.setAutomaticSchedulingEnabled(
        false);
    restarted.setNetworkEnabled(
        false);

    QString error;
    QVERIFY2(
        restarted.start(
            profile,
            QString::fromLatin1(
                kDeviceA),
            &error),
        qPrintable(error));

    QTRY_COMPARE(
        restarted.pendingOutboxCount(),
        1);
}

void tst_sync_engine::
bearerRejectionPausesForAuthenticationRecoveryWithoutDroppingOutbox() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile = accountProfile(&temp);
    Replica replica(
        &service,
        profile,
        QString::fromLatin1(kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral("manga/item"),
        QStringLiteral("pending"));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 1);

    QSignalSpy authRecoverySpy(
        &replica.engine,
        &SyncEngine::accessTokenRejected);

    replica.client.clearAccessToken();
    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Retrying);
    QCOMPARE(authRecoverySpy.count(), 1);
    QCOMPARE(replica.engine.pendingOutboxCount(), 1);
    QVERIFY(replica.engine.active());

    replica.client.setAccessToken(
        QByteArrayLiteral("fixture-access-refreshed"));
    replica.engine.setNetworkEnabled(true);
    replica.engine.requestImmediateSync();

    QTRY_COMPARE(replica.engine.state(), SyncEngine::State::Idle);
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    QCOMPARE(service.acceptedMutationCount(), 1);
}

void tst_sync_engine::
adapterRegisteredAfterStartSnapshotsExistingState() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs;

    FixtureSyncTransport transport(
        &service);
    AccountClient client(
        &transport);
    client.setAccessToken(
        QByteArrayLiteral(
            "fixture-access"));

    SyncAdapterRegistry registry;
    SyncEngine engine(
        &client,
        &registry,
        [&now]() {
            return now;
        });
    engine.setAutomaticSchedulingEnabled(
        false);
    engine.setNetworkEnabled(
        false);

    QString error;
    QVERIFY2(
        engine.start(
            accountProfile(&temp),
            QString::fromLatin1(
                kDeviceA),
            &error),
        qPrintable(error));

    SyntheticAdapter adapter;
    adapter.seedLocalWithoutSignal(
        QStringLiteral("manga/item"),
        QStringLiteral("preexisting"));

    QVERIFY(
        registry.registerAdapter(
            &adapter));

    QTRY_COMPARE(
        engine.pendingOutboxCount(),
        1);

    QVERIFY(
        engine.stopPreservingOutbox());
}

void tst_sync_engine::
duplicatePushAfterLostResponseIsIdempotent() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs;

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(
            kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("one"));

    QTRY_COMPARE(
        replica.engine
            .pendingOutboxCount(),
        1);

    replica.transport
        .dropNextPushResponseAfterCommit();
    replica.engine.setNetworkEnabled(
        true);

    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Retrying);
    QCOMPARE(
        service.acceptedMutationCount(),
        1);
    QCOMPARE(
        replica.engine
            .pendingOutboxCount(),
        1);

    replica.engine.requestImmediateSync();

    QTRY_COMPARE(
        replica.engine
            .pendingOutboxCount(),
        0);
    QCOMPARE(
        service.acceptedMutationCount(),
        1);
}

void tst_sync_engine::
twoReplicasConvergeByHLCTuple() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    FixtureSyncService service;
    qint64 nowA =
        service.serverTimeMs;
    qint64 nowB =
        service.serverTimeMs;

    Replica a(
        &service,
        accountProfile(&tempA),
        QString::fromLatin1(
            kDeviceA),
        &nowA);
    Replica b(
        &service,
        accountProfile(&tempB),
        QString::fromLatin1(
            kDeviceB),
        &nowB);

    a.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("from-a"));
    b.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("from-b"));

    a.engine.setNetworkEnabled(true);
    b.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);
    QTRY_COMPARE(
        b.engine.pendingOutboxCount(),
        0);

    a.engine.requestImmediateSync();
    b.engine.requestImmediateSync();

    QTRY_COMPARE(
        a.adapter.value(
            QStringLiteral(
                "manga/item")),
        QStringLiteral("from-b"));
    QTRY_COMPARE(
        b.adapter.value(
            QStringLiteral(
                "manga/item")),
        QStringLiteral("from-b"));
}

void tst_sync_engine::
tombstoneBeatsOlderOfflinePut() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    FixtureSyncService service;
    qint64 nowA =
        service.serverTimeMs;
    qint64 nowB =
        service.serverTimeMs;

    Replica a(
        &service,
        accountProfile(&tempA),
        QString::fromLatin1(
            kDeviceA),
        &nowA);
    Replica b(
        &service,
        accountProfile(&tempB),
        QString::fromLatin1(
            kDeviceB),
        &nowB);

    a.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("seed"));
    a.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);

    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();
    QTRY_VERIFY(
        b.adapter.contains(
            QStringLiteral(
                "manga/item")));

    a.engine.setNetworkEnabled(false);
    b.engine.setNetworkEnabled(false);

    nowA += 1000;
    a.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral(
            "stale-edit"));

    nowB += 2000;
    b.adapter.deleteLocal(
        QStringLiteral(
            "manga/item"));

    a.engine.setNetworkEnabled(true);
    b.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);
    QTRY_COMPARE(
        b.engine.pendingOutboxCount(),
        0);

    a.engine.requestImmediateSync();
    b.engine.requestImmediateSync();

    QTRY_VERIFY(
        !a.adapter.contains(
            QStringLiteral(
                "manga/item")));
    QTRY_VERIFY(
        !b.adapter.contains(
            QStringLiteral(
                "manga/item")));
}

void tst_sync_engine::
immutableSnapshotNeverInfersDelete() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    FixtureSyncService service;
    qint64 now = 2100000;
    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now,
        false);

    replica.adapter.putLocal(
        QStringLiteral("immutable/item"),
        QStringLiteral("present"));
    QCOMPARE(replica.engine.pendingOutboxCount(), 1);

    replica.engine.setNetworkEnabled(true);
    replica.engine.requestImmediateSync();
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    const int acceptedBeforeDelete =
        service.acceptedMutationCount();

    replica.engine.setNetworkEnabled(false);
    replica.adapter.deleteLocal(
        QStringLiteral("immutable/item"));

    QCOMPARE(replica.engine.pendingOutboxCount(), 0);
    QCOMPARE(
        service.acceptedMutationCount(),
        acceptedBeforeDelete);
}

void tst_sync_engine::
immutablePausedReplayNeverSynthesizesDelete() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    FixtureSyncService service;
    qint64 now = 2200000;
    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now,
        false);

    replica.adapter.putLocal(
        QStringLiteral("immutable/paused"),
        QStringLiteral("present"));
    replica.engine.setNetworkEnabled(true);
    replica.engine.requestImmediateSync();
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    const int acceptedBeforePause =
        service.acceptedMutationCount();

    replica.engine.setCategoryNetworkEnabled(
        QStringLiteral("collection"),
        false);
    replica.adapter.deleteLocal(
        QStringLiteral("immutable/paused"));
    QVERIFY(!replica.adapter.contains(
        QStringLiteral("immutable/paused")));

    replica.engine.setCategoryNetworkEnabled(
        QStringLiteral("collection"),
        true);
    QTRY_VERIFY(replica.adapter.contains(
        QStringLiteral("immutable/paused")));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    QCOMPARE(
        service.acceptedMutationCount(),
        acceptedBeforePause);
}

void tst_sync_engine::
immutablePausedReplayRequeuesUnsyncedBaselineFact() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    FixtureSyncService service;
    qint64 now = 2250000;
    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now,
        false);

    replica.adapter.putLocal(
        QStringLiteral("immutable/offline"),
        QStringLiteral("local"));
    QCOMPARE(replica.engine.pendingOutboxCount(), 1);

    replica.engine.setCategoryNetworkEnabled(
        QStringLiteral("collection"),
        false);
    QCOMPARE(replica.engine.pendingOutboxCount(), 0);

    replica.engine.setNetworkEnabled(true);
    replica.engine.setCategoryNetworkEnabled(
        QStringLiteral("collection"),
        true);

    QTRY_COMPARE(service.acceptedMutationCount(), 1);
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    QVERIFY(replica.adapter.contains(
        QStringLiteral("immutable/offline")));
}

void tst_sync_engine::
remoteImportDoesNotEchoIntoOutbox() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    FixtureSyncService service;
    qint64 nowA =
        service.serverTimeMs;
    qint64 nowB =
        service.serverTimeMs;

    Replica a(
        &service,
        accountProfile(&tempA),
        QString::fromLatin1(
            kDeviceA),
        &nowA);
    Replica b(
        &service,
        accountProfile(&tempB),
        QString::fromLatin1(
            kDeviceB),
        &nowB);

    b.adapter.setEmitDuringRemoteApply(
        true);

    a.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("remote"));
    a.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);

    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();

    QTRY_COMPARE(
        b.adapter.value(
            QStringLiteral(
                "manga/item")),
        QStringLiteral("remote"));
    QTRY_COMPARE(
        b.engine.pendingOutboxCount(),
        0);
    QCOMPARE(
        service.acceptedMutationCount(),
        1);
}

void tst_sync_engine::
futureClockIsRebasedAndRetried() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs
        + service.maxFutureSkewMs
        + 60000;

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(
            kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("future"));

    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        1);

    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        0);
    QCOMPARE(
        service.acceptedMutationCount(),
        1);
    QCOMPARE(
        service.journal().size(),
        1);
}

void tst_sync_engine::
canonicalOlderHlcAppliesByServerSeqAndPreservesPendingOutbox() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile = accountProfile(&temp);

    Replica replica(
        &service,
        profile,
        QString::fromLatin1(kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral("manga/item"),
        QStringLiteral("local-pending"));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 1);

    SyncStateStore stateStore;
    QString stateError;
    std::optional<SyncPersistentState> before;
    QTRY_VERIFY_WITH_TIMEOUT(
        ((before = stateStore.load(profile.syncStatePath(), &stateError)).has_value()
         && before->outbox.size() == 1),
        5000);
    const SyncWireMutation pendingBefore = before->outbox.constFirst();

    const qint64 canonicalHlc = now - 1000;
    service.appendRemote(
        remoteMutation(
            QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc"),
            QStringLiteral("collection"),
            QStringLiteral("manga/item"),
            QString::fromLatin1(kDeviceB),
            canonicalHlc,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    QStringLiteral("server-canonical")
                }
            }),
        false,
        true);

    replica.transport.setPushOnline(false);
    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Retrying);
    QCOMPARE(
        replica.adapter.value(QStringLiteral("manga/item")),
        QStringLiteral("server-canonical"));
    QCOMPARE(replica.engine.cursor(), quint64(1));
    QCOMPARE(replica.engine.pendingOutboxCount(), 1);

    const auto after =
        stateStore.load(profile.syncStatePath(), &stateError);
    QVERIFY2(after.has_value(), qPrintable(stateError));
    QCOMPARE(after->cursor, quint64(1));
    QCOMPARE(after->outbox.size(), 1);

    const SyncWireMutation pendingAfter = after->outbox.constFirst();
    QCOMPARE(pendingAfter.mutationId, pendingBefore.mutationId);
    QCOMPARE(pendingAfter.hlc.physicalMs, pendingBefore.hlc.physicalMs);
    QCOMPARE(pendingAfter.hlc.counter, pendingBefore.hlc.counter);
    QCOMPARE(pendingAfter.operation, pendingBefore.operation);
    QCOMPARE(pendingAfter.payload, pendingBefore.payload);

    const SyncWinner canonicalWinner =
        after->winners
            .value(QStringLiteral("collection"))
            .value(QStringLiteral("manga/item"));
    QCOMPARE(canonicalWinner.hlc.physicalMs, canonicalHlc);
    QCOMPARE(
        canonicalWinner.hlc.deviceId,
        QString::fromLatin1(kDeviceB));
    QCOMPARE(
        after->mirrors
            .value(QStringLiteral("collection"))
            .value(QStringLiteral("manga/item"))
            .payload,
        QJsonValue(QJsonObject{
            {
                QStringLiteral("value"),
                QStringLiteral("server-canonical")
            }
        }));
}

void tst_sync_engine::
canonicalPullThenPendingPushLossKeepsServerCanonical() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    service.setCanonicalCurrentPullMode(true);
    qint64 now = service.serverTimeMs;

    service.appendRemote(
        remoteMutation(
            QStringLiteral("f1000000-0000-4000-8000-000000000001"),
            QStringLiteral("collection"),
            QStringLiteral("manga/item"),
            QString::fromLatin1(kDeviceB),
            now + 1000,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    QStringLiteral("server-newer")
                }
            }),
        true,
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral("manga/item"),
        QStringLiteral("local-pending"));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 1);

    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(replica.engine.state(), SyncEngine::State::Idle);
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    QCOMPARE(
        replica.adapter.value(QStringLiteral("manga/item")),
        QStringLiteral("server-newer"));
    QCOMPARE(replica.engine.cursor(), quint64(1));
    QCOMPARE(service.acceptedMutationCount(), 1);
    QCOMPARE(service.journal().size(), 2);
    QVERIFY(!service.journal().constLast().won);
}

void tst_sync_engine::
canonicalPullThenPendingPushWinRepullsLocalWinner() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    service.setCanonicalCurrentPullMode(true);
    qint64 now = service.serverTimeMs;

    service.appendRemote(
        remoteMutation(
            QStringLiteral("f2000000-0000-4000-8000-000000000002"),
            QStringLiteral("collection"),
            QStringLiteral("manga/item"),
            QString::fromLatin1(kDeviceB),
            now - 1000,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    QStringLiteral("server-older")
                }
            }),
        true,
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral("manga/item"),
        QStringLiteral("local-winner"));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 1);

    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    QTRY_COMPARE(replica.engine.cursor(), quint64(2));
    QTRY_COMPARE(replica.engine.state(), SyncEngine::State::Idle);
    QCOMPARE(
        replica.adapter.value(QStringLiteral("manga/item")),
        QStringLiteral("local-winner"));
    QCOMPARE(service.acceptedMutationCount(), 1);
    QCOMPARE(service.journal().size(), 2);
    QVERIFY(service.journal().constLast().won);
    QCOMPARE(replica.adapter.remoteApplyCount(), 2);
}

void tst_sync_engine::
legacyNonCanonicalOlderHlcRemainsSuppressed() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral("manga/item"),
        QStringLiteral("local-newer"));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 1);

    service.appendRemote(
        remoteMutation(
            QStringLiteral("dddddddd-dddd-4ddd-8ddd-dddddddddddd"),
            QStringLiteral("collection"),
            QStringLiteral("manga/item"),
            QString::fromLatin1(kDeviceB),
            now - 1000,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    QStringLiteral("legacy-older")
                }
            }),
        true,
        false);

    replica.transport.setPushOnline(false);
    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Retrying);
    QCOMPARE(
        replica.adapter.value(QStringLiteral("manga/item")),
        QStringLiteral("local-newer"));
    QCOMPARE(replica.engine.cursor(), quint64(1));
    QCOMPARE(replica.engine.pendingOutboxCount(), 1);
}

void tst_sync_engine::
unknownCanonicalCategoryDoesNotAdvanceCursor() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;

    service.appendRemote(
        remoteMutation(
            QStringLiteral("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"),
            QStringLiteral("extension_roster"),
            QStringLiteral("extension/canonical-item"),
            QString::fromLatin1(kDeviceB),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    QStringLiteral("remote")
                }
            }),
        false,
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Blocked);
    QCOMPARE(replica.engine.cursor(), quint64(0));
    QCOMPARE(
        replica.engine.lastErrorCode(),
        QStringLiteral("adapter_not_registered"));
}

void tst_sync_engine::
unknownWinningCategoryDoesNotAdvanceCursor() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs;

    service.appendRemote(
        remoteMutation(
            QStringLiteral(
                "cccccccc-cccc-4ccc-8ccc-cccccccccccc"),
            QStringLiteral(
                "extension_roster"),
            QStringLiteral(
                "extension/item"),
            QString::fromLatin1(
                kDeviceB),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("value"),
                    QStringLiteral("remote")
                }
            }),
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(
            kDeviceA),
        &now);

    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Blocked);
    QCOMPARE(
        replica.engine.cursor(),
        quint64(0));
    QCOMPARE(
        replica.engine.lastErrorCode(),
        QStringLiteral(
            "adapter_not_registered"));
}

void tst_sync_engine::
bannedRemotePayloadDoesNotAdvanceCursor() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs;

    service.appendRemote(
        remoteMutation(
            QStringLiteral(
                "dddddddd-dddd-4ddd-8ddd-dddddddddddd"),
            QStringLiteral("collection"),
            QStringLiteral(
                "manga/item"),
            QString::fromLatin1(
                kDeviceB),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("path"),
                    QStringLiteral(
                        "C:\\Private\\book.cbz")
                }
            }),
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(
            kDeviceA),
        &now);

    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Blocked);
    QCOMPARE(
        replica.engine.cursor(),
        quint64(0));
    QCOMPARE(
        replica.adapter.remoteApplyCount(),
        0);
}

void tst_sync_engine::
signOutFlushWarnsWhenNetworkUnavailable() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs;

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(
            kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("pending"));

    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        1);

    QSignalSpy spy(
        &replica.engine,
        &SyncEngine::
            signOutFlushFinished);

    replica.engine.beginSignOutFlush();

    QTRY_COMPARE(
        spy.count(),
        1);
    const QList<QVariant> args =
        spy.takeFirst();

    QCOMPARE(
        args.at(0).toBool(),
        false);
    QCOMPARE(
        args.at(1).toString(),
        QStringLiteral("offline"));
    QCOMPARE(
        replica.engine.pendingOutboxCount(),
        1);
}

void tst_sync_engine::
signOutFlushSucceedsAfterDrain() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs;

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(
            kDeviceA),
        &now);

    replica.adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("pending"));

    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        1);

    QSignalSpy spy(
        &replica.engine,
        &SyncEngine::
            signOutFlushFinished);

    replica.engine.setNetworkEnabled(true);
    replica.engine.beginSignOutFlush();

    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        0);
    QTRY_COMPARE(
        spy.count(),
        1);

    QCOMPARE(
        spy.takeFirst()
            .at(0)
            .toBool(),
        true);
}

void tst_sync_engine::
accountSwitchUsesSeparateProfileState() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now =
        service.serverTimeMs;

    const ProfilePaths profileA =
        accountProfile(
            &temp,
            QString::fromLatin1(
                kAccountA));
    const ProfilePaths profileB =
        accountProfile(
            &temp,
            QString::fromLatin1(
                kAccountB));

    FixtureSyncTransport transport(
        &service);
    AccountClient client(
        &transport);
    client.setAccessToken(
        QByteArrayLiteral(
            "fixture-access"));

    SyncAdapterRegistry registry;
    SyntheticAdapter adapter;
    QVERIFY(
        registry.registerAdapter(
            &adapter));

    SyncEngine engine(
        &client,
        &registry,
        [&now]() {
            return now;
        });
    engine.setAutomaticSchedulingEnabled(
        false);
    engine.setNetworkEnabled(
        false);

    QString error;
    QVERIFY2(
        engine.start(
            profileA,
            QString::fromLatin1(
                kDeviceA),
            &error),
        qPrintable(error));

    adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral(
            "account-a"));

    QTRY_COMPARE(
        engine.pendingOutboxCount(),
        1);
    QVERIFY(
        engine.stopPreservingOutbox());

    // The synthetic adapter is a test object, so clear its visible state before
    // opening B. Product account switching destroys/unregisters A adapters.
    adapter.deleteLocal(
        QStringLiteral(
            "manga/item"));

    QVERIFY2(
        engine.start(
            profileB,
            QString::fromLatin1(
                kDeviceA),
            &error),
        qPrintable(error));

    // B's fresh snapshot deletion is relative only to B's empty mirror.
    QTRY_COMPARE(
        engine.pendingOutboxCount(),
        0);
    QVERIFY(
        engine.stopPreservingOutbox());

    SyncStateStore store;
    const auto aState =
        store.load(
            profileA.syncStatePath(),
            &error);
    QVERIFY2(
        aState.has_value(),
        qPrintable(error));
    QCOMPARE(
        aState->outbox.size(),
        1);

    const auto bState =
        store.load(
            profileB.syncStatePath(),
            &error);
    QVERIFY2(
        bState.has_value(),
        qPrintable(error));
    QCOMPARE(
        bState->outbox.size(),
        0);
}

void tst_sync_engine::
attachmentModeStampsPushesWithEnvelopeId() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    // Ordinary (mode-inactive) pushes carry no envelope attachment id.
    replica.adapter.putLocal(
        QStringLiteral("manga/item"),
        QStringLiteral("ordinary"));
    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        1);

    replica.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        0);
    QCOMPARE(
        service.pushAttachmentIds().size(),
        1);
    QVERIFY(service.pushAttachmentIds()
                .constFirst()
                .isEmpty());

    replica.engine.setNetworkEnabled(false);

    QString error;
    QVERIFY(!replica.engine.beginAttachmentMode(
        QStringLiteral("not-a-uuid"),
        &error));
    QVERIFY(
        !replica.engine
             .attachmentModeActive());
    QVERIFY(
        !replica.engine.endAttachmentMode(
            &error));

    const QString attachmentId =
        QStringLiteral(
            "36363636-3636-4363-8363-"
            "363636363636");
    QVERIFY2(
        replica.engine.beginAttachmentMode(
            attachmentId,
            &error),
        qPrintable(error));
    QVERIFY(
        replica.engine
            .attachmentModeActive());
    QCOMPARE(
        replica.engine.attachmentId(),
        attachmentId);
    // One attachment mode at a time.
    QVERIFY(
        !replica.engine.beginAttachmentMode(
            attachmentId,
            &error));

    // The bootstrap snapshot must complete before the attached push.
    FixtureSnapshotPage finalPage;
    finalPage.cursor = 1;
    finalPage.hasMore = false;
    service.setSnapshotPages(
        {finalPage});

    replica.adapter.putLocal(
        QStringLiteral("manga/item2"),
        QStringLiteral("attached"));
    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        1);

    replica.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        0);

    QCOMPARE(
        service.snapshotRequestCount(),
        1);
    QCOMPARE(
        service.pushAttachmentIds().size(),
        2);
    QCOMPARE(
        service.pushAttachmentIds()
            .constLast(),
        attachmentId);
    QCOMPARE(
        service.journal()
            .constLast()
            .mutation.payload
            .toObject()
            .value(QStringLiteral("value"))
            .toString(),
        QStringLiteral("attached"));

    // Exiting the mode returns pushes to the ordinary envelope.
    QVERIFY(
        replica.engine.endAttachmentMode(
            &error));
    QVERIFY(
        !replica.engine
             .attachmentModeActive());
    QVERIFY(replica.engine.attachmentId()
                .isEmpty());

    replica.adapter.putLocal(
        QStringLiteral("manga/item3"),
        QStringLiteral(
            "ordinary-again"));
    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        1);
    replica.engine.requestImmediateSync();
    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        0);
    QCOMPARE(
        service.pushAttachmentIds().size(),
        3);
    QVERIFY(service.pushAttachmentIds()
                .constLast()
                .isEmpty());
}

void tst_sync_engine::
attachmentSnapshotPagesApplyThenLoopStops() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    const QString attachmentId =
        QStringLiteral(
            "35353535-3535-4353-8353-"
            "353535353535");

    auto pageEntry =
        [&now](quint64 serverSeq,
               const QString &mutationSuffix,
               const QString &recordKey,
               const QString &value) {
            return canonicalSnapshotEntry(
                serverSeq,
                remoteMutation(
                    QStringLiteral(
                        "f3100000-0000-"
                        "4000-8000-") +
                        mutationSuffix,
                    QStringLiteral(
                        "collection"),
                    recordKey,
                    QString::fromLatin1(
                        kDeviceB),
                    now,
                    0,
                    SyncWireOperation::Put,
                    QJsonObject{
                        {
                            QStringLiteral(
                                "value"),
                            value
                        }
                    }));
        };

    FixtureSnapshotPage page1;
    page1.cursor = 4;
    page1.hasMore = true;
    page1.nextPageToken =
        QStringLiteral("page-2");
    page1.entries.append(
        pageEntry(
            1,
            QStringLiteral(
                "000000000001"),
            QStringLiteral(
                "manga/a"),
            QStringLiteral("one")));

    FixtureSnapshotPage page2;
    page2.cursor = 4;
    page2.hasMore = true;
    page2.nextPageToken =
        QStringLiteral("page-3");
    page2.entries.append(
        pageEntry(
            2,
            QStringLiteral(
                "000000000002"),
            QStringLiteral(
                "manga/b"),
            QStringLiteral("two")));
    page2.entries.append(
        pageEntry(
            3,
            QStringLiteral(
                "000000000003"),
            QStringLiteral(
                "manga/c"),
            QStringLiteral("three")));

    FixtureSnapshotPage page3;
    page3.cursor = 4;
    page3.hasMore = false;
    page3.entries.append(
        pageEntry(
            4,
            QStringLiteral(
                "000000000004"),
            QStringLiteral(
                "manga/d"),
            QStringLiteral("four")));

    service.setSnapshotPages(
        {page1, page2, page3});

    QVERIFY(replica.engine.beginAttachmentMode(
        attachmentId));
    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.adapter.value(
            QStringLiteral("manga/a")),
        QStringLiteral("one"));
    QTRY_COMPARE(
        replica.adapter.value(
            QStringLiteral("manga/b")),
        QStringLiteral("two"));
    QTRY_COMPARE(
        replica.adapter.value(
            QStringLiteral("manga/c")),
        QStringLiteral("three"));
    QTRY_COMPARE(
        replica.adapter.value(
            QStringLiteral("manga/d")),
        QStringLiteral("four"));

    QCOMPARE(
        service.snapshotRequestCount(),
        3);
    QCOMPARE(
        service.snapshotRequestTokens(),
        QStringList()
            << QString()
            << QStringLiteral("page-2")
            << QStringLiteral("page-3"));
    QCOMPARE(
        replica.engine.cursor(),
        quint64(4));

    // The loop stops: nothing else fires while the engine idles, and an
    // immediate sync issues an ordinary pull rather than a new snapshot.
    QTest::qWait(50);
    QCOMPARE(
        service.snapshotRequestCount(),
        3);
    replica.engine.requestImmediateSync();
    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Idle);
    QCOMPARE(
        service.snapshotRequestCount(),
        3);
    QVERIFY(
        replica.engine
            .attachmentModeActive());
}

void tst_sync_engine::
attachmentReplayDoesNotInferDeletesForDeleteCapableAdapter() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    // The account already holds manga/a; this device synced it
    // ordinarily, so the engine mirror contains it.
    replica.adapter.putLocal(
        QStringLiteral("manga/a"),
        QStringLiteral(
            "account-canonical"));
    replica.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        0);
    replica.engine.setNetworkEnabled(false);
    const int ordinaryAccepted =
        service.acceptedMutationCount();

    // The legacy local profile being attached lacks the record.
    replica.adapter.removeLocalWithoutSignal(
        QStringLiteral("manga/a"));
    QVERIFY(!replica.adapter.contains(
        QStringLiteral("manga/a")));

    const QString attachmentId =
        QStringLiteral(
            "34343434-3434-4343-8343-"
            "343434343434");

    auto snapshotMutation =
        [&now](const QString &mutationId,
               const QString &recordKey,
               const QString &value) {
            return remoteMutation(
                mutationId,
                QStringLiteral(
                    "collection"),
                recordKey,
                QString::fromLatin1(
                    kDeviceB),
                now,
                0,
                SyncWireOperation::Put,
                QJsonObject{
                    {
                        QStringLiteral(
                            "value"),
                        value
                    }
                });
        };

    FixtureSnapshotPage page1;
    page1.cursor = 3;
    page1.hasMore = true;
    page1.nextPageToken =
        QStringLiteral("replay-2");
    page1.entries.append(
        canonicalSnapshotEntry(
            2,
            snapshotMutation(
                QStringLiteral(
                    "f3200000-0000-4000-"
                    "8000-000000000001"),
                QStringLiteral(
                    "manga/b"),
                QStringLiteral(
                    "from-snapshot"))));

    FixtureSnapshotPage page2;
    page2.cursor = 3;
    page2.hasMore = false;
    page2.entries.append(
        canonicalSnapshotEntry(
            3,
            snapshotMutation(
                QStringLiteral(
                    "f3200000-0000-4000-"
                    "8000-000000000002"),
                QStringLiteral(
                    "manga/a"),
                QStringLiteral(
                    "account-canonical"))));

    service.setSnapshotPages(
        {page1, page2});
    // The page-2 resume fails once so the engine parks deterministically
    // mid-replay with page 1 applied and the bootstrap unfinished.
    service
        .setSnapshotNetworkFailuresAfterPages(
            1,
            1);

    QVERIFY(replica.engine.beginAttachmentMode(
        attachmentId));
    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        replica.adapter.value(
            QStringLiteral("manga/b")),
        QStringLiteral(
            "from-snapshot"));
    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Retrying);

    // A genuine local edit lands while the snapshot replay is still in
    // progress: the mirror holds manga/a but the local profile lacks it.
    replica.adapter.putLocal(
        QStringLiteral("manga/c"),
        QStringLiteral("local-edit"));
    replica.engine.requestImmediateSync();

    QTRY_COMPARE(
        replica.engine.pendingOutboxCount(),
        0);
    // Union/merge: the snapshot record missing locally is restored, not
    // deleted from the account.
    QTRY_VERIFY(replica.adapter.contains(
        QStringLiteral("manga/a")));
    QCOMPARE(
        replica.engine.cursor(),
        quint64(3));
    QCOMPARE(
        service.snapshotRequestTokens(),
        QStringList()
            << QString()
            << QStringLiteral("replay-2")
            << QStringLiteral("replay-2"));

    for (const FixtureJournalEntry &entry :
         service.journal()) {
        QVERIFY2(
            !(entry.mutation.operation
                  == SyncWireOperation::
                      Delete
              && entry.mutation.recordKey
                  == QLatin1String(
                      "manga/a")),
            "attachment replay must never "
            "infer a delete for a record "
            "missing locally");
    }
    QCOMPARE(
        service.acceptedMutationCount(),
        ordinaryAccepted + 1);
    QCOMPARE(
        service.pushAttachmentIds()
            .constLast(),
        attachmentId);
}

void tst_sync_engine::
attachmentCursorFreezesAheadThenPullResumesStrictlyAfter() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;

    auto journalMutation =
        [&now](const QString &mutationId,
               const QString &recordKey,
               const QString &value) {
            return remoteMutation(
                mutationId,
                QStringLiteral(
                    "collection"),
                recordKey,
                QString::fromLatin1(
                    kDeviceB),
                now,
                0,
                SyncWireOperation::Put,
                QJsonObject{
                    {
                        QStringLiteral(
                            "value"),
                        value
                    }
                });
        };

    const SyncWireMutation baseline1 =
        journalMutation(
            QStringLiteral(
                "f3300000-0000-4000-8000-"
                "000000000001"),
            QStringLiteral(
                "manga/old1"),
            QStringLiteral("one"));
    const SyncWireMutation baseline2 =
        journalMutation(
            QStringLiteral(
                "f3300000-0000-4000-8000-"
                "000000000002"),
            QStringLiteral(
                "manga/old2"),
            QStringLiteral("two"));
    const SyncWireMutation afterFreeze =
        journalMutation(
            QStringLiteral(
                "f3300000-0000-4000-8000-"
                "000000000003"),
            QStringLiteral(
                "manga/after"),
            QStringLiteral(
                "after-frozen"));

    service.appendRemote(
        baseline1,
        true,
        true);
    service.appendRemote(
        baseline2,
        true,
        true);
    service.appendRemote(
        afterFreeze,
        true,
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    FixtureSnapshotPage page1;
    page1.cursor = 2;
    page1.hasMore = true;
    page1.nextPageToken =
        QStringLiteral("cursor-2");
    page1.entries.append(
        canonicalSnapshotEntry(
            1,
            baseline1));

    FixtureSnapshotPage page2;
    page2.cursor = 2;
    page2.hasMore = false;
    page2.entries.append(
        canonicalSnapshotEntry(
            2,
            baseline2));

    service.setSnapshotPages(
        {page1, page2});

    QVERIFY(replica.engine.beginAttachmentMode(
        QStringLiteral(
            "33333333-3333-4333-8333-"
            "333333333333")));
    replica.engine.setNetworkEnabled(true);

    // The post-freeze journal row arrives via the ordinary pull that
    // resumes strictly after the frozen cursor.
    QTRY_COMPARE(
        replica.adapter.value(
            QStringLiteral(
                "manga/after")),
        QStringLiteral(
            "after-frozen"));

    QCOMPARE(
        replica.adapter.value(
            QStringLiteral(
                "manga/old1")),
        QStringLiteral("one"));
    QCOMPARE(
        replica.adapter.value(
            QStringLiteral(
                "manga/old2")),
        QStringLiteral("two"));
    QCOMPARE(
        replica.engine.cursor(),
        quint64(3));
    QCOMPARE(
        service.snapshotRequestCount(),
        2);
    QCOMPARE(
        service.pullRequestAfters(),
        QStringList()
            << QStringLiteral("2"));
    // Each record applied exactly once: no skips, no duplicates.
    QCOMPARE(
        replica.adapter
            .remoteApplyCount(),
        3);
}

void tst_sync_engine::
attachmentCursorNeverRegressesWhenAlreadyAhead() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;

    auto journalMutation =
        [&now](const QString &mutationId,
               const QString &recordKey) {
            return remoteMutation(
                mutationId,
                QStringLiteral(
                    "collection"),
                recordKey,
                QString::fromLatin1(
                    kDeviceB),
                now,
                0,
                SyncWireOperation::Put,
                QJsonObject{
                    {
                        QStringLiteral(
                            "value"),
                        QStringLiteral(
                            "journal")
                    }
                });
        };

    service.appendRemote(
        journalMutation(
            QStringLiteral(
                "f3400000-0000-4000-8000-"
                "000000000001"),
            QStringLiteral(
                "manga/j1")),
        true,
        true);
    service.appendRemote(
        journalMutation(
            QStringLiteral(
                "f3400000-0000-4000-8000-"
                "000000000002"),
            QStringLiteral(
                "manga/j2")),
        true,
        true);
    service.appendRemote(
        journalMutation(
            QStringLiteral(
                "f3400000-0000-4000-8000-"
                "000000000003"),
            QStringLiteral(
                "manga/j3")),
        true,
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    // Ordinary sync first: cursor advances past the frozen baseline.
    replica.engine.setNetworkEnabled(true);
    QTRY_VERIFY(replica.adapter.contains(
        QStringLiteral("manga/j3")));
    QCOMPARE(
        replica.engine.cursor(),
        quint64(3));
    QCOMPARE(
        replica.adapter
            .remoteApplyCount(),
        3);
    replica.engine.setNetworkEnabled(false);

    // The frozen snapshot cursor sits behind the engine cursor.
    FixtureSnapshotPage page;
    page.cursor = 2;
    page.hasMore = false;
    page.entries.append(
        canonicalSnapshotEntry(
            1,
            journalMutation(
                QStringLiteral(
                    "f3400000-0000-4000-"
                    "8000-000000000001"),
                QStringLiteral(
                    "manga/j1"))));
    page.entries.append(
        canonicalSnapshotEntry(
            2,
            journalMutation(
                QStringLiteral(
                    "f3400000-0000-4000-"
                    "8000-000000000002"),
                QStringLiteral(
                    "manga/j2"))));
    service.setSnapshotPages({page});

    QVERIFY(replica.engine.beginAttachmentMode(
        QStringLiteral(
            "32323232-3232-4323-8323-"
            "323232323232")));
    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(
        service.snapshotRequestCount(),
        1);
    QTRY_COMPARE(
        service.pullRequestAfters().size(),
        2);
    QCOMPARE(
        service.pullRequestAfters()
            .constFirst(),
        QStringLiteral("0"));
    QCOMPARE(
        service.pullRequestAfters()
            .constLast(),
        QStringLiteral("3"));

    // The cursor never regresses to the frozen baseline, and entries
    // the engine already pulled are neither re-applied nor duplicated.
    QCOMPARE(
        replica.engine.cursor(),
        quint64(3));
    QCOMPARE(
        replica.adapter
            .remoteApplyCount(),
        3);
}

void tst_sync_engine::
attachmentModeRestoresAcrossRestartAndResumesPerPhase() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile =
        accountProfile(&temp);
    const QString attachmentId =
        QStringLiteral(
            "37373737-3737-4373-8373-"
            "373737373737");

    auto snapshotMutation =
        [&now](const QString &mutationId,
               const QString &recordKey,
               const QString &value) {
            return remoteMutation(
                mutationId,
                QStringLiteral(
                    "collection"),
                recordKey,
                QString::fromLatin1(
                    kDeviceB),
                now,
                0,
                SyncWireOperation::Put,
                QJsonObject{
                    {
                        QStringLiteral(
                            "value"),
                        value
                    }
                });
        };

    FixtureSnapshotPage page1;
    page1.cursor = 2;
    page1.hasMore = true;
    page1.nextPageToken =
        QStringLiteral("resume-2");
    page1.entries.append(
        canonicalSnapshotEntry(
            1,
            snapshotMutation(
                QStringLiteral(
                    "f3500000-0000-4000-"
                    "8000-000000000001"),
                QStringLiteral(
                    "manga/p1"),
                QStringLiteral(
                    "page-one"))));

    FixtureSnapshotPage page2;
    page2.cursor = 2;
    page2.hasMore = false;
    page2.entries.append(
        canonicalSnapshotEntry(
            2,
            snapshotMutation(
                QStringLiteral(
                    "f3500000-0000-4000-"
                    "8000-000000000002"),
                QStringLiteral(
                    "manga/p2"),
                QStringLiteral(
                    "page-two"))));

    service.setSnapshotPages({page1, page2});

    // The resume request after page 1 fails once, parking the engine
    // mid-pagination with a durable continuation token.
    service
        .setSnapshotNetworkFailuresAfterPages(
            1,
            1);

    SyntheticAdapter adapter;

    {
        FixtureSyncTransport transport(
            &service);
        AccountClient client(&transport);
        client.setAccessToken(
            QByteArrayLiteral(
                "fixture-access"));

        SyncAdapterRegistry registry;
        QVERIFY(
            registry.registerAdapter(
                &adapter));

        SyncEngine first(
            &client,
            &registry,
            [&now]() {
                return now;
            });
        first.setAutomaticSchedulingEnabled(
            false);
        first.setNetworkEnabled(false);

        QString error;
        QVERIFY2(
            first.start(
                profile,
                QString::fromLatin1(
                    kDeviceA),
                &error),
            qPrintable(error));
        QVERIFY(
            first.beginAttachmentMode(
                attachmentId,
                &error));
        first.setNetworkEnabled(true);

        QTRY_COMPARE(
            adapter.value(
                QStringLiteral(
                    "manga/p1")),
            QStringLiteral(
                "page-one"));
        QTRY_COMPARE(
            first.state(),
            SyncEngine::State::Retrying);
        QVERIFY(
            first.stopPreservingOutbox());
    }

    {
        SyncStateStore store;
        QString error;
        const auto midState =
            store.load(
                profile.syncStatePath(),
                &error);
        QVERIFY2(
            midState.has_value(),
            qPrintable(error));
        QVERIFY(
            midState
                ->attachmentModeActive);
        QCOMPARE(
            midState->attachmentId,
            attachmentId);
        QVERIFY(
            !midState
                 ->attachmentSnapshotDone);
        QCOMPARE(
            midState
                ->attachmentSnapshotNextPageToken,
            QStringLiteral("resume-2"));
    }

    // Restart 1: the restored mode resumes the snapshot from the
    // durable token instead of restarting the bootstrap.
    {
        FixtureSyncTransport transport(
            &service);
        AccountClient client(&transport);
        client.setAccessToken(
            QByteArrayLiteral(
                "fixture-access"));

        SyncAdapterRegistry registry;
        QVERIFY(
            registry.registerAdapter(
                &adapter));

        SyncEngine second(
            &client,
            &registry,
            [&now]() {
                return now;
            });
        second.setAutomaticSchedulingEnabled(
            false);
        second.setNetworkEnabled(false);

        QString error;
        QVERIFY2(
            second.start(
                profile,
                QString::fromLatin1(
                    kDeviceA),
                &error),
            qPrintable(error));
        QVERIFY(
            second.attachmentModeActive());
        QCOMPARE(
            second.attachmentId(),
            attachmentId);

        second.setNetworkEnabled(true);

        QTRY_COMPARE(
            adapter.value(
                QStringLiteral(
                    "manga/p2")),
            QStringLiteral(
                "page-two"));
        QCOMPARE(
            second.cursor(),
            quint64(2));
        QCOMPARE(
            service.snapshotRequestTokens()
                .count(QString()),
            1);
        QCOMPARE(
            service.snapshotRequestTokens()
                .constLast(),
            QStringLiteral("resume-2"));

        // Park the engine with the snapshot complete but a pending
        // attached push still in the outbox.
        second.setNetworkEnabled(false);
        adapter.putLocal(
            QStringLiteral("manga/p3"),
            QStringLiteral(
                "attached-edit"));
        QTRY_COMPARE(
            second.pendingOutboxCount(),
            1);
        QVERIFY(
            second.stopPreservingOutbox());
    }

    {
        SyncStateStore store;
        QString error;
        const auto pushPhase =
            store.load(
                profile.syncStatePath(),
                &error);
        QVERIFY2(
            pushPhase.has_value(),
            qPrintable(error));
        QVERIFY(
            pushPhase
                ->attachmentModeActive);
        QCOMPARE(
            pushPhase->attachmentId,
            attachmentId);
        QVERIFY(
            pushPhase
                ->attachmentSnapshotDone);
        QVERIFY(pushPhase
                    ->attachmentSnapshotNextPageToken
                    .isEmpty());
        QCOMPARE(
            pushPhase->outbox.size(),
            1);
    }

    // Restart 2: the durable phase says the bootstrap is done, so only
    // the attached push remains to drain.
    {
        FixtureSyncTransport transport(
            &service);
        AccountClient client(&transport);
        client.setAccessToken(
            QByteArrayLiteral(
                "fixture-access"));

        SyncAdapterRegistry registry;
        QVERIFY(
            registry.registerAdapter(
                &adapter));

        SyncEngine third(
            &client,
            &registry,
            [&now]() {
                return now;
            });
        third.setAutomaticSchedulingEnabled(
            false);
        third.setNetworkEnabled(false);

        QString error;
        QVERIFY2(
            third.start(
                profile,
                QString::fromLatin1(
                    kDeviceA),
                &error),
            qPrintable(error));
        QVERIFY(
            third.attachmentModeActive());

        third.setNetworkEnabled(true);
        QTRY_COMPARE(
            third.pendingOutboxCount(),
            0);

        QCOMPARE(
            service.pushAttachmentIds(),
            QStringList()
                << attachmentId);
        QCOMPARE(
            service.snapshotRequestTokens()
                .count(QString()),
            1);

        QVERIFY(
            third.endAttachmentMode(
                &error));
        QVERIFY(
            !third.attachmentModeActive());
    }
}

QTEST_MAIN(tst_sync_engine)
#include "tst_sync_engine.moc"
