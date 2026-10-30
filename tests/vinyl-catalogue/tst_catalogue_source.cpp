#include <QtTest>
#include <QSignalSpy>
#include <QDateTime>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QUuid>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include "vinyl/VinylCatalogueSource.h"
#include "engine/VaultIdentity.h"
#include "CollectionStore.h"
#include <QNetworkReply>
#include <QImage>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>

namespace {
qint64 cacheTreeBytes(const QString &root){
    qint64 total=0;QDirIterator files(root,QDir::Files,QDirIterator::Subdirectories);
    while(files.hasNext())total+=QFileInfo(files.next()).size();
    return total;
}
int cacheTreeFileCount(const QString &root){
    int total=0;QDirIterator files(root,QDir::Files,QDirIterator::Subdirectories);
    while(files.hasNext()){files.next();++total;}
    return total;
}
int homeLaunchCount(const QString &path){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return 0;
    return file.readAll().count('\n');
}
}

class ArtworkReply : public QNetworkReply {
    QByteArray m_bytes; qint64 m_offset=0;
public:
    ArtworkReply(const QNetworkRequest &request,QByteArray bytes,QObject *parent):QNetworkReply(parent),m_bytes(std::move(bytes)){
        setRequest(request);setUrl(request.url());open(QIODevice::ReadOnly);
        QTimer::singleShot(20,this,[this]{setFinished(true);emit readyRead();emit finished();});
    }
    void abort() override {setError(OperationCanceledError,"cancelled");}
    qint64 bytesAvailable() const override{return m_bytes.size()-m_offset+QNetworkReply::bytesAvailable();}
    qint64 readData(char *data,qint64 max) override{auto n=qMin(max,qint64(m_bytes.size())-m_offset);if(!n)return -1;memcpy(data,m_bytes.constData()+m_offset,size_t(n));m_offset+=n;return n;}
};
class ArtworkNam : public QNetworkAccessManager {
public:bool duplicate=false;bool invalid=false;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {
        int seed=duplicate?0:request.url().fileName().section('.',0,0).toInt();
        QImage image(invalid?32:1280,invalid?32:720,QImage::Format_RGB32);
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
            image.setPixel(x,y,((x/142+y/90*7+seed*13)%17)<8?qRgb(240,240,240):qRgb(20,20,20));
        QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
        return new ArtworkReply(request,bytes,this);
    }
};

class TorrentSourceFake : public QObject {
    Q_OBJECT
public:
    int serial=0,cancellations=0;QVariantMap last;
    Q_INVOKABLE int resolve(const QVariantMap &track){last=track;return ++serial;}
    Q_INVOKABLE void cancel(){++cancellations;}
signals:
    void completed(int requestId,const QVariantMap &data,const QString &error);
};

