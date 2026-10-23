#include "account/ActivityStore.h"
#include "account/ConsumptionHistoryBridge.h"
#include "account/HistoryStore.h"

#include <QTemporaryDir>
#include <QtTest>

namespace {

QVariantMap canonicalRef(const QString &kind, const QString &mbid)
{
    return {{QStringLiteral("identity"), QStringLiteral("canonical")},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("mbid"), mbid}};
}

QVariantMap providerRef(const QString &kind, const QString &itemId)
{
    return {{QStringLiteral("identity"), QStringLiteral("provider")},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("providerId"), QStringLiteral("youtube-music")},
            {QStringLiteral("itemId"), itemId}};
}

QVariantMap musicAction()
{
    return {{QStringLiteral("eventId"), QStringLiteral("music-action-event-1")},
            {QStringLiteral("sessionId"), QStringLiteral("music-session-1")},
            {QStringLiteral("world"), QStringLiteral("vinyl")},
            {QStringLiteral("kind"), QStringLiteral("music")},
            {QStringLiteral("titleKey"), QStringLiteral("release-group:album-1")},
            {QStringLiteral("itemKey"), QStringLiteral("recording:track-1")},
            {QStringLiteral("title"), QStringLiteral("Track One")},
            {QStringLiteral("utcOffsetMinutes"), 0},
            {QStringLiteral("syncable"), false},
            {QStringLiteral("atMs"), qint64(1767225600000LL)},
            {QStringLiteral("action"), QStringLiteral("start")},
            {QStringLiteral("origin"), QStringLiteral("manual")},
            {QStringLiteral("positionMs"), qint64(0)},
            {QStringLiteral("durationMs"), qint64(180000)},
            {QStringLiteral("entityRef"), canonicalRef(
                QStringLiteral("recording"), QStringLiteral("550e8400-e29b-41d4-a716-446655440000"))},
            {QStringLiteral("artistRefs"), QVariantList{providerRef(
                QStringLiteral("artist"), QStringLiteral("UC1"))}},
            {QStringLiteral("releaseRef"), canonicalRef(
                QStringLiteral("release_group"), QStringLiteral("550e8400-e29b-41d4-a716-446655440001"))}};
}

} // namespace

class tst_vinyl_music_actions final : public QObject
{
    Q_OBJECT

private slots:
    void typedActionPersistsButDoesNotProjectUsageOrHistory();
    void acceptsSupportedActionAndOriginVocabulary();
    void malformedActionFieldsAreRejected();
    void actionsAreExcludedFromSyncImportAndExport();
    void clearResetAndRetentionOptOutKeepTheirExistingSemantics();
};

void tst_vinyl_music_actions::typedActionPersistsButDoesNotProjectUsageOrHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));
    const QString historyPath = dir.filePath(QStringLiteral("history.ini"));

    {
        ActivityStore activity(dbPath);
        HistoryStore history(historyPath);
        ConsumptionHistoryBridge bridge(&activity, nullptr, &history);

        QVERIFY(activity.recordMusicAction(musicAction()));
        QCOMPARE(activity.historyProjectionFacts().size(), 1);
        const QVariantMap stored = activity.historyProjectionFacts().first();
        QCOMPARE(stored.value(QStringLiteral("type")).toString(), QStringLiteral("music_action"));
        QCOMPARE(stored.value(QStringLiteral("action")).toString(), QStringLiteral("start"));
        QCOMPARE(stored.value(QStringLiteral("origin")).toString(), QStringLiteral("manual"));
        QCOMPARE(stored.value(QStringLiteral("positionMs")).toLongLong(), qint64(0));
        QCOMPARE(stored.value(QStringLiteral("durationMs")).toLongLong(), qint64(180000));
        QCOMPARE(stored.value(QStringLiteral("entityRef")).toMap(),
                 musicAction().value(QStringLiteral("entityRef")).toMap());
        QCOMPARE(stored.value(QStringLiteral("artistRefs")).toList().size(), 1);
        QCOMPARE(stored.value(QStringLiteral("releaseRef")).toMap(),
                 musicAction().value(QStringLiteral("releaseRef")).toMap());

        const QVariantMap month = activity.projectMonth(QStringLiteral("2026-01"));
        QCOMPARE(month.value(QStringLiteral("listenSeconds")).toLongLong(), qint64(0));
        QCOMPARE(month.value(QStringLiteral("completedCount")).toLongLong(), qint64(0));
        QCOMPARE(month.value(QStringLiteral("activeDays")).toLongLong(), qint64(0));
        QVERIFY(month.value(QStringLiteral("highlights")).toList().isEmpty());
        QVERIFY(month.value(QStringLiteral("recentActivity")).toList().isEmpty());
        QVERIFY(activity.earliestActivityMonth().isEmpty());
        QVERIFY(history.records().isEmpty());
        QString syncError;
        QVERIFY2(activity.portableSyncFacts(&syncError).isEmpty(), qPrintable(syncError));
        QVERIFY(bridge.replayExisting());
        QVERIFY(history.records().isEmpty());
    }

    ActivityStore reopened(dbPath);
    QVERIFY(reopened.healthy());
    const QList<QVariantMap> replayed = reopened.historyProjectionFacts();
    QCOMPARE(replayed.size(), 1);
    QCOMPARE(replayed.first().value(QStringLiteral("type")).toString(), QStringLiteral("music_action"));
    QCOMPARE(replayed.first().value(QStringLiteral("entityRef")).toMap(),
             musicAction().value(QStringLiteral("entityRef")).toMap());
    QCOMPARE(replayed.first().value(QStringLiteral("artistRefs")).toList().size(), 1);
    QCOMPARE(replayed.first().value(QStringLiteral("releaseRef")).toMap(),
             musicAction().value(QStringLiteral("releaseRef")).toMap());
}

