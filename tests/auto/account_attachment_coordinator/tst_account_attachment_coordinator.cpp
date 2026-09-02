// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.
//
// Arc 36 Wave 4B lane N-16: the attachment orchestrator tests. The fixture
// transport/service mirror the FixtureSyncTransport/FixtureSyncService
// patterns from tests/auto/sync_engine/tst_sync_engine.cpp, extended with
// the N-12 attachment lifecycle routes (begin/get/commit with the frozen
// open/uploaded/committed/aborted state machine and idempotent begin and
// commit). The coordinator is driven exactly as the embedder (N-17) will:
// client + engine + profile paths injected, verifier callback wired,
// progress/finished recorded.

#include "account/AccountAttachmentCoordinator.h"
#include "account/AccountAttachmentReceipt.h"
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
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <memory>

namespace {
constexpr auto kAccountA =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceA =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kDeviceB =
    "22222222-2222-4222-8222-222222222222";
constexpr auto kAttachmentId =
    "4f4f4f4f-4f4f-444f-8f4f-4f4f4f4f4f4f";
constexpr auto kOtherAttachmentId =
    "6e6e6e6e-6e6e-446e-8e6e-6e6e6e6e6e6e";

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

struct FixtureAttachmentRecord {
    QString sourceKind;
    QString sourceSemanticDigest;
    QString state = QStringLiteral("open");
};

QString recordIdentity(
    const QString &category,
    const QString &recordKey) {
    return category
        + QChar(0x1f)
        + recordKey;
}

// Fixture attachment service: the N-12 lifecycle (idempotent begin with
// conflict on mismatched identity, account/device scoped get, idempotent
// commit, aborted terminal) on top of the same journal machinery the sync
// engine fixture uses. Every route records an event so orchestration order
// assertions are exact.
class FixtureAttachmentService {
public:
    qint64 serverTimeMs = 2000000;
    qint64 maxFutureSkewMs = 10 * 60 * 1000;
    QStringList events;

    QString attachmentState(
        const QString &attachmentId) const {
        return m_attachments
            .value(attachmentId)
            .state;
    }

    bool hasAttachment(
        const QString &attachmentId) const {
        return m_attachments.contains(
            attachmentId);
    }

    const QStringList &pushAttachmentIds() const {
        return m_pushAttachmentIds;
    }

    int eventCount(const QString &prefix) const {
        int count = 0;
        for (const QString &event : events) {
            if (event.startsWith(prefix))
                ++count;
        }
        return count;
    }

    int acceptedMutationCount() const {
        return m_journal.size();
    }

    int firstEvent(const QString &prefix) const {
        for (int index = 0;
             index < events.size();
             ++index) {
            if (events.at(index)
                    .startsWith(prefix))
                return index;
        }
        return -1;
    }

    void setSnapshotPages(
        const QList<FixtureSnapshotPage> &pages) {
        m_snapshotPages = pages;
        m_snapshotServed = 0;
    }

    // Makes the next snapshot requests fail with a network error once the
    // given number of pages has already been served.
    void setSnapshotNetworkFailuresAfterPages(
        int servedPages,
        int failures) {
        m_snapshotFailAfterServed = servedPages;
        m_snapshotNetworkFailures = failures;
    }

    AccountTransportReply begin(
        const QString &attachmentId,
        const QString &sourceKind,
        const QString &sourceSemanticDigest) {
        events << QStringLiteral("begin");

        const auto existing =
            m_attachments.constFind(
                attachmentId);
        if (existing
            != m_attachments.constEnd()) {
            if (existing->sourceKind
                    != sourceKind
                || existing->sourceSemanticDigest
                    != sourceSemanticDigest) {
                return apiError(
                    409,
                    QStringLiteral(
                        "attachment_conflict"),
                    QStringLiteral(
                        "That profile attachment conflicts with an existing attachment."));
            }
            return attachmentView(
                attachmentId,
                existing->state);
        }

        FixtureAttachmentRecord record;
        record.sourceKind = sourceKind;
        record.sourceSemanticDigest =
            sourceSemanticDigest;
        m_attachments.insert(
            attachmentId,
            record);
        return attachmentView(
            attachmentId,
            record.state);
    }

    AccountTransportReply get(
        const QString &attachmentId) {
        events << QStringLiteral("get");

        const auto existing =
            m_attachments.constFind(
                attachmentId);
        if (existing
            == m_attachments.constEnd()) {
            return apiError(
                404,
                QStringLiteral(
                    "attachment_not_found"),
                QStringLiteral(
                    "That profile attachment was not found."));
        }
        return attachmentView(
            attachmentId,
            existing->state);
    }

    AccountTransportReply commit(
        const QString &attachmentId) {
        events << QStringLiteral("commit");

        const auto existing =
            m_attachments.find(
                attachmentId);
        if (existing
            == m_attachments.end()) {
            return apiError(
                404,
                QStringLiteral(
                    "attachment_not_found"),
                QStringLiteral(
                    "That profile attachment was not found."));
        }
        if (existing->state
            == QLatin1String("aborted")) {
            return apiError(
                409,
                QStringLiteral(
                    "attachment_not_active"),
                QStringLiteral(
                    "That profile attachment is no longer accepting changes."));
        }
        existing->state =
            QStringLiteral(
                "committed");
        return attachmentView(
            attachmentId,
            existing->state);
    }

