// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "CollectionStore.h"
#include "ProgressStore.h"

#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/CollectionSyncAdapter.h"
#include "account/CoreStateSyncProjection.h"
#include "account/HistoryStore.h"
#include "account/ProfilePaths.h"
#include "account/ProgressSyncAdapter.h"
#include "account/WatchStateSyncAdapter.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
constexpr auto kAccount =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceA =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kDeviceB =
    "22222222-2222-4222-8222-222222222222";

struct JournalEntry {
    quint64 sequence = 0;
    bool won = false;
    SyncWireMutation mutation;
};

struct Ack {
    quint64 sequence = 0;
    bool won = false;
};

QString identity(
    const SyncWireMutation &mutation) {
    return mutation.category
        + QChar(0x1f)
        + mutation.recordKey;
}

class CoreFixtureService {
public:
    qint64 serverTimeMs =
        QDateTime::
            currentMSecsSinceEpoch();

    int acceptedMutationCount() const {
        return m_acks.size();
    }

    AccountTransportReply push(
        const QJsonArray &mutations) {
        AccountTransportReply reply;
        reply.statusCode = 200;

        QJsonArray results;
        for (const QJsonValue &value :
             mutations) {
            const auto mutation =
                value.isObject()
                ? syncWireMutationFromJson(
                      value.toObject())
                : std::nullopt;

            if (!mutation.has_value()) {
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

            const auto duplicate =
                m_acks.constFind(
                    mutation->mutationId);
            if (duplicate
                != m_acks.constEnd()) {
                QJsonObject result;
                result.insert(
                    QStringLiteral(
                        "mutation_id"),
                    mutation->mutationId);
                result.insert(
                    QStringLiteral(
                        "accepted"),
                    true);
                result.insert(
                    QStringLiteral(
                        "server_seq"),
                    QString::number(
                        duplicate->sequence));
                result.insert(
                    QStringLiteral("won"),
                    duplicate->won);
                results.append(result);
                continue;
            }

            const QString recordIdentity =
                identity(*mutation);
            const auto current =
                m_current.constFind(
                    recordIdentity);
            const bool won =
                current
                    == m_current.constEnd()
                || syncWireHlcGreater(
                    mutation->hlc,
                    current->hlc);

            JournalEntry entry;
            entry.sequence =
                m_nextSequence++;
            entry.won = won;
            entry.mutation =
                *mutation;
            m_journal.append(entry);

            if (won) {
                m_current.insert(
                    recordIdentity,
                    *mutation);
            }

            m_acks.insert(
                mutation->mutationId,
                Ack{
                    entry.sequence,
                    won});

            QJsonObject result;
            result.insert(
                QStringLiteral(
                    "mutation_id"),
                mutation->mutationId);
            result.insert(
                QStringLiteral(
                    "accepted"),
                true);
            result.insert(
                QStringLiteral(
                    "server_seq"),
                QString::number(
                    entry.sequence));
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
        for (const JournalEntry &entry :
             m_journal) {
            if (entry.sequence <= after)
                continue;

            QJsonObject object;
            object.insert(
                QStringLiteral(
                    "server_seq"),
                QString::number(
                    entry.sequence));
            object.insert(
                QStringLiteral("won"),
                entry.won);
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
            false);
        return reply;
    }

private:
    quint64 m_nextSequence = 1;
    QList<JournalEntry> m_journal;
    QHash<QString, Ack> m_acks;
    QHash<QString, SyncWireMutation>
        m_current;
};

class CoreFixtureTransport final
    : public AccountTransport {
    Q_OBJECT

public:
    explicit CoreFixtureTransport(
        CoreFixtureService *service,
        QObject *parent = nullptr)
        : AccountTransport(parent),
          m_service(service) {}

    void setOnline(bool online) {
        m_online = online;
    }

    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override {
        AccountTransportReply reply;

        if (!m_online) {
            reply.networkError = true;
            reply.errorCode =
                QStringLiteral("offline");
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
            emit finished(
                requestId,
                m_service->push(
                    request.body
                        .value(
                            QStringLiteral(
                                "mutations"))
                        .toArray()));
            return;
        }

        if (request.method
                == QByteArrayLiteral("GET")
            && request.path.startsWith(
                QStringLiteral(
                    "/v1/sync/pull?after="))) {
            bool ok = false;
            const quint64 cursor =
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
                emit finished(
                    requestId,
                    reply);
                return;
            }

            emit finished(
                requestId,
                m_service->pull(cursor));
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
    CoreFixtureService *m_service =
        nullptr;
    bool m_online = true;
};

struct CoreReplica {
    ProfilePaths profile;
    CollectionStore collection;
    ProgressStore progress;
    CoreFixtureTransport transport;
    AccountClient client;
    SyncAdapterRegistry registry;
    CollectionSyncAdapter collectionAdapter;
    ProgressSyncAdapter progressAdapter;
    SyncEngine engine;

    CoreReplica(
        CoreFixtureService *service,
        const ProfilePaths &profileValue,
        const QString &deviceId)
        : profile(profileValue),
          collection(
              profile.collectionIniPath()),
          progress(
              profile.progressIniPath()),
          transport(service),
          client(&transport),
          collectionAdapter(
              &collection),
          progressAdapter(
              &progress,
              nullptr,
              1),
          engine(
              &client,
              &registry) {
        QDir().mkpath(
            profile.profileRoot());

        client.setAccessToken(
            QByteArrayLiteral(
                "fixture-access"));

        if (!registry.registerAdapter(
                &collectionAdapter)) {
            qFatal(
                "collection adapter registration failed");
        }

        if (!registry.registerAdapter(
                &progressAdapter)) {
            qFatal(
                "progress adapter registration failed");
        }

        engine.setAutomaticSchedulingEnabled(
            false);
        engine.setNetworkEnabled(false);

        QString error;
        if (!engine.start(
                profile,
                deviceId,
                &error)) {
            qFatal(
                "core replica engine start failed");
        }
    }
};

ProfilePaths makeProfile(
    QTemporaryDir *temp) {
    const auto profile =
        ProfilePaths::account(
            QString::fromLatin1(
                kAccount),
            temp->path());

    if (!profile.has_value())
        qFatal(
            "fixture profile invalid");

    QDir().mkpath(
        profile->profileRoot());
    return *profile;
}

QVariantMap collectionEntry(
    const QString &id,
    qint64 addedAt) {
    QVariantMap payload;
    payload.insert(
        QStringLiteral("sourceId"),
        QStringLiteral("source-1"));
    payload.insert(
        QStringLiteral("url"),
        QStringLiteral(
            "https://example.invalid/item/1"));

    QVariantMap entry;
    entry.insert(
        QStringLiteral("id"),
        id);
    entry.insert(
        QStringLiteral("type"),
        QStringLiteral("series"));
    entry.insert(
        QStringLiteral("title"),
        QStringLiteral("Fixture Collection"));
    entry.insert(
        QStringLiteral("cover"),
        QStringLiteral(
            "https://example.invalid/cover.jpg"));
    entry.insert(
        QStringLiteral("payload"),
        payload);
    entry.insert(
        QStringLiteral("addedAt"),
        addedAt);
    return entry;
}

QVariantMap progressEntry(
    const QString &id,
    double progressValue,
    qint64 updatedAt = 0) {
    QVariantMap entry;
    entry.insert(
        QStringLiteral("id"),
        id);
    entry.insert(
        QStringLiteral("kind"),
        QStringLiteral("manga"));
    entry.insert(
        QStringLiteral("title"),
        QStringLiteral("Fixture Continue"));
    entry.insert(
        QStringLiteral("caption"),
        QStringLiteral("Chapter 4"));
    entry.insert(
        QStringLiteral("progress"),
        progressValue);
    entry.insert(
        QStringLiteral("resume"),
        QVariantMap{
            {
                QStringLiteral("chapter"),
                4
            }
        });

    if (updatedAt > 0) {
        entry.insert(
            QStringLiteral("updatedAt"),
            updatedAt);
    }

    return entry;
}
}

class tst_core_sync_adapters final
    : public QObject {
    Q_OBJECT

private slots:
    // Isolation gate (same law as the 2026-08-14 store fix): without the tag,
    // the default-constructed replica stores resolve to the REAL user's
    // Brotherhood/Colosseum registry hive and every convergence test would
    // read AND write Hemanth's actual Continue/Collection data. The tag
    // diverts them to a file under this test binary's own AppData root.
    void init();
    void cleanup();
    void projectionStripsLocalOnlyNestedMaterial();
    void projectionExcludesFilesystemIdentity();
    void watchStateProjectionKeysRoundTrip();
    void watchStateProjectionKeysRejectMalformed();
    void watchStateAdapterExportsCanonicalSnapshot();
    void watchStateAdapterEmitsLocalMutation();
    void watchStateAdapterAppliesRemoteStateWithoutEcho();
    void watchStateAdapterRejectsNoncanonicalRemoteState();
    void collectionRemotePutPreservesLocalOnlyOverlay();
    void collectionAdapterRoundTripsAndTombstones();
    void progressSnapshotKeepsRawSiblingRecords();
    void progressSilentTickDoesNotEmitVisibleChanged();
    void repeatedSilentTicksAreThrottledNotIndefinitelyDebounced();
    void progressRemotePutPreservesLocalOnlyOverlay();
    void progressRemoteApplyPreservesTimestampWithoutEcho();
    void progressRemoteApplyEmitsRemoteOnlyOwnerSignal();
    void progressForgetDoesNotEraseHistory();
    void twoReplicaCollectionConverges();
    void twoReplicaProgressConvergesAfterSilentOfflineTick();
};

void tst_core_sync_adapters::init() {
    qputenv("COLOSSEUM_APPDATA_TAG", "b8c_test_isolated");
}

void tst_core_sync_adapters::cleanup() {
    qunsetenv("COLOSSEUM_APPDATA_TAG");
}

void tst_core_sync_adapters::
projectionStripsLocalOnlyNestedMaterial() {
    QVariantMap entry =
        collectionEntry(
            QStringLiteral("item-1"),
            1000);
    entry.insert(
        QStringLiteral("world"),
        QStringLiteral("tankoban"));

    QVariantMap nested =
        entry.value(
            QStringLiteral("payload"))
            .toMap();
    nested.insert(
        QStringLiteral("path"),
        QStringLiteral(
            "C:\\Private\\book.cbz"));
    nested.insert(
        QStringLiteral("streamUrl"),
        QStringLiteral(
            "https://example.invalid/private"));
    nested.insert(
        QStringLiteral("logicalChoice"),
        QStringLiteral("keep-me"));
    entry.insert(
        QStringLiteral("payload"),
        nested);

    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            collection(entry);

    QCOMPARE(
        projected.disposition,
        CoreStateSyncProjection::
            Disposition::Portable);

    const QJsonObject payload =
        projected.payload
            .value(
                QStringLiteral("payload"))
            .toObject();

    QVERIFY(
        !payload.contains(
            QStringLiteral("path")));
    QVERIFY(
        !payload.contains(
            QStringLiteral("streamUrl")));
    QCOMPARE(
        payload.value(
            QStringLiteral(
                "logicalChoice"))
            .toString(),
        QStringLiteral("keep-me"));
}

void tst_core_sync_adapters::
projectionExcludesFilesystemIdentity() {
    QVariantMap entry =
        progressEntry(
            QStringLiteral(
                "C:\\Private\\book.epub"),
            0.25);

    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            progress(entry);

    QCOMPARE(
        projected.disposition,
        CoreStateSyncProjection::
            Disposition::LocalOnly);
}

void tst_core_sync_adapters::
watchStateProjectionKeysRoundTrip() {
    const QString watchedId =
        QStringLiteral("tt123:Season 2/\u00c9pisode 7? [\u65e5\u672c]");
    const QString seasonId =
        QStringLiteral("series:\u0394 & finale/part#2");

    const QString watchedEncoded =
        QString::fromLatin1(
            watchedId.toUtf8().toBase64(
                QByteArray::Base64UrlEncoding
                | QByteArray::OmitTrailingEquals));
    const QString seasonEncoded =
        QString::fromLatin1(
            seasonId.toUtf8().toBase64(
                QByteArray::Base64UrlEncoding
                | QByteArray::OmitTrailingEquals));

    const QString watchedKey =
        CoreStateSyncProjection::watchedMarkKey(watchedId);
    const QString seasonKey =
        CoreStateSyncProjection::lastSeasonKey(seasonId);

    QCOMPARE(
        watchedKey,
        QStringLiteral("watch/mark/") + watchedEncoded);
    QCOMPARE(
        seasonKey,
        QStringLiteral("watch/season/") + seasonEncoded);

    QString decoded;
    QVERIFY(
        CoreStateSyncProjection::decodeWatchedMarkKey(
            watchedKey,
            &decoded));
    QCOMPARE(decoded, watchedId);

    decoded.clear();
    QVERIFY(
        CoreStateSyncProjection::decodeLastSeasonKey(
            seasonKey,
            &decoded));
    QCOMPARE(decoded, seasonId);
}

void tst_core_sync_adapters::
watchStateProjectionKeysRejectMalformed() {
    const QString canonicalId = QStringLiteral("f");
    const QString canonical =
        CoreStateSyncProjection::watchedMarkKey(canonicalId);
    QCOMPARE(canonical, QStringLiteral("watch/mark/Zg"));

    const QStringList invalidWatched{
        QString(),
        QStringLiteral("watch/mark/"),
        QStringLiteral("watch/mark/Zg=="),
        QStringLiteral("watch/mark/!!!"),
        QStringLiteral("watch/mark/Zg/extra"),
        QStringLiteral("watch/mark/Zg/"),
        QStringLiteral("watch/season/Zg"),
        QStringLiteral("progress/mark/Zg")
    };

    for (const QString &key : invalidWatched) {
        QString decoded = QStringLiteral("unchanged");
        QVERIFY2(
            !CoreStateSyncProjection::decodeWatchedMarkKey(
                key,
                &decoded),
            qPrintable(key));
        QCOMPARE(decoded, QStringLiteral("unchanged"));
    }

    const QStringList invalidSeason{
        QStringLiteral("watch/season/"),
        QStringLiteral("watch/season/Zg=="),
        QStringLiteral("watch/season/Zg/extra"),
        QStringLiteral("watch/mark/Zg"),
        QStringLiteral("watch/season/!!!")
    };

    for (const QString &key : invalidSeason) {
        QString decoded = QStringLiteral("unchanged");
        QVERIFY2(
            !CoreStateSyncProjection::decodeLastSeasonKey(
                key,
                &decoded),
            qPrintable(key));
        QCOMPARE(decoded, QStringLiteral("unchanged"));
    }
}

void tst_core_sync_adapters::
watchStateAdapterExportsCanonicalSnapshot() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));

    store.setWatchedMark(
        QStringLiteral("movie-1"),
        true);
    store.setWatchedMark(
        QStringLiteral("movie-2"),
        false);
    store.rememberLastSeason(
        QStringLiteral("series-1"),
        3);

    WatchStateSyncAdapter adapter(
        &store);
    QCOMPARE(
        adapter.categoryId(),
        QStringLiteral("watch_state"));
    QCOMPARE(adapter.schemaVersion(), 1);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));
    QCOMPARE(snapshot.records.size(), 3);

    QHash<QString, QJsonObject> records;
    for (const SyncAdapterRecord &record :
         snapshot.records) {
        QVERIFY(record.payload.isObject());
        records.insert(
            record.recordKey,
            record.payload.toObject());
    }

    QCOMPARE(
        records.value(
            CoreStateSyncProjection::
                watchedMarkKey(
                    QStringLiteral("movie-1"))),
        QJsonObject({
            {
                QStringLiteral("id"),
                QStringLiteral("movie-1")
            },
            {
                QStringLiteral("mark"),
                1
            }
        }));
    QCOMPARE(
        records.value(
            CoreStateSyncProjection::
                watchedMarkKey(
                    QStringLiteral("movie-2"))),
        QJsonObject({
            {
                QStringLiteral("id"),
                QStringLiteral("movie-2")
            },
            {
                QStringLiteral("mark"),
                -1
            }
        }));
    QCOMPARE(
        records.value(
            CoreStateSyncProjection::
                lastSeasonKey(
                    QStringLiteral("series-1"))),
        QJsonObject({
            {
                QStringLiteral("seriesId"),
                QStringLiteral("series-1")
            },
            {
                QStringLiteral("season"),
                3
            }
        }));
}

