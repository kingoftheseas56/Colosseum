#include "account/AccountAttachmentCoordinator.h"
#include "account/AccountAttachmentReceipt.h"
#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/ProfilePaths.h"
#include "account/SyncAdapter.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto kAccountId =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceA =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kDeviceB =
    "22222222-2222-4222-8222-222222222222";
constexpr auto kAttachmentId =
    "36363636-3636-4363-8363-363636363636";

QString recordIdentity(
    const QString &category,
    const QString &recordKey) {
    return category + QChar(0x1f) + recordKey;
}

struct MutationResult {
    quint64 serverSeq = 0;
    bool won = false;
};

struct AttachmentRecord {
    QString sourceKind;
    QString sourceSemanticDigest;
    QString state = QStringLiteral("open");
};

class DesktopConvergenceService {
public:
    qint64 serverTimeMs = 2000000;

    int beginRequestCount() const {
        return m_beginRequestCount;
    }

    int pushRequestCount() const {
        return m_pushRequestCount;
    }

    int acceptedMutationCount() const {
        return m_idempotency.size();
    }

    QString attachmentState(
        const QString &attachmentId) const {
        return m_attachments.value(attachmentId).state;
    }

    void setSnapshotEntries(
        const QList<SyncWirePullEntry> &entries) {
        m_snapshotEntries = entries;
    }

    void appendRemote(
        const SyncWireMutation &mutation) {
        const quint64 serverSeq = m_nextServerSeq++;
        SyncWirePullEntry entry;
        entry.serverSeq = serverSeq;
        entry.won = true;
        entry.canonical = true;
        entry.mutation = mutation;
        m_journal.append(entry);
        m_current.insert(
            recordIdentity(mutation.category, mutation.recordKey),
            mutation);
    }

    AccountTransportReply push(
        const QJsonArray &mutations,
        const QString &attachmentId) {
        ++m_pushRequestCount;

        AccountTransportReply reply;
        reply.statusCode = 200;
        QJsonArray results;
        bool acceptedAny = false;

        for (const QJsonValue &value : mutations) {
            if (!value.isObject())
                continue;

            const QJsonObject object = value.toObject();
            const auto parsed =
                syncWireMutationFromJson(object);
            const QString mutationId = object.value(
                QStringLiteral("mutation_id")).toString();
            if (!parsed.has_value()) {
                results.append(rejectedResult(
                    mutationId,
                    QStringLiteral("invalid_mutation")));
                continue;
            }

            const SyncWireMutation mutation = *parsed;
            const auto duplicate = m_idempotency.constFind(
                mutation.mutationId);
            if (duplicate != m_idempotency.constEnd()) {
                results.append(acceptedResult(
                    mutation.mutationId,
                    duplicate->serverSeq,
                    duplicate->won));
                continue;
            }

            const QString identity = recordIdentity(
                mutation.category,
                mutation.recordKey);
            const auto current = m_current.constFind(identity);
            const bool won = current == m_current.constEnd()
                || syncWireHlcGreater(mutation.hlc, current->hlc);
            const quint64 serverSeq = m_nextServerSeq++;

            SyncWirePullEntry entry;
            entry.serverSeq = serverSeq;
            entry.won = won;
            entry.canonical = true;
            entry.mutation = mutation;
            m_journal.append(entry);
            if (won)
                m_current.insert(identity, mutation);

            m_idempotency.insert(
                mutation.mutationId,
                MutationResult{serverSeq, won});
            results.append(acceptedResult(
                mutation.mutationId,
                serverSeq,
                won));
            acceptedAny = true;
        }

        if (!attachmentId.isEmpty() && acceptedAny) {
            auto attachment = m_attachments.find(attachmentId);
            if (attachment != m_attachments.end()
                && attachment->state == QLatin1String("open")) {
                attachment->state = QStringLiteral("uploaded");
            }
        }

        reply.body.insert(
            QStringLiteral("server_time_ms"),
            QString::number(serverTimeMs));
        reply.body.insert(QStringLiteral("results"), results);
        return reply;
    }

