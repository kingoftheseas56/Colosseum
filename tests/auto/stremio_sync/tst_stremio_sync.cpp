#include "account/ProfilePaths.h"
#include "account/WindowsAccountCredentialStore.h"
#include "stremio/StremioCodec.h"
#include "stremio/StremioState.h"
#include "stremio/StremioSync.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>
#include <utility>

class ScopedEnvironmentVariable final {
public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name),
          m_previous(qgetenv(name)),
          m_hadPrevious(qEnvironmentVariableIsSet(name)) {
        qputenv(m_name, value);
    }

    ~ScopedEnvironmentVariable() {
        if (m_hadPrevious)
            qputenv(m_name, m_previous);
        else
            qunsetenv(m_name);
    }

private:
    const char *m_name;
    QByteArray m_previous;
    bool m_hadPrevious = false;
};

class FixtureStremioApi final {
public:
    FixtureStremioApi() {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                if (!socket)
                    return;
                m_clients.append(socket);
                QObject::connect(socket, &QTcpSocket::readyRead, [this, socket] {
                    m_request += socket->readAll();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listen() {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    QUrl endpoint() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/api").arg(m_server.serverPort()));
    }

    QByteArray request() const { return m_request; }

    void replyIdentity(
        const QString &accountId = QStringLiteral("fixture-account"),
        const QString &displayName = QStringLiteral("Fixture Person")) {
        const QJsonObject payload{{QStringLiteral("result"), QJsonObject{
            {QStringLiteral("_id"), accountId},
            {QStringLiteral("fullname"), displayName}}}};
        const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(body.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
            + body;
        for (const QPointer<QTcpSocket> &candidate : std::as_const(m_clients)) {
            QTcpSocket *socket = candidate.data();
            if (!socket || socket->state() != QAbstractSocket::ConnectedState)
                continue;
            socket->write(response);
            socket->disconnectFromHost();
        }
    }

private:
    QTcpServer m_server;
    QList<QPointer<QTcpSocket>> m_clients;
    QByteArray m_request;
};

bool sendLoopbackCallback(const QUrl &login, const QByteArray &authKey) {
    const QUrl callback(QUrlQuery(login).queryItemValue(QStringLiteral("appCallback")));
    if (!callback.isValid() || callback.host() != QStringLiteral("127.0.0.1")
        || callback.port() <= 0) {
        return false;
    }
    QTcpSocket socket;
    socket.connectToHost(callback.host(), static_cast<quint16>(callback.port()));
    if (!socket.waitForConnected(2000))
        return false;
    const QByteArray target = callback.path(QUrl::FullyEncoded).toUtf8()
        + QByteArrayLiteral("?authKey=")
        + QUrl::toPercentEncoding(QString::fromUtf8(authKey));
    const QByteArray request = QByteArrayLiteral("GET ") + target
        + QByteArrayLiteral(" HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    socket.write(request);
    return socket.waitForBytesWritten(2000);
}

class tst_stremio_sync final : public QObject {
    Q_OBJECT

private slots:
    void callbackRejectsForgedReplayAndAmbiguousCredential();
    void callbackRejectsOversizedRequest();
    void productionEndpointsRequireHttps();
    void journalRejectsMalformedDataWithoutReseeding();
    void profileCredentialTargetsAreTagAndProfileIsolated();
    void intentRemainsPendingUntilRemoteAckAndLocalReceiptPersist();
    void profileReopenWithoutVaultCredentialRequiresReconnectAndSendsNothing();
    void browserCallbackValidatesIdentityAndPersistsVaultBeforeJournal();
    void intentDispatchWaitsForJournalAndReceipt();
    void taggedVaultCredentialRoundTripsOnlyForBoundProfile();
    void replayedAndCancelledCallbacksCannotValidateCredentials();
    void cancelledAndSwitchedProfilesRejectLateIdentityReplies();
    void vaultFailureAfterIdentityValidationLeavesNoCredentialOrJournalIdentity();
    void crashBeforeSendReplaysOnlyAfterJournalRecovery();
    void crashAfterRemoteAcknowledgementDoesNotReplayBeforeReceipt();
    void transientRetryBackoffIsBounded();
    void stateProjectionContainsNoSecretProperty();
};

void tst_stremio_sync::callbackRejectsForgedReplayAndAmbiguousCredential() {
    const QString callbackPath = QStringLiteral("/stremio/nonce-bound-to-attempt");

    const auto forged = StremioCodec::decodeLoopbackCallback(
        QByteArrayLiteral("GET /stremio/other?key=fixture HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"),
        callbackPath);
    QVERIFY(!forged.accepted);

    const auto ambiguous = StremioCodec::decodeLoopbackCallback(
        QByteArrayLiteral("GET /stremio/nonce-bound-to-attempt?key=fixture&authKey=other HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"),
        callbackPath);
    QVERIFY(!ambiguous.accepted);

    const auto accepted = StremioCodec::decodeLoopbackCallback(
        QByteArrayLiteral("GET /stremio/nonce-bound-to-attempt?authKey=fixture-key HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"),
        callbackPath);
    QVERIFY(accepted.accepted);
    QCOMPARE(accepted.authKey, QByteArrayLiteral("fixture-key"));
}

void tst_stremio_sync::callbackRejectsOversizedRequest() {
    const QByteArray oversized = QByteArrayLiteral("GET /stremio/n?key=")
        + QByteArray(17 * 1024, 'x')
        + QByteArrayLiteral(" HTTP/1.1\r\n\r\n");
    const auto result = StremioCodec::decodeLoopbackCallback(
        oversized, QStringLiteral("/stremio/n"));
    QVERIFY(!result.accepted);
}

void tst_stremio_sync::productionEndpointsRequireHttps() {
    QVERIFY(StremioCodec::isProductionEndpoint(QUrl(QStringLiteral("https://api.strem.io/api"))));
    QVERIFY(!StremioCodec::isProductionEndpoint(QUrl(QStringLiteral("http://api.strem.io/api"))));
    QVERIFY(!StremioCodec::isProductionEndpoint(QUrl(QStringLiteral("https://127.0.0.1:39123/api"))));
}

void tst_stremio_sync::journalRejectsMalformedDataWithoutReseeding() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("{not-json"), qint64(9));
    file.close();

    StremioState state;
    QString error;
    QVERIFY(!state.load(path, &error).has_value());
    QCOMPARE(error, QStringLiteral("The Stremio state file is malformed."));

    StremioSync sync;
    QVERIFY(!sync.activateProfile(QStringLiteral("local"), path, false, &error));
    QCOMPARE(sync.status(), QStringLiteral("paused"));
    QCOMPARE(error, QStringLiteral("The Stremio state file is malformed."));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("{not-json"));
    file.close();
}

void tst_stremio_sync::profileCredentialTargetsAreTagAndProfileIsolated() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-target-test"));
    const QString local = WindowsAccountCredentialStore::stremioTargetName(QStringLiteral("local"));
    const QString account = WindowsAccountCredentialStore::stremioTargetName(
        QStringLiteral("11111111-1111-4111-8111-111111111111"));

    QVERIFY(local.contains(QStringLiteral("Tagged.")));
    QVERIFY(local != account);
    QVERIFY(!local.contains(QStringLiteral("stremio-target-test")));
    QVERIFY(!local.contains(QStringLiteral("fixture-auth-key")));
}

void tst_stremio_sync::intentRemainsPendingUntilRemoteAckAndLocalReceiptPersist() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));

    StremioState state;
    StremioPersistentState value;
    value.profileId = QStringLiteral("local");
    value.bindingGeneration = 3;
    value.pendingIntents = {
        StremioPendingIntent{
            QStringLiteral("intent-1"),
            QStringLiteral("fixture"),
            QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}},
            false,
            false,
            0,
            0}
    };

    QSignalSpy committed(&state, &StremioState::persistenceCommitted);
    const quint64 save = state.saveAsync(path, value);
    QTRY_VERIFY(committed.count() == 1);
    QCOMPARE(committed.first().first().toULongLong(), save);

    auto persisted = state.load(path);
    QVERIFY(persisted.has_value());
    QCOMPARE(persisted->pendingIntents.size(), 1);
    QVERIFY(!persisted->pendingIntents.first().remoteAcknowledged);
    QVERIFY(!persisted->pendingIntents.first().localReceiptDurable);
}