void tst_core_sync_adapters::
watchStateAdapterEmitsLocalMutation() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));
    WatchStateSyncAdapter adapter(
        &store);

    QSignalSpy mutationSpy(
        &adapter,
        &WatchStateSyncAdapter::
            localMutationAvailable);

    store.setWatchedMark(
        QStringLiteral("movie-1"),
        true);
    QCOMPARE(mutationSpy.count(), 1);

    store.rememberLastSeason(
        QStringLiteral("series-1"),
        2);
    QCOMPARE(mutationSpy.count(), 2);
}

void tst_core_sync_adapters::
watchStateAdapterAppliesRemoteStateWithoutEcho() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));
    store.setWatchedMark(
        QStringLiteral("movie-keep"),
        true);
    store.rememberLastSeason(
        QStringLiteral("series-keep"),
        4);

    WatchStateSyncAdapter adapter(
        &store);
    SyncAdapterRegistry registry;
    QVERIFY(
        registry.registerAdapter(
            &adapter));

    QSignalSpy adapterMutationSpy(
        &adapter,
        &WatchStateSyncAdapter::
            localMutationAvailable);
    QSignalSpy registryMutationSpy(
        &registry,
        &SyncAdapterRegistry::
            localMutationAvailable);

    SyncAdapterMutation watchedPut;
    watchedPut.categoryId =
        QStringLiteral("watch_state");
    watchedPut.recordKey =
        CoreStateSyncProjection::
            watchedMarkKey(
                QStringLiteral("movie-remote"));
    watchedPut.schemaVersion = 1;
    watchedPut.operation =
        SyncWireOperation::Put;
    watchedPut.payload =
        QJsonObject{
            {
                QStringLiteral("id"),
                QStringLiteral("movie-remote")
            },
            {
                QStringLiteral("mark"),
                -1
            }
        };

    QVERIFY(
        registry.applyRemote(
            watchedPut));
    QCOMPARE(
        store.watchedMark(
            QStringLiteral("movie-remote")),
        -1);
    QCOMPARE(adapterMutationSpy.count(), 0);
    QCOMPARE(registryMutationSpy.count(), 0);

    SyncAdapterMutation seasonPut;
    seasonPut.categoryId =
        QStringLiteral("watch_state");
    seasonPut.recordKey =
        CoreStateSyncProjection::
            lastSeasonKey(
                QStringLiteral("series-remote"));
    seasonPut.schemaVersion = 1;
    seasonPut.operation =
        SyncWireOperation::Put;
    seasonPut.payload =
        QJsonObject{
            {
                QStringLiteral("seriesId"),
                QStringLiteral("series-remote")
            },
            {
                QStringLiteral("season"),
                5
            }
        };

    QVERIFY(
        registry.applyRemote(
            seasonPut));
    QCOMPARE(
        store.lastSeason(
            QStringLiteral("series-remote")),
        5);
    QCOMPARE(adapterMutationSpy.count(), 0);
    QCOMPARE(registryMutationSpy.count(), 0);

    const int revisionBeforeReplay =
        store.revision();
    QVERIFY(
        registry.applyRemote(
            watchedPut));
    QVERIFY(
        registry.applyRemote(
            seasonPut));
    QCOMPARE(
        store.revision(),
        revisionBeforeReplay);
    QCOMPARE(adapterMutationSpy.count(), 0);

    SyncAdapterMutation watchedDelete =
        watchedPut;
    watchedDelete.operation =
        SyncWireOperation::Delete;
    watchedDelete.payload =
        QJsonValue();
    QVERIFY(
        registry.applyRemote(
            watchedDelete));
    QCOMPARE(
        store.watchedMark(
            QStringLiteral("movie-remote")),
        0);
    QCOMPARE(
        store.lastSeason(
            QStringLiteral("series-remote")),
        5);
    QCOMPARE(
        store.watchedMark(
            QStringLiteral("movie-keep")),
        1);
    QCOMPARE(
        store.lastSeason(
            QStringLiteral("series-keep")),
        4);

    SyncAdapterMutation seasonDelete =
        seasonPut;
    seasonDelete.operation =
        SyncWireOperation::Delete;
    seasonDelete.payload =
        QJsonValue();
    QVERIFY(
        registry.applyRemote(
            seasonDelete));
    QCOMPARE(
        store.lastSeason(
            QStringLiteral("series-remote")),
        -1);
    QCOMPARE(
        store.watchedMark(
            QStringLiteral("movie-keep")),
        1);
    QCOMPARE(
        store.lastSeason(
            QStringLiteral("series-keep")),
        4);
    QCOMPARE(adapterMutationSpy.count(), 0);
    QCOMPARE(registryMutationSpy.count(), 0);
}