    AccountTransportReply pull(quint64 after) const {
        AccountTransportReply reply;
        reply.statusCode = 200;
        QJsonArray entries;
        for (const SyncWirePullEntry &entry : m_journal) {
            if (entry.serverSeq <= after)
                continue;
            QJsonObject object;
            object.insert(
                QStringLiteral("server_seq"),
                QString::number(entry.serverSeq));
            object.insert(QStringLiteral("won"), entry.won);
            object.insert(QStringLiteral("canonical"), true);
            object.insert(
                QStringLiteral("mutation"),
                syncWireMutationToJson(entry.mutation));
            entries.append(object);
        }
        reply.body.insert(
            QStringLiteral("server_time_ms"),
            QString::number(serverTimeMs));
        reply.body.insert(QStringLiteral("entries"), entries);
        reply.body.insert(QStringLiteral("has_more"), false);
        return reply;
    }

    AccountTransportReply snapshot() const {
        AccountTransportReply reply;
        reply.statusCode = 200;
        QJsonArray entries;
        quint64 cursor = 0;
        for (const SyncWirePullEntry &entry : m_snapshotEntries) {
            cursor = qMax(cursor, entry.serverSeq);
            QJsonObject object;
            object.insert(
                QStringLiteral("server_seq"),
                QString::number(entry.serverSeq));
            object.insert(QStringLiteral("won"), entry.won);
            object.insert(QStringLiteral("canonical"), true);
            object.insert(
                QStringLiteral("mutation"),
                syncWireMutationToJson(entry.mutation));
            entries.append(object);
        }
        reply.body.insert(
            QStringLiteral("server_time_ms"),
            QString::number(serverTimeMs));
        reply.body.insert(QStringLiteral("cursor"), QString::number(cursor));
        reply.body.insert(QStringLiteral("entries"), entries);
        reply.body.insert(QStringLiteral("has_more"), false);
        return reply;
    }

    AccountTransportReply begin(
        const QString &attachmentId,
        const QString &sourceKind,
        const QString &sourceSemanticDigest) {
        ++m_beginRequestCount;
        const auto existing = m_attachments.constFind(attachmentId);
        if (existing != m_attachments.constEnd()) {
            if (existing->sourceKind != sourceKind
                || existing->sourceSemanticDigest != sourceSemanticDigest) {
                return apiError(
                    409,
                    QStringLiteral("attachment_conflict"));
            }
            return attachmentView(attachmentId, existing->state);
        }

        m_attachments.insert(
            attachmentId,
            AttachmentRecord{
                sourceKind,
                sourceSemanticDigest,
                QStringLiteral("open")});
        return attachmentView(attachmentId, QStringLiteral("open"));
    }

    AccountTransportReply get(const QString &attachmentId) const {
        const auto existing = m_attachments.constFind(attachmentId);
        if (existing == m_attachments.constEnd())
            return apiError(404, QStringLiteral("attachment_not_found"));
        return attachmentView(attachmentId, existing->state);
    }

    AccountTransportReply commit(const QString &attachmentId) {
        auto existing = m_attachments.find(attachmentId);
        if (existing == m_attachments.end())
            return apiError(404, QStringLiteral("attachment_not_found"));
        existing->state = QStringLiteral("committed");
        return attachmentView(attachmentId, existing->state);
    }

private:
    static QJsonObject rejectedResult(
        const QString &mutationId,
        const QString &code) {
        QJsonObject result;
        result.insert(QStringLiteral("mutation_id"), mutationId);
        result.insert(QStringLiteral("accepted"), false);
        result.insert(QStringLiteral("code"), code);
        return result;
    }

    static QJsonObject acceptedResult(
        const QString &mutationId,
        quint64 serverSeq,
        bool won) {
        QJsonObject result;
        result.insert(QStringLiteral("mutation_id"), mutationId);
        result.insert(QStringLiteral("accepted"), true);
        result.insert(
            QStringLiteral("server_seq"),
            QString::number(serverSeq));
        result.insert(QStringLiteral("won"), won);
        return result;
    }

    static AccountTransportReply apiError(
        int statusCode,
        const QString &errorCode) {
        AccountTransportReply reply;
        reply.statusCode = statusCode;
        reply.errorCode = errorCode;
        return reply;
    }

    AccountTransportReply attachmentView(
        const QString &attachmentId,
        const QString &state) const {
        AccountTransportReply reply;
        reply.statusCode = 200;
        reply.body.insert(QStringLiteral("attachment_id"), attachmentId);
        reply.body.insert(
            QStringLiteral("device_id"),
            QString::fromLatin1(kDeviceA));
        reply.body.insert(QStringLiteral("baseline_server_seq"), 0);
        reply.body.insert(QStringLiteral("state"), state);
        return reply;
    }