    AccountTransportReply push(
        const QJsonArray &mutations,
        const QString &attachmentId) {
        m_pushAttachmentIds.append(
            attachmentId);
        events
            << (attachmentId.isEmpty()
                    ? QStringLiteral(
                          "push:ordinary")
                    : QStringLiteral(
                          "push:")
                          + attachmentId);

        if (!attachmentId.isEmpty()) {
            const auto existing =
                m_attachments.constFind(
                    attachmentId);
            if (existing
                == m_attachments
                       .constEnd()) {
                return apiError(
                    404,
                    QStringLiteral(
                        "attachment_not_found"),
                    QStringLiteral(
                        "That profile attachment was not found."));
            }
            if (existing->state
                    == QLatin1String(
                        "committed")
                || existing->state
                    == QLatin1String(
                        "aborted")) {
                return apiError(
                    409,
                    QStringLiteral(
                        "attachment_not_active"),
                    QStringLiteral(
                        "That profile attachment is no longer accepting changes."));
            }
        }

        AccountTransportReply reply;
        reply.statusCode = 200;
        bool anyProcessed = false;

        QJsonArray results;
        for (const QJsonValue &value :
             mutations) {
            if (!value.isObject())
                continue;

            const auto parsed =
                syncWireMutationFromJson(
                    value.toObject());
            if (!parsed.has_value()) {
                results.append(
                    rejectedResult(
                        value.toObject()
                            .value(
                                QStringLiteral(
                                    "mutation_id"))
                            .toString(),
                        QStringLiteral(
                            "invalid_mutation")));
                continue;
            }

            const SyncWireMutation mutation =
                *parsed;

            const auto duplicate =
                m_idempotency.constFind(
                    mutation.mutationId);
            if (duplicate
                != m_idempotency
                       .constEnd()) {
                results.append(
                    acceptedResult(
                        mutation.mutationId,
                        duplicate->serverSeq,
                        duplicate->won));
                continue;
            }

            if (mutation.hlc.physicalMs
                > serverTimeMs
                    + maxFutureSkewMs) {
                results.append(
                    rejectedResult(
                        mutation.mutationId,
                        QStringLiteral(
                            "clock_skew")));
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
            entry.mutation = mutation;
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

            anyProcessed = true;
            results.append(
                acceptedResult(
                    mutation.mutationId,
                    entry.serverSeq,
                    won));
        }

        // The first accepted attached mutation atomically advances the
        // attachment open -> uploaded.
        if (!attachmentId.isEmpty()
            && anyProcessed) {
            const auto existing =
                m_attachments.find(
                    attachmentId);
            if (existing
                    != m_attachments
                           .end()
                && existing->state
                    == QLatin1String(
                        "open")) {
                existing->state =
                    QStringLiteral(
                        "uploaded");
            }
        }

        reply.body.insert(
            QStringLiteral(
                "server_time_ms"),
            QString::number(
                serverTimeMs));
        reply.body.insert(
            QStringLiteral(
                "results"),
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
            QStringLiteral(
                "entries"),
            entries);
        reply.body.insert(
            QStringLiteral(
                "has_more"),
            hasMore);
        return reply;
    }

    AccountTransportReply snapshot(
        const QString &pageToken) {
        events << QStringLiteral(
                      "snapshot:")
            + pageToken;

        AccountTransportReply reply;

        if (m_snapshotServed
                >= m_snapshotFailAfterServed
            && m_snapshotNetworkFailures
                > 0) {
            --m_snapshotNetworkFailures;
            reply.networkError = true;
            reply.errorCode =
                QStringLiteral(
                    "offline");
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
            QStringLiteral(
                "cursor"),
            QString::number(
                page.cursor));
        reply.body.insert(
            QStringLiteral(
                "entries"),
            entries);
        reply.body.insert(
            QStringLiteral(
                "has_more"),
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

private:
    quint64 m_nextServerSeq = 1;
    QList<FixtureJournalEntry> m_journal;
    QHash<QString, FixtureMutationResult>
        m_idempotency;
    QHash<QString, SyncWireMutation>
        m_current;
    QHash<QString, FixtureAttachmentRecord>
        m_attachments;

    QList<FixtureSnapshotPage> m_snapshotPages;
    int m_snapshotServed = 0;
    int m_snapshotNetworkFailures = 0;
    int m_snapshotFailAfterServed = 0;
    QStringList m_pushAttachmentIds;

    static AccountTransportReply apiError(
        int statusCode,
        const QString &errorCode,
        const QString &errorMessage) {
        AccountTransportReply reply;
        reply.statusCode = statusCode;
        reply.errorCode = errorCode;
        reply.errorMessage =
            errorMessage;
        return reply;
    }

    static QJsonObject rejectedResult(
        const QString &mutationId,
        const QString &code) {
        QJsonObject result;
        result.insert(
            QStringLiteral(
                "mutation_id"),
            mutationId);
        result.insert(
            QStringLiteral(
                "accepted"),
            false);
        result.insert(
            QStringLiteral("code"),
            code);
        return result;
    }

    static QJsonObject acceptedResult(
        const QString &mutationId,
        quint64 serverSeq,
        bool won) {
        QJsonObject result;
        result.insert(
            QStringLiteral(
                "mutation_id"),
            mutationId);
        result.insert(
            QStringLiteral(
                "accepted"),
            true);
        result.insert(
            QStringLiteral(
                "server_seq"),
            QString::number(
                serverSeq));
        result.insert(
            QStringLiteral("won"),
            won);
        return result;
    }

    AccountTransportReply attachmentView(
        const QString &attachmentId,
        const QString &state) const {
        AccountTransportReply reply;
        reply.statusCode = 200;
        reply.body.insert(
            QStringLiteral(
                "attachment_id"),
            attachmentId);
        reply.body.insert(
            QStringLiteral(
                "device_id"),
            QString::fromLatin1(
                kDeviceA));
        reply.body.insert(
            QStringLiteral(
                "baseline_server_seq"),
            0);
        reply.body.insert(
            QStringLiteral("state"),
            state);
        return reply;
    }
};

// Fixture transport: route dispatch mirrors FixtureSyncTransport from the
// sync engine tests, extended with the three attachment lifecycle routes.
class FixtureAttachmentTransport final
    : public AccountTransport {
    Q_OBJECT

public:
    explicit FixtureAttachmentTransport(
        FixtureAttachmentService *service,
        QObject *parent = nullptr)
        : AccountTransport(parent),
          m_service(service) {}

    void setOnline(bool online) {
        m_online = online;
    }

    // The service processes the next commit (state advances durably) but
    // the reply is lost, like a connection drop after the server handled
    // the request.
    void dropNextCommitResponse() {
        m_dropNextCommit = true;
    }

    // The service processes begin or an attached upload, but the client sees
    // a lost response. These are the crash boundaries immediately after the
    // server has durably accepted the operation.
    void dropNextBeginResponse() {
        m_dropNextBegin = true;
    }

    void dropNextPushResponse() {
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

        const QString beginPath =
            QStringLiteral(
                "/v1/profile/attachments");
        const QString attachmentPrefix =
            QStringLiteral(
                "/v1/profile/attachments/");
        const QString commitSuffix =
            QStringLiteral("/commit");

        if (request.method
                == QByteArrayLiteral("POST")
            && request.path == beginPath) {
            const AccountTransportReply beginReply =
                m_service->begin(
                    request.body
                        .value(
                            QStringLiteral(
                                "attachment_id"))
                        .toString(),
                    request.body
                        .value(
                            QStringLiteral(
                                "source_kind"))
                        .toString(),
                    request.body
                        .value(
                            QStringLiteral(
                                "source_semantic_digest"))
                        .toString());
            if (m_dropNextBegin) {
                m_dropNextBegin = false;
                AccountTransportReply dropped;
                dropped.networkError = true;
                dropped.errorCode =
                    QStringLiteral("offline");
                dropped.errorMessage =
                    QStringLiteral(
                        "fixture lost begin response");
                emit finished(requestId, dropped);
                return;
            }
            emit finished(requestId, beginReply);
            return;
        }

        if (request.method
                == QByteArrayLiteral("POST")
            && request.path.startsWith(
                   attachmentPrefix)
            && request.path.endsWith(
                   commitSuffix)) {
            const QString id =
                request.path.mid(
                    attachmentPrefix
                        .size(),
                    request.path.size()
                        - attachmentPrefix
                              .size()
                        - commitSuffix
                              .size());
            reply = m_service->commit(id);
            if (m_dropNextCommit) {
                m_dropNextCommit = false;
                AccountTransportReply
                    dropped;
                dropped.networkError =
                    true;
                dropped.errorCode =
                    QStringLiteral(
                        "offline");
                dropped.errorMessage =
                    QStringLiteral(
                        "fixture lost commit response");
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
                   attachmentPrefix)) {
            emit finished(
                requestId,
                m_service->get(
                    request.path.mid(
                        attachmentPrefix
                            .size())));
            return;
        }

        if (request.method
                == QByteArrayLiteral("POST")
            && request.path
                == QLatin1String(
                       "/v1/sync/push")) {
            const AccountTransportReply pushReply =
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
                        "fixture lost push response");
                emit finished(requestId, dropped);
                return;
            }
            emit finished(requestId, pushReply);
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
    FixtureAttachmentService *m_service =
        nullptr;
    bool m_online = true;
    bool m_dropNextBegin = false;
    bool m_dropNextPush = false;
    bool m_dropNextCommit = false;
};

class SyntheticAdapter final
    : public SyncAdapter {
    Q_OBJECT

public:
    explicit SyntheticAdapter(
        const QString &persistencePath = QString(),
        QObject *parent = nullptr)
        : SyncAdapter(parent),
          m_persistencePath(persistencePath) {
        loadPersisted();
    }

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

    bool missingRecordsAreDeletes()
        const override {
        return true;
    }

    bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error) const override {
        if (!snapshot) {
            if (error) {
                *error =
                    QStringLiteral(
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
                    m_records.value(
                        key)});
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
                *error =
                    QStringLiteral(
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
        persist();
        return true;
    }

    void putLocal(
        const QString &recordKey,
        const QString &value) {
        m_records.insert(
            recordKey,
            QJsonObject{
                {
                    QStringLiteral(
                        "value"),
                    value
                }
            });

        ++m_revision;
        persist();
        emit localMutationAvailable(
            m_revision);
    }

    QString value(
        const QString &recordKey) const {
        return m_records
            .value(recordKey)
            .toObject()
            .value(
                QStringLiteral(
                    "value"))
            .toString();
    }

    bool contains(
        const QString &recordKey) const {
        return m_records.contains(
            recordKey);
    }

private:
    void loadPersisted() {
        if (m_persistencePath.isEmpty())
            return;

        QFile file(m_persistencePath);
        if (!file.open(QIODevice::ReadOnly))
            return;

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(
                file.readAll(),
                &parseError);
        if (parseError.error
                != QJsonParseError::NoError
            || !document.isObject()) {
            return;
        }

        const QJsonObject object =
            document.object();
        for (auto it = object.constBegin();
             it != object.constEnd(); ++it) {
            m_records.insert(it.key(), it.value());
        }
    }

    void persist() const {
        if (m_persistencePath.isEmpty())
            return;

        QSaveFile file(m_persistencePath);
        if (!file.open(QIODevice::WriteOnly))
            return;

        QJsonObject object;
        QStringList keys = m_records.keys();
        keys.sort();
        for (const QString &key : keys) {
            object.insert(key, m_records.value(key));
        }

        const QByteArray bytes =
            QJsonDocument(object).toJson(
                QJsonDocument::Compact);
        if (file.write(bytes) == bytes.size())
            file.commit();
    }

    QHash<QString, QJsonValue>
        m_records;
    quint64 m_revision = 0;
    int m_remoteApplyCount = 0;
    QString m_persistencePath;
};

ProfilePaths accountProfile(
    QTemporaryDir *temp) {
    const auto profile =
        ProfilePaths::account(
            QString::fromLatin1(
                kAccountA),
            temp->path());

    if (!profile.has_value())
        qFatal(
            "fixture account profile invalid");

    QDir().mkpath(
        profile->profileRoot());
    return *profile;
}

// One engine "process lifetime": transport, client, registry, and engine
// over a shared fixture service and shared adapter (the adapter plays the
// durable local stores that survive a restart).
struct EngineRun {
    FixtureAttachmentTransport transport;
    AccountClient client;
    SyncAdapterRegistry registry;
    SyncEngine engine;

    EngineRun(
        FixtureAttachmentService *service,
        SyntheticAdapter *adapter,
        const ProfilePaths &profile,
        qint64 *now)
        : transport(service),
          client(&transport),
          engine(
              &client,
              &registry,
              [now]() {
                  return *now;
              }) {
        client.setAccessToken(
            QByteArrayLiteral(
                "fixture-access"));

        if (!registry.registerAdapter(
                adapter)) {
            qFatal(
                "fixture adapter registration failed");
        }

        engine.setAutomaticSchedulingEnabled(
            false);
        engine.setNetworkEnabled(false);

        QString error;
        if (!engine.start(
                profile,
                QString::fromLatin1(
                    kDeviceA),
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

AccountAttachmentCoordinator::SourceIdentity
validSource() {
    AccountAttachmentCoordinator::
        SourceIdentity source;
    source.sourceKind =
        QStringLiteral(
            "legacy_local");
    source.sourceProfileId =
        QStringLiteral("legacy");
    source.sourceSemanticDigest =
        QStringLiteral(
            "sha256:source-semantic-v1");
    source.sourceActivityDigest =
        QStringLiteral(
            "sha256:source-activity-v1");
    return source;
}

QString stateName(
    AccountAttachmentCoordinator::State
        state) {
    switch (state) {
    case AccountAttachmentCoordinator::
        State::Idle:
        return QStringLiteral(
            "Idle");
    case AccountAttachmentCoordinator::
        State::Preparing:
        return QStringLiteral(
            "Preparing");
    case AccountAttachmentCoordinator::
        State::Beginning:
        return QStringLiteral(
            "Beginning");
    case AccountAttachmentCoordinator::
        State::EngineBootstrapping:
        return QStringLiteral(
            "EngineBootstrapping");
    case AccountAttachmentCoordinator::
        State::Verifying:
        return QStringLiteral(
            "Verifying");
    case AccountAttachmentCoordinator::
        State::Committing:
        return QStringLiteral(
            "Committing");
    case AccountAttachmentCoordinator::
        State::Retiring:
        return QStringLiteral(
            "Retiring");
    case AccountAttachmentCoordinator::
        State::Completed:
        return QStringLiteral(
            "Completed");
    case AccountAttachmentCoordinator::
        State::Failed:
        return QStringLiteral(
            "Failed");
    }
    return QStringLiteral(
        "Unknown");
}

struct Recording {
    QStringList states;
    int finishedCount = 0;
    bool succeeded = false;
    QString errorCode;
    QString errorMessage;

    void reset() {
        states.clear();
        finishedCount = 0;
        succeeded = false;
        errorCode.clear();
        errorMessage.clear();
    }
};

void recordProgress(
    AccountAttachmentCoordinator
        *coordinator,
    Recording *recording) {
    QObject::connect(
        coordinator,
        &AccountAttachmentCoordinator::
            progress,
        [recording](
            AccountAttachmentCoordinator::
                State state) {
            recording->states
                << stateName(state);
        });
    QObject::connect(
        coordinator,
        &AccountAttachmentCoordinator::
            finished,
        [recording](
            bool succeeded,
            const QString &errorCode,
            const QString
                &errorMessage) {
            recording->finishedCount =
                1;
            recording->succeeded =
                succeeded;
            recording->errorCode =
                errorCode;
            recording->errorMessage =
                errorMessage;
        });
}

bool writeBytes(
    const QString &path,
    const QByteArray &payload) {
    const QFileInfo info(path);
    if (!QDir().mkpath(
            info.absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(
            QIODevice::WriteOnly))
        return false;
    return file.write(payload)
        == payload.size();
}

QByteArray readBytes(
    const QString &path) {
    QFile file(path);
    if (!file.open(
            QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}
}

class tst_account_attachment_coordinator final
    : public QObject {
    Q_OBJECT

private slots:
    void inputValidationFailsClosedWithoutSideEffects();
    void happyPathOrchestrationOrderUnionAndRetirement();
    void resumeFromReceiptWhenBeginNeverReachedServer();
    void resumeMidBootstrapContinuesSnapshotFromDurableToken();
    void resumeFromUploadedAttachmentVerifiesCommitsRetires();
    void resumeFromCommittedAttachmentIsIdempotent();
    void commitResponseLossFailsClosedThenRecoversIdempotently();
    void verificationFailureKeepsSourceAndRecordsFailure();
    void missingVerifierFailsClosedThenZeroPushResumeReEntersBootstrap();
    void retiredReceiptResumeOnlyClears();
    void invalidReceiptFailsClosedUntouched();
    void mismatchedIdentityFailsClosed();
    void retirementWriteFailureKeepsReceiptAndSource();

    // Arc 36 Wave 4B lane N-18 — fresh-stack crash/restart proofs at the
    // durable receipt, begin, attached-upload, and cloud-verification edges.
    void crashRestartMatrixUsesDurableReceiptAndEngineState();
    void abandonedAttachmentKeepsSourceAndAccountUnchanged();
};

// Input validation refuses before any durable or server side effect.
void tst_account_attachment_coordinator::
    inputValidationFailsClosedWithoutSideEffects() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);
    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);

    Recording recording;
    recordProgress(
        &coordinator,
        &recording);

    QString error;
    QVERIFY(!coordinator.start(
        QStringLiteral(
            "not-a-uuid"),
        validSource(),
        &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "invalid_attachment_id"));

    AccountAttachmentCoordinator::
        SourceIdentity badKind =
            validSource();
    badKind.sourceKind =
        QStringLiteral("cloud");
    QVERIFY(!coordinator.start(
        QString::fromLatin1(
            kAttachmentId),
        badKind,
        &error));
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "invalid_source_kind"));

    AccountAttachmentCoordinator::
        SourceIdentity badDigest =
            validSource();
    badDigest.sourceSemanticDigest
        .clear();
    QVERIFY(!coordinator.start(
        QString::fromLatin1(
            kAttachmentId),
        badDigest,
        &error));
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "invalid_source_digest"));

    AccountAttachmentCoordinator::
        SourceIdentity badProfile =
            validSource();
    badProfile.sourceProfileId
        .clear();
    QVERIFY(!coordinator.start(
        QString::fromLatin1(
            kAttachmentId),
        badProfile,
        &error));
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "invalid_source_profile"));

    // Nothing was written and nothing reached the server. (The empty
    // Activity digest never made it into these refusals: every start was
    // rejected on other fields.)
    QVERIFY(service.events.isEmpty());
    QCOMPARE(
        AccountAttachmentReceipt::read(
            profile)
            .status,
        AccountAttachmentReceipt::
            ReadStatus::Missing);
    QCOMPARE(
        coordinator.state(),
        AccountAttachmentCoordinator::
            State::Failed);
}

// The full happy path: begin -> engine attachment mode (snapshot + attached
// push) -> local verification of the merged union -> commit -> retire ->
// clear, with the durable order recorded exactly.
void tst_account_attachment_coordinator::
    happyPathOrchestrationOrderUnionAndRetirement() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    // The account already owns one canonical record.
    FixtureSnapshotPage page;
    page.cursor = 1;
    page.hasMore = false;
    page.entries.append(
        canonicalSnapshotEntry(
            1,
            remoteMutation(
                QStringLiteral(
                    "f5100000-0000-"
                    "4000-8000-"
                    "000000000001"),
                QStringLiteral(
                    "collection"),
                QStringLiteral(
                    "manga/remote"),
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
                            "cloud")
                    }
                })));
    service.setSnapshotPages(
        {page});

    {
        EngineRun run(
            &service,
            &adapter,
            profile,
            &now);

        // The portable local state queues as an unsent mutation.
        adapter.putLocal(
            QStringLiteral(
                "manga/local"),
            QStringLiteral(
                "mine"));
        QTRY_COMPARE(
            run.engine
                .pendingOutboxCount(),
            1);

        AccountAttachmentCoordinator
            coordinator(
                &run.client,
                &run.engine,
                profile);
        Recording recording;
        recordProgress(
            &coordinator,
            &recording);

        bool verificationSawUnion =
            false;
        coordinator.setCloudStateVerifier(
            [&](QString *error) {
                service.events
                    << QStringLiteral(
                           "verify");
                // The applied canonical state must reconstruct the
                // expected merged projection: the account's pre-existing
                // record and the attached local record coexist.
                verificationSawUnion =
                    adapter.contains(
                        QStringLiteral(
                            "manga/remote"))
                    && adapter.contains(
                        QStringLiteral(
                            "manga/local"));
                if (!verificationSawUnion
                    && error) {
                    *error =
                        QStringLiteral(
                            "The merged projection is incomplete.");
                }
                return verificationSawUnion;
            });

        QString error;
        QVERIFY2(
            coordinator.start(
                QString::fromLatin1(
                    kAttachmentId),
                validSource(),
                &error),
            qPrintable(error));

        run.engine.setNetworkEnabled(
            true);
        QTRY_COMPARE(
            recording.finishedCount,
            1);
        QVERIFY(recording.succeeded);
        QVERIFY(verificationSawUnion);

        // Step order, both in the progress stream and on the wire.
        QCOMPARE(
            recording.states,
            QStringList()
                << QStringLiteral(
                       "Preparing")
                << QStringLiteral(
                       "Beginning")
                << QStringLiteral(
                       "EngineBootstrapping")
                << QStringLiteral(
                       "Verifying")
                << QStringLiteral(
                       "Committing")
                << QStringLiteral(
                       "Retiring")
                << QStringLiteral(
                       "Completed"));

        const int begin =
            service.firstEvent(
                QStringLiteral(
                    "begin"));
        const int snapshot =
            service.firstEvent(
                QStringLiteral(
                    "snapshot:"));
        const int push =
            service.firstEvent(
                QStringLiteral(
                    "push:4f4f4f4f"));
        const int verify =
            service.firstEvent(
                QStringLiteral(
                    "verify"));
        const int commit =
            service.firstEvent(
                QStringLiteral(
                    "commit"));
        QVERIFY(begin >= 0);
        QVERIFY(snapshot > begin);
        QVERIFY(push > snapshot);
        QVERIFY(verify > push);
        QVERIFY(commit > verify);

        // Exactly one push and it was attached.
        QCOMPARE(
            service.eventCount(
                QStringLiteral(
                    "push:")),
            1);
        QCOMPARE(
            service.pushAttachmentIds()
                .size(),
            1);
        QCOMPARE(
            service.pushAttachmentIds()
                .constFirst(),
            QString::fromLatin1(
                kAttachmentId));

        // Commit happened, the source was retired then the receipt was
        // cleared, and the engine returned to ordinary mode.
        QCOMPARE(
            service.attachmentState(
                QString::fromLatin1(
                    kAttachmentId)),
            QStringLiteral(
                "committed"));
        QCOMPARE(
            AccountAttachmentReceipt::read(
                profile)
                .status,
            AccountAttachmentReceipt::
                ReadStatus::Missing);
        QVERIFY(
            !run.engine
                 .attachmentModeActive());
        QCOMPARE(
            run.engine
                .pendingOutboxCount(),
            0);
    }
}