void tst_core_sync_adapters::
watchStateAdapterRejectsNoncanonicalRemoteState() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));
    WatchStateSyncAdapter adapter(
        &store);

    const QString watchedKey =
        CoreStateSyncProjection::
            watchedMarkKey(
                QStringLiteral("movie-1"));
    const QString seasonKey =
        CoreStateSyncProjection::
            lastSeasonKey(
                QStringLiteral("series-1"));

    QString error;
    QVERIFY(
        !adapter.applyRemote(
            watchedKey,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("id"),
                    QStringLiteral("movie-1")
                },
                {
                    QStringLiteral("mark"),
                    1
                }
            },
            2,
            &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(
        !adapter.applyRemote(
            watchedKey,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("id"),
                    QStringLiteral("different")
                },
                {
                    QStringLiteral("mark"),
                    1
                }
            },
            1,
            &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(
        !adapter.applyRemote(
            watchedKey,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("id"),
                    QStringLiteral("movie-1")
                },
                {
                    QStringLiteral("mark"),
                    0
                }
            },
            1,
            &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(
        !adapter.applyRemote(
            seasonKey,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral("seriesId"),
                    QStringLiteral("series-1")
                },
                {
                    QStringLiteral("season"),
                    1.5
                }
            },
            1,
            &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(
        !adapter.applyRemote(
            QStringLiteral("watch/other/Zg"),
            SyncWireOperation::Delete,
            QJsonValue(),
            1,
            &error));
    QVERIFY(!error.isEmpty());

    QCOMPARE(
        store.watchedMark(
            QStringLiteral("movie-1")),
        0);
    QCOMPARE(
        store.lastSeason(
            QStringLiteral("series-1")),
        -1);
}

