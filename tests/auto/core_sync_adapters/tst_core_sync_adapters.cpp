// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "CollectionStore.h"
#include "ProgressStore.h"

#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/CollectionSyncAdapter.h"
#include "account/CoreStateSyncProjection.h"
#include "account/HistoryStore.h"
#include "account/ProfilePaths.h"
#include "account/ProfilePreferencesStore.h"
#include "account/RatingsReviewsConversionMap.h"
#include "account/RatingsReviewsConversionSyncAdapter.h"
#include "account/ProgressSyncAdapter.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"
#include "account/WatchStateSyncAdapter.h"
#include "stremio/StremioSync.h"
#include "stremio/StremioTheatreImporter.h"

#include <QDeadlineTimer>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

#include <utility>

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

// A bounded tagged-loopback datastore fixture.  It intentionally exposes only
// the request verb and an already-canonical library row: this composed test
// proves the relay settles against provider-current data without a write, not
// that a mock callback was invoked.
class FixtureDatastoreApi final {
public:
    FixtureDatastoreApi() {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                if (!socket)
                    return;
                m_clients.append(socket);
                QObject::connect(socket, &QTcpSocket::readyRead, [this, socket] {
                    m_request += socket->readAll();
                    if (m_replied.contains(socket)
                        || !m_request.contains(QByteArrayLiteral("\r\n\r\n"))) {
                        return;
                    }
                    m_replied.insert(socket);
                    const QByteArray body = QJsonDocument(QJsonObject{
                        {QStringLiteral("result"), m_result}}).toJson(QJsonDocument::Compact);
                    socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                        + QByteArray::number(body.size())
                        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                        + body);
                    // The composed relay test must observe the actual bounded
                    // datastore response, rather than a reply dropped while
                    // the fixture immediately closes the socket.
                    socket->flush();
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listen() {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    void setResult(const QJsonValue &result) {
        m_result = result;
    }

    QUrl endpoint() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/api").arg(m_server.serverPort()));
    }

    QByteArray request() const {
        return m_request;
    }

private:
    QTcpServer m_server;
    QList<QPointer<QTcpSocket>> m_clients;
    QSet<QTcpSocket *> m_replied;
    QByteArray m_request;
    QJsonValue m_result;
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
    WatchStateSyncAdapter watchStateAdapter;
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
          watchStateAdapter(
              &progress),
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

        if (!registry.registerAdapter(
                &watchStateAdapter)) {
            qFatal(
                "watch-state adapter registration failed");
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

struct ConversionCoreReplica {
    ProfilePaths profile;
    RatingsReviewsConversionTestHook hook;
    ProfilePreferencesStore preferences;
    CoreFixtureTransport transport;
    AccountClient client;
    SyncAdapterRegistry registry;
    RatingsReviewsConversionSyncAdapter adapter;
    SyncEngine engine;

    ConversionCoreReplica(
        CoreFixtureService *service,
        const ProfilePaths &profileValue,
        const QString &deviceId)
        : profile(profileValue),
          hook(RatingsReviewsConversionTestHook::syntheticDomains()),
          preferences(profile.preferencesIniPath(), hook),
          transport(service),
          client(&transport),
          adapter(&preferences),
          engine(&client, &registry) {
        QDir().mkpath(profile.profileRoot());
        client.setAccessToken(QByteArrayLiteral("fixture-access"));
        if (!registry.registerAdapter(&adapter))
            qFatal("conversion core adapter registration failed");
        engine.setAutomaticSchedulingEnabled(false);
        engine.setNetworkEnabled(false);
        QString error;
        if (!engine.start(profile, deviceId, &error))
            qFatal("conversion core engine start failed: %s", qPrintable(error));
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
    void ratingsReviewsConversionMapsUseExistingCoreEngineWithoutEcho();
    void collectionRemotePutPreservesLocalOnlyOverlay();
    void collectionAdapterRoundTripsAndTombstones();
    void progressSnapshotKeepsRawSiblingRecords();
    void progressSilentTickDoesNotEmitVisibleChanged();
    void repeatedSilentTicksAreThrottledNotIndefinitelyDebounced();
    void progressRemotePutPreservesLocalOnlyOverlay();
    void progressRemoteApplyPreservesTimestampWithoutEcho();
    void progressAsyncRemoteReceiptPersistsBeforeCallback();
    void progressLocalDurabilityReceiptUsesAsyncWriter();
    void collectionAsyncRemoteReceiptPersistsBeforeCallback();
    void progressRemoteApplyEmitsRemoteOnlyOwnerSignal();
    void corruptProgressStorageFailsClosed();
    void corruptCollectionStorageFailsClosed();
    void progressPersistenceFailureDoesNotAdvanceCursor();
    void watchStatePersistenceFailureDoesNotAdvanceCursor();
    void collectionPersistenceFailureDoesNotAdvanceCursor();
    void progressForgetDoesNotEraseHistory();
    void progressForgetWatchRemovalFailureDoesNotPublishDelete();
    void watchStateAdapterRoundTripsPortableState();
    void watchedActionTimestampPersistsAndRoundTrips();
    void watchedActionTimestampWinsOverTransportOrder();
    void resolvedWatchStateWinsEqualAndUnknownActionTies();
    void importedMovieWatchStateKeepsNonManualOwnerProvenanceAndActionTime();
    void stremioImporterAppliesCanonicalOwnersAfterDurableReceipts();
    void stremioImporterRepairsPresentationWithoutReplacingNewerProgress();
    void stremioMovieWatchStateStaysSeparateFromHistory();
    void stremioImporterUsesRegistryAndFencesProfileSwitch();
    void stremioEpisodeImportUsesExactEpisodesAndRejectsAmbiguity();
    void stremioExplicitDualRemovalJournalsBeforeLocalDelete();
    void stremioRemoteRemovalPersistsInverseDifferenceWithoutDeletingLocal();
    void stremioLocalOnlyRemovalSuppressesPassiveMembershipReimport();
    void stremioProviderRedoIsDurableBeforeOwnerAndClearsAfterCheckpoint();
    void stremioEpisodeRedoIsDurableBeforeOwnerAndClearsAfterCheckpoint();
    void stremioProviderRedoReplaysAfterRestartBeforeOwnerApply();
    void stremioProviderRedoReplaysAfterOwnerBeforeNeonCheckpoint();
    void providerImportCheckpointPersistsNeonOutboxAfterOwnerReceipt();
    void watchStateAdapterSkipsFilesystemIdentity();
    void twoReplicaCollectionConverges();
    void twoReplicaProgressConvergesAfterSilentOfflineTick();
    void stremioTwoReplicasProviderEqualitySettlesWithoutEcho();
};

void tst_core_sync_adapters::init() {
    qputenv("COLOSSEUM_APPDATA_TAG", "b8c_test_isolated");
}

void tst_core_sync_adapters::cleanup() {
    qunsetenv("COLOSSEUM_APPDATA_TAG");
}

void tst_core_sync_adapters::
ratingsReviewsConversionMapsUseExistingCoreEngineWithoutEcho() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    CoreFixtureService service;
    ConversionCoreReplica a(
        &service, makeProfile(&tempA), QString::fromLatin1(kDeviceA));
    ConversionCoreReplica b(
        &service, makeProfile(&tempB), QString::fromLatin1(kDeviceB));
    QSignalSpy bLocal(
        &b.registry, &SyncAdapterRegistry::localMutationAvailable);

    QString error;
    const auto map = RatingsReviewsConversionMap::recommended(
        QStringLiteral("fixture-a"),
        QStringLiteral("fixture-halfpoint-v1"),
        1,
        a.hook,
        &error);
    QVERIFY2(map.has_value(), qPrintable(error));
    QVERIFY(a.preferences.setRatingsReviewsConversionMap(*map));
    QTRY_COMPARE(a.engine.pendingOutboxCount(), 1);

    a.engine.setNetworkEnabled(true);
    QTRY_COMPARE(a.engine.pendingOutboxCount(), 0);
    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();

    QTRY_VERIFY(
        b.preferences.ratingsReviewsConversionMap(map->providerId).has_value());
    QCOMPARE(
        b.preferences.ratingsReviewsConversionMap(map->providerId)->digest(),
        map->digest());
    QCOMPARE(bLocal.count(), 0);
    QTRY_COMPARE(b.engine.pendingOutboxCount(), 0);
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
watchStateAdapterRoundTripsPortableState() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore source(
        QDir(temp.path()).filePath(
            QStringLiteral("watch-source.ini")));
    ProgressStore target(
        QDir(temp.path()).filePath(
            QStringLiteral("watch-target.ini")));
    source.setWatchedMark(
        QStringLiteral("tt900:s1:e4"),
        true);
    source.rememberLastSeason(
        QStringLiteral("tt900"),
        3);

    WatchStateSyncAdapter sourceAdapter(&source);
    WatchStateSyncAdapter targetAdapter(&target);
    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        sourceAdapter.exportSnapshot(&snapshot, &error),
        qPrintable(error));
    QCOMPARE(snapshot.records.size(), 2);
    for (const SyncAdapterRecord &record : snapshot.records) {
        SyncAdapterValidationError validation;
        QVERIFY2(
            targetAdapter.validateRemote(
                record.recordKey,
                SyncWireOperation::Put,
                record.payload,
                1,
                &validation),
            qPrintable(validation.detail));
        QVERIFY2(
            targetAdapter.applyRemote(
                record.recordKey,
                SyncWireOperation::Put,
                record.payload,
                1,
                &error),
            qPrintable(error));
    }

    QCOMPARE(
        target.watchedMark(QStringLiteral("tt900:s1:e4")),
        1);
    QCOMPARE(
        target.lastSeason(QStringLiteral("tt900")),
        3);

    const QString markKey =
        CoreStateSyncProjection::watchedMarkKey(
            QStringLiteral("tt900"));
    const QString seasonKey =
        CoreStateSyncProjection::lastSeasonKey(
            QStringLiteral("tt900"));
    QVERIFY(targetAdapter.applyRemote(
        markKey,
        SyncWireOperation::Delete,
        QJsonValue(),
        1,
        &error));
    QVERIFY(targetAdapter.applyRemote(
        seasonKey,
        SyncWireOperation::Delete,
        QJsonValue(),
        1,
        &error));
    QCOMPARE(
        target.watchedMark(QStringLiteral("tt900:s1:e4")),
        0);
    QCOMPARE(
        target.lastSeason(QStringLiteral("tt900")),
        -1);
}

void tst_core_sync_adapters::resolvedWatchStateWinsEqualAndUnknownActionTies() {
    // A Core/Neon record has already resolved its HLC/action-time winner.
    // Arrival order at the losing owner must not retain a contradictory mark.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString id = QStringLiteral("tt-resolved-watch-tie");
    const QString recordKey = CoreStateSyncProjection::watchedMarkKey(id);
    const QJsonObject timedWinner{
        {QStringLiteral("id"), id},
        {QStringLiteral("mark"), -1},
        {QStringLiteral("actionAtMs"), QStringLiteral("5000")}};
    const QJsonObject unknownWinner{
        {QStringLiteral("id"), id},
        {QStringLiteral("mark"), -1}};

    ProgressStore timedA(QDir(temp.path()).filePath(QStringLiteral("timed-a.ini")));
    ProgressStore timedB(QDir(temp.path()).filePath(QStringLiteral("timed-b.ini")));
    QVERIFY(timedA.applySyncedWatchedMark(id, 1, 5000));
    QVERIFY(timedB.applySyncedWatchedMark(id, -1, 5000));
    WatchStateSyncAdapter timedAdapter(&timedA);
    QString error;
    QVERIFY2(timedAdapter.applyRemote(recordKey, SyncWireOperation::Put,
                                      timedWinner, 1, &error), qPrintable(error));
    QCOMPARE(timedA.watchedMark(id), -1);
    QCOMPARE(timedB.watchedMark(id), -1);

    ProgressStore unknownA(QDir(temp.path()).filePath(QStringLiteral("unknown-a.ini")));
    ProgressStore unknownB(QDir(temp.path()).filePath(QStringLiteral("unknown-b.ini")));
    QVERIFY(unknownA.applySyncedWatchedMark(id, 1));
    QVERIFY(unknownB.applySyncedWatchedMark(id, -1));
    WatchStateSyncAdapter unknownAdapter(&unknownA);
    QVERIFY2(unknownAdapter.applyRemote(recordKey, SyncWireOperation::Put,
                                        unknownWinner, 1, &error), qPrintable(error));
    QCOMPARE(unknownA.watchedMark(id), -1);
    QCOMPARE(unknownB.watchedMark(id), -1);
}

void tst_core_sync_adapters::importedMovieWatchStateKeepsNonManualOwnerProvenanceAndActionTime() {
    // The Stremio flag is a current provider fact, not a local Library
    // override. The owner retains that distinction alongside the cumulative
    // History completion so LibraryApi can compare its real action time.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.activate(QStringLiteral("profile-a"));
    StremioLibraryItem item;
    item.id = QStringLiteral("tt-provider-current");
    item.type = QStringLiteral("movie");
    item.raw = QJsonObject{{QStringLiteral("_id"), item.id},
                           {QStringLiteral("type"), item.type},
                           {QStringLiteral("state"), QJsonObject{
                               {QStringLiteral("flaggedWatched"), 1},
                               {QStringLiteral("lastWatched"),
                                QStringLiteral("2025-01-02T03:04:05.000Z")}}}};
    bool finished = false;
    QString error;
    QVERIFY(importer.apply(item, [&finished, &error](bool committed, const QString &message) {
        finished = committed;
        error = message;
    }));
    QTRY_VERIFY2(finished, qPrintable(error));
    QCOMPARE(progress.watchedMark(item.id), 1);
    QVERIFY(!progress.watchedMarkIsManual(item.id));
    QCOMPARE(progress.watchedMarkActionAt(item.id), qint64(1735787045000));
    QCOMPARE(history.get(QStringLiteral("movie"), item.id).value(
                 QStringLiteral("completedAt")).toLongLong(), qint64(1735787045000));

    progress.setWatchedMark(item.id, true);
    QVERIFY(progress.watchedMarkIsManual(item.id));
}

void tst_core_sync_adapters::stremioImporterAppliesCanonicalOwnersAfterDurableReceipts() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));