// A begin that never reached the server leaves a pending receipt; the retry
// reuses the same attachment id from the receipt and completes without a
// second attachment identity.
void tst_account_attachment_coordinator::
    resumeFromReceiptWhenBeginNeverReachedServer() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    FixtureSnapshotPage page;
    page.cursor = 0;
    page.hasMore = false;
    service.setSnapshotPages(
        {page});

    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("one"));
    QTRY_COMPARE(
        run.engine
            .pendingOutboxCount(),
        1);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);
    Recording recording;
    recordProgress(
        &coordinator,
        &recording);
    coordinator.setCloudStateVerifier(
        [&](QString *) {
            service.events
                << QStringLiteral(
                       "verify");
            return true;
        });

    // Run 1: the server is unreachable, so begin fails after the receipt
    // was written.
    run.transport.setOnline(false);
    QString error;
    QVERIFY2(
        coordinator.start(
            QString::fromLatin1(
                kAttachmentId),
            validSource(),
            &error),
        qPrintable(error));
    QTRY_COMPARE(
        recording.finishedCount,
        1);
    QVERIFY(
        !recording.succeeded);
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "server_begin_failed"));
    QVERIFY(service.events.isEmpty());

    const AccountAttachmentReceipt::
        ReadResult pending =
            AccountAttachmentReceipt::
                read(profile);
    QCOMPARE(
        pending.status,
        AccountAttachmentReceipt::
            ReadStatus::Ok);
    QCOMPARE(
        pending.data.attachmentId,
        QString::fromLatin1(
            kAttachmentId));
    QVERIFY(
        !pending.data.sourceRetired);
    QVERIFY(coordinator
                .hasPendingReceipt());

    // Run 2: online again. The same coordinator resumes from the receipt
    // with the same attachment id.
    run.transport.setOnline(true);
    recording.reset();
    QVERIFY2(
        coordinator.start(
            QString::fromLatin1(
                kAttachmentId),
            validSource(),
            &error),
        qPrintable(error));
    run.engine.setNetworkEnabled(
        true);
    QTRY_COMPARE(
        recording.finishedCount,
        1);
    QVERIFY(recording.succeeded);

    QCOMPARE(
        service.eventCount(
            QStringLiteral("begin")),
        1);
    QCOMPARE(
        service.pushAttachmentIds()
            .constFirst(),
        QString::fromLatin1(
            kAttachmentId));
    QCOMPARE(
        service.attachmentState(
            QString::fromLatin1(
                kAttachmentId)),
        QStringLiteral(
            "committed"));
    QCOMPARE(
        AccountAttachmentReceipt::read(
            profile)
            .status,
        AccountAttachmentReceipt::
            ReadStatus::Missing);
}