void tst_core_sync_adapters::
collectionRemotePutPreservesLocalOnlyOverlay() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    CollectionStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "collection.ini")));

    QVariantMap existing =
        collectionEntry(
            QStringLiteral("item-1"),
            1000);

    QVariantMap localPayload =
        existing.value(
            QStringLiteral("payload"))
            .toMap();
    localPayload.insert(
        QStringLiteral("path"),
        QStringLiteral(
            "C:\\Private\\owned.cbz"));
    localPayload.insert(
        QStringLiteral("logicalChoice"),
        QStringLiteral("old-safe"));
    existing.insert(
        QStringLiteral("payload"),
        localPayload);

    store.add(
        QStringLiteral("tankoban"),
        existing);

    QVariantMap remote =
        collectionEntry(
            QStringLiteral("item-1"),
            2000);
    remote.insert(
        QStringLiteral("world"),
        QStringLiteral("tankoban"));

    QVariantMap remotePayload =
        remote.value(
            QStringLiteral("payload"))
            .toMap();
    remotePayload.insert(
        QStringLiteral("logicalChoice"),
        QStringLiteral("new-safe"));
    remote.insert(
        QStringLiteral("payload"),
        remotePayload);

    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            collection(remote);
    QCOMPARE(
        projected.disposition,
        CoreStateSyncProjection::
            Disposition::Portable);

    CollectionSyncAdapter adapter(
        &store);
    SyncAdapterRegistry registry;
    QVERIFY(
        registry.registerAdapter(
            &adapter));

    SyncAdapterMutation mutation;
    mutation.categoryId =
        QStringLiteral("collection");
    mutation.recordKey =
        projected.recordKey;
    mutation.schemaVersion = 1;
    mutation.operation =
        SyncWireOperation::Put;
    mutation.payload =
        projected.payload;

    QVERIFY(
        registry.applyRemote(
            mutation));

    const QVariantList storedItems =
        store.items(
            QStringLiteral("tankoban"));
    QCOMPARE(storedItems.size(), 1);
    const QVariantMap stored =
        storedItems.first().toMap();
    QCOMPARE(
        stored.value(
            QStringLiteral("id"))
            .toString(),
        QStringLiteral("item-1"));

    const QVariantMap storedPayload =
        stored.value(
            QStringLiteral("payload"))
            .toMap();

    QCOMPARE(
        storedPayload.value(
            QStringLiteral("path"))
            .toString(),
        QStringLiteral(
            "C:\\Private\\owned.cbz"));
    QCOMPARE(
        storedPayload.value(
            QStringLiteral(
                "logicalChoice"))
            .toString(),
        QStringLiteral("new-safe"));

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(
        snapshot.records.first()
            .payload,
        projected.payload);
}

