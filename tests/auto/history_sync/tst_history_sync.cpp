// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/CoreStateSyncProjection.h"
#include "account/HistoryStore.h"
#include "account/HistorySyncAdapter.h"
#include "account/ProfilePaths.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"

#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
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

QString mutationIdentity(
    const SyncWireMutation &mutation) {
    return mutation.category
        + QChar(0x1f)
        + mutation.recordKey;
}

class HistoryFixtureService {
public:
    qint64 serverTimeMs = 2000000;

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
            const auto parsed =
                value.isObject()
                ? syncWireMutationFromJson(
                      value.toObject())
                : std::nullopt;

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
                    QStringLiteral("accepted"),
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
                m_acks.constFind(
                    mutation.mutationId);
            if (duplicate
                != m_acks.constEnd()) {
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
                        duplicate->sequence));
                result.insert(
                    QStringLiteral("won"),
                    duplicate->won);
                results.append(result);
                continue;
            }

            const QString identity =
                mutationIdentity(
                    mutation);
            const auto current =
                m_current.constFind(
                    identity);

            const bool won =
                current == m_current.constEnd()
                || syncWireHlcGreater(
                    mutation.hlc,
                    current->hlc);

            JournalEntry entry;
            entry.sequence =
                m_nextSequence++;
            entry.won = won;
            entry.mutation =
                mutation;
            m_journal.append(entry);

            if (won) {
                m_current.insert(
                    identity,
                    mutation);
            }

            m_acks.insert(
                mutation.mutationId,
                Ack{
                    entry.sequence,
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

class HistoryFixtureTransport final
    : public AccountTransport {
    Q_OBJECT

public:
    explicit HistoryFixtureTransport(
        HistoryFixtureService *service,
        QObject *parent = nullptr)
        : AccountTransport(parent),
          m_service(service) {}

    void setOnline(
        bool online) {
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
                m_service->pull(
                    cursor));
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
    HistoryFixtureService *m_service =
        nullptr;
    bool m_online = true;
};

ProfilePaths accountProfile(
    QTemporaryDir *temp) {
    const auto profile =
        ProfilePaths::account(
            QString::fromLatin1(
                kAccount),
            temp->path());

    if (!profile.has_value())
        qFatal(
            "History fixture account profile invalid.");

    QDir().mkpath(
        profile->profileRoot());
    return *profile;
}

struct HistoryReplica {
    HistoryFixtureTransport transport;
    AccountClient client;
    HistoryStore store;
    SyncAdapterRegistry registry;
    HistorySyncAdapter adapter;
    SyncEngine engine;
    ProfilePaths profile;

    HistoryReplica(
        HistoryFixtureService *service,
        const ProfilePaths &profileValue,
        const QString &deviceId,
        qint64 *now)
        : transport(service),
          client(&transport),
          store(
              profileValue.historyIniPath()),
          adapter(&store),
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

        if (!registry.registerAdapter(
                &adapter)) {
            qFatal(
                "History fixture adapter registration failed.");
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
                "History fixture engine start failed.");
        }
    }
};

QVariantMap historyRecord(
    const QString &kind,
    const QString &id,
    qint64 firstActivityAt,
    qint64 lastActivityAt,
    qint64 completedAt = 0) {
    QVariantMap record;
    record.insert(
        QStringLiteral("kind"),
        kind);
    record.insert(
        QStringLiteral("id"),
        id);
    record.insert(
        QStringLiteral(
            "firstActivityAt"),
        firstActivityAt);
    record.insert(
        QStringLiteral(
            "lastActivityAt"),
        lastActivityAt);

    if (completedAt > 0) {
        record.insert(
            QStringLiteral(
                "completedAt"),
            completedAt);
    }

    return record;
}
}

class tst_history_sync final
    : public QObject {
    Q_OBJECT

private slots:
    void ownerAccumulatesFirstLastAndCompletionFacts();
    void ownerPersistsAcrossRestart();
    void legacyCompletedOnlyRecordPromotesOnLoad();
    void malformedPersistedRecordFailsClosedWithoutOverwrite();
    void filesystemIdentityIsRejected();
    void adapterExportsCanonicalRecordAndOrderingHint();
    void remoteWinnerReplacesRecordExactlyWithoutEcho();
    void explicitDeleteBecomesSnapshotAbsence();
    void twoReplicasConvergeHistory();
    void newerDeleteBeatsOlderOfflineActivity();
    void accountProfilesKeepHistorySeparated();
};

