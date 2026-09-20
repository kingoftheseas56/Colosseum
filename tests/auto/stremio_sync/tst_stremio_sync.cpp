#include "account/ProfilePaths.h"
#include "account/WindowsAccountCredentialStore.h"
#include "stremio/StremioCodec.h"
#include "stremio/StremioState.h"
#include "stremio/StremioSync.h"

#include <QDir>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

    void replyRaw(const QByteArray &response) {
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

class BrowserLoginCapture final : public QObject {
    Q_OBJECT

public:
    QUrl openedUrl;

public slots:
    void opened(const QUrl &url) { openedUrl = url; }
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

bool sendRawLoopbackCallback(const QUrl &login, const QByteArray &request) {
    const QUrl callback(QUrlQuery(login).queryItemValue(QStringLiteral("appCallback")));
    if (!callback.isValid() || callback.host() != QStringLiteral("127.0.0.1")
        || callback.port() <= 0) {
        return false;
    }
    QTcpSocket socket;
    socket.connectToHost(callback.host(), static_cast<quint16>(callback.port()));
    if (!socket.waitForConnected(2000))
        return false;
    socket.write(request);
    if (!socket.waitForBytesWritten(2000))
        return false;
    QElapsedTimer elapsed;
    elapsed.start();
    while (socket.state() != QAbstractSocket::UnconnectedState && elapsed.elapsed() < 2000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return socket.state() == QAbstractSocket::UnconnectedState;
}

#ifdef Q_OS_WIN
class ScopedExclusiveFileLock final {
public:
    ~ScopedExclusiveFileLock() {
        unlock();
    }

    bool lock(const QString &path) {
        m_handle = CreateFileW(
            reinterpret_cast<LPCWSTR>(path.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        return m_handle != INVALID_HANDLE_VALUE;
    }

    void unlock() {
        if (m_handle == INVALID_HANDLE_VALUE)
            return;
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};
#endif

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
    void markerArrivalResumesRecoveredDurableIntent();
    void markerArrivalRearmsRecoveredBackoff();
    void crashAfterRemoteAcknowledgementDoesNotReplayBeforeReceipt();
    void transientRetryBackoffIsBounded();
    void dispatchWaitsForDurableIntentAndKeepsOneInFlightSend();
    void rapidProfileReturnRetainsPendingIntentsAcrossAsyncWrites();
    void returnedProfileWaitsForPriorJournalReceiptBeforeDispatch();
    void returnedProfilePausesWhenPriorJournalWriteFails();
    void reconnectRejectsDifferentStremioAccountForExistingProfile();
    void authenticationFailureDisablesLaterIntentDispatch();
    void cancelledAuthPersistenceCannotCompleteReplacementAttempt();
    void cancelledProvisionalCredentialCannotDispatchQueuedIntent();
    void authenticationRejectionRemainsFencedAcrossProfileReturn();
    void authenticationRejectionRemainsFencedAfterRestart();
    void markerOnlyProfileRequiresReconnectAndNeverDispatches();
    void markerArrivalAfterActivationUpdatesConnectionState();
    void markerWithMatchingCredentialActivatesSynced();
    void productionEndpointRejectsUnrelatedHttpsOrigin();
    void identityRedirectCannotValidateCredential();
    void oversizedIdentityResponseCannotValidateCredential();
    void defaultBrowserOpenerUsesOfficialLoginUrl();
    void progressIntentCoalescesOnlyUnsentReplaceableWork();
    void oversizedSocketRequestDoesNotPoisonNextCallback();
    void exactTaggedFixtureProjectsOnlySanitizedTerminalState();
    void stateProjectionContainsNoSecretProperty();
    void libraryItemCodecBatchesPreservesPatchAndSeparatesMembership();
    void libraryPullBatchesBoundedRowsAndIsolatesMalformedProviderData();
    void explicitLibraryRemovalReadsFreshProviderRowAndPreservesFields();
    void progressIntentReadsFreshProviderRowAndUsesMilliseconds();
    void staleProviderProgressDoesNotOverwriteNewerActivity();
    void equalProviderActivityPreservesCurrentStateWithoutPut();
    void canonicalTheatreReconcileJournalsOnlySemanticProviderDifference();
    void reconcileProgressKeepsExactEpisodeAndSeparateLibraryRoot();
    void reconciledOwnerIntentCarriesItsDurableLocalReceipt();
    void libraryAddAndWatchedPatchesReadBackBeforeBaselineSettlement();
    void datastoreBusyDefersSecondDurableIntentWithoutRetryPenalty();
    void olderProgressAcknowledgementCannotClearNewerGeneration();
    void explicitReaddClearsOnlyLocalMembershipSuppression();
    void remoteRemovalDifferenceSurvivesRestartWithoutLibraryAdd();
    void durableMembershipTransitionsRetireBaselineAndPermitExplicitReadd();
    void watchedEpisodeCodecIsBoundedAnchoredAndStable();
    void seriesWatchedSenderPreservesRemoteBitsAndReadsBack();
    void seriesReconcileQueuesOnlyResolvedExactEpisodes();
    void theatreProjectionKeepsOpaqueEpisodeIdentityAndConvertsMilliseconds();
    void episodeMetadataBridgeBindsOneShotRepliesToActiveProfile();
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

    // Task 2 membership suppression is a durable safety fence. A partial
    // entry must pause the profile rather than turn an arbitrary journal row
    // into a passive provider delete/re-add decision.
    StremioPersistentState valid;
    valid.profileId = QStringLiteral("local");
    valid.bindingGeneration = 1;
    QJsonObject malformedDifference = StremioState::encode(valid);
    malformedDifference.insert(
        QStringLiteral("intentionalMembershipDifferences"),
        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("tt100")}}});
    QVERIFY(!StremioState::decode(malformedDifference, &error).has_value());
    QCOMPARE(error, QStringLiteral("The Stremio state file is malformed."));
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

void tst_stremio_sync::markerArrivalResumesRecoveredDurableIntent() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState persisted;
    persisted.profileId = QStringLiteral("local");
    persisted.bindingGeneration = 1;
    persisted.accountId = QStringLiteral("fixture-account");
    persisted.pendingIntents = {StremioPendingIntent{
        QStringLiteral("recovered-durable-intent"),
        QStringLiteral("fixture"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}},
        false,
        true,
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
    options.intentSender = [&sends](const StremioPendingIntent &,
                                    std::function<void(bool, bool)> complete) {
        ++sends;
        complete(true, false);
    };
    StremioSync recovered(options);
    QVERIFY(recovered.activateProfile(QStringLiteral("local"), path, false));
    QCOMPARE(sends, 0);

    // Linking restores a valid profile binding and must resume only its
    // already-durable work. Removing the resume call from marker activation
    // leaves this intent stranded after an application restart.
    recovered.setMarkerLinked(true);
    QTRY_COMPARE(sends, 1);
    QTRY_COMPARE(recovered.pendingCount(), 0);
}

void tst_stremio_sync::markerArrivalRearmsRecoveredBackoff() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState persisted;
    persisted.profileId = QStringLiteral("local");
    persisted.bindingGeneration = 1;
    persisted.accountId = QStringLiteral("fixture-account");
    persisted.pendingIntents = {StremioPendingIntent{
        QStringLiteral("recovered-backoff-intent"),
        QStringLiteral("fixture"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-2")}},
        false,
        true,
        1,
        QDateTime::currentMSecsSinceEpoch() + 100}};
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
    options.intentSender = [&sends](const StremioPendingIntent &,
                                    std::function<void(bool, bool)> complete) {
        ++sends;
        complete(true, false);
    };
    StremioSync recovered(options);
    QVERIFY(recovered.activateProfile(QStringLiteral("local"), path, false));
    recovered.setMarkerLinked(true);

    // A restart between a failed attempt and its deadline must rearm the
    // bounded journal retry. Removing the post-marker retry scheduling keeps
    // this durable work indefinitely pending.
    QTRY_COMPARE(sends, 1);
    QTRY_COMPARE(recovered.pendingCount(), 0);
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
    StremioState inspector;
    const auto persistedAttempts = [&inspector, &path](int expectedAttempts) {
        const auto candidate = inspector.load(path);
        return candidate.has_value()
            && candidate->pendingIntents.size() == 1
            && candidate->pendingIntents.first().attempts == expectedAttempts;
    };
    QTRY_VERIFY(persistedAttempts(1));
    sync.retryPendingNow();
    QCOMPARE(sends, 1);

    int expectedAttempts = 1;
    for (const qint64 retryTime : {qint64(2100), qint64(6100), qint64(14100), qint64(30100)}) {
        now = retryTime;
        sync.retryPendingNow();
        ++expectedAttempts;
        // Each retry result must commit before the next retry becomes eligible.
        // Observe the real durable receipt, rather than letting this clock-only
        // fixture bypass the dispatch fence.
        QTRY_VERIFY(persistedAttempts(expectedAttempts));
    }
    QCOMPARE(sends, 5);
    QCOMPARE(sync.status(), QStringLiteral("syncFailed"));
    now = 999999;
    sync.retryPendingNow();
    QCOMPARE(sends, 5);

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

void tst_stremio_sync::dispatchWaitsForDurableIntentAndKeepsOneInFlightSend() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("local");
    existing.accountId = QStringLiteral("fixture-account");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    int sends = 0;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &) -> std::optional<QByteArray> {
        return QByteArrayLiteral("fixture-vault-key");
    };
    options.intentSender = [&sends](const StremioPendingIntent &, std::function<void(bool, bool)>) {
        ++sends;
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}}));
    sync.retryPendingNow();
    QCOMPARE(sends, 0);
    QTRY_COMPARE(sends, 1);
    sync.retryPendingNow();
    QCOMPARE(sends, 1);
}

void tst_stremio_sync::rapidProfileReturnRetainsPendingIntentsAcrossAsyncWrites() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString pathA = QDir(temp.path()).filePath(QStringLiteral("a/stremio-sync.json"));
    const QString pathB = QDir(temp.path()).filePath(QStringLiteral("b/stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("profile-a");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(pathA, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), pathA, false));
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("a-first")}}));
    QVERIFY(sync.activateProfile(QStringLiteral("profile-b"), pathB, false));
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("b-only")}}));
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), pathA, false));
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("a-second")}}));

    StremioState inspector;
    QTRY_VERIFY([&] {
        const auto persisted = inspector.load(pathA);
        return persisted.has_value() && persisted->pendingIntents.size() == 2;
    }());
    const auto persistedA = inspector.load(pathA);
    QVERIFY(persistedA.has_value());
    QCOMPARE(persistedA->pendingIntents.at(0).desired.value(QStringLiteral("id")).toString(), QStringLiteral("a-first"));
    QCOMPARE(persistedA->pendingIntents.at(1).desired.value(QStringLiteral("id")).toString(), QStringLiteral("a-second"));
}

