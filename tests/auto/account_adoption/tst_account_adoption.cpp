// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/ActivityStore.h"
#include "account/FirstAccountProfileCoordinator.h"
#include "account/LegacyPersonalStateStorage.h"
#include "account/ProfileAdoption.h"
#include "account/ProfilePaths.h"
#include "account/ProfileStoreRuntime.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto kAccountA =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kAccountB =
    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";

PersonalStateSnapshot populatedSnapshot() {
    PersonalStateSnapshot snapshot;

    QJsonObject progress;
    progress.insert(
        QStringLiteral("id"),
        QStringLiteral("series-1"));
    progress.insert(
        QStringLiteral("kind"),
        QStringLiteral("manga"));
    progress.insert(
        QStringLiteral("caption"),
        QStringLiteral("Chapter 9"));
    progress.insert(
        QStringLiteral("progress"),
        0.45);
    progress.insert(
        QStringLiteral("updatedAt"),
        1720000000000.0);
    snapshot.progressEntries.insert(
        QStringLiteral("manga\x1fseries-1"),
        progress);

    snapshot.progressLastSeason.insert(
        QStringLiteral("show-1"),
        3);
    snapshot.progressWatchedMarks.insert(
        QStringLiteral("show-1"),
        -1);

    QJsonObject collection;
    collection.insert(
        QStringLiteral("id"),
        QStringLiteral("series-1"));
    collection.insert(
        QStringLiteral("world"),
        QStringLiteral("Tankoban"));
    collection.insert(
        QStringLiteral("type"),
        QStringLiteral("manga"));
    collection.insert(
        QStringLiteral("title"),
        QStringLiteral("Fixture Manga"));
    collection.insert(
        QStringLiteral("addedAt"),
        1720000001000.0);
    snapshot.collectionEntries.insert(
        QStringLiteral("Tankoban\x1fseries-1"),
        collection);

    QJsonArray mangaSearch;
    mangaSearch.append(
        QStringLiteral("berserk"));
    mangaSearch.append(
        QStringLiteral("vagabond"));
    snapshot.searchHistory.insert(
        QStringLiteral("manga"),
        mangaSearch);

    QJsonObject pairing;
    pairing.insert(
        QStringLiteral("bookId"),
        QStringLiteral("book-1"));
    pairing.insert(
        QStringLiteral("audiobookId"),
        QStringLiteral("audio-1"));
    pairing.insert(
        QStringLiteral("mappings"),
        QJsonArray());
    pairing.insert(
        QStringLiteral("updatedAt"),
        1720000002000.0);
    snapshot.audioPairings.insert(
        QStringLiteral("book-1"),
        pairing);

    QJsonObject history;
    history.insert(
        QStringLiteral("kind"),
        QStringLiteral("manga"));
    history.insert(
        QStringLiteral("id"),
        QStringLiteral("series-1"));
    history.insert(
        QStringLiteral("firstActivityAt"),
        1720000000500.0);
    history.insert(
        QStringLiteral("lastActivityAt"),
        1720000002500.0);
    history.insert(
        QStringLiteral("completedAt"),
        1720000002500.0);
    snapshot.historyRecords.insert(
        QStringLiteral("manga")
            + QChar(0x1f)
            + QStringLiteral("series-1"),
        history);

    snapshot.showExplicit = true;
    return snapshot;
}

struct AdoptionFixture {
    QTemporaryDir temp;
    QString legacyRoot;
    QString appDataRoot;
    LegacyPersonalStateStorage legacy;

    AdoptionFixture()
        : legacyRoot(
              QDir(temp.path())
                  .filePath(
                      QStringLiteral("legacy"))),
          appDataRoot(
              QDir(temp.path())
                  .filePath(
                      QStringLiteral("appdata"))),
          legacy(
              LegacyPersonalStateStorage::isolated(
                  legacyRoot)) {
        if (!temp.isValid())
            qFatal("Could not create account adoption test directory.");
        QDir().mkpath(legacyRoot);
        QDir().mkpath(appDataRoot);
    }