void tst_stremio_sync::profileReopenWithoutVaultCredentialRequiresReconnectAndSendsNothing() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));

    StremioPersistentState persisted;
    persisted.profileId = QStringLiteral("local");
    persisted.bindingGeneration = 1;
    persisted.accountId = QStringLiteral("fixture-account");
    persisted.displayName = QStringLiteral("Fixture Person");
    persisted.pendingIntents = {
        StremioPendingIntent{
            QStringLiteral("pending-no-vault"),
            QStringLiteral("fixture"),
            QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}},
            false,
            false,
            0,
            0}
    };
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, persisted);
        QTRY_COMPARE(committed.count(), 1);
    }

    int sends = 0;
    StremioSyncOptions options;
    options.intentSender = [&sends](const StremioPendingIntent &, std::function<void(bool, bool)>) {
        ++sends;
    };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QCOMPARE(sync.status(), QStringLiteral("reconnectRequired"));
    sync.retryPendingNow();
    QCOMPARE(sends, 0);
}

void tst_stremio_sync::browserCallbackValidatesIdentityAndPersistsVaultBeforeJournal() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-auth-fixture"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));

    QUrl openedLogin;
    int vaultWrites = 0;
    bool vaultArgumentsValid = false;
    bool journalWasUnidentifiedAtVaultWrite = false;
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.browserOpener = [&openedLogin](const QUrl &url) { openedLogin = url; };
    options.saveCredential = [&](const QString &profileId, const QString &accountId, const QByteArray &authKey) {
        ++vaultWrites;
        vaultArgumentsValid = profileId == QStringLiteral("local")
            && accountId == QStringLiteral("fixture-account")
            && authKey == QByteArrayLiteral("fixture-auth-key");
        StremioState inspector;
        const auto beforeJournal = inspector.load(path);
        journalWasUnidentifiedAtVaultWrite = beforeJournal.has_value()
            && beforeJournal->accountId.isEmpty();
        return true;
    };
    options.clearCredential = [](const QString &) { return true; };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(openedLogin.isValid());
    QVERIFY(sendLoopbackCallback(openedLogin, QByteArrayLiteral("fixture-auth-key")));
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("\"authKey\":\"fixture-auth-key\"")));
    QCOMPARE(vaultWrites, 0);

    api.replyIdentity();
    QTRY_COMPARE(vaultWrites, 1);
    QVERIFY(vaultArgumentsValid);
    QVERIFY(journalWasUnidentifiedAtVaultWrite);
    QTRY_COMPARE(sync.status(), QStringLiteral("synced"));

    StremioState inspector;
    QTRY_VERIFY(inspector.load(path).has_value());
    const auto persisted = inspector.load(path);
    QVERIFY(persisted.has_value());
    QCOMPARE(persisted->accountId, QStringLiteral("fixture-account"));
    QFile journal(path);
    QVERIFY(journal.open(QIODevice::ReadOnly));
    QVERIFY(!journal.readAll().contains(QByteArrayLiteral("fixture-auth-key")));
}

