// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/HistoryStore.h"
#include "account/LegacyPersonalStateStorage.h"
#include "account/ProfilePaths.h"
#include "account/ProfilePreferencesStore.h"
#include "account/ProfileStoreRuntime.h"
#include "account/SharedPcProfileCoordinator.h"

#include "SearchHistoryStore.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto kAccountA =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kAccountB =
    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";

PersonalStateSnapshot snapshotWithHistory(
    const QString &id,
    qint64 completedAt) {
    PersonalStateSnapshot snapshot;

    QJsonObject record;
    record.insert(
        QStringLiteral("kind"),
        QStringLiteral("movie"));
    record.insert(
        QStringLiteral("id"),
        id);
    record.insert(
        QStringLiteral("firstActivityAt"),
        completedAt);
    record.insert(
        QStringLiteral("lastActivityAt"),
        completedAt);
    record.insert(
        QStringLiteral("completedAt"),
        completedAt);

    snapshot.historyRecords.insert(
        QStringLiteral("movie")
            + QChar(0x1f)
            + id,
        record);

    return snapshot;
}

PersonalStateSnapshot snapshotWithSearch(
    const QString &query,
    bool showExplicit) {
    PersonalStateSnapshot snapshot;

    QJsonArray search;
    search.append(query);
    snapshot.searchHistory.insert(
        QStringLiteral("manga"),
        search);
    snapshot.showExplicit =
        showExplicit;
    return snapshot;
}

struct SharedPcFixture {
    QTemporaryDir temp;
    QString legacyRoot;
    QString appDataRoot;
    LegacyPersonalStateStorage legacy;

    SharedPcFixture()
        : legacyRoot(
              QDir(temp.path()).filePath(
                  QStringLiteral("legacy"))),
          appDataRoot(
              QDir(temp.path()).filePath(
                  QStringLiteral("appdata"))),
          legacy(
              LegacyPersonalStateStorage::isolated(
                  legacyRoot)) {
        if (!temp.isValid())
            qFatal("Could not create shared-PC fixture.");
        QDir().mkpath(legacyRoot);
        QDir().mkpath(appDataRoot);
    }

    ProfilePaths paths(
        const QString &accountId) const {
        const auto result =
            ProfilePaths::account(
                accountId,
                appDataRoot);
        if (!result.has_value())
            qFatal("Fixture account id is invalid.");
        return *result;
    }

    void seedAccount(
        const QString &accountId,
        const PersonalStateSnapshot &snapshot) {
        const ProfilePaths profile =
            paths(accountId);
        if (!QDir().mkpath(
                profile.profileRoot())) {
            qFatal("Could not create fixture profile.");
        }

        QString error;
        const auto storage =
            LegacyPersonalStateStorage::forProfile(
                profile,
                &error);
        if (!storage.has_value()
            || !storage->restorePersonalState(
                snapshot,
                &error)) {
            qFatal(
                "Could not seed fixture account profile.");
        }
    }
};

SearchHistoryStore *searchHistory(
    QQmlApplicationEngine *engine) {
    QObject *object =
        engine->rootContext()
            ->contextProperty(
                QStringLiteral("SearchHistory"))
            .value<QObject *>();
    return qobject_cast<SearchHistoryStore *>(
        object);
}


HistoryStore *profileHistory(
    QQmlApplicationEngine *engine) {
    QObject *object =
        engine->rootContext()
            ->contextProperty(
                QStringLiteral("ProfileHistory"))
            .value<QObject *>();
    return qobject_cast<HistoryStore *>(
        object);
}

ProfilePreferencesStore *profilePreferences(
    QQmlApplicationEngine *engine) {
    QObject *object =
        engine->rootContext()
            ->contextProperty(
                QStringLiteral("ProfilePreferences"))
            .value<QObject *>();
    return qobject_cast<ProfilePreferencesStore *>(
        object);
}
}

class tst_account_shared_pc : public QObject {
    Q_OBJECT

private slots:
    void signedOutRuntimeStartsSealedAndHidesLegacyPersonalState();
    void accountAToBToASealsConcreteStoreObjects();
    void searchHistoryNeverCrossesAccountBoundary();
    void historyNeverCrossesAccountBoundary();
    void directCrossAccountOpenFailsUntilCurrentProfileIsSealed();
    void rememberedAccountRefusesMissingLocalProfile();
    void sealedAccountCanReopenOnlyThroughExplicitSessionPreparation();
};

