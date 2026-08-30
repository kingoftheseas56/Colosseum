// Task 3 RED contract: local privacy policy must govern retention without
// changing the existing portable preference-sync contract.

#include "SearchHistoryStore.h"
#include "ProgressStore.h"
#include "account/ActivityStore.h"
#include "account/ConsumptionHistoryBridge.h"
#include "account/HistoryStore.h"
#include "account/ProfilePaths.h"
#include "account/ProfilePreferencesStore.h"
#include "account/ProfileStoreRuntime.h"

#include <QDir>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QVariantMap playbackFact(const QString &eventId)
{
    return {{QStringLiteral("eventId"), eventId},
            {QStringLiteral("sessionId"), QStringLiteral("session")},
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("titleKey"), QStringLiteral("movie:title")},
            {QStringLiteral("itemKey"), QStringLiteral("movie:item")},
            {QStringLiteral("title"), QStringLiteral("Movie")},
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

class tst_privacy_policy final : public QObject
{
    Q_OBJECT

private slots:
    void privacyDefaultsAreTrueAndPersistPerProfile();
    void privacyPolicyChangesDoNotEmitPreferenceSyncDirty();
    void searchRetentionOffSuppressesFutureRecordsOnly();
    void activityRetentionOffSuppressesFutureFactsOnly();
    void retentionReenableRecordsAgain();
    void clearActivityHistoryClearsActivityAndHistoryButNotProgress();
    void profileSwitchRebindsRetentionPolicy();
};

void tst_privacy_policy::privacyDefaultsAreTrueAndPersistPerProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("preferences.ini"));

    ProfilePreferencesStore store(path);
    QCOMPARE(store.rememberSearchHistory(), true);
    QCOMPARE(store.keepActivityHistory(), true);
    QCOMPARE(store.syncActivityHistory(), true);

    store.setRememberSearchHistory(false);
    store.setKeepActivityHistory(false);
    store.setSyncActivityHistory(false);

    ProfilePreferencesStore reopened(path);
    QCOMPARE(reopened.rememberSearchHistory(), false);
    QCOMPARE(reopened.keepActivityHistory(), false);
    QCOMPARE(reopened.syncActivityHistory(), false);
}

void tst_privacy_policy::privacyPolicyChangesDoNotEmitPreferenceSyncDirty()
{
    ProfilePreferencesStore store;
    QSignalSpy dirty(&store, &ProfilePreferencesStore::syncDirty);
    store.setRememberSearchHistory(false);
    store.setKeepActivityHistory(false);
    store.setSyncActivityHistory(false);
    QCOMPARE(dirty.count(), 0);
}

void tst_privacy_policy::searchRetentionOffSuppressesFutureRecordsOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SearchHistoryStore store(dir.filePath(QStringLiteral("search.ini")));
    store.record(QStringLiteral("tankoban"), QStringLiteral("before"));
    store.setRetentionEnabled(false);
    QCOMPARE(store.record(QStringLiteral("tankoban"), QStringLiteral("after")),
             QStringList{QStringLiteral("before")});
    QCOMPARE(store.list(QStringLiteral("tankoban")), QStringList{QStringLiteral("before")});
}

void tst_privacy_policy::activityRetentionOffSuppressesFutureFactsOnly()
{
    ActivityStore store;
    QVERIFY(store.recordPlaybackDelta(playbackFact(QStringLiteral("before"))));
    store.setRetentionEnabled(false);
    QSignalSpy changed(&store, &ActivityStore::changed);
    QSignalSpy committed(&store, &ActivityStore::factCommitted);
    QVERIFY(store.recordPlaybackDelta(playbackFact(QStringLiteral("after"))));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(committed.count(), 0);
    QCOMPARE(store.historyProjectionFacts().size(), 1);
}

void tst_privacy_policy::retentionReenableRecordsAgain()
{
    SearchHistoryStore search;
    search.setRetentionEnabled(false);
    search.record(QStringLiteral("biblio"), QStringLiteral("hidden"));
    search.setRetentionEnabled(true);
    QCOMPARE(search.record(QStringLiteral("biblio"), QStringLiteral("visible")),
             QStringList{QStringLiteral("visible")});

    ActivityStore activity;
    activity.setRetentionEnabled(false);
    QVERIFY(activity.recordPlaybackDelta(playbackFact(QStringLiteral("ignored"))));
    activity.setRetentionEnabled(true);
    QVERIFY(activity.recordPlaybackDelta(playbackFact(QStringLiteral("accepted"))));
    QCOMPARE(activity.historyProjectionFacts().size(), 1);
}

void tst_privacy_policy::clearActivityHistoryClearsActivityAndHistoryButNotProgress()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ActivityStore activity;
    HistoryStore history(dir.filePath(QStringLiteral("history.ini")));
    ProgressStore progress(dir.filePath(QStringLiteral("progress.ini")));
    progress.record({{QStringLiteral("kind"), QStringLiteral("movie")},
                     {QStringLiteral("id"), QStringLiteral("keep")},
                     {QStringLiteral("progress"), 0.5}});
    ConsumptionHistoryBridge coordinator(&activity, &progress, &history);
    QVERIFY(activity.recordPlaybackDelta(playbackFact(QStringLiteral("clear-me"))));
    QVERIFY(coordinator.clearAll());
    QVERIFY(activity.historyProjectionFacts().isEmpty());
    QVERIFY(history.records().isEmpty());
    QVERIFY(!progress.recent().isEmpty());
}