void tst_stremio_sync::intentDispatchWaitsForJournalAndReceipt() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));

    StremioPersistentState existing;
    existing.profileId = QStringLiteral("local");
    existing.bindingGeneration = 1;
    existing.accountId = QStringLiteral("fixture-account");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    int sends = 0;
    bool intentWasDurableBeforeSend = false;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    options.intentSender = [&](const StremioPendingIntent &intent, std::function<void(bool, bool)> complete) {
        ++sends;
        StremioState inspector;
        const auto persisted = inspector.load(path);
        intentWasDurableBeforeSend = persisted.has_value()
            && std::any_of(
                persisted->pendingIntents.cbegin(),
                persisted->pendingIntents.cend(),
                [&intent](const StremioPendingIntent &candidate) {
                    return candidate.operationId == intent.operationId
                        && !candidate.remoteAcknowledged
                        && !candidate.localReceiptDurable;
                });
        complete(true, false);
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QString operationId;
    QVERIFY(sync.queueIntent(
        QStringLiteral("library-membership"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}},
        &operationId));
    QTRY_COMPARE(sends, 1);
    QVERIFY(intentWasDurableBeforeSend);
    QTRY_COMPARE(sync.pendingCount(), 1);

    QVERIFY(sync.acknowledgeLocalReceipt(operationId));
    QTRY_COMPARE(sync.pendingCount(), 0);

    StremioState inspector;
    QTRY_VERIFY([&] {
        const auto persisted = inspector.load(path);
        return persisted.has_value() && persisted->pendingIntents.isEmpty();
    }());
}

