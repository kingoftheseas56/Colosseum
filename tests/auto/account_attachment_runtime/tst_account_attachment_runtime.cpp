// PRE-FLIGHT DRAFT STATUS: red integration proof for F03 runtime wiring.

#include "account/AccountAttachmentReceipt.h"
#include "account/AccountRuntime.h"
#include "account/ActivityStore.h"
#include "CollectionStore.h"
#include "account/HistoryStore.h"
#include "account/LegacyPersonalStateStorage.h"
#include "account/ProfilePaths.h"
#include "account/ProfilePreferencesStore.h"
#include "account/ProfileStoreRuntime.h"
#include "stremio/StremioState.h"
#include "stremio/StremioCodec.h"
#include "stremio/StremioSync.h"
#include "ProgressStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

namespace {

constexpr auto kAccountId =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceId =
    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";

class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char *name)
        : m_name(name),
          m_wasSet(qEnvironmentVariableIsSet(name)),
          m_previous(qgetenv(name)) {}

    ~ScopedEnvironmentVariable() {
        if (m_wasSet)
            qputenv(m_name.constData(), m_previous);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previous;
};

class LoopbackAccountService final : public QObject {
    Q_OBJECT

public:
    ~LoopbackAccountService() override {
        m_server.close();
        const QList<QTcpSocket *> sockets = m_buffers.keys();
        for (QTcpSocket *socket : sockets) {
            if (!socket)
                continue;
            QObject::disconnect(socket, nullptr, this, nullptr);
            socket->abort();
            delete socket;
        }
        m_buffers.clear();
    }

    bool listen(QString *error) {
        if (!m_server.listen(QHostAddress::LocalHost, 0)) {
            if (error)
                *error = m_server.errorString();
            return false;
        }

        connect(
            &m_server,
            &QTcpServer::newConnection,
            this,
            &LoopbackAccountService::acceptConnection);
        return true;
    }

    quint16 port() const {
        return m_server.serverPort();
    }

    int createRequestCount() const {
        return m_createRequestCount;
    }

    void enableAttachmentCompletion() {
        m_attachmentCompletionEnabled = true;
    }

    void setConcurrentHistoryLastActivity(qint64 value) {
        m_concurrentHistoryLastActivity = value;
    }

    void setCertifiedLwwSupersession(bool enabled) {
        m_certifiedLwwSupersession = enabled;
    }

    QStringList requests() const {
        return m_requests;
    }

