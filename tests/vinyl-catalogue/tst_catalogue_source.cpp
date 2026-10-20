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

class CatalogueSourceTest : public QObject {
    Q_OBJECT
private slots:
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
