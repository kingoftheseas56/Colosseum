// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

// No-account edition runtime startup proof (1.1.9 review finding 4).
// Compiled only when the tree is configured with COLOSSEUM_NO_ACCOUNT_EDITION=ON.
// Drives the REAL AccountRuntime + ProfileStoreRuntime +
// FirstAccountProfileCoordinator through prepareForQml() with real profile
// ownership under a per-case tagged test-mode AppData (org/app names carry the
// tag, mirroring tst_account_attachment_runtime). A loopback listener sits at
// COLOSSEUM_ACCOUNT_SERVICE_URL and counts requests: the edition suppresses
// the endpoint, so every case must observe ZERO account-service requests.

#include "account/AccountBootstrapStore.h"
#include "account/AccountCredentialStore.h"
#include "account/AccountRuntime.h"
#include "account/ProfilePaths.h"
#include "../../support/account/MemoryAccountCredentialStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QtTest>

#include <memory>

namespace {

class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char *name)
        : m_name(name),
          m_wasSet(qEnvironmentVariableIsSet(name)),
          m_previous(qgetenv(name))
    {
    }

    ~ScopedEnvironmentVariable()
    {
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

// Counts any HTTP request that reaches the loopback endpoint. The edition
// must keep this at zero; a misbehaving restore/sync path would connect here.
class RequestCounterListener {
public:
    bool listen()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            QTcpServer *server = &m_server;
            while (QTcpSocket *socket = server->nextPendingConnection()) {
                ++m_connections;
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QTcpSocket::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    m_bytes += socket->readAll().size();
                    ++m_requests;
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                                  "Connection: close\r\n\r\n");
                });
            }
        });
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return m_server.serverPort(); }
    int connections() const { return m_connections; }
    int requests() const { return m_requests; }

private:
    QTcpServer m_server;
    int m_connections = 0;
    int m_requests = 0;
    int m_bytes = 0;
};

struct EditionHarness {
    ScopedEnvironmentVariable tag{"COLOSSEUM_APPDATA_TAG"};
    ScopedEnvironmentVariable endpoint{"COLOSSEUM_ACCOUNT_SERVICE_URL"};
    RequestCounterListener listener;
    MemoryAccountCredentialStore credentials;
    std::unique_ptr<AccountRuntime> runtime;
    std::unique_ptr<QQmlApplicationEngine> engine;

    // Per-case identity: the tag rides the application name (the same trick
    // tst_account_attachment_runtime uses), giving every case a private
    // test-mode AppData root and a private default-QSettings scope.
    EditionHarness(const char *tagValue)
    {
        QStandardPaths::setTestModeEnabled(true);
        const QByteArray tag = QByteArray(tagValue)
            + QByteArrayLiteral("-")
            + QByteArray::number(QCoreApplication::applicationPid());
        qputenv("COLOSSEUM_APPDATA_TAG", tag);
        QCoreApplication::setOrganizationName(
            QStringLiteral("Brotherhood-NoAccountEdition"));
        QCoreApplication::setApplicationName(
            QStringLiteral("Colosseum-%1").arg(QString::fromLatin1(tag)));
        if (!listener.listen())
            qFatal("no-account runtime harness: loopback listener failed");
        qputenv("COLOSSEUM_ACCOUNT_SERVICE_URL",
                QStringLiteral("http://127.0.0.1:%1").arg(listener.port()).toLatin1());

        QSettings settings;
        settings.remove(QStringLiteral("account"));
        settings.sync();
    }

    void prepare()
    {
        runtime = std::make_unique<AccountRuntime>(
            StremioSyncOptions{}, AccountRuntimeCredentialOptions{&credentials, {}});
        engine = std::make_unique<QQmlApplicationEngine>();
        runtime->prepareForQml(engine.get());
    }

    QObject *bound(const char *name) const
    {
        return engine->rootContext()->contextProperty(QString::fromLatin1(name))
            .value<QObject *>();
    }