    int uploadedMutationCount() const {
        int count = 0;
        for (const QJsonArray &batch : m_uploadedMutations)
            count += batch.size();
        return count;
    }

private:
    void acceptConnection() {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *socket = m_server.nextPendingConnection();
            m_buffers.insert(socket, QByteArray());
            connect(
                socket,
                &QTcpSocket::readyRead,
                this,
                [this, socket]() {
                    readRequest(socket);
                });
            connect(
                socket,
                &QTcpSocket::disconnected,
                this,
                [this, socket]() {
                    m_buffers.remove(socket);
                    socket->deleteLater();
                });
        }
    }

    void readRequest(QTcpSocket *socket) {
        if (!socket)
            return;

        QByteArray &buffer = m_buffers[socket];
        buffer.append(socket->readAll());
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        const QByteArray header = buffer.left(headerEnd);
        const QByteArray firstLine = header.left(header.indexOf("\r\n"));
        const QList<QByteArray> requestParts = firstLine.split(' ');
        if (requestParts.size() < 2)
            return send(socket, 400, QJsonObject());

        int contentLength = 0;
        const QList<QByteArray> headers = header.split('\n');
        for (QByteArray line : headers) {
            line = line.trimmed();
            if (line.toLower().startsWith("content-length:")) {
                bool ok = false;
                contentLength = line.mid(qstrlen("content-length:"))
                    .trimmed()
                    .toInt(&ok);
                if (!ok || contentLength < 0)
                    return send(socket, 400, QJsonObject());
            }
        }

        const qsizetype requestSize = headerEnd + 4 + contentLength;
        if (buffer.size() < requestSize)
            return;

        const QByteArray method = requestParts.at(0).trimmed();
        const QString path = QString::fromUtf8(
            requestParts.at(1).trimmed());
        const QByteArray body = buffer.mid(headerEnd + 4, contentLength);
        buffer.remove(0, requestSize);
        handleRequest(socket, method, path, body);
    }

    QJsonObject exportMutation(QJsonObject mutation) const {
        const QString category = mutation.value(
            QStringLiteral("category")).toString();
        QJsonValue payload = mutation.value(QStringLiteral("payload"));
        if (category == QLatin1String("full_history")
            && m_concurrentHistoryLastActivity > 0
            && payload.isObject()) {
            QJsonObject object = payload.toObject();
            object.insert(QStringLiteral("lastActivityAt"),
                          m_concurrentHistoryLastActivity);
            payload = object;
            mutation.insert(QStringLiteral("payload"), payload);
        } else if (category == QLatin1String("collection")
                   && m_certifiedLwwSupersession
                   && payload.isObject()) {
            QJsonObject object = payload.toObject();
            object.insert(QStringLiteral("title"), QStringLiteral("server-winner"));
            payload = object;
            mutation.insert(QStringLiteral("payload"), payload);
        }
        return mutation;
    }

    static QByteArray payloadHash(const QJsonValue &payload) {
        QByteArray encoded;
        if (payload.isObject())
            encoded = QJsonDocument(payload.toObject())
                .toJson(QJsonDocument::Compact);
        else if (payload.isArray())
            encoded = QJsonDocument(payload.toArray())
                .toJson(QJsonDocument::Compact);
        else if (!payload.isUndefined() && !payload.isNull())
            encoded = QJsonDocument(QJsonArray{payload})
                .toJson(QJsonDocument::Compact);
        if (!payload.isObject() && !payload.isArray()
            && !payload.isUndefined() && !payload.isNull()
            && encoded.size() >= 2) {
            encoded = encoded.mid(1, encoded.size() - 2);
        }
        return QCryptographicHash::hash(encoded, QCryptographicHash::Sha256);
    }

    void handleRequest(
        QTcpSocket *socket,
        const QByteArray &method,
        const QString &path,
        const QByteArray &bodyBytes) {
        m_requests.append(
            QString::fromLatin1(method)
            + QLatin1Char(' ')
            + path);
        QJsonObject body;
        if (!bodyBytes.isEmpty()) {
            const QJsonDocument document =
                QJsonDocument::fromJson(bodyBytes);
            if (document.isObject())
                body = document.object();
        }
        if (method == QByteArrayLiteral("POST")
            && path == QLatin1String("/v1/accounts")) {
            ++m_createRequestCount;
            QJsonObject account{
                {QStringLiteral("id"), QString::fromLatin1(kAccountId)},
                {QStringLiteral("username"), QStringLiteral("f03-user")},
                {QStringLiteral("protect_new_device_signins"), false}};
            QJsonObject device{
                {QStringLiteral("id"), QString::fromLatin1(kDeviceId)}};
            QJsonObject session{
                {QStringLiteral("account"), account},
                {QStringLiteral("device"), device},
                {QStringLiteral("access_token"), QStringLiteral("f03-access")},
                {QStringLiteral("refresh_token"), QStringLiteral("f03-refresh")},
                {QStringLiteral("access_expires_at"),
                 QDateTime::currentDateTimeUtc()
                     .addSecs(3600)
                     .toString(Qt::ISODateWithMs)}};
            send(
                socket,
                201,
                QJsonObject{
                    {QStringLiteral("session"), session},
                    {QStringLiteral("recovery_key"),
                     QStringLiteral("CLSM-F03-TEST-RECOVERY-KEY")}});
            return;
        }

        if (method == QByteArrayLiteral("GET")
            && path.startsWith(QStringLiteral("/v1/sync/pull?after="))) {
            send(
                socket,
                200,
                QJsonObject{
                    {QStringLiteral("server_time_ms"), QStringLiteral("1")},
                    {QStringLiteral("entries"), QJsonArray()},
                    {QStringLiteral("has_more"), false}});
            return;
        }

        if (method == QByteArrayLiteral("POST")
            && path == QLatin1String("/v1/sync/push")) {
            QJsonArray results;
            const QJsonArray mutations = body.value(
                QStringLiteral("mutations")).toArray();
            // The loopback fixture has no server-side current-state store;
            // every accepted push is its canonical export contribution. The
            // production service still binds attachment pushes by id and
            // manifest under the account lock.
            m_uploadedMutations.append(mutations);
            for (const QJsonValue &value : mutations) {
                if (!value.isObject())
                    continue;
                results.append(QJsonObject{
                    {QStringLiteral("mutation_id"),
                     value.toObject().value(
                         QStringLiteral("mutation_id"))},
                    {QStringLiteral("accepted"), true},
                    {QStringLiteral("server_seq"),
                     QStringLiteral("1")},
                    {QStringLiteral("won"), true}});
            }
            send(
                socket,
                200,
                QJsonObject{
                    {QStringLiteral("server_time_ms"), QStringLiteral("1")},
                    {QStringLiteral("results"), results}});
            return;
        }

        if (m_attachmentCompletionEnabled
            && method == QByteArrayLiteral("POST")
            && path == QLatin1String("/v1/profile/attachments")) {
            m_attachmentId = body.value(
                QStringLiteral("attachment_id")).toString();
            send(
                socket,
                200,
                QJsonObject{
                    {QStringLiteral("attachment_id"), m_attachmentId},
                    {QStringLiteral("device_id"), QString::fromLatin1(kDeviceId)},
                    {QStringLiteral("baseline_server_seq"), QStringLiteral("0")},
                    {QStringLiteral("state"), QStringLiteral("open")} });
            return;
        }

        if (m_attachmentCompletionEnabled
            && method == QByteArrayLiteral("GET")
            && path.startsWith(QStringLiteral("/v1/profile/attachments/"))) {
            if (m_attachmentId.isEmpty()) {
                send(
                    socket,
                    404,
                    QJsonObject{
                        {QStringLiteral("error"),
                         QJsonObject{
                             {QStringLiteral("code"),
                              QStringLiteral("attachment_not_found")},
                             {QStringLiteral("message"),
                              QStringLiteral("The attachment is not open yet.")}}}});
                return;
            }
            send(
                socket,
                200,
                QJsonObject{
                    {QStringLiteral("attachment_id"), m_attachmentId},
                    {QStringLiteral("device_id"), QString::fromLatin1(kDeviceId)},
                    {QStringLiteral("baseline_server_seq"), QStringLiteral("0")},
                    {QStringLiteral("state"), QStringLiteral("open")} });
            return;
        }

        if (m_attachmentCompletionEnabled
            && method == QByteArrayLiteral("POST")
            && path.startsWith(QStringLiteral("/v1/profile/attachments/"))
            && path.endsWith(QStringLiteral("/commit"))) {
            QJsonArray dispositions;
            for (const QJsonArray &batch : m_uploadedMutations) {
                for (const QJsonValue &mutationValue : batch) {
                    if (!mutationValue.isObject())
                        continue;
                    const QJsonObject mutation = exportMutation(
                        mutationValue.toObject());
                    const QJsonValue payload = mutation.value(
                        QStringLiteral("payload"));
                    const QByteArray hash = payloadHash(payload);
                    const QString disposition =
                        m_certifiedLwwSupersession
                            && mutation.value(QStringLiteral("category"))
                                   == QLatin1String("collection")
                        ? QStringLiteral("superseded")
                        : QStringLiteral("materialized");
                    dispositions.append(QJsonObject{
                        {QStringLiteral("mutation_id"), mutation.value(
                             QStringLiteral("mutation_id"))},
                        {QStringLiteral("category"), mutation.value(
                             QStringLiteral("category"))},
                        {QStringLiteral("record_key"), mutation.value(
                             QStringLiteral("record_key"))},
                        {QStringLiteral("operation"), mutation.value(
                             QStringLiteral("operation"))},
                        {QStringLiteral("disposition"), disposition},
                        {QStringLiteral("materialized_payload_hash"),
                         QString::fromLatin1(hash.toHex())}});
                }
            }
            send(
                socket,
                200,
                QJsonObject{
                    {QStringLiteral("attachment_id"), m_attachmentId},
                    {QStringLiteral("device_id"), QString::fromLatin1(kDeviceId)},
                    {QStringLiteral("baseline_server_seq"), QStringLiteral("0")},
                    {QStringLiteral("state"), QStringLiteral("committed")},
                    {QStringLiteral("fresh_export_snapshot_id"),
                     QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc")},
                    {QStringLiteral("fresh_export_cursor"), QStringLiteral("fresh-cursor")},
                    {QStringLiteral("fresh_export_high_water_server_seq"), QStringLiteral("1")},
                    {QStringLiteral("dispositions"), dispositions} });
            return;
        }

        if (m_attachmentCompletionEnabled
            && method == QByteArrayLiteral("GET")
            && path.startsWith(QStringLiteral("/v1/account/export?cursor="))) {
            QJsonArray items;
            for (const QJsonArray &batch : m_uploadedMutations) {
                for (const QJsonValue &mutation : batch) {
                    if (!mutation.isObject())
                        continue;
                    const QJsonObject object = exportMutation(
                        mutation.toObject());
                    items.append(QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("record")},
                        {QStringLiteral("category"), object.value(
                             QStringLiteral("category"))},
                        {QStringLiteral("key"), object.value(
                             QStringLiteral("record_key"))},
                        {QStringLiteral("payload"), object.value(
                             QStringLiteral("payload"))}});
                }
            }
            send(
                socket,
                200,
                QJsonObject{
                    {QStringLiteral("format"), QStringLiteral("colosseum.account.export")},
                    {QStringLiteral("schema_version"), 1},
                    {QStringLiteral("snapshot_id"),
                     QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc")},
                    {QStringLiteral("cursor"), QStringLiteral("fresh-cursor")},
                    {QStringLiteral("high_water_server_seq"), QStringLiteral("1")},
                    {QStringLiteral("items"), items},
                    {QStringLiteral("has_more"), false}});
            return;
        }

        if (method == QByteArrayLiteral("GET")
            && path.startsWith(QStringLiteral("/v1/sync/snapshot"))) {
            send(
                socket,
                200,
                QJsonObject{
                    {QStringLiteral("server_time_ms"), QStringLiteral("1")},
                    {QStringLiteral("cursor"), QStringLiteral("0")},
                    {QStringLiteral("entries"), QJsonArray()},
                    {QStringLiteral("has_more"), false}});
            return;
        }

        send(
            socket,
            404,
            QJsonObject{
                {QStringLiteral("error"),
                 QJsonObject{
                     {QStringLiteral("code"),
                      QStringLiteral("fixture_route_missing")},
                     {QStringLiteral("message"),
                      QStringLiteral("No route in the F03 runtime fixture.")}}}});
    }

    static void send(
        QTcpSocket *socket,
        int status,
        const QJsonObject &body) {
        if (!socket)
            return;

        const QByteArray payload = QJsonDocument(body).toJson(
            QJsonDocument::Compact);
        const QByteArray reason = status == 201
            ? QByteArrayLiteral("Created")
            : status == 200
                ? QByteArrayLiteral("OK")
                : QByteArrayLiteral("Bad Request");
        const QByteArray response = QByteArrayLiteral("HTTP/1.1 ")
            + QByteArray::number(status)
            + ' '
            + reason
            + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(payload.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
            + payload;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QList<QJsonArray> m_uploadedMutations;
    QStringList m_requests;
    QString m_attachmentId;
    bool m_attachmentCompletionEnabled = false;
    qint64 m_concurrentHistoryLastActivity = 0;
    bool m_certifiedLwwSupersession = false;
    int m_createRequestCount = 0;
};

}

