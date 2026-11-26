#include <QtTest>
#include <QTemporaryDir>
#include <QUuid>
#include "vinyl/VinylProvider.h"

class VinylProviderLifecycle final : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    static QVariantMap data(const QSignalSpy &spy,int at=-1) {
        return spy.at(at<0?spy.size()-1:at).at(2).toMap();
    }
    static QString error(const QSignalSpy &spy,int at=-1) {
        return spy.at(at<0?spy.size()-1:at).at(3).toString();
    }
    QString cache() const{return root.path()+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces);}
private slots:
    void initTestCase(){QVERIFY(root.isValid());QVERIFY(!qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON").isEmpty());}
    void init(){
        qunsetenv("COLOSSEUM_VINYL_REUSE_HELPERS");
        qunsetenv("VINYL_FAKE_WARM_SECONDS");
        qunsetenv("VINYL_FAKE_START_LOG");
    }
    void cleanup(){QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    void reusesAcrossDistinctSelectionsAndSeparatesCatalogue(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("play","resolve","song-a");QTRY_COMPARE(replies.size(),1);
        const auto first=data(replies);QVERIFY(error(replies).isEmpty());
        provider.request("play","resolve","song-b");QTRY_COMPARE(replies.size(),2);
        QCOMPARE(data(replies).value("pid"),first.value("pid"));
        QCOMPARE(data(replies).value("sequence").toInt(),2);
        QCOMPARE(data(replies).value("value").toString(),QString("song-b"));
        provider.request("video-stream","resolveVideo","song-c");QTRY_COMPARE(replies.size(),3);
        QCOMPARE(data(replies).value("pid"),first.value("pid"));
        provider.request("browse","album","album-a");QTRY_COMPARE(replies.size(),4);
        QVERIFY(data(replies).value("pid")!=first.value("pid"));
        const auto catalogue=data(replies);
        provider.request("browse","album","album-b");QTRY_COMPARE(replies.size(),5);
        QCOMPARE(data(replies).value("pid"),catalogue.value("pid"));
    }
    void homeReusesAWorkerAcrossLanesAndOperations(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        const QString channelA=QStringLiteral("UC")+QString(22,QChar('A'));
        const QString channelB=QStringLiteral("UC")+QString(22,QChar('B'));
        const auto options=[](const QString &channel){
            return QVariantMap{{"seed",QVariantMap{{"kind","artist"},{"provider","youtube-music"},{"providerId",channel}}}};
        };
        QVERIFY(provider.requestPayload("home-seed-0","artistCandidates",channelA,options(channelA))>0);
        QVERIFY(provider.requestPayload("home-seed-1","artistCandidates",channelB,options(channelB))>0);
        QTRY_COMPARE(replies.size(),2);
        const auto first=data(replies,0).value("provenance").toMap();
        const auto second=data(replies,1).value("provenance").toMap();
        QVERIFY(error(replies,0).isEmpty());QVERIFY(error(replies,1).isEmpty());
        QVERIFY(first.value("pid")!=second.value("pid"));
        QVERIFY(provider.requestPayload("home-identity","recommendationIdentity",channelA,options(channelA))>0);
        QTRY_COMPARE(replies.size(),3);
        QVERIFY(error(replies).isEmpty());
        const auto next=data(replies).value("provenance").toMap();
        QVERIFY(next.value("pid")==first.value("pid")||next.value("pid")==second.value("pid"));
        QCOMPARE(next.value("sequence").toInt(),2);
    }
    void homeListeningDoesNotTakeTheForegroundAlbumWorker(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        QVERIFY(provider.request("browse","album","first")>0);
        QTRY_COMPARE(replies.size(),1);
        const auto foregroundPid=data(replies).value("pid");
        QVERIFY(provider.request("home-listening-0","album","delay")>0);
        QVERIFY(provider.request("browse","album","second")>0);
        QTRY_COMPARE(replies.size(),3);
        for(int i=1;i<3;++i){
            QVERIFY(error(replies,i).isEmpty());
            if(replies[i][0].toString()=="browse")QCOMPARE(data(replies,i).value("pid"),foregroundPid);
            else QVERIFY(data(replies,i).value("pid")!=foregroundPid);
        }
    }
    void lyricsReuseAnIndependentWorker(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        QVERIFY(provider.request("lyrics","lyrics","first")>0);
        QTRY_COMPARE(replies.size(),1);
        QVERIFY(error(replies).isEmpty());
        const auto lyricsPid=data(replies).value("pid");
        QVERIFY(provider.request("lyrics","lyrics","delay")>0);
        QVERIFY(provider.request("play","resolve","song")>0);
        QTRY_COMPARE(replies.size(),3);
        for(int i=1;i<3;++i){
            QVERIFY(error(replies,i).isEmpty());
            if(replies[i][0].toString()=="lyrics"){
                QCOMPARE(data(replies,i).value("pid"),lyricsPid);
                QCOMPARE(data(replies,i).value("sequence").toInt(),2);
            }else{
                QVERIFY(data(replies,i).value("pid")!=lyricsPid);
            }
        }
    }
    void warmupIsIdempotentReadyAndUncacheable(){
        const auto path=cache();VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},path);
        QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.warmPlayback();provider.warmPlayback();QTRY_COMPARE(replies.size(),1);
        QCOMPARE(replies[0][0].toString(),QString("media-warmup"));
        const auto warmed=data(replies);provider.warmPlayback();QTest::qWait(50);QCOMPARE(replies.size(),1);
        provider.request("play","resolve","arbitrary-a");QTRY_COMPARE(replies.size(),2);
        QCOMPARE(data(replies).value("pid"),warmed.value("pid"));
        QCOMPARE(data(replies).value("sequence").toInt(),2);
        QVERIFY(QDir(path).entryList({"*.json"},QDir::Files).isEmpty());
        QCOMPARE(provider.request("play","mediaWarmup",""),-1);
        QCOMPARE(provider.request("media-warmup","mediaWarmup","song"),-1);
        QCOMPARE(provider.request("media-warmup","resolve","song"),-1);
        QCOMPARE(provider.requestPayload("media-warmup","mediaWarmup","",{{"source","youtube"}}),-1);
    }
    void foregroundPreemptsWarmupWithoutWaiting(){
        qputenv("VINYL_FAKE_WARM_SECONDS","2");
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.warmPlayback();QTest::qWait(120);
        QElapsedTimer dispatch;dispatch.start();provider.request("play","resolve","selected-song");
        QVERIFY2(dispatch.elapsed()<100,"Cancellation blocked the GUI thread");
        QTRY_COMPARE_WITH_TIMEOUT(replies.size(),1,1300);
        QCOMPARE(replies[0][0].toString(),QString("play"));
        QCOMPARE(data(replies).value("value").toString(),QString("selected-song"));
        QCOMPARE(data(replies).value("sequence").toInt(),1);
        QTest::qWait(2100);QCOMPARE(replies.size(),1);
    }
    void overlapDoesNotQueueBehindExtraction(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("prepare-next","resolve","delay"); // strict source guard preserved
        QCOMPARE(replies.size(),0);
        QVERIFY(provider.requestPayload("prepare-next","resolve","delay",{{"source","youtube"}})>0);
        QTest::qWait(100);provider.request("play","resolve","selected-song");
        QTRY_VERIFY_WITH_TIMEOUT(replies.size()>=1,400);
        QCOMPARE(replies[0][0].toString(),QString("play"));
        QTRY_COMPARE(replies.size(),2);QVERIFY(data(replies,0).value("pid")!=data(replies,1).value("pid"));
        QCOMPARE(provider.requestPayload("prepare-next","album","x",{{"source","youtube"}}),-1);
        QCOMPARE(provider.requestPayload("prepare-next","resolve","x",{{"source","torrent"}}),-1);
    }
    void cleanErrorsReuseAndExplicitRetryRetiresClients(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("play","resolve","a");QTRY_COMPARE(replies.size(),1);const auto first=data(replies).value("pid");
        provider.request("play","resolve","error");QTRY_COMPARE(replies.size(),2);QVERIFY(!error(replies).isEmpty());
        provider.request("play","resolve","b");QTRY_COMPARE(replies.size(),3);QCOMPARE(data(replies).value("pid"),first);
        QCOMPARE(data(replies).value("sequence").toInt(),3);const auto second=data(replies).value("pid");
        provider.invalidatePlaybackTransport();provider.request("play","resolve","c");QTRY_COMPARE(replies.size(),4);
        QVERIFY(data(replies).value("pid")!=second);QCOMPARE(data(replies).value("sequence").toInt(),1);
    }
    void cancelPrespawnsReusableMediaWorker(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("play","resolve","a");
        QTRY_COMPARE(replies.size(),1);
        const auto cancelled=data(replies).value("pid");
        provider.request("play","resolve","delay");
        QTest::qWait(80);
        provider.cancel("play");
        QTRY_COMPARE(replies.size(),2);
        QCOMPARE(replies[1][0].toString(),QString("media-warmup"));
        const auto replacement=data(replies).value("pid");
        QVERIFY(replacement!=cancelled);
        provider.request("play","resolve","after-cancel");
        QTRY_COMPARE(replies.size(),3);
        QCOMPARE(data(replies).value("pid"),replacement);
        QCOMPARE(data(replies).value("sequence").toInt(),2);
        QTest::qWait(600);
        QCOMPARE(replies.size(),3);
    }
    void noRespawnAfterTeardown_data(){
        QTest::addColumn<QString>("action");
        QTest::newRow("cancelAll")<<QString("cancelAll");
        QTest::newRow("shuttingDown")<<QString("shuttingDown");
        QTest::newRow("profileLeaving")<<QString("profileLeaving");
        QTest::newRow("invalidate")<<QString("invalidate");
    }
    void noRespawnAfterTeardown(){
        QFETCH(QString,action);
        const auto log=root.path()+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        qputenv("VINYL_FAKE_START_LOG",log.toUtf8());
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("play","resolve","initial");
        QTRY_COMPARE(replies.size(),1);
        provider.request("play","resolve","delay");
        QTest::qWait(80);
        provider.cancel("play");
        QTRY_COMPARE(replies.size(),2);
        QCOMPARE(replies[1][0].toString(),QString("media-warmup"));
        QFile starts(log);
        QVERIFY(starts.open(QIODevice::ReadOnly));
        const auto before=starts.readAll();
        starts.close();
        QCOMPARE(before.count('\n'),2);
        provider.request("play","resolve","delay");
        QTest::qWait(80);
        provider.cancel("play");
        if(action=="cancelAll")provider.cancelAll();
        else if(action=="shuttingDown")emit provider.shuttingDown();
        else if(action=="profileLeaving")emit provider.profileLeaving();
        else provider.invalidatePlaybackTransport();
        QTest::qWait(800);
        QCOMPARE(replies.size(),2);
        QVERIFY(starts.open(QIODevice::ReadOnly));
        QCOMPARE(starts.readAll(),before);
        qunsetenv("VINYL_FAKE_START_LOG");
    }
    void foregroundSearchReusesWorker(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("search-albums","search","first");
        QTRY_COMPARE(replies.size(),1);
        const auto first=data(replies).value("pid");
        provider.request("search-albums","search","second");
        QTRY_COMPARE(replies.size(),2);
        QCOMPARE(data(replies).value("pid"),first);
        QCOMPARE(data(replies).value("sequence").toInt(),2);
    }
    void overlappingSearchUsesOneShot(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("search-albums","search","slow-search");
        QTest::qWait(100);
        provider.request("search-artists","search","overlap");
        QTRY_COMPARE_WITH_TIMEOUT(replies.size(),1,1500);
        QCOMPARE(replies[0][0].toString(),QString("search-artists"));
        QCOMPARE(data(replies).value("sequence").toInt(),1);
        const auto oneShot=data(replies).value("pid");
        QTRY_COMPARE(replies.size(),2);
        const auto warm=data(replies).value("pid");
        QVERIFY(warm!=oneShot);
        provider.request("search-albums","search","after-overlap");
        QTRY_COMPARE(replies.size(),3);
        QCOMPARE(data(replies).value("pid"),warm);
        QCOMPARE(data(replies).value("sequence").toInt(),2);
    }
    void transportFailurePrespawnsReplacement_data(){
        QTest::addColumn<QString>("value");
        QTest::newRow("crash")<<QString("exit");
        QTest::newRow("malformed")<<QString("malformed");
    }
    void transportFailurePrespawnsReplacement(){
        QFETCH(QString,value);
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("play","resolve",value);
        QTRY_COMPARE(replies.size(),2);
        QVERIFY(!error(replies,0).isEmpty());
        QCOMPARE(replies[1][0].toString(),QString("media-warmup"));
        const auto replacement=data(replies).value("pid");
        provider.request("play","resolve","after-failure");
        QTRY_COMPARE(replies.size(),3);
        QCOMPARE(data(replies).value("pid"),replacement);
        QCOMPARE(data(replies).value("sequence").toInt(),2);
    }
    void catalogueAndBrowseCancelPrespawnIdleWorker_data(){
        QTest::addColumn<QString>("operation");
        QTest::newRow("catalogue")<<QString("album");
        QTest::newRow("browse")<<QString("search");
    }
    void catalogueAndBrowseCancelPrespawnIdleWorker(){
        QFETCH(QString,operation);
        const auto log=root.path()+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        qputenv("VINYL_FAKE_START_LOG",log.toUtf8());
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("browse",operation,"initial");
        QTRY_COMPARE(replies.size(),1);
        const auto first=data(replies).value("pid");
        provider.request("browse",operation,"delay");
        QTest::qWait(80);
        provider.cancel("browse");
        const auto starts=[&log]{
            QFile file(log);
            if(!file.open(QIODevice::ReadOnly))return QByteArray();
            return file.readAll();
        };
        QTRY_COMPARE(starts().count('\n'),2);
        const auto replacement=starts().trimmed().split('\n').last().toLongLong();
        provider.request("browse",operation,"after-cancel");
        QTRY_COMPARE(replies.size(),2);
        QCOMPARE(data(replies).value("pid").toLongLong(),replacement);
        QVERIFY(data(replies).value("pid")!=first);
        provider.request("browse",operation,"reuse");
        QTRY_COMPARE(replies.size(),3);
        QCOMPARE(data(replies).value("pid").toLongLong(),replacement);
        QCOMPARE(data(replies).value("sequence").toInt(),2);
    }
    void browseOperationsShareOneWorker(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QSignalSpy replies(&provider,&VinylProvider::completed);
        QVariant pid;
        int sequence=0;
        for(const auto &operation:QStringList{"search","catalogueSearch","artist","artistReleases","artistVideos","artistArtwork"}){
            provider.request("browse",operation,"distinct-"+operation);
            ++sequence;
            QTRY_COMPARE(replies.size(),sequence);
            QVERIFY(error(replies).isEmpty());
            if(sequence==1)pid=data(replies).value("pid");
            QCOMPARE(data(replies).value("pid"),pid);
            QCOMPARE(data(replies).value("sequence").toInt(),sequence);
        }
    }
    void crashedWorkerAndFailedStartupRecover(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("play","resolve","exit");QTRY_COMPARE(replies.size(),1);QVERIFY(!error(replies).isEmpty());
        provider.request("play","resolve","after-crash");QTRY_COMPARE(replies.size(),2);QVERIFY(error(replies).isEmpty());
        QCOMPARE(data(replies).value("sequence").toInt(),1);
        provider.cancelAll();const auto python=qgetenv("COLOSSEUM_VINYL_PYTHON");
        qputenv("COLOSSEUM_VINYL_PYTHON","nonexistent-vinyl-test-python");
        provider.warmPlayback();QTRY_COMPARE(replies.size(),3);QVERIFY(!error(replies).isEmpty());
        qputenv("COLOSSEUM_VINYL_PYTHON",python);provider.request("play","resolve","after-start-failure");
        QTRY_COMPARE(replies.size(),4);QVERIFY(error(replies).isEmpty());QCOMPARE(data(replies).value("sequence").toInt(),1);
    }
    void cancellationAndProfileEventsFenceLateReplies(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("play","resolve","delay");QTest::qWait(80);provider.cancel("play");
        provider.request("play","resolve","new-selection");QTRY_COMPARE(replies.size(),1);
        QCOMPARE(data(replies).value("value").toString(),QString("new-selection"));const auto first=data(replies).value("pid");
        emit provider.profileLeaving();provider.request("play","resolve","after-profile");QTRY_COMPARE(replies.size(),2);
        QVERIFY(data(replies).value("pid")!=first);const auto second=data(replies).value("pid");
        emit provider.shuttingDown();provider.request("play","resolve","after-shutdown");QTRY_COMPARE(replies.size(),3);
        QVERIFY(data(replies).value("pid")!=second);
        provider.request("play","resolve","delay");provider.cancelAll();QTest::qWait(700);QCOMPARE(replies.size(),3);
    }
    void idleGenerationCannotRetireNewerRequest(){
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache(),180,180);QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.request("play","resolve","a");QTRY_COMPARE(replies.size(),1);const auto first=data(replies).value("pid");
        QTest::qWait(100);provider.request("play","resolve","delay");QTRY_COMPARE(replies.size(),2);
        QCOMPARE(data(replies).value("pid"),first);
        provider.request("play","resolve","b");QTRY_COMPARE(replies.size(),3);QCOMPARE(data(replies).value("pid"),first);
        QTest::qWait(280);provider.request("play","resolve","after-idle");QTRY_COMPARE(replies.size(),4);
        QVERIFY(data(replies).value("pid")!=first);QCOMPARE(data(replies).value("sequence").toInt(),1);
    }
    void sameBinaryOptoutKeepsOneShotContract(){
        qputenv("COLOSSEUM_VINYL_REUSE_HELPERS","0");
        VinylProvider provider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.warmPlayback();QTest::qWait(40);QCOMPARE(replies.size(),0);
        provider.request("play","resolve","a");QTRY_COMPARE(replies.size(),1);const auto first=data(replies).value("pid");
        provider.request("play","resolve","b");QTRY_COMPARE(replies.size(),2);
        QVERIFY(data(replies).value("pid")!=first);QCOMPARE(data(replies).value("sequence").toInt(),1);
    }
    void destructionFencesDeferredLaunchAndCachedResult(){
        const auto path=cache();int delivered=0;
        auto *immediate=new VinylProvider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},path);
        connect(immediate,&VinylProvider::completed,this,[&delivered]{++delivered;});
        immediate->request("play","resolve","never-launch");delete immediate;
        QTest::qWait(100);QCOMPARE(delivered,0);
        // Populate a public cache, then destroy the next provider while its
        // cache result is still queued on its detached process context.
        {
            VinylProvider populate(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},path);
            QSignalSpy replies(&populate,&VinylProvider::completed);
            populate.request("browse","album","cached");QTRY_COMPARE(replies.size(),1);
        }
        auto *cached=new VinylProvider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},path);
        connect(cached,&VinylProvider::completed,this,[&delivered]{++delivered;});
        cached->request("browse","album","cached");delete cached;
        QTest::qWait(100);QCOMPARE(delivered,0);
    }
    void synchronousDeletionFromCompletedIsSafe(){
        auto *provider=new VinylProvider(VINYL_LIFECYCLE_HELPER,nullptr,4096,{},cache());
        QPointer<VinylProvider> alive(provider);int delivered=0;
        connect(provider,&VinylProvider::completed,this,[provider,&delivered]{++delivered;delete provider;});
        provider->request("play","resolve","selected-song");
        QTRY_COMPARE(delivered,1);QVERIFY(!alive);QTest::qWait(100);
    }
    void externalHelperKeepsOneShotContract(){
        const auto path=root.path()+"/external-helper.py";QVERIFY(QFile::copy(VINYL_LIFECYCLE_HELPER,path));
        VinylProvider provider(path,nullptr,4096,{},cache());QSignalSpy replies(&provider,&VinylProvider::completed);
        provider.warmPlayback();QTest::qWait(30);QCOMPARE(replies.size(),0);
        provider.request("play","resolve","a");QTRY_COMPARE(replies.size(),1);const auto first=data(replies).value("pid");
        provider.request("play","resolve","b");QTRY_COMPARE(replies.size(),2);QVERIFY(data(replies).value("pid")!=first);
    }
};
QTEST_GUILESS_MAIN(VinylProviderLifecycle)
#include "vinyl_provider_lifecycle_test.moc"