void tst_vinyl_music_actions::acceptsSupportedActionAndOriginVocabulary()
{
    ActivityStore activity;
    const QStringList actions{QStringLiteral("start"), QStringLiteral("skip"),
                              QStringLiteral("natural-eof"), QStringLiteral("stop"),
                              QStringLiteral("failure")};
    const QStringList origins{QStringLiteral("manual"), QStringLiteral("album-continuation"),
                              QStringLiteral("autoplay")};
    int ordinal = 0;
    for (const QString &action : actions) {
        for (const QString &origin : origins) {
            QVariantMap fact = musicAction();
            fact.insert(QStringLiteral("eventId"),
                        QStringLiteral("music-action-%1-%2").arg(action, origin));
            fact.insert(QStringLiteral("atMs"), qint64(1767225600000LL + ordinal));
            fact.insert(QStringLiteral("action"), action);
            fact.insert(QStringLiteral("origin"), origin);
            QVERIFY2(activity.recordMusicAction(fact),
                     qPrintable(QStringLiteral("rejected %1 / %2").arg(action, origin)));
            ++ordinal;
        }
    }
    QCOMPARE(activity.historyProjectionFacts().size(), actions.size() * origins.size());
}

void tst_vinyl_music_actions::malformedActionFieldsAreRejected()
{
    ActivityStore activity;
    const auto rejected = [&activity](QVariantMap fact) {
        QVERIFY(!activity.recordMusicAction(fact));
    };

    QVariantMap fact = musicAction();
    fact.insert(QStringLiteral("action"), QStringLiteral("pause"));
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("origin"), QStringLiteral("queue"));
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("world"), QStringLiteral("biblio"));
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("kind"), QStringLiteral("audiobook"));
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("syncable"), true);
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("positionMs"), qint64(-1));
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("positionMs"), 1.5);
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("durationMs"), 9007199254740992.0);
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("entityRef"), canonicalRef(QStringLiteral("unknown"),
        QStringLiteral("550e8400-e29b-41d4-a716-446655440000")));
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("entityRef"), canonicalRef(QStringLiteral("recording"),
        QStringLiteral("not-a-uuid")));
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("artistRefs"), QVariantList{canonicalRef(QStringLiteral("recording"),
        QStringLiteral("550e8400-e29b-41d4-a716-446655440000"))});
    rejected(fact);
    fact = musicAction();
    fact.insert(QStringLiteral("releaseRef"), providerRef(QStringLiteral("release"),
        QString(1025, QLatin1Char('x'))));
    rejected(fact);

    QCOMPARE(activity.revision(), quint64(0));
    QVERIFY(activity.historyProjectionFacts().isEmpty());
}

void tst_vinyl_music_actions::actionsAreExcludedFromSyncImportAndExport()
{
    ActivityStore activity;
    QVERIFY(activity.recordMusicAction(musicAction()));
    QString error;
    QVERIFY(activity.portableSyncFacts(&error).isEmpty());
    QVERIFY(error.isEmpty());

    QVariantMap incoming = musicAction();
    incoming.insert(QStringLiteral("syncable"), true);
    QVERIFY(!activity.applySyncedPortableFact(incoming, &error));
    QCOMPARE(error, QStringLiteral("invalid_event"));

    incoming = musicAction();
    QVERIFY(!activity.applySyncedPortableFact(incoming, &error));
    QCOMPARE(error, QStringLiteral("invalid_event"));
    QCOMPARE(activity.historyProjectionFacts().size(), 1);
}

void tst_vinyl_music_actions::clearResetAndRetentionOptOutKeepTheirExistingSemantics()
{
    ActivityStore activity;
    QVERIFY(activity.recordMusicAction(musicAction()));
    QVERIFY(activity.clearAll());
    QVERIFY(activity.historyProjectionFacts().isEmpty());

    activity.setRetentionEnabled(false);
    QVERIFY(activity.recordMusicAction(musicAction()));
    QVERIFY(activity.historyProjectionFacts().isEmpty());

    activity.setRetentionEnabled(true);
    QVariantMap postClearAction = musicAction();
    postClearAction.insert(QStringLiteral("atMs"), qint64(1800000000000LL));
    QVERIFY(activity.recordMusicAction(postClearAction));
    QVERIFY(!activity.historyProjectionFacts().isEmpty());
    QVERIFY(activity.applySyncedReset(1, qint64(1800000000001LL)));
    QVERIFY(activity.historyProjectionFacts().isEmpty());
}

QTEST_MAIN(tst_vinyl_music_actions)
#include "tst_vinyl_music_actions.moc"