// A crash mid-bootstrap restarts from the engine's durable snapshot
// continuation token: the second page is requested again, page one never
// re-fetched, and no second begin is issued.
void tst_account_attachment_coordinator::
    resumeMidBootstrapContinuesSnapshotFromDurableToken() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

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
        QStringLiteral("page-2");
    page1.entries.append(
        canonicalSnapshotEntry(
            1,
            snapshotMutation(
                QStringLiteral(
                    "f5100000-0000-"
                    "4000-8000-"
                    "000000000001"),
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
                    "f5100000-0000-"
                    "4000-8000-"
                    "000000000002"),
                QStringLiteral(
                    "manga/p2"),
                QStringLiteral(
                    "page-two"))));

    service.setSnapshotPages(
        {page1, page2});
    service
        .setSnapshotNetworkFailuresAfterPages(
            1,
            1);

    {
        EngineRun first(
            &service,
            &adapter,
            profile,
            &now);

        adapter.putLocal(
            QStringLiteral(
                "manga/local"),
            QStringLiteral(
                "mine"));
        QTRY_COMPARE(
            first.engine
                .pendingOutboxCount(),
            1);

        AccountAttachmentCoordinator
            coordinator(
                &first.client,
                &first.engine,
                profile);
        Recording recording;
        recordProgress(
            &coordinator,
            &recording);
        coordinator.setCloudStateVerifier(
            [&](QString *) {
                service.events
                    << QStringLiteral(
                           "verify");
                return true;
            });

        QString error;
        QVERIFY2(
            coordinator.start(
                QString::fromLatin1(
                    kAttachmentId),
                validSource(),
                &error),
            qPrintable(error));
        first.engine.setNetworkEnabled(
            true);

        // Page one lands, the page-2 request fails once: the engine parks
        // with a durable continuation token and the coordinator keeps
        // waiting.
        QTRY_COMPARE(
            service.eventCount(
                QStringLiteral(
                    "snapshot:")),
            2);
        QTRY_COMPARE(
            first.engine.state(),
            SyncEngine::State::Retrying);
        QCOMPARE(
            coordinator.state(),
            AccountAttachmentCoordinator::
                State::
                    EngineBootstrapping);
        QCOMPARE(
            AccountAttachmentReceipt::
                read(profile)
                .status,
            AccountAttachmentReceipt::
                ReadStatus::Ok);
    }

    // "Restart": a fresh transport/client/engine over the same profile and
    // service; the adapter (the local stores) survives.
    {
        EngineRun second(
            &service,
            &adapter,
            profile,
            &now);
        QVERIFY(
            second.engine
                .attachmentModeActive());

        AccountAttachmentCoordinator
            coordinator(
                &second.client,
                &second.engine,
                profile);
        Recording recording;
        recordProgress(
            &coordinator,
            &recording);
        coordinator.setCloudStateVerifier(
            [&](QString *) {
                service.events
                    << QStringLiteral(
                           "verify");
                return true;
            });

        QString error;
        QVERIFY2(
            coordinator.resumePending(
                &error),
            qPrintable(error));
        second.engine
            .setNetworkEnabled(
                true);
        QTRY_COMPARE(
            recording.finishedCount,
            1);
        QVERIFY(recording.succeeded);

        QCOMPARE(
            recording.states,
            QStringList()
                << QStringLiteral(
                       "Preparing")
                << QStringLiteral(
                       "EngineBootstrapping")
                << QStringLiteral(
                       "Verifying")
                << QStringLiteral(
                       "Committing")
                << QStringLiteral(
                       "Retiring")
                << QStringLiteral(
                       "Completed"));

        // Page one was fetched exactly once across both runs; the durable
        // token resumed page 2, and no second begin was issued.
        QCOMPARE(
            service.events
                .count(
                    QStringLiteral(
                        "snapshot:")),
            1);
        QCOMPARE(
            service.events
                .count(
                    QStringLiteral(
                        "snapshot:page-2")),
            2);
        QCOMPARE(
            service.eventCount(
                QStringLiteral("begin")),
            1);
        QCOMPARE(
            service.attachmentState(
                QString::fromLatin1(
                    kAttachmentId)),
            QStringLiteral(
                "committed"));
        QCOMPARE(
            AccountAttachmentReceipt::read(
                profile)
                .status,
            AccountAttachmentReceipt::
                ReadStatus::Missing);
        QVERIFY(adapter.contains(
            QStringLiteral(
                "manga/p1")));
        QVERIFY(adapter.contains(
            QStringLiteral(
                "manga/p2")));
    }
}