class tst_account_attachment_runtime final : public QObject {
    Q_OBJECT

private slots:
    void createNewAccountAdoptionWaitsForAttachmentVerificationBeforeRetiringSource();
    void createNewAccountRetiresSourceOnlyAfterFreshExportAbsorption();
    void createNewAccountAcceptsConcurrentHistoryMergeAfterCommit();
    void createNewAccountAcceptsCertifiedLwwSupersession();
    void activitySourceClearRemovesLedgerAfterAttachmentCompletion();
    void stremioMarkerChangeUpdatesActiveRuntimeState();
    void stremioLocalOnlyOwnerReceiptJournalsRelayWithoutNeonEngine();
    void stremioLateOwnerReceiptCannotCrossProfileIncarnation();
    void stremioRuntimeReplaysProviderRedoAfterCrashWithoutActivityFact();
    void stremioRuntimeAppliesInboundSeriesWatchedThroughMetadataBridge();
    void stremioSeriesWithoutBoundAccountCompletesFailClosed();
    void stremioPendingSeriesWatchSurvivesMissingMapAndRestart();
    void stremioQmlMetadataFixtureProjectsIdentityOnlyToNative();
    void stremioInactiveAccountEngineRetainsProviderRedo();
};

void tst_account_attachment_runtime::
stremioMarkerChangeUpdatesActiveRuntimeState() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("stremio-marker-runtime-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood-Stremio"));
    QCoreApplication::setApplicationName(QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    AccountRuntime runtime;
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);
    StremioSync *sync = qobject_cast<StremioSync *>(
        engine.rootContext()->contextProperty(QStringLiteral("stremioSyncState")).value<QObject *>());
    QVERIFY(sync);
    QString error;
    QVERIFY2(runtime.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
    QTRY_COMPARE(sync->status(), QStringLiteral("notConnected"));

    ProfilePreferencesStore *preferences = runtime.profileStores()->preferencesStore();
    QVERIFY(preferences);
    QVERIFY(preferences->setMainSyncProvider(QStringLiteral("stremio")));
    QTRY_COMPARE(sync->status(), QStringLiteral("reconnectRequired"));
}

void tst_account_attachment_runtime::
stremioLocalOnlyOwnerReceiptJournalsRelayWithoutNeonEngine() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("stremio-local-owner-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood-Stremio"));
    QCoreApplication::setApplicationName(QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    AccountRuntime runtime;
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);
    StremioSync *sync = qobject_cast<StremioSync *>(
        engine.rootContext()->contextProperty(QStringLiteral("stremioSyncState")).value<QObject *>());
    QVERIFY(sync);
    QString error;
    QVERIFY2(runtime.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
    ProfilePreferencesStore *preferences = runtime.profileStores()->preferencesStore();
    CollectionStore *collection = runtime.profileStores()->collectionStore();
    ProgressStore *progress = runtime.profileStores()->progressStore();
    QVERIFY(preferences && collection && progress);
    QVERIFY(preferences->setMainSyncProvider(QStringLiteral("stremio")));
    QVERIFY(collection->add(QStringLiteral("theatre"), QVariantMap{
        {QStringLiteral("id"), QStringLiteral("tt-runtime-relay")},
        {QStringLiteral("type"), QStringLiteral("movie")}}));
    progress->recordSilent(QVariantMap{
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("id"), QStringLiteral("tt-runtime-relay")},
        {QStringLiteral("duration"), 300.0},
        {QStringLiteral("resume"), QVariantMap{{QStringLiteral("position"), 12.5}}}});
    progress->setWatchedMark(QStringLiteral("tt-runtime-relay"), true);

    // The local-only profile has no active Neon engine. Its canonical owner
    // still crosses the asynchronous ProgressDiskWriter receipt and creates
    // private provider work; it is not discarded because account relay is off.
    QTRY_COMPARE(sync->pendingCount(), 3);
}