void tst_history_sync::
ownerAccumulatesFirstLastAndCompletionFacts() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    HistoryStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini")));

    QSignalSpy changed(
        &store,
        &HistoryStore::changed);
    QSignalSpy dirty(
        &store,
        &HistoryStore::syncDirty);

    QVERIFY(
        store.recordActivity(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"),
            2000));
    QVERIFY(
        store.recordActivity(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"),
            1000));
    QVERIFY(
        store.markCompleted(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"),
            2500));
    QVERIFY(
        store.recordActivity(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"),
            3000));

    const QVariantMap record =
        store.get(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"));

    QCOMPARE(
        record.value(
            QStringLiteral(
                "firstActivityAt"))
            .toLongLong(),
        qint64(1000));
    QCOMPARE(
        record.value(
            QStringLiteral(
                "lastActivityAt"))
            .toLongLong(),
        qint64(3000));
    QCOMPARE(
        record.value(
            QStringLiteral(
                "completedAt"))
            .toLongLong(),
        qint64(2500));

    QCOMPARE(changed.count(), 4);
    QCOMPARE(dirty.count(), 4);
}

void tst_history_sync::
ownerPersistsAcrossRestart() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path =
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini"));

    {
        HistoryStore store(path);
        QVERIFY(
            store.recordActivity(
                QStringLiteral("book"),
                QStringLiteral("book-1"),
                4000));
        QVERIFY(
            store.markCompleted(
                QStringLiteral("book"),
                QStringLiteral("book-1"),
                5000));
    }

    HistoryStore reopened(path);
    const QVariantMap record =
        reopened.get(
            QStringLiteral("book"),
            QStringLiteral("book-1"));

    QCOMPARE(
        record.value(
            QStringLiteral(
                "firstActivityAt"))
            .toLongLong(),
        qint64(4000));
    QCOMPARE(
        record.value(
            QStringLiteral(
                "lastActivityAt"))
            .toLongLong(),
        qint64(5000));
    QCOMPARE(
        record.value(
            QStringLiteral(
                "completedAt"))
            .toLongLong(),
        qint64(5000));
}

void tst_history_sync::
legacyCompletedOnlyRecordPromotesOnLoad() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path =
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini"));

    QJsonObject legacyRecord;
    legacyRecord.insert(
        QStringLiteral("kind"),
        QStringLiteral("movie"));
    legacyRecord.insert(
        QStringLiteral("id"),
        QStringLiteral("movie-1"));
    legacyRecord.insert(
        QStringLiteral("completedAt"),
        6000);

    QJsonObject legacyRoot;
    legacyRoot.insert(
        QStringLiteral("movie")
            + QChar(0x1f)
            + QStringLiteral("movie-1"),
        legacyRecord);

    QSettings settings(
        path,
        QSettings::IniFormat);
    settings.setValue(
        QStringLiteral("history/records"),
        QJsonDocument(legacyRoot)
            .toJson(
                QJsonDocument::Compact));
    settings.sync();

    HistoryStore store(path);
    const QVariantMap record =
        store.get(
            QStringLiteral("movie"),
            QStringLiteral("movie-1"));

    QCOMPARE(
        record.value(
            QStringLiteral(
                "firstActivityAt"))
            .toLongLong(),
        qint64(6000));
    QCOMPARE(
        record.value(
            QStringLiteral(
                "lastActivityAt"))
            .toLongLong(),
        qint64(6000));
    QCOMPARE(
        record.value(
            QStringLiteral(
                "completedAt"))
            .toLongLong(),
        qint64(6000));
}