    quint64 m_nextServerSeq = 1;
    QList<SyncWirePullEntry> m_journal;
    QHash<QString, MutationResult> m_idempotency;
    QHash<QString, SyncWireMutation> m_current;
    QHash<QString, AttachmentRecord> m_attachments;
    QList<SyncWirePullEntry> m_snapshotEntries;
    int m_beginRequestCount = 0;
    int m_pushRequestCount = 0;
};

class DesktopConvergenceTransport final : public AccountTransport {
public:
    explicit DesktopConvergenceTransport(
        DesktopConvergenceService *service,
        QObject *parent = nullptr)
        : AccountTransport(parent),
          m_service(service) {}

    void dropNextPushResponse() {
        m_dropNextPush = true;
    }

    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override {
        AccountTransportReply reply;
        if (request.bearerToken.isEmpty()) {
            reply.statusCode = 401;
            reply.errorCode = QStringLiteral("session_invalid");
            emit finished(requestId, reply);
            return;
        }

        const QString attachmentPrefix =
            QStringLiteral("/v1/profile/attachments/");
        if (request.method == QByteArrayLiteral("POST")
            && request.path == QLatin1String("/v1/profile/attachments")) {
            emit finished(
                requestId,
                m_service->begin(
                    request.body.value(QStringLiteral("attachment_id"))
                        .toString(),
                    request.body.value(QStringLiteral("source_kind"))
                        .toString(),
                    request.body.value(
                        QStringLiteral("source_semantic_digest"))
                        .toString()));
            return;
        }

        if (request.method == QByteArrayLiteral("GET")
            && request.path.startsWith(attachmentPrefix)) {
            emit finished(
                requestId,
                m_service->get(request.path.mid(attachmentPrefix.size())));
            return;
        }

        if (request.method == QByteArrayLiteral("POST")
            && request.path.startsWith(attachmentPrefix)
            && request.path.endsWith(QStringLiteral("/commit"))) {
            const QString attachmentId = request.path.mid(
                attachmentPrefix.size(),
                request.path.size() - attachmentPrefix.size()
                    - QStringLiteral("/commit").size());
            emit finished(requestId, m_service->commit(attachmentId));
            return;
        }

        if (request.method == QByteArrayLiteral("POST")
            && request.path == QLatin1String("/v1/sync/push")) {
            reply = m_service->push(
                request.body.value(QStringLiteral("mutations")).toArray(),
                request.body.value(QStringLiteral("attachment_id"))
                    .toString());
            if (m_dropNextPush) {
                m_dropNextPush = false;
                AccountTransportReply lost;
                lost.networkError = true;
                lost.errorCode = QStringLiteral("offline");
                emit finished(requestId, lost);
            } else {
                emit finished(requestId, reply);
            }
            return;
        }

        if (request.method == QByteArrayLiteral("GET")
            && request.path.startsWith(
                QStringLiteral("/v1/sync/pull?after="))) {
            bool ok = false;
            const quint64 after = request.path.mid(
                QStringLiteral("/v1/sync/pull?after=").size())
                .toULongLong(&ok);
            if (!ok) {
                AccountTransportReply invalid;
                invalid.statusCode = 400;
                invalid.errorCode = QStringLiteral("invalid_cursor");
                emit finished(requestId, invalid);
            } else {
                emit finished(requestId, m_service->pull(after));
            }
            return;
        }

        if (request.method == QByteArrayLiteral("GET")
            && request.path.startsWith(QStringLiteral("/v1/sync/snapshot"))) {
            emit finished(requestId, m_service->snapshot());
            return;
        }

        reply.statusCode = 404;
        reply.errorCode = QStringLiteral("fixture_route_missing");
        emit finished(requestId, reply);
    }

private:
    DesktopConvergenceService *m_service = nullptr;
    bool m_dropNextPush = false;
};

class DesktopConvergenceAdapter final : public SyncAdapter {
public:
    DesktopConvergenceAdapter(
        const QString &persistencePath,
        QObject *parent = nullptr)
        : SyncAdapter(parent),
          m_persistencePath(persistencePath) {
        load();
    }

    QString categoryId() const override {
        return QStringLiteral("collection");
    }

    int schemaVersion() const override {
        return 1;
    }

    quint64 revision() const override {
        return m_revision;
    }

    bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error) const override {
        if (!snapshot) {
            if (error)
                *error = QStringLiteral("snapshot output missing");
            return false;
        }
        snapshot->revision = m_revision;
        snapshot->records.clear();
        QStringList keys = m_records.keys();
        keys.sort();
        for (const QString &key : keys)
            snapshot->records.append({key, m_records.value(key)});
        return true;
    }

    bool applyRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        QString *error) override {
        if (schemaVersion != 1) {
            if (error)
                *error = QStringLiteral("schema mismatch");
            return false;
        }
        if (operation == SyncWireOperation::Put)
            m_records.insert(recordKey, payload);
        else
            m_records.remove(recordKey);
        ++m_revision;
        persist();
        ++m_remoteApplyCount;
        return true;
    }

    void putLocal(
        const QString &recordKey,
        const QJsonValue &payload) {
        m_records.insert(recordKey, payload);
        ++m_revision;
        persist();
        emit localMutationAvailable(m_revision);
    }

    bool contains(const QString &recordKey) const {
        return m_records.contains(recordKey);
    }

    QJsonValue value(const QString &recordKey) const {
        return m_records.value(recordKey);
    }

    int remoteApplyCount() const {
        return m_remoteApplyCount;
    }

private:
    void load() {
        QFile file(m_persistencePath);
        if (!file.open(QIODevice::ReadOnly))
            return;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            file.readAll(),
            &parseError);
        if (parseError.error != QJsonParseError::NoError
            || !document.isObject())
            return;
        const QJsonObject object = document.object();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            m_records.insert(it.key(), it.value());
    }

    void persist() const {
        QSaveFile file(m_persistencePath);
        if (!file.open(QIODevice::WriteOnly))
            return;
        QJsonObject object;
        QStringList keys = m_records.keys();
        keys.sort();
        for (const QString &key : keys)
            object.insert(key, m_records.value(key));
        const QByteArray bytes = QJsonDocument(object).toJson(
            QJsonDocument::Compact);
        if (file.write(bytes) == bytes.size())
            file.commit();
    }

    QString m_persistencePath;
    QHash<QString, QJsonValue> m_records;
    quint64 m_revision = 0;
    int m_remoteApplyCount = 0;
};

ProfilePaths accountProfile(
    QTemporaryDir *temp,
    const QString &accountId = QString::fromLatin1(kAccountId)) {
    const auto profile = ProfilePaths::account(accountId, temp->path());
    if (!profile.has_value())
        qFatal("account profile fixture could not be created");
    QDir().mkpath(profile->profileRoot());
    return *profile;
}

ProfilePaths localOnlyProfile(QTemporaryDir *temp) {
    const ProfilePaths profile = ProfilePaths::localOnly(temp->path());
    QDir().mkpath(profile.profileRoot());
    return profile;
}

struct DesktopRun {
    ProfilePaths profile;
    DesktopConvergenceTransport transport;
    AccountClient client;
    SyncAdapterRegistry registry;
    DesktopConvergenceAdapter adapter;
    SyncEngine engine;

    DesktopRun(
        DesktopConvergenceService *service,
        const ProfilePaths &profileValue,
        const QString &deviceId,
        qint64 *now)
        : profile(profileValue),
          transport(service),
          client(&transport),
          adapter(
              QDir(profile.profileRoot()).filePath(
                  QStringLiteral("desktop-adapter.json"))),
          engine(
              &client,
              &registry,
              [now]() { return *now; }) {
        client.setAccessToken(QByteArrayLiteral("fixture-access"));
        if (!registry.registerAdapter(&adapter))
            qFatal("desktop adapter registration failed");
        engine.setAutomaticSchedulingEnabled(false);
        engine.setNetworkEnabled(false);
        QString error;
        if (!engine.start(profile, deviceId, &error))
            qFatal("desktop engine start failed: %s",
                   qPrintable(error));
    }

    ~DesktopRun() {
        if (engine.active()) {
            QString error;
            engine.stopPreservingOutbox(&error);
        }
    }
};

SyncWireMutation remoteMutation(
    const QString &mutationId,
    const QString &recordKey,
    const QString &deviceId,
    qint64 physicalMs,
    const QJsonValue &payload) {
    SyncWireMutation mutation;
    mutation.mutationId = mutationId;
    mutation.deviceId = deviceId;
    mutation.category = QStringLiteral("collection");
    mutation.recordKey = recordKey;
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{physicalMs, 0, deviceId};
    mutation.operation = SyncWireOperation::Put;
    mutation.payload = payload;
    return mutation;
}

