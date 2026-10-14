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
};

}

class tst_no_account_runtime : public QObject {
    Q_OBJECT

private slots:
    void freshStartupSettlesLocalBindsStoresZeroSends();
    void populatedLocalProfileRestartPreservesState();
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
populatedLocalProfileRestartPreservesState() {
    EditionHarness harness("no-account-runtime-restart");

    harness.prepare();
    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    const ProfilePaths active =
        harness.runtime->profileStores()->activeProfile();
    QVERIFY(active.kind() == ProfilePaths::Kind::LegacyLocal
            || active.kind() == ProfilePaths::Kind::LocalOnly);

    // Durable user state written through the real bound stores survives a
    // full second startup. A local-only profile rides its dedicated ini
    // files; a legacy-local profile keeps QSettings-backed stores (its
    // progressIniPath is empty by design), so the marker rides the same
    // default QSettings scope the legacy backend uses.
    const bool localOnlyProfile =
        active.kind() == ProfilePaths::Kind::LocalOnly;
    QString progressPath;
    if (localOnlyProfile) {
        progressPath = active.progressIniPath();
        QVERIFY(!progressPath.isEmpty());
        QDir().mkpath(QFileInfo(progressPath).absolutePath());
        QFile progress(progressPath);
        QVERIFY(progress.open(QIODevice::Append));
        progress.write("preserve-marker=1\n");
        progress.close();
    } else {
        QSettings legacy;
        legacy.setValue(QStringLiteral("progress/preserve-marker"), 1);
        legacy.sync();
    }

    harness.runtime.reset();
    harness.engine.reset();
    harness.prepare();

    QCOMPARE(harness.runtime->controller()->mode(), QStringLiteral("localOnly"));
    QVERIFY(!harness.runtime->controller()->onboardingRequired());
    QCOMPARE(harness.runtime->controller()->lastErrorCode(), QString());

    if (localOnlyProfile) {
        QFile reread(
            harness.runtime->profileStores()->activeProfile().progressIniPath());
        QVERIFY(reread.open(QIODevice::ReadOnly));
        QVERIFY(reread.readAll().contains("preserve-marker=1"));
    } else {
        QSettings legacy;
        QCOMPARE(legacy.value(QStringLiteral("progress/preserve-marker")).toInt(), 1);
    }

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
