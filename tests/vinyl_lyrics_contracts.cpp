#include <QtTest>
#include "TimedTextCore.h"
#include "RecommendationEntityRef.h"
#include "vinyl/VinylLyricsService.h"

using namespace vinyl;

class LyricsProviderDouble final : public QObject {
    Q_OBJECT
public:
    int serial=0,cancels=0;
    QVariantMap payload;
    Q_INVOKABLE int requestPayload(const QString &lane,const QString &operation,const QString &,const QVariantMap &options) {
        if(lane!="lyrics"||operation!="lyrics")return -1;
        payload=options;return ++serial;
    }
    Q_INVOKABLE void cancel(const QString &lane) {if(lane=="lyrics")++cancels;}
    void finish(int id,const QVariantMap &record) {
        emit completed("lyrics",id,QVariantMap{{"source","lrclib"},{"status","found"},
            {"fetchedAt","2026-10-29T12:00:00.000Z"},{"record",record}},{});
    }
signals:
    void completed(const QString &lane,int requestId,const QVariant &data,const QString &error);
};

class VinylLyricsContracts : public QObject {
    Q_OBJECT
private:
    static RecommendationEntityRef recording(const QString &id) {
        return RecommendationEntityRef::fromProvider(CanonicalEntityKind::Recording,
            ProviderItemRef(*ProviderId::parse("youtube-music").value,
                            *ProviderItemId::parse(id).value));
    }
    static TimedTextProvenance provenance() {
        return TimedTextProvenance(*ProviderId::parse("lrclib").value,
                                  *ProviderItemId::parse("sample-1").value);
    }
    static QVariantMap context(const QString &id="catalogue-song-1",const QString &occurrence="queue:test-1") {
        return {{"recording",recording(id).toJson().toVariantMap()},{"occurrenceId",occurrence},
            {"profileScope","profile:test"},{"trackName","A Test Song"},{"artistName","Test Artist"},
            {"albumName","A Test Album"},{"duration",120.0}};
    }
    static QVariantMap record() {
        return {{"id",42},{"trackName","A Test Song"},{"artistName","Test Artist"},
            {"albumName","A Test Album"},{"duration",120.0},{"instrumental",false},
            {"plainLyrics","First original line\nSecond original line"},
            {"syncedLyrics","[offset:250]\n[00:01.20]First original line\n[00:03.456]Second original line\n[00:04.5]"}};
    }
private slots:
    void localContentKeepsItsOwnDocumentAndClock() {
        VinylLyricsService service;
        auto ctx=context();ctx["embeddedLyrics"]=QVariantList{
            QVariantMap{{"text","Original local first line"},{"time",1.25}},
            QVariantMap{{"text","Original local second line"},{"time",3.5}}};
        service.setContext(ctx);service.setRecordingTimeOffsetMs(0);
        QCOMPARE(service.status(),QString("line_synced"));
        QCOMPARE(service.sourceName(),QString("local"));
        QCOMPARE(service.seekTime(0).toDouble(),1.25);
        service.setPosition(3.5);QCOMPARE(service.currentLineIndex(),1);
        ctx["embeddedLyrics"]="Original plain first line\nOriginal plain second line";
        service.setContext(ctx);QVERIFY(!service.timed());
        QCOMPARE(service.lines().size(),2);QVERIFY(!service.seekTime(0).isValid());
    }
    void lateResultsCannotCrossOccurrenceOrProfile() {
        LyricsProviderDouble provider;VinylLyricsService service;
        service.setProvider(&provider);service.setContext(context());
        QCOMPARE(provider.serial,1);QCOMPARE(provider.payload.value("trackName").toString(),QString("A Test Song"));
        service.setContext(context("catalogue-song-1","queue:test-2"));
        QCOMPARE(provider.serial,2);provider.finish(1,record());
        QVERIFY(service.documentId().isEmpty());QCOMPARE(service.status(),QString("loading"));
        provider.finish(2,record());QVERIFY(!service.documentId().isEmpty());
        const auto document=service.documentId();
        service.setContext(context("catalogue-song-1","queue:test-2"));QCOMPARE(provider.serial,2);
        auto anotherProfile=context("catalogue-song-1","queue:test-2");anotherProfile["profileScope"]="profile:other";
        service.setContext(anotherProfile);QVERIFY(service.documentId().isEmpty());
        provider.finish(2,record());QVERIFY(service.documentId().isEmpty());
        provider.finish(3,record());QCOMPARE(service.documentId(),document);
        service.setContext({});provider.finish(3,record());QVERIFY(service.lines().isEmpty());
        QVERIFY(provider.cancels>=3);
    }
    void projectionRequiresAnExplicitPlaybackMapping() {
        LyricsProviderDouble provider;VinylLyricsService service;
        service.setProvider(&provider);service.setContext(context());provider.finish(1,record());
        QVERIFY(!service.timed());QVERIFY(!service.seekTime(0).isValid());
        QVERIFY(!service.lines()[0].toMap().contains("time"));
        service.setPosition(1);QCOMPARE(service.currentLineIndex(),-1);
        service.setRecordingTimeOffsetMs(0);
        QVERIFY(service.timed());QCOMPARE(service.currentLineIndex(),0);
        QCOMPARE(service.seekTime(0).toDouble(),0.95);
        const auto document=service.documentId();
        service.setRecordingTimeOffsetMs(-1000);
        QCOMPARE(service.seekTime(0).toDouble(),1.95);QCOMPARE(service.currentLineIndex(),-1);
        QCOMPARE(service.documentId(),document);
        service.setRecordingTimeOffsetMs(QVariant());QVERIFY(!service.timed());
    }
    void parsesRealLrcFractionsAndOffset() {
        auto doc=VinylLyricsService::fromLrclib(context(),record());
        QVERIFY2(doc.ok(),qPrintable(doc.error.detail));
        QCOMPARE(doc.value->kind(),TimedTextKind::LineSynced);
        QCOMPARE(doc.value->cues().size(),3);
        QCOMPARE(doc.value->cues()[0].startMs(),1200);
        QCOMPARE(doc.value->cues()[1].startMs(),3456);
        QCOMPARE(doc.value->cues()[2].startMs(),4500);
        QCOMPARE(doc.value->providerOffsetMs(),250);
        QCOMPARE(*timedTextCuePlaybackSeekTargetMs(*doc.value,0,0),950);
        QVERIFY(!currentTimedTextCueIndexForPlayback(*doc.value,949,0));
        QCOMPARE(*currentTimedTextCueIndexForPlayback(*doc.value,950,0),0);
        QVERIFY(!timedTextCuePlaybackSeekTargetMs(*doc.value,0,std::nullopt));
    }
    void rejectsWrongRecordingMetadata() {
        for(const auto &field:{"trackName","artistName","albumName"}) {
            auto wrong=record();wrong[field]="Another version";
            QVERIFY(!VinylLyricsService::fromLrclib(context(),wrong).ok());
        }
        auto wrong=record();wrong["duration"]=122.01;
        QVERIFY(!VinylLyricsService::fromLrclib(context(),wrong).ok());
        wrong["duration"]=true;
        QVERIFY(!VinylLyricsService::fromLrclib(context(),wrong).ok());
        wrong=record();wrong["trackName"]="A Test Song (Live)";
        QVERIFY(!VinylLyricsService::fromLrclib(context(),wrong).ok());
        wrong=record();wrong["duration"]=122.0;
        QVERIFY(VinylLyricsService::fromLrclib(context(),wrong).ok());
    }
    void malformedTimingFallsBackOnlyToRealPlainText() {
        auto plain=record();plain["syncedLyrics"]="[00:03]Later\n[00:01]Earlier";
        auto doc=VinylLyricsService::fromLrclib(context(),plain);
        QVERIFY(doc.ok());QCOMPARE(doc.value->kind(),TimedTextKind::Plain);
        QCOMPARE(doc.value->plainText(),plain["plainLyrics"].toString());
        plain["plainLyrics"]=QString();
        QVERIFY(!VinylLyricsService::fromLrclib(context(),plain).ok());
        plain=record();plain["syncedLyrics"]=QString();
        doc=VinylLyricsService::fromLrclib(context(),plain);
        QVERIFY(doc.ok());QCOMPARE(doc.value->kind(),TimedTextKind::Plain);
        plain["plainLyrics"]=QString();plain["instrumental"]=true;
        doc=VinylLyricsService::fromLrclib(context(),plain);
        QVERIFY(doc.ok());QCOMPARE(doc.value->kind(),TimedTextKind::Unavailable);
    }
    void repeatedPassagesAndRevisionsHaveDistinctIdentity() {
        auto repeated=record();repeated["syncedLyrics"]="[00:01]Same original line\n[00:04]Same original line";
        auto a=VinylLyricsService::fromLrclib(context(),repeated);
        QVERIFY(a.ok());QVERIFY(!(a.value->cues()[0].id()==a.value->cues()[1].id()));
        repeated["syncedLyrics"]="[00:01.1]Same original line\n[00:04]Same original line";
        auto b=VinylLyricsService::fromLrclib(context(),repeated);
        QVERIFY(b.ok());QVERIFY(a.value->documentId()!=b.value->documentId());
    }
    void providerIdentityHasNoInventedMbid() {
        auto ref=recording("catalogue-song-1");
        auto doc=TimedTextDocument::plain(ref,provenance(),"Original test text");
        QVERIFY(doc.ok());
        QVERIFY(!doc.value->recordingMbid().has_value());
        QCOMPARE(doc.value->recordingIdentity().stableKey(),ref.stableKey());
        auto another=TimedTextDocument::plain(recording("catalogue-song-2"),provenance(),"Original test text");
        QVERIFY(another.ok());
        QVERIFY(doc.value->documentId()!=another.value->documentId());
    }
    void requestsFenceProviderAndOccurrence() {
        auto first=recording("catalogue-song-1"),second=recording("catalogue-song-2");
        auto occurrence=*QueueOccurrenceId::parse("queue:test-1").value;
        auto other=*QueueOccurrenceId::parse("queue:test-2").value;
        auto doc=TimedTextDocument::plain(first,provenance(),"Original test text");
        QVERIFY(doc.ok());
        QVERIFY(canApplyTimedTextResult({first,occurrence},{first,occurrence},*doc.value));
        QVERIFY(!canApplyTimedTextResult({first,occurrence},{first,other},*doc.value));
        QVERIFY(!canApplyTimedTextResult({first,occurrence},{second,occurrence},*doc.value));
    }
    void canonicalFactoryCompatibility() {
        auto mbid=*RecordingMbid::parse("11111111-2222-3333-4444-555555555555").value;
        auto legacy=TimedTextDocument::plain(mbid,provenance(),"Original test text");
        auto explicitRef=TimedTextDocument::plain(RecommendationEntityRef(CanonicalEntityRef::from(mbid)),provenance(),"Original test text");
        QVERIFY(legacy.ok());QVERIFY(explicitRef.ok());
        QCOMPARE(legacy.value->documentId().value(),explicitRef.value->documentId().value());
        QVERIFY(legacy.value->recordingMbid()==mbid);
    }
};
QTEST_GUILESS_MAIN(VinylLyricsContracts)
#include "vinyl_lyrics_contracts.moc"
