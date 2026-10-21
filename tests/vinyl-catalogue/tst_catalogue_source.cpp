#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QUuid>
#include "vinyl/VinylCatalogueSource.h"
#include "engine/VaultIdentity.h"
#include "CollectionStore.h"
#include <QNetworkReply>
#include <QImage>
#include <QBuffer>

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
    void cleanup(){qunsetenv("VINYL_TEST_IDENTITY_UNAVAILABLE");qunsetenv("VINYL_TEST_PHOTOS");}
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