    const QString itemId = QStringLiteral("tt-stremio-import");
    QVariantMap local;
    local.insert(QStringLiteral("kind"), QStringLiteral("video"));
    local.insert(QStringLiteral("id"), itemId);
    local.insert(QStringLiteral("progress"), 0.2);
    local.insert(QStringLiteral("updatedAt"), qint64(1000));
    local.insert(QStringLiteral("resume"), QVariantMap{
        {QStringLiteral("localPath"), QStringLiteral("C:/private/movie.mkv")},
        {QStringLiteral("position"), 20.0}});
    QVERIFY(progress.applySyncedEntry(local));
    progress.flush();
    QVERIFY(!progress.get(QStringLiteral("video"), itemId).isEmpty());

    StremioLibraryItem item;
    item.id = itemId;
    item.type = QStringLiteral("movie");
    item.libraryMember = true;
    item.raw = QJsonObject{
        {QStringLiteral("_id"), item.id},
        {QStringLiteral("type"), item.type},
        {QStringLiteral("name"), QStringLiteral("Imported Film")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), item.id},
            {QStringLiteral("timeOffset"), 120500},
            {QStringLiteral("duration"), 300000},
            {QStringLiteral("flaggedWatched"), 1},
            {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:04:05.000Z")}}}};

    QSignalSpy collectionDirty(&collection, &CollectionStore::syncDirty);
    QSignalSpy progressDirty(&progress, &ProgressStore::syncDirty);
    QSignalSpy historyDirty(&history, &HistoryStore::syncDirty);
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.activate(QStringLiteral("profile-a"));
    int checkpointCalls = 0;
    importer.setNeonCheckpoint([&](StremioTheatreImporter::Completion completion) {
        ++checkpointCalls;
        QVERIFY(collection.has(QStringLiteral("theatre"), itemId));
        QVERIFY(!progress.get(QStringLiteral("video"), itemId).isEmpty());
        QVERIFY(!history.get(QStringLiteral("movie"), itemId).isEmpty());
        completion(true, {});
    });
    bool completed = false;
    QString completionError;
    QVERIFY(importer.apply(item, [&completed, &completionError](bool ok, const QString &error) {
        completed = ok;
        completionError = error;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(completionError));
    QVERIFY(collection.has(QStringLiteral("theatre"), itemId));
    const QVariantMap imported = progress.get(QStringLiteral("video"), itemId);
    QCOMPARE(imported.value(QStringLiteral("updatedAt")).toLongLong(), qint64(1735787045000));
    QCOMPARE(imported.value(QStringLiteral("resume")).toMap().value(
                 QStringLiteral("position")).toDouble(), 120.5);
    QCOMPARE(imported.value(QStringLiteral("resume")).toMap().value(
                 QStringLiteral("localPath")).toString(), QStringLiteral("C:/private/movie.mkv"));
    QCOMPARE(history.get(QStringLiteral("movie"), itemId).value(
                 QStringLiteral("source")).toString(), QStringLiteral("stremio"));
    QCOMPARE(collectionDirty.count(), 0);
    QCOMPARE(progressDirty.count(), 0);
    QCOMPARE(historyDirty.count(), 0);
    QCOMPARE(checkpointCalls, 1);

    item.raw.insert(QStringLiteral("state"), QJsonObject{
        {QStringLiteral("video_id"), item.id},
        {QStringLiteral("timeOffset"), 2000},
        {QStringLiteral("duration"), 300000},
        {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:03:05.000Z")}});
    completed = false;
    QVERIFY(importer.apply(item, [&completed, &completionError](bool ok, const QString &error) {
        completed = ok;
        completionError = error;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(completionError));
    QCOMPARE(progress.get(QStringLiteral("video"), itemId).value(
                 QStringLiteral("resume")).toMap().value(QStringLiteral("position")).toDouble(), 120.5);
}

void tst_core_sync_adapters::stremioImporterRepairsPresentationWithoutReplacingNewerProgress() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));

    const QString itemId = QStringLiteral("tt1234567");
    QVERIFY(progress.applySyncedEntry(QVariantMap{
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("id"), itemId},
        {QStringLiteral("progress"), 0.8},
        {QStringLiteral("updatedAt"), qint64(2000000000000)},
        {QStringLiteral("resume"), QVariantMap{
            {QStringLiteral("localPath"), QStringLiteral("C:/private/newer.mkv")},
            {QStringLiteral("position"), 240.0}}}}));
    progress.flush();

    StremioLibraryItem item;
    item.id = itemId;
    item.type = QStringLiteral("movie");
    item.raw = QJsonObject{
        {QStringLiteral("_id"), item.id},
        {QStringLiteral("type"), item.type},
        {QStringLiteral("name"), QStringLiteral("Provider Film")},
        {QStringLiteral("poster"), QStringLiteral("https://images.example.test/provider-film.jpg")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), item.id},
            {QStringLiteral("timeOffset"), 120500},
            {QStringLiteral("duration"), 300000},
            {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:04:05.000Z")}}}};

    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.activate(QStringLiteral("profile-a"));
    bool completed = false;
    QString completionError;
    QVERIFY(importer.apply(item, [&completed, &completionError](bool ok, const QString &error) {
        completed = ok;
        completionError = error;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(completionError));

    const QVariantMap repaired = progress.get(QStringLiteral("video"), itemId);
    QCOMPARE(repaired.value(QStringLiteral("progress")).toDouble(), 0.8);
    QCOMPARE(repaired.value(QStringLiteral("updatedAt")).toLongLong(), qint64(2000000000000));
    QCOMPARE(repaired.value(QStringLiteral("resume")).toMap().value(
                 QStringLiteral("position")).toDouble(), 240.0);
    QCOMPARE(repaired.value(QStringLiteral("resume")).toMap().value(
                 QStringLiteral("localPath")).toString(), QStringLiteral("C:/private/newer.mkv"));
    QCOMPARE(repaired.value(QStringLiteral("title")).toString(), QStringLiteral("Provider Film"));
    QCOMPARE(repaired.value(QStringLiteral("caption")).toString(), QStringLiteral("Provider Film"));
    QCOMPARE(repaired.value(QStringLiteral("cover")).toString(),
             QStringLiteral("https://images.example.test/provider-film.jpg"));
}

void tst_core_sync_adapters::stremioMovieWatchStateStaysSeparateFromHistory() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.activate(QStringLiteral("profile-a"));

    StremioLibraryItem movie;
    movie.id = QStringLiteral("tt-current-watch");
    movie.type = QStringLiteral("movie");
    movie.raw = QJsonObject{{QStringLiteral("_id"), movie.id},
                            {QStringLiteral("type"), movie.type},
                            {QStringLiteral("state"), QJsonObject{
                                {QStringLiteral("flaggedWatched"), 1},
                                {QStringLiteral("lastWatched"),
                                 QStringLiteral("2025-01-02T03:04:05.000Z")}}}};
    bool completed = false;
    QString error;
    QVERIFY(importer.apply(movie, [&completed, &error](bool committed, const QString &message) {
        completed = committed;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QCOMPARE(progress.watchedMark(movie.id), 1);
    QVERIFY(!history.get(QStringLiteral("movie"), movie.id).isEmpty());

    // A newer explicit unwatch changes the current movie state while the
    // cumulative completion record remains a separate owner.
    movie.raw.insert(QStringLiteral("state"), QJsonObject{
        {QStringLiteral("flaggedWatched"), 0},
        {QStringLiteral("lastWatched"), QStringLiteral("2025-01-03T03:04:05.000Z")}});
    completed = false;
    QVERIFY(importer.apply(movie, [&completed, &error](bool committed, const QString &message) {
        completed = committed;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QCOMPARE(progress.watchedMark(movie.id), -1);
    QVERIFY(!history.get(QStringLiteral("movie"), movie.id).isEmpty());

    // A legitimate watched state without a provider date must not synthesize
    // a History event, but remains valid current state.
    StremioLibraryItem undated = movie;
    undated.id = QStringLiteral("tt-undated-watch");
    undated.raw.insert(QStringLiteral("_id"), undated.id);
    undated.raw.insert(QStringLiteral("state"), QJsonObject{
        {QStringLiteral("flaggedWatched"), 1}});
    completed = false;
    QVERIFY(importer.apply(undated, [&completed, &error](bool committed, const QString &message) {
        completed = committed;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QCOMPARE(progress.watchedMark(undated.id), 1);
    QVERIFY(history.get(QStringLiteral("movie"), undated.id).isEmpty());
}

void tst_core_sync_adapters::stremioImporterUsesRegistryAndFencesProfileSwitch() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    CollectionSyncAdapter collectionAdapter(&collection);
    ProgressSyncAdapter progressAdapter(&progress, nullptr, 1);
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&collectionAdapter));
    QVERIFY(registry.registerAdapter(&progressAdapter));

    StremioLibraryItem item;
    item.id = QStringLiteral("tt-stremio-registry");
    item.type = QStringLiteral("movie");
    item.libraryMember = true;
    item.raw = QJsonObject{
        {QStringLiteral("_id"), item.id},
        {QStringLiteral("type"), item.type},
        {QStringLiteral("name"), QStringLiteral("Registry Film")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), item.id},
            {QStringLiteral("timeOffset"), 90000},
            {QStringLiteral("duration"), 300000}}}};

    // The production AccountRuntime owns this exact registry. A provider
    // import must traverse it so owner commits have ordinary remote-applied
    // receipts rather than bypassing registry fences and echo suppression.
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.setSyncAdapterRegistry(&registry);
    importer.activate(QStringLiteral("profile-a"));
    QSignalSpy remoteApplied(&registry, &SyncAdapterRegistry::remoteApplied);
    int checkpoints = 0;
    importer.setNeonCheckpoint([&](StremioTheatreImporter::Completion completion) {
        ++checkpoints;
        completion(true, {});
    });

    bool completed = false;
    QString completionError;
    QVERIFY(importer.apply(item, [&completed, &completionError](bool ok, const QString &error) {
        completed = ok;
        completionError = error;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(completionError));
    QCOMPARE(remoteApplied.count(), 2);
    QCOMPARE(checkpoints, 1);

    item.raw.insert(QStringLiteral("state"), QJsonObject{
        {QStringLiteral("video_id"), item.id},
        {QStringLiteral("timeOffset"), 120000},
        {QStringLiteral("duration"), 300000}});
    bool staleCompleted = true;
    QVERIFY(importer.apply(item, [&staleCompleted](bool ok, const QString &) {
        staleCompleted = ok;
    }));
    importer.activate(QStringLiteral("profile-b"));
    const QDeadlineTimer deadline(5000);
    while (staleCompleted && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QVERIFY(!staleCompleted);
    // The stale receipt cannot pass the checkpoint for B's active binding.
    QCOMPARE(checkpoints, 1);
}

void tst_core_sync_adapters::providerImportCheckpointPersistsNeonOutboxAfterOwnerReceipt() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CoreFixtureService service;
    CoreReplica replica(&service, makeProfile(&temp), QString::fromLatin1(kDeviceA));

    QVariantMap imported;
    imported.insert(QStringLiteral("kind"), QStringLiteral("video"));
    imported.insert(QStringLiteral("id"), QStringLiteral("tt-provider-checkpoint"));
    imported.insert(QStringLiteral("progress"), 0.4);
    imported.insert(QStringLiteral("updatedAt"), qint64(1735787045000));
    imported.insert(QStringLiteral("resume"), QVariantMap{
        {QStringLiteral("position"), 120.5}});
    QVERIFY(replica.progress.applySyncedEntry(imported));
    replica.progress.flush();
    QCOMPARE(replica.engine.pendingOutboxCount(), 0);

    bool checkpointed = false;
    QString checkpointError;
    QVERIFY(replica.engine.checkpointProviderImport(
        [&checkpointed, &checkpointError](bool committed, const QString &error) {
            checkpointed = committed;
            checkpointError = error;
        }));
    QVERIFY2(waitForAsyncFlag(checkpointed), qPrintable(checkpointError));
    QVERIFY(replica.engine.pendingOutboxCount() >= 1);
}

void tst_core_sync_adapters::stremioEpisodeImportUsesExactEpisodesAndRejectsAmbiguity() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    ProgressSyncAdapter progressAdapter(&progress, nullptr, 1);
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&progressAdapter));
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.setSyncAdapterRegistry(&registry);
    importer.activate(QStringLiteral("profile-a"));

    StremioLibraryItem series;
    series.id = QStringLiteral("kitsu:alpha");
    series.type = QStringLiteral("series");
    const QList<StremioEpisodeIdentity> videos{
        {QStringLiteral("kitsu:alpha:s0:e1"), 0, 1},
        {QStringLiteral("kitsu:alpha:s1:e1"), 1, 1},
        {QStringLiteral("kitsu:alpha:s1:e2"), 1, 2},
        {QStringLiteral("kitsu:alpha:s2:e1"), 2, 1}};

    QSignalSpy dirty(&progress, &ProgressStore::syncDirty);
    QSignalSpy remoteApplied(&registry, &SyncAdapterRegistry::remoteApplied);
    bool completed = false;
    QString completionError;
    QVERIFY(importer.applyWatchedEpisodes(
        series,
        QStringLiteral("kitsu:alpha:s2:e1:4:eJzjBAAACgAK"),
        videos,
        [&completed, &completionError](bool ok, const QString &error) {
            completed = ok;
            completionError = error;
        }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(completionError));
    QCOMPARE(progress.watchedMark(series.id), 0);
    QCOMPARE(progress.get(QStringLiteral("video"), QStringLiteral("kitsu:alpha:s0:e1"))
                 .value(QStringLiteral("progress")).toDouble(), 1.0);
    QCOMPARE(progress.get(QStringLiteral("video"), QStringLiteral("kitsu:alpha:s2:e1"))
                 .value(QStringLiteral("progress")).toDouble(), 1.0);
    QVERIFY(progress.get(QStringLiteral("video"), QStringLiteral("kitsu:alpha:s1:e1")).isEmpty());
    QCOMPARE(dirty.count(), 0);
    QCOMPARE(remoteApplied.count(), 2);

    QList<StremioEpisodeIdentity> ambiguous = videos;
    ambiguous.append(videos.first());
    QVERIFY(!importer.applyWatchedEpisodes(
        series,
        QStringLiteral("kitsu:alpha:s2:e1:4:eJzjBAAACgAK"),
        ambiguous,
        {}));
    QCOMPARE(progress.watchedMark(series.id), 0);
    QVERIFY(progress.get(QStringLiteral("video"), QStringLiteral("kitsu:alpha:s1:e1")).isEmpty());

    // The compressed field can only be interpreted against the exact series
    // metadata list. A mixed-series list is ambiguous even when its anchor
    // happens to be valid, and must not write either series.
    QList<StremioEpisodeIdentity> mixed = videos;
    mixed.append({QStringLiteral("kitsu:other:s1:e4"), 1, 4});
    QVERIFY(!importer.applyWatchedEpisodes(
        series,
        QStringLiteral("kitsu:alpha:s2:e1:4:eJzjBAAACgAK"),
        mixed,
        {}));
    QVERIFY(progress.get(QStringLiteral("video"), QStringLiteral("kitsu:other:s1:e4")).isEmpty());
}

void tst_core_sync_adapters::stremioExplicitDualRemovalJournalsBeforeLocalDelete() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    const QString id = QStringLiteral("kitsu:explicit-removal");
    QVERIFY(collection.add(QStringLiteral("theatre"), QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), QStringLiteral("series")},
        {QStringLiteral("title"), QStringLiteral("Keep progress")}}));

    const QString statePath = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), statePath, false));
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.activate(QStringLiteral("profile-a"));

    bool completed = false;
    QString error;
    QVERIFY(importer.removeFromColosseumAndStremio(
        &sync,
        id,
        [&completed, &error](bool ok, const QString &message) {
            completed = ok;
            error = message;
        }));
    // The user-visible owner must remain unchanged until the private remote
    // removal intent is on disk; a provider outage cannot lose the retry.
    QVERIFY(collection.has(QStringLiteral("theatre"), id));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QVERIFY(!collection.has(QStringLiteral("theatre"), id));
    QCOMPARE(sync.pendingCount(), 1);
    QVERIFY(progress.get(QStringLiteral("video"), id).isEmpty());

    // The private explicit-removal intent is durable but Stremio has not yet
    // acknowledged it. A passive provider membership must not resurrect the
    // local Collection row during that retry window.
    importer.setStremioSync(&sync);
    StremioLibraryItem passiveMember;
    passiveMember.id = id;
    passiveMember.type = QStringLiteral("series");
    passiveMember.libraryMember = true;
    passiveMember.raw = QJsonObject{{QStringLiteral("_id"), id},
                                    {QStringLiteral("type"), QStringLiteral("series")},
                                    {QStringLiteral("state"), QJsonObject{}}};
    completed = false;
    QVERIFY(importer.apply(passiveMember, [&completed, &error](bool ok, const QString &message) {
        completed = ok;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QVERIFY(!collection.has(QStringLiteral("theatre"), id));
}

void tst_core_sync_adapters::stremioRemoteRemovalPersistsInverseDifferenceWithoutDeletingLocal() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    const QString id = QStringLiteral("tt-remote-removal");
    QVERIFY(collection.add(QStringLiteral("theatre"), QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), QStringLiteral("movie")}}));
    QSignalSpy dirty(&collection, &CollectionStore::syncDirty);

    StremioSync sync;
    const QString statePath = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), statePath, false));
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.setStremioSync(&sync);
    importer.activate(QStringLiteral("profile-a"));

    StremioLibraryItem removed;
    removed.id = id;
    removed.type = QStringLiteral("movie");
    removed.removed = true;
    bool completed = false;
    QString error;
    QVERIFY(importer.apply(removed, [&completed, &error](bool ok, const QString &message) {
        completed = ok;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    // Passive provider removal never becomes a local Collection delete or a
    // synthetic provider delete; the inverse difference fences re-add logic.
    QVERIFY(collection.has(QStringLiteral("theatre"), id));
    QCOMPARE(dirty.count(), 0);
    QCOMPARE(sync.pendingCount(), 0);

    StremioState inspector;
    const auto state = inspector.load(statePath);
    QVERIFY(state.has_value());
    QCOMPARE(state->intentionalMembershipDifferences.size(), 1);
    const QJsonObject difference = state->intentionalMembershipDifferences.first().toObject();
    QCOMPARE(difference.value(QStringLiteral("id")).toString(), id);
    QVERIFY(difference.value(QStringLiteral("localPresent")).toBool());
    QVERIFY(!difference.value(QStringLiteral("remotePresent")).toBool());
    QVERIFY(!difference.value(QStringLiteral("explicitRemoteRemoval")).toBool());
}

void tst_core_sync_adapters::stremioLocalOnlyRemovalSuppressesPassiveMembershipReimport() {
    // This fails if a later passive provider pull can undo an explicit local
    // library removal. The durable difference, not arrival order, owns that
    // decision until an explicit re-add clears it.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    const QString id = QStringLiteral("tt-local-only-removal");
    StremioSync sync;
    QVERIFY(sync.activateProfile(
        QStringLiteral("profile-a"),
        QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")),
        false));
    bool differenceDurable = false;
    QVERIFY(sync.recordLocalOnlyLibraryRemoval(
        id, QStringLiteral("movie"), [&differenceDurable](bool committed) {
            differenceDurable = committed;
        }));
    QVERIFY(waitForAsyncFlag(differenceDurable));

    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.setStremioSync(&sync);
    importer.activate(QStringLiteral("profile-a"));
    StremioLibraryItem member;
    member.id = id;
    member.type = QStringLiteral("movie");
    member.libraryMember = true;
    member.raw = QJsonObject{
        {QStringLiteral("_id"), id},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("name"), QStringLiteral("Do not silently re-add")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), id},
            {QStringLiteral("timeOffset"), 120000},
            {QStringLiteral("duration"), 300000},
            {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:04:05.000Z")}}}};
    bool imported = false;
    QString error;
    QVERIFY(importer.apply(member, [&imported, &error](bool committed, const QString &message) {
        imported = committed;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(imported), qPrintable(error));
    QVERIFY(!collection.has(QStringLiteral("theatre"), id));
    QVERIFY(!progress.get(QStringLiteral("video"), id).isEmpty());

    // The suppression belongs to A's durable state, not to the importer
    // instance. A restart keeps it, while B's clean profile remains free to
    // merge the same provider member.
    importer.deactivate();
    sync.deactivateProfile();
    StremioSync reopened;
    QVERIFY(reopened.activateProfile(
        QStringLiteral("profile-a"),
        QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")),
        false));
    StremioTheatreImporter restarted(&collection, &progress, &history);
    restarted.setStremioSync(&reopened);
    restarted.activate(QStringLiteral("profile-a"));
    imported = false;
    QVERIFY(restarted.apply(member, [&imported, &error](bool committed, const QString &message) {
        imported = committed;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(imported), qPrintable(error));
    QVERIFY(!collection.has(QStringLiteral("theatre"), id));

    CollectionStore otherCollection(QDir(temp.path()).filePath(QStringLiteral("other-collection.ini")));
    ProgressStore otherProgress(QDir(temp.path()).filePath(QStringLiteral("other-progress.ini")));
    HistoryStore otherHistory(QDir(temp.path()).filePath(QStringLiteral("other-history.ini")));
    StremioSync other;
    QVERIFY(other.activateProfile(
        QStringLiteral("profile-b"),
        QDir(temp.path()).filePath(QStringLiteral("other-stremio-sync.json")),
        false));
    StremioTheatreImporter otherImporter(&otherCollection, &otherProgress, &otherHistory);
    otherImporter.setStremioSync(&other);
    otherImporter.activate(QStringLiteral("profile-b"));
    imported = false;
    QVERIFY(otherImporter.apply(member, [&imported, &error](bool committed, const QString &message) {
        imported = committed;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(imported), qPrintable(error));
    QVERIFY(otherCollection.has(QStringLiteral("theatre"), id));
}

void tst_core_sync_adapters::stremioProviderRedoIsDurableBeforeOwnerAndClearsAfterCheckpoint() {
    // This catches a crash window where a provider record reaches a canonical
    // owner before the local redo journal can replay it after restart.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    const QString statePath = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), statePath, false));
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.setStremioSync(&sync);
    importer.activate(QStringLiteral("profile-a"));

    bool redoPresentAtCheckpoint = false;
    importer.setNeonCheckpoint([&](StremioTheatreImporter::Completion completion) {
        StremioState inspector;
        const auto state = inspector.load(statePath);
        redoPresentAtCheckpoint = state.has_value() && !state->importRedoReceipts.isEmpty();
        completion(true, {});
    });
    StremioLibraryItem item;
    item.id = QStringLiteral("tt-provider-redo");
    item.type = QStringLiteral("movie");
    item.libraryMember = true;
    item.raw = QJsonObject{
        {QStringLiteral("_id"), item.id},
        {QStringLiteral("type"), item.type},
        {QStringLiteral("name"), QStringLiteral("Redo Film")},
        {QStringLiteral("state"), QJsonObject{}}};
    bool completed = false;
    QString error;
    QVERIFY(importer.apply(item, [&completed, &error](bool committed, const QString &message) {
        completed = committed;
        error = message;
    }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QVERIFY(collection.has(QStringLiteral("theatre"), item.id));
    QVERIFY(redoPresentAtCheckpoint);
    StremioState inspector;
    const auto settled = inspector.load(statePath);
    QVERIFY(settled.has_value());
    QVERIFY(settled->importRedoReceipts.isEmpty());
}

void tst_core_sync_adapters::stremioProviderRedoReplaysAfterRestartBeforeOwnerApply() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    const QString statePath = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));

    StremioLibraryItem item;
    item.id = QStringLiteral("tt-redo-restart");
    item.type = QStringLiteral("movie");
    item.libraryMember = true;
    item.raw = QJsonObject{
        {QStringLiteral("_id"), item.id},
        {QStringLiteral("type"), item.type},
        {QStringLiteral("name"), QStringLiteral("Restart-safe film")},
        {QStringLiteral("state"), QJsonObject{}}};

    StremioSync interrupted;
    QVERIFY(interrupted.activateProfile(QStringLiteral("profile-a"), statePath, false));
    StremioTheatreImporter beforeCrash(&collection, &progress, &history);
    beforeCrash.setStremioSync(&interrupted);
    beforeCrash.activate(QStringLiteral("profile-a"));
    QVERIFY(beforeCrash.apply(item, {}));
    // Simulate process loss after the private redo write is accepted but
    // before its asynchronous owner continuation can apply Collection.
    beforeCrash.deactivate();
    StremioState inspector;
    QTRY_VERIFY([&] {
        const auto state = inspector.load(statePath);
        return state.has_value() && state->importRedoReceipts.size() == 1;
    }());
    QVERIFY(!collection.has(QStringLiteral("theatre"), item.id));

    interrupted.deactivateProfile();
    StremioSync reopened;
    QVERIFY(reopened.activateProfile(QStringLiteral("profile-a"), statePath, false));
    const auto redos = reopened.pendingProviderImports();
    QCOMPARE(redos.size(), 1);
    StremioTheatreImporter replayed(&collection, &progress, &history);
    replayed.setStremioSync(&reopened);
    replayed.activate(QStringLiteral("profile-a"));
    bool completed = false;
    QString error;
    QVERIFY(replayed.replayProviderImport(
        redos.first(),
        [&completed, &error](bool committed, const QString &message) {
            completed = committed;
            error = message;
        }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QVERIFY(collection.has(QStringLiteral("theatre"), item.id));
    QTRY_VERIFY(reopened.pendingProviderImports().isEmpty());
    QVERIFY(!replayed.replayProviderImport(redos.first(), {}));
}

void tst_core_sync_adapters::stremioProviderRedoReplaysAfterOwnerBeforeNeonCheckpoint() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    const QString statePath = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    std::function<void(bool, const QString &)> heldCheckpoint;

    {
        StremioSync beforeCrash;
        QVERIFY(beforeCrash.activateProfile(QStringLiteral("profile-a"), statePath, false));
        StremioTheatreImporter importer(&collection, &progress, &history);
        importer.setStremioSync(&beforeCrash);
        importer.activate(QStringLiteral("profile-a"));
        importer.setNeonCheckpoint([&heldCheckpoint](StremioTheatreImporter::Completion completion) {
            heldCheckpoint = std::move(completion);
        });
        StremioLibraryItem item;
        item.id = QStringLiteral("tt-redo-after-owner");
        item.type = QStringLiteral("movie");
        item.libraryMember = true;
        item.raw = QJsonObject{
            {QStringLiteral("_id"), item.id},
            {QStringLiteral("type"), item.type},
            {QStringLiteral("name"), QStringLiteral("Owner committed")},
            {QStringLiteral("state"), QJsonObject{}}};
        QVERIFY(importer.apply(item, {}));
        QTRY_VERIFY(collection.has(QStringLiteral("theatre"), item.id));
        QVERIFY(heldCheckpoint);
        StremioState inspector;
        QTRY_VERIFY([&] {
            const auto state = inspector.load(statePath);
            return state.has_value() && state->importRedoReceipts.size() == 1;
        }());
    }

    StremioSync reopened;
    QVERIFY(reopened.activateProfile(QStringLiteral("profile-a"), statePath, false));
    const auto redos = reopened.pendingProviderImports();
    QCOMPARE(redos.size(), 1);
    StremioTheatreImporter replayed(&collection, &progress, &history);
    replayed.setStremioSync(&reopened);
    replayed.activate(QStringLiteral("profile-a"));
    bool completed = false;
    QString error;
    QVERIFY(replayed.replayProviderImport(
        redos.first(),
        [&completed, &error](bool committed, const QString &message) {
            completed = committed;
            error = message;
        }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QVERIFY(collection.has(QStringLiteral("theatre"), QStringLiteral("tt-redo-after-owner")));
    QTRY_VERIFY(reopened.pendingProviderImports().isEmpty());
}

void tst_core_sync_adapters::stremioEpisodeRedoIsDurableBeforeOwnerAndClearsAfterCheckpoint() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    CollectionStore collection(QDir(temp.path()).filePath(QStringLiteral("collection.ini")));
    ProgressStore progress(QDir(temp.path()).filePath(QStringLiteral("progress.ini")));
    HistoryStore history(QDir(temp.path()).filePath(QStringLiteral("history.ini")));
    const QString statePath = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), statePath, false));
    StremioTheatreImporter importer(&collection, &progress, &history);
    importer.setStremioSync(&sync);
    importer.activate(QStringLiteral("profile-a"));
    bool redoPresentAtCheckpoint = false;
    importer.setNeonCheckpoint([&](StremioTheatreImporter::Completion completion) {
        StremioState inspector;
        const auto state = inspector.load(statePath);
        redoPresentAtCheckpoint = state.has_value() && !state->importRedoReceipts.isEmpty();
        completion(true, {});
    });
    StremioLibraryItem series;
    series.id = QStringLiteral("kitsu:redo");
    series.type = QStringLiteral("series");
    const QList<StremioEpisodeIdentity> videos{
        {QStringLiteral("kitsu:redo:s1:e1"), 1, 1}};
    QString watched;
    QString encodeError;
    QVERIFY2(StremioCodec::encodeWatchedEpisodes(
        QSet<QString>{QStringLiteral("kitsu:redo:s1:e1")}, videos, &watched, &encodeError),
        qPrintable(encodeError));
    bool completed = false;
    QString error;
    QVERIFY(importer.applyWatchedEpisodes(
        series, watched, videos, [&completed, &error](bool committed, const QString &message) {
            completed = committed;
            error = message;
        }));
    QVERIFY2(waitForAsyncFlag(completed), qPrintable(error));
    QVERIFY(redoPresentAtCheckpoint);
    StremioState inspector;
    const auto settled = inspector.load(statePath);
    QVERIFY(settled.has_value());
    QVERIFY(settled->importRedoReceipts.isEmpty());
}

void tst_core_sync_adapters::
watchedActionTimestampPersistsAndRoundTrips() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString sourcePath = QDir(temp.path()).filePath(
        QStringLiteral("watch-source.ini"));
    const QString targetPath = QDir(temp.path()).filePath(
        QStringLiteral("watch-target.ini"));
    const QString legacyPath = QDir(temp.path()).filePath(
        QStringLiteral("watch-legacy.ini"));
    const QString id = QStringLiteral("tt900:s1:e4");

    qint64 actionAtMs = 0;
    SyncAdapterRecord exported;
    {
        ProgressStore source(sourcePath);
        source.setWatchedMark(id, true);
        WatchStateSyncAdapter sourceAdapter(&source);
        SyncAdapterExport snapshot;
        QString error;
        QVERIFY2(sourceAdapter.exportSnapshot(&snapshot, &error), qPrintable(error));
        QCOMPARE(snapshot.records.size(), 1);
        exported = snapshot.records.first();
        QCOMPARE(exported.payload.toObject().value(QStringLiteral("id")).toString(), QStringLiteral("tt900"));
        QCOMPARE(exported.payload.toObject().value(QStringLiteral("mark")).toInt(), 1);
        QVERIFY(exported.payload.toObject().contains(QStringLiteral("actionAtMs")));
        actionAtMs = exported.payload.toObject().value(QStringLiteral("actionAtMs")).toVariant().toLongLong();
        QVERIFY(actionAtMs > 0);
    }

    ProgressStore reopened(sourcePath);
    WatchStateSyncAdapter reopenedAdapter(&reopened);
    SyncAdapterExport reopenedSnapshot;
    QString error;
    QVERIFY2(reopenedAdapter.exportSnapshot(&reopenedSnapshot, &error), qPrintable(error));
    QCOMPARE(reopenedSnapshot.records.size(), 1);
    QCOMPARE(reopenedSnapshot.records.first().payload.toObject().value(QStringLiteral("actionAtMs")).toVariant().toLongLong(), actionAtMs);

    ProgressStore target(targetPath);
    WatchStateSyncAdapter targetAdapter(&target);
    SyncAdapterValidationError validation;
    QVERIFY2(targetAdapter.validateRemote(
        exported.recordKey,
        SyncWireOperation::Put,
        exported.payload,
        1,
        &validation), qPrintable(validation.detail));
    QVERIFY2(targetAdapter.applyRemote(
        exported.recordKey,
        SyncWireOperation::Put,
        exported.payload,
        1,
        &error), qPrintable(error));
    WatchStateSyncAdapter targetExport(&target);
    SyncAdapterExport targetSnapshot;
    QVERIFY2(targetExport.exportSnapshot(&targetSnapshot, &error), qPrintable(error));
    QCOMPARE(targetSnapshot.records.size(), 1);
    QCOMPARE(targetSnapshot.records.first().payload.toObject().value(QStringLiteral("actionAtMs")).toVariant().toLongLong(), actionAtMs);

    QSettings legacySettings(legacyPath, QSettings::IniFormat);
    legacySettings.setValue(QStringLiteral("video/watchedMark/tt901"), 1);
    legacySettings.sync();
    ProgressStore legacy(legacyPath);
    WatchStateSyncAdapter legacyAdapter(&legacy);
    SyncAdapterExport legacySnapshot;
    QVERIFY2(legacyAdapter.exportSnapshot(&legacySnapshot, &error), qPrintable(error));
    QCOMPARE(legacySnapshot.records.size(), 1);
    QVERIFY(!legacySnapshot.records.first().payload.toObject().contains(QStringLiteral("actionAtMs")));
}

void tst_core_sync_adapters::
watchedActionTimestampWinsOverTransportOrder() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore progress(
        QDir(temp.path()).filePath(QStringLiteral("watch-action-order.ini")));
    WatchStateSyncAdapter adapter(&progress);
    const QString id = QStringLiteral("tt-action-order:s1:e4");
    const QString recordKey =
        CoreStateSyncProjection::watchedMarkKey(QStringLiteral("tt-action-order"));

    auto apply = [&](int mark, qint64 actionAtMs) {
        QJsonObject payload{
            {QStringLiteral("id"), QStringLiteral("tt-action-order")},
            {QStringLiteral("mark"), mark}};
        if (actionAtMs > 0) {
            payload.insert(
                QStringLiteral("actionAtMs"),
                QString::number(actionAtMs));
        }
        QString error;
        QVERIFY2(adapter.applyRemote(
            recordKey,
            SyncWireOperation::Put,
            payload,
            1,
            &error), qPrintable(error));
    };

    apply(1, 2000);
    QCOMPARE(progress.watchedMark(id), 1);

    // A later transport envelope with an older real action cannot undo a
    // watched decision. This is intentionally exercised through the owner,
    // not a merge helper, because all remote imports share this path.
    apply(-1, 1000);
    QCOMPARE(progress.watchedMark(id), 1);

    apply(-1, 3000);
    QCOMPARE(progress.watchedMark(id), -1);

    // An unknown legacy action cannot manufacture an order over a known
    // action. An equal real action is a Core-resolved tie, so the remote
    // winner replaces the contradictory local materialization.
    apply(1, 0);
    QCOMPARE(progress.watchedMark(id), -1);
    apply(1, 3000);
    QCOMPARE(progress.watchedMark(id), 1);

    QString error;
    QVERIFY2(adapter.applyRemote(
        recordKey,
        SyncWireOperation::Delete,
        QJsonValue(),
        1,
        &error), qPrintable(error));
    QCOMPARE(progress.watchedMark(id), 0);
}

void tst_core_sync_adapters::
watchStateAdapterSkipsFilesystemIdentity() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProgressStore source(
        QDir(temp.path()).filePath(
            QStringLiteral("watch-path.ini")));
    const QString localPath =
        QStringLiteral("C:/private/library/episode-1");
    source.setWatchedMark(localPath, true);
    source.rememberLastSeason(localPath, 2);

    WatchStateSyncAdapter adapter(&source);
    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(&snapshot, &error),
        qPrintable(error));
    QCOMPARE(snapshot.records.size(), 0);
    QVERIFY(
        CoreStateSyncProjection::watchedMarkKey(localPath).isEmpty());
    QVERIFY(
        CoreStateSyncProjection::lastSeasonKey(localPath).isEmpty());

    SyncAdapterValidationError validation;
    QVERIFY(!adapter.validateRemote(
        QStringLiteral("watch/mark/QzovcHJpdmF0ZS9saWJyYXJ5L2VwaXNvZGUtMQ"),
        SyncWireOperation::Put,
        QJsonObject{{QStringLiteral("id"), localPath},
                    {QStringLiteral("mark"), 1}},
        1,
        &validation));
    QCOMPARE(validation.code, QStringLiteral("invalid_record_key"));
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
progressLocalDurabilityReceiptUsesAsyncWriter() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("progress.ini"));
    ProgressStore store(path);
    store.recordSilent(progressEntry(QStringLiteral("receipt-local"), 0.40, 123456789));

    bool receiptCalled = false;
    QString receiptError;
    QVERIFY(store.requestDurableReceipt([&](bool committed, const QString &error) {
        receiptCalled = committed;
        receiptError = error;
    }));
    QVERIFY(!receiptCalled);
    QTRY_VERIFY(receiptCalled);
    QVERIFY2(receiptError.isEmpty(), qPrintable(receiptError));

    ProgressStore reopened(path);
    QVERIFY(reopened.get(QStringLiteral("manga"), QStringLiteral("receipt-local"))
                .value(QStringLiteral("updatedAt")).toLongLong() > 0);
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
stremioTwoReplicasProviderEqualitySettlesWithoutEcho() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    CoreFixtureService service;
    CoreReplica a(
        &service,
        makeProfile(&tempA),
        QString::fromLatin1(kDeviceA));
    CoreReplica b(
        &service,
        makeProfile(&tempB),
        QString::fromLatin1(kDeviceB));

    QVariantMap movie = collectionEntry(
        QStringLiteral("tt-provider-current"),
        service.serverTimeMs);
    movie.insert(QStringLiteral("type"), QStringLiteral("movie"));
    QVERIFY(a.collection.add(QStringLiteral("theatre"), movie));
    a.progress.recordSilent(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("tt-provider-current")},
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("title"), QStringLiteral("Provider-current movie")},
        {QStringLiteral("progress"), 0.20},
        {QStringLiteral("duration"), 100.0},
        {QStringLiteral("resume"), QVariantMap{{QStringLiteral("position"), 20.0}}}});

    a.engine.setNetworkEnabled(true);
    QTRY_COMPARE(a.engine.pendingOutboxCount(), 0);

    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();
    QTRY_VERIFY(b.collection.has(
        QStringLiteral("theatre"),
        QStringLiteral("tt-provider-current")));
    QTRY_COMPARE(b.progress.get(
        QStringLiteral("video"),
        QStringLiteral("tt-provider-current")).value(
            QStringLiteral("progress")).toDouble(), 0.20);

    const QVariantMap convergedProgress = b.progress.get(
        QStringLiteral("video"),
        QStringLiteral("tt-provider-current"));
    const qint64 convergedAt = convergedProgress.value(QStringLiteral("updatedAt")).toLongLong();
    QVERIFY(convergedAt > 0);

    FixtureDatastoreApi datastore;
    QVERIFY(datastore.listen());
    datastore.setResult(QJsonArray{QJsonObject{
        {QStringLiteral("_id"), QStringLiteral("tt-provider-current")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("removed"), false},
        {QStringLiteral("temp"), false},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), QStringLiteral("tt-provider-current")},
            {QStringLiteral("timeOffset"), 20000},
            {QStringLiteral("duration"), 100000},
            {QStringLiteral("lastWatched"), QDateTime::fromMSecsSinceEpoch(
                convergedAt, Qt::UTC).toString(Qt::ISODateWithMs)}}}}});

    StremioPersistentState state;
    state.profileId = b.profile.profileId();
    state.bindingGeneration = 1;
    state.accountId = QStringLiteral("fixture-account");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(b.profile.stremioSyncStatePath(), state);
        QTRY_COMPARE(committed.count(), 1);
    }
    StremioSyncOptions options;
    options.apiEndpoint = datastore.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(
        b.profile.profileId(),
        b.profile.stremioSyncStatePath(),
        false));
    sync.setMarkerLinked(true);

    QVERIFY(sync.reconcileTheatreState(
        b.collection.items(QStringLiteral("theatre")),
        b.progress.syncEntries(),
        {},
        {}));
    QTRY_COMPARE(sync.pendingCount(), 0);
    QTRY_COMPARE(datastore.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 2);
    QCOMPARE(datastore.request().count(QByteArrayLiteral("POST /api/datastorePut")), 0);
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
watchStatePersistenceFailureDoesNotAdvanceCursor() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile = makeProfile(&temp);

    CoreFixtureService service;
    const QString seriesId = QStringLiteral("tt-watch-disk-failure");
    const QString recordKey =
        CoreStateSyncProjection::watchedMarkKey(seriesId);

    SyncWireMutation mutation;
    mutation.mutationId = QStringLiteral(
        "cccccccc-cccc-4ccc-8ccc-cccccccccccd");
    mutation.deviceId = QString::fromLatin1(kDeviceB);
    mutation.category = QStringLiteral("watch_state");
    mutation.recordKey = recordKey;
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{
        service.serverTimeMs,
        0,
        mutation.deviceId};
    mutation.operation = SyncWireOperation::Put;
    mutation.payload = QJsonObject{
        {QStringLiteral("id"), seriesId},
        {QStringLiteral("mark"), 1}};
    const AccountTransportReply seeded =
        service.push(QJsonArray{syncWireMutationToJson(mutation)});
    QCOMPARE(seeded.statusCode, 200);

    CoreReplica replica(
        &service,
        profile,
        QString::fromLatin1(kDeviceA));

    // The owner loaded as a valid empty store, but its backing INI path is
    // now a directory. A remote watch-state apply must reject the write and
    // leave the sync cursor behind the durable owner receipt.
    QFile::remove(profile.progressIniPath());
    QVERIFY(QDir().mkpath(profile.progressIniPath()));

    replica.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        replica.engine.state(),
        SyncEngine::State::Blocked);
    QCOMPARE(replica.engine.cursor(), quint64(0));
    QCOMPARE(
        replica.progress.watchedMark(seriesId),
        0);

    SyncStateStore stateStore;
    QString error;
    const auto state =
        stateStore.load(profile.syncStatePath(), &error);
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