void tst_account_attachment_runtime::
stremioLateOwnerReceiptCannotCrossProfileIncarnation() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("stremio-incarnation-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood-Stremio"));
    QCoreApplication::setApplicationName(QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    AccountRuntime runtime;
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);
    StremioSync *sync = qobject_cast<StremioSync *>(
        engine.rootContext()->contextProperty(QStringLiteral("stremioSyncState")).value<QObject *>());
    QVERIFY(sync);
    QString error;
    QVERIFY2(runtime.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
    QVERIFY(runtime.profileStores()->preferencesStore()->setMainSyncProvider(QStringLiteral("stremio")));
    QVERIFY(runtime.profileStores()->collectionStore()->add(QStringLiteral("theatre"), QVariantMap{
        {QStringLiteral("id"), QStringLiteral("tt-old-incarnation")},
        {QStringLiteral("type"), QStringLiteral("movie")}}));
    runtime.profileStores()->progressStore()->recordSilent(QVariantMap{
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("id"), QStringLiteral("tt-old-incarnation")},
        {QStringLiteral("duration"), 60.0},
        {QStringLiteral("resume"), QVariantMap{{QStringLiteral("position"), 3.0}}}});

    // Do not wait for A's queued writer receipt. A new profile incarnation
    // must fence that late callback before it can journal A's state into B.
    const QString accountId = QStringLiteral("dddddddd-dddd-4ddd-8ddd-dddddddddddd");
    const auto accountPaths = ProfilePaths::account(accountId);
    QVERIFY(accountPaths.has_value());
    QVERIFY(QDir().mkpath(accountPaths->profileRoot()));
    QVERIFY2(runtime.profileStores()->activateAccountProfile(accountId, &error), qPrintable(error));
    QTRY_COMPARE(sync->activeProfileId(), accountId);
    QTest::qWait(100);
    QCOMPARE(sync->pendingCount(), 0);
}

void tst_account_attachment_runtime::
stremioRuntimeReplaysProviderRedoAfterCrashWithoutActivityFact() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("stremio-redo-runtime-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood-Stremio"));
    QCoreApplication::setApplicationName(QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    QString profileId;
    QString statePath;
    {
        AccountRuntime interrupted;
        QQmlApplicationEngine engine;
        interrupted.prepareForQml(&engine);
        QString error;
        QVERIFY2(interrupted.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
        const ProfilePaths profile = interrupted.profileStores()->activeProfile();
        profileId = profile.profileId();
        statePath = profile.stremioSyncStatePath();

        StremioPersistentState state;
        state.profileId = profileId;
        state.bindingGeneration = 1;
        const QJsonObject projection{
            {QStringLiteral("kind"), QStringLiteral("item")},
            {QStringLiteral("hasCollection"), true},
            {QStringLiteral("hasProgress"), false},
            {QStringLiteral("hasHistory"), false},
            {QStringLiteral("collection"), QJsonObject{
                {QStringLiteral("world"), QStringLiteral("theatre")},
                {QStringLiteral("id"), QStringLiteral("tt-runtime-redo")},
                {QStringLiteral("type"), QStringLiteral("movie")},
                {QStringLiteral("title"), QStringLiteral("Restart relay")}}},
            {QStringLiteral("progress"), QJsonObject{}},
            {QStringLiteral("history"), QJsonObject{}}};
        const QJsonObject redo{
            {QStringLiteral("operationId"), QStringLiteral("redo-runtime-op")},
            {QStringLiteral("profileId"), profileId},
            {QStringLiteral("accountId"), QString()},
            {QStringLiteral("bindingGeneration"), QStringLiteral("1")},
            {QStringLiteral("id"), QStringLiteral("tt-runtime-redo")},
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("libraryMember"), true},
            {QStringLiteral("removed"), false},
            {QStringLiteral("projection"), projection}};
        state.importRedoReceipts = QJsonArray{redo};
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(statePath, state);
        QTRY_COMPARE(committed.count(), 1);
    }

    AccountRuntime restarted;
    QQmlApplicationEngine engine;
    restarted.prepareForQml(&engine);
    QString error;
    QVERIFY2(restarted.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
    StremioSync *sync = qobject_cast<StremioSync *>(
        engine.rootContext()->contextProperty(QStringLiteral("stremioSyncState")).value<QObject *>());
    QVERIFY(sync);
    QCOMPARE(sync->activeProfileId(), profileId);
    QCOMPARE(sync->pendingProviderImports().size(), 1);
    QTRY_VERIFY(restarted.profileStores()->collectionStore()->has(
        QStringLiteral("theatre"),
        QStringLiteral("tt-runtime-redo")));
    QVERIFY(restarted.profileStores()->activityStore()->historyProjectionFacts().isEmpty());

    StremioState inspector;
    QTRY_VERIFY([&] {
        const auto settled = inspector.load(statePath);
        return settled.has_value() && settled->importRedoReceipts.isEmpty();
    }());
}

void tst_account_attachment_runtime::
stremioRuntimeAppliesInboundSeriesWatchedThroughMetadataBridge() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("stremio-series-runtime-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood-Stremio"));
    QCoreApplication::setApplicationName(QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    QString profileId;
    QString statePath;
    {
        AccountRuntime seeded;
        QQmlApplicationEngine engine;
        seeded.prepareForQml(&engine);
        QString error;
        QVERIFY2(seeded.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
        const ProfilePaths profile = seeded.profileStores()->activeProfile();
        profileId = profile.profileId();
        statePath = profile.stremioSyncStatePath();
        StremioPersistentState state;
        state.profileId = profileId;
        state.bindingGeneration = 1;
        state.accountId = QStringLiteral("fixture-account");
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(statePath, state);
        QTRY_COMPARE(committed.count(), 1);
    }

    AccountRuntime runtime;
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);
    QString error;
    QVERIFY2(runtime.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
    QVERIFY(runtime.profileStores()->preferencesStore()->setMainSyncProvider(QStringLiteral("stremio")));
    StremioSync *sync = qobject_cast<StremioSync *>(
        engine.rootContext()->contextProperty(QStringLiteral("stremioSyncState")).value<QObject *>());
    QVERIFY(sync);
    sync->setEpisodeMetadataBridgeReady(true);
    connect(sync, &StremioSync::episodeMetadataRequested,
            sync, [sync](const QString &requestId, const QString &seriesId) {
                QVERIFY(sync->submitEpisodeMetadata(requestId, seriesId, QVariantList{
                    QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:runtime:s1:e1")},
                                {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 1}},
                    QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:runtime:s1:e2")},
                                {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 2}}}));
            });
    const QList<StremioEpisodeIdentity> videos{
        {QStringLiteral("kitsu:runtime:s1:e1"), 1, 1},
        {QStringLiteral("kitsu:runtime:s1:e2"), 1, 2}};
    QString encoded;
    QString encodeError;
    QVERIFY2(StremioCodec::encodeWatchedEpisodes(
                 QSet<QString>{QStringLiteral("kitsu:runtime:s1:e2")}, videos, &encoded, &encodeError),
             qPrintable(encodeError));
    StremioLibraryItem series;
    series.id = QStringLiteral("kitsu:runtime");
    series.type = QStringLiteral("series");
    series.libraryMember = true;
    series.raw = QJsonObject{{QStringLiteral("_id"), series.id},
                             {QStringLiteral("type"), series.type},
                             {QStringLiteral("name"), QStringLiteral("Runtime series")},
                             {QStringLiteral("state"), QJsonObject{{QStringLiteral("watched"), encoded}}}};
    bool completed = false;
    QString importError;
    QVERIFY(runtime.applyStremioLibraryItem(
        series, [&completed, &importError](bool committed, const QString &errorText) {
            completed = committed;
            importError = errorText;
        }));
    QTRY_VERIFY2(completed, qPrintable(importError));
    ProgressStore *progress = runtime.profileStores()->progressStore();
    QVERIFY(progress);
    QTRY_COMPARE(progress->get(QStringLiteral("video"), QStringLiteral("kitsu:runtime:s1:e2"))
                     .value(QStringLiteral("progress")).toDouble(), 1.0);
    QVERIFY(progress->get(QStringLiteral("video"), QStringLiteral("kitsu:runtime:s1:e1")).isEmpty());
    QVERIFY(runtime.profileStores()->activityStore()->historyProjectionFacts().isEmpty());
}

