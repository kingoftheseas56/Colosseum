// PRE-FLIGHT DRAFT STATUS: intentional RED contract; production category-policy
// and v2 paused-state interfaces are not present yet.

#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/HistoryStore.h"
#include "account/HistorySyncAdapter.h"
#include "account/ProfilePaths.h"
#include "account/SyncAdapter.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"

#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <utility>

namespace {
constexpr auto kAccount = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceA = "11111111-1111-4111-8111-111111111111";
constexpr auto kDeviceB = "22222222-2222-4222-8222-222222222222";

QString identity(const QString &category, const QString &key) {
    return category + QChar(0x1f) + key;
}

QString historyKey(const QString &kind, const QString &id) {
    const auto encode = [](const QString &value) {
        return QString::fromLatin1(value.toUtf8().toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    return QStringLiteral("history/") + encode(kind) + QLatin1Char('/') + encode(id);
}

struct JournalEntry {
    quint64 sequence = 0;
    bool won = false;
    SyncWireMutation mutation;
};

class FixtureService {
public:
    qint64 serverTimeMs = 2000000;

    int acceptedMutationCount() const { return m_acks.size(); }

    const QList<JournalEntry> &journal() const { return m_journal; }

    void appendRemote(const SyncWireMutation &mutation, bool won = true) {
        JournalEntry entry{m_nextSequence++, won, mutation};
        m_journal.append(entry);
        if (won)
            m_current.insert(identity(mutation.category, mutation.recordKey), mutation);
    }

    AccountTransportReply push(const QJsonArray &values) {
        AccountTransportReply reply;
        reply.statusCode = 200;
        QJsonArray results;
        for (const QJsonValue &value : values) {
            const auto parsed = value.isObject()
                ? syncWireMutationFromJson(value.toObject())
                : std::nullopt;
            if (!parsed.has_value())
                continue;
            const SyncWireMutation mutation = *parsed;
            if (m_acks.contains(mutation.mutationId)) {
                const JournalEntry &old = m_acks.value(mutation.mutationId);
                QJsonObject result;
                result.insert("mutation_id", mutation.mutationId);
                result.insert("accepted", true);
                result.insert("server_seq", QString::number(old.sequence));
                result.insert("won", old.won);
                results.append(result);
                continue;
            }
            const auto current = m_current.constFind(identity(mutation.category, mutation.recordKey));
            const bool won = current == m_current.constEnd()
                || syncWireHlcGreater(mutation.hlc, current->hlc);
            JournalEntry entry{m_nextSequence++, won, mutation};
            m_journal.append(entry);
            m_acks.insert(mutation.mutationId, entry);
            if (won)
                m_current.insert(identity(mutation.category, mutation.recordKey), mutation);
            QJsonObject result;
            result.insert("mutation_id", mutation.mutationId);
            result.insert("accepted", true);
            result.insert("server_seq", QString::number(entry.sequence));
            result.insert("won", won);
            results.append(result);
        }
        reply.body.insert("server_time_ms", QString::number(serverTimeMs));
        reply.body.insert("results", results);
        return reply;
    }

    AccountTransportReply pull(quint64 after) const {
        AccountTransportReply reply;
        reply.statusCode = 200;
        QJsonArray entries;
        for (const JournalEntry &entry : m_journal) {
            if (entry.sequence <= after)
                continue;
            QJsonObject object;
            object.insert("server_seq", QString::number(entry.sequence));
            object.insert("won", entry.won);
            object.insert("mutation", syncWireMutationToJson(entry.mutation));
            entries.append(object);
        }
        reply.body.insert("server_time_ms", QString::number(serverTimeMs));
        reply.body.insert("entries", entries);
        reply.body.insert("has_more", false);
        return reply;
    }

private:
    quint64 m_nextSequence = 1;
    QList<JournalEntry> m_journal;
    QHash<QString, JournalEntry> m_acks;
    QHash<QString, SyncWireMutation> m_current;
};

class FixtureTransport final : public AccountTransport {
    Q_OBJECT
public:
    explicit FixtureTransport(FixtureService *service, QObject *parent = nullptr)
        : AccountTransport(parent), m_service(service) {}

    void setOnline(bool online) { m_online = online; }
    int requestCount() const { return m_requestCount; }

    void send(quint64 requestId, const AccountTransportRequest &request) override {
        ++m_requestCount;
        AccountTransportReply reply;
        if (!m_online) {
            reply.networkError = true;
            reply.errorCode = "offline";
            reply.errorMessage = "fixture offline";
        } else if (request.bearerToken.isEmpty()) {
            reply.statusCode = 401;
            reply.errorCode = "session_invalid";
        } else if (request.method == "POST" && request.path == "/v1/sync/push") {
            reply = m_service->push(request.body.value("mutations").toArray());
        } else if (request.method == "GET" && request.path.startsWith("/v1/sync/pull?after=")) {
            bool ok = false;
            const quint64 after = request.path.mid(QStringLiteral("/v1/sync/pull?after=").size()).toULongLong(&ok);
            reply = ok ? m_service->pull(after) : AccountTransportReply{};
            if (!ok)
                reply.statusCode = 400;
        } else {
            reply.statusCode = 404;
            reply.errorCode = "fixture_route_missing";
        }
        emit finished(requestId, reply);
    }

private:
    FixtureService *m_service = nullptr;
    bool m_online = true;
    int m_requestCount = 0;
};

class CategoryAdapter final : public SyncAdapter {
    Q_OBJECT
public:
    explicit CategoryAdapter(QString category, QObject *parent = nullptr)
        : SyncAdapter(parent), m_category(std::move(category)) {}

    QString categoryId() const override { return m_category; }
    int schemaVersion() const override { return 1; }
    quint64 revision() const override { return m_revision; }

    bool exportSnapshot(SyncAdapterExport *snapshot, QString *error = nullptr) const override {
        if (!snapshot) {
            if (error) *error = "missing snapshot";
            return false;
        }
        snapshot->revision = m_revision;
        snapshot->records.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
            snapshot->records.append({it.key(), it.value()});
        return true;
    }

    bool applyRemote(const QString &key, SyncWireOperation operation,
                     const QJsonValue &payload, int schemaVersion,
                     QString *error = nullptr) override {
        if (schemaVersion != 1 || (operation == SyncWireOperation::Put && !payload.isObject())) {
            if (error) *error = "invalid fixture record";
            return false;
        }
        ++m_remoteApplyCount;
        if (operation == SyncWireOperation::Delete)
            m_values.remove(key);
        else
            m_values.insert(key, payload);
        return true;
    }

    void put(const QString &key, const QString &value) {
        m_values.insert(key, QJsonObject{{"value", value}});
        ++m_revision;
        emit localMutationAvailable(m_revision);
    }

    void remove(const QString &key) {
        m_values.remove(key);
        ++m_revision;
        emit localMutationAvailable(m_revision);
    }

    QJsonValue value(const QString &key) const { return m_values.value(key); }
    bool contains(const QString &key) const { return m_values.contains(key); }
    int remoteApplyCount() const { return m_remoteApplyCount; }

private:
    QString m_category;
    QHash<QString, QJsonValue> m_values;
    quint64 m_revision = 0;
    int m_remoteApplyCount = 0;
};

ProfilePaths profileFor(QTemporaryDir *temp) {
    const auto profile = ProfilePaths::account(QString::fromLatin1(kAccount), temp->path());
    if (!profile.has_value())
        qFatal("invalid fixture profile");
    QDir().mkpath(profile->profileRoot());
    return *profile;
}

SyncWireMutation remoteMutation(const QString &id, const QString &category,
                                const QString &key, const QString &device,
                                qint64 physicalMs, const QString &value,
                                SyncWireOperation operation = SyncWireOperation::Put) {
    SyncWireMutation mutation;
    mutation.mutationId = QUuid::createUuidV5(
        QUuid(QStringLiteral("00000000-0000-4000-8000-000000000000")),
        id).toString(QUuid::WithoutBraces).toLower();
    mutation.deviceId = device;
    mutation.category = category;
    mutation.recordKey = key;
    mutation.schemaVersion = 1;
    mutation.hlc = {physicalMs, 0, device};
    mutation.operation = operation;
    if (operation == SyncWireOperation::Put) {
        if (category == QStringLiteral("full_history")) {
            const QString id = QString::fromUtf8(QByteArray::fromBase64(
                key.section(QLatin1Char('/'), 2, 2).toLatin1(),
                QByteArray::Base64UrlEncoding));
            mutation.payload = QJsonObject{
                {"kind", "book"},
                {"id", id},
                {"firstActivityAt", physicalMs},
                {"lastActivityAt", physicalMs}};
        } else {
            mutation.payload = QJsonObject{{"value", value}};
        }
    }
    return mutation;
}

struct Replica {
    FixtureTransport transport;
    AccountClient client;
    HistoryStore history;
    HistorySyncAdapter historyAdapter;
    CategoryAdapter other;
    SyncAdapterRegistry registry;
    SyncEngine engine;
    ProfilePaths profile;
    int completedRequestCount = 0;

    Replica(FixtureService *service, const ProfilePaths &profileValue,
            const QString &device, qint64 *now, bool startNow = true)
        : transport(service), client(&transport), history(profileValue.profileRoot() + "/history.ini"),
          historyAdapter(&history), other(QStringLiteral("collection")),
          engine(&client, &registry, [now]() { return *now; }), profile(profileValue) {
        client.setAccessToken("fixture-access");
        if (!registry.registerAdapter(&historyAdapter)
            || !registry.registerAdapter(&other))
            qFatal("fixture adapter registration failed");
        QObject::connect(&client, &AccountClient::completed,
                         [this]() {
            ++completedRequestCount;
        });
        engine.setAutomaticSchedulingEnabled(false);
        engine.setNetworkEnabled(false);
        if (startNow)
            start(device);
    }

    void start(const QString &device) {
        QString error;
        if (!engine.start(profile, device, &error))
            qFatal("fixture engine start failed: %s", qPrintable(error));
    }
};

bool quiescent(const SyncEngine &engine, const FixtureTransport &transport,
               int completedRequestCount, const SyncStateStore &stateStore,
               const ProfilePaths &profile) {
    QString error;
    const auto persisted = stateStore.load(profile.syncStatePath(), &error);
    if (!persisted.has_value())
        return false;

    return engine.state() == SyncEngine::State::Idle
        && engine.pendingOutboxCount() == 0
        && completedRequestCount == transport.requestCount()
        && persisted->cursor == engine.cursor()
        && persisted->outbox.isEmpty()
        && persisted->pausedCategories.contains(QStringLiteral("full_history"))
            == !engine.categoryNetworkEnabled(QStringLiteral("full_history"));
}

void waitIdle(SyncEngine &engine, FixtureTransport &transport,
              int &completedRequestCount, const ProfilePaths &profile,
              int minimumRequestCount = 0) {
    if (minimumRequestCount > 0)
        QTRY_VERIFY(transport.requestCount() >= minimumRequestCount);

    SyncStateStore stateStore;
    int stableQuiescentChecks = 0;
    QTRY_VERIFY([&]() {
        if (quiescent(engine, transport, completedRequestCount, stateStore, profile))
            ++stableQuiescentChecks;
        else
            stableQuiescentChecks = 0;
        return stableQuiescentChecks >= 2;
    }());
}

void waitIdle(Replica &replica) {
    waitIdle(replica.engine, replica.transport, replica.completedRequestCount,
             replica.profile);
}

void sync(Replica &replica) {
    const int requestCountBefore = replica.transport.requestCount();
    replica.engine.setNetworkEnabled(true);
    replica.engine.requestImmediateSync();
    waitIdle(replica.engine, replica.transport, replica.completedRequestCount,
             replica.profile, requestCountBefore + 1);
}
}

class tst_sync_category_policy final : public QObject {
    Q_OBJECT
private slots:
    void disabledHistoryDoesNotPushButOtherCategoryStillPushes();
    void disabledHistoryPullAdvancesCursorWithoutMutatingOwner();
    void preStartDisableCapturesBaselineAndReenablesAfterOtherSync();
    void pausedStateSurvivesSyncEngineRestart();
    void reenableImportsRemoteOnlyHistory();
    void reenablePreservesLocalPutMadeWhilePaused();
    void reenablePreservesLocalDeleteMadeWhilePaused();
    void reenableConvergesConflictWithLocalChangeWinningOnlyChangedKey();
    void reenableReplayDoesNotReapplyOlderWinnersForOtherCategories();
    void disableDropsPendingHistoryOutbox();
};

void tst_sync_category_policy::disabledHistoryDoesNotPushButOtherCategoryStillPushes() {
    QTemporaryDir temp;
    FixtureService service;
    qint64 now = service.serverTimeMs;
    Replica replica(&service, profileFor(&temp), kDeviceA, &now);
    replica.engine.setCategoryNetworkEnabled("full_history", false);
    QVERIFY(!replica.engine.categoryNetworkEnabled("full_history"));
    QVERIFY(replica.history.recordActivity("book", "private", now));
    replica.other.put("item", "public");
    sync(replica);
    QCOMPARE(service.acceptedMutationCount(), 1);
    QCOMPARE(service.journal().constLast().mutation.category, QStringLiteral("collection"));
}

void tst_sync_category_policy::disabledHistoryPullAdvancesCursorWithoutMutatingOwner() {
    QTemporaryDir temp;
    FixtureService service;
    qint64 now = service.serverTimeMs;
    service.appendRemote(remoteMutation("remote-history", "full_history", historyKey("book", "remote"), kDeviceB, now, "secret"));
    service.appendRemote(remoteMutation("remote-other", "collection", "item", kDeviceB, now + 1, "visible"));
    Replica replica(&service, profileFor(&temp), kDeviceA, &now);
    replica.engine.setCategoryNetworkEnabled("full_history", false);
    sync(replica);
    QVERIFY(replica.engine.cursor() >= 2);
    QVERIFY(replica.history.get("book", "remote").isEmpty());
    const QJsonObject visible{{"value", "visible"}};
    QCOMPARE(replica.other.value("item"), QJsonValue(visible));
}

void tst_sync_category_policy::preStartDisableCapturesBaselineAndReenablesAfterOtherSync() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    FixtureService service;
    qint64 nowA = service.serverTimeMs;
    qint64 nowB = service.serverTimeMs;
    Replica a(&service, profileFor(&tempA), kDeviceA, &nowA, false);
    QVERIFY(a.history.recordActivity("book", "baseline", nowA));
    a.engine.setCategoryNetworkEnabled("full_history", false);
    a.start(kDeviceA);

    Replica b(&service, profileFor(&tempB), kDeviceB, &nowB);
    QVERIFY(a.history.recordActivity("book", "changed-while-paused", nowA + 1));
    a.other.put("shared", "from-a");
    sync(a);
    sync(b);

    a.engine.setCategoryNetworkEnabled("full_history", true);
    sync(a);
    sync(b);

    QVERIFY(a.engine.categoryNetworkEnabled("full_history"));
    QCOMPARE(service.acceptedMutationCount(), 2);
    QVERIFY(!a.history.get("book", "changed-while-paused").isEmpty());
    QCOMPARE(b.other.value("shared"), QJsonValue(QJsonObject{{"value", "from-a"}}));
}

void tst_sync_category_policy::pausedStateSurvivesSyncEngineRestart() {
    QTemporaryDir temp;
    FixtureService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile = profileFor(&temp);
    {
        Replica replica(&service, profile, kDeviceA, &now);
        QVERIFY(replica.history.recordActivity("book", "resume-after-restart", now));
        replica.engine.setCategoryNetworkEnabled("full_history", false);
        QVERIFY(!replica.engine.categoryNetworkEnabled("full_history"));
        QVERIFY(replica.history.recordActivity("book", "resume-after-restart", now + 1));
        replica.engine.setCategoryNetworkEnabled("full_history", true);
        QVERIFY(!replica.engine.categoryNetworkEnabled("full_history"));
        QVERIFY(replica.engine.stopPreservingOutbox());
    }
    FixtureTransport transport(&service);
    AccountClient client(&transport);
    int completedRequestCount = 0;
    QObject::connect(&client, &AccountClient::completed,
                     [&completedRequestCount]() {
        ++completedRequestCount;
    });
    client.setAccessToken("fixture-access");
    HistoryStore history(profile.profileRoot() + "/history.ini");
    HistorySyncAdapter historyAdapter(&history);
    CategoryAdapter other("collection");
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&historyAdapter));
    QVERIFY(registry.registerAdapter(&other));
    SyncEngine restarted(&client, &registry, [&now]() { return now; });
    restarted.setAutomaticSchedulingEnabled(false);
    QString error;
    QVERIFY2(restarted.start(profile, kDeviceA, &error), qPrintable(error));
    QVERIFY(!restarted.categoryNetworkEnabled("full_history"));
    const int requestCountBefore = transport.requestCount();
    restarted.setNetworkEnabled(true);
    waitIdle(restarted, transport, completedRequestCount, profile,
             requestCountBefore + 1);
    QVERIFY(restarted.categoryNetworkEnabled("full_history"));
    QVERIFY(!history.get("book", "resume-after-restart").isEmpty());
}

