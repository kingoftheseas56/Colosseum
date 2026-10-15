// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/ActivityStore.h"
#include "account/ActivitySyncAdapter.h"
#include "account/ProfilePaths.h"
#include "account/SyncAdapter.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncHybridClock.h"
#include "account/SyncProtocol.h"
#include "account/SyncStateStore.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <limits>
#include <utility>

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

QJsonObject malformedActivityPayload(
    const QString &eventId,
    bool syncable) {
    return QJsonObject{
        {QStringLiteral("v"), 1},
        {QStringLiteral("type"), QStringLiteral("media_completed")},
        {QStringLiteral("eventId"), eventId},
        {QStringLiteral("sessionId"), QStringLiteral("fixture-session")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("titleKey"), QStringLiteral("movie:fixture")},
        {QStringLiteral("itemKey"), QStringLiteral("movie:fixture")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")},
        {QStringLiteral("utcOffsetMinutes"), 0},
        {QStringLiteral("syncable"), syncable},
        {QStringLiteral("source"), QStringLiteral("fixture")},
        {QStringLiteral("atMs"), 1000},
        {QStringLiteral("reason"), QStringLiteral("eof")}};
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

    void setRejectRecordKey(const QString &recordKey) {
        m_rejectRecordKey = recordKey;
    }

    // Fail pull requests after the requested number of successful requests.
    // This lets restart tests stop after a durable replay page without
    // changing the transport or engine implementation.
    void setPullNetworkFailuresAfterRequests(
        int servedRequests,
        int failures) {
        m_pullFailAfterServed = servedRequests;
        m_pullNetworkFailures = failures;
    }

    void appendRemote(
        const SyncWireMutation &mutation,
        bool won) {
        FixtureJournalEntry entry;
        entry.serverSeq =
            m_nextServerSeq++;
        entry.won = won;
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

            if (!m_rejectRecordKey.isEmpty()
                && mutation.recordKey == m_rejectRecordKey) {
                QJsonObject result;
                result.insert(QStringLiteral("mutation_id"), mutation.mutationId);
                result.insert(QStringLiteral("accepted"), false);
                result.insert(QStringLiteral("code"), QStringLiteral("category_not_supported"));
                result.insert(QStringLiteral("message"), QStringLiteral("fixture server lacks this category capability"));
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
        quint64 after) {
        AccountTransportReply reply;

        if (m_pullRequestAfters.size()
                > m_pullFailAfterServed
            && m_pullNetworkFailures > 0) {
            --m_pullNetworkFailures;
            reply.networkError = true;
            reply.errorCode = QStringLiteral("offline");
            reply.errorMessage = QStringLiteral("fixture pull offline");
            return reply;
        }

        reply.statusCode = 200;

        QJsonArray entries;
        bool hasMore = false;

        for (const FixtureJournalEntry &entry :
             m_journal) {
            if (entry.serverSeq <= after)
                continue;

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
    QList<FixtureSnapshotPage> m_snapshotPages;
    int m_snapshotServed = 0;
    int m_snapshotNetworkFailures = 0;
    int m_snapshotFailAfterServed = 0;
    QStringList m_snapshotRequestTokens;
    QStringList m_pushAttachmentIds;
    QStringList m_pullRequestAfters;
    QString m_rejectRecordKey;
    int m_pullFailAfterServed = std::numeric_limits<int>::max();
    int m_pullNetworkFailures = 0;
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

    QList<int> pushBodyBytes() const {
        return m_pushBodyBytes;
    }

    QList<int> pushMutationCounts() const {
        return m_pushMutationCounts;
    }

    QList<QByteArray> pushBodyPayloads() const {
        return m_pushBodyPayloads;
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

            const QJsonArray requestMutations =
                request.body
                    .value(QStringLiteral("mutations"))
                    .toArray();
            // Capture the AccountTransportRequest at the same boundary used
            // by AccountHttpTransport before the fixture service sees it.
            // This proves the engine's admission bound against the emitted
            // compact UTF-8 HTTP body, rather than only against an internal
            // sizing helper.
            const QByteArray requestBody =
                QJsonDocument(request.body)
                    .toJson(QJsonDocument::Compact);
            m_pushBodyBytes.append(requestBody.size());
            m_pushMutationCounts.append(requestMutations.size());
            m_pushBodyPayloads.append(requestBody);

            reply =
                m_service->push(
                    requestMutations,
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
    QList<int> m_pushBodyBytes;
    QList<int> m_pushMutationCounts;
    QList<QByteArray> m_pushBodyPayloads;
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
        if (m_rejectRemote) {
            if (error)
                error->clear();
            return false;
        }

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

    bool applyRemoteAsync(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        std::function<void(bool, const QString &)> callback,
        QString *error) override {
        if (m_remoteApplyDelayMs <= 0)
            return SyncAdapter::applyRemoteAsync(
                recordKey,
                operation,
                payload,
                schemaVersion,
                std::move(callback),
                error);

        QTimer::singleShot(
            m_remoteApplyDelayMs,
            this,
            [this, recordKey, operation, payload, schemaVersion,
             callback = std::move(callback)]() mutable {
                QString applyError;
                const bool applied = applyRemote(
                    recordKey,
                    operation,
                    payload,
                    schemaVersion,
                    &applyError);
                if (callback)
                    callback(applied, applyError);
            });
        return true;
    }

    void setRemoteApplyDelayMs(int delayMs) {
        m_remoteApplyDelayMs = delayMs;
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

    void setRejectRemote(bool enabled) {
        m_rejectRemote = enabled;
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
    bool m_rejectRemote = false;
    bool m_missingRecordsAreDeletes = true;
    int m_remoteApplyCount = 0;
    int m_remoteApplyDelayMs = 0;
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

}

class tst_sync_engine final
    : public QObject {
    Q_OBJECT

private slots:
    void hlcNeverGoesBackwards();
    void trustedLocalOrderingHintsBecomeOrderedHLCs();
    void rejectedFutureCanRebaseToServiceTime();
    void stateStoreRoundTripPreservesCheckpoint();
    void stateStoreRoundTripPreservesOwnerRedo();
    void legacyStateMigratesToBoundedHistoricalReplay();
    void historicalReplayCrashReloadPromotesNormalCursor();
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
    void winningPullThenPendingPushLossKeepsServerWinner();
    void olderWinningPullRemainsSuppressed();
    void unknownWinningCategoryQuarantinesAndAdvancesCursor();
    void bannedRemotePayloadQuarantinesAndAdvancesCursor();
    void quarantinePersistsAcrossRestartWithoutBlockingLaterDomain();
    void ownerApplyFailureDoesNotAdvanceCursor();
    void asyncOwnerAckKeepsCursorBehindDurableReceipt();
    void ownerRedoRecoversBeforeReconcile();
    void staleOwnerReceiptCannotCommitNewProfile();
    void realActivityPreflightQuarantinesWithoutBlockingCollection();
    void mixedPushRejectionRetainsAckAndExplicitRetry();
    void pushBatchesStayWithinWireByteAndCountBounds();
    void signOutFlushWarnsWhenNetworkUnavailable();
    void signOutFlushReportsParkedOversize();
    void signOutFlushSucceedsAfterDrain();
    void accountSwitchUsesSeparateProfileState();
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
    source.historicalReplayCursor = 7;
    source.historicalReplayLimit = 42;
    source.historicalReplayPending = true;
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
        loaded->historicalReplayCursor,
        quint64(7));
    QCOMPARE(
        loaded->historicalReplayLimit,
        quint64(42));
    QCOMPARE(
        loaded->historicalReplayPending,
        true);
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
stateStoreRoundTripPreservesOwnerRedo() {
    SyncPersistentState source;
    source.cursor = 7;
    source.ownerRedos.append(SyncOwnerRedo{
        7,
        true,
        remoteMutation(
            QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc"),
            QStringLiteral("collection"),
            QStringLiteral("tankoban/item"),
            QString::fromLatin1(kDeviceB),
            2000,
            1,
            SyncWireOperation::Put,
            QJsonObject{{QStringLiteral("value"), QStringLiteral("remote")}})});

    QString error;
    const auto encoded = SyncStateStore::encode(source);
    const auto loaded = SyncStateStore::decode(encoded, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->ownerRedos.size(), 1);
    QCOMPARE(loaded->ownerRedos.first().serverSeq, quint64(7));
    QCOMPARE(loaded->ownerRedos.first().mutation.recordKey,
             QStringLiteral("tankoban/item"));
}

void tst_sync_engine::
legacyStateMigratesToBoundedHistoricalReplay() {
    SyncPersistentState source;
    source.cursor = 42;
    source.hlcPhysicalMs = 1000;
    source.hlcCounter = 3;
    source.serverOffsetMs = -12;

    QJsonObject legacy = SyncStateStore::encode(source);
    legacy.insert(QStringLiteral("schema_version"), 2);
    legacy.remove(QStringLiteral("historical_replay_cursor"));
    legacy.remove(QStringLiteral("historical_replay_limit"));
    legacy.remove(QStringLiteral("historical_replay_pending"));
    legacy.remove(QStringLiteral("quarantined_entries"));
    legacy.remove(QStringLiteral("rejected_mutations"));

    QString error;
    const auto migrated = SyncStateStore::decode(legacy, &error);
    QVERIFY2(migrated.has_value(), qPrintable(error));
    QCOMPARE(migrated->cursor, quint64(42));
    QCOMPARE(migrated->historicalReplayPending, true);
    QCOMPARE(migrated->historicalReplayCursor, quint64(0));
    QCOMPARE(migrated->historicalReplayLimit, quint64(42));

    QJsonObject completed = SyncStateStore::encode(*migrated);
    const auto roundTripped = SyncStateStore::decode(completed, &error);
    QVERIFY2(roundTripped.has_value(), qPrintable(error));
    QCOMPARE(roundTripped->historicalReplayPending, true);
    QCOMPARE(roundTripped->historicalReplayLimit, quint64(42));
}

void tst_sync_engine::
historicalReplayCrashReloadPromotesNormalCursor() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile = accountProfile(&temp);

    // The old cursor has already crossed the first 201 journal rows. The
    // replay generation must revisit only that bounded prefix, while the
    // local winner for this key remains authoritative.
    SyncPersistentState source;
    source.cursor = 201;
    source.hlcPhysicalMs = 3000000;
    source.hlcCounter = 4;
    source.serverOffsetMs = 0;
    const SyncWireMutation local = remoteMutation(
        QStringLiteral("99999999-9999-4999-8999-999999999999"),
        QStringLiteral("collection"),
        QStringLiteral("manga/local"),
        QString::fromLatin1(kDeviceA),
        3000000,
        4,
        SyncWireOperation::Put,
        QJsonObject{{QStringLiteral("value"), QStringLiteral("local-newer")} });
    source.mirrors[QStringLiteral("collection")].insert(
        local.recordKey,
        SyncMirrorRecord{1, local.payload});
    source.winners[QStringLiteral("collection")].insert(
        local.recordKey,
        SyncWinner{local.hlc, 1, SyncWireOperation::Put});

    for (int index = 1; index <= 250; ++index) {
        const QString suffix =
            QStringLiteral("%1").arg(index, 12, 16, QLatin1Char('0'));
        const QString mutationId =
            QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-") + suffix;
        const bool isLocalKey = index == 100;
        service.appendRemote(
            remoteMutation(
                mutationId,
                QStringLiteral("collection"),
                isLocalKey
                    ? QStringLiteral("manga/local")
                    : QStringLiteral("manga/historical-%1").arg(index),
                QString::fromLatin1(kDeviceB),
                service.serverTimeMs,
                static_cast<quint64>(index),
                SyncWireOperation::Put,
                QJsonObject{{QStringLiteral("value"),
                             isLocalKey
                                 ? QStringLiteral("remote-older")
                                 : QStringLiteral("historical-%1").arg(index)}}),
            true);
    }

    // Write a schema-v2 checkpoint. Loading it starts the one-time replay from
    // zero and captures 201 as its durable upper bound.
    QJsonObject legacy = SyncStateStore::encode(source);
    legacy.insert(QStringLiteral("schema_version"), 2);
    legacy.remove(QStringLiteral("historical_replay_cursor"));
    legacy.remove(QStringLiteral("historical_replay_limit"));
    legacy.remove(QStringLiteral("historical_replay_pending"));
    legacy.remove(QStringLiteral("quarantined_entries"));
    legacy.remove(QStringLiteral("rejected_mutations"));
    QSaveFile stateFile(profile.syncStatePath());
    QVERIFY(QDir().mkpath(QFileInfo(profile.syncStatePath()).absolutePath()));
    QVERIFY(stateFile.open(QIODevice::WriteOnly));
    const QByteArray legacyBytes =
        QJsonDocument(legacy).toJson(QJsonDocument::Compact);
    QCOMPARE(stateFile.write(legacyBytes), legacyBytes.size());
    QVERIFY(stateFile.commit());

    SyntheticAdapter adapter;

    {
        FixtureSyncTransport transport(&service);
        AccountClient client(&transport);
        client.setAccessToken(QByteArrayLiteral("fixture-access"));
        SyncAdapterRegistry registry;
        QVERIFY(registry.registerAdapter(&adapter));
        adapter.putLocal(QStringLiteral("manga/local"),
                         QStringLiteral("local-newer"));

        SyncEngine engine(&client, &registry, [&now]() { return now; });
        engine.setAutomaticSchedulingEnabled(false);
        engine.setNetworkEnabled(false);
        QString error;
        QVERIFY2(engine.start(profile, QString::fromLatin1(kDeviceA), &error),
                 qPrintable(error));
        service.setPullNetworkFailuresAfterRequests(1, 1);
        engine.setNetworkEnabled(true);

        QTRY_COMPARE(service.pullRequestAfters().size(), 2);
        QTRY_COMPARE(engine.state(), SyncEngine::State::Retrying);
        QCOMPARE(service.pullRequestAfters().at(0), QStringLiteral("0"));
        QCOMPARE(service.pullRequestAfters().at(1), QStringLiteral("200"));
    }

    SyncStateStore store;
    QString error;
    const auto midReplay = store.load(profile.syncStatePath(), &error);
    QVERIFY2(midReplay.has_value(), qPrintable(error));
    QVERIFY(midReplay->historicalReplayPending);
    QCOMPARE(midReplay->historicalReplayCursor, quint64(200));
    QCOMPARE(midReplay->historicalReplayLimit, quint64(201));
    QCOMPARE(midReplay->cursor, quint64(201));

    {
        FixtureSyncTransport transport(&service);
        AccountClient client(&transport);
        client.setAccessToken(QByteArrayLiteral("fixture-access"));
        SyncAdapterRegistry registry;
        QVERIFY(registry.registerAdapter(&adapter));
        adapter.putLocal(QStringLiteral("manga/local"),
                         QStringLiteral("local-newer"));

        SyncEngine engine(&client, &registry, [&now]() { return now; });
        engine.setAutomaticSchedulingEnabled(false);
        engine.setNetworkEnabled(false);
        QVERIFY2(engine.start(profile, QString::fromLatin1(kDeviceA), &error),
                 qPrintable(error));
        engine.setNetworkEnabled(true);

        QTRY_COMPARE(engine.historicalReplayPending(), false);
        QTRY_COMPARE(engine.cursor(), quint64(201));
        QCOMPARE(service.pullRequestAfters().last(), QStringLiteral("200"));
        QCOMPARE(adapter.value(QStringLiteral("manga/local")),
                 QStringLiteral("local-newer"));
        QCOMPARE(adapter.value(QStringLiteral("manga/historical-1")),
                 QStringLiteral("historical-1"));
    }

    const auto completed = store.load(profile.syncStatePath(), &error);
    QVERIFY2(completed.has_value(), qPrintable(error));
    QCOMPARE(completed->historicalReplayPending, false);
    QCOMPARE(completed->historicalReplayCursor, quint64(0));
    QCOMPARE(completed->historicalReplayLimit, quint64(0));
    // Final replay checkpoint promotes the normal cursor to the furthest
    // recovered sequence, preventing the already-replayed page from being
    // downloaded again after restart.
    QCOMPARE(completed->cursor, quint64(201));
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
winningPullThenPendingPushLossKeepsServerWinner() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
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
    QTRY_COMPARE(replica.engine.cursor(), quint64(2));
    QCOMPARE(
        replica.adapter.value(QStringLiteral("manga/item")),
        QStringLiteral("server-newer"));
    QCOMPARE(replica.engine.cursor(), quint64(2));
    QCOMPARE(service.acceptedMutationCount(), 1);
    QCOMPARE(service.journal().size(), 2);
    QVERIFY(!service.journal().constLast().won);
}

void tst_sync_engine::
olderWinningPullRemainsSuppressed() {
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
        true);

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
unknownWinningCategoryQuarantinesAndAdvancesCursor() {
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
        replica.engine.quarantinedEntryCount(),
        1);
    QCOMPARE(
        replica.engine.state(),
        SyncEngine::State::Idle);
    QCOMPARE(
        replica.engine.cursor(),
        quint64(1));
    QCOMPARE(
        replica.engine.quarantinedEntryCount(),
        1);
    QCOMPARE(
        replica.engine.lastErrorCode(),
        QStringLiteral("adapter_not_registered"));
}

void tst_sync_engine::
bannedRemotePayloadQuarantinesAndAdvancesCursor() {
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
        replica.engine.quarantinedEntryCount(),
        1);
    QCOMPARE(
        replica.engine.state(),
        SyncEngine::State::Idle);
    QCOMPARE(
        replica.engine.cursor(),
        quint64(1));
    QCOMPARE(
        replica.engine.quarantinedEntryCount(),
        1);
    QCOMPARE(
        replica.adapter.remoteApplyCount(),
        0);
}

void tst_sync_engine::
quarantinePersistsAcrossRestartWithoutBlockingLaterDomain() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile = accountProfile(&temp);
    service.appendRemote(
        remoteMutation(
            QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc"),
            QStringLiteral("extension_roster"),
            QStringLiteral("extension/item"),
            QString::fromLatin1(kDeviceB),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{{QStringLiteral("value"), QStringLiteral("remote")}}),
        true);
    service.appendRemote(
        remoteMutation(
            QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccd"),
            QStringLiteral("collection"),
            QStringLiteral("manga/after-quarantine"),
            QString::fromLatin1(kDeviceB),
            now,
            1,
            SyncWireOperation::Put,
            QJsonObject{{QStringLiteral("value"), QStringLiteral("live")}}),
        true);

    {
        Replica first(&service, profile, QString::fromLatin1(kDeviceA), &now);
        first.engine.setNetworkEnabled(true);
        QTRY_COMPARE(first.engine.quarantinedEntryCount(), 1);
        QTRY_COMPARE(first.engine.cursor(), quint64(2));
        QCOMPARE(first.adapter.value(QStringLiteral("manga/after-quarantine")),
                 QStringLiteral("live"));
        QVERIFY(first.engine.stopPreservingOutbox());
    }

    {
        FixtureSyncTransport transport(&service);
        AccountClient client(&transport);
        client.setAccessToken(QByteArrayLiteral("fixture-access"));
        SyncAdapterRegistry registry;
        SyntheticAdapter adapter;
        QVERIFY(registry.registerAdapter(&adapter));
        SyncEngine restarted(&client, &registry, [&now]() { return now; });
        restarted.setAutomaticSchedulingEnabled(false);
        restarted.setNetworkEnabled(false);
        QString error;
        QVERIFY2(restarted.start(profile, QString::fromLatin1(kDeviceA), &error),
                 qPrintable(error));
        QCOMPARE(restarted.quarantinedEntryCount(), 1);
        QCOMPARE(restarted.cursor(), quint64(2));
        restarted.setNetworkEnabled(true);
        QTRY_VERIFY(service.pullRequestAfters().size() >= 2);
        QCOMPARE(restarted.quarantinedEntryCount(), 1);
        QCOMPARE(restarted.cursor(), quint64(2));
        QCOMPARE(service.pullRequestAfters().last(), QStringLiteral("2"));
    }
}

void tst_sync_engine::ownerApplyFailureDoesNotAdvanceCursor() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    service.appendRemote(
        remoteMutation(
            QStringLiteral("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"),
            QStringLiteral("collection"),
            QStringLiteral("manga/item"),
            QString::fromLatin1(kDeviceB),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{{QStringLiteral("value"), QStringLiteral("remote")}}),
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);
    replica.adapter.setRejectRemote(true);
    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(replica.engine.state(), SyncEngine::State::Blocked);
    QCOMPARE(replica.engine.cursor(), quint64(0));
    QCOMPARE(replica.engine.quarantinedEntryCount(), 0);
    QCOMPARE(replica.engine.lastErrorCode(), QStringLiteral("adapter_apply_failed"));
    QCOMPARE(replica.adapter.remoteApplyCount(), 0);

    // The existing sync-repair action is also the bounded recovery path for
    // a durable owner failure. Once the owner is healthy again, it replays
    // the redo before reopening network work.
    replica.adapter.setRejectRemote(false);
    replica.engine.retryRejectedMutations();
    QTRY_COMPARE(replica.adapter.remoteApplyCount(), 1);
    QTRY_COMPARE(replica.engine.cursor(), quint64(1));
    QTRY_COMPARE(replica.engine.state(), SyncEngine::State::Idle);
}

void tst_sync_engine::asyncOwnerAckKeepsCursorBehindDurableReceipt() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    service.appendRemote(
        remoteMutation(
            QStringLiteral("abababab-abab-4aba-8aba-abababababab"),
            QStringLiteral("collection"),
            QStringLiteral("manga/async-owner"),
            QString::fromLatin1(kDeviceB),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{{QStringLiteral("value"), QStringLiteral("durable")}}),
        true);

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);
    replica.adapter.setRemoteApplyDelayMs(100);
    replica.engine.setNetworkEnabled(true);

    SyncStateStore stateStore;
    bool sawRedo = false;
    QTRY_VERIFY((sawRedo = [&]() {
        QString error;
        const auto state = stateStore.load(replica.profile.syncStatePath(), &error);
        return state.has_value() && !state->ownerRedos.isEmpty();
    }()));
    QCOMPARE(replica.engine.cursor(), quint64(0));
    QCOMPARE(replica.engine.pendingOutboxCount(), 0);
    QCOMPARE(replica.adapter.remoteApplyCount(), 0);

    QTRY_COMPARE(replica.adapter.remoteApplyCount(), 1);
    QTRY_COMPARE(replica.engine.cursor(), quint64(1));
    QCOMPARE(replica.adapter.value(QStringLiteral("manga/async-owner")),
             QStringLiteral("durable"));
    QString finalStateError;
    QTRY_VERIFY2(([&]() {
        finalStateError.clear();
        const auto state = stateStore.load(replica.profile.syncStatePath(), &finalStateError);
        return state.has_value() && state->ownerRedos.isEmpty();
    })(), qPrintable(finalStateError));
    QCOMPARE(replica.engine.pendingOutboxCount(), 0);
}

void tst_sync_engine::ownerRedoRecoversBeforeReconcile() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const ProfilePaths profile = accountProfile(&temp);
    QVERIFY(QDir().mkpath(QFileInfo(profile.syncStatePath()).absolutePath()));
    SyncPersistentState source;
    const SyncWireMutation redoMutation = remoteMutation(
        QStringLiteral("cdcdcdcd-cdcd-4cdc-8cdc-cdcdcdcdcdcd"),
        QStringLiteral("collection"),
        QStringLiteral("manga/recovered"),
        QString::fromLatin1(kDeviceB),
        2000000,
        1,
        SyncWireOperation::Put,
        QJsonObject{{QStringLiteral("value"), QStringLiteral("redo")} });
    source.ownerRedos.append(SyncOwnerRedo{5, true, redoMutation});

    QSaveFile stateFile(profile.syncStatePath());
    QVERIFY(stateFile.open(QIODevice::WriteOnly));
    const QByteArray bytes =
        QJsonDocument(SyncStateStore::encode(source))
            .toJson(QJsonDocument::Compact);
    QCOMPARE(stateFile.write(bytes), bytes.size());
    QVERIFY(stateFile.commit());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    FixtureSyncTransport transport(&service);
    AccountClient client(&transport);
    client.setAccessToken(QByteArrayLiteral("fixture-access"));
    SyncAdapterRegistry registry;
    SyntheticAdapter adapter;
    QVERIFY(registry.registerAdapter(&adapter));
    SyncEngine engine(&client, &registry, [&now]() { return now; });
    engine.setAutomaticSchedulingEnabled(false);
    engine.setNetworkEnabled(false);

    QString error;
    QVERIFY2(engine.start(profile, QString::fromLatin1(kDeviceA), &error),
             qPrintable(error));
    QTRY_COMPARE(adapter.remoteApplyCount(), 1);
    QTRY_COMPARE(engine.cursor(), quint64(5));
    QCOMPARE(adapter.value(QStringLiteral("manga/recovered")),
             QStringLiteral("redo"));
    QCOMPARE(engine.pendingOutboxCount(), 0);

    SyncStateStore store;
    QTRY_VERIFY(([&]() {
        QString loadError;
        const auto loaded = store.load(profile.syncStatePath(), &loadError);
        return loaded.has_value() && loaded->ownerRedos.isEmpty();
    })());
}