void tst_stremio_sync::returnedProfileWaitsForPriorJournalReceiptBeforeDispatch() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString pathA = QDir(temp.path()).filePath(QStringLiteral("a/stremio-sync.json"));
    const QString pathB = QDir(temp.path()).filePath(QStringLiteral("b/stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("profile-a");
    existing.accountId = QStringLiteral("fixture-account");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(pathA, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    int sends = 0;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &) -> std::optional<QByteArray> {
        return QByteArrayLiteral("fixture-vault-key");
    };
    options.intentSender = [&sends](const StremioPendingIntent &, std::function<void(bool, bool)>) {
        ++sends;
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), pathA, false));
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("a-return")}}));
    QVERIFY(sync.activateProfile(QStringLiteral("profile-b"), pathB, false));
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), pathA, false));

    // The prior A write belongs to the old binding, but this reactivated A has
    // adopted its snapshot. A retry cannot send until that snapshot's receipt.
    sync.retryPendingNow();
    QCOMPARE(sends, 0);
    QTRY_COMPARE(sends, 1);
}

void tst_stremio_sync::returnedProfilePausesWhenPriorJournalWriteFails() {
#ifndef Q_OS_WIN
    QSKIP("The real QSaveFile replacement failure is exercised with a Windows exclusive handle.");
#else
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString pathA = QDir(temp.path()).filePath(QStringLiteral("a/stremio-sync.json"));
    const QString pathB = QDir(temp.path()).filePath(QStringLiteral("b/stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("profile-a");
    existing.accountId = QStringLiteral("fixture-account");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(pathA, existing);
        QTRY_COMPARE(committed.count(), 1);
    }
    int sends = 0;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &) -> std::optional<QByteArray> {
        return QByteArrayLiteral("fixture-vault-key");
    };
    options.intentSender = [&sends](const StremioPendingIntent &, std::function<void(bool, bool)>) {
        ++sends;
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), pathA, false));
    ScopedExclusiveFileLock lock;
    QVERIFY(lock.lock(pathA));
    StremioState *state = sync.findChild<StremioState *>(QStringLiteral("stremioState"));
    QVERIFY(state);
    QSignalSpy failed(state, &StremioState::persistenceFailed);
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("a-failed-return")}}));
    QVERIFY(sync.activateProfile(QStringLiteral("profile-b"), pathB, false));
    QTRY_COMPARE(failed.count(), 1);
    lock.unlock();
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), pathA, false));

    sync.retryPendingNow();
    QCOMPARE(sends, 0);
    QTRY_COMPARE(sync.status(), QStringLiteral("paused"));
    QCOMPARE(sends, 0);
#endif
}

void tst_stremio_sync::reconnectRejectsDifferentStremioAccountForExistingProfile() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-account-switch"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("local");
    existing.accountId = QStringLiteral("account-a");
    existing.pendingIntents = {StremioPendingIntent{QStringLiteral("a-pending"), QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-a")}}}};
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    QUrl login;
    int writes = 0;
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.browserOpener = [&login](const QUrl &url) { login = url; };
    options.saveCredential = [&writes](const QString &, const QString &, const QByteArray &) { ++writes; return true; };
    options.loadCredential = [](const QString &, const QString &) -> std::optional<QByteArray> {
        return QByteArrayLiteral("account-a-key");
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("account-b-key")));
    QTRY_VERIFY(!api.request().isEmpty());
    api.replyIdentity(QStringLiteral("account-b"));
    QTRY_COMPARE(sync.status(), QStringLiteral("reconnectRequired"));
    QCOMPARE(writes, 0);
    StremioState inspector;
    const auto persisted = inspector.load(path);
    QVERIFY(persisted.has_value());
    QCOMPARE(persisted->accountId, QStringLiteral("account-a"));
    QCOMPARE(persisted->pendingIntents.size(), 1);
}

void tst_stremio_sync::authenticationFailureDisablesLaterIntentDispatch() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("local");
    existing.accountId = QStringLiteral("fixture-account");
    existing.pendingIntents = {StremioPendingIntent{QStringLiteral("first"), QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}}}};
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    int sends = 0;
    std::function<void(bool, bool)> completion;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &) -> std::optional<QByteArray> {
        return QByteArrayLiteral("fixture-vault-key");
    };
    options.intentSender = [&](const StremioPendingIntent &, std::function<void(bool, bool)> complete) {
        ++sends;
        completion = std::move(complete);
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.retryPendingNow();
    QCOMPARE(sends, 1);
    QVERIFY(completion);
    completion(false, true);
    QCOMPARE(sync.status(), QStringLiteral("reconnectRequired"));
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-2")}}));
    sync.retryPendingNow();
    QCOMPARE(sends, 1);
}

void tst_stremio_sync::authenticationRejectionRemainsFencedAcrossProfileReturn() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString pathA = QDir(temp.path()).filePath(QStringLiteral("a/stremio-sync.json"));
    const QString pathB = QDir(temp.path()).filePath(QStringLiteral("b/stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("profile-a");
    existing.accountId = QStringLiteral("fixture-account");
    existing.pendingIntents = {StremioPendingIntent{
        QStringLiteral("pending-a"),
        QStringLiteral("library"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-a")}}}};
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(pathA, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    QHash<QString, QByteArray> vault{{QStringLiteral("profile-a"), QByteArrayLiteral("fixture-vault-key")}};
    int sends = 0;
    std::function<void(bool, bool)> completion;
    StremioSyncOptions options;
    options.loadCredential = [&vault](const QString &profileId, const QString &) -> std::optional<QByteArray> {
        const auto key = vault.constFind(profileId);
        return key == vault.cend() ? std::nullopt : std::optional<QByteArray>(*key);
    };
    options.clearCredential = [&vault](const QString &profileId) {
        vault.remove(profileId);
        return true;
    };
    options.intentSender = [&sends, &completion](const StremioPendingIntent &, std::function<void(bool, bool)> complete) {
        ++sends;
        completion = std::move(complete);
    };

    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), pathA, false));
    sync.retryPendingNow();
    QCOMPARE(sends, 1);
    QVERIFY(completion);
    completion(false, true);
    QCOMPARE(sync.status(), QStringLiteral("reconnectRequired"));

    QVERIFY(sync.activateProfile(QStringLiteral("profile-b"), pathB, false));
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), pathA, false));
    sync.setMarkerLinked(true);
    sync.retryPendingNow();
    QCOMPARE(sync.status(), QStringLiteral("reconnectRequired"));
    QCOMPARE(sends, 1);
}

void tst_stremio_sync::authenticationRejectionRemainsFencedAfterRestart() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("profile-a");
    existing.accountId = QStringLiteral("fixture-account");
    existing.pendingIntents = {StremioPendingIntent{
        QStringLiteral("pending-a"),
        QStringLiteral("library"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-a")}}}};
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, existing);
        QTRY_COMPARE(committed.count(), 1);
    }

    int sends = 0;
    std::function<void(bool, bool)> completion;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &) -> std::optional<QByteArray> {
        return QByteArrayLiteral("vault-key-that-could-not-be-deleted");
    };
    options.clearCredential = [](const QString &) {
        return false;
    };
    options.intentSender = [&sends, &completion](const StremioPendingIntent &, std::function<void(bool, bool)> complete) {
        ++sends;
        completion = std::move(complete);
    };

    {
        StremioSync sync(options);
        QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), path, false));
        sync.retryPendingNow();
        QCOMPARE(sends, 1);
        QVERIFY(completion);
        completion(false, true);
        QTRY_COMPARE(sync.status(), QStringLiteral("reconnectRequired"));
        StremioState inspector;
        QTRY_VERIFY([&] {
            const auto persisted = inspector.load(path);
            return persisted.has_value()
                && persisted->reconnectRequired
                && persisted->pendingIntents.size() == 1
                && persisted->pendingIntents.first().attempts == 0;
        }());
    }

    StremioSync recovered(options);
    QVERIFY(recovered.activateProfile(QStringLiteral("profile-a"), path, false));
    recovered.setMarkerLinked(true);
    recovered.retryPendingNow();
    QCOMPARE(recovered.status(), QStringLiteral("reconnectRequired"));
    QCOMPARE(sends, 1);
}

void tst_stremio_sync::cancelledAuthPersistenceCannotCompleteReplacementAttempt() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-auth-attempt-fence"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        StremioPersistentState empty;
        empty.profileId = QStringLiteral("local");
        writer.saveAsync(path, empty);
        QTRY_COMPARE(committed.count(), 1);
    }

    QUrl login;
    int vaultWrites = 0;
    StremioSync *syncPointer = nullptr;
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.browserOpener = [&login](const QUrl &url) { login = url; };
    options.saveCredential = [&vaultWrites, &syncPointer](const QString &, const QString &, const QByteArray &) {
        ++vaultWrites;
        // getUser has returned and the credential has passed the real vault boundary,
        // but the queued journal receipt belongs to this now-cancelled attempt.
        syncPointer->cancelAuthentication();
        return true;
    };

    StremioSync sync(options);
    syncPointer = &sync;
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("first-attempt-key")));
    QTRY_VERIFY(!api.request().isEmpty());
    api.replyIdentity();
    QTRY_COMPARE(vaultWrites, 1);
    QVERIFY(sync.startBrowserAuthentication());
    StremioState *state = sync.findChild<StremioState *>(QStringLiteral("stremioState"));
    QVERIFY(state);
    QString writeError;
    QVERIFY(state->flush(&writeError));
    QFile persisted(path);
    QVERIFY(persisted.open(QIODevice::ReadOnly));
    QVERIFY(!persisted.readAll().contains(QByteArrayLiteral("\"accountId\":\"fixture-account\"")));
    QCOMPARE(sync.status(), QStringLiteral("connecting"));
    QCOMPARE(sync.completedRun(), quint64(0));
}