    static QStringList profileFiles(const QString &root)
    {
        QStringList found;
        const auto entries =
            QDir(root).entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &info : entries)
            found << info.fileName();
        return found;
    }

    // Seed representative personal state through the REAL bound owners
    // (the same Progress/Collection/History instances QML uses), read it back
    // through those owners, and check values plus the canonical "mal:13"
    // identity — not file existence.
    void seedPersonalState()
    {
        QObject *progress = bound("Progress");
        QObject *collection = bound("Collection");
        QObject *history = bound("ProfileHistory");
        QVERIFY(progress && collection && history);

        QVariantMap progressEntry;
        progressEntry.insert(QStringLiteral("kind"), QStringLiteral("manga"));
        progressEntry.insert(QStringLiteral("id"), QStringLiteral("mal:13"));
        progressEntry.insert(QStringLiteral("title"), QStringLiteral("One Piece"));
        progressEntry.insert(QStringLiteral("position"), 42.0);
        QVERIFY(QMetaObject::invokeMethod(progress, "record",
                                          Q_ARG(QVariantMap, progressEntry)));

        QVariantMap collectionEntry;
        collectionEntry.insert(QStringLiteral("id"), QStringLiteral("mal:13"));
        collectionEntry.insert(QStringLiteral("title"), QStringLiteral("One Piece"));
        bool added = false;
        QVERIFY(QMetaObject::invokeMethod(collection, "add",
                                          Q_RETURN_ARG(bool, added),
                                          Q_ARG(QString, QStringLiteral("manga")),
                                          Q_ARG(QVariantMap, collectionEntry)));
        QVERIFY(added);

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(QMetaObject::invokeMethod(history, "recordActivity",
                                          Q_ARG(QString, QStringLiteral("manga")),
                                          Q_ARG(QString, QStringLiteral("mal:13")),
                                          Q_ARG(qint64, now)));
        bool marked = false;
        QVERIFY(QMetaObject::invokeMethod(history, "markCompleted",
                                          Q_RETURN_ARG(bool, marked),
                                          Q_ARG(QString, QStringLiteral("manga")),
                                          Q_ARG(QString, QStringLiteral("mal:13")),
                                          Q_ARG(qint64, now)));
        QVERIFY(marked);

        QMetaObject::invokeMethod(progress, "flush");
        QMetaObject::invokeMethod(collection, "flush");
    }

    void verifyPersonalState()
    {
        QVariantMap entry;
        QVERIFY(QMetaObject::invokeMethod(bound("Progress"), "get",
                                          Q_RETURN_ARG(QVariantMap, entry),
                                          Q_ARG(QString, QStringLiteral("manga")),
                                          Q_ARG(QString, QStringLiteral("mal:13"))));
        QCOMPARE(entry.value(QStringLiteral("id")).toString(),
                 QStringLiteral("mal:13"));
        QCOMPARE(entry.value(QStringLiteral("title")).toString(),
                 QStringLiteral("One Piece"));
        QCOMPARE(entry.value(QStringLiteral("position")).toDouble(), 42.0);

        bool has = false;
        QVERIFY(QMetaObject::invokeMethod(bound("Collection"), "has",
                                          Q_RETURN_ARG(bool, has),
                                          Q_ARG(QString, QStringLiteral("manga")),
                                          Q_ARG(QString, QStringLiteral("mal:13"))));
        QVERIFY(has);

        bool completed = false;
        QVERIFY(QMetaObject::invokeMethod(bound("ProfileHistory"), "completed",
                                          Q_RETURN_ARG(bool, completed),
                                          Q_ARG(QString, QStringLiteral("manga")),
                                          Q_ARG(QString, QStringLiteral("mal:13"))));
        QVERIFY(completed);
    }
};

}

class tst_no_account_runtime : public QObject {
    Q_OBJECT

private slots:
    void freshStartupSettlesLocalBindsStoresZeroSends();
    void legacyRoutePreservesRealPersonalStateAcrossRestart();
    void localOnlyAdoptedRoutePreservesRealPersonalState();
    void rememberedCredentialsArePreservedNotRestored();
    void pendingDeletionStaysPendingWithoutResume();
    void legacyAndAccountTreesArePreserved();
    void localProfileFailureSurfacesErrorAndRecovers();
};

void tst_no_account_runtime::
freshStartupSettlesLocalBindsStoresZeroSends() {
    EditionHarness harness("no-account-runtime-fresh");
    harness.prepare();

    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QVERIFY(!harness.runtime->controller()->onboardingRequired());
    QVERIFY(!harness.runtime->controller()->accountFeaturesAvailable());
    QCOMPARE(harness.runtime->controller()->lastErrorCode(), QString());

    // Real profile ownership through the real coordinator: a fresh install
    // settles onto the legacy-local profile (existing stores keep their
    // QSettings-based home) or the local-only profile — never sealed, never
    // an account profile.
    const ProfilePaths active =
        harness.runtime->profileStores()->activeProfile();
    QVERIFY2(active.kind() == ProfilePaths::Kind::LegacyLocal
                 || active.kind() == ProfilePaths::Kind::LocalOnly,
             "fresh no-account startup must settle a local profile");
    if (active.kind() == ProfilePaths::Kind::LocalOnly) {
        const QStringList files =
            EditionHarness::profileFiles(active.profileRoot());
        QVERIFY(files.contains(QStringLiteral("progress.ini")));
        QVERIFY(files.contains(QStringLiteral("collection.ini")));
        QVERIFY(files.contains(QStringLiteral("preferences.ini")));
        QVERIFY(files.contains(QStringLiteral("history.ini")));
    }

    // Every personal store stays bound for QML exactly as in the standard
    // edition — the ownership machinery AccountRuntime provides is intact.
    for (const char *name : {"Progress", "Collection", "SearchHistory",
                             "ProfilePreferences", "ProfileHistory",
                             "ProfileContext", "ProfileRuntime"}) {
        QVERIFY2(harness.bound(name), qPrintable(QString::fromLatin1(name)));
    }

    QCOMPARE(harness.listener.connections(), 0);
    QCOMPARE(harness.listener.requests(), 0);

    AccountBootstrapStore reopened;
    QVERIFY(reopened.onboardingCompleted());
    QVERIFY(reopened.localOnlyChosen());
}

