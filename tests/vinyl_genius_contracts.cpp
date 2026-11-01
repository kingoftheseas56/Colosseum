#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include "vinyl/VinylGeniusService.h"
#include "vinyl/VinylProvider.h"
#include <cstring>

// Original synthetic text is used only for deterministic contract tests.
class GeniusReply final : public QNetworkReply {
    Q_OBJECT
public:
    GeniusReply(const QNetworkRequest &request,QObject *parent):QNetworkReply(parent){setRequest(request);setUrl(request.url());setOperation(QNetworkAccessManager::GetOperation);open(ReadOnly|Unbuffered);}
    void abort() override {if(done)return;done=true;setError(OperationCanceledError,"cancelled");setFinished(true);emit finished();}
    qint64 bytesAvailable() const override {return data.size()+QNetworkReply::bytesAvailable();}
    void finish(QVariantMap response,int http=200){finishBytes(QJsonDocument::fromVariant(QVariantMap{{"meta",QVariantMap{{"status",http}}},{"response",response}}).toJson(),http);}
    void finishBytes(QByteArray bytes,int http=200){if(done)return;data=bytes;setAttribute(QNetworkRequest::HttpStatusCodeAttribute,http);setRawHeader("Retry-After","1");emit readyRead();if(done)return;done=true;setFinished(true);emit finished();}
    bool done=false;
protected:
    qint64 readData(char *out,qint64 maximum) override {const auto n=qMin(maximum,qint64(data.size()));if(!n)return -1;std::memcpy(out,data.constData(),size_t(n));data.remove(0,n);return n;}
private:
    QByteArray data;
};
class GeniusNetwork final : public QNetworkAccessManager {
    Q_OBJECT
public:
    QList<QPointer<GeniusReply>> replies;
    QList<QNetworkRequest> requests;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {requests.append(request);auto *reply=new GeniusReply(request,this);replies.append(reply);return reply;}
};
class VinylGeniusContracts : public QObject {
    Q_OBJECT
    static QString document(){return "ttd:sha256:"+QString(64,'a');}
    static QVariantMap context(QString occurrence="queue:test-1",QString profile="profile:test") {
        const auto ref=vinyl::RecommendationEntityRef::fromProvider(vinyl::CanonicalEntityKind::Recording,
            vinyl::ProviderItemRef(*vinyl::ProviderId::parse("youtube-music").value,*vinyl::ProviderItemId::parse("test-recording").value));
        return {{"recording",ref.toJson().toVariantMap()},{"occurrenceId",occurrence},{"profileScope",profile},
            {"trackName","Original Test Song"},{"artistName","Original Artist"},{"artistNames",QStringList{"Original Artist"}},{"duration",120.0}};
    }
    static QVariantMap song(int id=89,QString title="Original Test Song",QString artist="Original Artist") {
        return {{"id",id},{"title",title},{"url",QString("https://genius.com/test-song-%1").arg(id)},
            {"primary_artist",QVariantMap{{"name",artist}}},{"album",QVariantMap{{"name","Original Album"}}}};
    }
    static QVariantMap search(QVariantList songs={song()}) {
        QVariantList hits;for(const auto &s:songs)hits.append(QVariantMap{{"type","song"},{"result",s}});return {{"hits",hits}};
    }
    static QVariantList lines(QStringList texts={"First original phrase","Second original phrase","Final original phrase"}) {
        QVariantList result;for(int i=0;i<texts.size();++i)result.append(QVariantMap{{"text",texts[i]},{"cueId",QString("cue:%1").arg(i)}});return result;
    }
    static QVariantMap referent(QString fragment="First original phrase",int id=101) {
        const QVariantMap author{{"id",7},{"name","Test Author"},{"url","https://genius.com/test-author"}};
        const QVariantMap note{{"id",id+1000},{"referent_id",id},{"url",QString("https://genius.com/%1/test-note").arg(id+1000)},
            {"body",QVariantMap{{"plain","This original explanatory test note stays attached to its real source passage."}}},
            {"votes_total",12},{"verified",false},{"deleted",false},
            {"authors",QVariantList{QVariantMap{{"user",author}}}}};
        return {{"id",id},{"song_id",89},{"fragment",fragment},{"is_description",false},{"annotations",QVariantList{note}}};
    }
    static void inputs(VinylGeniusService &service){service.setContext(context());service.setDocumentId(document());service.setLyrics(lines());}
private slots:
    void matchesOnlyUnambiguousArtistAndVersion(){
        QCOMPARE(VinylGeniusService::matchingSongs(context(),search()).size(),1);
        QVERIFY(VinylGeniusService::matchingSongs(context(),search({song(89,"Original Test Song","Cover Artist")})).isEmpty());
        QVERIFY(VinylGeniusService::matchingSongs(context(),search({song(89,"Original Test Song (Live)")})).isEmpty());
        QCOMPARE(VinylGeniusService::matchingSongs(context(),search({song(),song(90)})).size(),2);
        QCOMPARE(VinylGeniusService::matchingSongs(context(),search({song(),song()})).size(),1);
    }
    void alignsAcrossCueBoundariesAndKeepsSourceFields(){
        auto ref=referent("original phrase Second original");
        auto result=VinylGeniusService::align(document(),lines(),{ref});
        QCOMPARE(result.value("annotationCount").toInt(),1);
        const auto notes=result.value("notes").toMap();QVERIFY(notes.contains("0")&&notes.contains("1"));QVERIFY(!notes.contains("2"));
        const auto note=notes["0"].toList().first().toMap();
        QCOMPARE(note.value("startLine").toInt(),0);QCOMPARE(note.value("endLine").toInt(),1);
        QCOMPARE(note.value("startCueId").toString(),QString("cue:0"));QCOMPARE(note.value("documentId").toString(),document());
        QCOMPARE(note.value("votes").toInt(),12);QVERIFY(!note.value("verified").toBool());
        QCOMPARE(note.value("authors").toList().first().toMap().value("name").toString(),QString("Test Author"));
        QCOMPARE(result.value("anchors").toMap()["1"].toInt(),0);QCOMPARE(result.value("groupEndLines").toMap()["0"].toInt(),1);
    }
    void repeatedChorusesDoNotChooseAnArbitraryOccurrence(){
        const auto result=VinylGeniusService::align(document(),lines({"A repeated refrain","A unique verse","A repeated refrain"}),{referent("A repeated refrain")});
        QVERIFY(result.value("notes").toMap().isEmpty());QCOMPARE(result.value("ambiguousReferentCount").toInt(),1);
        const auto overlapping=VinylGeniusService::align(document(),lines({"Again again again"}),{referent("again again")});
        QVERIFY(overlapping.value("notes").toMap().isEmpty());QCOMPARE(overlapping.value("ambiguousReferentCount").toInt(),1);
    }
    void respectsWordsAndUnicodeWithoutDroppingMarks(){
        auto result=VinylGeniusService::align(document(),lines({"Wrapped wrapping wrap"}),{referent("wrap")});
        QCOMPARE(result.value("annotationCount").toInt(),1);
        result=VinylGeniusService::align(document(),lines({"Wrapped wrapping"}),{referent("wrap")});QVERIFY(result.value("notes").toMap().isEmpty());
        result=VinylGeniusService::align(document(),lines({QString::fromUtf8("I’m singing — corazón"),QString::fromUtf8("আমার সোনার বাংলা")}),
            {referent(QString::fromUtf8("I'm singing corazón")),referent(QString::fromUtf8("আমার সোনার বাংলা"),102)});
        QCOMPARE(result.value("annotationCount").toInt(),2);
    }
    void missingMetadataIsNeverInvented(){
        auto ref=referent();auto entries=ref["annotations"].toList();auto note=entries[0].toMap();note.remove("votes_total");note.remove("verified");note.remove("authors");entries[0]=note;ref["annotations"]=entries;
        const auto out=VinylGeniusService::align(document(),lines(),{ref})["notes"].toMap()["0"].toList().first().toMap();
        QVERIFY(!out.contains("votes"));QVERIFY(!out.contains("verified"));QVERIFY(out["authors"].toList().isEmpty());
    }
    void rejectsDeletedUnsafeAndMalformedBodies(){
        for(const auto &change:QVariantList{QVariantMap{{"deleted",true}},QVariantMap{{"url","javascript:alert(1)"}},
            QVariantMap{{"url","https://genius.com.evil.invalid/note"}},QVariantMap{{"body",QVariantMap{{"html","<b>not plain</b>"}}}},
            QVariantMap{{"body",QVariantMap{{"plain",QString(65537,'x')}}}}}){
            auto ref=referent();auto note=ref["annotations"].toList()[0].toMap();
            const auto map=change.toMap();for(auto it=map.cbegin();it!=map.cend();++it)note[it.key()]=it.value();ref["annotations"]=QVariantList{note};
            QVERIFY(VinylGeniusService::align(document(),lines(),{ref})["notes"].toMap().isEmpty());
        }
    }
    void transportIsAnonymousAndPublishesAlignedData(){
        GeniusNetwork network;VinylGeniusService service(nullptr,&network);inputs(service);
        QTRY_COMPARE(network.replies.size(),1);
        QCOMPARE(network.requests[0].url().host(),QString("genius.com"));QVERIFY(network.requests[0].rawHeader("Authorization").isEmpty());
        QCOMPARE(network.requests[0].attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),int(QNetworkRequest::Manual));
        network.replies[0]->finish(search());QTRY_COMPARE(network.replies.size(),2);
        network.replies[1]->finish({{"song",song()}});QTRY_COMPARE(network.replies.size(),3);
        network.replies[2]->finish({{"referents",QVariantList{referent()}}});
        QCOMPARE(service.status(),QString("ready"));QCOMPARE(service.annotationCount(),1);
        auto moved=lines();auto row=moved[0].toMap();row["time"]=20.0;moved[0]=row;service.setLyrics(moved);QTest::qWait(350);
        QCOMPARE(network.replies.size(),3);QCOMPARE(service.annotationCount(),1);
    }
    void lateRepliesCannotCrossDocumentOccurrenceOrProfile(){
        for(int kind=0;kind<3;++kind){
            GeniusNetwork network;VinylGeniusService service(nullptr,&network);inputs(service);QTRY_COMPARE(network.replies.size(),1);
            const auto old=network.replies[0];
            if(kind==0)service.setDocumentId("ttd:sha256:"+QString(64,'b'));
            else service.setContext(context(kind==1?"queue:test-2":"queue:test-1",kind==2?"profile:other":"profile:test"));
            if(old)old->finish(search());
            QVERIFY(service.notes().isEmpty());QVERIFY(service.geniusSongId().isEmpty());QTRY_COMPARE(network.replies.size(),2);
            network.replies[1]->finish({{"hits",QVariantList{}}});QCOMPARE(service.status(),QString("unavailable"));
        }
    }
    void albumMismatchAndAmbiguousSearchStayUnavailable(){
        GeniusNetwork network;VinylGeniusService service(nullptr,&network);auto ctx=context();ctx["albumName"]="A Different Album";
        inputs(service);service.setContext(ctx);QTRY_COMPARE(network.replies.size(),1);network.replies[0]->finish(search());QTRY_COMPARE(network.replies.size(),2);
        network.replies[1]->finish({{"song",song()}});QCOMPARE(service.status(),QString("ambiguous"));QVERIFY(service.notes().isEmpty());
        service.retry();QTRY_COMPARE(network.replies.size(),3);network.replies[2]->finish(search({song(),song(90)}));
        QCOMPARE(service.status(),QString("ambiguous"));QCOMPARE(network.replies.size(),3);
    }
    void failuresAndRateLimitsAreBounded(){
        GeniusNetwork network;VinylGeniusService service(nullptr,&network);inputs(service);QTRY_COMPARE(network.replies.size(),1);
        network.replies[0]->finish({},429);QCOMPARE(service.status(),QString("rate_limited"));service.retry();QTest::qWait(350);QCOMPARE(network.replies.size(),1);
        QTest::qWait(750);service.retry();QTRY_COMPARE(network.replies.size(),2);network.replies[1]->finishBytes("<html>not metadata</html>");QCOMPARE(service.status(),QString("error"));
        service.retry();QTRY_COMPARE(network.replies.size(),3);network.replies[2]->finishBytes(QByteArray(2*1024*1024+1,'x'));QCOMPARE(service.status(),QString("error"));QVERIFY(service.notes().isEmpty());
    }
    void usesTheExistingMetadataCache(){
        QTemporaryDir directory;QVERIFY(directory.isValid());VinylProvider cache("unused",nullptr,1024*1024,{},directory.path());
        const auto key=QString("genius-web-v1:search?q=test");cache.storeAnnotationCache(key,search());QCOMPARE(cache.annotationCache(key),search());
        QVERIFY(cache.annotationCache("../outside").isEmpty());
        GeniusNetwork network;VinylGeniusService service(nullptr,&network);service.setProvider(&cache);
        const auto query="genius-web-v1:search?q=Original Artist Original Test Song&per_page=10";
        cache.storeAnnotationCache(QString(query).replace(' ',"%20"),{{"hits",QVariantList{}}});
        inputs(service);QTRY_COMPARE(service.status(),QString("unavailable"));QTest::qWait(350);QCOMPARE(network.requests.size(),0);
    }
};
QTEST_GUILESS_MAIN(VinylGeniusContracts)
#include "vinyl_genius_contracts.moc"