class CatalogueSourceTest : public QObject {
    Q_OBJECT
private slots:
    void searchPagesRegisterExactSourcesAndPlaylistContinuation(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QVariantMap cursor{{"token","next"},{"offset",20}};
        QVERIFY(source.request("search-songs","cataloguePage","Nas",{{"kind","songs"},{"cursor",cursor}})>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QCOMPARE(done.last()[2].toMap().value("request").toMap().value("cursor").toMap(),cursor);
        QCOMPARE(done.last()[2].toMap().value("offset").toInt(),20);
        QVERIFY(source.request("play","resolve","provider-recording")>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);
        QCOMPARE(done.last()[2].toMap().value("videoId").toString(),QString("abcdefghijk"));
        source.setAlbums({QVariantMap{{"id","public-playlist"},{"browseId","ytm:playlist:PLtest"},{"playlistId","PLtest"},{"type","Playlist"},{"origin","youtube-music"}}});
        QVERIFY(source.request("browse","album","public-playlist")>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,10000);
        QCOMPARE(done.last()[2].toMap().value("offset").toInt(),0);
        QVERIFY(source.request("playlist-page","playlistPage","PLtest",{{"cursor",QVariantMap{{"offset",1}}}})>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),4,10000);
        QVERIFY(source.request("play","resolve","later-recording")>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),5,10000);
        QCOMPARE(done.last()[2].toMap().value("videoId").toString(),QString("lmnopqrstuv"));
        // Reopening the first page must preserve a retained complete source,
        // including the server's explicit count of omitted private/deleted rows.
        QVERIFY(source.request("browse","album","public-playlist")>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),6,10000);
        const auto retained=done.last()[2].toMap();
        QCOMPARE(retained.value("tracks").toList().size(),2);
        QVERIFY(retained.value("complete").toBool());
        QCOMPARE(retained.value("totalTracks").toInt(),5);
        QCOMPARE(retained.value("omittedTracks").toInt(),3);
    }
    void discoveryRegistersExactTracksAndPublicPlaylistRoutes(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        QVERIFY(source.request("discover-trending","discover","trending",{{"refresh",true}})>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QVERIFY(done.last()[3].toString().isEmpty());
        QCOMPARE(done.last()[2].toMap().value("records").toList().size(),1);
        QVERIFY(source.request("play","resolve","provider-recording")>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);
        QCOMPARE(done.last()[2].toMap().value("videoId").toString(),QString("abcdefghijk"));
        QCOMPARE(done.last()[2].toMap().value("trackId").toString(),QString("provider-recording"));
        QVERIFY(source.request("discover-charts","discover","charts",{{"refresh",true}})>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,10000);
        QVERIFY(source.request("browse","album","public-playlist")>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),4,10000);
        QVERIFY(done.last()[3].toString().isEmpty());
        const auto playlist=done.last()[2].toMap();
        QCOMPARE(playlist.value("id").toString(),QString("public-playlist"));
        QCOMPARE(playlist.value("type").toString(),QString("Playlist"));
        QCOMPARE(playlist.value("browseId").toString(),QString("ytm:playlist:PLtest"));
        QCOMPARE(playlist.value("tracks").toList().size(),1);
    }
    void discoveryCacheIsFastRefreshableAndCancellable(){
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));QSignalSpy done(&provider,&VinylProvider::completed);
        QVERIFY(provider.requestPayload("discover-trending","discover","trending",{{"refresh",true}})>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        const auto python=qgetenv("COLOSSEUM_VINYL_PYTHON");
        qputenv("COLOSSEUM_VINYL_PYTHON","missing-discovery-runtime.exe");
        provider.request("discover-trending","discover","trending");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);
        QVERIFY(done.last()[3].toString().isEmpty());
        QVERIFY(done.last()[2].toMap().value("_cached").toBool());
        QVERIFY(done.last()[2].toMap().value("_cacheError").toString().isEmpty());
        provider.requestPayload("discover-trending","discover","trending",{{"refresh",true}});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,10000);
        QVERIFY(done.last()[2].toMap().value("_cached").toBool());
        QVERIFY(!done.last()[2].toMap().value("_cacheError").toString().isEmpty());
        provider.request("discover-trending","discover","trending");provider.cancel("discover-trending");
        QTest::qWait(50);QCOMPARE(done.size(),3);
        qputenv("COLOSSEUM_VINYL_PYTHON",python);
    }
    void summariesPreserveQueuedReleaseContext(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));VinylCatalogueSource source(&local,&provider,nullptr);
        TorrentSourceFake torrent;source.setTorrentProvider(&torrent);
        const QVariantMap track{{"id","shared-song"},{"videoId","abcdefghijk"},{"title","Song"},{"artists",QStringList{"Artist"}},{"seconds",180}};
        source.setAlbums({QVariantMap{{"id","original"},{"browseId","MPRE_original"},{"title","Original"},{"year",1994},{"tracks",QVariantList{track}}}});
        source.setAlbums({QVariantMap{{"id","other"},{"browseId","MPRE_other"},{"title","Compilation"},{"year",2024},{"tracks",QVariantList{track}}}});
        source.setAlbums({QVariantMap{{"id","original"},{"browseId","MPRE_original"},{"title","Original"},{"tracks",QVariantList{}}}});
        QVERIFY(source.request("play","resolve","shared-song",{{"browseId","MPRE_original"}})>0);
        QCOMPARE(torrent.last.value("album").toString(),QString("Original"));
        QCOMPARE(torrent.last.value("releaseYear").toInt(),1994);
        source.cancel("play");
        QVERIFY(source.request("play","resolve","shared-song",{{"browseId","MPRE_other"}})>0);
        QCOMPARE(torrent.last.value("album").toString(),QString("Compilation"));
    }
    void torrentPreferredAndFailureFallsBackWithoutChangingTrack(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));VinylCatalogueSource source(&local,&provider,nullptr);
        TorrentSourceFake torrent;source.setTorrentProvider(&torrent);
        const QVariantMap track{{"id","remote-song"},{"videoId","abcdefghijk"},{"title","Song"},{"artists",QStringList{"Artist"}},{"seconds",180}};
        source.setAlbums({QVariantMap{{"id","remote-album"},{"browseId","MPRE_album"},{"origin","youtube-music"},{"title","Album"},{"artists",QStringList{"Artist"}},{"year","1994"},{"tracks",QVariantList{track}}}});
        QSignalSpy done(&source,&VinylCatalogueSource::completed),helper(&provider,&VinylProvider::completed);
        const auto first=source.request("play","resolve","remote-song");
        QCOMPARE(torrent.serial,1);QCOMPARE(helper.size(),0);QCOMPARE(torrent.last.value("title").toString(),QString("Song"));
        QCOMPARE(torrent.last.value("releaseYear").toInt(),1994);
        emit torrent.completed(1,{{"provider","torrent"},{"url","http://127.0.0.1:9999/torrent/1"}},{});
        QTRY_COMPARE(done.size(),1);QCOMPARE(done.last()[1].toInt(),first);QCOMPARE(done.last()[2].toMap().value("trackId").toString(),QString("remote-song"));
        QCOMPARE(helper.size(),0);
        const auto second=source.request("play","resolve","remote-song");
        emit torrent.completed(2,{},"No matching file");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);QCOMPARE(done.last()[1].toInt(),second);
        QCOMPARE(done.last()[2].toMap().value("provider").toString(),QString("youtube"));
        QCOMPARE(done.last()[2].toMap().value("fallbackReason").toString(),QString("No matching file"));
        QVERIFY(done.last()[3].toString().isEmpty());QCOMPARE(helper.size(),1);
        source.request("play","resolve","remote-song");source.cancel("play");
        emit torrent.completed(3,{{"provider","torrent"},{"url","http://127.0.0.1/stale"}},{});
        QTest::qWait(40);QCOMPARE(done.size(),2);QVERIFY(torrent.cancellations>0);
        source.request("play","resolveFallback","remote-song");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,10000);QCOMPARE(torrent.serial,3);
        QCOMPARE(done.last()[2].toMap().value("provider").toString(),QString("youtube"));
    }
    void cleanup(){qunsetenv("VINYL_TEST_IDENTITY_UNAVAILABLE");qunsetenv("VINYL_TEST_PHOTOS");
        qunsetenv("VINYL_TEST_HOME_FAIL");qunsetenv("VINYL_TEST_HOME_SEED_MISMATCH");
        qunsetenv("VINYL_TEST_HOME_WRONG_SHAPE");qunsetenv("VINYL_TEST_HOME_OVERSIZE");
        qunsetenv("VINYL_TEST_HOME_SLOW");qunsetenv("VINYL_TEST_HOME_UNAVAILABLE");
        qunsetenv("VINYL_TEST_HOME_EMPTY");qunsetenv("VINYL_TEST_HOME_RATE");qunsetenv("VINYL_TEST_HOME_COUNTER");}
    void initTestCase(){QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName("VinylCatalogueContract-"+QUuid::createUuid().toString());}
    void independentChannelsAndPayload(){
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));QSignalSpy done(&provider,&VinylProvider::completed);
        QVERIFY(provider.request("browse","invalid","x")<0);
        const int a=provider.request("browse","catalogueSearch","Artist");
        const int b=provider.requestPayload("artist-albums","artistReleases","UCedvOgsKFzcK3hA5taf3KoQ",{{"kind","albums"}});
        QVERIFY(a>0&&b>a);QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);
        QSet<QString> lanes;for(const auto &row:done){lanes.insert(row[0].toString());QVERIFY(row[3].toString().isEmpty());}
        QCOMPARE(lanes,QSet<QString>({"browse","artist-albums"}));
    }
    void cancellationAndBounds(){
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));QSignalSpy done(&provider,&VinylProvider::completed);
        provider.request("browse","catalogueSearch","slow");provider.cancel("browse");QTest::qWait(1200);QCOMPARE(done.size(),0);
        provider.request("browse","catalogueSearch","oversize");QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(!done.first()[3].toString().isEmpty());
    }
    void cachedMetadataSurvivesUnavailableRuntimeButStreamsDoNot(){
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));QSignalSpy done(&provider,&VinylProvider::completed);
        const auto python=qgetenv("COLOSSEUM_VINYL_PYTHON");QVERIFY(!python.isEmpty());
        provider.request("browse","catalogueSearch","cache-contract");QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QVERIFY(done.first()[3].toString().isEmpty());
        provider.request("play","resolve","abcdefghijk");QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);
        QVERIFY(done.last()[3].toString().isEmpty());QVERIFY(done.last()[2].toMap().value("url").toString().contains("TRANSIENT_ONLY"));
        qputenv("COLOSSEUM_VINYL_PYTHON","missing-runtime-for-contract.exe");
        provider.request("browse","catalogueSearch","cache-contract");QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,10000);
        QVERIFY(done.last()[3].toString().isEmpty());QVERIFY(done.last()[2].toMap().value("_cached").toBool());
        provider.request("play","resolve","abcdefghijk");QTRY_COMPARE_WITH_TIMEOUT(done.size(),4,10000);
        QVERIFY(!done.last()[3].toString().isEmpty());QVERIFY(done.last()[2].toMap().isEmpty());
        qputenv("COLOSSEUM_VINYL_PYTHON",python);
    }
    void facadeReceivesMissingRuntimeCompletionAfterRequestReturns(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));VinylCatalogueSource source(&local,&provider,nullptr);QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const auto python=qgetenv("COLOSSEUM_VINYL_PYTHON");qputenv("COLOSSEUM_VINYL_PYTHON","missing-runtime-for-facade.exe");
        const int id=source.request("browse","catalogueSearch","missing-facade-runtime");
        qputenv("COLOSSEUM_VINYL_PYTHON",python);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,2000);QCOMPARE(done.first()[1].toInt(),id);QVERIFY(!done.first()[3].toString().isEmpty());
    }
    void canonicalBridgeUsesCollectionAndProviderPlaybackIds(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        CollectionStore collection(dir.filePath("collection.ini"));VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));
        VinylCatalogueSource source(&local,&provider,&collection);
        source.openArtist({{"id","ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ"},{"name","Artist"}});
        QTRY_VERIFY_WITH_TIMEOUT(!source.artistBusy(),15000);QVERIFY2(source.artistError().isEmpty(),qPrintable(source.artistError()));
        QCOMPARE(source.artistAlbums().size(),1);QCOMPARE(source.artistSingles().size(),1);
        auto *bridge=qobject_cast<vinyl::artist::ArtistServiceBridge *>(source.canonicalArtistService());QVERIFY(bridge);
        QTRY_COMPARE(bridge->artistState(),vinyl::artist::State::Ready);QTRY_COMPARE(bridge->discography().size(),1);
        QVERIFY(bridge->toggleSavedArtist());QVERIFY(collection.has("Vinyl","ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ"));
        const auto savedArtist=collection.items("Vinyl").first().toMap();
        QCOMPARE(savedArtist.value("payload").toMap().value("externalIds").toMap().value("musicbrainz").toString(),bridge->artistMbid());
        QVERIFY(!bridge->selectRelease(bridge->artistMbid(),"22222222-2222-4222-8222-222222222222","33333333-3333-4333-8333-333333333333"));
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const int album=source.request("browse","album","provider-release");QVERIFY(album>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done.first()[3].toString().isEmpty());
        const int play=source.request("play","resolve","provider-recording");QVERIFY(play>album);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);QCOMPARE(done.last()[2].toMap().value("trackId").toString(),QString("provider-recording"));
        source.leaveArtist();QCOMPARE(bridge->artistState(),vinyl::artist::State::Idle);
        source.openArtist({{"id","ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ"},{"name","Artist"}});
        emit local.profileLeaving();QTest::qWait(500);
        QVERIFY(!source.artistBusy());QVERIFY(source.artist().isEmpty());QVERIFY(source.artistAlbums().isEmpty());QVERIFY(!bridge->active());
    }
    void primaryCatalogueSurvivesOptionalMusicBrainzFailure(){
        qputenv("VINYL_TEST_IDENTITY_UNAVAILABLE","1");
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));VinylCatalogueSource source(&local,&provider,nullptr);
        source.openArtist({{"id","ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ"},{"name","No MB mapping"}});
        QTRY_VERIFY_WITH_TIMEOUT(!source.artistBusy(),15000);QCOMPARE(source.artistAlbums().size(),1);QCOMPARE(source.artistSingles().size(),1);
        QVERIFY(source.artistIdentity().value("unavailable").toBool());QVERIFY(!source.artistError().isEmpty());
        QVERIFY(!qobject_cast<vinyl::artist::ArtistServiceBridge *>(source.canonicalArtistService())->active());
        QSignalSpy done(&source,&VinylCatalogueSource::completed);source.request("browse","album","provider-release");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done.first()[3].toString().isEmpty());
        QCOMPARE(done.first()[2].toMap().value("tracks").toList().size(),1);
    }
    void homeArtistCandidatesForwardAndNormalizeProviderIdentity(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,{},dir.filePath("cache"));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        const int id=source.request("home-seed-0","artistCandidates",channel,{{"seed",seed}});
        QVERIFY(id>0);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QCOMPARE(done.first()[0].toString(),QString("home-seed-0"));QCOMPARE(done.first()[1].toInt(),id);
        QVERIFY(done.first()[3].toString().isEmpty());
        const auto response=done.first()[2].toMap();
        QCOMPARE(response.value("version").toInt(),1);QCOMPARE(response.value("seed").toMap(),seed);
        QVERIFY(!response.value("complete").toBool());QCOMPARE(response.value("status").toString(),QString("ready"));
        QVERIFY(QDateTime::fromString(response.value("fetchedAt").toString(),Qt::ISODateWithMs).isValid());
        QCOMPARE(response.value("provenance").toMap().value("source").toString(),QString("youtube-music"));
        QCOMPARE(response.value("provenance").toMap().value("generator").toString(),QString("artist-catalogue"));
        const auto items=response.value("items").toList();QCOMPARE(items.size(),1);
        const auto item=items.first().toMap();
        QCOMPARE(item.value("provider").toString(),QString("youtube-music"));
        QCOMPARE(item.value("providerId").toString(),QString("MPRE_test"));
        QCOMPARE(item.value("kind").toString(),QString("album"));
        QCOMPARE(item.value("id").toString(),QString("ytm:album:MPRE_test"));
        QCOMPARE(item.value("relation").toString(),QString("own-release"));
        QCOMPARE(item.value("artistRefs").toList().first().toMap(),seed);
        const auto ref=item.value("ref").toMap();
        QCOMPARE(ref.value("identity").toString(),QString("provider"));
        QCOMPARE(ref.value("kind").toString(),QString("release"));
        QCOMPARE(ref.value("providerId").toString(),QString("youtube-music"));
        QCOMPARE(ref.value("itemId").toString(),QString("MPRE_test"));
        const auto expectedKey=QString::fromUtf8(QJsonDocument(QJsonArray{"provider","release","youtube-music","MPRE_test"}).toJson(QJsonDocument::Compact));
        QCOMPARE(item.value("key").toString(),expectedKey);
        const auto destination=item.value("destination").toMap();
        QCOMPARE(destination.value("id").toString(),QString("ytm:album:MPRE_test"));
        QCOMPARE(destination.value("kind").toString(),QString("album"));
        QVERIFY(!destination.value("playable").toBool());
        const auto artists=item.value("artists").toList();QCOMPARE(artists.size(),1);
        QCOMPARE(artists.first().toMap().value("ref").toMap().value("providerId").toString(),QString("youtube-music"));
        QCOMPARE(artists.first().toMap().value("ref").toMap().value("itemId").toString(),channel);
        QVERIFY(source.request("browse","album",destination.value("id").toString())>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);
        QVERIFY(done.last()[3].toString().isEmpty());
        QCOMPARE(done.last()[2].toMap().value("browseId").toString(),QString("MPRE_test"));
    }
    void homeListeningMetadataLanesAreAllowlistedAndIsolated(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,{},dir.filePath("cache"));
        VinylCatalogueSource source(&local,&provider,nullptr);QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};

        const int browseId=source.request("browse","catalogueSearch","slow");
        const int queueId=source.request("queue","album","queue-release");
        const int albumId=source.request("home-listening-0","album","MPRE_test");
        const int candidateId=source.request("home-listening-1","artistCandidates",channel,{{"seed",seed}});
        QVERIFY(browseId>0);QVERIFY(queueId>0);QVERIFY(albumId>0);QVERIFY(candidateId>0);

        QHash<QString,int> expectedIds{{"browse",browseId},{"queue",queueId},
            {"home-listening-0",albumId},{"home-listening-1",candidateId}};
        QHash<QString,QVariantMap> responses;
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),expectedIds.size(),10000);
        for(const auto &completion:done){
            const auto lane=completion[0].toString();
            QVERIFY(expectedIds.contains(lane));
            QCOMPARE(completion[1].toInt(),expectedIds.value(lane));
            QVERIFY2(completion[3].toString().isEmpty(),qPrintable(completion[3].toString()));
            responses.insert(lane,completion[2].toMap());
        }
        QCOMPARE(responses.size(),expectedIds.size());

        const auto homeAlbum=responses.value("home-listening-0");
        QCOMPARE(homeAlbum.value("id").toString(),QString("provider-release"));
        QCOMPARE(homeAlbum.value("browseId").toString(),QString("MPRE_test"));
        QCOMPARE(homeAlbum.value("tracks").toList().size(),1);

        const auto candidateResponse=responses.value("home-listening-1");
        QCOMPARE(candidateResponse.value("seed").toMap(),seed);
        const auto items=candidateResponse.value("items").toList();QCOMPARE(items.size(),1);
        const auto item=items.first().toMap();
        QCOMPARE(item.value("kind").toString(),QString("album"));
        QCOMPARE(item.value("ref").toMap().value("kind").toString(),QString("release"));
        QCOMPARE(item.value("ref").toMap().value("itemId").toString(),QString("MPRE_test"));
        QCOMPARE(item.value("destination").toMap().value("id").toString(),QString("ytm:album:MPRE_test"));
        QVERIFY(!item.value("destination").toMap().value("playable").toBool());

        const auto browse=responses.value("browse");
        QCOMPARE(browse.value("request").toMap().value("operation").toString(),QString("catalogueSearch"));
        const auto queue=responses.value("queue");
        QCOMPARE(queue.value("browseId").toString(),QString("queue-release"));
        QCOMPARE(queue.value("tracks").toList().size(),1);

        const int lane0CandidateId=source.request("home-listening-0","artistCandidates",channel,{{"seed",seed}});
        QVERIFY(lane0CandidateId>0);QTRY_COMPARE_WITH_TIMEOUT(done.size(),5,10000);
        QCOMPARE(done.last()[0].toString(),QString("home-listening-0"));
        QCOMPARE(done.last()[1].toInt(),lane0CandidateId);QVERIFY(done.last()[3].toString().isEmpty());
        const auto lane0Items=done.last()[2].toMap().value("items").toList();QCOMPARE(lane0Items.size(),1);
        QCOMPARE(lane0Items.first().toMap().value("destination").toMap().value("id").toString(),QString("ytm:album:MPRE_test"));

        const int lane1AlbumId=source.request("home-listening-1","album","MPRE_test");
        QVERIFY(lane1AlbumId>0);QTRY_COMPARE_WITH_TIMEOUT(done.size(),6,10000);
        QCOMPARE(done.last()[0].toString(),QString("home-listening-1"));
        QCOMPARE(done.last()[1].toInt(),lane1AlbumId);QVERIFY(done.last()[3].toString().isEmpty());
        QCOMPARE(done.last()[2].toMap().value("browseId").toString(),QString("MPRE_test"));

        QVERIFY(source.request("home-listening-2","album","MPRE_test")<0);
        QVERIFY(source.request("home-listening-0","resolve","provider-recording")<0);
        QVERIFY(source.request("home-listening-1","resolve","provider-recording")<0);
        QTest::qWait(50);
        QCOMPARE(done.size(),expectedIds.size()+2);
    }
    void matchedRecordingIsRequestBoundAndUsesExistingResolver(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,{},dir.filePath("cache"));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QVariantMap artist{{"provider","youtube-music"},{"providerId","UCedvOgsKFzcK3hA5taf3KoQ"},{"kind","artist"},{"name","Artist"}};
        const QVariantMap recording{{"title","Track"},{"seconds",180},{"version","studio"},{"artistRefs",QVariantList{artist}}};
        const QVariantMap options{{"recording",recording},{"seed",QVariantMap()}};
        QVERIFY(source.request("home-seed-0","recordingMatch","Track",options)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done.first()[3].toString().isEmpty());
        const auto response=done.first()[2].toMap();const auto destination=response.value("items").toList().first().toMap().value("destination").toMap();
        QVERIFY(destination.value("playable").toBool());
        QVERIFY(source.request("play","resolve",destination.value("id").toString())>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);QVERIFY(done.last()[3].toString().isEmpty());
        QCOMPARE(done.last()[2].toMap().value("videoId").toString(),QString("abcdefghijk"));
        for(const auto &field:{"title","seconds","artistRefs","version"}){
            auto bad=recording;
            if(QString(field)=="title")bad[field]="Different";
            else if(QString(field)=="seconds")bad[field]=120;
            else if(QString(field)=="version")bad[field]="radio edit";
            else bad[field]=QVariantList{QVariantMap{{"provider","youtube-music"},{"kind","artist"},{"providerId","UCPoQYATXIYvN5WB0c4f6jfQ"}}};
            auto badOptions=options;badOptions["recording"]=bad;QVariantMap result;
            QVERIFY2(!VinylProvider::normalizeHome("recordingMatch","Track",badOptions,response,&result),field);
        }
        auto badResponse=response;auto badItem=response.value("items").toList().first().toMap();badItem["version"]="cover";
        badResponse["items"]=QVariantList{badItem};QVariantMap result;
        QVERIFY(!VinylProvider::normalizeHome("recordingMatch","Track",options,badResponse,&result));
        auto malformedArtist=artist;malformedArtist["providerId"]="bad";
        auto malformedRecording=recording;malformedRecording["artistRefs"]=QVariantList{malformedArtist};
        auto malformedOptions=options;malformedOptions["recording"]=malformedRecording;
        badItem=response.value("items").toList().first().toMap();badItem["artistRefs"]=QVariantList{malformedArtist};
        badResponse=response;badResponse["items"]=QVariantList{badItem};
        QVERIFY(!VinylProvider::normalizeHome("recordingMatch","Track",malformedOptions,badResponse,&result));
        TorrentSourceFake torrent;source.setTorrentProvider(&torrent);done.clear();
        QVERIFY(source.request("play","resolve",destination.value("id").toString())>0);
        QCOMPARE(torrent.last.value("artist").toString(),QString("Artist"));
        QCOMPARE(torrent.last.value("title").toString(),QString("Track"));
        source.cancel("play");
    }
    void canonicalPopulationEvidenceHasNoInventedPlayableDestination(){
        const QString mbid="cfbc0924-0035-4d6c-8197-f024653af823";
        const QVariantMap seed{{"identity","canonical"},{"kind","artist"},{"mbid",mbid}};
        const QVariantMap recording{{"identity","canonical"},{"kind","recording"},{"mbid","f4fe1d03-6170-40fd-b86a-68e8d7599afd"}};
        QVariantMap data{{"version",1},{"seed",seed},{"complete",true},{"status","ready"},{"provenance",QVariantMap{{"source","listenbrainz"}}},
            {"fetchedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"items",QVariantList{QVariantMap{{"ref",recording},{"artistMbid",mbid},{"destination",QVariantMap{{"playable",true}}},{"available",true}}}}};
        QVariantMap normalized;QVERIFY(VinylProvider::normalizeHome("populationCandidates",mbid,{{"seed",seed}},data,&normalized));
        const auto item=normalized.value("items").toList().first().toMap();QVERIFY(!item.value("available").toBool());
        QVERIFY(item.value("destination").toMap().isEmpty());QVERIFY(!item.value("key").toString().isEmpty());
        QVERIFY(!VinylProvider::normalizeHome("populationCandidates","different",{{"seed",seed}},data,&normalized));
    }
    void liveSelectedArtistCandidates(){
        const auto helper=qEnvironmentVariable("VINYL_LIVE_CANDIDATE_HELPER");
        if(helper.isEmpty())QSKIP("Opt-in live provider qualification only.");
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(helper,nullptr,128*1024*1024,{},dir.filePath("cache"));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QStringList channels{"UCPoQYATXIYvN5WB0c4f6jfQ","UCwzzSiogpjsYBMoyVcwDXwQ","UCl4iPtukwe7m0kIxUMskkgA"};
        for(const auto &channel:channels){
            const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"}};
            done.clear();QElapsedTimer timer;timer.start();
            QVERIFY(source.request("home-seed-0","artistCandidates",channel,{{"seed",seed}})>0);
            QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,9500);
            QVERIFY2(done.first()[3].toString().isEmpty(),qPrintable(done.first()[3].toString()));
            const auto data=done.first()[2].toMap();
            QCOMPARE(data.value("status").toString(),QString("ready"));QCOMPARE(data.value("seed").toMap(),seed);
            const auto items=data.value("items").toList();QVERIFY(!items.isEmpty());QVERIFY(items.size()<=60);
            int own=0;QString album;
            for(const auto &entry:items){const auto item=entry.toMap();QVERIFY(!item.value("key").toString().isEmpty());
                if(item.value("relation")=="own-release"){++own;if(album.isEmpty())album=item.value("destination").toMap().value("id").toString();}}
            QVERIFY(own>0);qInfo()<<"LIVE"<<channel<<"candidates"<<items.size()<<"own releases"<<own<<"ms"<<timer.elapsed();
            QVERIFY(!album.isEmpty());done.clear();QVERIFY(source.request("browse","album",album)>0);
            QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);
            QVERIFY2(done.first()[3].toString().isEmpty(),qPrintable(done.first()[3].toString()));
            QVERIFY(!done.first()[2].toMap().value("tracks").toList().isEmpty());
        }
    }
    void homeIdentityForwardsTypedSeedAndPreservesUnresolvedState(){
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,{},dir.filePath("cache"));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        const int id=source.request("home-identity","recommendationIdentity",channel,{{"seed",seed}});
        QVERIFY(id>0);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QCOMPARE(done.first()[0].toString(),QString("home-identity"));QCOMPARE(done.first()[1].toInt(),id);
        QVERIFY(done.first()[3].toString().isEmpty());
        const auto response=done.first()[2].toMap();
        QCOMPARE(response.value("version").toInt(),1);QCOMPARE(response.value("seed").toMap(),seed);
        QCOMPARE(response.value("mappingStatus").toString(),QString("unresolved"));
        QCOMPARE(response.value("status").toString(),QString("unresolved"));
        QVERIFY(response.value("complete").toBool());QVERIFY(response.value("items").toList().isEmpty());
        QCOMPARE(response.value("provenance").toMap().value("source").toString(),QString("musicbrainz"));
        QVERIFY(QDateTime::fromString(response.value("fetchedAt").toString(),Qt::ISODateWithMs).isValid());
    }
    void unavailableHomeCandidateIsValidEmptyEnvelope(){
        qputenv("VINYL_TEST_HOME_UNAVAILABLE","1");
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,{},dir.filePath("cache"));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        const int id=source.request("home-seed-0","artistCandidates",channel,{{"seed",seed},{"refresh",true}});
        QVERIFY(id>0);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QCOMPARE(done.first()[1].toInt(),id);QVERIFY(done.first()[3].toString().isEmpty());
        const auto response=done.first()[2].toMap();
        QCOMPARE(response.value("status").toString(),QString("unavailable"));
        QCOMPARE(response.value("sourceStatus").toString(),QString("unavailable"));
        QVERIFY(!response.value("error").toString().isEmpty());
        QVERIFY(response.value("items").toList().isEmpty());
    }
    void homeCandidatesRejectSeedMismatchWrongShapeAndOversize_data(){
        QTest::addColumn<QByteArray>("fault");
        QTest::newRow("seed-mismatch")<<QByteArray("VINYL_TEST_HOME_SEED_MISMATCH");
        QTest::newRow("wrong-shape")<<QByteArray("VINYL_TEST_HOME_WRONG_SHAPE");
        QTest::newRow("oversize")<<QByteArray("VINYL_TEST_HOME_OVERSIZE");
    }
    void homeCandidatesRejectSeedMismatchWrongShapeAndOversize(){
        QFETCH(QByteArray,fault);qputenv(fault.constData(),"1");
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,{},dir.filePath("cache"));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        const int id=source.request("home-seed-0","artistCandidates",channel,{{"seed",seed},{"refresh",true}});
        QVERIFY(id>0);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QCOMPARE(done.first()[1].toInt(),id);QVERIFY(!done.first()[3].toString().isEmpty());
        QVERIFY(done.first()[2].toMap().isEmpty());
    }
    void leaveArtistDoesNotCancelIndependentHomeCandidateLane(){
        qputenv("VINYL_TEST_HOME_SLOW","1");
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,{},dir.filePath("cache"));VinylCatalogueSource source(&local,&provider,nullptr);
        QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        const int id=source.request("home-seed-0","artistCandidates",channel,{{"seed",seed},{"refresh",true}});
        QVERIFY(id>0);source.leaveArtist();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QCOMPARE(done.first()[0].toString(),QString("home-seed-0"));QCOMPARE(done.first()[1].toInt(),id);
        QVERIFY(done.first()[3].toString().isEmpty());
        QCOMPARE(done.first()[2].toMap().value("items").toList().size(),1);
    }
    void homeCandidateSubprocessesStayWithinTwoSlots(){
        qputenv("VINYL_TEST_HOME_SLOW","1");
        QTemporaryDir cache;QVERIFY(cache.isValid());
        qint64 nowMs=1800000000000LL;
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,[&nowMs]{return nowMs;},cache.path());
        QSignalSpy done(&provider,&VinylProvider::completed);
        const QStringList lanes{"home-seed-0","home-seed-1","home-population"};
        for(int i=0;i<lanes.size();++i){
            const QString channel="UC"+QString(20,'0')+QString::number(i).rightJustified(2,'0');
            const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name",QString("Artist %1").arg(i)}};
            QVERIFY(provider.requestPayload(lanes[i],"artistCandidates",channel,{{"seed",seed},{"refresh",true}})>0);
        }
        auto runningCount=[&provider]{
            int active=0;
            for(auto *process:provider.findChildren<QProcess*>())if(process->state()!=QProcess::NotRunning)++active;
            return active;
        };
        QElapsedTimer elapsed;elapsed.start();int maximumRunning=0;
        while(done.size()<lanes.size()&&elapsed.elapsed()<10000){
            maximumRunning=qMax(maximumRunning,runningCount());QTest::qWait(5);
        }
        maximumRunning=qMax(maximumRunning,runningCount());
        QCOMPARE(done.size(),lanes.size());QVERIFY(maximumRunning<=2);
        QSet<QString> completedLanes;
        for(const auto &completion:done){
            completedLanes.insert(completion[0].toString());QVERIFY(completion[3].toString().isEmpty());
        }
        QCOMPARE(completedLanes.size(),lanes.size());
    }
    void incompleteHomeCandidateResultCanUseBoundedCacheAfterFailure(){
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));QSignalSpy done(&provider,&VinylProvider::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        const QVariantMap payload{{"seed",seed}};
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done.first()[3].toString().isEmpty());
        QVERIFY(!done.first()[2].toMap().value("complete").toBool());
        qputenv("VINYL_TEST_HOME_FAIL","1");
        auto refreshPayload=payload;refreshPayload["refresh"]=true;
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,refreshPayload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);QVERIFY(done.last()[3].toString().isEmpty());
        const auto cached=done.last()[2].toMap();
        QVERIFY(cached.value("_cached").toBool());QVERIFY(!cached.value("_cacheError").toString().isEmpty());
        QVERIFY(!cached.value("complete").toBool());QCOMPARE(cached.value("items").toList().size(),1);
    }
    void homeCandidateCacheServesFreshThenStaleAfterExpiry(){
        QTemporaryDir cache;QVERIFY(cache.isValid());
        qint64 nowMs=1800000000000LL;
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,[&nowMs]{return nowMs;},cache.path());
        QSignalSpy done(&provider,&VinylProvider::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        QVariantMap payload{{"seed",seed}};
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done.first()[3].toString().isEmpty());
        QVERIFY(cacheTreeFileCount(cache.path())>0);
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);QVERIFY(done.last()[3].toString().isEmpty());
        QVERIFY(done.last()[2].toMap().value("_cached").toBool());
        QVERIFY(done.last()[2].toMap().value("_cacheError").toString().isEmpty());
        nowMs+=7*60*60*1000LL;qputenv("VINYL_TEST_HOME_FAIL","1");payload["refresh"]=true;
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,10000);QVERIFY(done.last()[3].toString().isEmpty());
        const auto stale=done.last()[2].toMap();
        QVERIFY(stale.value("_cached").toBool());QVERIFY(!stale.value("_cacheError").toString().isEmpty());
        QCOMPARE(stale.value("items").toList().size(),1);
    }
    void incompleteEmptyHomeCandidateResultIsNeverNegativeCached(){
        QTemporaryDir cache;QVERIFY(cache.isValid());
        qint64 nowMs=1800000000000LL;
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,4*1024*1024,[&nowMs]{return nowMs;},cache.path());
        QSignalSpy done(&provider,&VinylProvider::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        QVariantMap payload{{"seed",seed},{"refresh",true}};qputenv("VINYL_TEST_HOME_EMPTY","1");
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done.first()[3].toString().isEmpty());
        QVERIFY(done.first()[2].toMap().value("items").toList().isEmpty());
        QVERIFY(!done.first()[2].toMap().value("complete").toBool());
        QCOMPARE(cacheTreeFileCount(cache.path()),0);
        qunsetenv("VINYL_TEST_HOME_EMPTY");qputenv("VINYL_TEST_HOME_FAIL","1");payload["refresh"]=true;
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);
        QVERIFY(!done.last()[3].toString().isEmpty());QVERIFY(done.last()[2].toMap().isEmpty());
        QCOMPARE(cacheTreeFileCount(cache.path()),0);
    }
    void homeRateLimitDefersNewProcessUntilRetryWindowEnds(){
        QTemporaryDir cache;QVERIFY(cache.isValid());
        qint64 nowMs=1800000000000LL;
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,4*1024*1024,[&nowMs]{return nowMs;},cache.path());
        QSignalSpy done(&provider,&VinylProvider::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        QVariantMap payload{{"seed",seed},{"refresh",true}};
        const auto counter=cache.filePath("launches.txt");qputenv("VINYL_TEST_HOME_COUNTER",counter.toUtf8());
        qputenv("VINYL_TEST_HOME_RATE","1");
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done.first()[3].toString().isEmpty());
        QCOMPARE(done.first()[2].toMap().value("status").toString(),QString("unavailable"));
        QCOMPARE(done.first()[2].toMap().value("sourceStatus").toString(),QString("rate_limited"));
        QCOMPARE(done.first()[2].toMap().value("retryAfterSeconds").toInt(),60);
        QCOMPARE(homeLaunchCount(counter),1);
        qunsetenv("VINYL_TEST_HOME_RATE");
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);QCOMPARE(homeLaunchCount(counter),1);
        QCOMPARE(done.last()[2].toMap().value("sourceStatus").toString(),QString("rate_limited"));
        nowMs+=61000;
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,payload)>0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,10000);QVERIFY(done.last()[3].toString().isEmpty());
        QCOMPARE(homeLaunchCount(counter),2);
        QCOMPARE(done.last()[2].toMap().value("status").toString(),QString("ready"));
    }
    void cacheEvictsCandidateResponsesWithinConfiguredBudget(){
        QTemporaryDir cache;QVERIFY(cache.isValid());
        qint64 nowMs=1800000000000LL;constexpr qint64 budget=4096;constexpr int requests=24;
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,budget,[&nowMs]{return nowMs;},cache.path());
        QSignalSpy done(&provider,&VinylProvider::completed);
        for(int i=0;i<requests;++i){
            const QString channel="UC"+QString(20,'0')+QString::number(i).rightJustified(2,'0');
            const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name",QString("Artist %1").arg(i)}};
            QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channel,{{"seed",seed}})>0);
            QTRY_COMPARE_WITH_TIMEOUT(done.size(),i+1,10000);QVERIFY(done.last()[3].toString().isEmpty());
        }
        QVERIFY(cacheTreeBytes(cache.path())>0);QVERIFY(cacheTreeBytes(cache.path())<=budget);
        QVERIFY(cacheTreeFileCount(cache.path())<requests);
    }
    void malformedHomeEnvelopeIsNotWrittenToDurableCache(){
        qputenv("VINYL_TEST_HOME_WRONG_SHAPE","1");
        QTemporaryDir dir;QVERIFY(dir.isValid());
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT),nullptr,128*1024*1024,{},dir.filePath("cache"));
        VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylCatalogueSource source(&local,&provider,nullptr);QSignalSpy done(&source,&VinylCatalogueSource::completed);
        const QString channel="UCedvOgsKFzcK3hA5taf3KoQ";
        const QVariantMap seed{{"provider","youtube-music"},{"providerId",channel},{"kind","artist"},{"name","Artist"}};
        const int id=source.request("home-seed-0","artistCandidates",channel,{{"seed",seed},{"refresh",true}});
        QVERIFY(id>0);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QVERIFY(!done.first()[3].toString().isEmpty());QVERIFY(done.first()[2].toMap().isEmpty());
        QCOMPARE(cacheTreeFileCount(dir.filePath("cache")),0);
    }
    void artworkAdmission_data(){
        QTest::addColumn<int>("count");QTest::addColumn<bool>("duplicate");QTest::addColumn<bool>("invalid");QTest::addColumn<int>("expected");
        QTest::newRow("four")<<4<<false<<false<<4;
        QTest::newRow("one")<<1<<false<<false<<1;
        QTest::newRow("none")<<0<<false<<false<<0;
        QTest::newRow("duplicates")<<4<<true<<false<<1;
        QTest::newRow("bad-geometry")<<4<<false<<true<<0;
    }
    void artworkAdmission(){
        QFETCH(int,count);QFETCH(bool,duplicate);QFETCH(bool,invalid);QFETCH(int,expected);
        qputenv("VINYL_TEST_PHOTOS",QByteArray::number(count));
        QTemporaryDir dir;VaultIdentity identity(dir.filePath("identity.json"));VinylLocalSource local(&identity);
        VinylProvider provider(QStringLiteral(VINYL_FAKE_TRANSPORT));ArtworkNam nam;nam.duplicate=duplicate;nam.invalid=invalid;
        VinylCatalogueSource source(&local,&provider,nullptr);source.setArtworkNetworkManager(&nam);
        source.openArtist({{"id","ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ"},{"name","Artist"}});
        QTRY_VERIFY_WITH_TIMEOUT(!source.artistBusy(),15000);
        QCOMPARE(source.artistArtwork().value("photos").toList().size(),expected);
        QCOMPARE(source.artistArtwork().value("distributionCleared").toBool(),false);
        auto *bridge=qobject_cast<vinyl::artist::ArtistServiceBridge *>(source.canonicalArtistService());
        QTRY_COMPARE(bridge->artwork().value("photos").toList().size(),expected);
        emit local.profileLeaving();QVERIFY(source.artistArtwork().isEmpty());
    }
};
QTEST_GUILESS_MAIN(CatalogueSourceTest)
#include "tst_catalogue_source.moc"