void tst_core_sync_adapters::
collectionAdapterRoundTripsAndTombstones() {
    QTemporaryDir sourceTemp;
    QTemporaryDir targetTemp;
    QVERIFY(sourceTemp.isValid());
    QVERIFY(targetTemp.isValid());

    CollectionStore source(
        QDir(sourceTemp.path())
            .filePath(
                QStringLiteral(
                    "collection.ini")));
    CollectionStore target(
        QDir(targetTemp.path())
            .filePath(
                QStringLiteral(
                    "collection.ini")));

    QVariantMap entry =
        collectionEntry(
            QStringLiteral("item-1"),
            1000);
    source.add(
        QStringLiteral("tankoban"),
        entry);

    CollectionSyncAdapter sourceAdapter(
        &source);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        sourceAdapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));
    QCOMPARE(
        snapshot.records.size(),
        1);

    SyncAdapterRegistry registry;
    CollectionSyncAdapter targetAdapter(
        &target);
    QVERIFY(
        registry.registerAdapter(
            &targetAdapter));

    QSignalSpy localSpy(
        &registry,
        &SyncAdapterRegistry::
            localMutationAvailable);

    SyncAdapterMutation put;
    put.categoryId =
        QStringLiteral("collection");
    put.recordKey =
        snapshot.records.first()
            .recordKey;
    put.schemaVersion = 1;
    put.operation =
        SyncWireOperation::Put;
    put.payload =
        snapshot.records.first()
            .payload;

    QVERIFY2(
        registry.applyRemote(
            put),
        "remote Collection PUT failed");
    QCOMPARE(localSpy.count(), 0);
    QVERIFY(
        target.has(
            QStringLiteral("tankoban"),
            QStringLiteral("item-1")));

    SyncAdapterMutation remove =
        put;
    remove.operation =
        SyncWireOperation::Delete;
    remove.payload =
        QJsonValue();

    QVERIFY(
        registry.applyRemote(
            remove));
    QCOMPARE(localSpy.count(), 0);
    QVERIFY(
        !target.has(
            QStringLiteral("tankoban"),
            QStringLiteral("item-1")));
}