    ProfilePaths accountPaths(
        const QString &accountId =
            QString::fromLatin1(kAccountA)) const {
        const auto paths =
            ProfilePaths::account(
                accountId,
                appDataRoot);
        if (!paths.has_value())
            qFatal("Fixture account id was invalid.");
        return *paths;
    }
};

void seedMachineSentinels(
    const LegacyPersonalStateStorage &legacy) {
    QSettings progress(
        legacy.progressIniPath(),
        QSettings::IniFormat);
    progress.setValue(
        QStringLiteral("session/machineSentinel"),
        QStringLiteral("keep-progress-machine-state"));
    progress.sync();

    QSettings preferences(
        legacy.preferencesIniPath(),
        QSettings::IniFormat);
    preferences.setValue(
        QStringLiteral("machine/windowGeometry"),
        QByteArrayLiteral("keep-window-state"));
    preferences.sync();
}

// A single valid playback_delta fact — 30 seconds of movie playback, the
// projector's own per-event activeMs cap (ActivityProjector.cpp) — used to
// seed a legacy activity ledger the same way a real playback session would.
// Field set mirrors tests/auto/activity/tst_activity_store.cpp's own fixture
// builders; duplicated here deliberately, same reasoning as that file's own
// compareJson() note: this is test infrastructure, not activity-engine logic.
QVariantMap fixtureMovieFact() {
    QVariantMap fact;
    fact.insert(QStringLiteral("sessionId"), QStringLiteral("adoption-fixture-session"));
    fact.insert(QStringLiteral("world"), QStringLiteral("theatre"));
    fact.insert(QStringLiteral("kind"), QStringLiteral("movie"));
    fact.insert(QStringLiteral("titleKey"), QStringLiteral("theatre:adoption-fixture-movie"));
    fact.insert(QStringLiteral("itemKey"), QStringLiteral("adoption-fixture-movie"));
    fact.insert(QStringLiteral("title"), QStringLiteral("Adoption Fixture Movie"));
    fact.insert(QStringLiteral("itemLabel"), QString());
    fact.insert(QStringLiteral("cover"), QString());
    fact.insert(QStringLiteral("utcOffsetMinutes"), qint64(330));
    fact.insert(QStringLiteral("syncable"), true);
    fact.insert(QStringLiteral("source"), QStringLiteral("test"));
    fact.insert(QStringLiteral("startAtMs"), qint64(1720000000000));
    fact.insert(QStringLiteral("endAtMs"), qint64(1720000030000));
    fact.insert(QStringLiteral("activeMs"), qint64(30000));
    fact.insert(QStringLiteral("rateMilli"), qint64(1000));
    return fact;
}

QVariantMap fixtureMovieCompletionFact() {
    QVariantMap fact;
    fact.insert(QStringLiteral("eventId"), QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc"));
    fact.insert(QStringLiteral("sessionId"), QStringLiteral("adoption-completion-session"));
    fact.insert(QStringLiteral("world"), QStringLiteral("theatre"));
    fact.insert(QStringLiteral("kind"), QStringLiteral("movie"));
    fact.insert(QStringLiteral("titleKey"), QStringLiteral("theatre:adoption-completion-movie"));
    fact.insert(QStringLiteral("itemKey"), QStringLiteral("adoption-completion-movie"));
    fact.insert(QStringLiteral("title"), QStringLiteral("Adoption Completion Movie"));
    fact.insert(QStringLiteral("itemLabel"), QString());
    fact.insert(QStringLiteral("cover"), QString());
    fact.insert(QStringLiteral("utcOffsetMinutes"), qint64(330));
    fact.insert(QStringLiteral("syncable"), true);
    fact.insert(QStringLiteral("source"), QStringLiteral("test"));
    fact.insert(QStringLiteral("atMs"), qint64(1720000030000));
    fact.insert(QStringLiteral("reason"), QStringLiteral("eof"));
    return fact;
}

void verifyMachineSentinels(
    const LegacyPersonalStateStorage &legacy) {
    QSettings progress(
        legacy.progressIniPath(),
        QSettings::IniFormat);
    QCOMPARE(
        progress.value(
            QStringLiteral("session/machineSentinel"))
            .toString(),
        QStringLiteral("keep-progress-machine-state"));

    QSettings preferences(
        legacy.preferencesIniPath(),
        QSettings::IniFormat);
    QCOMPARE(
        preferences.value(
            QStringLiteral("machine/windowGeometry"))
            .toByteArray(),
        QByteArrayLiteral("keep-window-state"));
}
}