// A restart after the bootstrap fully drained (server uploaded) verifies,
// commits, and retires without issuing a second begin.
void tst_account_attachment_coordinator::
    resumeFromUploadedAttachmentVerifiesCommitsRetires() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    // Pre-seed: the attachment exists on the server and already received
    // its attached push (uploaded), but the receipt is still pending.
    const SyncWireMutation seeded =
        remoteMutation(
            QStringLiteral(
                "f5100000-0000-"
                "4000-8000-"
                "00000000000a"),
            QStringLiteral(
                "collection"),
            QStringLiteral(
                "manga/cloud"),
            QString::fromLatin1(
                kDeviceA),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral(
                        "value"),
                    QStringLiteral(
                        "seeded")
                }
            });
    QVERIFY(service.begin(
                 QString::fromLatin1(
                     kAttachmentId),
                 QStringLiteral(
                     "legacy_local"),
                 QStringLiteral(
                     "sha256:source-semantic-v1"))
                 .statusCode
        == 200);
    QVERIFY(service
                .push(
                    QJsonArray{
                        syncWireMutationToJson(
                            seeded)},
                    QString::fromLatin1(
                        kAttachmentId))
                .statusCode
        == 200);
    QCOMPARE(
        service.attachmentState(
            QString::fromLatin1(
                kAttachmentId)),
        QStringLiteral(
            "uploaded"));

    AccountAttachmentReceiptData
        receiptData;
    receiptData.version = 1;
    receiptData.attachmentId =
        QString::fromLatin1(
            kAttachmentId);
    receiptData.sourceKind =
        QStringLiteral(
            "legacy_local");
    receiptData.sourceProfileId =
        QStringLiteral("legacy");
    receiptData.sourceSemanticDigest =
        QStringLiteral(
            "sha256:source-semantic-v1");
    receiptData.sourceRetired =
        false;
    QString error;
    QVERIFY2(
        AccountAttachmentReceipt::
            save(profile,
                 receiptData,
                 &error),
        qPrintable(error));

    // The stable snapshot serves the account's canonical record.
    FixtureSnapshotPage page;
    page.cursor = 1;
    page.hasMore = false;
    page.entries.append(
        canonicalSnapshotEntry(
            1,
            seeded));
    service.setSnapshotPages(
        {page});

    {
        EngineRun run(
            &service,
            &adapter,
            profile,
            &now);

        AccountAttachmentCoordinator
            coordinator(
                &run.client,
                &run.engine,
                profile);
        Recording recording;
        recordProgress(
            &coordinator,
            &recording);
        coordinator.setCloudStateVerifier(
            [&](QString *) {
                service.events
                    << QStringLiteral(
                           "verify");
                return true;
            });

        QVERIFY2(
            coordinator.resumePending(
                &error),
            qPrintable(error));
        run.engine.setNetworkEnabled(
            true);
        QTRY_COMPARE(
            recording.finishedCount,
            1);
        QVERIFY(recording.succeeded);

        QCOMPARE(
            recording.states,
            QStringList()
                << QStringLiteral(
                       "Preparing")
                << QStringLiteral(
                       "EngineBootstrapping")
                << QStringLiteral(
                       "Verifying")
                << QStringLiteral(
                       "Committing")
                << QStringLiteral(
                       "Retiring")
                << QStringLiteral(
                       "Completed"));

        // The attachment was resumed from its uploaded state: exactly the
        // one pre-seeded begin, one commit, and the snapshot applied the
        // account's canonical record into the local projection.
        QCOMPARE(
            service.eventCount(
                QStringLiteral("begin")),
            1);
        QCOMPARE(
            service.eventCount(
                QStringLiteral(
                    "commit")),
            1);
        QVERIFY(adapter.contains(
            QStringLiteral(
                "manga/cloud")));
        QCOMPARE(
            AccountAttachmentReceipt::read(
                profile)
            .status,
            AccountAttachmentReceipt::
                ReadStatus::Missing);
    }
}

// A restart after the server committed (reply lost before retirement)
// re-verifies, commits again idempotently, and then retires.
void tst_account_attachment_coordinator::
    resumeFromCommittedAttachmentIsIdempotent() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    const SyncWireMutation seeded =
        remoteMutation(
            QStringLiteral(
                "f5100000-0000-"
                "4000-8000-"
                "00000000000b"),
            QStringLiteral(
                "collection"),
            QStringLiteral(
                "manga/cloud"),
            QString::fromLatin1(
                kDeviceA),
            now,
            0,
            SyncWireOperation::Put,
            QJsonObject{
                {
                    QStringLiteral(
                        "value"),
                    QStringLiteral(
                        "seeded")
                }
            });
    QVERIFY(service.begin(
                 QString::fromLatin1(
                     kAttachmentId),
                 QStringLiteral(
                     "legacy_local"),
                 QStringLiteral(
                     "sha256:source-semantic-v1"))
                 .statusCode
        == 200);
    QVERIFY(service
                .push(
                    QJsonArray{
                        syncWireMutationToJson(
                            seeded)},
                    QString::fromLatin1(
                        kAttachmentId))
                .statusCode
        == 200);
    QVERIFY(service
                .commit(
                    QString::fromLatin1(
                        kAttachmentId))
                .statusCode
        == 200);

    AccountAttachmentReceiptData
        receiptData;
    receiptData.version = 1;
    receiptData.attachmentId =
        QString::fromLatin1(
            kAttachmentId);
    receiptData.sourceKind =
        QStringLiteral(
            "legacy_local");
    receiptData.sourceProfileId =
        QStringLiteral("legacy");
    receiptData.sourceSemanticDigest =
        QStringLiteral(
            "sha256:source-semantic-v1");
    receiptData.sourceRetired =
        false;
    QString error;
    QVERIFY2(
        AccountAttachmentReceipt::
            save(profile,
                 receiptData,
                 &error),
        qPrintable(error));

    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);
    Recording recording;
    recordProgress(
        &coordinator,
        &recording);
    coordinator.setCloudStateVerifier(
        [&](QString *) {
            service.events
                << QStringLiteral(
                       "verify");
            return true;
        });

    QVERIFY2(
        coordinator.resumePending(
            &error),
        qPrintable(error));
    QTRY_COMPARE(
        recording.finishedCount,
        1);
    QVERIFY(recording.succeeded);

    // Committed on the server already: no engine bootstrap, a fresh local
    // verification, an idempotent second commit, then retirement.
    QCOMPARE(
        recording.states,
        QStringList()
            << QStringLiteral(
                   "Preparing")
            << QStringLiteral(
                   "Verifying")
            << QStringLiteral(
                   "Committing")
            << QStringLiteral(
                   "Retiring")
            << QStringLiteral(
                   "Completed"));
    QCOMPARE(
        service.eventCount(
            QStringLiteral("commit")),
        2);
    QCOMPARE(
        service.eventCount(
            QStringLiteral(
                "verify")),
        1);
    QCOMPARE(
        AccountAttachmentReceipt::read(
            profile)
            .status,
        AccountAttachmentReceipt::
            ReadStatus::Missing);
}

// A lost commit response fails closed (receipt kept, source alive) and the
// retry recovers through the idempotent commit.
void tst_account_attachment_coordinator::
    commitResponseLossFailsClosedThenRecoversIdempotently() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    FixtureSnapshotPage page;
    page.cursor = 0;
    page.hasMore = false;
    service.setSnapshotPages(
        {page});

    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("one"));
    QTRY_COMPARE(
        run.engine
            .pendingOutboxCount(),
        1);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);
    Recording recording;
    recordProgress(
        &coordinator,
        &recording);
    coordinator.setCloudStateVerifier(
        [&](QString *) {
            service.events
                << QStringLiteral(
                       "verify");
            return true;
        });

    run.transport
        .dropNextCommitResponse();
    QString error;
    QVERIFY2(
        coordinator.start(
            QString::fromLatin1(
                kAttachmentId),
            validSource(),
            &error),
        qPrintable(error));
    run.engine.setNetworkEnabled(
        true);

    // The server committed but the reply was lost: the attempt fails with
    // the receipt and the source intact.
    QTRY_COMPARE(
        recording.finishedCount,
        1);
    QVERIFY(
        !recording.succeeded);
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "commit_failed"));
    QCOMPARE(
        service.attachmentState(
            QString::fromLatin1(
                kAttachmentId)),
        QStringLiteral(
            "committed"));

    const AccountAttachmentReceipt::
        ReadResult pending =
            AccountAttachmentReceipt::
                read(profile);
    QCOMPARE(
        pending.status,
        AccountAttachmentReceipt::
            ReadStatus::Ok);
    QVERIFY(
        !pending.data.sourceRetired);
    QVERIFY(
        run.engine
            .attachmentModeActive());

    // Recovery: a new coordinator resumes from the committed state.
    AccountAttachmentCoordinator
        recovered(
            &run.client,
            &run.engine,
            profile);
    recovered.setCloudStateVerifier(
        [&](QString *) {
            service.events
                << QStringLiteral(
                       "verify");
            return true;
        });
    QVERIFY(recovered
                .hasPendingReceipt());
    QVERIFY2(
        recovered.resumePending(
            &error),
        qPrintable(error));
    QTRY_VERIFY(
        !recovered
             .hasPendingReceipt());

    QCOMPARE(
        service.eventCount(
            QStringLiteral("commit")),
        2);
    QCOMPARE(
        AccountAttachmentReceipt::read(
            profile)
            .status,
        AccountAttachmentReceipt::
            ReadStatus::Missing);
    QVERIFY(
        !run.engine
             .attachmentModeActive());
}