void tst_no_account_runtime::
legacyRoutePreservesRealPersonalStateAcrossRestart() {
    EditionHarness harness("no-account-runtime-legacy-restart");

    // A fresh install settles onto the LEGACY-LOCAL profile explicitly (the
    // route a real no-account user's first startup takes).
    harness.prepare();
    QCOMPARE(harness.runtime->profileStores()->activeProfile().kind(),
             ProfilePaths::Kind::LegacyLocal);
    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));

    harness.seedPersonalState();

    harness.runtime.reset();
    harness.engine.reset();
    harness.prepare();

    QCOMPARE(harness.runtime->profileStores()->activeProfile().kind(),
             ProfilePaths::Kind::LegacyLocal);
    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QCOMPARE(harness.runtime->controller()->lastErrorCode(), QString());
    harness.verifyPersonalState();
    QCOMPARE(harness.listener.connections(), 0);
    QCOMPARE(harness.listener.requests(), 0);
}

void tst_no_account_runtime::
localOnlyAdoptedRoutePreservesRealPersonalState() {
    EditionHarness harness("no-account-runtime-localonly-restart");

    // LOCAL-ONLY route explicitly: a previously adopted profile (committed
    // adoption journal + existing promoted account root) makes
    // prepareLocalOnly activate the dedicated local-only profile instead of
    // reloading legacy stores. Seeded through the real ProfileAdoption
    // journal contract, not a coordinator stub.
    const QString accountId = QStringLiteral(
        "dddddddd-dddd-4ddd-8ddd-dddddddddddd");
    const auto accountPaths = ProfilePaths::account(accountId);
    QVERIFY(accountPaths.has_value());
    QVERIFY(QDir().mkpath(accountPaths->profileRoot()));

    // The journal must match the account profile exactly (ProfileAdoption
    // validates accountId/staging/final/backup roots and requires a
    // non-empty source digest), so seed it from the real ProfilePaths API.
    const QString adoptionRoot = QDir(ProfilePaths::localOnly().appDataRoot())
        .filePath(QStringLiteral("profile-adoption"));
    QVERIFY(QDir().mkpath(adoptionRoot));
    const QString journalJson = QStringLiteral(
        "{\"version\":1,\"state\":\"committed\",\"account_id\":\"%1\","
        "\"source_semantic_digest\":\"%2\","
        "\"target_semantic_digest\":\"\","
        "\"legacy_backup_semantic_digest\":\"\","
        "\"legacy_backup_root\":\"%3\","
        "\"staging_root\":\"%4\","
        "\"final_root\":\"%5\","
        "\"activity_source_digest\":\"\","
        "\"activity_target_digest\":\"\"}")
        .arg(accountId,
             QString(QStringLiteral("ab").repeated(32)),
             accountPaths->adoptionBackupRoot(),
             accountPaths->accountStagingRoot(),
             accountPaths->profileRoot());
    QFile journal(adoptionRoot + QStringLiteral("/") + accountId
                  + QStringLiteral(".json"));
    QVERIFY(journal.open(QIODevice::WriteOnly));
    journal.write(journalJson.toUtf8());
    journal.close();

    harness.prepare();
    QCOMPARE(harness.runtime->profileStores()->activeProfile().kind(),
             ProfilePaths::Kind::LocalOnly);
    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QCOMPARE(harness.runtime->controller()->lastErrorCode(), QString());

    harness.seedPersonalState();

    harness.runtime.reset();
    harness.engine.reset();
    harness.prepare();

    QCOMPARE(harness.runtime->profileStores()->activeProfile().kind(),
             ProfilePaths::Kind::LocalOnly);
    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QCOMPARE(harness.runtime->controller()->lastErrorCode(), QString());
    harness.verifyPersonalState();
    QCOMPARE(harness.listener.connections(), 0);
    QCOMPARE(harness.listener.requests(), 0);
}