void tst_core_sync_adapters::
progressSnapshotKeepsRawSiblingRecords() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));

    QVariantMap first =
        progressEntry(
            QStringLiteral(
                "tt100:s1:e1"),
            0.25);
    first.insert(
        QStringLiteral("kind"),
        QStringLiteral("video"));

    QVariantMap second =
        progressEntry(
            QStringLiteral(
                "tt100:s1:e2"),
            0.50);
    second.insert(
        QStringLiteral("kind"),
        QStringLiteral("video"));

    store.recordSilent(first);
    store.recordSilent(second);

    ProgressSyncAdapter adapter(
        &store,
        nullptr,
        1);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));
    QCOMPARE(
        snapshot.records.size(),
        2);
}

void tst_core_sync_adapters::
progressSilentTickDoesNotEmitVisibleChanged() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));
    ProgressSyncAdapter adapter(
        &store,
        nullptr,
        1);

    QSignalSpy visibleSpy(
        &store,
        &ProgressStore::changed);
    QSignalSpy syncSpy(
        &adapter,
        &ProgressSyncAdapter::
            localMutationAvailable);

    store.recordSilent(
        progressEntry(
            QStringLiteral("manga-1"),
            0.40));

    QCOMPARE(
        visibleSpy.count(),
        0);
    QTRY_COMPARE(
        syncSpy.count(),
        1);
    QCOMPARE(
        visibleSpy.count(),
        0);
}