// A failed local verification keeps the local source, records the failure,
// and never commits or retires.
void tst_account_attachment_coordinator::
    verificationFailureKeepsSourceAndRecordsFailure() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    FixtureSnapshotPage page;
    page.cursor = 0;
    page.hasMore = false;
    service.setSnapshotPages(
        {page});

    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("one"));
    QTRY_COMPARE(
        run.engine
            .pendingOutboxCount(),
        1);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);
    Recording recording;
    recordProgress(
        &coordinator,
        &recording);
    coordinator.setCloudStateVerifier(
        [&](QString *error) {
            service.events
                << QStringLiteral(
                       "verify");
            if (error) {
                *error =
                    QStringLiteral(
                        "The expected merged projection is missing a record.");
            }
            return false;
        });

    QString error;
    QVERIFY2(
        coordinator.start(
            QString::fromLatin1(
                kAttachmentId),
            validSource(),
            &error),
        qPrintable(error));
    run.engine.setNetworkEnabled(
        true);
    QTRY_COMPARE(
        recording.finishedCount,
        1);

    QVERIFY(
        !recording.succeeded);
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "cloud_state_verification_failed"));
    QVERIFY(!recording
                .errorMessage
                .isEmpty());
    QCOMPARE(
        recording.states
            .constLast(),
        QStringLiteral(
            "Failed"));
    QVERIFY(
        recording.states
            .contains(
                QStringLiteral(
                    "Verifying")));

    // No commit was ever requested; the attachment stays uploaded and
    // resumable; the receipt fails closed with the source unretired.
    QCOMPARE(
        service.eventCount(
            QStringLiteral(
                "commit")),
        0);
    QCOMPARE(
        service.attachmentState(
            QString::fromLatin1(
                kAttachmentId)),
        QStringLiteral(
            "uploaded"));

    const AccountAttachmentReceipt::
        ReadResult pending =
            AccountAttachmentReceipt::
                read(profile);
    QCOMPARE(
        pending.status,
        AccountAttachmentReceipt::
            ReadStatus::Ok);
    QVERIFY(
        !pending.data.sourceRetired);
    QVERIFY(
        run.engine
            .attachmentModeActive());
}

// Without a verifier the flow fails closed (never retires without a local
// verification), and a zero-push attachment whose bootstrap already
// completed resumes observably by re-entering the engine bootstrap.
void tst_account_attachment_coordinator::
    missingVerifierFailsClosedThenZeroPushResumeReEntersBootstrap() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    FixtureSnapshotPage page;
    page.cursor = 0;
    page.hasMore = false;
    service.setSnapshotPages(
        {page});

    // Run 1: no local mutations at all, and no verifier wired. The
    // bootstrap completes but the flow must fail closed instead of
    // retiring an unverified source.
    {
        EngineRun first(
            &service,
            &adapter,
            profile,
            &now);

        AccountAttachmentCoordinator
            coordinator(
                &first.client,
                &first.engine,
                profile);
        Recording recording;
        recordProgress(
            &coordinator,
            &recording);

        QString error;
        QVERIFY2(
            coordinator.start(
                QString::fromLatin1(
                    kAttachmentId),
                validSource(),
                &error),
            qPrintable(error));
        first.engine.setNetworkEnabled(
            true);
        QTRY_COMPARE(
            recording.finishedCount,
            1);

        QVERIFY(
            !recording.succeeded);
        QCOMPARE(
            recording.errorCode,
            QStringLiteral(
                "verification_unavailable"));
        QCOMPARE(
            service.eventCount(
                QStringLiteral(
                    "commit")),
            0);
        // Zero pushes: the attachment is still open.
        QCOMPARE(
            service.attachmentState(
                QString::fromLatin1(
                    kAttachmentId)),
            QStringLiteral(
                "open"));
        const AccountAttachmentReceipt::
            ReadResult pending =
                AccountAttachmentReceipt::
                    read(profile);
        QCOMPARE(
            pending.status,
            AccountAttachmentReceipt::
                ReadStatus::Ok);
        QVERIFY(
            !pending.data.sourceRetired);
    }

    // Restart with a verifier wired. The engine restored its attachment
    // mode with the snapshot already done and an empty outbox, so the
    // coordinator re-enters the bootstrap to observe the stable snapshot
    // before verifying. The stable view is re-servable, so the fixture
    // re-arms its (single-serve scripted) pages.
    service.setSnapshotPages(
        {page});
    {
        EngineRun second(
            &service,
            &adapter,
            profile,
            &now);
        QVERIFY(
            second.engine
                .attachmentModeActive());
        QCOMPARE(
            second.engine
                .pendingOutboxCount(),
            0);

        AccountAttachmentCoordinator
            coordinator(
                &second.client,
                &second.engine,
                profile);
        Recording recording;
        recordProgress(
            &coordinator,
            &recording);
        coordinator.setCloudStateVerifier(
            [&](QString *) {
                service.events
                    << QStringLiteral(
                           "verify");
                return true;
            });

        QString error;
        QVERIFY2(
            coordinator.resumePending(
                &error),
            qPrintable(error));
        second.engine
            .setNetworkEnabled(
                true);
        QTRY_COMPARE(
            recording.finishedCount,
            1);
        QVERIFY(recording.succeeded);

        // Two first-page snapshot fetches total: the original bootstrap
        // plus the observable re-entry; still exactly one begin.
        QCOMPARE(
            service.events
                .count(
                    QStringLiteral(
                        "snapshot:")),
            2);
        QCOMPARE(
            service.eventCount(
                QStringLiteral("begin")),
            1);
        QCOMPARE(
            service.attachmentState(
                QString::fromLatin1(
                    kAttachmentId)),
            QStringLiteral(
                "committed"));
        QCOMPARE(
            AccountAttachmentReceipt::read(
                profile)
                .status,
            AccountAttachmentReceipt::
                ReadStatus::Missing);
    }
}

// A receipt whose source is already retired only needs the explicit clear.
void tst_account_attachment_coordinator::
    retiredReceiptResumeOnlyClears() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    AccountAttachmentReceiptData
        receiptData;
    receiptData.version = 1;
    receiptData.attachmentId =
        QString::fromLatin1(
            kAttachmentId);
    receiptData.sourceKind =
        QStringLiteral(
            "legacy_local");
    receiptData.sourceProfileId =
        QStringLiteral("legacy");
    receiptData.sourceSemanticDigest =
        QStringLiteral(
            "sha256:source-semantic-v1");
    receiptData.sourceRetired =
        true;
    QString error;
    QVERIFY2(
        AccountAttachmentReceipt::
            save(profile,
                 receiptData,
                 &error),
        qPrintable(error));

    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);
    Recording recording;
    recordProgress(
        &coordinator,
        &recording);

    QVERIFY2(
        coordinator.resumePending(
            &error),
        qPrintable(error));
    QCOMPARE(
        recording.finishedCount,
        1);
    QVERIFY(recording.succeeded);
    QCOMPARE(
        recording.states,
        QStringList()
            << QStringLiteral(
                   "Preparing")
            << QStringLiteral(
                   "Completed"));

    // The server was never touched.
    QVERIFY(service.events.isEmpty());
    QCOMPARE(
        AccountAttachmentReceipt::read(
            profile)
            .status,
        AccountAttachmentReceipt::
            ReadStatus::Missing);
}

// A corrupted receipt fails closed: it is neither cleared nor overwritten
// and nothing reaches the server.
void tst_account_attachment_coordinator::
    invalidReceiptFailsClosedUntouched() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);
    Recording recording;
    recordProgress(
        &coordinator,
        &recording);

    // A pending-but-invalid receipt (written behind the coordinator's
    // back) blocks resumePending and start alike.
    const QByteArray garbage =
        QByteArrayLiteral(
            "{ not json");
    QVERIFY(writeBytes(
        profile
            .cloudAttachmentReceiptPath(),
        garbage));

    QString error;
    QVERIFY(!coordinator.resumePending(
        &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "receipt_invalid"));
    QCOMPARE(
        coordinator.state(),
        AccountAttachmentCoordinator::
            State::Failed);

    QVERIFY(!coordinator.start(
        QString::fromLatin1(
            kAttachmentId),
        validSource(),
        &error));
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "receipt_invalid"));

    QVERIFY(service.events.isEmpty());
    QCOMPARE(
        readBytes(
            profile
                .cloudAttachmentReceiptPath()),
        garbage);
}

// One attachment at a time: a pending receipt refuses a different
// attachment id or a different source identity, without side effects.
void tst_account_attachment_coordinator::
    mismatchedIdentityFailsClosed() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    AccountAttachmentReceiptData
        receiptData;
    receiptData.version = 1;
    receiptData.attachmentId =
        QString::fromLatin1(
            kAttachmentId);
    receiptData.sourceKind =
        QStringLiteral(
            "legacy_local");
    receiptData.sourceProfileId =
        QStringLiteral("legacy");
    receiptData.sourceSemanticDigest =
        QStringLiteral(
            "sha256:source-semantic-v1");
    receiptData.sourceRetired =
        false;
    QString error;
    QVERIFY2(
        AccountAttachmentReceipt::
            save(profile,
                 receiptData,
                 &error),
        qPrintable(error));

    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);
    Recording recording;
    recordProgress(
        &coordinator,
        &recording);

    QVERIFY(!coordinator.start(
        QString::fromLatin1(
            kOtherAttachmentId),
        validSource(),
        &error));
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "receipt_mismatch"));

    AccountAttachmentCoordinator::
        SourceIdentity differentSource =
            validSource();
    differentSource
        .sourceSemanticDigest =
        QStringLiteral(
            "sha256:some-other-digest");
    QVERIFY(!coordinator.start(
        QString::fromLatin1(
            kAttachmentId),
        differentSource,
        &error));
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "receipt_mismatch"));

    QVERIFY(service.events.isEmpty());
    const AccountAttachmentReceipt::
        ReadResult pending =
            AccountAttachmentReceipt::
                read(profile);
    QCOMPARE(
        pending.status,
        AccountAttachmentReceipt::
            ReadStatus::Ok);
    QCOMPARE(
        pending.data.attachmentId,
        QString::fromLatin1(
            kAttachmentId));
    QVERIFY(
        !pending.data.sourceRetired);
}

