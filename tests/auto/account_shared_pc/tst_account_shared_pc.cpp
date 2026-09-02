// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/HistoryStore.h"
#include "account/LegacyPersonalStateStorage.h"
#include "account/ActivityStore.h"
#include "account/ConsumptionHistoryBridge.h"
#include "account/ProfilePaths.h"
#include "account/ProfilePreferencesStore.h"
#include "account/ProfileStoreRuntime.h"
#include "account/SharedPcProfileCoordinator.h"
#include "ProgressStore.h"

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

QVariantMap playbackFact(const QString &eventId);

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

ConsumptionHistoryBridge *profileConsumptionHistory(
    QQmlApplicationEngine *engine) {
    QObject *object =
        engine->rootContext()
            ->contextProperty(
                QStringLiteral("ProfileConsumptionHistory"))
            .value<QObject *>();
    return qobject_cast<ConsumptionHistoryBridge *>(object);
}

QVariantMap consumptionPlaybackFact(
    const QString &eventId,
    const QString &itemKey = QStringLiteral("movie:item")) {
    auto fact = playbackFact(eventId);
    fact[QStringLiteral("titleKey")] = QStringLiteral("movie:title");
    fact[QStringLiteral("itemKey")] = itemKey;
    fact[QStringLiteral("title")] = QStringLiteral("Movie");
    return fact;
}

QVariantMap playbackFact(const QString &eventId) {
    return {{QStringLiteral("eventId"), eventId},
            {QStringLiteral("sessionId"), QStringLiteral("shared-pc-session")},
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("titleKey"), QStringLiteral("movie:shared-pc")},
            {QStringLiteral("itemKey"), QStringLiteral("movie:shared-pc")},
            {QStringLiteral("title"), QStringLiteral("Shared PC Movie")},
            {QStringLiteral("itemLabel"), QString()},
            {QStringLiteral("cover"), QString()},
            {QStringLiteral("utcOffsetMinutes"), 330},
            {QStringLiteral("syncable"), true},
            {QStringLiteral("source"), QStringLiteral("test")},
            {QStringLiteral("startAtMs"), qint64(1000)},
            {QStringLiteral("endAtMs"), qint64(2000)},
            {QStringLiteral("activeMs"), qint64(1000)},
            {QStringLiteral("rateMilli"), 1000}};
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
    void consumptionPrivacyAndHistoryRemainOwnedAcrossSharedPcSwitch();

    // Arc 36 Wave 4B lane N-17 — the attach seam must report the source
    // identity the network AccountAttachmentCoordinator flow needs, and the
    // seal must fail closed while that flow is in flight.
    void attachReportsLegacyLocalIdentityForNetworkAttachment();
    void attachReportsLocalOnlyIdentityForNetworkAttachment();
    void failedAttachLeavesNoPendingIdentity();
    void idempotentReattachReportsNoNewIdentity();
    void otherLifecyclePathsVoidPendingAttachIdentity();
    void sealFailsClosedWhileAttachmentInFlight();
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