void tst_history_sync::
malformedPersistedRecordFailsClosedWithoutOverwrite() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path =
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini"));

    QJsonObject malformedRecord;
    malformedRecord.insert(
        QStringLiteral("kind"),
        QStringLiteral("movie"));
    malformedRecord.insert(
        QStringLiteral("id"),
        QStringLiteral("movie-1"));
    malformedRecord.insert(
        QStringLiteral("firstActivityAt"),
        5000);
    malformedRecord.insert(
        QStringLiteral("lastActivityAt"),
        4000);

    QJsonObject root;
    root.insert(
        QStringLiteral("movie")
            + QChar(0x1f)
            + QStringLiteral("movie-1"),
        malformedRecord);

    QSettings settings(
        path,
        QSettings::IniFormat);
    settings.setValue(
        QStringLiteral("history/records"),
        QJsonDocument(root)
            .toJson(
                QJsonDocument::Compact));
    settings.sync();

    HistoryStore store(path);
    QString ownerError;
    QVERIFY(
        !store.healthy(
            &ownerError));
    QVERIFY(!ownerError.isEmpty());

    HistorySyncAdapter adapter(
        &store);
    SyncAdapterExport snapshot;
    QString exportError;
    QVERIFY(
        !adapter.exportSnapshot(
            &snapshot,
            &exportError));
    QVERIFY(!exportError.isEmpty());

    QVERIFY(
        !store.recordActivity(
            QStringLiteral("movie"),
            QStringLiteral("movie-2"),
            6000));

    QSettings readback(
        path,
        QSettings::IniFormat);
    const QByteArray preserved =
        readback
            .value(
                QStringLiteral(
                    "history/records"))
            .toByteArray();

    QCOMPARE(
        QJsonDocument::fromJson(
            preserved)
            .object(),
        root);
}

void tst_history_sync::
filesystemIdentityIsRejected() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    HistoryStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini")));

    QVERIFY(
        !store.recordActivity(
            QStringLiteral("book"),
            QStringLiteral(
                "C:\\Private\\book.epub"),
            1000));
    QVERIFY(
        !store.recordActivity(
            QStringLiteral("book"),
            QStringLiteral(
                "../private/book.epub"),
            1000));

    QVERIFY(store.records().isEmpty());
}

void tst_history_sync::
adapterExportsCanonicalRecordAndOrderingHint() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    HistoryStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini")));

    QVERIFY(
        store.recordActivity(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"),
            1000));
    QVERIFY(
        store.markCompleted(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"),
            2500));

    HistorySyncAdapter adapter(
        &store);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));

    QCOMPARE(snapshot.records.size(), 1);
    const SyncAdapterRecord exported =
        snapshot.records.first();

    QString kind;
    QString id;
    QVERIFY(
        CoreStateSyncProjection::
            decodeHistoryKey(
                exported.recordKey,
                &kind,
                &id));

    QCOMPARE(
        kind,
        QStringLiteral("episode"));
    QCOMPARE(
        id,
        QStringLiteral("show-1/e1"));
    QCOMPARE(
        exported.localOrderMs,
        qint64(2500));

    const QJsonObject payload =
        exported.payload.toObject();
    QCOMPARE(
        payload.value(
            QStringLiteral(
                "firstActivityAt"))
            .toVariant()
            .toLongLong(),
        qint64(1000));
    QCOMPARE(
        payload.value(
            QStringLiteral(
                "lastActivityAt"))
            .toVariant()
            .toLongLong(),
        qint64(2500));
}

void tst_history_sync::
remoteWinnerReplacesRecordExactlyWithoutEcho() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    HistoryStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini")));
    QVERIFY(
        store.recordActivity(
            QStringLiteral("book"),
            QStringLiteral("book-1"),
            1000));
    QVERIFY(
        store.recordActivity(
            QStringLiteral("book"),
            QStringLiteral("book-1"),
            9000));

    HistorySyncAdapter adapter(
        &store);
    SyncAdapterRegistry registry;
    QVERIFY(
        registry.registerAdapter(
            &adapter));

    QSignalSpy localSpy(
        &registry,
        &SyncAdapterRegistry::
            localMutationAvailable);

    const QVariantMap remote =
        historyRecord(
            QStringLiteral("book"),
            QStringLiteral("book-1"),
            2000,
            4000,
            3500);
    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            history(remote);
    QCOMPARE(
        projected.disposition,
        CoreStateSyncProjection::
            Disposition::Portable);

    SyncAdapterMutation mutation;
    mutation.categoryId =
        QStringLiteral("full_history");
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

    QCOMPARE(localSpy.count(), 0);

    const QVariantMap stored =
        store.get(
            QStringLiteral("book"),
            QStringLiteral("book-1"));

    QCOMPARE(
        stored.value(
            QStringLiteral(
                "firstActivityAt"))
            .toLongLong(),
        qint64(2000));
    QCOMPARE(
        stored.value(
            QStringLiteral(
                "lastActivityAt"))
            .toLongLong(),
        qint64(4000));
    QCOMPARE(
        stored.value(
            QStringLiteral(
                "completedAt"))
            .toLongLong(),
        qint64(3500));
}