void tst_account_attachment_runtime::
stremioSeriesWithoutBoundAccountCompletesFailClosed() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("stremio-series-no-account-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood-Stremio"));
    QCoreApplication::setApplicationName(QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));
    AccountRuntime runtime;
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);
    QString error;
    QVERIFY2(runtime.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
    QVERIFY(runtime.profileStores()->preferencesStore()->setMainSyncProvider(QStringLiteral("stremio")));
    StremioLibraryItem series;
    series.id = QStringLiteral("kitsu:no-account");
    series.type = QStringLiteral("series");
    series.libraryMember = true;
    series.raw = QJsonObject{{QStringLiteral("_id"), series.id},
                             {QStringLiteral("type"), series.type},
                             {QStringLiteral("state"), QJsonObject{
                                 {QStringLiteral("watched"), QStringLiteral("not-a-real-field")}}}};
    bool called = false;
    bool committed = true;
    QVERIFY(runtime.applyStremioLibraryItem(
        series, [&called, &committed](bool ok, const QString &) {
            called = true;
            committed = ok;
        }));
    QTRY_VERIFY(called);
    QVERIFY(!committed);
}

void tst_account_attachment_runtime::
stremioPendingSeriesWatchSurvivesMissingMapAndRestart() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("stremio-series-restart-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood-Stremio"));
    QCoreApplication::setApplicationName(QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    const QList<StremioEpisodeIdentity> videos{
        {QStringLiteral("kitsu:restart:s1:e1"), 1, 1},
        {QStringLiteral("kitsu:restart:s1:e2"), 1, 2}};
    QString encoded;
    QString encodeError;
    QVERIFY2(StremioCodec::encodeWatchedEpisodes(
                 QSet<QString>{QStringLiteral("kitsu:restart:s1:e2")}, videos,
                 &encoded, &encodeError), qPrintable(encodeError));
    StremioLibraryItem series;
    series.id = QStringLiteral("kitsu:restart");
    series.type = QStringLiteral("series");
    series.libraryMember = true;
    series.raw = QJsonObject{{QStringLiteral("_id"), series.id},
                             {QStringLiteral("type"), series.type},
                             {QStringLiteral("name"), QStringLiteral("Restart series")},
                             {QStringLiteral("state"), QJsonObject{{QStringLiteral("watched"), encoded}}}};
    QString profileId;
    QString statePath;
    {
        AccountRuntime seeded;
        QQmlApplicationEngine engine;
        seeded.prepareForQml(&engine);
        QString error;
        QVERIFY2(seeded.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
        const ProfilePaths profile = seeded.profileStores()->activeProfile();
        profileId = profile.profileId();
        statePath = profile.stremioSyncStatePath();
        StremioPersistentState state;
        state.profileId = profileId;
        state.bindingGeneration = 1;
        state.accountId = QStringLiteral("fixture-account");
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(statePath, state);
        QTRY_COMPARE(committed.count(), 1);
    }
    {
        AccountRuntime pending;
        QQmlApplicationEngine engine;
        pending.prepareForQml(&engine);
        QString error;
        QVERIFY2(pending.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
        QVERIFY(pending.profileStores()->preferencesStore()->setMainSyncProvider(QStringLiteral("stremio")));
        StremioSync *sync = qobject_cast<StremioSync *>(
            engine.rootContext()->contextProperty(QStringLiteral("stremioSyncState")).value<QObject *>());
        QVERIFY(sync);
        sync->setEpisodeMetadataBridgeReady(true);
        QSignalSpy requested(sync, &StremioSync::episodeMetadataRequested);
        // The metadata request is deliberately unanswered: no guessed map and
        // no partial completion may reach Progress.
        QVERIFY(pending.applyStremioLibraryItem(series, {}));
        QTRY_COMPARE(requested.count(), 1);
        StremioLibraryItem unrelated;
        unrelated.id = QStringLiteral("tt-unrelated-continuues");
        unrelated.type = QStringLiteral("movie");
        unrelated.libraryMember = true;
        unrelated.raw = QJsonObject{{QStringLiteral("_id"), unrelated.id},
                                    {QStringLiteral("type"), unrelated.type},
                                    {QStringLiteral("name"), QStringLiteral("Unrelated")},
                                    {QStringLiteral("state"), QJsonObject{}}};
        bool unrelatedCommitted = false;
        QVERIFY(pending.applyStremioLibraryItem(
            unrelated, [&unrelatedCommitted](bool committed, const QString &) {
                unrelatedCommitted = committed;
            }));
        QTRY_VERIFY(unrelatedCommitted);
        QVERIFY(pending.profileStores()->collectionStore()->has(
            QStringLiteral("theatre"), unrelated.id));
        StremioState inspector;
        QTRY_VERIFY([&] {
            const auto state = inspector.load(statePath);
            return state.has_value() && std::any_of(
                state->importRedoReceipts.cbegin(), state->importRedoReceipts.cend(),
                [](const QJsonValue &redo) {
                    return redo.toObject().value(QStringLiteral("projection")).toObject()
                        .value(QStringLiteral("kind")).toString() == QLatin1String("episode_pending");
                });
        }());
        const auto persisted = inspector.load(statePath);
        QVERIFY(persisted.has_value());
        const QByteArray privateState = QJsonDocument(StremioState::encode(*persisted))
            .toJson(QJsonDocument::Compact);
        QVERIFY(!privateState.contains("authKey"));
        QVERIFY(!privateState.contains("addon"));
        QVERIFY(!privateState.contains("requestId"));
    }

    AccountRuntime restarted;
    QQmlApplicationEngine engine;
    restarted.prepareForQml(&engine);
    QString error;
    QVERIFY2(restarted.profileStores()->activateLocalOnlyProfile(&error), qPrintable(error));
    StremioSync *sync = qobject_cast<StremioSync *>(
        engine.rootContext()->contextProperty(QStringLiteral("stremioSyncState")).value<QObject *>());
    QVERIFY(sync);
    connect(sync, &StremioSync::episodeMetadataRequested,
            sync, [sync](const QString &requestId, const QString &seriesId) {
                QVERIFY(sync->submitEpisodeMetadata(requestId, seriesId, QVariantList{
                    QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:restart:s1:e1")},
                                {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 1}},
                    QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:restart:s1:e2")},
                                {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 2}}}));
            });
    sync->setEpisodeMetadataBridgeReady(true);
    QTRY_COMPARE(restarted.profileStores()->progressStore()->get(
        QStringLiteral("video"), QStringLiteral("kitsu:restart:s1:e2"))
                     .value(QStringLiteral("progress")).toDouble(), 1.0);
    StremioState inspector;
    QTRY_VERIFY([&] {
        const auto settled = inspector.load(statePath);
        return settled.has_value() && std::none_of(
            settled->importRedoReceipts.cbegin(), settled->importRedoReceipts.cend(),
            [](const QJsonValue &redo) {
                return redo.toObject().value(QStringLiteral("projection")).toObject()
                    .value(QStringLiteral("kind")).toString() == QLatin1String("episode_pending");
            });
    }());
}