void tst_account_shared_pc::
consumptionPrivacyAndHistoryRemainOwnedAcrossSharedPcSwitch() {
    SharedPcFixture fixture;
    fixture.seedAccount(QString::fromLatin1(kAccountA), PersonalStateSnapshot{});
    fixture.seedAccount(QString::fromLatin1(kAccountB), PersonalStateSnapshot{});

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(&runtime, fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);

    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::Sealed);
    QVERIFY(profileHistory(&engine));
    QVERIFY(profileHistory(&engine)->records().isEmpty());

    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::Sealed);
    QVERIFY(profilePreferences(&engine));
    QCOMPARE(profilePreferences(&engine)->keepActivityHistory(), true);
    QCOMPARE(profilePreferences(&engine)->syncActivityHistory(), true);
    QVERIFY(profileHistory(&engine));
    QVERIFY(profileHistory(&engine)->records().isEmpty());

    QString error;
    QVERIFY2(profiles.prepareAccountSession(QString::fromLatin1(kAccountA), &error), qPrintable(error));
    auto *preferencesA = profilePreferences(&engine);
    auto *activityA = runtime.activityStore();
    auto *historyA = profileHistory(&engine);
    auto *progressA = runtime.progressStore();
    auto *searchA = runtime.searchHistoryStore();
    auto *coordinatorA = profileConsumptionHistory(&engine);
    QVERIFY(preferencesA);
    QVERIFY(activityA);
    QVERIFY(historyA);
    QVERIFY(progressA);
    QVERIFY(searchA);
    QVERIFY(coordinatorA);
    QPointer<ProfilePreferencesStore> oldPreferencesA(preferencesA);
    QPointer<ActivityStore> oldActivityA(activityA);
    QPointer<HistoryStore> oldHistoryA(historyA);
    QPointer<ProgressStore> oldProgressA(progressA);
    QPointer<SearchHistoryStore> oldSearchA(searchA);
    QPointer<ConsumptionHistoryBridge> oldCoordinatorA(coordinatorA);

    QVERIFY(activityA->recordPlaybackDelta(consumptionPlaybackFact(QStringLiteral("event-a-play"))));
    QVERIFY(!activityA->historyProjectionFacts().isEmpty());
    QVERIFY(!historyA->get(QStringLiteral("movie"), QStringLiteral("movie:item")).isEmpty());

    progressA->record({{QStringLiteral("kind"), QStringLiteral("video")},
                       {QStringLiteral("id"), QStringLiteral("movie:item")},
                       {QStringLiteral("progress"), 0.42}});
    progressA->record({{QStringLiteral("kind"), QStringLiteral("video")},
                       {QStringLiteral("id"), QStringLiteral("movie:item")},
                       {QStringLiteral("progress"), 0.90}});
    QVERIFY(historyA->completed(QStringLiteral("movie"), QStringLiteral("movie:item")));
    QVERIFY(historyA->get(QStringLiteral("movie"), QStringLiteral("movie:item"))
                .value(QStringLiteral("completedAt")).toLongLong() > 0);

    preferencesA->setKeepActivityHistory(false);
    auto suppressedFact = consumptionPlaybackFact(
        QStringLiteral("event-a-suppressed"), QStringLiteral("movie:suppressed"));
    QVERIFY(activityA->recordPlaybackDelta(suppressedFact));
    QCOMPARE(activityA->historyProjectionFacts().size(), 1);
    QVERIFY(historyA->get(QStringLiteral("movie"), QStringLiteral("movie:suppressed")).isEmpty());

    progressA->record({{QStringLiteral("kind"), QStringLiteral("movie")},
                       {QStringLiteral("id"), QStringLiteral("keep-progress")},
                       {QStringLiteral("progress"), 0.5}});
    const auto progressBeforeClear =
        progressA->get(QStringLiteral("movie"), QStringLiteral("keep-progress"));
    QVERIFY(coordinatorA->clearAll());
    QVERIFY(activityA->historyProjectionFacts().isEmpty());
    QVERIFY(historyA->records().isEmpty());
    const auto keptProgress = progressA->get(QStringLiteral("movie"), QStringLiteral("keep-progress"));
    QVERIFY(!keptProgress.isEmpty());
    QCOMPARE(keptProgress, progressBeforeClear);
    QCOMPARE(keptProgress.value(QStringLiteral("progress")).toDouble(), 0.5);

    preferencesA->setKeepActivityHistory(true);
    const auto aSurvivorFact = consumptionPlaybackFact(
        QStringLiteral("event-a-survivor"), QStringLiteral("movie:a-survivor"));
    QVERIFY(activityA->recordPlaybackDelta(aSurvivorFact));
    const auto aProjectionFacts = activityA->historyProjectionFacts();
    QVERIFY([&]() {
        for (const QVariantMap &fact : aProjectionFacts) {
            if (fact.value(QStringLiteral("itemKey")).toString()
                == QStringLiteral("movie:a-survivor")) {
                return true;
            }
        }
        return false;
    }());
    QVERIFY(!historyA->get(QStringLiteral("movie"), QStringLiteral("movie:a-survivor"))
                 .isEmpty());

    QVERIFY2(profiles.sealAccountSession(QString::fromLatin1(kAccountA), &error), qPrintable(error));
    QVERIFY(oldPreferencesA.isNull());
    QVERIFY(oldActivityA.isNull());
    QVERIFY(oldHistoryA.isNull());
    QVERIFY(oldProgressA.isNull());
    QVERIFY(oldSearchA.isNull());
    QVERIFY(oldCoordinatorA.isNull());
    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::Sealed);
    QVERIFY(profileHistory(&engine));
    QVERIFY(profileHistory(&engine)->records().isEmpty());

    QVERIFY2(profiles.prepareAccountSession(QString::fromLatin1(kAccountB), &error), qPrintable(error));
    auto *preferencesB = profilePreferences(&engine);
    auto *activityB = runtime.activityStore();
    auto *historyB = profileHistory(&engine);
    auto *progressB = runtime.progressStore();
    auto *coordinatorB = profileConsumptionHistory(&engine);
    QVERIFY(preferencesB);
    QVERIFY(activityB);
    QVERIFY(historyB);
    QVERIFY(progressB);
    QVERIFY(coordinatorB);
    QCOMPARE(preferencesB->keepActivityHistory(), true);
    QCOMPARE(preferencesB->rememberSearchHistory(), true);
    QCOMPARE(preferencesB->syncActivityHistory(), true);
    QVERIFY(activityB->historyProjectionFacts().isEmpty());
    QVERIFY(historyB->records().isEmpty());
    QVERIFY(progressB->syncEntries().isEmpty());

    const auto bFact = consumptionPlaybackFact(
        QStringLiteral("event-b-play"), QStringLiteral("movie:b-only"));
    QVERIFY(activityB->recordPlaybackDelta(bFact));
    const auto bProjectionFacts = activityB->historyProjectionFacts();
    QVERIFY(!bProjectionFacts.isEmpty());
    QVERIFY([&]() {
        for (const QVariantMap &fact : bProjectionFacts) {
            if (fact.value(QStringLiteral("eventId")).toString()
                == QStringLiteral("event-b-play")) {
                return true;
            }
        }
        return false;
    }());
    QVERIFY(!historyB->get(QStringLiteral("movie"), QStringLiteral("movie:b-only")).isEmpty());
    QVERIFY(historyB->get(QStringLiteral("movie"), QStringLiteral("movie:item")).isEmpty());
    QVERIFY(historyB->get(QStringLiteral("movie"), QStringLiteral("movie:suppressed")).isEmpty());
    QVERIFY(historyB->get(QStringLiteral("movie"), QStringLiteral("keep-progress")).isEmpty());
    QVERIFY(historyB->get(QStringLiteral("movie"), QStringLiteral("movie:a-survivor")).isEmpty());

    QVERIFY2(profiles.sealAccountSession(QString::fromLatin1(kAccountB), &error), qPrintable(error));
    QVERIFY2(profiles.prepareAccountSession(QString::fromLatin1(kAccountA), &error), qPrintable(error));
    auto *preferencesAReopened = profilePreferences(&engine);
    auto *activityAReopened = runtime.activityStore();
    auto *historyAReopened = profileHistory(&engine);
    auto *progressAReopened = runtime.progressStore();
    QVERIFY(preferencesAReopened);
    QVERIFY(activityAReopened);
    QVERIFY(historyAReopened);
    QVERIFY(progressAReopened);
    QCOMPARE(preferencesAReopened->keepActivityHistory(), true);
    QCOMPARE(preferencesAReopened->rememberSearchHistory(), true);
    QCOMPARE(preferencesAReopened->syncActivityHistory(), true);

    const auto aReopenedProjectionFacts = activityAReopened->historyProjectionFacts();
    QVERIFY([&]() {
        for (const QVariantMap &fact : aReopenedProjectionFacts) {
            if (fact.value(QStringLiteral("eventId")).toString()
                == QStringLiteral("event-a-survivor")) {
                return true;
            }
        }
        return false;
    }());
    QVERIFY(!historyAReopened->get(QStringLiteral("movie"), QStringLiteral("movie:a-survivor"))
                 .isEmpty());
    const auto reopenedProgress =
        progressAReopened->get(QStringLiteral("movie"), QStringLiteral("keep-progress"));
    QCOMPARE(reopenedProgress, progressBeforeClear);
    QCOMPARE(reopenedProgress.value(QStringLiteral("progress")).toDouble(), 0.5);
    QVERIFY([&]() {
        for (const QVariantMap &fact : aReopenedProjectionFacts) {
            if (fact.value(QStringLiteral("eventId")).toString()
                == QStringLiteral("event-b-play")) {
                return false;
            }
        }
        return true;
    }());
    QVERIFY(historyAReopened->get(QStringLiteral("movie"), QStringLiteral("movie:b-only")).isEmpty());
}