void tst_history_sync::
explicitDeleteBecomesSnapshotAbsence() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    HistoryStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "history.ini")));
    HistorySyncAdapter adapter(
        &store);

    QVERIFY(
        store.markCompleted(
            QStringLiteral("movie"),
            QStringLiteral("movie-1"),
            7000));

    SyncAdapterExport before;
    QVERIFY(
        adapter.exportSnapshot(
            &before));
    QCOMPARE(before.records.size(), 1);

    QSignalSpy dirty(
        &store,
        &HistoryStore::syncDirty);

    QVERIFY(
        store.remove(
            QStringLiteral("movie"),
            QStringLiteral("movie-1")));
    QCOMPARE(dirty.count(), 1);

    SyncAdapterExport after;
    QVERIFY(
        adapter.exportSnapshot(
            &after));
    QVERIFY(after.records.isEmpty());
}

void tst_history_sync::
twoReplicasConvergeHistory() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    HistoryFixtureService service;
    qint64 nowA =
        service.serverTimeMs;
    qint64 nowB =
        service.serverTimeMs;

    HistoryReplica a(
        &service,
        accountProfile(&tempA),
        QString::fromLatin1(
            kDeviceA),
        &nowA);
    HistoryReplica b(
        &service,
        accountProfile(&tempB),
        QString::fromLatin1(
            kDeviceB),
        &nowB);

    QVERIFY(
        a.store.recordActivity(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"),
            nowA - 2000));
    QVERIFY(
        a.store.markCompleted(
            QStringLiteral("episode"),
            QStringLiteral("show-1/e1"),
            nowA - 1000));

    a.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);

    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();

    QTRY_COMPARE(
        b.store
            .get(
                QStringLiteral("episode"),
                QStringLiteral("show-1/e1"))
            .value(
                QStringLiteral(
                    "completedAt"))
            .toLongLong(),
        nowA - 1000);

    QCOMPARE(
        service.acceptedMutationCount(),
        1);
}

void tst_history_sync::
newerDeleteBeatsOlderOfflineActivity() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    HistoryFixtureService service;
    qint64 nowA =
        service.serverTimeMs;
    qint64 nowB =
        service.serverTimeMs;

    HistoryReplica a(
        &service,
        accountProfile(&tempA),
        QString::fromLatin1(
            kDeviceA),
        &nowA);
    HistoryReplica b(
        &service,
        accountProfile(&tempB),
        QString::fromLatin1(
            kDeviceB),
        &nowB);

    QVERIFY(
        a.store.recordActivity(
            QStringLiteral("movie"),
            QStringLiteral("movie-1"),
            nowA));
    a.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);

    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();
    QTRY_VERIFY(
        !b.store
             .get(
                 QStringLiteral("movie"),
                 QStringLiteral("movie-1"))
             .isEmpty());

    a.engine.setNetworkEnabled(false);
    b.engine.setNetworkEnabled(false);

    nowA += 1000;
    QVERIFY(
        a.store.recordActivity(
            QStringLiteral("movie"),
            QStringLiteral("movie-1"),
            nowA));

    nowB += 2000;
    QVERIFY(
        b.store.remove(
            QStringLiteral("movie"),
            QStringLiteral("movie-1")));

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
        a.store
            .get(
                QStringLiteral("movie"),
                QStringLiteral("movie-1"))
            .isEmpty());
    QTRY_VERIFY(
        b.store
            .get(
                QStringLiteral("movie"),
                QStringLiteral("movie-1"))
            .isEmpty());
}

void tst_history_sync::
accountProfilesKeepHistorySeparated() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto a =
        ProfilePaths::account(
            QString::fromLatin1(
                kAccount),
            temp.path());
    const auto b =
        ProfilePaths::account(
            QStringLiteral(
                "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"),
            temp.path());

    QVERIFY(a.has_value());
    QVERIFY(b.has_value());
    QVERIFY(
        a->historyIniPath()
        != b->historyIniPath());

    QDir().mkpath(a->profileRoot());
    QDir().mkpath(b->profileRoot());

    HistoryStore storeA(
        a->historyIniPath());
    HistoryStore storeB(
        b->historyIniPath());

    QVERIFY(
        storeA.markCompleted(
            QStringLiteral("movie"),
            QStringLiteral("private-a"),
            9000));

    QVERIFY(
        storeB
            .get(
                QStringLiteral("movie"),
                QStringLiteral("private-a"))
            .isEmpty());
}

QTEST_MAIN(tst_history_sync)
#include "tst_history_sync.moc"