void tst_sync_engine::staleOwnerReceiptCannotCommitNewProfile() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    service.appendRemote(
        remoteMutation(
            QStringLiteral("efefefef-efef-4efe-8efe-efefefefefef"),
            QStringLiteral("collection"),
            QStringLiteral("manga/stale-owner"),
            QString::fromLatin1(kDeviceB),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{{QStringLiteral("value"), QStringLiteral("old-profile")}}),
        true);

    FixtureSyncTransport transport(&service);
    AccountClient client(&transport);
    client.setAccessToken(QByteArrayLiteral("fixture-access"));
    SyncAdapterRegistry registry;
    SyntheticAdapter oldAdapter;
    QVERIFY(registry.registerAdapter(&oldAdapter));

    SyncEngine engine(&client, &registry, [&now]() { return now; });
    engine.setAutomaticSchedulingEnabled(false);
    engine.setNetworkEnabled(false);

    const ProfilePaths oldProfile = accountProfile(&temp, QString::fromLatin1(kAccountA));
    const ProfilePaths newProfile = accountProfile(&temp, QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"));
    QString error;
    QVERIFY2(engine.start(oldProfile, QString::fromLatin1(kDeviceA), &error),
             qPrintable(error));

    oldAdapter.setRemoteApplyDelayMs(500);
    engine.setNetworkEnabled(true);

    SyncStateStore store;
    QTRY_VERIFY(([&]() {
        QString loadError;
        const auto state = store.load(oldProfile.syncStatePath(), &loadError);
        return state.has_value() && !state->ownerRedos.isEmpty();
    })());

    QVERIFY2(engine.stopPreservingOutbox(&error), qPrintable(error));
    QVERIFY(!engine.active());

    // Keep the old delayed adapter alive so its late callback reaches the
    // registry after a new profile and adapter have taken the category slot.
    QVERIFY(registry.unregisterAdapter(QStringLiteral("collection")));
    SyntheticAdapter newAdapter;
    QVERIFY(registry.registerAdapter(&newAdapter));

    QVERIFY2(engine.start(newProfile, QString::fromLatin1(kDeviceA), &error),
             qPrintable(error));
    QCOMPARE(engine.cursor(), quint64(0));
    QCOMPARE(newAdapter.remoteApplyCount(), 0);

    QTest::qWait(650);
    QCOMPARE(engine.cursor(), quint64(0));
    QCOMPARE(newAdapter.remoteApplyCount(), 0);
}