void tst_stremio_sync::cancelledProvisionalCredentialCannotDispatchQueuedIntent() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-provisional-cancel"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));

    QUrl login;
    int vaultWrites = 0;
    int sends = 0;
    StremioSync *syncPointer = nullptr;
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.browserOpener = [&login](const QUrl &url) { login = url; };
    options.saveCredential = [&vaultWrites, &syncPointer](const QString &, const QString &, const QByteArray &) {
        ++vaultWrites;
        QTimer::singleShot(0, syncPointer, [syncPointer] {
            syncPointer->cancelAuthentication();
        });
        return true;
    };
    options.intentSender = [&sends](const StremioPendingIntent &, std::function<void(bool, bool)>) {
        ++sends;
    };

    StremioSync sync(options);
    syncPointer = &sync;
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("cancelled-auth-work")}}));
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("provisional-key")));
    QTRY_VERIFY(!api.request().isEmpty());
    api.replyIdentity();
    QTRY_COMPARE(vaultWrites, 1);

    StremioState *state = sync.findChild<StremioState *>(QStringLiteral("stremioState"));
    QVERIFY(state);
    QString writeError;
    QVERIFY(state->flush(&writeError));
    QTRY_VERIFY([&] {
        const auto persisted = state->load(path);
        return persisted.has_value() && persisted->pendingIntents.size() == 1;
    }());
    QCOMPARE(sends, 0);
    QCOMPARE(sync.completedRun(), quint64(0));
}

void tst_stremio_sync::markerOnlyProfileRequiresReconnectAndNeverDispatches() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    int sends = 0;
    StremioSyncOptions options;
    options.intentSender = [&sends](const StremioPendingIntent &, std::function<void(bool, bool)>) {
        ++sends;
    };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")), false));
    sync.setMarkerLinked(true);
    QCOMPARE(sync.status(), QStringLiteral("reconnectRequired"));
    QVERIFY(sync.queueIntent(QStringLiteral("library"), QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}}));
    sync.retryPendingNow();
    QCOMPARE(sends, 0);
}

void tst_stremio_sync::markerArrivalAfterActivationUpdatesConnectionState() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("local"), QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")), false));
    QCOMPARE(sync.status(), QStringLiteral("notConnected"));
    sync.setMarkerLinked(true);
    QCOMPARE(sync.status(), QStringLiteral("reconnectRequired"));
}

void tst_stremio_sync::markerWithMatchingCredentialActivatesSynced() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("local");
    existing.accountId = QStringLiteral("fixture-account");
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, existing);
        QTRY_COMPARE(committed.count(), 1);
    }
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &) -> std::optional<QByteArray> {
        return QByteArrayLiteral("fixture-vault-key");
    };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QCOMPARE(sync.status(), QStringLiteral("reconnectRequired"));
    sync.setMarkerLinked(true);
    QCOMPARE(sync.status(), QStringLiteral("synced"));
}

void tst_stremio_sync::productionEndpointRejectsUnrelatedHttpsOrigin() {
    QVERIFY(!StremioCodec::isProductionEndpoint(QUrl(QStringLiteral("https://example.invalid/api"))));
    QVERIFY(!StremioCodec::isProductionEndpoint(QUrl(QStringLiteral("https://api.strem.io/elsewhere"))));
}

void tst_stremio_sync::identityRedirectCannotValidateCredential() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-identity-redirect"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
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
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")), false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("fixture-auth-key")));
    QTRY_VERIFY(!api.request().isEmpty());
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 302 Found\r\nLocation: https://api.strem.io/api/getUser\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    QTRY_COMPARE(sync.status(), QStringLiteral("notConnected"));
    QCOMPARE(vaultWrites, 0);
}

void tst_stremio_sync::oversizedIdentityResponseCannotValidateCredential() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-identity-oversize"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
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
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")), false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("fixture-auth-key")));
    QTRY_VERIFY(!api.request().isEmpty());
    const QByteArray body = QByteArrayLiteral("{\"result\":{\"_id\":\"fixture-account\",\"fullname\":\"Fixture Person\",\"padding\":\"")
        + QByteArray(70 * 1024, 'x') + QByteArrayLiteral("\"}}");
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
    QTRY_COMPARE(sync.status(), QStringLiteral("notConnected"));
    QCOMPARE(vaultWrites, 0);
}

void tst_stremio_sync::defaultBrowserOpenerUsesOfficialLoginUrl() {
    BrowserLoginCapture capture;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &capture, "opened");
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("local"), QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")), false));
    QVERIFY(sync.startBrowserAuthentication());
    QTRY_VERIFY(capture.openedUrl.isValid());
    QCOMPARE(capture.openedUrl.scheme(), QStringLiteral("https"));
    QCOMPARE(capture.openedUrl.host(), QStringLiteral("www.stremio.com"));
    QCOMPARE(capture.openedUrl.path(), QStringLiteral("/login"));
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void tst_stremio_sync::progressIntentCoalescesOnlyUnsentReplaceableWork() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    QString first;
    QString second;
    QVERIFY(sync.queueIntent(QStringLiteral("progress"), QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}, {QStringLiteral("position"), 12}}, &first));
    QVERIFY(sync.queueIntent(QStringLiteral("progress"), QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}, {QStringLiteral("position"), 24}}, &second));
    QCOMPARE(sync.pendingCount(), 1);
    QCOMPARE(second, first);
    QVERIFY(sync.queueIntent(QStringLiteral("library-removal"), QJsonObject{{QStringLiteral("id"), QStringLiteral("movie-1")}}));
    QCOMPARE(sync.pendingCount(), 2);
}

void tst_stremio_sync::oversizedSocketRequestDoesNotPoisonNextCallback() {
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-oversize-recovery"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QUrl login;
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.browserOpener = [&login](const QUrl &url) { login = url; };
    options.saveCredential = [](const QString &, const QString &, const QByteArray &) { return true; };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")), false));
    QVERIFY(sync.startBrowserAuthentication());
    QVERIFY(sendRawLoopbackCallback(login, QByteArrayLiteral("GET /stremio/") + QByteArray(17 * 1024, 'x')));
    QVERIFY(sendLoopbackCallback(login, QByteArrayLiteral("fixture-auth-key")));
    QTRY_VERIFY(!api.request().isEmpty());
}

void tst_stremio_sync::exactTaggedFixtureProjectsOnlySanitizedTerminalState() {
    {
        ScopedEnvironmentVariable otherTag(
            "COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-fixture-not-authorized"));
        StremioSync sync;
        QVERIFY(!sync.activateTaggedFixture());
        QCOMPARE(sync.status(), QStringLiteral("notConnected"));
    }

    ScopedEnvironmentVariable fixtureTag(
        "COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-task1-fixture"));
    StremioSync sync;
    QVERIFY(sync.activateTaggedFixture());
    QCOMPARE(sync.activeProfileId(), QStringLiteral("stremio-task1-fixture"));
    QCOMPARE(sync.status(), QStringLiteral("synced"));
    QCOMPARE(sync.pendingCount(), 0);
    QCOMPARE(sync.lastSuccessAt(), qint64(1));
    QVERIFY(sync.mergeComplete());
    QCOMPARE(sync.completedRun(), quint64(1));
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

void tst_stremio_sync::
libraryItemCodecBatchesPreservesPatchAndSeparatesMembership() {
    const QJsonArray meta{
        QJsonArray{QStringLiteral("tt100"), QStringLiteral("1700000000000")},
        QJsonArray{QStringLiteral("kitsu:alpha"), QStringLiteral("1700000000001")},
        QJsonArray{QStringLiteral("tt100"), QStringLiteral("1700000000002")},
        QJsonArray{QStringLiteral(""), QStringLiteral("bad")},
        QJsonValue(QStringLiteral("malformed"))};
    int malformedMetaRows = 0;
    const QStringList ids = StremioCodec::decodeLibraryItemMeta(
        meta, &malformedMetaRows);
    QCOMPARE(ids, QStringList({QStringLiteral("tt100"), QStringLiteral("kitsu:alpha")}));
    QCOMPARE(malformedMetaRows, 2);

    const QJsonArray oversizedMeta{
        QJsonArray{QStringLiteral("tt200"), QStringLiteral("1")},
        QJsonArray{QStringLiteral("tt201"), QStringLiteral("2")},
        QJsonArray{QStringLiteral("tt202"), QStringLiteral("3")}};
    QCOMPARE(StremioCodec::decodeLibraryItemMeta(oversizedMeta, nullptr, 2),
             QStringList({QStringLiteral("tt200"), QStringLiteral("tt201")}));

    const QList<QStringList> batches = StremioCodec::boundedLibraryItemBatches(ids, 1);
    QCOMPARE(batches.size(), 2);
    QCOMPARE(batches.at(0), QStringList({QStringLiteral("tt100")}));
    QCOMPARE(batches.at(1), QStringList({QStringLiteral("kitsu:alpha")}));

    const QJsonArray rows{
        QJsonObject{
            {QStringLiteral("_id"), QStringLiteral("tt100")},
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("name"), QStringLiteral("A Film")},
            {QStringLiteral("customTopLevel"), QStringLiteral("keep")},
            {QStringLiteral("state"), QJsonObject{
                {QStringLiteral("video_id"), QStringLiteral("tt100")},
                {QStringLiteral("timeOffset"), 120000},
                {QStringLiteral("duration"), 7200000},
                {QStringLiteral("unknownState"), QStringLiteral("keep")}}}},
        QJsonObject{
            {QStringLiteral("_id"), QStringLiteral("tt101")},
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("temp"), true},
            {QStringLiteral("state"), QJsonObject{
                {QStringLiteral("video_id"), QStringLiteral("tt101")},
                {QStringLiteral("timeOffset"), 1000},
                {QStringLiteral("duration"), 10000}}}},
        QJsonObject{
            {QStringLiteral("_id"), QStringLiteral("tt102")},
            {QStringLiteral("type"), QStringLiteral("series")},
            {QStringLiteral("removed"), true}},
        QJsonObject{{QStringLiteral("_id"), QStringLiteral("bad")},
                    {QStringLiteral("type"), QStringLiteral("other")}},
        QJsonValue(QStringLiteral("not-an-object"))};
    const StremioLibraryItemDecode decoded =
        StremioCodec::decodeLibraryItems(rows);
    QCOMPARE(decoded.items.size(), 3);
    QCOMPARE(decoded.malformedRows, 2);
    QVERIFY(decoded.items.at(0).libraryMember);
    QVERIFY(!decoded.items.at(1).libraryMember);
    QVERIFY(decoded.items.at(1).temporary);
    QVERIFY(!decoded.items.at(2).libraryMember);
    QVERIFY(decoded.items.at(1).raw.value(QStringLiteral("state")).isObject());

    QJsonObject merged;
    QString error;
    QVERIFY2(StremioCodec::mergeLibraryItemPatch(
        decoded.items.at(0).raw,
        QJsonObject{{QStringLiteral("removed"), true},
                    {QStringLiteral("state"), QJsonObject{
                        {QStringLiteral("timeOffset"), 240000}}}},
        &merged,
        &error), qPrintable(error));
    QCOMPARE(merged.value(QStringLiteral("customTopLevel")).toString(), QStringLiteral("keep"));
    QCOMPARE(merged.value(QStringLiteral("state")).toObject().value(
        QStringLiteral("unknownState")).toString(), QStringLiteral("keep"));
    QCOMPARE(merged.value(QStringLiteral("state")).toObject().value(
        QStringLiteral("duration")).toInt(), 7200000);
    QCOMPARE(merged.value(QStringLiteral("state")).toObject().value(
        QStringLiteral("timeOffset")).toInt(), 240000);

    const QList<StremioDatastoreRequest> requests =
        StremioCodec::datastoreGetRequests(
            QByteArrayLiteral("native-only-test-key"), ids, 1);
    QCOMPARE(requests.size(), 2);
    QCOMPARE(requests.at(0).method, QStringLiteral("datastoreGet"));
    QCOMPARE(requests.at(0).payload.value(QStringLiteral("collection")).toString(),
             QStringLiteral("libraryItem"));
    QCOMPARE(requests.at(0).payload.value(QStringLiteral("ids")).toArray().size(), 1);

    const StremioDatastoreRequest put = StremioCodec::datastorePutRequest(
        QByteArrayLiteral("native-only-test-key"), merged);
    QCOMPARE(put.method, QStringLiteral("datastorePut"));
    QCOMPARE(put.payload.value(QStringLiteral("changes")).toArray().size(), 1);
    QCOMPARE(put.payload.value(QStringLiteral("changes")).toArray().first().toObject(), merged);
}