void tst_stremio_sync::taggedVaultCredentialRoundTripsOnlyForBoundProfile() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-vault-roundtrip"));
    WindowsAccountCredentialStore store;
    QVERIFY(store.isAvailable());
    const QString profileId = QStringLiteral("local");
    QVERIFY(store.clearStremio(profileId));
    struct Cleanup final {
        WindowsAccountCredentialStore &store;
        QString profileId;
        ~Cleanup() { store.clearStremio(profileId); }
    } cleanup{store, profileId};

    const StoredStremioCredential credential{
        profileId,
        QStringLiteral("fixture-account"),
        QByteArrayLiteral("fixture-vault-key")};
    QVERIFY(store.saveStremio(credential));
    const auto loaded = store.loadStremio(profileId, credential.accountId);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->profileId, credential.profileId);
    QCOMPARE(loaded->accountId, credential.accountId);
    QCOMPARE(loaded->authKey, credential.authKey);
    QVERIFY(!store.loadStremio(profileId, QStringLiteral("other-account")).has_value());
}

void tst_stremio_sync::replayedAndCancelledCallbacksCannotValidateCredentials() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-replay-cancel"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));

    QUrl login;
    int vaultWrites = 0;
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.browserOpener = [&login](const QUrl &url) { login = url; };
    options.saveCredential = [&vaultWrites](const QString &, const QString &, const QByteArray &) {
        ++vaultWrites;
        return true;
    };
    options.clearCredential = [](const QString &) { return true; };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("fixture-auth-key")));
    QTRY_VERIFY(!api.request().isEmpty());
    QVERIFY(!sendLoopbackCallback(login, QByteArrayLiteral("fixture-auth-key")));

    sync.cancelAuthentication();
    api.replyIdentity();
    QTest::qWait(100);
    QCOMPARE(vaultWrites, 0);

    QVERIFY(sync.startBrowserAuthentication());
    const QUrl cancelledLogin = login;
    sync.cancelAuthentication();
    QVERIFY(!sendLoopbackCallback(cancelledLogin, QByteArrayLiteral("fixture-auth-key")));
}

void tst_stremio_sync::cancelledAndSwitchedProfilesRejectLateIdentityReplies() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-late-identity"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString pathA = QDir(temp.path()).filePath(QStringLiteral("a/stremio-sync.json"));
    const QString pathB = QDir(temp.path()).filePath(QStringLiteral("b/stremio-sync.json"));

    QUrl login;
    QList<QString> savedProfiles;
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.browserOpener = [&login](const QUrl &url) { login = url; };
    options.saveCredential = [&savedProfiles](const QString &profileId, const QString &, const QByteArray &) {
        savedProfiles.append(profileId);
        return true;
    };
    options.clearCredential = [](const QString &) { return true; };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), pathA, false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("fixture-auth-key")));
    QTRY_VERIFY(!api.request().isEmpty());
    sync.cancelAuthentication();
    api.replyIdentity();
    QTest::qWait(100);
    QVERIFY(savedProfiles.isEmpty());

    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("fixture-auth-key")));
    const qsizetype firstRequestBytes = api.request().size();
    QTRY_VERIFY(api.request().size() > firstRequestBytes);
    const QString profileB = QStringLiteral("11111111-1111-4111-8111-111111111111");
    QVERIFY(sync.activateProfile(profileB, pathB, false));
    QCOMPARE(sync.activeProfileId(), profileB);
    api.replyIdentity();
    QTest::qWait(100);
    QVERIFY(savedProfiles.isEmpty());
    QCOMPARE(sync.status(), QStringLiteral("notConnected"));
}

void tst_stremio_sync::vaultFailureAfterIdentityValidationLeavesNoCredentialOrJournalIdentity() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-vault-failure"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));

    QUrl login;
    int clears = 0;
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.browserOpener = [&login](const QUrl &url) { login = url; };
    options.saveCredential = [](const QString &, const QString &, const QByteArray &) { return false; };
    options.clearCredential = [&clears](const QString &) {
        ++clears;
        return true;
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("fixture-auth-key")));
    QTRY_VERIFY(!api.request().isEmpty());
    api.replyIdentity();
    QTRY_COMPARE(clears, 1);
    QTRY_COMPARE(sync.status(), QStringLiteral("notConnected"));

    StremioState inspector;
    const auto persisted = inspector.load(path);
    QVERIFY(persisted.has_value());
    QVERIFY(persisted->accountId.isEmpty());
    QVERIFY(persisted->displayName.isEmpty());
}

