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
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"

#include <QDeadlineTimer>
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

bool waitForAsyncFlag(const bool &flag) {
    const QDeadlineTimer deadline(5000);
    while (!flag && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return flag;
}

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
    void collectionRemotePutPreservesLocalOnlyOverlay();
    void collectionAdapterRoundTripsAndTombstones();
    void progressSnapshotKeepsRawSiblingRecords();
    void progressSilentTickDoesNotEmitVisibleChanged();
    void repeatedSilentTicksAreThrottledNotIndefinitelyDebounced();
    void progressRemotePutPreservesLocalOnlyOverlay();
    void progressRemoteApplyPreservesTimestampWithoutEcho();
    void progressAsyncRemoteReceiptPersistsBeforeCallback();
    void collectionAsyncRemoteReceiptPersistsBeforeCallback();
    void progressRemoteApplyEmitsRemoteOnlyOwnerSignal();
    void corruptProgressStorageFailsClosed();
    void corruptCollectionStorageFailsClosed();
    void progressPersistenceFailureDoesNotAdvanceCursor();
    void collectionPersistenceFailureDoesNotAdvanceCursor();
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
corruptProgressStorageFailsClosed() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path =
        QDir(temp.path()).filePath(QStringLiteral("progress.ini"));

    QSettings corrupt(path, QSettings::IniFormat);
    corrupt.setValue(QStringLiteral("continue/entries"),
                     QByteArrayLiteral(R"({"progress/manga/bad":42})"));
    corrupt.sync();

    ProgressStore store(path);
    QString error;
    QVERIFY(!store.healthy(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.syncEntries().isEmpty());
    QVERIFY(!store.applySyncedEntry(progressEntry(
        QStringLiteral("manga-1"), 0.4)));
    QVERIFY(store.get(QStringLiteral("manga"),
                      QStringLiteral("manga-1")).isEmpty());
}

void tst_core_sync_adapters::
corruptCollectionStorageFailsClosed() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path =
        QDir(temp.path()).filePath(QStringLiteral("collection.ini"));

    QSettings corrupt(path, QSettings::IniFormat);
    corrupt.setValue(QStringLiteral("collection/entries"),
                     QByteArrayLiteral(R"({"collection/tankoban/bad":42})"));
    corrupt.sync();

    CollectionStore store(path);
    QString error;
    QVERIFY(!store.healthy(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.syncEntries().isEmpty());
    store.add(QStringLiteral("tankoban"), collectionEntry(
        QStringLiteral("item-1"), 1000));
    QVERIFY(!store.has(QStringLiteral("tankoban"),
                       QStringLiteral("item-1")));
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
progressAsyncRemoteReceiptPersistsBeforeCallback() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path = QDir(temp.path()).filePath(QStringLiteral("progress.ini"));
    ProgressStore store(path);
    ProgressSyncAdapter adapter(&store, nullptr, 1);
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&adapter));

    const QVariantMap entry = progressEntry(
        QStringLiteral("manga-async"),
        0.65,
        123456789);
    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::progress(entry);
    SyncAdapterMutation mutation{
        QStringLiteral("continue_progress"),
        projected.recordKey,
        1,
        SyncWireOperation::Put,
        projected.payload};

    bool callbackCalled = false;
    SyncAdapterRegistryError callbackError;
    QSignalSpy localSpy(
        &registry,
        &SyncAdapterRegistry::localMutationAvailable);
    QVERIFY(registry.applyRemoteAsync(
        mutation,
        [&](const SyncAdapterRegistryError &result) {
            callbackCalled = true;
            callbackError = result;
        }));
    QVERIFY(!callbackCalled);
    QTRY_VERIFY(callbackCalled);
    QVERIFY2(callbackError.isEmpty(), qPrintable(callbackError.detail));
    QCOMPARE(localSpy.count(), 0);

    ProgressStore reopened(path);
    QCOMPARE(
        reopened.get(QStringLiteral("manga"), QStringLiteral("manga-async"))
            .value(QStringLiteral("updatedAt"))
            .toLongLong(),
        qint64(123456789));

    // A remote owner write races with a local write while its worker receipt
    // is pending. Record-level base detection must preserve a newer same-key
    // local edit, while an unrelated local edit must not suppress the remote
    // winner or its delete. The registry signal is the outbox seam: the local
    // write reports once, and the remote receipt never echoes.
    QTemporaryDir raceTemp;
    QVERIFY(raceTemp.isValid());
    const auto runProgressRace = [&](SyncWireOperation operation,
                                     bool sameKey,
                                     const QString &label) -> QString {
        const QString path = QDir(raceTemp.path()).filePath(
            QStringLiteral("progress-%1.ini").arg(label));
        ProgressStore raceStore(path);
        ProgressSyncAdapter raceAdapter(&raceStore, nullptr, 1);
        SyncAdapterRegistry raceRegistry;
        SyncAdapterRegistryError registrationError;
        if (!raceRegistry.registerAdapter(&raceAdapter, &registrationError))
            return registrationError.detail;

        const QString targetId = QStringLiteral("race-target");
        const QString unrelatedId = QStringLiteral("race-unrelated");
        raceStore.record(progressEntry(targetId, 0.10, 1000));

        QVariantMap remote = progressEntry(targetId, 0.80, 2000);
        const CoreStateSyncProjection projected =
            CoreStateSyncProjection::progress(remote);
        SyncAdapterMutation mutation{
            QStringLiteral("continue_progress"),
            projected.recordKey,
            1,
            operation,
            operation == SyncWireOperation::Put
                ? projected.payload
                : QJsonValue()};

        QSignalSpy localSpy(
            &raceRegistry,
            &SyncAdapterRegistry::localMutationAvailable);
        bool callbackCalled = false;
        SyncAdapterRegistryError callbackError;
        if (!raceRegistry.applyRemoteAsync(
                mutation,
                [&](const SyncAdapterRegistryError &result) {
                    callbackCalled = true;
                    callbackError = result;
                })) {
            return QStringLiteral("remote race did not start");
        }
        if (callbackCalled)
            return QStringLiteral("remote race callback completed synchronously");

        if (sameKey)
            raceStore.record(progressEntry(targetId, 0.60, 3000));
        else
            raceStore.record(progressEntry(unrelatedId, 0.30, 3000));

        if (!waitForAsyncFlag(callbackCalled))
            return QStringLiteral("remote race callback timed out");
        if (!callbackError.isEmpty())
            return callbackError.detail;
        if (localSpy.count() != 1)
            return QStringLiteral("local outbox signal was echoed or dropped");

        const QVariantMap target = raceStore.get(QStringLiteral("manga"), targetId);
        const QVariantMap unrelated = raceStore.get(QStringLiteral("manga"), unrelatedId);
        if (operation == SyncWireOperation::Put) {
            const double expectedTarget = sameKey ? 0.60 : 0.80;
            if (!qFuzzyCompare(target.value(QStringLiteral("progress")).toDouble(), expectedTarget))
                return QStringLiteral("Progress PUT race chose the wrong same-key result");
            if (!sameKey
                && !qFuzzyCompare(unrelated.value(QStringLiteral("progress")).toDouble(), 0.30))
                return QStringLiteral("Progress PUT race dropped unrelated local work");
        } else {
            if (sameKey) {
                if (!qFuzzyCompare(target.value(QStringLiteral("progress")).toDouble(), 0.60))
                    return QStringLiteral("Progress DELETE race dropped same-key local work");
            } else if (!target.isEmpty()) {
                return QStringLiteral("Progress DELETE race retained an unchanged remote key");
            }
            if (!sameKey
                && !qFuzzyCompare(unrelated.value(QStringLiteral("progress")).toDouble(), 0.30))
                return QStringLiteral("Progress DELETE race dropped unrelated local work");
        }

        raceStore.flush();
        ProgressStore persisted(path);
        const QVariantMap persistedTarget =
            persisted.get(QStringLiteral("manga"), targetId);
        const QVariantMap persistedUnrelated =
            persisted.get(QStringLiteral("manga"), unrelatedId);
        if (operation == SyncWireOperation::Put) {
            const double expectedTarget = sameKey ? 0.60 : 0.80;
            if (!qFuzzyCompare(
                    persistedTarget.value(QStringLiteral("progress")).toDouble(),
                    expectedTarget))
                return QStringLiteral("Progress PUT race did not persist the winning snapshot");
        } else if (sameKey) {
            if (!qFuzzyCompare(
                    persistedTarget.value(QStringLiteral("progress")).toDouble(),
                    0.60))
                return QStringLiteral("Progress DELETE race did not persist the local winner");
        } else if (!persistedTarget.isEmpty()) {
            return QStringLiteral("Progress DELETE race persisted a stale remote key");
        }
        if (!sameKey
            && !qFuzzyCompare(
                persistedUnrelated.value(QStringLiteral("progress")).toDouble(),
                0.30))
            return QStringLiteral("Progress race did not persist unrelated local work");
        return QString();
    };

    QVERIFY2(
        runProgressRace(SyncWireOperation::Put, true, QStringLiteral("put-same")).isEmpty(),
        "Progress same-key PUT race failed");
    QVERIFY2(
        runProgressRace(SyncWireOperation::Put, false, QStringLiteral("put-unrelated")).isEmpty(),
        "Progress unrelated-key PUT race failed");
    QVERIFY2(
        runProgressRace(SyncWireOperation::Delete, true, QStringLiteral("delete-same")).isEmpty(),
        "Progress same-key DELETE race failed");
    QVERIFY2(
        runProgressRace(SyncWireOperation::Delete, false, QStringLiteral("delete-unrelated")).isEmpty(),
        "Progress unrelated-key DELETE race failed");
}