void tst_stremio_sync::libraryPullBatchesBoundedRowsAndIsolatesMalformedProviderData() {
    // This catches an unbounded `datastoreMeta` implementation or one corrupt
    // provider row aborting an otherwise valid Theatre import pull.
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-datastore-pull"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
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

    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);

    bool completed = false;
    bool succeeded = false;
    QList<StremioLibraryItem> pulled;
    QVERIFY(sync.pullLibraryItems([&](bool ok, QList<StremioLibraryItem> items) {
        completed = true;
        succeeded = ok;
        pulled = std::move(items);
    }));
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastoreMeta")));

    QJsonArray meta;
    for (int index = 0; index < 65; ++index)
        meta.append(QJsonArray{QStringLiteral("tt%1").arg(100 + index), QString::number(index + 1)});
    meta.append(QJsonValue(QStringLiteral("malformed-meta-row")));
    const QByteArray metaBody = QJsonDocument(QJsonObject{{QStringLiteral("result"), meta}})
        .toJson(QJsonDocument::Compact);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(metaBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + metaBody);

    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastoreGet")));
    auto latestGetIds = [&api] {
        const int requestStart = api.request().lastIndexOf(QByteArrayLiteral("POST /api/datastoreGet"));
        const int bodyStart = api.request().indexOf(QByteArrayLiteral("\r\n\r\n"), requestStart) + 4;
        return QJsonDocument::fromJson(api.request().mid(bodyStart))
            .object().value(QStringLiteral("ids")).toArray();
    };
    QCOMPARE(latestGetIds().size(), 64);
    const QByteArray firstGetBody = QJsonDocument(QJsonObject{{QStringLiteral("result"), QJsonArray{
        QJsonObject{{QStringLiteral("_id"), QStringLiteral("tt100")},
                    {QStringLiteral("type"), QStringLiteral("movie")},
                    {QStringLiteral("state"), QJsonObject{}}},
        QJsonValue(QStringLiteral("malformed-library-row"))}}})
        .toJson(QJsonDocument::Compact);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(firstGetBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + firstGetBody);

    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 2);
    QCOMPARE(latestGetIds().size(), 1);
    const QByteArray secondGetBody = QJsonDocument(QJsonObject{{QStringLiteral("result"), QJsonArray{
        QJsonObject{{QStringLiteral("_id"), QStringLiteral("tt164")},
                    {QStringLiteral("type"), QStringLiteral("movie")},
                    {QStringLiteral("state"), QJsonObject{}}}}}})
        .toJson(QJsonDocument::Compact);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(secondGetBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + secondGetBody);

    QTRY_VERIFY(completed);
    QVERIFY(succeeded);
    QCOMPARE(pulled.size(), 2);
    QCOMPARE(pulled.at(0).id, QStringLiteral("tt100"));
    QCOMPARE(pulled.at(1).id, QStringLiteral("tt164"));
    // A provider pull is only the first phase. AccountRuntime must now finish
    // the durable owner/Neon sequence before the union baseline is published.
    QVERIFY(!sync.mergeComplete());
    bool mergeDurable = false;
    QVERIFY(sync.completeFirstMerge([&mergeDurable](bool committed) { mergeDurable = committed; }));
    QTRY_VERIFY(mergeDurable);
    StremioState inspector;
    const auto persisted = inspector.load(path);
    QVERIFY(persisted.has_value());
    QVERIFY(persisted->firstMergeComplete);
}

