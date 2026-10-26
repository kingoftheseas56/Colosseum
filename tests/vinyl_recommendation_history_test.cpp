#include <QtTest>
#include <QTemporaryDir>
#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include "vinyl/VinylRecommendationHistory.h"
#include "RecommendationDerivation.h"

namespace {
constexpr qint64 now=1792756800000LL;
QVariantMap ref(const QString &kind,const QString &id) {
    return {{"identity","provider"},{"kind",kind},{"providerId","local"},{"itemId",id}};
}
QVariantMap common(const QString &session) {
    return {{"sessionId",session},{"world","vinyl"},{"kind","music"},{"titleKey","album"},
        {"itemKey","track"},{"title","Track"},{"syncable",false},{"utcOffsetMinutes",0}};
}
void action(ActivityStore &store,const QString &session,const QString &value,qint64 at=now-1000,qint64 pos=12000) {
    auto fact=common(session);fact.insert("action",value);fact.insert("origin","manual");fact.insert("atMs",at);
    fact.insert("positionMs",pos);fact.insert("durationMs",60000);fact.insert("entityRef",ref("recording","track"));
    fact.insert("releaseRef",ref("release","album"));fact.insert("artistRefs",QVariantList{ref("artist","artist")});
    QVERIFY(store.recordMusicAction(fact));
}
void delta(ActivityStore &store,const QString &session,qint64 active=12000) {
    auto fact=common(session);fact.insert("startAtMs",now-20000);fact.insert("endAtMs",now-20000+active);
    fact.insert("activeMs",active);fact.insert("rateMilli",1000);QVERIFY(store.recordPlaybackDelta(fact));
}
}
class VinylRecommendationHistoryTest:public QObject {
    Q_OBJECT
private slots:
    void seekDoesNotCountAsPlay() {
        QTemporaryDir directory;ActivityStore activity;HistoryStore history(directory.filePath("history.ini"));
        action(activity,"seek","start",now-30000,0);action(activity,"seek","stop",now-1000,59000);
        auto result=VinylRecommendationHistory::project(&activity,&history,{},now);
        QCOMPARE(result.value("qualifiedManualSessions").toInt(),0);
        auto derived=VinylRecommendationHistory::derive("local",result,now);QVERIFY(derived.ok());
        QVERIFY(derived.value->aggregates().isEmpty());
    }
    void failureIsNotSkipAndStopIsNotNaturalCompletion() {
        QTemporaryDir directory;ActivityStore activity;HistoryStore history(directory.filePath("history.ini"));
        action(activity,"failure","start",now-30000);delta(activity,"failure");action(activity,"failure","failure");
        action(activity,"stop","start",now-30000);delta(activity,"stop");action(activity,"stop","stop");
        auto result=VinylRecommendationHistory::project(&activity,&history,{},now);
        QCOMPARE(result.value("qualifiedManualSessions").toInt(),2);
        auto derived=VinylRecommendationHistory::derive("local",result,now);QVERIFY(derived.ok());
        for(const auto &entry:derived.value->aggregates()) {QCOMPARE(entry.naturalCompletionCount(),0);QCOMPARE(entry.earlySkipCount(),0);}
    }
    void oneSkipIsNotDislikeAndRepeatedSkipsRemainBehavior() {
        QTemporaryDir directory;ActivityStore activity;HistoryStore history(directory.filePath("history.ini"));
        action(activity,"one","start",now-30000);action(activity,"one","skip");
        auto result=VinylRecommendationHistory::project(&activity,&history,{},now);
        auto derived=VinylRecommendationHistory::derive("local",result,now);QVERIFY(derived.ok());
        for(const auto &entry:derived.value->aggregates())QCOMPARE(entry.earlySkipCount(),0);
        action(activity,"two","start",now-30000);action(activity,"two","skip");
        derived=VinylRecommendationHistory::derive("local",VinylRecommendationHistory::project(&activity,&history,{},now),now);
        QVERIFY(derived.ok());QVERIFY(!derived.value->aggregates().isEmpty());
        for(const auto &entry:derived.value->aggregates()) {QCOMPARE(entry.playCount(),0);QCOMPARE(entry.earlySkipCount(),2);}
    }
    void repeatSessionsDedupeBySessionNotTimer() {
        QTemporaryDir directory;ActivityStore activity;HistoryStore history(directory.filePath("history.ini"));
        action(activity,"repeat","start",now-30000);delta(activity,"repeat");delta(activity,"repeat");
        action(activity,"repeat","natural-eof");
        auto result=VinylRecommendationHistory::project(&activity,&history,{},now);
        QCOMPARE(result.value("qualifiedManualSessions").toInt(),1);
        auto derived=VinylRecommendationHistory::derive("local",result,now);QVERIFY(derived.ok());
        for(const auto &entry:derived.value->aggregates()) {QCOMPARE(entry.playCount(),1);QCOMPARE(entry.naturalCompletionCount(),1);}
        action(activity,"replay","start",now-30000);delta(activity,"replay");
        result=VinylRecommendationHistory::project(&activity,&history,{},now);
        QCOMPARE(result.value("qualifiedManualSessions").toInt(),2);
    }
    void historyOffAndResetRemoveDerivedSignals() {
        QTemporaryDir directory;ActivityStore activity;HistoryStore history(directory.filePath("history.ini"));action(activity,"one","start",now-30000);delta(activity,"one");
        QCOMPARE(VinylRecommendationHistory::project(&activity,&history,{},now).value("qualifiedManualSessions").toInt(),1);
        activity.setRetentionEnabled(false);
        auto result=VinylRecommendationHistory::project(&activity,&history,{},now);
        QCOMPARE(result.value("qualifiedManualSessions").toInt(),0);QVERIFY(!result.value("historyComplete").toBool());
        activity.setRetentionEnabled(true);QVERIFY(activity.clearAll());
        QCOMPARE(VinylRecommendationHistory::project(&activity,&history,{},now).value("qualifiedManualSessions").toInt(),0);
    }
    void unknownAndConflictingIdentityNeverMergeByName() {
        QTemporaryDir directory;ActivityStore activity;HistoryStore history(directory.filePath("history.ini"));delta(activity,"unknown");
        auto result=VinylRecommendationHistory::project(&activity,&history,{},now);
        QCOMPARE(result.value("qualifiedManualSessions").toInt(),0);
        QVERIFY(result.value("sessions").toList().isEmpty());
        action(activity,"unknown","start",now-30000);
        auto conflict=common("unknown");conflict.insert("atMs",now-1000);conflict.insert("action","stop");
        conflict.insert("origin","manual");conflict.insert("positionMs",12000);conflict.insert("durationMs",60000);
        conflict.insert("entityRef",ref("recording","other-track"));QVERIFY(activity.recordMusicAction(conflict));
        QVERIFY(VinylRecommendationHistory::project(&activity,&history,{},now).value("sessions").toList().isEmpty());
    }
};
QTEST_GUILESS_MAIN(VinylRecommendationHistoryTest)
#include "vinyl_recommendation_history_test.moc"
