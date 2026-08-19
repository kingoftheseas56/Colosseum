// tst_profile_activity_isolation — Slice D4 proof that ActivityStore joins
// ProfileStoreRuntime::StoreSet exactly like Progress/CollectionStore already
// do (CPP-PORT-CONTRACT.md §2/§17): a profile switch clears/rebinds the
// `ProfileActivity` QML context property in the same transaction as the
// other stores, the previous profile's ActivityStore object is destroyed
// (not merely hidden), and no fact recorded under one profile is ever
// readable from another profile's ActivityStore — including a fresh sealed
// session in between.
//
// This lives in tests/auto/store_isolation/ alongside tst_store_isolation.cpp
// as a SEPARATE binary rather than folding into that file: tst_store_isolation
// guards a narrow, already-documented contract (ProgressStore/CollectionStore
// registry-vs-tagged-file routing) and pulls in nothing beyond those two
// header-only stores. Testing ProfileStoreRuntime's real QML-context rebind
// needs the full account/profile graph (ProfileStoreRuntime,
// FirstAccountProfileCoordinator, SharedPcProfileCoordinator, ActivityStore,
// Qt6::Qml, Qt6::Sql) — a different, heavier dependency set for a different
// concern, so it gets its own target rather than diluting that file's scope
// statement. tests/auto/account_shared_pc/tst_account_shared_pc.cpp already
// proves this exact "construct a real QQmlApplicationEngine, call
// prepareForQml(), read back a context property, QPointer-prove the old
// object is gone" pattern for SearchHistory/ProfileHistory/ProfilePreferences
// — this file applies the same proven pattern to ProfileActivity.

#include "account/ActivityStore.h"
#include "account/LegacyPersonalStateStorage.h"
#include "account/ProfilePaths.h"
#include "account/ProfileStoreRuntime.h"
#include "account/SharedPcProfileCoordinator.h"

#include <QDir>
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

// Field set mirrors tests/auto/activity/tst_activity_store.cpp's own fixture
// builders and tests/auto/account_adoption/tst_account_adoption.cpp's
// fixtureMovieFact() — duplicated deliberately (test infrastructure, not
// activity-engine logic each file must stay independently readable without
// cross-including a sibling test's internals).
QVariantMap fixtureMovieFact(
    const QString &sessionId) {
    QVariantMap fact;
    fact.insert(QStringLiteral("sessionId"), sessionId);
    fact.insert(QStringLiteral("world"), QStringLiteral("theatre"));
    fact.insert(QStringLiteral("kind"), QStringLiteral("movie"));
    fact.insert(QStringLiteral("titleKey"), QStringLiteral("theatre:isolation-fixture-movie"));
    fact.insert(QStringLiteral("itemKey"), QStringLiteral("isolation-fixture-movie"));
    fact.insert(QStringLiteral("title"), QStringLiteral("Isolation Fixture Movie"));
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

struct ProfileActivityFixture {
    QTemporaryDir temp;
    QString legacyRoot;
    QString appDataRoot;
    LegacyPersonalStateStorage legacy;

    ProfileActivityFixture()
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
            qFatal("Could not create profile-activity isolation fixture.");
        QDir().mkpath(legacyRoot);
        QDir().mkpath(appDataRoot);
    }
};

ActivityStore *profileActivity(
    QQmlApplicationEngine *engine) {
    QObject *object =
        engine->rootContext()
            ->contextProperty(
                QStringLiteral("ProfileActivity"))
            .value<QObject *>();
    return qobject_cast<ActivityStore *>(
        object);
}
}

class tst_profile_activity_isolation : public QObject {
    Q_OBJECT

private slots:
    void sealedStartupBindsAHealthyActivityStore();
    void accountSwitchRebindsAndDestroysPreviousActivityStore();
    void noStaleCrossProfileActivityLeakage();
};

void tst_profile_activity_isolation::
sealedStartupBindsAHealthyActivityStore() {
    ProfileActivityFixture fixture;

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Sealed);

    ActivityStore *sealed =
        profileActivity(&engine);
    QVERIFY(sealed);
    QVERIFY(sealed->healthy());
    QCOMPARE(sealed->revision(), quint64(0));
}

void tst_profile_activity_isolation::
accountSwitchRebindsAndDestroysPreviousActivityStore() {
    ProfileActivityFixture fixture;

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);

    ActivityStore *sealed =
        profileActivity(&engine);
    QVERIFY(sealed);
    QPointer<ActivityStore> oldSealed(sealed);

    QString error;
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    // The sealed session's store is gone, not merely hidden — the same
    // "destroyed, not hidden" guarantee tst_account_shared_pc.cpp already
    // proves for SearchHistoryStore.
    QVERIFY(oldSealed.isNull());

    ActivityStore *accountA =
        profileActivity(&engine);
    QVERIFY(accountA);
    QVERIFY(accountA != sealed);
    QVERIFY(accountA->healthy());
    QVERIFY2(
        accountA->recordPlaybackDelta(
            fixtureMovieFact(
                QStringLiteral("account-a-session"))),
        "recording an activity fact into account A's store should succeed");
    QCOMPARE(accountA->revision(), quint64(1));

    QPointer<ActivityStore> oldAccountA(accountA);

    QVERIFY2(
        profiles.sealAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Sealed);
    QVERIFY(oldAccountA.isNull());

    ActivityStore *resealed =
        profileActivity(&engine);
    QVERIFY(resealed);
    QVERIFY(resealed != accountA);
    QCOMPARE(resealed->revision(), quint64(0));

    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    // Reopening the SAME durable account profile is a fresh ActivityStore
    // object (distinct C++ instance — QML never sees a stale pointer), but
    // the durable fact recorded earlier is still there on disk.
    ActivityStore *accountAReopened =
        profileActivity(&engine);
    QVERIFY(accountAReopened);
    QVERIFY(accountAReopened != accountA);
    const QString monthKey =
        accountAReopened->earliestActivityMonth();
    QVERIFY(!monthKey.isEmpty());
    const QVariantMap projection =
        accountAReopened->projectMonth(monthKey);
    QCOMPARE(
        projection.value(
            QStringLiteral("watchSeconds"))
            .toLongLong(),
        qint64(30));
}

void tst_profile_activity_isolation::
noStaleCrossProfileActivityLeakage() {
    ProfileActivityFixture fixture;

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    SharedPcProfileCoordinator profiles(
        &runtime,
        fixture.appDataRoot);
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);

    QString error;
    QVERIFY2(
        profiles.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    ActivityStore *accountA =
        profileActivity(&engine);
    QVERIFY(accountA);
    QVERIFY2(
        accountA->recordPlaybackDelta(
            fixtureMovieFact(
                QStringLiteral("account-a-only-session"))),
        "recording an activity fact into account A's store should succeed");
    QVERIFY(!accountA->earliestActivityMonth().isEmpty());

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

    ActivityStore *accountB =
        profileActivity(&engine);
    QVERIFY(accountB);
    QVERIFY(accountB != accountA);
    QVERIFY(accountB->healthy());

    // Account B's store is a brand-new empty ledger — none of account A's
    // activity is reachable from it (CPP-PORT-CONTRACT §2/§17 — profile
    // switch rebinds ProfileActivity, it never merges ledgers).
    QCOMPARE(accountB->revision(), quint64(0));
    QVERIFY(accountB->earliestActivityMonth().isEmpty());
}

QTEST_MAIN(tst_profile_activity_isolation)
#include "tst_profile_activity_isolation.moc"
