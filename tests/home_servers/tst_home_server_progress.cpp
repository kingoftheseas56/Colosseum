#include <QtTest>
#include "HomeServerProgressOwner.h"
#include "HomeServerCatalog.h"

class Owner final : public HomeServerProgressOwner {
public:
    QHash<QString,QVariantMap> rows;
    QHash<QString,Watch> marks;
    QVariantList entries() const override { QVariantList r; for(const auto &v:rows) r.append(v); return r; }
    QVariantMap get(const QString &id) const override { return rows.value(id); }
    bool apply(const QVariantMap &v) override { rows[v.value("id").toString()]=v; return true; }
    bool remove(const QString &id) override { rows.remove(id); return true; }
    Watch watch(const QString &id) const override { return marks.value(id); }
    bool applyWatched(const QString &id,bool played,qint64 at) override { marks[id]={played?1:-1,at,false}; return true; }
};
class ProgressTest : public QObject {
    Q_OBJECT
    static QJsonObject item(qint64 at=2000,qint64 position=40000,bool played=false) {
        return {{"id","server-item"},{"kind","movie"},{"title","Movie"},
            {"identity",QJsonObject{{"imdbId","tt123"}}},
            {"progress",QJsonObject{{"positionMs",position},{"durationMs",100000},{"updatedAt",at},{"played",played}}}};
    }
    static QVariantMap local(qint64 at=3000) {
        return {{"id","tt123"},{"kind","video"},{"title","Local title"},{"updatedAt",at},
            {"duration",100},{"progress",.6},{"resume",QVariantMap{{"infoHash","ordinary-hash"},{"localPath","C:/fixture/movie.mp4"},{"position",60}}}};
    }
private slots:
    void provenFallbackAliasesKeepCanonicalProgress() {
        Owner o; auto row=local();
        row["_homeServerIdentity"]=QVariantMap{{"type","movie"},{"imdbId","tt123"},{"tmdbId",101}}; o.apply(row);
        const auto aliases=HomeServerProgress::identityAliases(o.entries());
        auto remote=item(); remote["identity"]=QJsonObject{{"tmdbId",101}};
        auto enriched=HomeServerProgress::withAliases(remote,aliases);
        QCOMPARE(HomeServerCatalog::canonicalVideoId(enriched),QString("tt123"));
        auto d=HomeServerProgress::reconcile(o,enriched,"home-server:c:i:v"); QVERIFY(d.localWins); QCOMPARE(d.canonicalId,QString("tt123"));
        remote["kind"]="episode"; remote["identity"]=QJsonObject{{"tmdbId",101},{"season",1},{"episode",2}};
        QCOMPARE(HomeServerCatalog::canonicalVideoId(HomeServerProgress::withAliases(remote,aliases)),QString("tmdb:tv:101:1:2"));
        remote=item(); remote["identity"]=QJsonObject{{"imdbId","tt999"},{"tmdbId",101}};
        QCOMPARE(HomeServerCatalog::canonicalVideoId(HomeServerProgress::withAliases(remote,aliases)),QString("tt999"));
        row["id"]="ttWrong"; o.rows.clear(); o.apply(row); QVERIFY(HomeServerProgress::identityAliases(o.entries()).isEmpty());
    }
    void importsOriginalTimeAndOpaqueResume() {
        Owner o; auto d=HomeServerProgress::reconcile(o,item(),"home-server:c:i:v");
        QCOMPARE(d.canonicalId,QString("tt123")); QVERIFY(!d.localWins);
        auto e=o.get("tt123"); QCOMPARE(e.value("updatedAt").toLongLong(),2000LL);
        QCOMPARE(e.value("resume").toMap().value("position").toDouble(),40.);
        QCOMPARE(e.value("resume").toMap().value("infoHash").toString(),QString("home-server:c:i:v"));
    }
    void newerLocalWinsAndStableTie() {
        Owner o; o.apply(local()); auto original=o.get("tt123");
        auto d=HomeServerProgress::reconcile(o,item(),"home-server:c:i:v");
        QVERIFY(d.localWins); QCOMPARE(d.state.positionMs,60000LL); QCOMPARE(o.get("tt123"),original);
        d=HomeServerProgress::reconcile(o,item(3000),"home-server:c:i:v"); QVERIFY(!d.localWins); QCOMPARE(o.get("tt123"),original);
    }
    void newerRemotePreservesOrdinaryResume() {
        Owner o; o.apply(local(1000)); HomeServerProgress::reconcile(o,item(),"home-server:c:i:v");
        auto e=o.get("tt123"),r=e.value("resume").toMap();
        QCOMPARE(r.value("infoHash").toString(),QString("ordinary-hash")); QCOMPARE(r.value("localPath").toString(),QString("C:/fixture/movie.mp4"));
        QCOMPARE(r.value("position").toDouble(),40.); QCOMPARE(e.value("updatedAt").toLongLong(),2000LL);
        HomeServerProgress::hideConnection(o,"c"); QVERIFY(!o.get("tt123").isEmpty());
    }
    void unknownTimestampCannotOverwriteKnown() {
        Owner o; o.apply(local()); auto original=o.get("tt123");
        QVERIFY(HomeServerProgress::reconcile(o,item(0),"home-server:c:i:v").localWins); QCOMPARE(o.get("tt123"),original);
        Owner empty; HomeServerProgress::reconcile(empty,item(0),"home-server:c:i:v");
        QCOMPARE(empty.get("tt123").value("updatedAt").toLongLong(),0LL);
    }
    void disconnectedHidesOnlySelectedServerRows() {
        Owner o; HomeServerProgress::reconcile(o,item(),"home-server:c:i:v");
        auto other=local(); other["id"]="tt456"; other["resume"]=QVariantMap{{"infoHash","home-server:d:i:v"},{"position",30}}; o.apply(other);
        HomeServerProgress::hideConnection(o,"c"); QVERIFY(o.get("tt123").value("_homeServerHidden").toBool()); QVERIFY(!o.get("tt456").value("_homeServerHidden").toBool());
        HomeServerProgress::reconcile(o,item(),"home-server:d:i:v");
        QVERIFY(!o.get("tt123").value("_homeServerHidden").toBool());
        QCOMPARE(o.get("tt123").value("resume").toMap().value("infoHash").toString(),QString("home-server:d:i:v"));
    }
    void unknownTitlesAndUntouchedItemsStayOut() {
        Owner o; auto x=item(); x["identity"]=QJsonObject{}; HomeServerProgress::reconcile(o,x,"home-server:c:i:v"); QVERIFY(o.rows.isEmpty());
        HomeServerProgress::reconcile(o,item(0,0),"home-server:c:i:v"); QVERIFY(o.rows.isEmpty());
    }
    void playedRewatchAndExplicitUnwatch() {
        Owner o; o.apply(local()); auto d=HomeServerProgress::reconcile(o,item(2000,40000,true),"home-server:c:i:v");
        QVERIFY(d.state.played); QVERIFY(!d.state.writePlayed); // short rewatch must not unwatch.
        o.marks["tt123"]={-1,4000,true}; d=HomeServerProgress::reconcile(o,item(2000,40000,true),"home-server:c:i:v");
        QVERIFY(!d.state.played); QVERIFY(d.state.writePlayed); QCOMPARE(d.state.updatedAt,4000LL);
    }
    void episodeCompletionDoesNotMarkWholeShow() {
        Owner o; auto x=item(); x["kind"]="episode";
        x["identity"]=QJsonObject{{"imdbId","tt123"},{"season",0},{"episode",2}};
        HomeServerProgress::completed(o,x,"home-server:c:i:v",7000);
        QVERIFY(o.get("tt123:0:2").value("watched").toBool()); QVERIFY(o.marks.isEmpty());
        QCOMPARE(o.get("tt123:0:2").value("updatedAt").toLongLong(),7000LL);
    }
    void movieEndMarksWatched() {
        Owner o; HomeServerProgress::completed(o,item(),"home-server:c:i:v",7000);
        QCOMPARE(o.watch("tt123").mark,1); QCOMPARE(o.watch("tt123").updatedAt,7000LL);
    }
};
QTEST_GUILESS_MAIN(ProgressTest)
#include "tst_home_server_progress.moc"
