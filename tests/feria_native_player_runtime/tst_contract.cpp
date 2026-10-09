#include "../../native/feria/FeriaNativePlayer.h"
#include "../../native/feria/FeriaYouTubeSource.h"
#include <QtTest>
class NativePlayerContract : public QObject {
    Q_OBJECT
private slots:
    void admission() {
        for(const auto &source : {"file:///C:/private", "javascript:alert(1)","blob:https://example.test/id","https://user:secret@example.test/video"})
            QVERIFY(!FeriaNativePlayer::allowsSource(QUrl(source)));
        QVERIFY(FeriaNativePlayer::allowsSource(QUrl("https://cdn.example.test/master.m3u8?token=value")));
    }
    void youtubeAdmission() {
        QCOMPARE(FeriaYouTubeSource::watchUrl(QUrl("https://youtu.be/aqz-KE-bpKQ?t=20")).toString(),QString("https://www.youtube.com/watch?v=aqz-KE-bpKQ"));
        for(const auto &url:{"https://youtube.com.evil.test/watch?v=aqz-KE-bpKQ","https://www.youtube.com/playlist?list=x","https://www.youtube.com/watch?v=bad"})
            QVERIFY(FeriaYouTubeSource::watchUrl(QUrl(url)).isEmpty());
        const auto watch=FeriaYouTubeSource::watchUrl(QUrl("https://www.youtube.com/watch?v=aqz-KE-bpKQ"));
        const QByteArray json=R"({"id":"aqz-KE-bpKQ","title":"Fixture","requested_formats":[{"url":"https://rr1.googlevideo.com/video","vcodec":"avc1","acodec":"none"},{"url":"https://rr1.googlevideo.com/audio","vcodec":"none","acodec":"opus"}]})";
        const auto source=FeriaYouTubeSource::candidate(json,watch);
        QCOMPARE(source["href"].toString(),watch.toString());
        QCOMPARE(source["audioSource"].toString(),QString("https://rr1.googlevideo.com/audio"));
        QVERIFY(FeriaYouTubeSource::candidate(QByteArray(json).replace("aqz-KE-bpKQ","other-id"),watch).isEmpty());
        QVERIFY(FeriaYouTubeSource::candidate(QByteArray(json).replace("https://rr1.googlevideo.com","file:///private"),watch).isEmpty());
        QVERIFY(FeriaYouTubeSource::candidate(QByteArray(json).replace("\"title\":","\"has_drm\":true,\"title\":"),watch).isEmpty());
    }
    void staleControls() {
        FeriaNativePlayer player;
        QVERIFY(player.openSource(QUrl("https://example.test/video.mp4"),"document-a",0,false,100,1,{}));
        const QString first=player.bootstrap()["session"].toString();
        QVERIFY(!player.act("document-b",first,"close")["ok"].toBool());
        QVERIFY(player.active());
        QVERIFY(!player.act("document-a",first,"raw-mpv-command",0)["ok"].toBool());
        QVERIFY(!player.act("document-a",first,"seek",std::numeric_limits<double>::infinity())["ok"].toBool());
        player.close();
        QVERIFY(!player.active());
        QVERIFY(player.openSource(QUrl("https://example.test/other.mp4"),"document-a",0,true,37,1,{}));
        QVERIFY(player.bootstrap()["session"].toString()!=first);
        QVERIFY(!player.act("document-a",first,"close")["ok"].toBool());
        QVERIFY(player.active());
    }
    void invalidOpenPreservesPlayback() {
        FeriaNativePlayer player;
        QVERIFY(player.openSource(QUrl("https://example.test/video.mp4"),"document",0,false,100,1,{}));
        const auto session=player.bootstrap()["session"];
        QVERIFY(!player.openSource(QUrl("file:///C:/private"),"document",0,false,100,1,{}));
        QCOMPARE(player.bootstrap()["session"],session);
    }
};
QTEST_MAIN(NativePlayerContract)
#include "tst_contract.moc"