void tst_sync_category_policy::reenableImportsRemoteOnlyHistory() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    FixtureService service;
    qint64 nowA = service.serverTimeMs;
    qint64 nowB = service.serverTimeMs;
    Replica a(&service, profileFor(&tempA), kDeviceA, &nowA);
    Replica b(&service, profileFor(&tempB), kDeviceB, &nowB);
    sync(a);
    a.engine.setCategoryNetworkEnabled("full_history", false);
    QVERIFY(b.history.recordActivity("book", "remote-only", nowB));
    sync(b);
    const int requestCountBefore = a.transport.requestCount();
    a.engine.setCategoryNetworkEnabled("full_history", true);
    waitIdle(a.engine, a.transport, a.completedRequestCount, a.profile,
             requestCountBefore + 1);
    QVERIFY(a.history.get("book", "remote-only").contains("lastActivityAt"));
}

void tst_sync_category_policy::reenablePreservesLocalPutMadeWhilePaused() {
    QTemporaryDir temp;
    FixtureService service;
    qint64 now = service.serverTimeMs;
    Replica replica(&service, profileFor(&temp), kDeviceA, &now);
    replica.engine.setCategoryNetworkEnabled("full_history", false);
    QVERIFY(replica.history.recordActivity("book", "local", now + 1));
    replica.engine.setCategoryNetworkEnabled("full_history", true);
    sync(replica);
    QCOMPARE(replica.history.get("book", "local").value("lastActivityAt").toLongLong(), now + 1);
}