void tst_stremio_sync::crashBeforeSendReplaysOnlyAfterJournalRecovery() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState persisted;
    persisted.profileId = QStringLiteral("local");
    persisted.bindingGeneration = 1;
    persisted.accountId = QStringLiteral("fixture-account");
    persisted.pendingIntents = {StremioPendingIntent{
        QStringLiteral("crash-before-send"),
        QStringLiteral("fixture"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}},
        false,
        false,
        0,
        0}};
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, persisted);
        QTRY_COMPARE(committed.count(), 1);
    }

    StremioSyncOptions recoveredOptions;
    recoveredOptions.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    int recoveredSends = 0;
    recoveredOptions.intentSender = [&recoveredSends](const StremioPendingIntent &, std::function<void(bool, bool)>) {
        ++recoveredSends;
    };
    StremioSync recovered(recoveredOptions);
    QVERIFY(recovered.activateProfile(QStringLiteral("local"), path, false));
    recovered.retryPendingNow();
    QCOMPARE(recoveredSends, 1);
}

void tst_stremio_sync::crashAfterRemoteAcknowledgementDoesNotReplayBeforeReceipt() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState persisted;
    persisted.profileId = QStringLiteral("local");
    persisted.bindingGeneration = 1;
    persisted.accountId = QStringLiteral("fixture-account");
    persisted.pendingIntents = {StremioPendingIntent{
        QStringLiteral("crash-after-ack"),
        QStringLiteral("fixture"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}},
        true,
        false,
        0,
        0}};
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, persisted);
        QTRY_COMPARE(committed.count(), 1);
    }

    int sends = 0;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    options.intentSender = [&sends](const StremioPendingIntent &, std::function<void(bool, bool)>) {
        ++sends;
    };
    StremioSync recovered(options);
    QVERIFY(recovered.activateProfile(QStringLiteral("local"), path, false));
    recovered.retryPendingNow();
    QCOMPARE(sends, 0);
    QVERIFY(recovered.acknowledgeLocalReceipt(QStringLiteral("crash-after-ack")));
    QTRY_COMPARE(recovered.pendingCount(), 0);
}

void tst_stremio_sync::transientRetryBackoffIsBounded() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("local");
    existing.bindingGeneration = 1;
    existing.accountId = QStringLiteral("fixture-account");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    qint64 now = 100;
    int sends = 0;
    StremioSyncOptions options;
    options.clock = [&now] { return now; };
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    options.intentSender = [&sends](const StremioPendingIntent &, std::function<void(bool, bool)> complete) {
        ++sends;
        complete(false, false);
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(sync.queueIntent(
        QStringLiteral("fixture"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}}));
    QTRY_COMPARE(sends, 1);
    sync.retryPendingNow();
    QCOMPARE(sends, 1);

    for (const qint64 retryTime : {qint64(2100), qint64(6100), qint64(14100), qint64(30100)}) {
        now = retryTime;
        sync.retryPendingNow();
    }
    QCOMPARE(sends, 5);
    QCOMPARE(sync.status(), QStringLiteral("syncFailed"));
    now = 999999;
    sync.retryPendingNow();
    QCOMPARE(sends, 5);

    StremioState inspector;
    const auto fifthAttemptPersisted = [&inspector, &path] {
        const auto candidate = inspector.load(path);
        return candidate.has_value()
            && candidate->pendingIntents.size() == 1
            && candidate->pendingIntents.first().attempts == 5;
    };
    QTRY_VERIFY_WITH_TIMEOUT(fifthAttemptPersisted(), 10000);
    const auto persisted = inspector.load(path);
    QVERIFY(persisted.has_value());
    QCOMPARE(persisted->pendingIntents.size(), 1);
    QCOMPARE(persisted->pendingIntents.first().attempts, 5);
}

void tst_stremio_sync::stateProjectionContainsNoSecretProperty() {
    StremioSync sync;
    const QMetaObject *metaObject = sync.metaObject();
    QStringList names;
    for (int index = 0; index < metaObject->propertyCount(); ++index)
        names.append(QString::fromLatin1(metaObject->property(index).name()).toLower());
    QVERIFY(names.contains(QStringLiteral("status")));
    QVERIFY(names.contains(QStringLiteral("pendingcount")));
    QVERIFY(names.contains(QStringLiteral("lastsuccessat")));
    QVERIFY(names.contains(QStringLiteral("activeprofileid")));
    QVERIFY(names.contains(QStringLiteral("mergecomplete")));
    QVERIFY(names.contains(QStringLiteral("completedrun")));
    for (const QString &name : std::as_const(names)) {
        QVERIFY(!name.contains(QStringLiteral("auth")));
        QVERIFY(!name.contains(QStringLiteral("secret")));
        QVERIFY(!name.contains(QStringLiteral("token")));
        QVERIFY(!name.contains(QStringLiteral("url")));
    }
}

QTEST_MAIN(tst_stremio_sync)
#include "tst_stremio_sync.moc"