class tst_account_adoption : public QObject {
    Q_OBJECT

private slots:
    void populatedFirstAccountQuarantinesOnlyAfterSemanticVerification();
    void cleanRestartCommitsQuarantinedAdoption();
    void committedAccountSessionMergesResidualLocalOnlyState();
    void ordinarySignInAdoptsLegacyLocalState();
    void ordinarySignInMergesExistingAccountWithLocalOnlyState();
    void activeAccountSessionMergesLaterLocalOnlyState();
    void rememberedAccountSessionMergesLaterLocalOnlyState();
    void continueLocalBeforeAdoptionKeepsLegacyAuthority();
    void continueLocalAfterAdoptionUsesDedicatedLocalProfile();
    void corruptRestartRestoresLegacyAndLeavesRetryIntent();
    void retryIntentReAdoptsOnLaterSignIn();
    void legacySnapshotV1RemainsReadableWithoutHistory();
    void directAccountSwitchRequiresSealing();

    void existingAccountMergeAcceptsCompletedActivity();
    void activityOnlyLocalStateIsMergedIntoExistingAccount();
    void firstAccountAdoptionMigratesActivityLedger();
    void interruptedAdoptionRestoresLegacyActivityLedger();
};

void tst_account_adoption::
legacySnapshotV1RemainsReadableWithoutHistory() {
    PersonalStateSnapshot source =
        populatedSnapshot();

    QJsonObject legacy =
        source.toJson();
    legacy.insert(
        QStringLiteral("version"),
        1);
    legacy.remove(
        QStringLiteral(
            "history_records"));

    QString error;
    const auto parsed =
        PersonalStateSnapshot::
            fromJson(
                legacy,
                &error);

    QVERIFY2(
        parsed.has_value(),
        qPrintable(error));
    QVERIFY(
        parsed->historyRecords.isEmpty());
    QVERIFY(
        parsed->matchesSemanticDigest(
            parsed->legacySemanticDigestV1()));
    QVERIFY(
        parsed->semanticDigest()
        != parsed->legacySemanticDigestV1());
    QCOMPARE(
        parsed->collectionEntries,
        source.collectionEntries);
    QCOMPARE(
        parsed->progressEntries,
        source.progressEntries);
}

void tst_account_adoption::
populatedFirstAccountQuarantinesOnlyAfterSemanticVerification() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();

    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));
    seedMachineSentinels(fixture.legacy);

    const QString mediaPath =
        QDir(fixture.appDataRoot)
            .filePath(
                QStringLiteral("Vault/fixture.cbz"));
    QVERIFY(
        QDir().mkpath(
            QFileInfo(mediaPath)
                .absolutePath()));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QCOMPARE(
        media.write(
            QByteArrayLiteral("MEDIA-SENTINEL")),
        qint64(14));
    media.close();

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareCreatedAccount(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const ProfilePaths paths =
        fixture.accountPaths();

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);
    QCOMPARE(
        runtime.activeProfile().profileId(),
        paths.profileId());

    const auto legacyAfter =
        fixture.legacy.capture(&error);
    QVERIFY2(
        legacyAfter.has_value(),
        qPrintable(error));
    QVERIFY(legacyAfter->isEmpty());

    const auto profileStorage =
        LegacyPersonalStateStorage::forProfile(
            paths,
            &error);
    QVERIFY2(
        profileStorage.has_value(),
        qPrintable(error));

    const auto profileAfter =
        profileStorage->capture(&error);
    QVERIFY2(
        profileAfter.has_value(),
        qPrintable(error));
    QCOMPARE(
        profileAfter->semanticDigest(),
        source.semanticDigest());

    const auto adoption =
        ProfileAdoption::open(
            paths,
            &error);
    QVERIFY2(
        adoption.has_value(),
        qPrintable(error));
    QCOMPARE(
        adoption->state(),
        ProfileAdoption::State::LegacyQuarantined);

    QVERIFY(
        QFileInfo::exists(
            QDir(paths.adoptionBackupRoot())
                .filePath(
                    QStringLiteral(
                        "personal-state.json"))));

    verifyMachineSentinels(fixture.legacy);

    QFile mediaRead(mediaPath);
    QVERIFY(mediaRead.open(QIODevice::ReadOnly));
    QCOMPARE(
        mediaRead.readAll(),
        QByteArrayLiteral("MEDIA-SENTINEL"));
}