void tst_sync_category_policy::reenablePreservesLocalDeleteMadeWhilePaused() {
    QTemporaryDir temp;
    FixtureService service;
    qint64 now = service.serverTimeMs;
    Replica replica(&service, profileFor(&temp), kDeviceA, &now);
    QVERIFY(replica.history.recordActivity("book", "delete-me", now));
    sync(replica);
    replica.engine.setCategoryNetworkEnabled("full_history", false);
    QVERIFY(replica.history.remove("book", "delete-me"));
    replica.engine.setCategoryNetworkEnabled("full_history", true);
    sync(replica);
    QVERIFY(replica.history.get("book", "delete-me").isEmpty());
}

void tst_sync_category_policy::reenableConvergesConflictWithLocalChangeWinningOnlyChangedKey() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    FixtureService service;
    qint64 nowA = service.serverTimeMs;
    qint64 nowB = service.serverTimeMs;
    Replica a(&service, profileFor(&tempA), kDeviceA, &nowA);
    Replica b(&service, profileFor(&tempB), kDeviceB, &nowB);
    QVERIFY(a.history.recordActivity("book", "conflict", nowA));
    QVERIFY(a.history.recordActivity("book", "delete-me", nowA));
    QVERIFY(a.history.recordActivity("book", "unchanged", nowA));
    a.other.put("shared", "baseline");
    sync(a);
    sync(b);
    QVERIFY(!b.history.get("book", "delete-me").isEmpty());
    QCOMPARE(b.other.value("shared"), QJsonValue(QJsonObject{{"value", "baseline"}}));

    a.engine.setCategoryNetworkEnabled("full_history", false);
    QVERIFY(a.history.recordActivity("book", "conflict", nowA + 100));
    QVERIFY(a.history.remove("book", "delete-me"));
    b.other.put("shared", "while-a-paused");
    QVERIFY(b.history.recordActivity("book", "conflict", nowB + 50));
    QVERIFY(b.history.recordActivity("book", "remote-only", nowB + 51));
    sync(b);
    a.engine.setNetworkEnabled(false);
    sync(a);
    QCOMPARE(a.other.value("shared"), QJsonValue(QJsonObject{{"value", "while-a-paused"}}));

    a.engine.setCategoryNetworkEnabled("full_history", true);
    sync(a);
    a.engine.setNetworkEnabled(false);
    sync(a);
    b.engine.setNetworkEnabled(false);
    sync(b);
    QCOMPARE(a.history.get("book", "conflict").value("lastActivityAt").toLongLong(), nowA + 100);
    QVERIFY(a.history.get("book", "delete-me").isEmpty());
    QVERIFY(!a.history.get("book", "remote-only").isEmpty());
    QCOMPARE(b.history.get("book", "conflict").value("lastActivityAt").toLongLong(), nowA + 100);
    QVERIFY(b.history.get("book", "delete-me").isEmpty());
    QVERIFY(!b.history.get("book", "remote-only").isEmpty());
    QCOMPARE(a.other.value("shared"), QJsonValue(QJsonObject{{"value", "while-a-paused"}}));
    QCOMPARE(b.other.value("shared"), QJsonValue(QJsonObject{{"value", "while-a-paused"}}));
    QCOMPARE(a.history.get("book", "unchanged").value("lastActivityAt").toLongLong(), nowA);
}