void tst_core_sync_adapters::
repeatedSilentTicksAreThrottledNotIndefinitelyDebounced() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));
    ProgressSyncAdapter adapter(
        &store,
        nullptr,
        25);

    QSignalSpy visibleSpy(
        &store,
        &ProgressStore::changed);
    QSignalSpy syncSpy(
        &adapter,
        &ProgressSyncAdapter::
            localMutationAvailable);

    store.recordSilent(
        progressEntry(
            QStringLiteral("manga-1"),
            0.10));

    QTimer::singleShot(
        10,
        &store,
        [&store]() {
            store.recordSilent(
                progressEntry(
                    QStringLiteral("manga-1"),
                    0.20));
        });
    QTimer::singleShot(
        20,
        &store,
        [&store]() {
            store.recordSilent(
                progressEntry(
                    QStringLiteral("manga-1"),
                    0.30));
        });

    QCOMPARE(visibleSpy.count(), 0);
    QVERIFY2(
        syncSpy.wait(35),
        "silent-progress throttle did not emit within the original window");
    QCOMPARE(visibleSpy.count(), 0);
}

void tst_core_sync_adapters::
progressRemotePutPreservesLocalOnlyOverlay() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));

    QVariantMap existing =
        progressEntry(
            QStringLiteral("manga-1"),
            0.25);
    QVariantMap localResume =
        existing.value(
            QStringLiteral("resume"))
            .toMap();
    localResume.insert(
        QStringLiteral("path"),
        QStringLiteral(
            "C:\\Private\\chapter.cbz"));
    localResume.insert(
        QStringLiteral("chapter"),
        3);
    existing.insert(
        QStringLiteral("resume"),
        localResume);
    store.record(existing);

    QVariantMap remote =
        progressEntry(
            QStringLiteral("manga-1"),
            0.80,
            987654321);
    QVariantMap remoteResume =
        remote.value(
            QStringLiteral("resume"))
            .toMap();
    remoteResume.insert(
        QStringLiteral("chapter"),
        8);
    remote.insert(
        QStringLiteral("resume"),
        remoteResume);

    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            progress(remote);
    QCOMPARE(
        projected.disposition,
        CoreStateSyncProjection::
            Disposition::Portable);

    ProgressSyncAdapter adapter(
        &store,
        nullptr,
        1);
    SyncAdapterRegistry registry;
    QVERIFY(
        registry.registerAdapter(
            &adapter));

    SyncAdapterMutation mutation;
    mutation.categoryId =
        QStringLiteral(
            "continue_progress");
    mutation.recordKey =
        projected.recordKey;
    mutation.schemaVersion = 1;
    mutation.operation =
        SyncWireOperation::Put;
    mutation.payload =
        projected.payload;

    QVERIFY(
        registry.applyRemote(
            mutation));

    const QVariantMap stored =
        store.get(
            QStringLiteral("manga"),
            QStringLiteral("manga-1"));
    const QVariantMap storedResume =
        stored.value(
            QStringLiteral("resume"))
            .toMap();

    QCOMPARE(
        storedResume.value(
            QStringLiteral("path"))
            .toString(),
        QStringLiteral(
            "C:\\Private\\chapter.cbz"));
    QCOMPARE(
        storedResume.value(
            QStringLiteral("chapter"))
            .toInt(),
        8);
    QCOMPARE(
        stored.value(
            QStringLiteral("progress"))
            .toDouble(),
        0.80);
}

void tst_core_sync_adapters::
progressRemoteApplyPreservesTimestampWithoutEcho() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));
    ProgressSyncAdapter adapter(
        &store,
        nullptr,
        1);
    SyncAdapterRegistry registry;
    QVERIFY(
        registry.registerAdapter(
            &adapter));

    const qint64 remoteUpdatedAt =
        123456789;

    QVariantMap entry =
        progressEntry(
            QStringLiteral("manga-1"),
            0.65,
            remoteUpdatedAt);

    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            progress(entry);
    QCOMPARE(
        projected.disposition,
        CoreStateSyncProjection::
            Disposition::Portable);

    QSignalSpy visibleSpy(
        &store,
        &ProgressStore::changed);
    QSignalSpy localSpy(
        &registry,
        &SyncAdapterRegistry::
            localMutationAvailable);

    SyncAdapterMutation mutation;
    mutation.categoryId =
        QStringLiteral(
            "continue_progress");
    mutation.recordKey =
        projected.recordKey;
    mutation.schemaVersion = 1;
    mutation.operation =
        SyncWireOperation::Put;
    mutation.payload =
        projected.payload;

    QVERIFY(
        registry.applyRemote(
            mutation));

    QCOMPARE(
        visibleSpy.count(),
        1);
    QCOMPARE(
        localSpy.count(),
        0);

    const QVariantMap stored =
        store.get(
            QStringLiteral("manga"),
            QStringLiteral("manga-1"));

    QCOMPARE(
        stored.value(
            QStringLiteral("updatedAt"))
            .toLongLong(),
        remoteUpdatedAt);
}