void tst_account_adoption::
cleanRestartCommitsQuarantinedAdoption() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    const ProfilePaths paths =
        fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto adoption =
            ProfileAdoption::open(
                paths,
                &error);
        QVERIFY2(
            adoption.has_value(),
            qPrintable(error));
        QCOMPARE(
            adoption->state(),
            ProfileAdoption::State::LegacyQuarantined);
    }

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto adoption =
            ProfileAdoption::open(
                paths,
                &error);
        QVERIFY2(
            adoption.has_value(),
            qPrintable(error));
        QCOMPARE(
            adoption->state(),
            ProfileAdoption::State::Committed);

        QCOMPARE(
            runtime.activeProfile().profileId(),
            paths.profileId());
    }
}

void tst_account_adoption::
committedAccountSessionMergesResidualLocalOnlyState() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    const ProfilePaths paths =
        fixture.accountPaths();
    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }
    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto adoption =
            ProfileAdoption::open(
                paths,
                &error);
        QVERIFY2(
            adoption.has_value(),
            qPrintable(error));
        QCOMPARE(
            adoption->state(),
            ProfileAdoption::State::Committed);

        PersonalStateSnapshot residual;
        residual.progressEntries.insert(
            QStringLiteral("movie\x1fresidual-local-movie"),
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("residual-local-movie")},
                {QStringLiteral("kind"), QStringLiteral("movie")},
                {QStringLiteral("progress"), 0.6},
                {QStringLiteral("updatedAt"), 1720000006000.0}});

        const ProfilePaths localPaths =
            ProfilePaths::localOnly(fixture.appDataRoot);
        const auto localStorage =
            LegacyPersonalStateStorage::forProfile(
                localPaths,
                &error);
        QVERIFY2(
            localStorage.has_value(),
            qPrintable(error));
        QVERIFY2(
            localStorage->restorePersonalState(
                residual,
                &error),
            qPrintable(error));

        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto accountStorage =
            LegacyPersonalStateStorage::forProfile(
                paths,
                &error);
        QVERIFY2(
            accountStorage.has_value(),
            qPrintable(error));
        const auto merged =
            accountStorage->capture(&error);
        QVERIFY2(
            merged.has_value(),
            qPrintable(error));
        QVERIFY(
            merged->progressEntries.contains(
                QStringLiteral("movie\x1fresidual-local-movie")));

        const auto localAfter =
            localStorage->capture(&error);
        QVERIFY2(
            localAfter.has_value(),
            qPrintable(error));
        QVERIFY(localAfter->isEmpty());
    }
}