// Retirement writes the receipt before the clear: a receipt file that
// cannot be replaced mid-retirement keeps the source and fails closed.
void tst_account_attachment_coordinator::
    retirementWriteFailureKeepsReceiptAndSource() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    SyntheticAdapter adapter;
    const ProfilePaths profile =
        accountProfile(&temp);

    FixtureSnapshotPage page;
    page.cursor = 0;
    page.hasMore = false;
    service.setSnapshotPages(
        {page});

    EngineRun run(
        &service,
        &adapter,
        profile,
        &now);

    adapter.putLocal(
        QStringLiteral(
            "manga/item"),
        QStringLiteral("one"));
    QTRY_COMPARE(
        run.engine
            .pendingOutboxCount(),
        1);

    AccountAttachmentCoordinator
        coordinator(
            &run.client,
            &run.engine,
            profile);
    Recording recording;
    recordProgress(
        &coordinator,
        &recording);
    coordinator.setCloudStateVerifier(
        [&](QString *) {
            service.events
                << QStringLiteral(
                       "verify");
            return true;
        });

    // Block replacement of the receipt the moment retirement starts.
    // Windows uses an open read handle; POSIX makes the containing directory
    // non-writable so QSaveFile cannot create/replace its temporary file.
    std::unique_ptr<QFile> receiptLock;
#ifndef Q_OS_WIN
    QString receiptDirectory;
    QFile::Permissions receiptDirectoryPermissions;
    bool receiptDirectoryWriteBlocked = false;
#endif
    QObject::connect(
        &coordinator,
        &AccountAttachmentCoordinator::
            progress,
        [&](AccountAttachmentCoordinator::
                 State state) {
            if (state
                    == AccountAttachmentCoordinator::
                        State::Retiring
                && !receiptLock) {
#ifdef Q_OS_WIN
                receiptLock =
                    std::make_unique<
                        QFile>(
                        profile
                            .cloudAttachmentReceiptPath());
                const bool lockHeld =
                    receiptLock->open(
                        QIODevice::
                            ReadOnly);
                Q_UNUSED(lockHeld)
#else
                receiptDirectory = QFileInfo(
                    profile.cloudAttachmentReceiptPath())
                    .absolutePath();
                receiptDirectoryPermissions =
                    QFile::permissions(receiptDirectory);
                QFile::Permissions blocked =
                    receiptDirectoryPermissions;
                blocked &= ~QFileDevice::WriteOwner;
                blocked &= ~QFileDevice::WriteUser;
                blocked &= ~QFileDevice::WriteGroup;
                blocked &= ~QFileDevice::WriteOther;
                receiptDirectoryWriteBlocked =
                    QFile::setPermissions(
                        receiptDirectory,
                        blocked);
#endif
            }
        });

    QString error;
    QVERIFY2(
        coordinator.start(
            QString::fromLatin1(
                kAttachmentId),
            validSource(),
            &error),
        qPrintable(error));
    run.engine.setNetworkEnabled(
        true);
    QTRY_COMPARE(
        recording.finishedCount,
        1);

#ifndef Q_OS_WIN
    QVERIFY(receiptDirectoryWriteBlocked);
    QVERIFY(QFile::setPermissions(
        receiptDirectory,
        receiptDirectoryPermissions));
#endif

    // The server committed and the flow reached retirement, but the
    // retire write failed closed: the receipt survives with the source
    // unretired, proving retirement precedes the clear.
    QVERIFY(
        !recording.succeeded);
    QCOMPARE(
        recording.errorCode,
        QStringLiteral(
            "receipt_retire_failed"));
    QCOMPARE(
        service.attachmentState(
            QString::fromLatin1(
                kAttachmentId)),
        QStringLiteral(
            "committed"));

    const AccountAttachmentReceipt::
        ReadResult pending =
            AccountAttachmentReceipt::
                read(profile);
    QCOMPARE(
        pending.status,
        AccountAttachmentReceipt::
            ReadStatus::Ok);
    QVERIFY(
        !pending.data.sourceRetired);

    receiptLock.reset();

    // The pending receipt resumes through the idempotent commit and
    // completes the retirement.
    AccountAttachmentCoordinator
        recovered(
            &run.client,
            &run.engine,
            profile);
    recovered.setCloudStateVerifier(
        [&](QString *) {
            service.events
                << QStringLiteral(
                       "verify");
            return true;
        });
    QVERIFY2(
        recovered.resumePending(
            &error),
        qPrintable(error));
    QTRY_VERIFY(
        !recovered
             .hasPendingReceipt());

    QCOMPARE(
        AccountAttachmentReceipt::read(
            profile)
            .status,
        AccountAttachmentReceipt::
            ReadStatus::Missing);
}

