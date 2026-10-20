#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QUuid>
#include "vinyl/VinylCatalogueSource.h"
#include "engine/VaultIdentity.h"
#include "CollectionStore.h"

class CatalogueSourceTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup(){qunsetenv("VINYL_TEST_IDENTITY_UNAVAILABLE");}
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
};
QTEST_GUILESS_MAIN(CatalogueSourceTest)
#include "tst_catalogue_source.moc"