void tst_sync_engine::realActivityPreflightQuarantinesWithoutBlockingCollection() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    const QString eventId = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    service.appendRemote(
        remoteMutation(
            QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaab"),
            QStringLiteral("activity_fact"),
            QStringLiteral("activity/") + eventId,
            QString::fromLatin1(kDeviceB),
            now,
            0,
            SyncWireOperation::Put,
            malformedActivityPayload(eventId, false)),
        true);
    service.appendRemote(
        remoteMutation(
            QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaac"),
            QStringLiteral("collection"),
            QStringLiteral("manga/after-activity"),
            QString::fromLatin1(kDeviceB),
            now,
            1,
            SyncWireOperation::Put,
            QJsonObject{{QStringLiteral("value"), QStringLiteral("still-live")}}),
        true);

    FixtureSyncTransport transport(&service);
    AccountClient client(&transport);
    ActivityStore activityStore;
    ActivitySyncAdapter activityAdapter(&activityStore);
    SyntheticAdapter collectionAdapter;
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&activityAdapter));
    QVERIFY(registry.registerAdapter(&collectionAdapter));
    SyncEngine engine(&client, &registry, [&now]() { return now; });
    client.setAccessToken(QByteArrayLiteral("fixture-access"));
    engine.setAutomaticSchedulingEnabled(false);
    engine.setNetworkEnabled(false);

    QString error;
    QVERIFY2(engine.start(accountProfile(&temp), QString::fromLatin1(kDeviceA), &error),
             qPrintable(error));
    engine.setNetworkEnabled(true);
    QTRY_COMPARE(engine.quarantinedEntryCount(), 1);
    QTRY_COMPARE(engine.cursor(), quint64(2));
    QCOMPARE(engine.state(), SyncEngine::State::Idle);
    QCOMPARE(engine.lastErrorCode(), QStringLiteral("activity_not_syncable"));
    QCOMPARE(activityStore.portableSyncFacts().size(), 0);
    QCOMPARE(collectionAdapter.value(QStringLiteral("manga/after-activity")),
             QStringLiteral("still-live"));
}

