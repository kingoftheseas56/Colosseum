#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include "vinyl/VinylSourcePool.h"
#include "vinyl/VinylRecommendationIdentity.h"
#include "vinyl/VinylProvider.h"

namespace {
QVariantMap item(QString provider, QString id, QString title, QString kind = "album")
{
    return {{"provider",provider},{"providerId",id},{"kind",kind},{"title",title},{"name",title}};
}
QVariantMap artist(int index)
{
    auto row=item("youtube-music","UC"+QString::number(index).rightJustified(22,'0'),
                  "Fixture Artist "+QString::number(index),"artist");
    row["key"]=VinylRecommendationIdentity::fromCatalogue(row).value("key");
    return row;
}
QVariantMap feed(QString key, QString storefront, QVariantList items)
{
    return {{"key",key},{"storefront",storefront},{"kind","albums"},
            {"source","apple-music"},{"status","ready"},{"complete",true},
            {"fetchedAt","2026-11-09T00:00:00.000Z"},{"week","2026-W46"},{"items",items}};
}
}

class VinylSourcePoolTest final : public QObject {
    Q_OBJECT
    QTemporaryDir transportRoot;
    QString statePath, logPath;
    static QJsonArray examples()
    {
        QFile file(QFINDTESTDATA("fixtures/vinyl-source-pool/transport-envelopes.json"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object().value("examples").toArray();
    }
    QVariantMap chart(const QString &storefront = "us") const
    {
        auto data = examples().first().toObject().value("response").toObject().toVariantMap();
        auto provenance = data.value("provenance").toMap();
        provenance["feed"] = "apple:" + storefront + ":albums";
        data["provenance"] = provenance;
        auto items = data.value("items").toList();
        for (auto &value : items) { auto row = value.toMap(); row["storefront"] = storefront; value = row; }
        data["items"] = items;
        return data;
    }
    static QVariantMap discography(const QString &id)
    {
        return {{"version",1},{"seed",QVariantMap{{"identity","canonical"},{"kind","artist"},{"mbid",id}}},
            {"items",QVariantList()},{"provenance",QVariantMap{{"source","musicbrainz"},{"generator","artist-evidence"}}},
            {"fetchedAt","2026-11-13T00:00:00.000Z"},{"status","ready"},{"complete",true},
            {"artist",QVariantMap{{"id",id},{"releaseGroups",QVariantList{QVariantMap{
                {"id","11111111-1111-4111-8111-111111111111"},{"title","Fixture Album"},{"first-release-date","2026-05-13"}}}},
                {"releaseGroupsComplete",true},{"releaseGroupCount",1}}}};
    }
    bool state(QVariantMap data)
    {
        data["logPath"] = logPath;
        QFile file(statePath);
        return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument::fromVariant(data).toJson()) > 0;
    }
    QVariantList events() const
    {
        QFile file(logPath); if (!file.open(QIODevice::ReadOnly)) return {};
        QVariantList result;
        while (!file.atEnd()) {
            const auto entry = QJsonDocument::fromJson(file.readLine());
            if (entry.isObject()) result.append(entry.object().toVariantMap());
        }
        return result;
    }
    static QVariantMap reply(const QSignalSpy &spy, int index = -1)
    { return spy.at(index < 0 ? spy.size() - 1 : index).at(2).toMap(); }
    static QString failure(const QSignalSpy &spy, int index = -1)
    { return spy.at(index < 0 ? spy.size() - 1 : index).at(3).toString(); }
private slots:
    void initTestCase()
    {
        QVERIFY(transportRoot.isValid());
        QVERIFY(!qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON").isEmpty());
    }
    void init()
    {
        const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
        statePath = transportRoot.filePath(key + "-state.json");
        logPath = transportRoot.filePath(key + "-requests.jsonl");
        qputenv("VINYL_POOL_TEST_STATE",statePath.toUtf8());
    }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); }
    void cleanupTestCase() { qunsetenv("VINYL_POOL_TEST_STATE"); }
    void nativeAndEnglishNamesSurviveVerifiedIdentity()
    {
        const QVariantMap identity{{"status","verified"},{"mbid","00000000-0000-0000-0000-000000000001"},
            {"artist",QVariantMap{{"id","00000000-0000-0000-0000-000000000001"},{"name",QString::fromUtf8("竹内まりや")},
                {"aliases",QStringList{QString::fromUtf8("竹内まりや"),"Mariya Takeuchi","Takeuchi, Mariya","Mariya Takeuchi"}},
                {"aliasesComplete",true}}}};
        QCOMPARE(VinylSourcePool::artistNames(identity),
                 QStringList({QString::fromUtf8("竹内まりや"),"Mariya Takeuchi","Takeuchi, Mariya"}));
        auto unresolved=identity;unresolved["status"]="unresolved";
        QVERIFY(VinylSourcePool::artistNames(unresolved).isEmpty());
    }
    void verifiedCrossSourceIdentityDeduplicatesWithoutNameGuessing()
    {
        auto apple=item("apple-music","123","Shared Album");
        auto lastfm=item("lastfm","https://www.last.fm/music/Fixture/Shared+Album","Shared Album");
        for(auto *row:{&apple,&lastfm}) (*row)["mapping"]=QVariantMap{
            {"status","verified"},{"provider",row->value("provider")},{"providerId",row->value("providerId")},
            {"kind","release"},{"mbid","00000000-0000-0000-0000-000000000002"},
            {"method","external-id"},{"fetchedAtMs",1}};
        const auto merged=VinylSourcePool::mergeCandidates({apple,lastfm});
        QCOMPARE(merged.size(),1);
        QCOMPARE(merged.first().toMap().value("ref").toMap().value("identity").toString(),QString("canonical"));
        QCOMPARE(merged.first().toMap().value("poolSources").toStringList(),QStringList({"apple-music","lastfm"}));
        apple.remove("mapping");lastfm.remove("mapping");
        QCOMPARE(VinylSourcePool::mergeCandidates({apple,lastfm}).size(),2);
    }
    void metadataCannotAttestPlaybackOrCanonicalIdentity()
    {
        const QVariantMap card{{"catalogueCard",true},{"source","apple-music"},
            {"id","apple-music:albums:0123456789abcdef01234567"},{"kind","albums"},
            {"title","Fixture Album"},{"artist","Fixture Artist"},
            {"sourceIds",QVariantMap{{"id","12345"},{"artistId","23456"}}},
            {"hints",QVariantMap{{"artists",QStringList{"Fixture Artist"}},
                {"releaseDate","2026-11-01"},
                {"mbidEvidence",QVariantMap{{"artist","00000000-0000-0000-0000-000000000001"}}}}}};
        const auto candidate=VinylSourcePool::catalogueCandidate(card);
        QCOMPARE(candidate.value("provider").toString(),QString("apple-music"));
        QCOMPARE(candidate.value("providerId").toString(),QString("12345"));
        QCOMPARE(candidate.value("kind").toString(),QString("album"));
        QCOMPARE(candidate.value("catalogueItem").toMap(),card);
        QVERIFY(candidate.value("requiresResolution").toBool());
        const auto converted=VinylRecommendationIdentity::fromCatalogue(candidate);
        QVERIFY(converted.value("ok").toBool());
        QCOMPARE(converted.value("ref").toMap().value("identity").toString(),QString("provider"));
        QVERIFY(!converted.value("destination").toMap().value("playable").toBool());
        auto forged=card;forged["mapping"]=QVariantMap{{"status","verified"}};
        QVERIFY(VinylSourcePool::catalogueCandidate(forged).isEmpty());
        forged=card;forged["playable"]=true;
        QVERIFY(VinylSourcePool::catalogueCandidate(forged).isEmpty());
    }
    void chartsMergeByRankWithOneVotePerStorefront()
    {
        const auto a=item("apple-music","100","Album A"),b=item("apple-music","200","Album B"),c=item("apple-music","300","Album C");
        const QVariantList feeds{feed("apple:us:albums","us",{a,b,c,a}),feed("apple:gb:albums","gb",{b,c,a})};
        const auto merged=VinylSourcePool::mergeCharts(feeds,"albums");
        QCOMPARE(merged.size(),3);
        QCOMPARE(merged[0].toMap().value("providerId").toString(),QString("200"));
        QCOMPARE(merged[0].toMap().value("chartScore").toInt(),199);
        QCOMPARE(merged[0].toMap().value("chartCoverage").toInt(),2);
        QCOMPARE(merged[1].toMap().value("providerId").toString(),QString("100"));
        QCOMPARE(VinylSourcePool::mergeCharts(feeds,"songs").size(),0);
    }
    void aFailedFeedRetainsItsLastGoodWeekAndNoCacheSkips()
    {
        const auto good=feed("apple:us:albums","us",{item("apple-music","100","Album A")});
        const QVariantMap failed{{"key","apple:us:albums"},{"status","unavailable"},{"items",QVariantList()}};
        const auto retained=VinylSourcePool::updateCharts({good},{failed});
        QCOMPARE(retained.size(),1);
        QCOMPARE(retained.first().toMap().value("week").toString(),QString("2026-W46"));
        QVERIFY(retained.first().toMap().value("_stale").toBool());
        QCOMPARE(retained.first().toMap().value("items"),good.value("items"));
        QVERIFY(VinylSourcePool::updateCharts({}, {failed}).isEmpty());
    }
    void lastfmGlobalSongOrderLeadsAppleCorroborationAndFallback()
    {
        const auto apple = item("apple-music","100","Storefront Song","song");
        const auto global = item("lastfm","https://www.last.fm/music/Fixture/_/Global","Global Song","song");
        QVariantList feeds;
        for (const auto &country : QStringList{"us","gb","jp","kr","in","br","ng","de","fr","mx"}) {
            auto value = feed("apple:"+country+":songs",country,{apple});
            value["kind"] = "songs"; feeds.append(value);
        }
        QCOMPARE(VinylSourcePool::mergeCharts(feeds,"songs").first().toMap().value("provider").toString(),QString("apple-music"));
        auto lastfm = feed("lastfm:tracks","",{global});
        lastfm["source"] = "lastfm"; lastfm["kind"] = "songs"; feeds.append(lastfm);
        const auto merged = VinylSourcePool::mergeCharts(feeds,"songs");
        QCOMPARE(merged.size(),2);
        QCOMPARE(merged.first().toMap().value("provider").toString(),QString("lastfm"));
        QCOMPARE(merged.first().toMap().value("chartPrimaryRank").toInt(),1);
        QVERIFY(!merged.last().toMap().contains("chartPrimaryRank"));
    }
    void newMeansTwelveCalendarMonthsWithoutInventingPartialDates()
    {
        const QDate today(2026,11,13);
        QVERIFY(VinylSourcePool::isNewRelease("2025-11-13",today));
        QVERIFY(VinylSourcePool::isNewRelease("2026-05-13",today));
        QVERIFY(!VinylSourcePool::isNewRelease("2025-11-12",today));
        QVERIFY(!VinylSourcePool::isNewRelease("2026-11-14",today));
        QVERIFY(!VinylSourcePool::isNewRelease("2026",today));
        QVERIFY(!VinylSourcePool::isNewRelease("2026-11",today));
        QVERIFY(!VinylSourcePool::isNewRelease("2026-19-42",today));
        QVERIFY(!VinylSourcePool::isNewRelease("",today));
    }
    void underfilledNewReleasesWidenToTenDistinctNeighborsPerSeed()
    {
        const auto seed=artist(1);
        QVariantList related;
        for(int index=10;index<23;++index)related.append(artist(index));
        related.insert(2,related.first());
        const QVariantMap bySeed{{seed.value("key").toString(),related}};
        const auto selected=VinylSourcePool::newReleaseNeighbors({seed},bySeed,7);
        QCOMPARE(selected.size(),10);
        QCOMPARE(selected.first().toMap().value("providerId"),artist(10).value("providerId"));
        QCOMPARE(selected.last().toMap().value("providerId"),artist(19).value("providerId"));
        QVERIFY(VinylSourcePool::newReleaseNeighbors({seed},bySeed,8).isEmpty());
    }
    void rowsStayFullAndRediscoverRequiresActualHistory()
    {
        const QStringList keys{"mixes-radio","new-from-your-artists","albums-for-you","because-you-played","deep-cuts","rediscover","artists-like-yours"};
        QVariantList eight;
        for(int index=0;index<8;++index)eight.append(item("apple-music",QString::number(100+index),"Fixture Album"));
        QVariantMap buckets;
        for(const auto &key:keys)buckets[key]=eight;
        const auto firstVisit=VinylSourcePool::homeRows(buckets,false);
        QCOMPARE(firstVisit.size(),6);
        QStringList actual;
        for(const auto &value:firstVisit){const auto row=value.toMap();actual.append(row.value("key").toString());QCOMPARE(row.value("items").toList().size(),8);}
        auto withoutRediscover=keys;withoutRediscover.removeOne("rediscover");
        QCOMPARE(actual,withoutRediscover);
        QCOMPARE(firstVisit[3].toMap().value("title").toString(),QString("Because you picked…"));
        const auto returning=VinylSourcePool::homeRows(buckets,true);
        QCOMPARE(returning.size(),7);
        QCOMPARE(returning[3].toMap().value("title").toString(),QString("Because you played…"));
        buckets["deep-cuts"]=eight.mid(0,7);
        const auto thin=VinylSourcePool::homeRows(buckets,true);
        QCOMPARE(thin.size(),6);
        for(const auto &value:thin)QVERIFY(value.toMap().value("key")!="deep-cuts");
    }
    void providerReceivesRecordedMetadataAndItsCacheRoundTrip()
    {
        QFile file(QFINDTESTDATA("fixtures/vinyl-source-pool/transport-envelopes.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto examples=QJsonDocument::fromJson(file.readAll()).object().value("examples").toArray();
        QCOMPARE(examples.size(),4);
        for(const auto &entry:examples) {
            const auto example=entry.toObject(),request=example.value("request").toObject();
            const auto operation=request.value("operation").toString(),value=request.value("value").toString();
            auto options=request.toVariantMap();options.remove("operation");options.remove("value");
            QVERIFY2(VinylProvider::homeOperation(operation),qPrintable(operation));
            QVariantMap received;
            QVERIFY2(VinylProvider::normalizeHome(operation,value,options,example.value("response").toVariant(),&received),qPrintable(operation));
            QCOMPARE(received.value("items").toList().size(),1);
            const auto item=received.value("items").toList().first().toMap();
            QVERIFY(item.value("requiresResolution").toBool());
            QCOMPARE(item.value("ref").toMap().value("identity").toString(),QString("provider"));
            QVERIFY(!item.value("destination").toMap().value("playable").toBool());
            QVERIFY(!item.contains("mapping"));
            QCOMPARE(item.value("catalogueItem").toMap(),example.value("response").toObject().value("items").toArray().first().toObject().toVariantMap());
            QVariantMap again;
            const auto cached=QJsonDocument::fromJson(QJsonDocument::fromVariant(received).toJson()).toVariant();
            QVERIFY(VinylProvider::normalizeHome(operation,value,options,cached,&again));
            QCOMPARE(again,received);
        }
    }
    void providerRejectsForgedMetadataAndWrongFeedBinding()
    {
        QFile file(QFINDTESTDATA("fixtures/vinyl-source-pool/transport-envelopes.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto example=QJsonDocument::fromJson(file.readAll()).object().value("examples").toArray().first().toObject();
        const auto request=example.value("request").toObject();
        auto options=request.toVariantMap();options.remove("operation");options.remove("value");
        const auto response=example.value("response").toObject().toVariantMap();
        QVariantMap received;
        QVERIFY(VinylProvider::normalizeHome("chartCandidates","apple:us:albums",options,response,&received));
        auto forged=response;
        auto items=forged.value("items").toList();auto item=items.first().toMap();
        item["ref"]=QVariantMap{{"identity","canonical"},{"kind","release"},{"mbid","11111111-1111-4111-8111-111111111111"}};
        items[0]=item;forged["items"]=items;
        QVERIFY(!VinylProvider::normalizeHome("chartCandidates","apple:us:albums",options,forged,&received));
        forged=response;auto provenance=forged.value("provenance").toMap();provenance["source"]="lastfm";forged["provenance"]=provenance;
        QVERIFY(!VinylProvider::normalizeHome("chartCandidates","apple:us:albums",options,forged,&received));
        QVERIFY(!VinylProvider::normalizeHome("chartCandidates","apple:gb:albums",options,response,&received));
        auto extra=options;extra["region"]="us";
        QVERIFY(!VinylProvider::normalizeHome("chartCandidates","apple:us:albums",extra,response,&received));
        forged=response;items=forged.value("items").toList();item=items.first().toMap();item["rank"]=true;items[0]=item;forged["items"]=items;
        QVERIFY(!VinylProvider::normalizeHome("chartCandidates","apple:us:albums",options,forged,&received));
    }
    void listenBrainzListRankSurvivesAnUnreportedSimilarityScore()
    {
        QJsonObject example;
        for (const auto &entry : examples()) {
            const auto candidate = entry.toObject();
            if (candidate.value("request").toObject().value("source") == "listenbrainz") example = candidate;
        }
        QVERIFY(!example.isEmpty());
        const auto request = example.value("request").toObject();
        auto options = request.toVariantMap(); options.remove("operation"); options.remove("value");
        auto data = example.value("response").toObject().toVariantMap();
        auto items = data.value("items").toList(); auto row = items.first().toMap();
        row.remove("similarity"); items[0] = row; data["items"] = items;
        QVariantMap received;
        QVERIFY(VinylProvider::normalizeHome("similarArtists",request.value("value").toString(),options,data,&received));
        const auto item = received.value("items").toList().first().toMap();
        QCOMPARE(item.value("rank").toInt(),1);
        QVERIFY(!item.contains("similarity"));
        QVERIFY(!item.value("catalogueItem").toMap().contains("similarity"));
    }
    void discographyHasIndependentCompletenessWithoutIdentityAttestation()
    {
        const QString id="b95ce3ff-3d05-4e87-9e01-c97b66af13d4";
        const QVariantMap seed{{"identity","canonical"},{"kind","artist"},{"mbid",id}},options{{"seed",seed}};
        const QVariantMap group{{"id","11111111-1111-4111-8111-111111111111"},{"title","Fixture Release"},{"first-release-date","2026-03"}};
        QVariantMap response{{"version",1},{"seed",seed},{"items",QVariantList()},
            {"provenance",QVariantMap{{"source","musicbrainz"},{"generator","artist-evidence"}}},
            {"fetchedAt","2026-11-13T00:00:00.000Z"},{"status","ready"},{"complete",true},
            {"artist",QVariantMap{{"id",id},{"releaseGroups",QVariantList{group}},{"releaseGroupsComplete",true},{"releaseGroupCount",1}}}};
        QVERIFY(VinylProvider::homeOperation("recommendationDiscography"));
        QVariantMap received;
        QVERIFY(VinylProvider::normalizeHome("recommendationDiscography",id,options,response,&received));
        QVERIFY(!received.contains("mapping") && !received.contains("ref"));
        QCOMPARE(received.value("artist").toMap().value("releaseGroups").toList().first().toMap().value("first-release-date").toString(),QString("2026-03"));
        auto partial=response;auto artist=partial.value("artist").toMap();
        partial["complete"]=false;partial["status"]="partial";artist["releaseGroupsComplete"]=false;artist["releaseGroupCount"]=2;partial["artist"]=artist;
        QVERIFY(VinylProvider::normalizeHome("recommendationDiscography",id,options,partial,&received));
        auto malformed=response;artist=malformed.value("artist").toMap();artist["releaseGroupsComplete"]=false;malformed["artist"]=artist;
        QVERIFY(!VinylProvider::normalizeHome("recommendationDiscography",id,options,malformed,&received));
        malformed=response;artist=malformed.value("artist").toMap();artist["id"]="11111111-1111-4111-8111-111111111111";malformed["artist"]=artist;
        QVERIFY(!VinylProvider::normalizeHome("recommendationDiscography",id,options,malformed,&received));
        malformed=response;malformed["mapping"]=QVariantMap{{"status","verified"}};
        QVERIFY(!VinylProvider::normalizeHome("recommendationDiscography",id,options,malformed,&received));
    }
    void datedDiscographyIsBoundToTheRequestedWindow()
    {
        const QString id = "b95ce3ff-3d05-4e87-9e01-c97b66af13d4";
        auto data = discography(id), artist = data.value("artist").toMap();
        const QVariantMap window{{"since","2025-11-13"},{"until","2026-11-13"}};
        QVariantMap options{{"seed",data.value("seed")},{"since",window.value("since")},{"until",window.value("until")}};
        artist["releaseGroupScope"] = "recent-releases";
        artist["releaseGroupWindow"] = window;
        data["artist"] = artist;
        QVariantMap received;
        QVERIFY(VinylProvider::normalizeHome("recommendationDiscography",id,options,data,&received));
        auto bad = options; bad.remove("until");
        QVERIFY(!VinylProvider::normalizeHome("recommendationDiscography",id,bad,data,&received));
        bad = options; bad["since"] = "2026-12-01";
        QVERIFY(!VinylProvider::normalizeHome("recommendationDiscography",id,bad,data,&received));
        bad = options; bad["until"] = "2026-11-12";
        QVERIFY(!VinylProvider::normalizeHome("recommendationDiscography",id,bad,data,&received));
        artist.remove("releaseGroupScope"); data["artist"] = artist;
        QVERIFY(!VinylProvider::normalizeHome("recommendationDiscography",id,options,data,&received));
    }
    void metadataDispatchHasStrictLanesAndRejectsInvalidRequestsBeforePreemption()
    {
        QVERIFY(state({{"response",chart()},{"delayMs",150}}));
        VinylProvider provider(QFINDTESTDATA("fixtures/vinyl-source-pool/provider.py"),nullptr,1024*1024,{},transportRoot.filePath("dispatch-cache"));
        QSignalSpy done(&provider,&VinylProvider::completed);
        const QVariantMap options{{"seed",QVariantMap()}};
        QVERIFY(provider.requestPayload("home-metadata-0","chartCandidates","apple:us:albums",options) > 0);
        auto bad = options; bad["region"] = "us";
        QCOMPARE(provider.requestPayload("home-metadata-0","chartCandidates","apple:us:albums",bad),-1);
        QCOMPARE(provider.requestPayload("play","chartCandidates","apple:us:albums",options),-1);
        QCOMPARE(provider.requestPayload("home-metadata-1","resolve","song",{}),-1);
        QTRY_COMPARE(done.size(),1);
        QVERIFY2(failure(done).isEmpty(),qPrintable(failure(done)));
        QCOMPARE(reply(done).value("items").toList().size(),1);
        QCOMPARE(events().size(),2);
    }
    void chartRefreshKeepsTheLastGoodFeedOnFailureOrEmptyRefresh()
    {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QVariantMap options{{"seed",QVariantMap()}}, refresh{{"seed",QVariantMap()},{"refresh",true}};
        QVERIFY(state({{"response",chart()}}));
        VinylProvider provider(QFINDTESTDATA("fixtures/vinyl-source-pool/provider.py"),nullptr,1024*1024,[&now]{return now;},transportRoot.filePath("chart-cache"));
        QSignalSpy done(&provider,&VinylProvider::completed);
        QVERIFY(provider.requestPayload("home-metadata-0","chartCandidates","apple:us:albums",options) > 0);
        QTRY_COMPARE(done.size(),1);
        QVERIFY2(failure(done).isEmpty(),qPrintable(failure(done)));
        const auto good = reply(done).value("items");
        now += 12LL*3600000;
        auto cached = provider.cachedRecommendation("chartCandidates","apple:us:albums",options);
        QCOMPARE(cached.value("items"),good);
        QVERIFY(!cached.value("_stale").toBool());
        auto unavailable = chart();
        unavailable["items"] = QVariantList(); unavailable["status"] = "unavailable"; unavailable["complete"] = false;
        unavailable["error"] = "Fixture feed unavailable";
        QVERIFY(state({{"response",unavailable}}));
        QVERIFY(provider.requestPayload("home-metadata-0","chartCandidates","apple:us:albums",refresh) > 0);
        QTRY_COMPARE(done.size(),2);
        QVERIFY(failure(done).isEmpty());
        QCOMPARE(reply(done).value("items"),good);
        QVERIFY(reply(done).value("_stale").toBool());
        auto empty = chart(); empty["items"] = QVariantList(); empty["status"] = "empty";
        QVERIFY(state({{"response",empty}}));
        QVERIFY(provider.requestPayload("home-metadata-0","chartCandidates","apple:us:albums",refresh) > 0);
        QTRY_COMPARE(done.size(),3);
        QVERIFY(failure(done).isEmpty());
        QCOMPARE(reply(done).value("items"),good);
        QVERIFY(reply(done).value("_stale").toBool());
        QCOMPARE(provider.cachedRecommendation("chartCandidates","apple:us:albums",options).value("items"),good);
    }
    void discographyCacheTreatsReleaseGroupsAsPositiveMetadata()
    {
        const QString id = "b95ce3ff-3d05-4e87-9e01-c97b66af13d4";
        const auto data = discography(id);
        const QVariantMap options{{"seed",data.value("seed")}};
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(state({{"response",data}}));
        VinylProvider provider(QFINDTESTDATA("fixtures/vinyl-source-pool/provider.py"),nullptr,1024*1024,[&now]{return now;},transportRoot.filePath("discography-cache"));
        QSignalSpy done(&provider,&VinylProvider::completed);
        QVERIFY(provider.requestPayload("home-metadata-0","recommendationDiscography",id,options) > 0);
        QTRY_COMPARE(done.size(),1);
        QVERIFY2(failure(done).isEmpty(),qPrintable(failure(done)));
        now += 4LL*3600000;
        const auto cached = provider.cachedRecommendation("recommendationDiscography",id,options);
        QCOMPARE(cached.value("artist"),data.value("artist"));
        QVERIFY(!cached.value("_stale").toBool());
        QCOMPARE(events().size(),2);
    }
    void cooldownIsSharedByOneSourceWithoutBlockingOtherSources()
    {
        auto limited = chart();
        limited["items"] = QVariantList(); limited["status"] = "unavailable"; limited["complete"] = false;
        limited["provenance"] = QVariantMap{{"source","lastfm"}};
        limited["sourceStatus"] = "rate_limited"; limited["retryAfterSeconds"] = 60;
        const QVariantMap options{{"seed",QVariantMap()}};
        QVERIFY(state({{"responses",QVariantMap{{"lastfm:tracks",limited},{"apple:us:albums",chart()}}}}));
        VinylProvider provider(QFINDTESTDATA("fixtures/vinyl-source-pool/provider.py"),nullptr,1024*1024,{},transportRoot.filePath("cooldown-cache"));
        QSignalSpy done(&provider,&VinylProvider::completed);
        QVERIFY(provider.requestPayload("home-metadata-0","chartCandidates","lastfm:tracks",options) > 0);
        QTRY_COMPARE(done.size(),1);
        QCOMPARE(reply(done).value("status").toString(),QString("unavailable"));
        QVERIFY(provider.requestPayload("home-metadata-0","chartCandidates","lastfm:artists",options) > 0);
        QTRY_COMPARE(done.size(),2);
        QVERIFY(failure(done).isEmpty());
        QCOMPARE(reply(done).value("sourceStatus").toString(),QString("rate_limited"));
        QCOMPARE(events().size(),2);
        QVERIFY(provider.requestPayload("home-metadata-1","chartCandidates","apple:us:albums",options) > 0);
        QTRY_COMPARE(done.size(),3);
        QVERIFY2(failure(done).isEmpty(),qPrintable(failure(done)));
        QCOMPARE(reply(done).value("status").toString(),QString("ready"));
        QCOMPARE(events().size(),4);
    }
    void optionalDeadlineStartsAtDispatchAfterWaitingForTheWorkerCap()
    {
        const QVariantMap options{{"seed",QVariantMap()}};
        const QVariantMap seed = artist(1);
        const QVariantMap identity{{"version",1},{"seed",seed},{"items",QVariantList()},
            {"provenance",QVariantMap{{"source","musicbrainz"}}},{"fetchedAt","2026-11-13T00:00:00.000Z"},
            {"complete",true},{"status","unresolved"}};
        const QVariantMap responses{{"apple:us:albums",chart("us")},{"apple:gb:albums",chart("gb")},
            {"UC0000000000000000000001",identity}};
        QVERIFY(state({{"responses",responses},{"delayMs",4200}}));
        VinylProvider provider(QFINDTESTDATA("fixtures/vinyl-source-pool/provider.py"),nullptr,1024*1024,{},transportRoot.filePath("queue-cache"));
        QSignalSpy done(&provider,&VinylProvider::completed);
        // One provider owns all optional work. Distinct supported lanes exercise
        // its two-worker cap, including an identity metadata request queued last.
        QVERIFY(provider.requestPayload("home-metadata-0","chartCandidates","apple:us:albums",options) > 0);
        QVERIFY(provider.requestPayload("home-metadata-1","chartCandidates","apple:gb:albums",options) > 0);
        QVERIFY(provider.requestPayload("home-identity","recommendationIdentity","UC0000000000000000000001",{{"seed",seed},{"refresh",true}}) > 0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,15000);
        for (int index = 0; index < done.size(); ++index) QVERIFY2(failure(done,index).isEmpty(),qPrintable(failure(done,index)));
        QCOMPARE(events().size(),6);
        auto log = events(); int running = 0, maximum = 0;
        for (const auto &entry : log) { running += entry.toMap().value("event") == "start" ? 1 : -1; maximum = qMax(maximum,running); }
        QCOMPARE(maximum,2);
    }
    void aQueuedRequestRechecksItsSourceCooldownBeforeStartingAProcess()
    {
        auto limited = chart();
        limited["items"] = QVariantList(); limited["status"] = "unavailable"; limited["complete"] = false;
        limited["provenance"] = QVariantMap{{"source","lastfm"}};
        limited["sourceStatus"] = "rate_limited"; limited["retryAfterSeconds"] = 60;
        const auto seed = artist(1);
        const QString id = seed.value("providerId").toString();
        const QVariantMap core{{"version",1},{"seed",seed},{"items",QVariantList()},
            {"status","empty"},{"complete",true},{"provenance",QVariantMap{{"source","youtube-music"}}},
            {"fetchedAt","2026-11-13T00:00:00.000Z"}};
        QVERIFY(state({{"responses",QVariantMap{{id,core},{"lastfm:tracks",limited},{"lastfm:artists",limited}}},
            {"delays",QVariantMap{{id,350}}}}));
        VinylProvider provider(QFINDTESTDATA("fixtures/vinyl-source-pool/provider.py"),nullptr,1024*1024,{},transportRoot.filePath("queued-cooldown-cache"));
        QSignalSpy done(&provider,&VinylProvider::completed);
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",id,{{"seed",seed}}) > 0);
        const QVariantMap options{{"seed",QVariantMap()}};
        QVERIFY(provider.requestPayload("home-metadata-0","chartCandidates","lastfm:tracks",options) > 0);
        QVERIFY(provider.requestPayload("home-metadata-1","chartCandidates","lastfm:artists",options) > 0);
        QTRY_COMPARE(done.size(),3);
        for (int index=0;index<done.size();++index) {
            QVERIFY2(failure(done,index).isEmpty(),qPrintable(failure(done,index)));
            if (done[index][0].toString()=="home-metadata-1")
                QCOMPARE(reply(done,index).value("sourceStatus").toString(),QString("rate_limited"));
        }
        QCOMPARE(events().size(),4); // Core plus first feed, no queued second feed.
    }
    void musicBrainzRequestsAreSerializedAcrossProviderInstances()
    {
        const QString first = "b95ce3ff-3d05-4e87-9e01-c97b66af13d4", second = "11111111-1111-4111-8111-111111111111";
        const auto a = discography(first), b = discography(second);
        QVERIFY(state({{"responses",QVariantMap{{first,a},{second,b},{"artist-fixture",QVariantMap{{"id","fixture"}}}}},{"delayMs",120}}));
        const auto helper = QFINDTESTDATA("fixtures/vinyl-source-pool/provider.py");
        VinylProvider one(helper,nullptr,1024*1024,{},transportRoot.filePath("mb-one")), two(helper,nullptr,1024*1024,{},transportRoot.filePath("mb-two"));
        QSignalSpy doneOne(&one,&VinylProvider::completed), doneTwo(&two,&VinylProvider::completed);
        QVERIFY(one.requestPayload("home-metadata-0","recommendationDiscography",first,{{"seed",a.value("seed")}}) > 0);
        QVERIFY(two.requestPayload("home-metadata-0","recommendationDiscography",second,{{"seed",b.value("seed")}}) > 0);
        QVERIFY(one.request("artist-identity","artistIdentity","artist-fixture") > 0);
        QTRY_COMPARE_WITH_TIMEOUT(doneOne.size()+doneTwo.size(),3,7500);
        for (const auto *spy : {&doneOne,&doneTwo})
            for (int index=0;index<spy->size();++index) QVERIFY2(failure(*spy,index).isEmpty(),qPrintable(failure(*spy,index)));
        const auto log = events(); QCOMPARE(log.size(),6);
        for (int index=0;index<6;index+=2) {
            QCOMPARE(log[index].toMap().value("event").toString(),QString("start"));
            QCOMPARE(log[index+1].toMap().value("event").toString(),QString("finish"));
            if (index) {
                const auto gap=log[index].toMap().value("atNs").toLongLong()-log[index-1].toMap().value("atNs").toLongLong();
                QVERIFY2(gap>=1000000000LL,qPrintable(QString::number(gap)));
            }
        }
    }
};
QTEST_GUILESS_MAIN(VinylSourcePoolTest)
#include "tst_vinyl_source_pool.moc"