void tst_stremio_sync::explicitLibraryRemovalReadsFreshProviderRowAndPreservesFields() {
    // This fails if the durable `library_remove` intent is only an in-memory
    // sender seam: the real provider boundary must read a fresh datastore row
    // before it sends a field-preserving removal patch.
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-datastore-remove"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
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

    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);

    QString operationId;
    QVERIFY(sync.queueExplicitLibraryRemoval(
        QStringLiteral("tt100"), QStringLiteral("movie"), {}, &operationId));
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastoreGet")));

    const QJsonObject freshItem{
        {QStringLiteral("_id"), QStringLiteral("tt100")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("name"), QStringLiteral("Keep this provider field")},
        {QStringLiteral("temp"), true},
        {QStringLiteral("state"), QJsonObject{{QStringLiteral("timeOffset"), 42000}}}};
    const QByteArray getBody = QJsonDocument(QJsonObject{{QStringLiteral("result"), QJsonArray{freshItem}}})
        .toJson(QJsonDocument::Compact);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(getBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + getBody);

    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastorePut")));
    const int putStart = api.request().lastIndexOf(QByteArrayLiteral("POST /api/datastorePut"));
    const int bodyStart = api.request().indexOf(QByteArrayLiteral("\r\n\r\n"), putStart) + 4;
    QJsonParseError parseError;
    const QJsonDocument put = QJsonDocument::fromJson(api.request().mid(bodyStart), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const QJsonObject changed = put.object().value(QStringLiteral("changes")).toArray().first().toObject();
    QCOMPARE(changed.value(QStringLiteral("name")).toString(), QStringLiteral("Keep this provider field"));
    QCOMPARE(changed.value(QStringLiteral("state")).toObject().value(QStringLiteral("timeOffset")).toInt(), 42000);
    QVERIFY(changed.value(QStringLiteral("removed")).toBool());
    QVERIFY(!changed.value(QStringLiteral("temp")).toBool());

    QVERIFY(sync.acknowledgeLocalReceipt(operationId));
    const QByteArray putBody = QByteArrayLiteral("{\"result\":true}");
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(putBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + putBody);
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 2);
    const QByteArray readBackBody = QJsonDocument(QJsonObject{{QStringLiteral("result"), QJsonArray{changed}}})
        .toJson(QJsonDocument::Compact);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(readBackBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + readBackBody);
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::progressIntentReadsFreshProviderRowAndUsesMilliseconds() {
    // This catches a sender that emits seconds into Stremio's millisecond
    // state, or overwrites provider-owned fields without a fresh read.
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-datastore-progress"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
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
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);
    QString operationId;
    QVERIFY(sync.queueIntent(
        QStringLiteral("progress"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("tt200")},
                    {QStringLiteral("libraryId"), QStringLiteral("tt200")},
                    {QStringLiteral("positionSeconds"), 12.5},
                    {QStringLiteral("durationSeconds"), 300.0},
                    {QStringLiteral("updatedAt"), QStringLiteral("1735787045000")}},
        &operationId));
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastoreGet")));
    const QJsonObject providerRow{
        {QStringLiteral("_id"), QStringLiteral("tt200")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("name"), QStringLiteral("Keep provider name")},
        {QStringLiteral("state"), QJsonObject{{QStringLiteral("custom"), QStringLiteral("keep")}}}};
    const QByteArray getBody = QJsonDocument(QJsonObject{
        {QStringLiteral("result"), QJsonArray{providerRow}}}).toJson(QJsonDocument::Compact);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(getBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + getBody);
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastorePut")));
    const int putStart = api.request().lastIndexOf(QByteArrayLiteral("POST /api/datastorePut"));
    const int bodyStart = api.request().indexOf(QByteArrayLiteral("\r\n\r\n"), putStart) + 4;
    const QJsonObject changed = QJsonDocument::fromJson(api.request().mid(bodyStart))
        .object().value(QStringLiteral("changes")).toArray().first().toObject();
    QCOMPARE(changed.value(QStringLiteral("name")).toString(), QStringLiteral("Keep provider name"));
    QCOMPARE(changed.value(QStringLiteral("state")).toObject().value(QStringLiteral("timeOffset")).toInt(), 12500);
    QCOMPARE(changed.value(QStringLiteral("state")).toObject().value(QStringLiteral("duration")).toInt(), 300000);
    QCOMPARE(changed.value(QStringLiteral("state")).toObject().value(QStringLiteral("video_id")).toString(), QStringLiteral("tt200"));
    QVERIFY(sync.acknowledgeLocalReceipt(operationId));
    const QByteArray putBody = QByteArrayLiteral("{\"result\":true}");
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(putBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + putBody);
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 2);
    const QByteArray readBackBody = QJsonDocument(QJsonObject{{QStringLiteral("result"), QJsonArray{changed}}})
        .toJson(QJsonDocument::Compact);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(readBackBody.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + readBackBody);
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::staleProviderProgressDoesNotOverwriteNewerActivity() {
    // The GET immediately before a provider mutation is the conflict boundary:
    // an older durable retry must retire without a PUT when Stremio already has
    // a newer partial position.  This is a real loopback datastore sequence,
    // not an intent-sender assertion.
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-stale-progress"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
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
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);

    QString operationId;
    QVERIFY(sync.queueIntent(QStringLiteral("progress"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("tt-stale")},
        {QStringLiteral("libraryId"), QStringLiteral("tt-stale")},
        {QStringLiteral("positionSeconds"), 12.5},
        {QStringLiteral("durationSeconds"), 300.0},
        {QStringLiteral("updatedAt"), QStringLiteral("1735787045000")}}, &operationId));
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastoreGet")));
    const QJsonObject newerProviderRow{
        {QStringLiteral("_id"), QStringLiteral("tt-stale")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), QStringLiteral("tt-stale")},
            {QStringLiteral("timeOffset"), 22000},
            {QStringLiteral("duration"), 300000},
            {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:04:06.000Z")}}}};
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("result"), QJsonArray{newerProviderRow}}}).toJson(QJsonDocument::Compact);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);

    QTest::qWait(100);
    QVERIFY2(!api.request().contains(QByteArrayLiteral("POST /api/datastorePut")),
             "an older local retry must not overwrite newer provider progress");
    QVERIFY(sync.acknowledgeLocalReceipt(operationId));
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::equalProviderActivityPreservesCurrentStateWithoutPut() {
    // Equal real activity has already reached the provider. Its concrete
    // state is the acknowledged current winner, so neither a progress nor a
    // watched retry may turn an equal-time tie into an echo PUT.
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-equal-provider"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
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
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);

    const QString timestamp = QStringLiteral("1735787045000");
    const auto replyCurrent = [&api](const QJsonObject &row) {
        const QByteArray body = QJsonDocument(QJsonObject{
            {QStringLiteral("result"), QJsonArray{row}}}).toJson(QJsonDocument::Compact);
        api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
    };
    QString progressOperation;
    QVERIFY(sync.queueIntent(QStringLiteral("progress"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("tt-equal-provider")},
        {QStringLiteral("libraryId"), QStringLiteral("tt-equal-provider")},
        {QStringLiteral("positionSeconds"), 12.5},
        {QStringLiteral("durationSeconds"), 300.0},
        {QStringLiteral("updatedAt"), timestamp}}, &progressOperation));
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 1);
    const QJsonObject progressWinner{
        {QStringLiteral("_id"), QStringLiteral("tt-equal-provider")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), QStringLiteral("tt-equal-provider")},
            {QStringLiteral("timeOffset"), 22000},
            {QStringLiteral("duration"), 300000},
            {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:04:05.000Z")}}}};
    replyCurrent(progressWinner);
    QTest::qWait(100);
    QVERIFY2(!api.request().contains(QByteArrayLiteral("POST /api/datastorePut")),
             "equal-time provider progress must remain the current winner");
    QVERIFY(sync.acknowledgeLocalReceipt(progressOperation));
    QTRY_COMPARE(sync.pendingCount(), 0);

    QString watchedOperation;
    QVERIFY(sync.queueIntent(QStringLiteral("watched"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("tt-equal-provider")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("watched"), true},
        {QStringLiteral("updatedAt"), timestamp}}, &watchedOperation));
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 2);
    const QJsonObject watchedWinner{
        {QStringLiteral("_id"), QStringLiteral("tt-equal-provider")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("flaggedWatched"), 0},
            {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:04:05.000Z")}}}};
    replyCurrent(watchedWinner);
    QTest::qWait(100);
    QVERIFY2(!api.request().contains(QByteArrayLiteral("POST /api/datastorePut")),
             "equal-time provider watched state must remain the current winner");
    QVERIFY(sync.acknowledgeLocalReceipt(watchedOperation));
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::canonicalTheatreReconcileJournalsOnlySemanticProviderDifference() {
    // AccountRuntime supplies committed Theatre owner snapshots. This narrow
    // native boundary must journal every distinct provider-relevant change
    // once, while a remote-applied invalidation with the same semantic owner
    // state remains a no-op rather than a Stremio echo.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    StremioSync sync;
    QVERIFY(sync.activateProfile(
        QStringLiteral("local"),
        QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json")),
        false));

    const QVariantList collection{
        QVariantMap{{QStringLiteral("world"), QStringLiteral("theatre")},
                    {QStringLiteral("id"), QStringLiteral("tt-reconcile")},
                    {QStringLiteral("type"), QStringLiteral("movie")}}};
    const QVariantList progress{
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                    {QStringLiteral("id"), QStringLiteral("tt-reconcile")},
                    {QStringLiteral("duration"), 300.0},
                    {QStringLiteral("resume"), QVariantMap{{QStringLiteral("position"), 12.5}}},
                    {QStringLiteral("updatedAt"), 1735787045000LL}}};
    const QHash<QString, int> watched{{QStringLiteral("tt-reconcile"), 1}};
    const QHash<QString, qint64> watchedAt{{QStringLiteral("tt-reconcile"), 1735787046000LL}};

    QVERIFY(sync.reconcileTheatreState(collection, progress, watched, watchedAt));
    QTRY_COMPARE(sync.pendingCount(), 3);
    QVERIFY(sync.reconcileTheatreState(collection, progress, watched, watchedAt));
    QCOMPARE(sync.pendingCount(), 3);
}

void tst_stremio_sync::reconcileProgressKeepsExactEpisodeAndSeparateLibraryRoot() {
    // Playback is allowed before a Collection membership exists.  The exact
    // episode identity remains the video id while the independently supplied
    // root selects its existing Stremio libraryItem; no membership is inferred.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));

    const QVariantList progress{
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                    {QStringLiteral("id"), QStringLiteral("kitsu:alpha:s2:e7")},
                    {QStringLiteral("libraryId"), QStringLiteral("kitsu:alpha")},
                    {QStringLiteral("duration"), 1440.0},
                    {QStringLiteral("resume"), QVariantMap{
                        {QStringLiteral("position"), 721.5},
                        {QStringLiteral("infoHash"), QStringLiteral("keep-local")},
                        {QStringLiteral("localPath"), QStringLiteral("D:/keep.mkv")}}},
                    {QStringLiteral("updatedAt"), 1735787045000LL}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                    {QStringLiteral("id"), QStringLiteral("tt-uncollected")},
                    {QStringLiteral("libraryId"), QStringLiteral("tt-uncollected")},
                    {QStringLiteral("duration"), 600.0},
                    {QStringLiteral("resume"), QVariantMap{
                        {QStringLiteral("position"), 60.0},
                        {QStringLiteral("localPath"), QStringLiteral("D:/also-keep.mkv")}}},
                    {QStringLiteral("updatedAt"), 1735787046000LL}}};
    QVERIFY(sync.reconcileTheatreState({}, progress, {}, {}));
    QTRY_COMPARE(sync.pendingCount(), 2);

    StremioState inspector;
    QTRY_VERIFY(([&inspector, &path] {
        const auto state = inspector.load(path);
        return state.has_value() && state->pendingIntents.size() == 2;
    }()));
    const auto persisted = inspector.load(path);
    QVERIFY(persisted.has_value());
    const auto episode = std::find_if(persisted->pendingIntents.cbegin(), persisted->pendingIntents.cend(),
                                      [](const StremioPendingIntent &intent) {
        return intent.kind == QLatin1String("progress")
            && intent.desired.value(QStringLiteral("id")).toString()
                == QLatin1String("kitsu:alpha:s2:e7");
    });
    QVERIFY(episode != persisted->pendingIntents.cend());
    QCOMPARE(episode->desired.value(QStringLiteral("libraryId")).toString(),
             QStringLiteral("kitsu:alpha"));
    QCOMPARE(episode->desired.value(QStringLiteral("durationSeconds")).toDouble(), 1440.0);
    QCOMPARE(episode->desired.value(QStringLiteral("positionSeconds")).toDouble(), 721.5);
    QVERIFY(std::none_of(persisted->pendingIntents.cbegin(), persisted->pendingIntents.cend(),
                         [](const StremioPendingIntent &intent) {
        return intent.kind == QLatin1String("library_add");
    }));
}