void tst_sync_engine::mixedPushRejectionRetainsAckAndExplicitRetry() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    service.setRejectRecordKey(QStringLiteral("manga/second"));

    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);
    replica.adapter.putLocal(QStringLiteral("manga/first"), QStringLiteral("one"));
    replica.adapter.putLocal(QStringLiteral("manga/second"), QStringLiteral("two"));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 2);

    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 1);
    QTRY_COMPARE(replica.engine.rejectedMutationCount(), 1);
    QCOMPARE(replica.engine.state(), SyncEngine::State::Idle);
    QCOMPARE(replica.engine.lastErrorCode(), QStringLiteral("category_not_supported"));
    QCOMPARE(service.acceptedMutationCount(), 1);
    QCOMPARE(service.journal().size(), 1);

    service.setRejectRecordKey(QString());
    replica.engine.retryRejectedMutations();

    QTRY_COMPARE(replica.engine.rejectedMutationCount(), 0);
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 0);
    QCOMPARE(service.acceptedMutationCount(), 2);
    QCOMPARE(service.journal().size(), 2);
}

void tst_sync_engine::pushBatchesStayWithinWireByteAndCountBounds() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureSyncService service;
    qint64 now = service.serverTimeMs;
    Replica replica(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);

    const QString largeValue(900, QChar(0x4e00));
    for (int index = 0; index < 100; ++index) {
        replica.adapter.putLocal(
            QStringLiteral("manga/large-%1")
                .arg(index, 3, 10, QLatin1Char('0')),
            largeValue);
    }
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 100);

    // AccountClient emits completion after the fixture has accepted each
    // emitted push. Wait on that event instead of relying on QTRY's default
    // five-second polling window while the debug build drains large bodies.
    const auto waitForPushDrain = [&]() {
        QSignalSpy completed(
            &replica.client,
            &AccountClient::completed);
        const QDeadlineTimer deadline(15000);
        while (replica.engine.pendingOutboxCount() != 0) {
            if (deadline.hasExpired())
                return false;

            const qint64 remaining = deadline.remainingTime();
            const int waitMs = static_cast<int>(
                qMax<qint64>(1, remaining));
            if (!completed.wait(waitMs))
                return false;
        }
        return true;
    };

    replica.engine.setNetworkEnabled(true);
    QVERIFY2(
        waitForPushDrain(),
        "timed out waiting for the push completion signal while draining the byte-bounded batch");

    // Add a second bounded batch whose 100 mutations should exercise the
    // count ceiling while the first batch exercises the byte ceiling.
    replica.engine.setNetworkEnabled(false);
    for (int index = 0; index < 100; ++index) {
        replica.adapter.putLocal(
            QStringLiteral("manga/small-%1")
                .arg(index, 3, 10, QLatin1Char('0')),
            QStringLiteral("small"));
    }
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 100);
    replica.engine.setNetworkEnabled(true);
    QVERIFY2(
        waitForPushDrain(),
        "timed out waiting for the push completion signal while draining the count-bounded batch");

    const QList<int> bodyBytes = replica.transport.pushBodyBytes();
    const QList<int> mutationCounts =
        replica.transport.pushMutationCounts();
    const QList<QByteArray> bodies =
        replica.transport.pushBodyPayloads();
    QVERIFY(!bodyBytes.isEmpty());
    QCOMPARE(bodyBytes.size(), mutationCounts.size());
    QCOMPARE(bodyBytes.size(), bodies.size());

    bool sawCountLimit = false;
    int totalMutations = 0;
    for (int index = 0; index < bodyBytes.size(); ++index) {
        QVERIFY2(bodyBytes.at(index) <= 64 * 1024,
                 "an emitted AccountTransport push body exceeded 64 KiB");
        QVERIFY(mutationCounts.at(index) > 0);
        QVERIFY(mutationCounts.at(index) <= 100);
        totalMutations += mutationCounts.at(index);
        sawCountLimit = sawCountLimit || mutationCounts.at(index) == 100;

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(bodies.at(index), &parseError);
        QVERIFY2(parseError.error == QJsonParseError::NoError,
                 qPrintable(parseError.errorString()));
        QVERIFY(document.isObject());
        QCOMPARE(
            bodies.at(index),
            QJsonDocument(document.object())
                .toJson(QJsonDocument::Compact));
        QCOMPARE(
            bodies.at(index),
            syncWirePushRequestBytes(
                document.object()
                    .value(QStringLiteral("mutations"))
                    .toArray()));
    }
    QCOMPARE(totalMutations, 200);
    QVERIFY(sawCountLimit);
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