void tst_core_sync_adapters::
progressForgetWatchRemovalFailureDoesNotPublishDelete() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path =
        QDir(temp.path()).filePath(QStringLiteral("progress.ini"));

    ProgressStore progress(path);
    const QString seriesId = QStringLiteral("tt-forget-watch-failure");
    const QString episodeId = seriesId + QStringLiteral(":s1:e1");
    progress.record(progressEntry(episodeId, 0.75));
    progress.setWatchedMark(episodeId, true);
    progress.flush();
    QCOMPARE(progress.watchedMark(episodeId), 1);

    QSignalSpy watchStateSpy(
        &progress,
        &ProgressStore::watchStateChanged);

    progress.forceWatchStatePersistenceFailureForTesting(true);

    progress.forget(QStringLiteral("manga"), episodeId);

    // The combined action must abort before mutating Continue when the
    // watched-mark owner cannot durably commit either half of the request.
    QCOMPARE(
        progress
            .get(
                QStringLiteral("manga"),
                episodeId)
            .value(QStringLiteral("id"))
            .toString(),
        episodeId);
    QCOMPARE(progress.watchedMark(episodeId), 1);
    QCOMPARE(watchStateSpy.count(), 0);
    QVERIFY(!progress.healthy());
    QVERIFY(!progress.persistenceError().isEmpty());
}

QTEST_MAIN(tst_core_sync_adapters)
#include "tst_core_sync_adapters.moc"