void tst_stremio_sync::reconciledOwnerIntentCarriesItsDurableLocalReceipt() {
    // AccountRuntime invokes reconciliation only after the canonical owner
    // receipt. The private provider intent must carry that fact in its first
    // durable journal write; no second caller acknowledgement exists here.
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
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    options.intentSender = [&sends](const StremioPendingIntent &, auto completion) {
        ++sends;
        completion(true, false);
    };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);
    const QVariantList collection{
        QVariantMap{{QStringLiteral("world"), QStringLiteral("theatre")},
                    {QStringLiteral("id"), QStringLiteral("tt-owner-receipt")},
                    {QStringLiteral("type"), QStringLiteral("movie")}}};
    QVERIFY(sync.reconcileTheatreState(collection, {}, {}, {}));
    QTRY_COMPARE(sends, 1);
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::libraryAddAndWatchedPatchesReadBackBeforeBaselineSettlement() {
    // Library membership and a manual movie watch both use a fresh provider
    // row, preserve its opaque fields, and settle their private baseline only
    // after the provider readback. A synthetic success response is not enough.
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-add-watch-readback"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
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
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);

    QString addOperation;
    QVERIFY(sync.queueIntent(QStringLiteral("library_add"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("tt-add-watch")},
                    {QStringLiteral("type"), QStringLiteral("movie")}}, &addOperation));
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastoreGet")));
    const QJsonObject prior{{QStringLiteral("_id"), QStringLiteral("tt-add-watch")},
                            {QStringLiteral("type"), QStringLiteral("movie")},
                            {QStringLiteral("name"), QStringLiteral("Provider title")},
                            {QStringLiteral("removed"), true},
                            {QStringLiteral("temp"), true},
                            {QStringLiteral("state"), QJsonObject{{QStringLiteral("custom"), QStringLiteral("keep")}}}};
    const auto replyResult = [&api](const QJsonObject &row) {
        const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("result"), QJsonArray{row}}})
            .toJson(QJsonDocument::Compact);
        api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
    };
    replyResult(prior);
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastorePut")));
    const int addPutStart = api.request().lastIndexOf(QByteArrayLiteral("POST /api/datastorePut"));
    const int addBodyStart = api.request().indexOf(QByteArrayLiteral("\r\n\r\n"), addPutStart) + 4;
    const QJsonObject added = QJsonDocument::fromJson(api.request().mid(addBodyStart))
        .object().value(QStringLiteral("changes")).toArray().first().toObject();
    QCOMPARE(added.value(QStringLiteral("name")).toString(), QStringLiteral("Provider title"));
    QVERIFY(!added.value(QStringLiteral("removed")).toBool());
    QVERIFY(!added.value(QStringLiteral("temp")).toBool());
    const QByteArray putOk = QByteArrayLiteral("{\"result\":true}");
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(putOk.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + putOk);
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 2);
    replyResult(added);
    QVERIFY(sync.acknowledgeLocalReceipt(addOperation));
    QTRY_COMPARE(sync.pendingCount(), 0);

    QString watchedOperation;
    QVERIFY(sync.queueIntent(QStringLiteral("watched"),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("tt-add-watch")},
                    {QStringLiteral("type"), QStringLiteral("movie")},
                    {QStringLiteral("watched"), true},
                    {QStringLiteral("updatedAt"), QStringLiteral("1735787046000")}}, &watchedOperation));
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 3);
    replyResult(added);
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastorePut")), 2);
    const int watchedPutStart = api.request().lastIndexOf(QByteArrayLiteral("POST /api/datastorePut"));
    const int watchedBodyStart = api.request().indexOf(QByteArrayLiteral("\r\n\r\n"), watchedPutStart) + 4;
    const QJsonObject watched = QJsonDocument::fromJson(api.request().mid(watchedBodyStart))
        .object().value(QStringLiteral("changes")).toArray().first().toObject();
    QCOMPARE(watched.value(QStringLiteral("state")).toObject().value(QStringLiteral("custom")).toString(), QStringLiteral("keep"));
    QCOMPARE(watched.value(QStringLiteral("state")).toObject().value(QStringLiteral("flaggedWatched")).toInt(), 1);
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(putOk.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + putOk);
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 4);
    replyResult(watched);
    QVERIFY(sync.acknowledgeLocalReceipt(watchedOperation));
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::datastoreBusyDefersSecondDurableIntentWithoutRetryPenalty() {
    // Two independent durable intents can become eligible in the same event
    // turn. The datastore has one active reply by design; the second intent
    // must wait for it, not consume a retry slot before a provider request.
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-datastore-busy"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioPersistentState existing;
    existing.profileId = QStringLiteral("local");
    existing.bindingGeneration = 1;
    existing.accountId = QStringLiteral("fixture-account");
    StremioPendingIntent library;
    library.operationId = QStringLiteral("library-current");
    library.kind = QStringLiteral("library_add");
    library.desired = QJsonObject{{QStringLiteral("id"), QStringLiteral("tt-busy")},
                                  {QStringLiteral("type"), QStringLiteral("movie")}};
    library.localReceiptDurable = true;
    StremioPendingIntent progress;
    progress.operationId = QStringLiteral("progress-current");
    progress.kind = QStringLiteral("progress");
    progress.desired = QJsonObject{{QStringLiteral("id"), QStringLiteral("tt-busy")},
                                   {QStringLiteral("libraryId"), QStringLiteral("tt-busy")},
                                   {QStringLiteral("positionSeconds"), 20.0},
                                   {QStringLiteral("durationSeconds"), 100.0},
                                   {QStringLiteral("updatedAt"), QStringLiteral("1735787046000")}};
    progress.localReceiptDurable = true;
    existing.pendingIntents = {library, progress};
    {
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, existing);
        QTRY_COMPARE(committed.count(), 1);
    }
    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);
    sync.retryPendingNow();
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 1);

    StremioState inspector;
    const auto noBusyPenalty = [&] {
        const auto persisted = inspector.load(path);
        return persisted.has_value()
            && persisted->pendingIntents.size() == 2
            && persisted->pendingIntents.at(1).operationId == progress.operationId
            && persisted->pendingIntents.at(1).attempts == 0;
    };
    QTRY_VERIFY(noBusyPenalty());

    const QJsonObject current{{QStringLiteral("_id"), QStringLiteral("tt-busy")},
                              {QStringLiteral("type"), QStringLiteral("movie")},
                              {QStringLiteral("removed"), false},
                              {QStringLiteral("temp"), false},
                              {QStringLiteral("state"), QJsonObject{
                                  {QStringLiteral("video_id"), QStringLiteral("tt-busy")},
                                  {QStringLiteral("timeOffset"), 20000},
                                  {QStringLiteral("duration"), 100000}}}};
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("result"), QJsonArray{current}}}).toJson(QJsonDocument::Compact);
    const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
    api.replyRaw(response);
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 2);
}

void tst_stremio_sync::olderProgressAcknowledgementCannotClearNewerGeneration() {
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
    QList<std::function<void(bool, bool)>> completions;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    options.intentSender = [&completions](const StremioPendingIntent &, auto completion) {
        completions.append(std::move(completion));
    };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);

    QString older;
    QString newer;
    QVERIFY(sync.queueIntent(QStringLiteral("progress"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("tt-newer")},
        {QStringLiteral("libraryId"), QStringLiteral("tt-newer")},
        {QStringLiteral("positionSeconds"), 10.0},
        {QStringLiteral("durationSeconds"), 100.0},
        {QStringLiteral("updatedAt"), QStringLiteral("1735787045000")}}, &older));
    QTRY_COMPARE(completions.size(), 1);
    QVERIFY(sync.queueIntent(QStringLiteral("progress"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("tt-newer")},
        {QStringLiteral("libraryId"), QStringLiteral("tt-newer")},
        {QStringLiteral("positionSeconds"), 20.0},
        {QStringLiteral("durationSeconds"), 100.0},
        {QStringLiteral("updatedAt"), QStringLiteral("1735787046000")}}, &newer));
    QVERIFY(newer != older);
    QTRY_COMPARE(completions.size(), 2);

    completions.at(0)(true, false);
    QVERIFY(sync.acknowledgeLocalReceipt(older));
    QTRY_COMPARE(sync.pendingCount(), 1);
    completions.at(1)(true, false);
    QVERIFY(sync.acknowledgeLocalReceipt(newer));
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::explicitReaddClearsOnlyLocalMembershipSuppression() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));

    bool suppressionCommitted = false;
    QVERIFY(sync.recordLocalOnlyLibraryRemoval(
        QStringLiteral("tt-explicit-readd"),
        QStringLiteral("movie"),
        [&suppressionCommitted](bool committed) {
            suppressionCommitted = committed;
        }));
    QTRY_VERIFY(suppressionCommitted);
    QVERIFY(sync.suppressesRemoteLibraryMembership(
        QStringLiteral("tt-explicit-readd"),
        QStringLiteral("movie")));

    // The current Theatre membership exists only after a fresh explicit add.
    // Reconciliation must turn that action into a new provider membership and
    // retire the stale local-only suppression; it may not revive an inferred
    // cross-service deletion.
    QVERIFY(sync.reconcileTheatreState(
        QVariantList{QVariantMap{
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("id"), QStringLiteral("tt-explicit-readd")},
            {QStringLiteral("type"), QStringLiteral("movie")}}},
        {},
        {},
        {}));
    QTRY_COMPARE(sync.pendingCount(), 1);
    QVERIFY(!sync.suppressesRemoteLibraryMembership(
        QStringLiteral("tt-explicit-readd"),
        QStringLiteral("movie")));

    StremioState inspector;
    QTRY_VERIFY([&] {
        const auto persisted = inspector.load(path);
        return persisted.has_value()
            && persisted->intentionalMembershipDifferences.isEmpty()
            && persisted->pendingIntents.size() == 1;
    }());
    const auto persisted = inspector.load(path);
    QVERIFY(persisted.has_value());
    QCOMPARE(persisted->pendingIntents.first().kind, QStringLiteral("library_add"));

    sync.deactivateProfile();
    StremioSync reopened;
    QVERIFY(reopened.activateProfile(QStringLiteral("local"), path, false));
    QVERIFY(!reopened.suppressesRemoteLibraryMembership(
        QStringLiteral("tt-explicit-readd"),
        QStringLiteral("movie")));

    StremioSync otherProfile;
    QVERIFY(otherProfile.activateProfile(
        QStringLiteral("other-profile"),
        QDir(temp.path()).filePath(QStringLiteral("other-stremio-sync.json")),
        false));
    QVERIFY(!otherProfile.suppressesRemoteLibraryMembership(
        QStringLiteral("tt-explicit-readd"),
        QStringLiteral("movie")));
}

void tst_stremio_sync::remoteRemovalDifferenceSurvivesRestartWithoutLibraryAdd() {
    // A passive provider removal is represented by the durable inverse
    // difference (local present, remote absent). Re-reading the unchanged
    // Collection after a restart must not reinterpret that import as a local
    // request to put the item back into Stremio.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("stremio-sync.json"));
    const QVariantList collection{QVariantMap{
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("id"), QStringLiteral("tt-remote-removed")},
        {QStringLiteral("type"), QStringLiteral("movie")}}};

    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    bool persisted = false;
    QVERIFY(sync.recordRemoteLibraryRemoval(
        QStringLiteral("tt-remote-removed"), QStringLiteral("movie"),
        [&persisted](bool committed) { persisted = committed; }));
    QTRY_VERIFY(persisted);
    sync.deactivateProfile();

    QList<StremioPendingIntent> emitted;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    options.intentSender = [&emitted](const StremioPendingIntent &intent, auto completion) {
        emitted.append(intent);
        completion(true, false);
    };
    StremioSync reopened(options);
    QVERIFY(reopened.activateProfile(QStringLiteral("local"), path, false));
    reopened.setMarkerLinked(true);
    QVERIFY(reopened.reconcileTheatreState(collection, {}, {}, {}));
    QTRY_COMPARE(reopened.pendingCount(), 0);
    QCOMPARE(emitted.size(), 0);
}