void tst_account_adoption::
ordinarySignInAdoptsLegacyLocalState() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const auto legacyAfter =
        fixture.legacy.capture(&error);
    QVERIFY2(
        legacyAfter.has_value(),
        qPrintable(error));
    QVERIFY(legacyAfter->isEmpty());

    const ProfilePaths paths =
        fixture.accountPaths();
    const auto profileStorage =
        LegacyPersonalStateStorage::forProfile(
            paths,
            &error);
    QVERIFY2(
        profileStorage.has_value(),
        qPrintable(error));

    const auto accountState =
        profileStorage->capture(&error);
    QVERIFY2(
        accountState.has_value(),
        qPrintable(error));
    QCOMPARE(
        accountState->semanticDigest(),
        source.semanticDigest());
    QVERIFY(QFileInfo::exists(paths.adoptionJournalPath()));
}

void tst_account_adoption::
ordinarySignInMergesExistingAccountWithLocalOnlyState() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot accountState = populatedSnapshot();
    PersonalStateSnapshot localState = populatedSnapshot();
    localState.progressEntries.insert(
        QStringLiteral("movie\x1fmovie-2"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("movie-2")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("progress"), 0.8},
            {QStringLiteral("updatedAt"), 1720000003000.0}});
    localState.collectionEntries.insert(
        QStringLiteral("Theatre\x1fmovie-2"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("movie-2")},
            {QStringLiteral("world"), QStringLiteral("Theatre")},
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("Fixture Movie")}});

    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(localPaths);
    QVERIFY(localStorage.has_value());
    QVERIFY(localStorage->restorePersonalState(localState));

    const ProfilePaths accountPaths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(accountPaths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(accountPaths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(accountState));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    const auto merged =
        accountStorage->capture(&error);
    QVERIFY2(merged.has_value(), qPrintable(error));
    QVERIFY(merged->progressEntries.contains(QStringLiteral("movie\x1fmovie-2")));
    QVERIFY(merged->collectionEntries.contains(QStringLiteral("Theatre\x1fmovie-2")));

    const auto localAfter =
        localStorage->capture(&error);
    QVERIFY2(localAfter.has_value(), qPrintable(error));
    QVERIFY(localAfter->isEmpty());
}

void tst_account_adoption::
activeAccountSessionMergesLaterLocalOnlyState() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot accountState = populatedSnapshot();
    const ProfilePaths accountPaths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(accountPaths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(accountPaths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(accountState));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    PersonalStateSnapshot laterLocalState;
    laterLocalState.progressEntries.insert(
        QStringLiteral("movie\x1flate-local-movie"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("late-local-movie")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("progress"), 0.7},
            {QStringLiteral("updatedAt"), 1720000004000.0}});
    laterLocalState.collectionEntries.insert(
        QStringLiteral("Theatre\x1flate-local-movie"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("late-local-movie")},
            {QStringLiteral("world"), QStringLiteral("Theatre")},
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("Later Local Movie")} });

    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(localPaths);
    QVERIFY(localStorage.has_value());
    QVERIFY(localStorage->restorePersonalState(laterLocalState));

    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const auto merged = accountStorage->capture(&error);
    QVERIFY2(merged.has_value(), qPrintable(error));
    QVERIFY(merged->progressEntries.contains(
        QStringLiteral("movie\x1flate-local-movie")));
    QVERIFY(merged->collectionEntries.contains(
        QStringLiteral("Theatre\x1flate-local-movie")));

    const auto localAfter = localStorage->capture(&error);
    QVERIFY2(localAfter.has_value(), qPrintable(error));
    QVERIFY(localAfter->isEmpty());
}