void tst_account_attachment_runtime::
stremioQmlMetadataFixtureProjectsIdentityOnlyToNative() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString statePath = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState state;
    state.profileId = QStringLiteral("qml-profile");
    state.bindingGeneration = 1;
    state.accountId = QStringLiteral("fixture-account");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(statePath, state);
        QTRY_COMPARE(committed.count(), 1);
    }
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("qml-profile"), statePath, false));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("stremioSyncState"), &sync);
    const QString fixture = QFINDTESTDATA("stremio_episode_metadata_fixture.qml");
    QVERIFY2(!fixture.isEmpty(), "The QML metadata fixture is available to the runtime test.");
    engine.load(QUrl::fromLocalFile(fixture));
    QVERIFY(!engine.rootObjects().isEmpty());

    bool completed = false;
    QList<StremioEpisodeIdentity> resolved;
    QVERIFY(sync.requestEpisodeMetadata(
        QStringLiteral("kitsu:qml-fixture"),
        [&completed, &resolved](bool ok, QList<StremioEpisodeIdentity> videos) {
            completed = ok;
            resolved = std::move(videos);
        }));
    QTRY_VERIFY(completed);
    QCOMPARE(resolved.size(), 2);
    QCOMPARE(resolved.at(0).videoId, QStringLiteral("kitsu:qml-fixture:s0:e1"));
    QCOMPARE(resolved.at(1).season, 1);
    QCOMPARE(resolved.at(1).episode, 1);
}

void tst_account_attachment_runtime::
stremioInactiveAccountEngineRetainsProviderRedo() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("stremio-inactive-account-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood-Stremio"));
    QCoreApplication::setApplicationName(QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));
    const QString accountId = QStringLiteral("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee");
    const auto expectedProfile = ProfilePaths::account(accountId);
    QVERIFY(expectedProfile.has_value());
    QVERIFY(QDir().mkpath(expectedProfile->profileRoot()));

    const QJsonObject projection{
        {QStringLiteral("kind"), QStringLiteral("item")},
        {QStringLiteral("hasCollection"), true},
        {QStringLiteral("hasProgress"), false},
        {QStringLiteral("hasHistory"), false},
        {QStringLiteral("collection"), QJsonObject{
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("id"), QStringLiteral("tt-account-engine-held")},
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("Held for Neon")}}},
        {QStringLiteral("progress"), QJsonObject{}},
        {QStringLiteral("history"), QJsonObject{}}};
    StremioPersistentState state;
    state.profileId = accountId;
    state.bindingGeneration = 1;
    state.importRedoReceipts = QJsonArray{QJsonObject{
        {QStringLiteral("operationId"), QStringLiteral("redo-inactive-account")},
        {QStringLiteral("profileId"), accountId},
        {QStringLiteral("accountId"), QString()},
        {QStringLiteral("bindingGeneration"), QStringLiteral("1")},
        {QStringLiteral("id"), QStringLiteral("tt-account-engine-held")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("libraryMember"), true},
        {QStringLiteral("removed"), false},
        {QStringLiteral("projection"), projection}}};
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(expectedProfile->stremioSyncStatePath(), state);
        QTRY_COMPARE(committed.count(), 1);
    }

    AccountRuntime runtime;
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);
    QString error;
    QVERIFY2(runtime.profileStores()->activateAccountProfile(accountId, &error), qPrintable(error));
    StremioSync *sync = qobject_cast<StremioSync *>(
        engine.rootContext()->contextProperty(QStringLiteral("stremioSyncState")).value<QObject *>());
    QVERIFY(sync);
    // The canonical owner may already be durable, but without an active Neon
    // engine the provider redo cannot be settled. This differs from a
    // local-only profile, where no Neon relay is owed.
    QTRY_VERIFY(runtime.profileStores()->collectionStore()->has(
        QStringLiteral("theatre"),
        QStringLiteral("tt-account-engine-held")));
    QCOMPARE(sync->pendingProviderImports().size(), 1);
}