void tst_stremio_sync::durableMembershipTransitionsRetireBaselineAndPermitExplicitReadd() {
    // Adds and explicit removals are opposite values of one durable membership
    // baseline.  Acknowledge the complete add -> dual-remove sequence, then
    // prove a later canonical re-add is not hidden by the old add baseline.
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
    QList<StremioPendingIntent> sent;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    options.intentSender = [&sent](const StremioPendingIntent &intent, auto completion) {
        sent.append(intent);
        completion(true, false);
    };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);

    QString add;
    QVERIFY(sync.queueIntent(QStringLiteral("library_add"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("tt-membership-cycle")},
        {QStringLiteral("type"), QStringLiteral("movie")}}, &add));
    QTRY_COMPARE(sent.size(), 1);
    QVERIFY(sync.acknowledgeLocalReceipt(add));
    QTRY_COMPARE(sync.pendingCount(), 0);

    QString remove;
    QVERIFY(sync.queueExplicitLibraryRemoval(
        QStringLiteral("tt-membership-cycle"), QStringLiteral("movie"), {}, &remove));
    QTRY_COMPARE(sent.size(), 2);
    QVERIFY(sync.acknowledgeLocalReceipt(remove));
    QTRY_COMPARE(sync.pendingCount(), 0);

    QVERIFY(sync.reconcileTheatreState(
        QVariantList{QVariantMap{{QStringLiteral("world"), QStringLiteral("theatre")},
                                 {QStringLiteral("id"), QStringLiteral("tt-membership-cycle")},
                                 {QStringLiteral("type"), QStringLiteral("movie")}}},
        {}, {}, {}));
    QTRY_VERIFY(std::count_if(sent.cbegin(), sent.cend(), [](const StremioPendingIntent &intent) {
        return intent.kind == QLatin1String("library_add")
            && intent.desired.value(QStringLiteral("id")).toString()
                == QLatin1String("tt-membership-cycle");
    }) == 2);
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::watchedEpisodeCodecIsBoundedAnchoredAndStable() {
    const QList<StremioEpisodeIdentity> videos{
        {QStringLiteral("kitsu:alpha:s0:e1"), 0, 1},
        {QStringLiteral("kitsu:alpha:s1:e1"), 1, 1},
        {QStringLiteral("kitsu:alpha:s1:e2"), 1, 2},
        {QStringLiteral("kitsu:alpha:s2:e1"), 2, 1}};
    QSet<QString> watched{QStringLiteral("unchanged")};
    QString error;
    // zlib([0b00001001]) with a full-length anchor: the special and the
    // final ordinary episode are watched. This literal does not reuse the
    // production encoder, so a bit-order or anchor-offset regression is red.
    QVERIFY2(StremioCodec::decodeWatchedEpisodes(
        QStringLiteral("kitsu:alpha:s2:e1:4:eJzjBAAACgAK"),
        videos,
        &watched,
        &error), qPrintable(error));
    QCOMPARE(watched, QSet<QString>({
        QStringLiteral("kitsu:alpha:s0:e1"),
        QStringLiteral("kitsu:alpha:s2:e1")}));

    QString encoded;
    QVERIFY2(StremioCodec::encodeWatchedEpisodes(watched, videos, &encoded, &error),
             qPrintable(error));
    QVERIFY(encoded.startsWith(QStringLiteral("kitsu:alpha:s2:e1:4:")));
    QSet<QString> roundTripped;
    QVERIFY2(StremioCodec::decodeWatchedEpisodes(encoded, videos, &roundTripped, &error),
             qPrintable(error));
    QCOMPARE(roundTripped, watched);

    const QSet<QString> before{QStringLiteral("keep-on-failure")};
    watched = before;
    QVERIFY(!StremioCodec::decodeWatchedEpisodes(
        QStringLiteral("kitsu:alpha:s2:e1:4:not-base64"), videos, &watched, &error));
    QCOMPARE(watched, before);

    QList<StremioEpisodeIdentity> ambiguous = videos;
    ambiguous.append(videos.first());
    QVERIFY(!StremioCodec::decodeWatchedEpisodes(encoded, ambiguous, &watched, &error));
    QCOMPARE(watched, before);

    StremioLibraryItem movie;
    movie.id = QStringLiteral("tt100");
    movie.type = QStringLiteral("movie");
    movie.raw = QJsonObject{{QStringLiteral("state"), QJsonObject{
        {QStringLiteral("flaggedWatched"), 1}}}};
    bool movieWatched = false;
    QVERIFY(StremioCodec::movieFlaggedWatched(movie, &movieWatched));
    QVERIFY(movieWatched);
}

void tst_stremio_sync::seriesWatchedSenderPreservesRemoteBitsAndReadsBack() {
    // A completed local episode subset must be merged into the current
    // provider bitfield. The sender may not clear an unrelated remote bit or
    // replace provider state with an inferred series-root watch.
    ScopedEnvironmentVariable tag("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("stremio-series-watched"));
    FixtureStremioApi api;
    QVERIFY(api.listen());
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

    const QList<StremioEpisodeIdentity> videos{
        {QStringLiteral("kitsu:alpha:s0:e1"), 0, 1},
        {QStringLiteral("kitsu:alpha:s1:e1"), 1, 1},
        {QStringLiteral("kitsu:alpha:s1:e2"), 1, 2}};
    QJsonArray episodeProjection;
    for (const StremioEpisodeIdentity &video : videos) {
        episodeProjection.append(QJsonObject{{QStringLiteral("id"), video.videoId},
                                             {QStringLiteral("season"), video.season},
                                             {QStringLiteral("episode"), video.episode}});
    }
    QString remoteWatched;
    QString error;
    QVERIFY2(StremioCodec::encodeWatchedEpisodes(
                 QSet<QString>{QStringLiteral("kitsu:alpha:s1:e2")},
                 videos, &remoteWatched, &error), qPrintable(error));

    StremioSyncOptions options;
    options.apiEndpoint = api.endpoint();
    options.allowTaggedLoopbackFixture = true;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);

    QString operationId;
    QVERIFY(sync.queueIntent(QStringLiteral("series_watched"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("kitsu:alpha")},
        {QStringLiteral("type"), QStringLiteral("series")},
        {QStringLiteral("episodeIds"), QJsonArray{QStringLiteral("kitsu:alpha:s1:e1")}},
        {QStringLiteral("episodes"), episodeProjection}}, &operationId));
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastoreGet")));

    const QJsonObject fresh{
        {QStringLiteral("_id"), QStringLiteral("kitsu:alpha")},
        {QStringLiteral("type"), QStringLiteral("series")},
        {QStringLiteral("name"), QStringLiteral("Provider title")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("custom"), QStringLiteral("keep")},
            {QStringLiteral("watched"), remoteWatched}}}};
    const auto replyResult = [&api](const QJsonObject &row) {
        const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("result"), QJsonArray{row}}})
            .toJson(QJsonDocument::Compact);
        api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
    };
    replyResult(fresh);
    QTRY_VERIFY(api.request().contains(QByteArrayLiteral("POST /api/datastorePut")));
    const int putStart = api.request().lastIndexOf(QByteArrayLiteral("POST /api/datastorePut"));
    const int bodyStart = api.request().indexOf(QByteArrayLiteral("\r\n\r\n"), putStart) + 4;
    const QJsonObject changed = QJsonDocument::fromJson(api.request().mid(bodyStart))
        .object().value(QStringLiteral("changes")).toArray().first().toObject();
    const QJsonObject state = changed.value(QStringLiteral("state")).toObject();
    QCOMPARE(state.value(QStringLiteral("custom")).toString(), QStringLiteral("keep"));
    QVERIFY(!state.value(QStringLiteral("watched")).toString().isEmpty());
    QSet<QString> mergedWatched;
    QVERIFY2(StremioCodec::decodeWatchedEpisodes(
                 state.value(QStringLiteral("watched")).toString(), videos, &mergedWatched, &error),
             qPrintable(error));
    QCOMPARE(mergedWatched, QSet<QString>({QStringLiteral("kitsu:alpha:s1:e1"),
                                           QStringLiteral("kitsu:alpha:s1:e2")}));

    const QByteArray putOk = QByteArrayLiteral("{\"result\":true}");
    api.replyRaw(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(putOk.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + putOk);
    QTRY_COMPARE(api.request().count(QByteArrayLiteral("POST /api/datastoreGet")), 2);
    replyResult(changed);
    QVERIFY(sync.acknowledgeLocalReceipt(operationId));
    QTRY_COMPARE(sync.pendingCount(), 0);
}

void tst_stremio_sync::seriesReconcileQueuesOnlyResolvedExactEpisodes() {
    // A Theatre root watch mark and an incomplete episode subset have no
    // provider bitfield meaning. Reconciliation waits for the bridge's full
    // ordered map, then emits only completed exact episode identities.
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
    QList<StremioPendingIntent> sent;
    StremioSyncOptions options;
    options.loadCredential = [](const QString &, const QString &)
        -> std::optional<QByteArray> { return QByteArrayLiteral("fixture-vault-key"); };
    options.intentSender = [&sent](const StremioPendingIntent &intent, auto completion) {
        sent.append(intent);
        completion(true, false);
    };
    StremioSync sync(options);
    QVERIFY(sync.activateProfile(QStringLiteral("local"), path, false));
    sync.setMarkerLinked(true);
    QSignalSpy requested(&sync, &StremioSync::episodeMetadataRequested);
    connect(&sync, &StremioSync::episodeMetadataRequested,
            &sync, [&sync](const QString &requestId, const QString &seriesId) {
        QVERIFY(sync.submitEpisodeMetadata(requestId, seriesId, QVariantList{
            QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:alpha:s0:e1")},
                        {QStringLiteral("season"), 0}, {QStringLiteral("episode"), 1}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:alpha:s1:e1")},
                        {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 1}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:alpha:s1:e2")},
                        {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 2}}}));
    });

    const QVariantList collection{QVariantMap{
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("id"), QStringLiteral("kitsu:alpha")},
        {QStringLiteral("type"), QStringLiteral("series")}}};
    const QVariantList progress{
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                    {QStringLiteral("id"), QStringLiteral("kitsu:alpha")},
                    {QStringLiteral("progress"), 1.0}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                    {QStringLiteral("id"), QStringLiteral("kitsu:alpha:s1:e1")},
                    {QStringLiteral("progress"), 0.9}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                    {QStringLiteral("id"), QStringLiteral("kitsu:alpha:s1:e2")},
                    {QStringLiteral("progress"), 0.4}}};
    QVERIFY(sync.reconcileTheatreState(
        collection, progress, QHash<QString, int>{{QStringLiteral("kitsu:alpha"), 1}}, {}));
    QCOMPARE(requested.count(), 0);
    sync.setEpisodeMetadataBridgeReady(true);
    QTRY_COMPARE(requested.count(), 1);
    QTRY_VERIFY(std::any_of(sent.cbegin(), sent.cend(), [](const StremioPendingIntent &intent) {
        return intent.kind == QLatin1String("series_watched");
    }));
    const auto intent = std::find_if(sent.cbegin(), sent.cend(), [](const StremioPendingIntent &candidate) {
        return candidate.kind == QLatin1String("series_watched");
    });
    QCOMPARE(intent->desired.value(QStringLiteral("episodeIds")).toArray(),
             QJsonArray{QStringLiteral("kitsu:alpha:s1:e1")});
    QCOMPARE(intent->desired.value(QStringLiteral("episodes")).toArray().size(), 3);
}