void tst_no_account_runtime::
rememberedCredentialsArePreservedNotRestored() {
    EditionHarness harness("no-account-runtime-remembered");
    StoredAccountCredential credential;
    credential.accountId = QStringLiteral(
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    credential.deviceId = QStringLiteral(
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    credential.refreshToken = QByteArrayLiteral("refresh-fixture");
    QVERIFY(harness.credentials.saveActive(credential));

    QSettings settings;
    settings.setValue(QStringLiteral("account/rememberedAccountId"),
                      credential.accountId);
    settings.setValue(QStringLiteral("account/rememberedUsername"),
                      QStringLiteral("RememberedUser"));
    settings.sync();

    harness.prepare();

    // Not restored: the edition settles local-only; no session refresh was
    // attempted (which would have hit the loopback endpoint).
    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QCOMPARE(harness.runtime->controller()->username(), QString());
    QCOMPARE(harness.listener.connections(), 0);
    QCOMPARE(harness.listener.requests(), 0);

    // Preserved for the later account edition: the vault entry is untouched.
    const auto stillStored = harness.credentials.loadActive();
    QVERIFY(stillStored.has_value());
    QCOMPARE(stillStored->accountId, credential.accountId);
    QCOMPARE(stillStored->refreshToken, credential.refreshToken);
}

void tst_no_account_runtime::
pendingDeletionStaysPendingWithoutResume() {
    EditionHarness harness("no-account-runtime-pending-deletion");
    StoredAccountDeletion deletion;
    deletion.accountId = QStringLiteral(
        "cccccccc-cccc-4ccc-8ccc-cccccccccccc");
    deletion.requestId = QStringLiteral("deletion-fixture-1");
    deletion.retryCapability = QByteArrayLiteral("retry");
    QVERIFY(harness.credentials.savePendingDeletion(deletion));

    harness.prepare();

    // The edition never resumes a pending deletion (that flow talks to the
    // account service); the request stays queued for the account edition.
    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QCOMPARE(harness.listener.connections(), 0);
    QCOMPARE(harness.listener.requests(), 0);
    const auto pending = harness.credentials.pendingDeletions();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().requestId, deletion.requestId);
}

void tst_no_account_runtime::
legacyAndAccountTreesArePreserved() {
    EditionHarness harness("no-account-runtime-preserve-trees");

    // A previously signed-in user's account profile tree and the legacy
    // storage root must survive edition startups untouched — the later
    // account edition owns them again.
    const auto accountPaths = ProfilePaths::account(
        QStringLiteral("dddddddd-dddd-4ddd-8ddd-dddddddddddd"));
    QVERIFY(accountPaths.has_value());
    const QString accountRoot = accountPaths->profileRoot();
    QVERIFY(QDir().mkpath(accountRoot));
    QFile accountMarker(accountRoot + QStringLiteral("/marker"));
    QVERIFY(accountMarker.open(QIODevice::WriteOnly));
    accountMarker.write("account");
    accountMarker.close();

    harness.prepare();

    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QVERIFY(QFile::exists(accountRoot + QStringLiteral("/marker")));
    QCOMPARE(harness.listener.connections(), 0);
}

void tst_no_account_runtime::
localProfileFailureSurfacesErrorAndRecovers() {
    EditionHarness harness("no-account-runtime-prepare-failure");

    // Deterministic REAL-coordinator failure: a corrupt profile-adoption
    // journal makes legacyPersonalStateClaimed() fail inside
    // FirstAccountProfileCoordinator::prepareLocalOnly(), which is exactly
    // the prepare path prepareForQml drives in this edition. The failure
    // must surface as a terminal error state — hiding onboarding in this
    // edition must not conceal a storage failure — and must not touch the
    // account service.
    const QString adoptionRoot = QDir(ProfilePaths::localOnly().appDataRoot())
        .filePath(QStringLiteral("profile-adoption"));
    QVERIFY(QDir().mkpath(adoptionRoot));
    QFile journal(
        adoptionRoot + QStringLiteral(
            "/eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee.json"));
    QVERIFY(journal.open(QIODevice::WriteOnly));
    journal.write("not-a-journal");
    journal.close();

    harness.prepare();

    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("error"));
    QCOMPARE(harness.runtime->controller()->lastErrorCode(),
             QStringLiteral("local_profile_prepare_failed"));
    QVERIFY(harness.runtime->controller()->onboardingRequired());
    AccountBootstrapStore failedBootstrap;
    QVERIFY(!failedBootstrap.onboardingCompleted());
    QCOMPARE(harness.listener.connections(), 0);
    QCOMPARE(harness.listener.requests(), 0);

    // Recovery: removing the corrupt journal and re-running the same settle
    // path the visible error surface's retry offers returns to a healthy
    // local-only startup.
    QVERIFY(journal.remove());
    harness.runtime->controller()->continueWithoutAccount();
    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QVERIFY(!harness.runtime->controller()->onboardingRequired());
    QCOMPARE(harness.listener.connections(), 0);
}

QTEST_MAIN(tst_no_account_runtime)
#include "tst_no_account_runtime.moc"
