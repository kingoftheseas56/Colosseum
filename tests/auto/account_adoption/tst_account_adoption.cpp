// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

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
    void ordinarySignInDoesNotClaimLegacyLocalState();
    void continueLocalBeforeAdoptionKeepsLegacyAuthority();
    void continueLocalAfterAdoptionUsesDedicatedLocalProfile();
    void corruptRestartRestoresLegacyAndLeavesRetryIntent();
    void retryIntentReAdoptsOnLaterSignIn();
    void legacySnapshotV1RemainsReadableWithoutHistory();
    void directAccountSwitchRequiresSealing();
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
ordinarySignInDoesNotClaimLegacyLocalState() {
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
    QCOMPARE(
        legacyAfter->semanticDigest(),
        source.semanticDigest());

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
    QVERIFY(accountState->isEmpty());

    QVERIFY(
        !QFileInfo::exists(
            paths.adoptionJournalPath()));
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

QTEST_MAIN(tst_account_adoption)
#include "tst_account_adoption.moc"