void tst_stremio_sync::theatreProjectionKeepsOpaqueEpisodeIdentityAndConvertsMilliseconds() {
    StremioLibraryItem series;
    series.id = QStringLiteral("kitsu:alpha");
    series.type = QStringLiteral("series");
    series.libraryMember = true;
    series.raw = QJsonObject{
        {QStringLiteral("_id"), series.id},
        {QStringLiteral("type"), series.type},
        {QStringLiteral("name"), QStringLiteral("Alpha")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), QStringLiteral("kitsu:alpha:s0:e1")},
            {QStringLiteral("timeOffset"), 120500},
            {QStringLiteral("duration"), 300000},
            {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:04:05.000Z")}}}};

    const StremioTheatreItemProjection projected =
        StremioCodec::projectTheatreItem(series);
    QVERIFY2(projected.valid, qPrintable(projected.error));
    QVERIFY(projected.hasCollection);
    QVERIFY(projected.hasProgress);
    QVERIFY(!projected.hasHistory);
    QCOMPARE(projected.collection.value(QStringLiteral("world")).toString(), QStringLiteral("theatre"));
    QCOMPARE(projected.collection.value(QStringLiteral("id")).toString(), QStringLiteral("kitsu:alpha"));
    QCOMPARE(projected.collection.value(QStringLiteral("type")).toString(), QStringLiteral("series"));
    QCOMPARE(projected.progress.value(QStringLiteral("id")).toString(), QStringLiteral("kitsu:alpha:s0:e1"));
    QCOMPARE(projected.progress.value(QStringLiteral("libraryId")).toString(), QStringLiteral("kitsu:alpha"));
    QCOMPARE(projected.progress.value(QStringLiteral("kind")).toString(), QStringLiteral("video"));
    QCOMPARE(projected.progress.value(QStringLiteral("duration")).toDouble(), 300.0);
    QCOMPARE(projected.progress.value(QStringLiteral("resume")).toMap().value(
                 QStringLiteral("position")).toDouble(), 120.5);
    QCOMPARE(projected.progress.value(QStringLiteral("updatedAt")).toLongLong(), qint64(1735787045000));

    StremioLibraryItem movie;
    movie.id = QStringLiteral("tt100");
    movie.type = QStringLiteral("movie");
    movie.temporary = true;
    movie.raw = QJsonObject{
        {QStringLiteral("_id"), movie.id},
        {QStringLiteral("type"), movie.type},
        {QStringLiteral("name"), QStringLiteral("A Film")},
        {QStringLiteral("state"), QJsonObject{
            {QStringLiteral("video_id"), movie.id},
            {QStringLiteral("timeOffset"), 1000},
            {QStringLiteral("duration"), 10000},
            {QStringLiteral("flaggedWatched"), 1},
            {QStringLiteral("lastWatched"), QStringLiteral("2025-01-02T03:04:05.000Z")}}}};
    const StremioTheatreItemProjection movieProjection =
        StremioCodec::projectTheatreItem(movie);
    QVERIFY2(movieProjection.valid, qPrintable(movieProjection.error));
    QVERIFY(!movieProjection.hasCollection);
    QVERIFY(movieProjection.hasProgress);
    QVERIFY(movieProjection.hasHistory);
    QVERIFY(movieProjection.hasWatchState);
    QVERIFY(movieProjection.watched);
    QCOMPARE(movieProjection.watchActionAtMs, qint64(1735787045000));
    QCOMPARE(movieProjection.progress.value(QStringLiteral("id")).toString(), QStringLiteral("tt100"));
    QCOMPARE(movieProjection.history.value(QStringLiteral("kind")).toString(), QStringLiteral("movie"));
    QCOMPARE(movieProjection.history.value(QStringLiteral("id")).toString(), QStringLiteral("tt100"));
    QCOMPARE(movieProjection.history.value(QStringLiteral("source")).toString(), QStringLiteral("stremio"));
    QCOMPARE(movieProjection.history.value(QStringLiteral("displayId")).toString(), QStringLiteral("tt100"));
    QCOMPARE(movieProjection.history.value(QStringLiteral("displayTitle")).toString(), QStringLiteral("A Film"));
    QCOMPARE(movieProjection.history.value(QStringLiteral("latestKnownAt")).toLongLong(), qint64(1735787045000));

    StremioLibraryItem undatedWatched = movie;
    undatedWatched.id = QStringLiteral("tt101");
    undatedWatched.raw.insert(QStringLiteral("_id"), undatedWatched.id);
    undatedWatched.raw.insert(QStringLiteral("state"), QJsonObject{
        {QStringLiteral("flaggedWatched"), 1}});
    const StremioTheatreItemProjection undatedProjection =
        StremioCodec::projectTheatreItem(undatedWatched);
    QVERIFY2(undatedProjection.valid, qPrintable(undatedProjection.error));
    QVERIFY(undatedProjection.hasWatchState);
    QVERIFY(undatedProjection.watched);
    QCOMPARE(undatedProjection.watchActionAtMs, qint64(0));
    QVERIFY(!undatedProjection.hasHistory);

    series.raw.insert(QStringLiteral("state"), QJsonObject{
        {QStringLiteral("video_id"), QStringLiteral("tt100")},
        {QStringLiteral("timeOffset"), 1000},
        {QStringLiteral("duration"), 10000}});
    const StremioTheatreItemProjection malformed =
        StremioCodec::projectTheatreItem(series);
    QVERIFY(!malformed.valid);
    QVERIFY(!malformed.hasCollection);
    QVERIFY(!malformed.hasProgress);
    QVERIFY(!malformed.hasHistory);
}

void tst_stremio_sync::episodeMetadataBridgeBindsOneShotRepliesToActiveProfile() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString profileAPath = QDir(temp.path()).filePath(QStringLiteral("a-stremio.json"));
    const QString profileBPath = QDir(temp.path()).filePath(QStringLiteral("b-stremio.json"));
    for (const auto &[profileId, path] : {
             std::pair<QString, QString>{QStringLiteral("profile-a"), profileAPath},
             std::pair<QString, QString>{QStringLiteral("profile-b"), profileBPath}}) {
        StremioPersistentState state;
        state.profileId = profileId;
        state.bindingGeneration = 1;
        state.accountId = QStringLiteral("fixture-account");
        StremioState writer;
        QSignalSpy committed(&writer, &StremioState::persistenceCommitted);
        writer.saveAsync(path, state);
        QTRY_COMPARE(committed.count(), 1);
    }

    StremioSync sync;
    QVERIFY(sync.activateProfile(QStringLiteral("profile-a"), profileAPath, false));
    sync.setEpisodeMetadataBridgeReady(true);
    QSignalSpy requested(&sync, &StremioSync::episodeMetadataRequested);
    bool completed = false;
    QList<StremioEpisodeIdentity> resolved;
    QVERIFY(sync.requestEpisodeMetadata(
        QStringLiteral("kitsu:alpha"),
        [&completed, &resolved](bool ok, QList<StremioEpisodeIdentity> episodes) {
            completed = ok;
            resolved = std::move(episodes);
        }));
    QTRY_COMPARE(requested.count(), 1);
    const QString firstRequestId = requested.at(0).at(0).toString();
    QCOMPARE(requested.at(0).at(1).toString(), QStringLiteral("kitsu:alpha"));

    const QVariantList validEpisodes{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:alpha:s0:e1")},
                    {QStringLiteral("season"), 0},
                    {QStringLiteral("episode"), 1}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:alpha:s1:e1")},
                    {QStringLiteral("season"), 1},
                    {QStringLiteral("episode"), 1}}};
    QVERIFY(!sync.submitEpisodeMetadata(
        firstRequestId,
        QStringLiteral("kitsu:other"),
        validEpisodes));
    QVERIFY(!completed);
    QVERIFY(sync.submitEpisodeMetadata(
        firstRequestId,
        QStringLiteral("kitsu:alpha"),
        validEpisodes));
    QVERIFY(completed);
    QCOMPARE(resolved.size(), 2);
    QCOMPARE(resolved.first().videoId, QStringLiteral("kitsu:alpha:s0:e1"));
    QVERIFY(!sync.submitEpisodeMetadata(
        firstRequestId,
        QStringLiteral("kitsu:alpha"),
        validEpisodes));

    bool ambiguousCompletion = false;
    QVERIFY(sync.requestEpisodeMetadata(
        QStringLiteral("kitsu:alpha"),
        [&ambiguousCompletion](bool, QList<StremioEpisodeIdentity>) {
            ambiguousCompletion = true;
        }));
    QTRY_COMPARE(requested.count(), 2);
    const QString ambiguousRequestId = requested.at(1).at(0).toString();
    const QVariantList ambiguousEpisodes{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:alpha:s1:e1a")},
                    {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 1}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("kitsu:alpha:s1:e1b")},
                    {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 1}}};
    QVERIFY(!sync.submitEpisodeMetadata(
        ambiguousRequestId, QStringLiteral("kitsu:alpha"), ambiguousEpisodes));
    QVERIFY(!ambiguousCompletion);

    bool staleCompletion = false;
    QVERIFY(sync.requestEpisodeMetadata(
        QStringLiteral("kitsu:alpha"),
        [&staleCompletion](bool, QList<StremioEpisodeIdentity>) {
            staleCompletion = true;
        }));
    QTRY_COMPARE(requested.count(), 3);
    const QString staleRequestId = requested.at(2).at(0).toString();
    QVERIFY(sync.activateProfile(QStringLiteral("profile-b"), profileBPath, false));
    QVERIFY(!sync.submitEpisodeMetadata(
        staleRequestId,
        QStringLiteral("kitsu:alpha"),
        validEpisodes));
    QVERIFY(staleCompletion);
}

QTEST_MAIN(tst_stremio_sync)
#include "tst_stremio_sync.moc"