void tst_sync_category_policy::reenableReplayDoesNotReapplyOlderWinnersForOtherCategories() {
    QTemporaryDir temp;
    FixtureService service;
    qint64 now = service.serverTimeMs;
    Replica replica(&service, profileFor(&temp), kDeviceA, &now);
    service.appendRemote(remoteMutation("old-other", "collection", "item", kDeviceB, now, "old"));
    sync(replica);
    const int appliesBefore = replica.other.remoteApplyCount();
    replica.engine.setCategoryNetworkEnabled("full_history", false);
    QVERIFY(replica.history.recordActivity("book", "local", now + 1));
    replica.engine.setCategoryNetworkEnabled("full_history", true);
    sync(replica);
    QCOMPARE(replica.other.remoteApplyCount(), appliesBefore);
    const QJsonObject old{{"value", "old"}};
    QCOMPARE(replica.other.value("item"), QJsonValue(old));
}

void tst_sync_category_policy::disableDropsPendingHistoryOutbox() {
    QTemporaryDir temp;
    FixtureService service;
    qint64 now = service.serverTimeMs;
    Replica replica(&service, profileFor(&temp), kDeviceA, &now);
    replica.transport.setOnline(false);
    QVERIFY(replica.history.recordActivity("book", "pending", now));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 1);
    replica.engine.setCategoryNetworkEnabled("full_history", false);
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    replica.transport.setOnline(true);
    sync(replica);
    QCOMPARE(service.acceptedMutationCount(), 0);
}

QTEST_MAIN(tst_sync_category_policy)
#include "tst_sync_category_policy.moc"