void tst_account_attachment_runtime::
createNewAccountAdoptionWaitsForAttachmentVerificationBeforeRetiringSource() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    ScopedEnvironmentVariable restoreEndpoint(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");

    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("f03-runtime-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(
        QStringLiteral("Brotherhood-F03"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    LoopbackAccountService service;
    QString error;
    QVERIFY2(service.listen(&error), qPrintable(error));
    qputenv(
        "COLOSSEUM_ACCOUNT_SERVICE_URL",
        QStringLiteral("http://127.0.0.1:%1")
            .arg(service.port())
            .toLatin1());

    const LegacyPersonalStateStorage legacy =
        LegacyPersonalStateStorage::forCurrentInstallation();
    {
        ProgressStore sourceProgress;
        sourceProgress.record(
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("source-video")},
                {QStringLiteral("kind"), QStringLiteral("video")},
                {QStringLiteral("progress"), 0.42}});
        sourceProgress.flush();
    }
    const auto before = legacy.capture(&error);
    QVERIFY2(before.has_value(), qPrintable(error));
    QVERIFY(!before->isEmpty());

    AccountRuntime runtime;
    QSignalSpy profileReady(
        runtime.controller(),
        &AccountController::accountProfileReadyForSync);
    runtime.controller()->createAccount(
        QStringLiteral("f03-user"),
        QStringLiteral("correct horse battery staple 884"));

    QTest::qWait(1000);
    qInfo() << "runtime fixture create requests"
            << service.createRequestCount()
            << "mode" << runtime.controller()->mode()
            << "error" << runtime.controller()->lastErrorCode()
            << runtime.controller()->lastErrorMessage();
    QTRY_COMPARE(profileReady.count(), 1);
    QCOMPARE(service.createRequestCount(), 1);

    const auto after = legacy.capture(&error);
    QVERIFY2(after.has_value(), qPrintable(error));
    const auto accountPaths = ProfilePaths::account(
        QString::fromLatin1(kAccountId));
    QVERIFY(accountPaths.has_value());
    const bool sourceStillPresent =
        !after->isEmpty()
        && after->semanticDigest() == before->semanticDigest();
    const bool attachmentReceiptExists =
        QFileInfo::exists(accountPaths->cloudAttachmentReceiptPath());
    QVERIFY2(
        sourceStillPresent && attachmentReceiptExists,
        qPrintable(QStringLiteral(
            "F03 runtime red condition: source_retired=%1 receipt_exists=%2")
            .arg(sourceStillPresent ? QStringLiteral("false")
                                     : QStringLiteral("true"))
            .arg(attachmentReceiptExists ? QStringLiteral("true")
                                          : QStringLiteral("false"))));

    QVERIFY(runtime.controller()->mode() == QStringLiteral("signedIn"));
}