void tst_account_adoption::
rememberedAccountSessionMergesLaterLocalOnlyState() {
    AdoptionFixture fixture;
    const ProfilePaths accountPaths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(accountPaths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(accountPaths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(populatedSnapshot()));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareLocalOnly(&error),
        qPrintable(error));

    PersonalStateSnapshot laterLocalState;
    laterLocalState.progressEntries.insert(
        QStringLiteral("movie\x1fremembered-local-movie"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("remembered-local-movie")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("progress"), 0.4},
            {QStringLiteral("updatedAt"), 1720000005000.0}});

    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(localPaths);
    QVERIFY(localStorage.has_value());
    QVERIFY(localStorage->restorePersonalState(laterLocalState));

    QVERIFY2(
        coordinator.prepareRememberedAccount(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const auto merged = accountStorage->capture(&error);
    QVERIFY2(merged.has_value(), qPrintable(error));
    QVERIFY(merged->progressEntries.contains(
        QStringLiteral("movie\x1fremembered-local-movie")));

    const auto localAfter = localStorage->capture(&error);
    QVERIFY2(localAfter.has_value(), qPrintable(error));
    QVERIFY(localAfter->isEmpty());
}

void tst_account_adoption::
continueLocalBeforeAdoptionKeepsLegacyAuthority() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareLocalOnly(
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::LegacyLocal);

    const auto legacyAfter =
        fixture.legacy.capture(&error);
    QVERIFY2(
        legacyAfter.has_value(),
        qPrintable(error));
    QCOMPARE(
        legacyAfter->semanticDigest(),
        source.semanticDigest());
}

void tst_account_adoption::
continueLocalAfterAdoptionUsesDedicatedLocalProfile() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareCreatedAccount(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    QVERIFY2(
        coordinator.prepareLocalOnly(
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::LocalOnly);

    const ProfilePaths local =
        ProfilePaths::localOnly(
            fixture.appDataRoot);
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(
            local,
            &error);
    QVERIFY2(
        localStorage.has_value(),
        qPrintable(error));

    const auto localState =
        localStorage->capture(&error);
    QVERIFY2(
        localState.has_value(),
        qPrintable(error));
    QVERIFY(localState->isEmpty());

    const auto legacyState =
        fixture.legacy.capture(&error);
    QVERIFY2(
        legacyState.has_value(),
        qPrintable(error));
    QVERIFY(legacyState->isEmpty());

    const ProfilePaths account =
        fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(
            account,
            &error);
    QVERIFY2(
        accountStorage.has_value(),
        qPrintable(error));

    const auto accountState =
        accountStorage->capture(&error);
    QVERIFY2(
        accountState.has_value(),
        qPrintable(error));
    QCOMPARE(
        accountState->semanticDigest(),
        source.semanticDigest());
}

void tst_account_adoption::
corruptRestartRestoresLegacyAndLeavesRetryIntent() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    const ProfilePaths paths =
        fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    QSettings corrupted(
        paths.collectionIniPath(),
        QSettings::IniFormat);
    corrupted.setValue(
        QStringLiteral("collection/entries"),
        QByteArrayLiteral("not-json"));
    corrupted.sync();
    QCOMPARE(
        corrupted.status(),
        QSettings::NoError);

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY(
        !coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error));
    QVERIFY(!error.isEmpty());

    const auto restored =
        fixture.legacy.capture(&error);
    QVERIFY2(
        restored.has_value(),
        qPrintable(error));
    QCOMPARE(
        restored->semanticDigest(),
        source.semanticDigest());

    QVERIFY(
        !QFileInfo::exists(
            paths.profileRoot()));

    const auto adoption =
        ProfileAdoption::open(
            paths,
            &error);
    QVERIFY2(
        adoption.has_value(),
        qPrintable(error));
    QCOMPARE(
        adoption->state(),
        ProfileAdoption::State::RetryPending);

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::LegacyLocal);
}

void tst_account_adoption::
retryIntentReAdoptsOnLaterSignIn() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    const ProfilePaths paths =
        fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    QSettings corrupted(
        paths.progressIniPath(),
        QSettings::IniFormat);
    corrupted.setValue(
        QStringLiteral("continue/entries"),
        QByteArrayLiteral("not-json"));
    corrupted.sync();
    QCOMPARE(
        corrupted.status(),
        QSettings::NoError);

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY(
            !coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error));
    }

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto adoption =
            ProfileAdoption::open(
                paths,
                &error);
        QVERIFY2(
            adoption.has_value(),
            qPrintable(error));
        QCOMPARE(
            adoption->state(),
            ProfileAdoption::State::LegacyQuarantined);
    }
}