void tst_account_shared_pc::
signedOutRuntimeStartsSealedAndHidesLegacyPersonalState() {
    SharedPcFixture fixture;

    const PersonalStateSnapshot legacyState =
        snapshotWithSearch(
            QStringLiteral("legacy-private-query"),
            true);
    QVERIFY(
        fixture.legacy.restorePersonalState(
            legacyState));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(
        &engine);

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Sealed);
    QCOMPARE(
        runtime.activeProfile().appDataRoot(),
        QDir::cleanPath(
            QFileInfo(
                fixture.appDataRoot)
                .absoluteFilePath()));

    QDir sessionRoot(
        QDir(fixture.appDataRoot)
            .filePath(
                QStringLiteral("profile-session")));
    const QStringList sealedDirectories =
        sessionRoot.entryList(
            QStringList()
                << QStringLiteral("sealed-*"),
            QDir::Dirs
                | QDir::NoDotAndDotDot,
            QDir::Name);
    QCOMPARE(sealedDirectories.size(), 1);

    SearchHistoryStore *sealedSearch =
        searchHistory(&engine);
    QVERIFY(sealedSearch);
    QVERIFY(
        sealedSearch
            ->list(QStringLiteral("manga"))
            .isEmpty());

    QString error;
    const auto legacyReadback =
        fixture.legacy.capture(
            &error);
    QVERIFY2(
        legacyReadback.has_value(),
        qPrintable(error));
    QCOMPARE(
        legacyReadback->semanticDigest(),
        legacyState.semanticDigest());
}

void tst_account_shared_pc::
accountAToBToASealsConcreteStoreObjects() {
    SharedPcFixture fixture;
    fixture.seedAccount(
        QString::fromLatin1(kAccountA),
        snapshotWithSearch(
            QStringLiteral("account-a-query"),
            true));
    fixture.seedAccount(
        QString::fromLatin1(kAccountB),
        snapshotWithSearch(
            QStringLiteral("account-b-query"),
            false));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(
        &engine);

    QString error;
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().profileId(),
        fixture.paths(
            QString::fromLatin1(kAccountA))
            .profileId());

    SearchHistoryStore *accountA =
        searchHistory(&engine);
    QVERIFY(accountA);
    QCOMPARE(
        accountA->list(
            QStringLiteral("manga")),
        QStringList()
            << QStringLiteral(
                "account-a-query"));
    QVERIFY(profilePreferences(&engine));
    QVERIFY(
        profilePreferences(&engine)
            ->showExplicit());

    QPointer<SearchHistoryStore> oldAccountA(
        accountA);

    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Sealed);
    QVERIFY(oldAccountA.isNull());

    SearchHistoryStore *sealed =
        searchHistory(&engine);
    QVERIFY(sealed);
    QVERIFY(
        sealed->list(
            QStringLiteral("manga"))
            .isEmpty());
    QVERIFY(profilePreferences(&engine));
    QVERIFY(
        !profilePreferences(&engine)
             ->showExplicit());

    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountB),
            &error),
        qPrintable(error));

    SearchHistoryStore *accountB =
        searchHistory(&engine);
    QVERIFY(accountB);
    QVERIFY(accountB != sealed);
    QCOMPARE(
        accountB->list(
            QStringLiteral("manga")),
        QStringList()
            << QStringLiteral(
                "account-b-query"));
    QVERIFY(profilePreferences(&engine));
    QVERIFY(
        !profilePreferences(&engine)
             ->showExplicit());

    QPointer<SearchHistoryStore> oldAccountB(
        accountB);

    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(kAccountB),
            &error),
        qPrintable(error));
    QVERIFY(oldAccountB.isNull());

    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    SearchHistoryStore *accountAReopened =
        searchHistory(&engine);
    QVERIFY(accountAReopened);
    QCOMPARE(
        accountAReopened->list(
            QStringLiteral("manga")),
        QStringList()
            << QStringLiteral(
                "account-a-query"));
    QVERIFY(profilePreferences(&engine));
    QVERIFY(
        profilePreferences(&engine)
            ->showExplicit());
}