SyncWirePullEntry snapshotEntry(
    quint64 serverSeq,
    const SyncWireMutation &mutation) {
    SyncWirePullEntry entry;
    entry.serverSeq = serverSeq;
    entry.won = true;
    entry.canonical = true;
    entry.mutation = mutation;
    return entry;
}

AccountAttachmentCoordinator::SourceIdentity sourceIdentity() {
    AccountAttachmentCoordinator::SourceIdentity source;
    source.sourceKind = QStringLiteral("legacy_local");
    source.sourceProfileId = QStringLiteral("legacy");
    source.sourceSemanticDigest = QStringLiteral("sha256:desktop-source");
    source.sourceActivityDigest = QStringLiteral("sha256:desktop-activity");
    return source;
}

QJsonObject portableFacts(const QString &owner) {
    return QJsonObject{
        {QStringLiteral("collection"), owner + QStringLiteral("-collection")},
        {QStringLiteral("progress"), 42},
        {QStringLiteral("history"), owner + QStringLiteral("-history")},
        {QStringLiteral("watch_state"), QStringLiteral("in_progress")},
        {QStringLiteral("preferences"), QStringLiteral("dark")},
        {QStringLiteral("activity"), owner + QStringLiteral("-activity")}};
}
}

class tst_account_convergence final : public QObject {
    Q_OBJECT

private slots:
    void localProfileAttachesAndMaterializesUnion();
    void preExistingCloudRecordsSurviveAttachmentUnion();
    void restartMidAttachmentResumesReceiptWithoutDuplicateMutation();
    void secondDevicePullsUnifiedStreamWithoutDoubleApply();
    void logoutSealingIsolatesNextLocalOnlySession();
    void outboxSurvivesAuthRejectionAndRecovery();
};

void tst_account_convergence::localProfileAttachesAndMaterializesUnion() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DesktopConvergenceService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile = accountProfile(&temp);
    const QJsonObject cloudFacts = portableFacts(QStringLiteral("cloud"));
    const QJsonObject localFacts = portableFacts(QStringLiteral("local"));
    const SyncWireMutation cloudMutation = remoteMutation(
        QStringLiteral("f3600000-0000-4000-8000-000000000001"),
        QStringLiteral("portable/cloud"),
        QString::fromLatin1(kDeviceB),
        now,
        cloudFacts);
    service.appendRemote(cloudMutation);
    service.setSnapshotEntries({snapshotEntry(1, cloudMutation)});

    DesktopRun run(
        &service,
        profile,
        QString::fromLatin1(kDeviceA),
        &now);
    run.adapter.putLocal(QStringLiteral("portable/local"), localFacts);
    QTRY_COMPARE(run.engine.pendingOutboxCount(), 1);

    AccountAttachmentCoordinator coordinator(
        &run.client,
        &run.engine,
        profile);
    coordinator.setCloudStateVerifier([&](QString *) {
        return run.adapter.contains(QStringLiteral("portable/cloud"))
            && run.adapter.value(QStringLiteral("portable/local"))
                == localFacts;
    });
    QSignalSpy finished(&coordinator,
                       &AccountAttachmentCoordinator::finished);
    QString error;
    QVERIFY2(
        coordinator.start(
            QString::fromLatin1(kAttachmentId),
            sourceIdentity(),
            &error),
        qPrintable(error));
    run.engine.setNetworkEnabled(true);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.takeFirst().at(0).toBool());
    QCOMPARE(
        service.attachmentState(QString::fromLatin1(kAttachmentId)),
        QStringLiteral("committed"));
    QCOMPARE(
        AccountAttachmentReceipt::read(profile).status,
        AccountAttachmentReceipt::ReadStatus::Missing);
    QVERIFY(run.adapter.value(QStringLiteral("portable/local")) == localFacts);
}