void tst_core_sync_adapters::
collectionAsyncRemoteReceiptPersistsBeforeCallback() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path = QDir(temp.path()).filePath(QStringLiteral("collection.ini"));
    CollectionStore store(path);
    CollectionSyncAdapter adapter(&store);
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&adapter));

    QVariantMap entry = collectionEntry(QStringLiteral("item-async"), 1000);
    entry.insert(QStringLiteral("world"), QStringLiteral("tankoban"));
    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::collection(entry);
    SyncAdapterMutation mutation{
        QStringLiteral("collection"),
        projected.recordKey,
        1,
        SyncWireOperation::Put,
        projected.payload};

    bool callbackCalled = false;
    SyncAdapterRegistryError callbackError;
    QVERIFY(registry.applyRemoteAsync(
        mutation,
        [&](const SyncAdapterRegistryError &result) {
            callbackCalled = true;
            callbackError = result;
        }));
    QVERIFY(!callbackCalled);
    QTRY_VERIFY(callbackCalled);
    QVERIFY2(callbackError.isEmpty(), qPrintable(callbackError.detail));

    CollectionStore reopened(path);
    QVERIFY(reopened.has(QStringLiteral("tankoban"), QStringLiteral("item-async")));

    // Exercise the same record-level rebase rules for Collection PUT and
    // DELETE. The immediate local add is the outbox mutation; only that local
    // signal may be observed while the remote owner receipt is in flight.
    QTemporaryDir raceTemp;
    QVERIFY(raceTemp.isValid());
    const auto runCollectionRace = [&](SyncWireOperation operation,
                                       bool sameKey,
                                       const QString &label) -> QString {
        const QString path = QDir(raceTemp.path()).filePath(
            QStringLiteral("collection-%1.ini").arg(label));
        CollectionStore raceStore(path);
        CollectionSyncAdapter raceAdapter(&raceStore);
        SyncAdapterRegistry raceRegistry;
        SyncAdapterRegistryError registrationError;
        if (!raceRegistry.registerAdapter(&raceAdapter, &registrationError))
            return registrationError.detail;

        const QString targetId = QStringLiteral("race-target");
        const QString unrelatedId = QStringLiteral("race-unrelated");
        raceStore.add(
            QStringLiteral("tankoban"),
            collectionEntry(targetId, 1000));

        QVariantMap remote = collectionEntry(targetId, 2000);
        remote.insert(QStringLiteral("world"), QStringLiteral("tankoban"));
        const CoreStateSyncProjection projected =
            CoreStateSyncProjection::collection(remote);
        SyncAdapterMutation mutation{
            QStringLiteral("collection"),
            projected.recordKey,
            1,
            operation,
            operation == SyncWireOperation::Put
                ? projected.payload
                : QJsonValue()};

        QSignalSpy localSpy(
            &raceRegistry,
            &SyncAdapterRegistry::localMutationAvailable);
        bool callbackCalled = false;
        SyncAdapterRegistryError callbackError;
        if (!raceRegistry.applyRemoteAsync(
                mutation,
                [&](const SyncAdapterRegistryError &result) {
                    callbackCalled = true;
                    callbackError = result;
                })) {
            return QStringLiteral("remote race did not start");
        }
        if (callbackCalled)
            return QStringLiteral("remote race callback completed synchronously");

        if (sameKey)
            raceStore.add(
                QStringLiteral("tankoban"),
                collectionEntry(targetId, 3000));
        else
            raceStore.add(
                QStringLiteral("tankoban"),
                collectionEntry(unrelatedId, 3000));

        if (!waitForAsyncFlag(callbackCalled))
            return QStringLiteral("remote race callback timed out");
        if (!callbackError.isEmpty())
            return callbackError.detail;
        if (localSpy.count() != 1)
            return QStringLiteral("local outbox signal was echoed or dropped");

        auto findEntry = [](CollectionStore &store, const QString &id) {
            for (const QVariant &value : store.items(QStringLiteral("tankoban"))) {
                const QVariantMap candidate = value.toMap();
                if (candidate.value(QStringLiteral("id")).toString() == id)
                    return candidate;
            }
            return QVariantMap();
        };

        const QVariantMap target = findEntry(raceStore, targetId);
        const QVariantMap unrelated = findEntry(raceStore, unrelatedId);
        if (operation == SyncWireOperation::Put) {
            const qint64 expectedAddedAt = sameKey ? 3000 : 2000;
            if (target.value(QStringLiteral("addedAt")).toLongLong() != expectedAddedAt)
                return QStringLiteral("Collection PUT race chose the wrong same-key result");
            if (!sameKey
                && unrelated.value(QStringLiteral("addedAt")).toLongLong() != 3000)
                return QStringLiteral("Collection PUT race dropped unrelated local work");
        } else {
            if (sameKey) {
                if (target.value(QStringLiteral("addedAt")).toLongLong() != 3000)
                    return QStringLiteral("Collection DELETE race dropped same-key local work");
            } else if (!target.isEmpty()) {
                return QStringLiteral("Collection DELETE race retained an unchanged remote key");
            }
            if (!sameKey
                && unrelated.value(QStringLiteral("addedAt")).toLongLong() != 3000)
                return QStringLiteral("Collection DELETE race dropped unrelated local work");
        }

        raceStore.flush();
        CollectionStore persisted(path);
        const QVariantMap persistedTarget = findEntry(persisted, targetId);
        const QVariantMap persistedUnrelated = findEntry(persisted, unrelatedId);
        if (operation == SyncWireOperation::Put) {
            const qint64 expectedAddedAt = sameKey ? 3000 : 2000;
            if (persistedTarget.value(QStringLiteral("addedAt")).toLongLong()
                != expectedAddedAt)
                return QStringLiteral("Collection PUT race did not persist the winning snapshot");
        } else if (sameKey) {
            if (persistedTarget.value(QStringLiteral("addedAt")).toLongLong() != 3000)
                return QStringLiteral("Collection DELETE race did not persist the local winner");
        } else if (!persistedTarget.isEmpty()) {
            return QStringLiteral("Collection DELETE race persisted a stale remote key");
        }
        if (!sameKey
            && persistedUnrelated.value(QStringLiteral("addedAt")).toLongLong() != 3000)
            return QStringLiteral("Collection race did not persist unrelated local work");
        return QString();
    };

    QVERIFY2(
        runCollectionRace(SyncWireOperation::Put, true, QStringLiteral("put-same")).isEmpty(),
        "Collection same-key PUT race failed");
    QVERIFY2(
        runCollectionRace(SyncWireOperation::Put, false, QStringLiteral("put-unrelated")).isEmpty(),
        "Collection unrelated-key PUT race failed");
    QVERIFY2(
        runCollectionRace(SyncWireOperation::Delete, true, QStringLiteral("delete-same")).isEmpty(),
        "Collection same-key DELETE race failed");
    QVERIFY2(
        runCollectionRace(SyncWireOperation::Delete, false, QStringLiteral("delete-unrelated")).isEmpty(),
        "Collection unrelated-key DELETE race failed");
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
progressPersistenceFailureDoesNotAdvanceCursor() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile = makeProfile(&temp);

    CoreFixtureService service;
    const QVariantMap entry = progressEntry(
        QStringLiteral("manga-disk-failure"),
        0.45,
        service.serverTimeMs);
    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::progress(entry);

    SyncWireMutation mutation;
    mutation.mutationId = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaab");
    mutation.deviceId = QString::fromLatin1(kDeviceB);
    mutation.category = QStringLiteral("continue_progress");
    mutation.recordKey = projected.recordKey;
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{service.serverTimeMs, 0, mutation.deviceId};
    mutation.operation = SyncWireOperation::Put;
    mutation.payload = projected.payload;
    const AccountTransportReply seeded =
        service.push(QJsonArray{syncWireMutationToJson(mutation)});
    QCOMPARE(seeded.statusCode, 200);

    CoreReplica replica(&service, profile, QString::fromLatin1(kDeviceA));

    // Replace the unopened INI file with a directory. The store loaded as a
    // legitimate empty owner, but its worker now cannot commit a snapshot.
    QFile::remove(profile.progressIniPath());
    QVERIFY(QDir().mkpath(profile.progressIniPath()));

    replica.engine.setNetworkEnabled(true);
    QTRY_COMPARE(replica.engine.state(), SyncEngine::State::Blocked);
    QCOMPARE(replica.engine.cursor(), quint64(0));
    QCOMPARE(replica.progress.syncEntries().size(), 0);

    SyncStateStore stateStore;
    QString error;
    const auto state = stateStore.load(profile.syncStatePath(), &error);
    QVERIFY2(state.has_value(), qPrintable(error));
    QCOMPARE(state->cursor, quint64(0));
    QCOMPARE(state->ownerRedos.size(), 1);
}