void tst_account_attachment_runtime::
createNewAccountRetiresSourceOnlyAfterFreshExportAbsorption() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    ScopedEnvironmentVariable restoreEndpoint(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");

    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("f03-runtime-complete-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(
        QStringLiteral("Brotherhood-F03"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    LoopbackAccountService service;
    QString error;
    QVERIFY2(service.listen(&error), qPrintable(error));
    service.enableAttachmentCompletion();
    qputenv(
        "COLOSSEUM_ACCOUNT_SERVICE_URL",
        QStringLiteral("http://127.0.0.1:%1")
            .arg(service.port())
            .toLatin1());

    const LegacyPersonalStateStorage legacy =
        LegacyPersonalStateStorage::forCurrentInstallation();
    {
        ProgressStore sourceProgress;
        sourceProgress.record(
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("source-video")},
                {QStringLiteral("kind"), QStringLiteral("video")},
                {QStringLiteral("progress"), 0.42}});
        sourceProgress.flush();
    }
    const auto before = legacy.capture(&error);
    QVERIFY2(before.has_value(), qPrintable(error));
    QVERIFY(!before->isEmpty());

    AccountRuntime runtime;
    QSignalSpy profileReady(
        runtime.controller(),
        &AccountController::accountProfileReadyForSync);
    runtime.controller()->createAccount(
        QStringLiteral("f03-user"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(profileReady.count(), 1);
    QCOMPARE(service.createRequestCount(), 1);

    const auto accountPaths = ProfilePaths::account(
        QString::fromLatin1(kAccountId));
    QVERIFY(accountPaths.has_value());
    const auto receiptPath =
        accountPaths->cloudAttachmentReceiptPath();

    // The source may disappear only after the coordinator has received and
    // semantically checked the sealed fresh export.  This is the end-to-end
    // red proof against a coordinator that retires at promotion time or
    // accepts a malformed export.
    bool completionObserved = false;
    for (int attempt = 0; attempt < 50 && !completionObserved; ++attempt) {
        QTest::qWait(100);
        QString captureError;
        const auto current = legacy.capture(&captureError);
        completionObserved = current.has_value()
            && current->isEmpty()
            && !QFileInfo::exists(receiptPath);
    }
    const auto pending = AccountAttachmentReceipt::read(*accountPaths);
    qInfo() << "completion receipt status"
            << static_cast<int>(pending.status)
            << "phase" << pending.data.retirementPhase
            << "retired" << pending.data.sourceRetired
            << "runtime mode" << runtime.controller()->mode()
            << "sync state" << runtime.controller()->syncState()
            << "error" << runtime.controller()->lastErrorCode()
            << runtime.controller()->lastErrorMessage()
            << "requests" << service.requests()
            << "uploaded" << service.uploadedMutationCount()
            << "manifest" << pending.data.manifest.size();
    QVERIFY(completionObserved);
}

void tst_account_attachment_runtime::
createNewAccountAcceptsConcurrentHistoryMergeAfterCommit() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    ScopedEnvironmentVariable restoreEndpoint(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");

    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("f03-runtime-history-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(
        QStringLiteral("Brotherhood-F03"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    LoopbackAccountService service;
    QString error;
    QVERIFY2(service.listen(&error), qPrintable(error));
    service.enableAttachmentCompletion();
    service.setConcurrentHistoryLastActivity(6000);
    qputenv(
        "COLOSSEUM_ACCOUNT_SERVICE_URL",
        QStringLiteral("http://127.0.0.1:%1")
            .arg(service.port())
            .toLatin1());

    const LegacyPersonalStateStorage legacy =
        LegacyPersonalStateStorage::forCurrentInstallation();
    const qint64 historyBase = QDateTime::currentMSecsSinceEpoch();
    const QString historyId = QStringLiteral("history-source-%1")
        .arg(historyBase);
    service.setConcurrentHistoryLastActivity(historyBase + 200);
    {
        // Tagged legacy installations keep History in the default QSettings
        // backend, matching ProfileStoreRuntime::createLegacyStores().
        HistoryStore source;
        QString historyError;
        QVERIFY2(source.healthy(&historyError), qPrintable(historyError));
        QVERIFY(source.recordActivityRange(
            QStringLiteral("manga"),
            historyId,
            historyBase,
            historyBase + 100));
    }
    const auto before = legacy.capture(&error);
    QVERIFY2(before.has_value(), qPrintable(error));
    QVERIFY(!before->historyRecords.isEmpty());

    AccountRuntime runtime;
    QSignalSpy profileReady(
        runtime.controller(),
        &AccountController::accountProfileReadyForSync);
    runtime.controller()->createAccount(
        QStringLiteral("f03-user"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(profileReady.count(), 1);
    const auto accountPaths = ProfilePaths::account(
        QString::fromLatin1(kAccountId));
    QVERIFY(accountPaths.has_value());
    bool completionObserved = false;
    for (int attempt = 0; attempt < 80 && !completionObserved; ++attempt) {
        QTest::qWait(100);
        QString captureError;
        const auto current = legacy.capture(&captureError);
        completionObserved = current.has_value()
            && current->isEmpty()
            && !QFileInfo::exists(
                accountPaths->cloudAttachmentReceiptPath());
    }
    QVERIFY2(
        completionObserved,
        qPrintable(runtime.controller()->lastErrorMessage()));
    QVERIFY(service.uploadedMutationCount() > 0);
}

void tst_account_attachment_runtime::
createNewAccountAcceptsCertifiedLwwSupersession() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    ScopedEnvironmentVariable restoreEndpoint(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");

    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("f03-runtime-lww-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(
        QStringLiteral("Brotherhood-F03"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    LoopbackAccountService service;
    QString error;
    QVERIFY2(service.listen(&error), qPrintable(error));
    service.enableAttachmentCompletion();
    service.setCertifiedLwwSupersession(true);
    qputenv(
        "COLOSSEUM_ACCOUNT_SERVICE_URL",
        QStringLiteral("http://127.0.0.1:%1")
            .arg(service.port())
            .toLatin1());

    const LegacyPersonalStateStorage legacy =
        LegacyPersonalStateStorage::forCurrentInstallation();
    {
        CollectionStore source(legacy.collectionIniPath());
        QVERIFY(source.add(
            QStringLiteral("Tankoban"),
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("collection-source")},
                {QStringLiteral("type"), QStringLiteral("manga")},
                {QStringLiteral("title"), QStringLiteral("local-winner")},
                {QStringLiteral("addedAt"), qint64(1000)}}));
        source.flush();
    }
    const auto before = legacy.capture(&error);
    QVERIFY2(before.has_value(), qPrintable(error));
    QVERIFY(!before->collectionEntries.isEmpty());

    AccountRuntime runtime;
    QSignalSpy profileReady(
        runtime.controller(),
        &AccountController::accountProfileReadyForSync);
    runtime.controller()->createAccount(
        QStringLiteral("f03-user"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(profileReady.count(), 1);
    const auto accountPaths = ProfilePaths::account(
        QString::fromLatin1(kAccountId));
    QVERIFY(accountPaths.has_value());
    bool completionObserved = false;
    for (int attempt = 0; attempt < 80 && !completionObserved; ++attempt) {
        QTest::qWait(100);
        QString captureError;
        const auto current = legacy.capture(&captureError);
        completionObserved = current.has_value()
            && current->isEmpty()
            && !QFileInfo::exists(
                accountPaths->cloudAttachmentReceiptPath());
    }
    QVERIFY2(
        completionObserved,
        qPrintable(runtime.controller()->lastErrorMessage()));
    QVERIFY(service.uploadedMutationCount() > 0);
}

void tst_account_attachment_runtime::
activitySourceClearRemovesLedgerAfterAttachmentCompletion() {
    ScopedEnvironmentVariable restoreTag("COLOSSEUM_APPDATA_TAG");
    ScopedEnvironmentVariable restoreEndpoint(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");

    QStandardPaths::setTestModeEnabled(true);
    const QByteArray tag = QByteArrayLiteral("f03-runtime-activity-")
        + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("COLOSSEUM_APPDATA_TAG", tag);
    QCoreApplication::setOrganizationName(
        QStringLiteral("Brotherhood-F03"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));

    LoopbackAccountService service;
    QString error;
    QVERIFY2(service.listen(&error), qPrintable(error));
    service.enableAttachmentCompletion();
    qputenv(
        "COLOSSEUM_ACCOUNT_SERVICE_URL",
        QStringLiteral("http://127.0.0.1:%1")
            .arg(service.port())
            .toLatin1());

    const LegacyPersonalStateStorage legacy =
        LegacyPersonalStateStorage::forCurrentInstallation();
    {
        ActivityStore source(legacy.activityDbPath());
        QVERIFY(source.healthy(&error));
        QVERIFY(source.recordPlaybackDelta(QVariantMap{
            {QStringLiteral("sessionId"), QStringLiteral("f03-session")},
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("titleKey"), QStringLiteral("theatre:f03-movie")},
            {QStringLiteral("itemKey"), QStringLiteral("f03-movie")},
            {QStringLiteral("title"), QStringLiteral("F03 Movie")},
            {QStringLiteral("itemLabel"), QString()},
            {QStringLiteral("cover"), QString()},
            {QStringLiteral("utcOffsetMinutes"), qint64(330)},
            {QStringLiteral("syncable"), true},
            {QStringLiteral("source"), QStringLiteral("test")},
            {QStringLiteral("startAtMs"), qint64(1720000000000)},
            {QStringLiteral("endAtMs"), qint64(1720000030000)},
            {QStringLiteral("activeMs"), qint64(30000)},
            {QStringLiteral("rateMilli"), qint64(1000)}}));
        QVERIFY(source.checkpointForSafeCopy(&error));
    }
    QVERIFY(QFileInfo::exists(legacy.activityDbPath()));

    AccountRuntime runtime;
    QSignalSpy profileReady(
        runtime.controller(),
        &AccountController::accountProfileReadyForSync);
    runtime.controller()->createAccount(
        QStringLiteral("f03-user"),
        QStringLiteral("correct horse battery staple 884"));

    QTRY_COMPARE(profileReady.count(), 1);
    const auto accountPaths = ProfilePaths::account(
        QString::fromLatin1(kAccountId));
    QVERIFY(accountPaths.has_value());
    bool completionObserved = false;
    for (int attempt = 0; attempt < 80 && !completionObserved; ++attempt) {
        QTest::qWait(100);
        QString captureError;
        const auto current = legacy.capture(&captureError);
        completionObserved = current.has_value()
            && current->isEmpty()
            && !QFileInfo::exists(legacy.activityDbPath())
            && !QFileInfo::exists(
                accountPaths->cloudAttachmentReceiptPath());
    }
    QVERIFY2(
        completionObserved,
        qPrintable(runtime.controller()->lastErrorMessage()));
}

QTEST_MAIN(tst_account_attachment_runtime)

#include "tst_account_attachment_runtime.moc"