void tst_account_convergence::preExistingCloudRecordsSurviveAttachmentUnion() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DesktopConvergenceService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile = accountProfile(&temp);
    const QJsonObject cloudFacts = portableFacts(QStringLiteral("cloud"));
    const QJsonObject localFacts = portableFacts(QStringLiteral("local"));
    const SyncWireMutation cloudMutation = remoteMutation(
        QStringLiteral("f3600000-0000-4000-8000-000000000002"),
        QStringLiteral("portable/cloud"),
        QString::fromLatin1(kDeviceB),
        now,
        cloudFacts);
    service.appendRemote(cloudMutation);
    service.setSnapshotEntries({snapshotEntry(1, cloudMutation)});

    DesktopRun run(
        &service,
        profile,
        QString::fromLatin1(kDeviceA),
        &now);
    run.adapter.putLocal(QStringLiteral("portable/local"), localFacts);
    QTRY_COMPARE(run.engine.pendingOutboxCount(), 1);

    AccountAttachmentCoordinator coordinator(
        &run.client,
        &run.engine,
        profile);
    coordinator.setCloudStateVerifier([&](QString *error) {
        const bool survives =
            run.adapter.value(QStringLiteral("portable/cloud")) == cloudFacts;
        if (!survives && error)
            *error = QStringLiteral("cloud record was lost during union");
        return survives
            && run.adapter.value(QStringLiteral("portable/local"))
                == localFacts;
    });
    QSignalSpy finished(&coordinator,
                       &AccountAttachmentCoordinator::finished);
    QString error;
    QVERIFY2(
        coordinator.start(
            QString::fromLatin1(kAttachmentId),
            sourceIdentity(),
            &error),
        qPrintable(error));
    run.engine.setNetworkEnabled(true);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.takeFirst().at(0).toBool());
    QCOMPARE(
        run.adapter.value(QStringLiteral("portable/cloud")),
        QJsonValue(cloudFacts));
    QCOMPARE(
        run.adapter.value(QStringLiteral("portable/local")),
        QJsonValue(localFacts));
}

void tst_account_convergence::restartMidAttachmentResumesReceiptWithoutDuplicateMutation() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DesktopConvergenceService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths profile = accountProfile(&temp);
    const QJsonObject localFacts = portableFacts(QStringLiteral("local"));
    service.setSnapshotEntries({});

    {
        DesktopRun first(
            &service,
            profile,
            QString::fromLatin1(kDeviceA),
            &now);
        first.adapter.putLocal(QStringLiteral("portable/local"), localFacts);
        QTRY_COMPARE(first.engine.pendingOutboxCount(), 1);
        first.transport.dropNextPushResponse();

        AccountAttachmentCoordinator coordinator(
            &first.client,
            &first.engine,
            profile);
        QString error;
        QVERIFY2(
            coordinator.start(
                QString::fromLatin1(kAttachmentId),
                sourceIdentity(),
                &error),
            qPrintable(error));
        first.engine.setNetworkEnabled(true);
        QTRY_COMPARE(first.engine.state(), SyncEngine::State::Retrying);
        QCOMPARE(
            service.attachmentState(QString::fromLatin1(kAttachmentId)),
            QStringLiteral("uploaded"));
        QCOMPARE(
            AccountAttachmentReceipt::read(profile).status,
            AccountAttachmentReceipt::ReadStatus::Ok);
        QVERIFY(first.engine.pendingOutboxCount() == 1);
        QVERIFY(first.engine.stopPreservingOutbox());
    }

    QCOMPARE(service.beginRequestCount(), 1);
    QCOMPARE(service.acceptedMutationCount(), 1);

    DesktopRun second(
        &service,
        profile,
        QString::fromLatin1(kDeviceA),
        &now);
    AccountAttachmentCoordinator coordinator(
        &second.client,
        &second.engine,
        profile);
    coordinator.setCloudStateVerifier([&](QString *) {
        return second.adapter.value(QStringLiteral("portable/local"))
            == localFacts;
    });
    QSignalSpy finished(&coordinator,
                       &AccountAttachmentCoordinator::finished);
    QString error;
    QVERIFY2(coordinator.resumePending(&error), qPrintable(error));
    second.engine.setNetworkEnabled(true);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.takeFirst().at(0).toBool());
    QCOMPARE(service.beginRequestCount(), 1);
    QCOMPARE(service.acceptedMutationCount(), 1);
    QCOMPARE(second.engine.pendingOutboxCount(), 0);
    QCOMPARE(
        AccountAttachmentReceipt::read(profile).status,
        AccountAttachmentReceipt::ReadStatus::Missing);
}