void tst_core_sync_adapters::
progressRemoteApplyEmitsRemoteOnlyOwnerSignal() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));

    QSignalSpy importedSpy(
        &store,
        &ProgressStore::
            syncedEntryApplied);

    QVariantMap local =
        progressEntry(
            QStringLiteral(
                "tankoban-series"),
            0.25);
    local.insert(
        QStringLiteral("kind"),
        QStringLiteral("tankoban"));

    store.record(local);
    QCOMPARE(importedSpy.count(), 0);

    QVariantMap remote = local;
    remote.insert(
        QStringLiteral("progress"),
        0.75);
    remote.insert(
        QStringLiteral("updatedAt"),
        qint64(987654321));
    remote.insert(
        QStringLiteral("resume"),
        QVariantMap{
            {
                QStringLiteral("chapterId"),
                QStringLiteral("volume-2")
            },
            {
                QStringLiteral("page"),
                7
            },
            {
                QStringLiteral("pageFraction"),
                0.42
            }
        });

    QVERIFY(
        store.applySyncedEntry(
            remote));

    QCOMPARE(importedSpy.count(), 1);
    QCOMPARE(
        importedSpy.at(0).at(0)
            .toString(),
        QStringLiteral("tankoban"));
    QCOMPARE(
        importedSpy.at(0).at(1)
            .toString(),
        QStringLiteral(
            "tankoban-series"));

    // Idempotent remote replay must not reposition an active reader twice.
    QVERIFY(
        store.applySyncedEntry(
            remote));
    QCOMPARE(importedSpy.count(), 1);

    // Local writes continue using the existing owner signals and never
    // masquerade as remote sync imports.
    store.record(local);
    QCOMPARE(importedSpy.count(), 1);
}

void tst_core_sync_adapters::
twoReplicaCollectionConverges() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    CoreFixtureService service;
    CoreReplica a(
        &service,
        makeProfile(&tempA),
        QString::fromLatin1(
            kDeviceA));
    CoreReplica b(
        &service,
        makeProfile(&tempB),
        QString::fromLatin1(
            kDeviceB));

    a.collection.add(
        QStringLiteral("tankoban"),
        collectionEntry(
            QStringLiteral("item-1"),
            service.serverTimeMs));

    a.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);

    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();

    QTRY_VERIFY(
        b.collection.has(
            QStringLiteral("tankoban"),
            QStringLiteral("item-1")));

    b.collection.remove(
        QStringLiteral("tankoban"),
        QStringLiteral("item-1"));

    QTRY_VERIFY(
        b.engine.pendingOutboxCount()
            >= 1);
    b.engine.requestImmediateSync();
    QTRY_COMPARE(
        b.engine.pendingOutboxCount(),
        0);

    a.engine.requestImmediateSync();
    QTRY_VERIFY(
        !a.collection.has(
            QStringLiteral("tankoban"),
            QStringLiteral("item-1")));
}

void tst_core_sync_adapters::
twoReplicaProgressConvergesAfterSilentOfflineTick() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    CoreFixtureService service;
    CoreReplica a(
        &service,
        makeProfile(&tempA),
        QString::fromLatin1(
            kDeviceA));
    CoreReplica b(
        &service,
        makeProfile(&tempB),
        QString::fromLatin1(
            kDeviceB));

    a.progress.record(
        progressEntry(
            QStringLiteral("manga-1"),
            0.20));

    a.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);

    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();

    QTRY_COMPARE(
        b.progress
            .get(
                QStringLiteral("manga"),
                QStringLiteral("manga-1"))
            .value(
                QStringLiteral("progress"))
            .toDouble(),
        0.20);

    b.engine.setNetworkEnabled(false);

    QSignalSpy visibleSpy(
        &b.progress,
        &ProgressStore::changed);

    b.progress.recordSilent(
        progressEntry(
            QStringLiteral("manga-1"),
            0.75));

    QCOMPARE(
        visibleSpy.count(),
        0);

    QTRY_VERIFY(
        b.engine.pendingOutboxCount()
            >= 1);

    b.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        b.engine.pendingOutboxCount(),
        0);

    a.engine.requestImmediateSync();

    QTRY_COMPARE(
        a.progress
            .get(
                QStringLiteral("manga"),
                QStringLiteral("manga-1"))
            .value(
                QStringLiteral("progress"))
            .toDouble(),
        0.75);
}

void tst_core_sync_adapters::
progressForgetDoesNotEraseHistory() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore progress(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "progress.ini")));
    HistoryStore history(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini")));

    QVariantMap entry =
        progressEntry(
            QStringLiteral("manga-1"),
            0.75);
    progress.record(entry);

    QVERIFY(
        history.markCompleted(
            QStringLiteral("manga"),
            QStringLiteral("manga-1"),
            7000));

    progress.forget(
        QStringLiteral("manga"),
        QStringLiteral("manga-1"));

    QVERIFY(
        progress
            .get(
                QStringLiteral("manga"),
                QStringLiteral("manga-1"))
            .isEmpty());

    QVERIFY(
        !history
             .get(
                 QStringLiteral("manga"),
                 QStringLiteral("manga-1"))
             .isEmpty());
}

QTEST_MAIN(tst_core_sync_adapters)
#include "tst_core_sync_adapters.moc"