// Every client-visible crash edge below is followed by a fresh transport,
// client, engine, coordinator, and persistent synthetic local store. The
// fixture service is the durable server side; the receipt and SyncStateStore
// are the durable desktop side. These are intentionally separate scopes so a
// passing result cannot depend on a still-live coordinator object.
void tst_account_attachment_coordinator::
    crashRestartMatrixUsesDurableReceiptAndEngineState() {
    // Receipt written, but the begin request never reached the server.
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        FixtureAttachmentService service;
        qint64 now = service.serverTimeMs;
        const ProfilePaths profile =
            accountProfile(&temp);
        const QString adapterPath =
            QDir(temp.path()).filePath(
                QStringLiteral("adapter.json"));

        FixtureSnapshotPage page;
        page.cursor = 0;
        page.hasMore = false;
        service.setSnapshotPages({page});

        {
            SyntheticAdapter adapter(adapterPath);
            EngineRun run(
                &service,
                &adapter,
                profile,
                &now);
            adapter.putLocal(
                QStringLiteral("manga/local"),
                QStringLiteral("source"));
            QTRY_COMPARE(
                run.engine.pendingOutboxCount(),
                1);

            run.transport.setOnline(false);
            AccountAttachmentCoordinator coordinator(
                &run.client,
                &run.engine,
                profile);
            Recording recording;
            recordProgress(&coordinator, &recording);

            QString error;
            QVERIFY2(
                coordinator.start(
                    QString::fromLatin1(kAttachmentId),
                    validSource(),
                    &error),
                qPrintable(error));
            QTRY_COMPARE(recording.finishedCount, 1);
            QVERIFY(!recording.succeeded);
            QCOMPARE(
                recording.errorCode,
                QStringLiteral("server_begin_failed"));
            QCOMPARE(service.eventCount(
                         QStringLiteral("begin")),
                     0);
            QCOMPARE(
                AccountAttachmentReceipt::read(profile)
                    .status,
                AccountAttachmentReceipt::ReadStatus::Ok);
        }

        {
            SyntheticAdapter adapter(adapterPath);
            EngineRun run(
                &service,
                &adapter,
                profile,
                &now);
            AccountAttachmentCoordinator coordinator(
                &run.client,
                &run.engine,
                profile);
            Recording recording;
            recordProgress(&coordinator, &recording);
            coordinator.setCloudStateVerifier(
                [](QString *) { return true; });

            QString error;
            QVERIFY2(
                coordinator.resumePending(&error),
                qPrintable(error));
            run.engine.setNetworkEnabled(true);
            QTRY_COMPARE(recording.finishedCount, 1);
            QVERIFY(recording.succeeded);
            QCOMPARE(
                service.eventCount(
                    QStringLiteral("begin")),
                1);
            QCOMPARE(
                service.attachmentState(
                    QString::fromLatin1(kAttachmentId)),
                QStringLiteral("committed"));
            QCOMPARE(
                AccountAttachmentReceipt::read(profile)
                    .status,
                AccountAttachmentReceipt::ReadStatus::Missing);
            QVERIFY(adapter.contains(
                QStringLiteral("manga/local")));
        }
    }

    // The server accepted begin, but its response was lost before the crash.
    // Restart must query the existing open attachment rather than re-begin it.
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        FixtureAttachmentService service;
        qint64 now = service.serverTimeMs;
        const ProfilePaths profile =
            accountProfile(&temp);
        const QString adapterPath =
            QDir(temp.path()).filePath(
                QStringLiteral("adapter.json"));
        FixtureSnapshotPage page;
        page.cursor = 0;
        page.hasMore = false;
        service.setSnapshotPages({page});

        {
            SyntheticAdapter adapter(adapterPath);
            EngineRun run(
                &service,
                &adapter,
                profile,
                &now);
            run.transport.dropNextBeginResponse();
            AccountAttachmentCoordinator coordinator(
                &run.client,
                &run.engine,
                profile);
            Recording recording;
            recordProgress(&coordinator, &recording);

            QString error;
            QVERIFY2(
                coordinator.start(
                    QString::fromLatin1(kAttachmentId),
                    validSource(),
                    &error),
                qPrintable(error));
            QTRY_COMPARE(recording.finishedCount, 1);
            QVERIFY(!recording.succeeded);
            QCOMPARE(
                service.eventCount(
                    QStringLiteral("begin")),
                1);
            QCOMPARE(
                service.attachmentState(
                    QString::fromLatin1(kAttachmentId)),
                QStringLiteral("open"));
            QVERIFY(
                !AccountAttachmentReceipt::read(profile)
                     .data.sourceRetired);
        }

        {
            SyntheticAdapter adapter(adapterPath);
            EngineRun run(
                &service,
                &adapter,
                profile,
                &now);
            AccountAttachmentCoordinator coordinator(
                &run.client,
                &run.engine,
                profile);
            Recording recording;
            recordProgress(&coordinator, &recording);
            coordinator.setCloudStateVerifier(
                [](QString *) { return true; });

            QString error;
            QVERIFY2(
                coordinator.resumePending(&error),
                qPrintable(error));
            run.engine.setNetworkEnabled(true);
            QTRY_COMPARE(recording.finishedCount, 1);
            QVERIFY(recording.succeeded);
            QCOMPARE(
                service.eventCount(
                    QStringLiteral("begin")),
                1);
            QCOMPARE(
                service.attachmentState(
                    QString::fromLatin1(kAttachmentId)),
                QStringLiteral("committed"));
            QCOMPARE(
                AccountAttachmentReceipt::read(profile)
                    .status,
                AccountAttachmentReceipt::ReadStatus::Missing);
        }
    }

    // An attached upload was accepted, but the response was lost. The fresh
    // engine replays the same mutation idempotently and then completes.
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        FixtureAttachmentService service;
        qint64 now = service.serverTimeMs;
        const ProfilePaths profile =
            accountProfile(&temp);
        const QString adapterPath =
            QDir(temp.path()).filePath(
                QStringLiteral("adapter.json"));
        FixtureSnapshotPage page;
        page.cursor = 0;
        page.hasMore = false;
        service.setSnapshotPages({page});

        {
            SyntheticAdapter adapter(adapterPath);
            EngineRun run(
                &service,
                &adapter,
                profile,
                &now);
            adapter.putLocal(
                QStringLiteral("manga/local"),
                QStringLiteral("source"));
            QTRY_COMPARE(
                run.engine.pendingOutboxCount(),
                1);

            run.transport.dropNextPushResponse();
            AccountAttachmentCoordinator coordinator(
                &run.client,
                &run.engine,
                profile);
            Recording recording;
            recordProgress(&coordinator, &recording);
            coordinator.setCloudStateVerifier(
                [](QString *) { return true; });

            QString error;
            QVERIFY2(
                coordinator.start(
                    QString::fromLatin1(kAttachmentId),
                    validSource(),
                    &error),
                qPrintable(error));
            run.engine.setNetworkEnabled(true);
            QTRY_COMPARE(
                service.eventCount(
                    QStringLiteral("push:")),
                1);
            QTest::qWait(50);
            QCOMPARE(recording.finishedCount, 0);
            QCOMPARE(
                service.attachmentState(
                    QString::fromLatin1(kAttachmentId)),
                QStringLiteral("uploaded"));
            QVERIFY(
                !AccountAttachmentReceipt::read(profile)
                     .data.sourceRetired);
        }

        {
            SyntheticAdapter adapter(adapterPath);
            EngineRun run(
                &service,
                &adapter,
                profile,
                &now);
            QVERIFY(run.engine.attachmentModeActive());
            QVERIFY(run.engine.pendingOutboxCount() > 0);

            AccountAttachmentCoordinator coordinator(
                &run.client,
                &run.engine,
                profile);
            Recording recording;
            recordProgress(&coordinator, &recording);
            coordinator.setCloudStateVerifier(
                [](QString *) { return true; });

            QString error;
            QVERIFY2(
                coordinator.resumePending(&error),
                qPrintable(error));
            run.engine.setNetworkEnabled(true);
            QTRY_COMPARE(recording.finishedCount, 1);
            QVERIFY(recording.succeeded);
            QCOMPARE(
                service.eventCount(
                    QStringLiteral("push:")),
                2);
            QCOMPARE(
                service.acceptedMutationCount(),
                1);
            QCOMPARE(
                service.attachmentState(
                    QString::fromLatin1(kAttachmentId)),
                QStringLiteral("committed"));
            QVERIFY(adapter.contains(
                QStringLiteral("manga/local")));
        }
    }

    // The cloud verifier returned success, but the commit request failed
    // before reaching the server. A fresh stack must retry from uploaded,
    // preserving the receipt and source until commit really succeeds.
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        FixtureAttachmentService service;
        qint64 now = service.serverTimeMs;
        const ProfilePaths profile =
            accountProfile(&temp);
        const QString adapterPath =
            QDir(temp.path()).filePath(
                QStringLiteral("adapter.json"));
        FixtureSnapshotPage page;
        page.cursor = 0;
        page.hasMore = false;
        service.setSnapshotPages({page});

        {
            SyntheticAdapter adapter(adapterPath);
            EngineRun run(
                &service,
                &adapter,
                profile,
                &now);
            adapter.putLocal(
                QStringLiteral("manga/local"),
                QStringLiteral("source"));
            QTRY_COMPARE(
                run.engine.pendingOutboxCount(),
                1);

            AccountAttachmentCoordinator coordinator(
                &run.client,
                &run.engine,
                profile);
            Recording recording;
            recordProgress(&coordinator, &recording);
            coordinator.setCloudStateVerifier(
                [&](QString *) {
                    service.events << QStringLiteral(
                        "verify");
                    run.transport.setOnline(false);
                    return true;
                });

            QString error;
            QVERIFY2(
                coordinator.start(
                    QString::fromLatin1(kAttachmentId),
                    validSource(),
                    &error),
                qPrintable(error));
            run.engine.setNetworkEnabled(true);
            QTRY_COMPARE(recording.finishedCount, 1);
            QVERIFY(!recording.succeeded);
            QCOMPARE(
                recording.errorCode,
                QStringLiteral("commit_failed"));
            QCOMPARE(
                service.eventCount(
                    QStringLiteral("verify")),
                1);
            QCOMPARE(
                service.eventCount(
                    QStringLiteral("commit")),
                0);
            QVERIFY(
                !AccountAttachmentReceipt::read(profile)
                     .data.sourceRetired);
        }

        {
            SyntheticAdapter adapter(adapterPath);
            EngineRun run(
                &service,
                &adapter,
                profile,
                &now);
            AccountAttachmentCoordinator coordinator(
                &run.client,
                &run.engine,
                profile);
            Recording recording;
            recordProgress(&coordinator, &recording);
            coordinator.setCloudStateVerifier(
                [](QString *) { return true; });

            QString error;
            QVERIFY2(
                coordinator.resumePending(&error),
                qPrintable(error));
            run.engine.setNetworkEnabled(true);
            QTRY_COMPARE(recording.finishedCount, 1);
            QVERIFY(recording.succeeded);
            QCOMPARE(
                service.eventCount(
                    QStringLiteral("commit")),
                1);
            QCOMPARE(
                service.attachmentState(
                    QString::fromLatin1(kAttachmentId)),
                QStringLiteral("committed"));
            QCOMPARE(
                AccountAttachmentReceipt::read(profile)
                    .status,
                AccountAttachmentReceipt::ReadStatus::Missing);
        }
    }
}

// An attachment abandoned before any server work leaves the original local
// source usable after a fresh stack and leaves the account side untouched.
void tst_account_attachment_coordinator::
    abandonedAttachmentKeepsSourceAndAccountUnchanged() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    FixtureAttachmentService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile =
        accountProfile(&temp);
    const QString adapterPath =
        QDir(temp.path()).filePath(
            QStringLiteral("adapter.json"));

    {
        SyntheticAdapter adapter(adapterPath);
        EngineRun run(
            &service,
            &adapter,
            profile,
            &now);
        adapter.putLocal(
            QStringLiteral("manga/local"),
            QStringLiteral("source"));
        QTRY_COMPARE(
            run.engine.pendingOutboxCount(),
            1);

        run.transport.setOnline(false);
        AccountAttachmentCoordinator coordinator(
            &run.client,
            &run.engine,
            profile);
        Recording recording;
        recordProgress(&coordinator, &recording);

        QString error;
        QVERIFY2(
            coordinator.start(
                QString::fromLatin1(kAttachmentId),
                validSource(),
                &error),
            qPrintable(error));
        QTRY_COMPARE(recording.finishedCount, 1);
        QVERIFY(!recording.succeeded);
        QCOMPARE(service.events.size(), 0);
        QCOMPARE(service.acceptedMutationCount(), 0);
        QVERIFY(adapter.contains(
            QStringLiteral("manga/local")));
        QCOMPARE(
            adapter.value(
                QStringLiteral("manga/local")),
            QStringLiteral("source"));
        QVERIFY(
            !AccountAttachmentReceipt::read(profile)
                 .data.sourceRetired);
    }

    // The persistent synthetic store is reopened independently, matching the
    // original source's usable-after-abandonment requirement.
    SyntheticAdapter reopened(adapterPath);
    QVERIFY(reopened.contains(
        QStringLiteral("manga/local")));
    QCOMPARE(
        reopened.value(
            QStringLiteral("manga/local")),
        QStringLiteral("source"));
    QVERIFY(!service.hasAttachment(
        QString::fromLatin1(kAttachmentId)));
    QCOMPARE(
        AccountAttachmentReceipt::read(profile)
            .status,
        AccountAttachmentReceipt::ReadStatus::Ok);
}

QTEST_GUILESS_MAIN(
    tst_account_attachment_coordinator)

#include "tst_account_attachment_coordinator.moc"