void tst_account_adoption::
directAccountSwitchRequiresSealing() {
    AdoptionFixture fixture;

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    error.clear();
    QVERIFY(
        !coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountB),
            &error));
    QVERIFY(
        error.contains(
            QStringLiteral("sealed")));

    QCOMPARE(
        runtime.activeProfile().profileId(),
        fixture.accountPaths().profileId());
}

void tst_account_adoption::
existingAccountMergeAcceptsCompletedActivity() {
    AdoptionFixture fixture;
    QVERIFY(fixture.legacy.restorePersonalState(populatedSnapshot()));

    const ProfilePaths paths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(paths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(PersonalStateSnapshot{}));

    {
        ActivityStore legacyActivity(fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY(legacyActivity.recordCompletion(fixtureMovieCompletionFact()));
    }

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(QString::fromLatin1(kAccountA), &error),
        qPrintable(error));

    ActivityStore mergedActivity(paths.activityDbPath());
    QVERIFY(mergedActivity.healthy());
    const QList<QVariantMap> facts = mergedActivity.historyProjectionFacts();
    QCOMPARE(facts.size(), 1);
    QCOMPARE(facts.first().value(QStringLiteral("type")).toString(),
             QStringLiteral("media_completed"));
}

void tst_account_adoption::
activityOnlyLocalStateIsMergedIntoExistingAccount() {
    AdoptionFixture fixture;

    const ProfilePaths paths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(paths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(PersonalStateSnapshot{}));

    {
        ActivityStore legacyActivity(fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY(legacyActivity.recordPlaybackDelta(fixtureMovieFact()));
    }

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(QString::fromLatin1(kAccountA), &error),
        qPrintable(error));

    ActivityStore mergedActivity(paths.activityDbPath());
    QVERIFY(mergedActivity.healthy());
    const QList<QVariantMap> facts = mergedActivity.historyProjectionFacts();
    QCOMPARE(facts.size(), 1);
    QCOMPARE(
        facts.first().value(QStringLiteral("type")).toString(),
        QStringLiteral("playback_delta"));
}