// ── Arc 36 Wave 4B lane N-17: attach seam → network attachment lifecycle ────

// A legacy-local attach reports the exact source identity the network
// coordinator needs: kind, profile id, the semantic digest of the captured
// personal state, and the Activity ledger's semantic event digest (empty
// sentinel when there is no ledger).
void tst_account_shared_pc::
    attachReportsLegacyLocalIdentityForNetworkAttachment() {
    SharedPcFixture fixture;

    const PersonalStateSnapshot legacyState =
        snapshotWithSearch(
            QStringLiteral("legacy-attach-query"),
            true);
    QVERIFY(
        fixture.legacy.restorePersonalState(
            legacyState));

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
        profiles.prepareLocalOnly(
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::LegacyLocal);

    // A durable ledger event so the activity digest is the real thing.
    ActivityStore *legacyActivity =
        runtime.activityStore();
    QVERIFY(legacyActivity);
    QVERIFY(
        legacyActivity->recordPlaybackDelta(
            consumptionPlaybackFact(
                QStringLiteral(
                    "event-legacy-attach"))));

    const auto expectedState =
        fixture.legacy.capture(
            &error);
    QVERIFY2(
        expectedState.has_value(),
        qPrintable(error));
    const QString expectedActivity =
        ActivityStore::semanticEventDigest(
            fixture
                .legacy
                .activityDbPath(),
            &error);
    QVERIFY(
        !expectedActivity.isEmpty());

    QVERIFY2(
        profiles.attachLocalProfileToAccount(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    const auto identity =
        profiles.takePendingLocalAttachment();
    QVERIFY(
        identity.has_value());
    QCOMPARE(
        identity->accountId,
        QString::fromLatin1(
            kAccountA));
    QCOMPARE(
        identity->sourceKind,
        QStringLiteral(
            "legacy_local"));
    QCOMPARE(
        identity->sourceProfileId,
        QStringLiteral(
            "legacy"));
    QCOMPARE(
        identity->sourceSemanticDigest,
        expectedState
            ->semanticDigest());
    QCOMPARE(
        identity->sourceActivityDigest,
        expectedActivity);

    // Taken exactly once.
    QVERIFY(
        !profiles
             .takePendingLocalAttachment()
             .has_value());
}

// A local-only attach reports the same fields with the local_only kind and
// the empty activity sentinel when the source has no Activity ledger.
void tst_account_shared_pc::
    attachReportsLocalOnlyIdentityForNetworkAttachment() {
    SharedPcFixture fixture;

    const ProfilePaths local =
        ProfilePaths::localOnly(
            fixture.appDataRoot);

    QString error;
    const auto localStorage =
        LegacyPersonalStateStorage::
            forProfile(
                local,
                &error);
    QVERIFY2(
        localStorage.has_value(),
        qPrintable(error));
    QVERIFY2(
        localStorage->restorePersonalState(
            snapshotWithSearch(
                QStringLiteral(
                    "local-attach-query"),
                false),
            &error),
        qPrintable(error));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);

    QVERIFY2(
        runtime.activateLocalOnlyProfile(
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::LocalOnly);

    const auto expectedState =
        localStorage->capture(
            &error);
    QVERIFY2(
        expectedState.has_value(),
        qPrintable(error));

    // Activating the local-only profile stamps an empty activity.sqlite, so
    // the faithful activity digest is whatever semanticEventDigest says for
    // the source ledger exactly as the local merge would compute it.
    const QString expectedActivity =
        ActivityStore::semanticEventDigest(
            local.activityDbPath(),
            &error);
    QVERIFY(
        !expectedActivity.isEmpty());

    QVERIFY2(
        profiles.attachLocalProfileToAccount(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    const auto identity =
        profiles.takePendingLocalAttachment();
    QVERIFY(
        identity.has_value());
    QCOMPARE(
        identity->accountId,
        QString::fromLatin1(
            kAccountA));
    QCOMPARE(
        identity->sourceKind,
        QStringLiteral(
            "local_only"));
    QCOMPARE(
        identity->sourceProfileId,
        QStringLiteral("local"));
    QCOMPARE(
        identity->sourceSemanticDigest,
        expectedState
            ->semanticDigest());
    QCOMPARE(
        identity->sourceActivityDigest,
        expectedActivity);
}

// A refused attach (no active local profile) never reports an identity.
void tst_account_shared_pc::
    failedAttachLeavesNoPendingIdentity() {
    SharedPcFixture fixture;

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY(
        !profiles.attachLocalProfileToAccount(
            QString::fromLatin1(
                kAccountA),
            &error));
    QVERIFY(
        !profiles
             .takePendingLocalAttachment()
             .has_value());
}

// Re-attaching while the account profile is already active is the
// idempotent no-op; it must not fabricate a second identity.
void tst_account_shared_pc::
    idempotentReattachReportsNoNewIdentity() {
    SharedPcFixture fixture;

    const PersonalStateSnapshot legacyState =
        snapshotWithSearch(
            QStringLiteral("legacy-once"),
            false);
    QVERIFY(
        fixture.legacy.restorePersonalState(
            legacyState));

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
        profiles.prepareLocalOnly(
            &error),
        qPrintable(error));
    QVERIFY2(
        profiles.attachLocalProfileToAccount(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));
    QVERIFY(
        profiles
            .takePendingLocalAttachment()
            .has_value());

    QVERIFY2(
        profiles.attachLocalProfileToAccount(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));
    QVERIFY(
        !profiles
             .takePendingLocalAttachment()
             .has_value());
}

// Only the attach path reports an identity: the ordinary created-account,
// session, remembered, and local-only lifecycles void any stale pending
// identity so a later unrelated sign-in never starts a phantom attachment.
void tst_account_shared_pc::
    otherLifecyclePathsVoidPendingAttachIdentity() {
    SharedPcFixture fixture;

    const PersonalStateSnapshot legacyState =
        snapshotWithSearch(
            QStringLiteral("legacy-void"),
            false);
    QVERIFY(
        fixture.legacy.restorePersonalState(
            legacyState));

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
        profiles.prepareLocalOnly(
            &error),
        qPrintable(error));
    QVERIFY2(
        profiles.attachLocalProfileToAccount(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));
    QVERIFY(
        profiles
            .takePendingLocalAttachment()
            .has_value());

    // Produce one more pending identity and LEAVE it pending, then run a
    // non-attach lifecycle transition and confirm it is voided.
    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));
    QVERIFY2(
        profiles.prepareLocalOnly(
            &error),
        qPrintable(error));
    QVERIFY2(
        profiles.attachLocalProfileToAccount(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));

    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(
                kAccountA),
            &error),
        qPrintable(error));
    QVERIFY(
        !profiles
             .takePendingLocalAttachment()
             .has_value());
}

// Sealing with an in-flight cloud attachment fails closed: the local source
// must never be sealed away unverified. With no flow in flight the seal
// behaves exactly as before.
void tst_account_shared_pc::
    sealFailsClosedWhileAttachmentInFlight() {
    SharedPcFixture fixture;
    fixture.seedAccount(
        QString::fromLatin1(kAccountA),
        snapshotWithSearch(
            QStringLiteral("seal-guard"),
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

    bool inFlight = true;
    profiles.setAttachmentInFlightProbe(
        [&inFlight]() {
            return inFlight;
        });

    error.clear();
    QVERIFY(
        !profiles.sealAccountSession(
            QString::fromLatin1(kAccountA),
            &error));
    QVERIFY(
        error.contains(
            QStringLiteral(
                "attachment")));
    // Fail closed means the account profile stays active and intact.
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    inFlight = false;
    error.clear();
    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Sealed);
}

QTEST_MAIN(tst_account_shared_pc)
#include "tst_account_shared_pc.moc"
