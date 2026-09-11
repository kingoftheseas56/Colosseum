// PRE-FLIGHT DRAFT STATUS: red integration proof for F03 runtime wiring.

#include "account/AccountAttachmentReceipt.h"
#include "account/AccountRuntime.h"
#include "account/ActivityStore.h"
#include "CollectionStore.h"
#include "account/HistoryStore.h"
#include "account/LegacyPersonalStateStorage.h"
#include "account/ProfilePaths.h"
#include "ProgressStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStandardPaths>
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
};

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