void tst_account_shared_pc::
searchHistoryNeverCrossesAccountBoundary() {
    SharedPcFixture fixture;
    fixture.seedAccount(
        QString::fromLatin1(kAccountA),
        snapshotWithSearch(
            QStringLiteral("private-a"),
            false));
    fixture.seedAccount(
        QString::fromLatin1(kAccountB),
        snapshotWithSearch(
            QStringLiteral("private-b"),
            false));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(
        &engine);

    QString error;
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    SearchHistoryStore *accountA =
        searchHistory(&engine);
    QVERIFY(accountA);
    QVERIFY(
        accountA
            ->list(QStringLiteral("manga"))
            .contains(
                QStringLiteral("private-a")));
    QVERIFY(
        !accountA
             ->list(QStringLiteral("manga"))
             .contains(
                 QStringLiteral("private-b")));

    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountB),
            &error),
        qPrintable(error));

    SearchHistoryStore *accountB =
        searchHistory(&engine);
    QVERIFY(accountB);
    QVERIFY(
        accountB
            ->list(QStringLiteral("manga"))
            .contains(
                QStringLiteral("private-b")));
    QVERIFY(
        !accountB
             ->list(QStringLiteral("manga"))
             .contains(
                 QStringLiteral("private-a")));
}

void tst_account_shared_pc::
historyNeverCrossesAccountBoundary() {
    SharedPcFixture fixture;
    fixture.seedAccount(
        QString::fromLatin1(kAccountA),
        snapshotWithHistory(
            QStringLiteral("history-a"),
            1000));
    fixture.seedAccount(
        QString::fromLatin1(kAccountB),
        snapshotWithHistory(
            QStringLiteral("history-b"),
            2000));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(
        &engine);

    QString error;
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    HistoryStore *accountA =
        profileHistory(&engine);
    QVERIFY(accountA);
    QVERIFY(
        !accountA
             ->get(
                 QStringLiteral("movie"),
                 QStringLiteral("history-a"))
             .isEmpty());
    QVERIFY(
        accountA
            ->get(
                QStringLiteral("movie"),
                QStringLiteral("history-b"))
            .isEmpty());

    QPointer<HistoryStore> oldAccountA(
        accountA);

    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QVERIFY(oldAccountA.isNull());

    HistoryStore *sealed =
        profileHistory(&engine);
    QVERIFY(sealed);
    QVERIFY(sealed->records().isEmpty());

    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountB),
            &error),
        qPrintable(error));

    HistoryStore *accountB =
        profileHistory(&engine);
    QVERIFY(accountB);
    QVERIFY(accountB != sealed);
    QVERIFY(
        !accountB
             ->get(
                 QStringLiteral("movie"),
                 QStringLiteral("history-b"))
             .isEmpty());
    QVERIFY(
        accountB
            ->get(
                QStringLiteral("movie"),
                QStringLiteral("history-a"))
            .isEmpty());
}

void tst_account_shared_pc::
directCrossAccountOpenFailsUntilCurrentProfileIsSealed() {
    SharedPcFixture fixture;
    fixture.seedAccount(
        QString::fromLatin1(kAccountA),
        snapshotWithSearch(
            QStringLiteral("a"),
            false));
    fixture.seedAccount(
        QString::fromLatin1(kAccountB),
        snapshotWithSearch(
            QStringLiteral("b"),
            false));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    error.clear();
    QVERIFY(
        !profiles.prepareAccountSession(
            QString::fromLatin1(kAccountB),
            &error));
    QVERIFY(
        error.contains(
            QStringLiteral("sealed")));

    QCOMPARE(
        runtime.activeProfile().profileId(),
        fixture.paths(
            QString::fromLatin1(kAccountA))
            .profileId());
}

void tst_account_shared_pc::
rememberedAccountRefusesMissingLocalProfile() {
    SharedPcFixture fixture;

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY(
        !profiles.prepareRememberedAccount(
            QString::fromLatin1(kAccountA),
            &error));
    QVERIFY(
        error.contains(
            QStringLiteral("missing")));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Sealed);
}

void tst_account_shared_pc::
sealedAccountCanReopenOnlyThroughExplicitSessionPreparation() {
    SharedPcFixture fixture;
    fixture.seedAccount(
        QString::fromLatin1(kAccountA),
        snapshotWithSearch(
            QStringLiteral("reopen-a"),
            false));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(
        &engine);

    QString error;
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Sealed);
    QVERIFY(
        searchHistory(&engine)
            ->list(QStringLiteral("manga"))
            .isEmpty());

    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    QCOMPARE(
        searchHistory(&engine)
            ->list(QStringLiteral("manga")),
        QStringList()
            << QStringLiteral("reopen-a"));
}

QTEST_MAIN(tst_account_shared_pc)
#include "tst_account_shared_pc.moc"