void tst_account_adoption::
firstAccountAdoptionMigratesActivityLedger() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    // Seed a legacy activity ledger the way a real legacy-local session
    // would accumulate one: write directly at the legacy activity path,
    // then let the store close cleanly (scope exit) before adoption runs.
    {
        ActivityStore legacyActivity(
            fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY2(
            legacyActivity.recordPlaybackDelta(
                fixtureMovieFact()),
            "seeding the legacy activity fact should succeed");
    }

    const QString expectedActivityDigest =
        ActivityStore::fileDigestSha256(
            fixture.legacy.activityDbPath());
    QVERIFY(!expectedActivityDigest.isEmpty());

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareCreatedAccount(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const ProfilePaths paths =
        fixture.accountPaths();

    // Legacy activity ledger is quarantined (removed) — mirrors the legacy
    // personal-state quarantine the existing adoption tests already prove.
    QVERIFY(
        !QFileInfo::exists(
            fixture.legacy.activityDbPath()));

    // The promoted profile's activity ledger is a byte-identical copy.
    QCOMPARE(
        ActivityStore::fileDigestSha256(
            paths.activityDbPath()),
        expectedActivityDigest);

    // A rollback backup of the activity ledger exists alongside the
    // existing personal-state.json backup and matches too.
    const QString backupActivityPath =
        QDir(paths.adoptionBackupRoot())
            .filePath(
                QStringLiteral("activity.sqlite"));
    QVERIFY(
        QFileInfo::exists(backupActivityPath));
    QCOMPARE(
        ActivityStore::fileDigestSha256(
            backupActivityPath),
        expectedActivityDigest);

    // The migrated fact is readable and semantically intact through the
    // real ActivityStore API, not just byte-identical on disk.
    ActivityStore promotedActivity(
        paths.activityDbPath());
    QVERIFY(promotedActivity.healthy());
    const QString monthKey =
        promotedActivity.earliestActivityMonth();
    QVERIFY(!monthKey.isEmpty());
    const QVariantMap projection =
        promotedActivity.projectMonth(monthKey);
    QCOMPARE(
        projection.value(
            QStringLiteral("watchSeconds"))
            .toLongLong(),
        qint64(30));
}

void tst_account_adoption::
interruptedAdoptionRestoresLegacyActivityLedger() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    {
        ActivityStore legacyActivity(
            fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY2(
            legacyActivity.recordPlaybackDelta(
                fixtureMovieFact()),
            "seeding the legacy activity fact should succeed");
    }

    const QString expectedActivityDigest =
        ActivityStore::fileDigestSha256(
            fixture.legacy.activityDbPath());
    QVERIFY(!expectedActivityDigest.isEmpty());

    const ProfilePaths paths =
        fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    // Same restart-verification trip wire
    // corruptRestartRestoresLegacyAndLeavesRetryIntent() already uses: a
    // promoted personal-state file corrupted between sessions fails the
    // second session's readback and forces a full rollback — this proves
    // the activity ledger is restored through that same rollback, not just
    // personal state.
    QSettings corrupted(
        paths.collectionIniPath(),
        QSettings::IniFormat);
    corrupted.setValue(
        QStringLiteral("collection/entries"),
        QByteArrayLiteral("not-json"));
    corrupted.sync();
    QCOMPARE(
        corrupted.status(),
        QSettings::NoError);

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY(
            !coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error));
        QVERIFY(!error.isEmpty());
    }

    // The legacy activity ledger is restored, and the journal is left
    // RetryPending — no silent history drop (CPP-PORT-CONTRACT §17).
    //
    // Verified SEMANTICALLY (open the restored file and re-project it), not
    // by raw file-byte digest: the rollback path's own
    // reloadLegacyProfile() legitimately reopens a live ActivityStore on the
    // restored path afterward, and — exactly like
    // tst_activity_store.cpp's own restartPersistsAndProjectsDeterministically
    // proves — an independent open/close cycle over unchanged SQLite content
    // is not guaranteed byte-identical (page/WAL layout can differ) even
    // though the persisted ledger is. The earlier digest checks in
    // firstAccountAdoptionMigratesActivityLedger already cover the adoption
    // path's own single-session digest chain (legacy -> staged -> promoted
    // -> backup), where no such intervening reopen occurs.
    {
        ActivityStore restoredLegacyActivity(
            fixture.legacy.activityDbPath());
        QVERIFY(restoredLegacyActivity.healthy());
        const QString monthKey =
            restoredLegacyActivity.earliestActivityMonth();
        QVERIFY(!monthKey.isEmpty());
        const QVariantMap projection =
            restoredLegacyActivity.projectMonth(monthKey);
        QCOMPARE(
            projection.value(
                QStringLiteral("watchSeconds"))
                .toLongLong(),
            qint64(30));
    }

    QString adoptError;
    const auto adoption =
        ProfileAdoption::open(
            paths,
            &adoptError);
    QVERIFY2(
        adoption.has_value(),
        qPrintable(adoptError));
    QCOMPARE(
        adoption->state(),
        ProfileAdoption::State::RetryPending);

    // A later retry re-adopts cleanly, re-migrating the restored ledger.
    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    QVERIFY(
        !QFileInfo::exists(
            fixture.legacy.activityDbPath()));

    ActivityStore reAdoptedActivity(
        paths.activityDbPath());
    QVERIFY(reAdoptedActivity.healthy());
    const QString reAdoptedMonthKey =
        reAdoptedActivity.earliestActivityMonth();
    QVERIFY(!reAdoptedMonthKey.isEmpty());
    const QVariantMap reAdoptedProjection =
        reAdoptedActivity.projectMonth(
            reAdoptedMonthKey);
    QCOMPARE(
        reAdoptedProjection.value(
            QStringLiteral("watchSeconds"))
            .toLongLong(),
        qint64(30));
}

QTEST_MAIN(tst_account_adoption)
#include "tst_account_adoption.moc"