void tst_core_sync_adapters::
collectionPersistenceFailureDoesNotAdvanceCursor() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile = makeProfile(&temp);

    CoreFixtureService service;
    QVariantMap entry = collectionEntry(
        QStringLiteral("disk-failure"),
        1000);
    entry.insert(QStringLiteral("world"), QStringLiteral("tankoban"));
    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::collection(entry);

    SyncWireMutation mutation;
    mutation.mutationId = QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbc");
    mutation.deviceId = QString::fromLatin1(kDeviceB);
    mutation.category = QStringLiteral("collection");
    mutation.recordKey = projected.recordKey;
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{service.serverTimeMs, 0, mutation.deviceId};
    mutation.operation = SyncWireOperation::Put;
    mutation.payload = projected.payload;
    const AccountTransportReply seeded =
        service.push(QJsonArray{syncWireMutationToJson(mutation)});
    QCOMPARE(seeded.statusCode, 200);

    CoreReplica replica(&service, profile, QString::fromLatin1(kDeviceA));

    QFile::remove(profile.collectionIniPath());
    QVERIFY(QDir().mkpath(profile.collectionIniPath()));

    replica.engine.setNetworkEnabled(true);
    QTRY_COMPARE(replica.engine.state(), SyncEngine::State::Blocked);
    QCOMPARE(replica.engine.cursor(), quint64(0));
    QCOMPARE(replica.collection.syncEntries().size(), 0);

    SyncStateStore stateStore;
    QString error;
    const auto state = stateStore.load(profile.syncStatePath(), &error);
    QVERIFY2(state.has_value(), qPrintable(error));
    QCOMPARE(state->cursor, quint64(0));
    QCOMPARE(state->ownerRedos.size(), 1);
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