void tst_privacy_policy::profileSwitchRebindsRetentionPolicy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString appDataRoot = dir.filePath(QStringLiteral("appdata"));
    const QString accountA = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const QString accountB = QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    const auto pathsA = ProfilePaths::account(accountA, appDataRoot);
    const auto pathsB = ProfilePaths::account(accountB, appDataRoot);
    QVERIFY(pathsA.has_value());
    QVERIFY(pathsB.has_value());
    QVERIFY(QDir().mkpath(pathsA->profileRoot()));
    QVERIFY(QDir().mkpath(pathsB->profileRoot()));

    ProfileStoreRuntime runtime(
        LegacyPersonalStateStorage::isolated(
            dir.filePath(QStringLiteral("legacy"))),
        appDataRoot);
    QString error;
    QVERIFY2(runtime.activateAccountProfile(accountA, &error), qPrintable(error));

    auto *preferencesA = runtime.preferencesStore();
    auto *searchA = runtime.searchHistoryStore();
    auto *activityA = runtime.activityStore();
    auto *historyA = runtime.historyStore();
    auto *progressA = runtime.progressStore();
    QVERIFY(preferencesA);
    QVERIFY(searchA);
    QVERIFY(activityA);
    QVERIFY(historyA);
    QVERIFY(progressA);
    QPointer<ProfilePreferencesStore> oldPreferencesA(preferencesA);
    QPointer<SearchHistoryStore> oldSearchA(searchA);
    QPointer<ActivityStore> oldActivityA(activityA);
    QPointer<HistoryStore> oldHistoryA(historyA);
    QPointer<ProgressStore> oldProgressA(progressA);

    preferencesA->setRememberSearchHistory(false);
    preferencesA->setKeepActivityHistory(false);
    QCOMPARE(preferencesA->syncActivityHistory(), true);
    preferencesA->setSyncActivityHistory(false);
    QCOMPARE(searchA->retentionEnabled(), false);
    QCOMPARE(activityA->retentionEnabled(), false);
    QCOMPARE(preferencesA->syncActivityHistory(), false);
    QCOMPARE(searchA->record(QStringLiteral("manga"), QStringLiteral("a-hidden")),
             QStringList());
    QVERIFY(activityA->recordPlaybackDelta(
        playbackFact(QStringLiteral("a-hidden"))));
    QCOMPARE(activityA->historyProjectionFacts().size(), 0);

    QVERIFY2(runtime.sealAccountProfile(accountA, &error), qPrintable(error));
    QVERIFY(oldPreferencesA.isNull());
    QVERIFY(oldSearchA.isNull());
    QVERIFY(oldActivityA.isNull());
    QVERIFY(oldHistoryA.isNull());
    QVERIFY(oldProgressA.isNull());
    auto *sealedHistory = runtime.historyStore();
    auto *sealedProgress = runtime.progressStore();
    QVERIFY2(runtime.activateAccountProfile(accountB, &error), qPrintable(error));

    auto *preferencesB = runtime.preferencesStore();
    auto *searchB = runtime.searchHistoryStore();
    auto *activityB = runtime.activityStore();
    auto *historyB = runtime.historyStore();
    auto *progressB = runtime.progressStore();
    QVERIFY(preferencesB);
    QVERIFY(searchB);
    QVERIFY(activityB);
    QVERIFY(historyB);
    QVERIFY(progressB);
    QVERIFY(historyB != sealedHistory);
    QVERIFY(progressB != sealedProgress);
    QCOMPARE(preferencesB->rememberSearchHistory(), true);
    QCOMPARE(preferencesB->keepActivityHistory(), true);
    QCOMPARE(preferencesB->syncActivityHistory(), true);
    QVERIFY(historyB->records().isEmpty());
    QVERIFY(progressB->recent().isEmpty());
    QCOMPARE(searchB->retentionEnabled(), true);
    QCOMPARE(activityB->retentionEnabled(), true);
    QCOMPARE(searchB->record(QStringLiteral("manga"), QStringLiteral("b-visible")),
             QStringList{QStringLiteral("b-visible")});
    QVERIFY(activityB->recordPlaybackDelta(
        playbackFact(QStringLiteral("b-visible"))));
    QCOMPARE(activityB->historyProjectionFacts().size(), 1);
    QVERIFY(!searchB->list(QStringLiteral("manga")).contains(
        QStringLiteral("a-hidden")));

    QVERIFY2(runtime.sealAccountProfile(accountB, &error), qPrintable(error));
    QVERIFY2(runtime.activateAccountProfile(accountA, &error), qPrintable(error));
    QVERIFY(runtime.preferencesStore());
    QVERIFY(runtime.searchHistoryStore());
    QVERIFY(runtime.activityStore());
    QCOMPARE(runtime.preferencesStore()->rememberSearchHistory(), false);
    QCOMPARE(runtime.preferencesStore()->keepActivityHistory(), false);
    QCOMPARE(runtime.searchHistoryStore()->retentionEnabled(), false);
    QCOMPARE(runtime.activityStore()->retentionEnabled(), false);
    QCOMPARE(runtime.preferencesStore()->syncActivityHistory(), false);
    QVERIFY(!runtime.searchHistoryStore()->list(QStringLiteral("manga")).contains(
        QStringLiteral("b-visible")));
}

QTEST_MAIN(tst_privacy_policy)
#include "tst_privacy_policy.moc"