void tst_account_convergence::secondDevicePullsUnifiedStreamWithoutDoubleApply() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DesktopConvergenceService service;
    qint64 now = service.serverTimeMs;
    const QJsonObject cloudFacts = portableFacts(QStringLiteral("cloud"));
    const QJsonObject localFacts = portableFacts(QStringLiteral("local"));
    const SyncWireMutation cloudMutation = remoteMutation(
        QStringLiteral("f3600000-0000-4000-8000-000000000003"),
        QStringLiteral("portable/cloud"),
        QString::fromLatin1(kDeviceA),
        now,
        cloudFacts);
    service.appendRemote(cloudMutation);

    const ProfilePaths firstProfile = accountProfile(&temp);
    DesktopRun first(
        &service,
        firstProfile,
        QString::fromLatin1(kDeviceA),
        &now);
    first.adapter.putLocal(QStringLiteral("portable/local"), localFacts);
    QTRY_COMPARE(first.engine.pendingOutboxCount(), 1);
    first.engine.setNetworkEnabled(true);
    QTRY_COMPARE(first.engine.pendingOutboxCount(), 0);
    QVERIFY(first.engine.stopPreservingOutbox());

    QTemporaryDir secondTemp;
    QVERIFY(secondTemp.isValid());
    const ProfilePaths secondProfile = accountProfile(&secondTemp);
    DesktopRun second(
        &service,
        secondProfile,
        QString::fromLatin1(kDeviceB),
        &now);
    second.engine.setNetworkEnabled(true);
    QTRY_VERIFY(second.adapter.contains(QStringLiteral("portable/cloud")));
    QTRY_VERIFY(second.adapter.contains(QStringLiteral("portable/local")));
    QCOMPARE(
        second.adapter.value(QStringLiteral("portable/cloud")),
        QJsonValue(cloudFacts));
    QCOMPARE(
        second.adapter.value(QStringLiteral("portable/local")),
        QJsonValue(localFacts));
    const int applied = second.adapter.remoteApplyCount();
    second.engine.requestImmediateSync();
    QTest::qWait(50);
    QCOMPARE(second.adapter.remoteApplyCount(), applied);
}

void tst_account_convergence::logoutSealingIsolatesNextLocalOnlySession() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DesktopConvergenceService service;
    qint64 now = service.serverTimeMs;
    const ProfilePaths account = accountProfile(&temp);
    const ProfilePaths localOnly = localOnlyProfile(&temp);
    QVERIFY(account.syncStatePath() != localOnly.syncStatePath());

    {
        DesktopRun signedIn(
            &service,
            account,
            QString::fromLatin1(kDeviceA),
            &now);
        signedIn.adapter.putLocal(
            QStringLiteral("portable/account-only"),
            portableFacts(QStringLiteral("account")));
        QTRY_COMPARE(signedIn.engine.pendingOutboxCount(), 1);
        QVERIFY(signedIn.engine.stopPreservingOutbox());
    }

    DesktopConvergenceAdapter nextLocalSession(
        QDir(localOnly.profileRoot()).filePath(
            QStringLiteral("desktop-adapter.json")));
    QVERIFY(!nextLocalSession.contains(
        QStringLiteral("portable/account-only")));
    QVERIFY(!QFile::exists(localOnly.syncStatePath()));
}

void tst_account_convergence::outboxSurvivesAuthRejectionAndRecovery() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DesktopConvergenceService service;
    qint64 now = service.serverTimeMs;
    DesktopRun run(
        &service,
        accountProfile(&temp),
        QString::fromLatin1(kDeviceA),
        &now);
    run.adapter.putLocal(
        QStringLiteral("portable/pending"),
        portableFacts(QStringLiteral("pending")));
    QTRY_COMPARE(run.engine.pendingOutboxCount(), 1);

    QSignalSpy rejected(&run.engine, &SyncEngine::accessTokenRejected);
    run.client.clearAccessToken();
    run.engine.setNetworkEnabled(true);
    QTRY_COMPARE(run.engine.state(), SyncEngine::State::Retrying);
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(run.engine.pendingOutboxCount(), 1);

    run.client.setAccessToken(QByteArrayLiteral("fixture-access-refreshed"));
    run.engine.setNetworkEnabled(true);
    run.engine.requestImmediateSync();
    QTRY_COMPARE(run.engine.pendingOutboxCount(), 0);
    QCOMPARE(service.acceptedMutationCount(), 1);
}

QTEST_GUILESS_MAIN(tst_account_convergence)

#include "tst_account_convergence.moc"