void tst_sync_engine::signOutFlushReportsParkedOversize() {
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
        QStringLiteral("manga/oversize"),
        QString(70 * 1024, QLatin1Char('x')));
    replica.adapter.putLocal(
        QStringLiteral("manga/fitting"),
        QStringLiteral("small"));
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 2);

    QSignalSpy spy(
        &replica.engine,
        &SyncEngine::signOutFlushFinished);
    replica.engine.setNetworkEnabled(true);

    QTRY_COMPARE(replica.engine.rejectedMutationCount(), 1);
    QTRY_COMPARE(replica.engine.pendingOutboxCount(), 1);
    QCOMPARE(replica.engine.state(), SyncEngine::State::Idle);
    QCOMPARE(service.journal().size(), 1);
    QCOMPARE(service.journal().constFirst().mutation.recordKey,
             QStringLiteral("manga/fitting"));
    QVERIFY(!replica.transport.pushBodyBytes().isEmpty());
    for (const int bodyBytes : replica.transport.pushBodyBytes())
        QVERIFY(bodyBytes <= 64 * 1024);

    replica.engine.beginSignOutFlush();
    QTRY_COMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toBool(), false);
    QCOMPARE(args.at(1).toString(), QStringLiteral("sync_unsynced_retained"));
    QCOMPARE(replica.engine.pendingOutboxCount(), 1);
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

QTEST_MAIN(tst_sync_engine)
#include "tst_sync_engine.moc"
