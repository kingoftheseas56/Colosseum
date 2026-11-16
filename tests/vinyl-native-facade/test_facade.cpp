#include <QtTest>
#include <QQuickItem>
#include <QJsonDocument>
#include <QJsonObject>
#include "player/mpvitem.h"
#include "vinyl/VinylBackendSession.h"
#include "VinylSessionPersistence.h"
#include "FeedbackTastePersistence.h"

class DeferredSource : public QObject {
    Q_OBJECT
public:
    int serial=0;
    QHash<QString,int> requests;
    Q_INVOKABLE int request(const QString &lane,const QString &,const QString &) {
        requests[lane]=++serial;
        return serial;
    }
    Q_INVOKABLE void cancel(const QString &) {}
signals:
    void completed(const QString &,int,const QVariant &,const QString &);
};
class PersonalPorts : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString vinylFeedbackSnapshot MEMBER feedback)
public:
    QString feedback;
    QVariantMap saved;
    Q_INVOKABLE bool setVinylFeedbackSnapshot(const QString &value) { feedback=value;return true; }
    Q_INVOKABLE bool add(const QString &world,const QVariantMap &entry) { if(world!="Vinyl")return false;saved=entry;return true; }
};

class FacadeTest : public QObject {
    Q_OBJECT
    QVariantMap row(const QString &id) {
        QVariantMap track{{"id",id},{"title",id},{"seconds",30}};
        QVariantMap edition{{"id","edition"},{"albumId","album"},{"name","Album"},
            {"discs",QVariantList{QVariant::fromValue(QVariantList{track})}}};
        return {{"trackId",id},{"title",id},{"artist","Artist"},{"seconds",30},
            {"albumId","album"},{"editionId","edition"},{"discIndex",0},{"trackIndex",0},
            {"track",track},{"edition",edition}};
    }
    void resolveAudio(DeferredSource &source, VinylBackendSession &session, const QString &id) {
        const int previous=source.requests.value("play");
        QTRY_VERIFY(source.requests.value("play")>previous);
        emit source.completed("play",source.requests.value("play"),QVariantMap{
            {"trackId",id},{"url",QUrl::fromLocalFile(QStringLiteral(VAULT_FIXTURES_DIR)
                +"/media/tiny-audio.m4a").toString()}},{});
        QTRY_VERIFY(session.player());
        auto *item=qobject_cast<MpvItem *>(session.player());
        QVERIFY(item);
        item->setProperty("ao","null");
        QTRY_VERIFY(session.loaded());
    }
    void rejectAudio(DeferredSource &source, const QString &error="This track is unavailable.") {
        const int previous=source.requests.value("play");
        QTRY_VERIFY(source.requests.value("play")>previous);
        emit source.completed("play",source.requests.value("play"),QVariantMap{},error);
    }
private slots:
    void positionTicksDoNotPublishUnrelatedState() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a")},0,false));
        session.setPaused(true);
        resolveAudio(source,session,"vault:a");
        QTest::qWait(100);
        QSignalSpy state(&session,&VinylBackendSession::stateChanged);
        const auto property=session.metaObject()->property(session.metaObject()->indexOfProperty("position"));
        QSignalSpy position(&session,property.notifySignal());
        auto *item=qobject_cast<MpvItem *>(session.player());
        QVERIFY(item);
        emit item->positionChanged();
        QCOMPARE(position.count(),1);
        QCOMPARE(state.count(),0);
    }
    void unavailableMiddleTrackAdvancesWithoutCompletingListen() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b"),row("vault:c")},1,false));
        QSignalSpy endings(&session,&VinylBackendSession::playbackEnding);
        rejectAudio(source);
        QTRY_COMPARE(session.activeIndex(),2);
        QCOMPARE(session.property("skippedNotice").toString(),QString("Skipped vault:b: not available."));
        QVERIFY(!endings.isEmpty());
        for(const auto &ending:endings)QVERIFY(!ending[0].toBool());
        QCOMPARE(session.preparationState().value("listeningMs").toLongLong(),0);
        session.setPaused(true);
        resolveAudio(source,session,"vault:c");
        QCOMPARE(session.sourceState(),QString("playing"));
    }
    void unavailableLastTrackStopsAndCanRetry() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},1,false));
        rejectAudio(source);
        QTRY_COMPARE(session.sourceState(),QString("error"));
        QCOMPARE(session.activeIndex(),1);
        QVERIFY(session.property("skippedNotice").toString().isEmpty());
        session.retry();session.setPaused(true);
        resolveAudio(source,session,"vault:b");
        QVERIFY(session.loaded());
    }
    void threeUnavailableTracksStopTheRun() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b"),row("vault:c"),row("vault:d")},0,false));
        rejectAudio(source);QTRY_COMPARE(session.activeIndex(),1);
        rejectAudio(source);QTRY_COMPARE(session.activeIndex(),2);
        rejectAudio(source);QTRY_COMPARE(session.sourceState(),QString("error"));
        QCOMPARE(session.activeIndex(),2);
        QVERIFY(session.paused());
    }
    void successfulLoadResetsUnavailableRun() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b"),row("vault:c"),row("vault:d"),row("vault:e")},0,false));
        rejectAudio(source);QTRY_COMPARE(session.activeIndex(),1);
        rejectAudio(source);QTRY_COMPARE(session.activeIndex(),2);
        session.setPaused(true);resolveAudio(source,session,"vault:c");
        session.next(1);
        rejectAudio(source);QTRY_COMPARE(session.activeIndex(),4);
        QVERIFY(session.sourceState()!="error");
    }
    void unavailableNavigation_data() {
        QTest::addColumn<bool>("backward");
        QTest::newRow("backward") << true;
        QTest::newRow("repeat-all-wrap") << false;
    }
    void unavailableNavigation() {
        QFETCH(bool,backward);
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b"),row("vault:c")},-1,false));
        if(backward){session.playQueue(2);session.next(-1);}
        else {session.cycleRepeat();session.playQueue(2);}
        rejectAudio(source);
        QTRY_COMPARE(session.activeIndex(),0);
        QVERIFY(session.sourceState()!="error");
    }
    void decodeFailureEndsListeningWithoutCompletion() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},0,false));
        session.setPaused(true);resolveAudio(source,session,"vault:a");
        session.beginListeningFacts();
        QSignalSpy endings(&session,&VinylBackendSession::playbackEnding);
        QStringList reasons;
        connect(&session,&VinylBackendSession::playbackEnding,this,[&](bool) {
            reasons.append(session.playbackEndingReason());
        });
        emit qobject_cast<MpvItem *>(session.player())->playbackError("decode","Invalid audio");
        QCOMPARE(session.activeIndex(),1);
        QCOMPARE(endings.size(),1);
        QVERIFY(!endings[0][0].toBool());
        QCOMPARE(reasons,QStringList{"failure"});
        QVERIFY(!session.qualifyListeningSample(true));
        QCOMPARE(session.preparationState().value("listenEvents").toInt(),0);
    }
    void singleTrackRepeatDoesNotSkip() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        session.cycleRepeat();
        QVERIFY(session.replaceRows({row("vault:a")},0,false));
        rejectAudio(source);
        QTRY_COMPARE(session.sourceState(),QString("error"));
        QCOMPARE(session.activeIndex(),0);
        QVERIFY(session.skippedNotice().isEmpty());
    }
    void unavailableFlagAndRetiredResolution() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        auto unavailable=row("vault:a");
        auto track=unavailable.value("track").toMap();track["available"]=false;unavailable["track"]=track;
        QVERIFY(session.replaceRows({unavailable,row("vault:b")},0,false));
        QTRY_COMPARE(session.activeIndex(),1);
        QTRY_VERIFY(source.requests.value("play")>0);
        const int request=source.requests.value("play");
        session.suspend();
        emit source.completed("play",request,QVariantMap{},"This track is unavailable.");
        QCoreApplication::processEvents();
        QCOMPARE(session.activeIndex(),1);
        QCOMPARE(session.sourceState(),QString("paused"));
        QVERIFY(session.skippedNotice().isEmpty());
    }
    void globalSourceFailureDoesNotSkip_data() {
        QTest::addColumn<QString>("error");
        QTest::newRow("helper") << QString("The music catalogue helper failed.");
        QTest::newRow("transport") << QString("The music catalogue runtime is unavailable. Local music is still available.");
    }
    void globalSourceFailureDoesNotSkip() {
        QFETCH(QString,error);
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},0,false));
        rejectAudio(source,error);
        QTRY_COMPARE(session.sourceState(),QString("error"));
        QCOMPARE(session.activeIndex(),0);
    }
    void consecutiveAudioLoadsReuseCoreAndRejectRetiredEvents() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");
        session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},0,false));
        session.setPaused(true);
        resolveAudio(source,session,"vault:a");
        QVERIFY(session.loaded());
        QPointer<MpvItem> first=qobject_cast<MpvItem *>(session.player());
        const auto firstGeneration=first->playbackGeneration();
        first->setVolume(37);
        first->setMute(true);
        first->setSpeed(1.25);
        QTRY_COMPARE(first->volume(),37);
        QTRY_VERIFY(first->mute());
        QTRY_COMPARE(first->speed(),1.25);

        session.playQueue(1);
        session.setPaused(true);
        resolveAudio(source,session,"vault:b");
        QVERIFY(first);
        QCOMPARE(session.player(),first.data());
        QCOMPARE(session.findChildren<QObject *>("vinylMpvPlayer").size(),1);
        QVERIFY(first->playbackGeneration()>firstGeneration);
        QCOMPARE(first->volume(),37);
        QVERIFY(first->mute());
        QCOMPARE(first->speed(),1.25);
        const auto state=session.sourceState();
        const auto index=session.activeIndex();
        emit first->nativePlaybackEvent("ended-eof",1,0,1,firstGeneration,1,1);
        emit first->nativePlaybackEvent("ended-error",1,0,1,firstGeneration,1,2);
        emit first->trackedSeekReply(1,false,firstGeneration,1);
        QCOMPARE(session.sourceState(),state);
        QCOMPARE(session.activeIndex(),index);
        QVERIFY(session.loaded());
    }
    void consecutivePlayingLoadsDoNotInheritRetirementPause() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");
        session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},0,false));
        session.setPaused(true);
        resolveAudio(source,session,"vault:a");
        QVERIFY(session.loaded());
        auto *first=qobject_cast<MpvItem *>(session.player());
        session.setPaused(false);
        QTRY_VERIFY(!first->pause());
        QVERIFY(!session.paused());
        session.playQueue(1);
        resolveAudio(source,session,"vault:b");
        QVERIFY(session.loaded());
        auto *second=qobject_cast<MpvItem *>(session.player());
        // Do not trust the old cached pause value immediately after loading:
        // require actual progress from the replacement file as well.
        QTRY_VERIFY(second->position()>0.1);
        QVERIFY(!second->pause());
        QVERIFY(!session.paused());
    }
    void lifecycleDestroysActiveAndParkedCores_data() {
        QTest::addColumn<QString>("action");
        QTest::addColumn<bool>("parked");
        for(const auto &action:{QString("suspend"),QString("shutdown"),QString("profile")}) {
            QTest::newRow(qPrintable(action+"-active")) << action << false;
            QTest::newRow(qPrintable(action+"-parked")) << action << true;
        }
    }
    void lifecycleDestroysActiveAndParkedCores() {
        QFETCH(QString,action);
        QFETCH(bool,parked);
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");
        session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},0,false));
        session.setPaused(true);
        resolveAudio(source,session,"vault:a");
        QPointer<QObject> core=session.player();
        QVERIFY(core);
        if(parked) {
            session.playQueue(1); // Leave resolution pending, with the audio core idle.
            QVERIFY(!session.player());
        }
        if(action=="suspend")session.suspend();
        else if(action=="shutdown")session.shutdown();
        else session.setProfileScope("profile-b");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(core.isNull());
        QVERIFY(!session.player());
        QVERIFY(session.findChildren<QObject *>("vinylMpvPlayer").isEmpty());
    }
    void videoNeverReusesAudioCore() {
        DeferredSource source;
        VinylBackendSession session;
        session.setProfileScope("profile-a");
        session.setSource(&source);
        auto videoRow=row("vault:a");
        auto track=videoRow["track"].toMap();
        track["videoId"]="abcdefghijk";
        videoRow["track"]=track;
        QVERIFY(session.replaceRows({videoRow},0,false));
        session.setPaused(true);
        resolveAudio(source,session,"vault:a");
        QPointer<QObject> audio=session.player();
        QTRY_VERIFY(source.requests.value("video-match")>0);
        emit source.completed("video-match",source.requests.value("video-match"),QVariantMap{
            {"version",1},{"status","ready"},{"trackId","vault:a"},
            {"sourceVideoId","abcdefghijk"},{"videoId","abcdefghijk"}},{});
        QTRY_VERIFY(session.hasVideo());
        session.toggleVideo();
        QVERIFY(source.requests.value("video-stream")>0);
        // No render surface/window: the video load waits for frameSwapped, so
        // this synthetic approved URL is never fetched.
        emit source.completed("video-stream",source.requests.value("video-stream"),QVariantMap{
            {"version",1},{"trackId","vault:a"},{"videoId","abcdefghijk"},
            {"hasVideo",true},{"hasAudio",true},{"url","https://test.googlevideo.com/video"}},{});
        QTRY_VERIFY(session.videoActive());
        QVERIFY(session.player());
        QVERIFY(session.player()!=audio.data());
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(audio.isNull());
        session.suspend();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(session.findChildren<QObject *>("vinylMpvPlayer").isEmpty());
    }
    void unrelatedStateDoesNotNotifyPaused() {
        DeferredSource source;VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},-1,false));
        const auto property=session.metaObject()->property(session.metaObject()->indexOfProperty("paused"));
        QSignalSpy pauses(&session,property.notifySignal());QVERIFY(pauses.isValid());
        session.cycleRepeat();session.setShuffle(true);session.moveQueue(0,1);
        QCOMPARE(pauses.count(),0);
        session.playQueue(0);QCOMPARE(pauses.count(),1);
        session.setPaused(true);QCOMPARE(pauses.count(),2);
        session.setPaused(true);QCOMPARE(pauses.count(),2);
    }
    void localQueueHasNoInventedMbid() {
        VinylBackendSession session;
        QVERIFY(!session.replaceRows({row("vault:a")},-1,false));
        session.setProfileScope("profile-a");
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:a"),row("vault:b")},-1,false));
        QCOMPARE(session.rows().size(),3);
        QVERIFY(session.rows()[0].toMap()["queueId"]!=session.rows()[1].toMap()["queueId"]);
        const auto checkpoint=session.checkpoint();
        QVERIFY(!checkpoint.value("backendState").toString().contains("recordingMbid\":\""));
        QVERIFY(checkpoint.value("backendState").toString().contains("vault:a"));
    }
    void nativeQueueCommandsPreserveOccurrenceAndColdRestore() {
        DeferredSource source;
        VinylBackendSession session; session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b"),row("vault:a")},0,false));
        const auto id=session.rows()[0].toMap()["queueId"];
        session.moveQueue(0,2); QCOMPARE(session.activeIndex(),2);
        QCOMPARE(session.rows()[2].toMap()["queueId"],id);
        session.cycleRepeat();session.cycleRepeat();QCOMPARE(session.repeatMode(),QString("one"));
        session.setShuffle(true);session.setShuffle(false);
        const auto checkpoint=session.checkpoint();
        VinylBackendSession restored;restored.setProfileScope("profile-a");
        QVERIFY(restored.restoreCheckpoint(checkpoint));
        QCOMPARE(restored.rows(),session.rows());QVERIFY(restored.paused());QVERIFY(!restored.player());
        QCOMPARE(restored.repeatMode(),QString("one"));
        VinylBackendSession foreign;foreign.setProfileScope("profile-b");
        QVERIFY(!foreign.restoreCheckpoint(checkpoint));QVERIFY(foreign.rows().isEmpty());
    }
    void radioContextAndPlaybackTimestampRoundTripInV3Checkpoint() {
        DeferredSource source;
        VinylBackendSession session;session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},0,false));
        const QVariantMap context{{"kind","artist-radio"},{"seed",QVariantMap{
            {"provider","youtube-music"},{"providerId","UCedvOgsKFzcK3hA5taf3KoQ"},
            {"kind","artist"},{"name","  Test artist  "}}}};
        const QVariantMap normalized{{"kind","artist-radio"},{"seed",QVariantMap{
            {"provider","youtube-music"},{"providerId","UCedvOgsKFzcK3hA5taf3KoQ"},
            {"kind","artist"},{"name","Test artist"}}}};
        QSignalSpy stateChanges(&session,&VinylBackendSession::stateChanged);
        QVERIFY(session.setProperty("radioContext",QVariant::fromValue(context)));
        QCOMPARE(session.radioContext(),normalized);
        QCOMPARE(stateChanges.count(),1);
        auto invalid=context;
        auto invalidSeed=invalid.value("seed").toMap();invalidSeed["providerId"]="UC-short";invalid["seed"]=invalidSeed;
        QVERIFY(session.setProperty("radioContext",QVariant::fromValue(invalid)));
        QCOMPARE(session.radioContext(),normalized);
        QCOMPARE(stateChanges.count(),1);
        QVERIFY(session.setProperty("radioContext",QVariant::fromValue(QVariantMap{})));
        QVERIFY(session.radioContext().isEmpty());
        QCOMPARE(stateChanges.count(),2);
        QVERIFY(session.setProperty("radioContext",QVariant::fromValue(normalized)));
        QCOMPARE(session.radioContext(),normalized);
        QCOMPARE(stateChanges.count(),3);
        const int timestampProperty=session.metaObject()->indexOfProperty("lastPlaybackAtMs");
        QVERIFY(timestampProperty>=0);
        QVERIFY(!session.metaObject()->property(timestampProperty).isWritable());
        QCOMPARE(session.lastPlaybackAtMs(),qint64(0));
        session.beginListeningFacts();
        QVERIFY(!session.qualifyListeningSample(false));
        QVERIFY(!session.qualifyListeningSample(true));
        QCOMPARE(session.lastPlaybackAtMs(),qint64(0));

        session.next(1);
        QCOMPARE(session.listeningOrigin(),QString("manual"));
        QCOMPARE(session.radioContext(),normalized);
        auto checkpoint=session.checkpoint();
        QCOMPARE(checkpoint.value("version").toInt(),3);
        QCOMPARE(checkpoint.value("lastPlaybackAtMs").toLongLong(),qint64(0));
        QCOMPARE(checkpoint.value("radioContext").toMap(),normalized);
        const qint64 lastPlayback=1700000000123LL;
        checkpoint["lastPlaybackAtMs"]=lastPlayback;
        const auto summary=session.checkpointSummary(checkpoint);
        QVERIFY(summary.value("valid").toBool());
        QCOMPARE(summary.value("lastPlaybackAtMs").toLongLong(),lastPlayback);
        QCOMPARE(summary.value("radioContext").toMap(),normalized);

        VinylBackendSession restored;restored.setProfileScope("profile-a");
        QVERIFY(restored.restoreCheckpoint(checkpoint));
        QCOMPARE(restored.lastPlaybackAtMs(),lastPlayback);
        QCOMPARE(restored.radioContext(),normalized);
        DeferredSource restoredSource;restored.setSource(&restoredSource);
        QVERIFY(restored.appendRows({row("vault:c")},false));
        restored.moveQueue(0,1);
        restored.next(1);
        QCOMPARE(restored.lastPlaybackAtMs(),lastPlayback);
        QCOMPARE(restored.radioContext(),normalized);
        QCOMPARE(restored.listeningOrigin(),QString("manual"));

        auto olderV3=checkpoint;
        olderV3.remove("lastPlaybackAtMs");olderV3.remove("radioContext");
        VinylBackendSession older;older.setProfileScope("profile-a");
        QVERIFY(older.restoreCheckpoint(olderV3));
        QCOMPARE(older.lastPlaybackAtMs(),qint64(0));
        QVERIFY(older.radioContext().isEmpty());
    }
    void radioContextAndPlaybackTimestampClearOnQueueAndProfileReplacement() {
        DeferredSource source;VinylBackendSession seed;seed.setProfileScope("profile-a");seed.setSource(&source);
        QVERIFY(seed.replaceRows({row("vault:a")},0,false));
        const QVariantMap context{{"kind","artist-radio"},{"seed",QVariantMap{
            {"provider","youtube-music"},{"providerId","UCedvOgsKFzcK3hA5taf3KoQ"},
            {"kind","artist"},{"name","Test artist"}}}};
        seed.setRadioContext(context);
        auto checkpoint=seed.checkpoint();checkpoint["lastPlaybackAtMs"]=1700000000123LL;

        VinylBackendSession replaced;replaced.setProfileScope("profile-a");
        QVERIFY(replaced.restoreCheckpoint(checkpoint));
        QVERIFY(replaced.replaceRows({row("vault:b")},-1,false));
        QCOMPARE(replaced.lastPlaybackAtMs(),qint64(0));
        QVERIFY(replaced.radioContext().isEmpty());

        VinylBackendSession cleared;cleared.setProfileScope("profile-a");
        QVERIFY(cleared.restoreCheckpoint(checkpoint));
        cleared.clearQueue();
        QCOMPARE(cleared.lastPlaybackAtMs(),qint64(0));
        QVERIFY(cleared.radioContext().isEmpty());

        VinylBackendSession profileChanged;profileChanged.setProfileScope("profile-a");
        QVERIFY(profileChanged.restoreCheckpoint(checkpoint));
        profileChanged.setProfileScope("profile-b");
        QCOMPARE(profileChanged.lastPlaybackAtMs(),qint64(0));
        QVERIFY(profileChanged.radioContext().isEmpty());
    }
    void cancellationAndShutdownFenceOldResolution() {
        DeferredSource source;
        VinylBackendSession session;session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a")},0,false));
        QTRY_VERIFY(source.serial>0);const auto old=source.serial;
        session.clearProfile();
        emit source.completed("play",old,QVariantMap{{"trackId","vault:a"},{"url","file:///unused.wav"}},{});
        QVERIFY(session.rows().isEmpty());QVERIFY(!session.player());
        session.shutdown();QVERIFY(!session.replaceRows({row("vault:a")},0,false));
    }
    void malformedReplacementIsAtomic() {
        VinylBackendSession session;session.setProfileScope("profile-a");
        QVERIFY(session.replaceRows({row("vault:a")},-1,false));const auto before=session.rows();
        QVERIFY(!session.replaceRows({row("")},0,false));QCOMPARE(session.rows(),before);
        auto state=session.checkpoint();auto rows=state["rows"].toList();
        auto forged=rows[0].toMap();forged["trackId"]="vault:other";rows[0]=forged;state["rows"]=rows;
        QVERIFY(!session.restoreCheckpoint(state));QCOMPARE(session.rows(),before);
    }
    void pauseDuringResolutionUpdatesNativeIntent() {
        DeferredSource source;
        VinylBackendSession session;session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a")},0,false));session.setPaused(true);
        auto profile=vinyl::ProfileScopeId::parse("profile-a");
        auto captured=session.checkpoint();
        auto restored=vinyl::VinylSessionPersistence::restore(*profile.value,captured["backendState"].toString().toUtf8());
        QVERIFY(restored.ok());QVERIFY(restored.value->coordinatorIntent.pendingInput);
        QVERIFY(!restored.value->coordinatorIntent.pendingInput->playWhenReady);
        QCOMPARE(captured["editionTable"].toList().size(),1);
    }
    void existingPersonalPortsRetainLocalCatalogueAndRejectForeignFeedback() {
        PersonalPorts ports;VinylBackendSession session;session.setProfileScope("profile-a");
        QVERIFY(session.setProperty("preferences",QVariant::fromValue<QObject *>(&ports)));
        QVERIFY(session.setProperty("collectionStore",QVariant::fromValue<QObject *>(&ports)));
        const QVariantMap album{{"id","local-album"},{"origin","local"}};
        QVERIFY(session.setSavedAlbum({{"id","local-album"},{"payload",QVariantMap{{"album",album}}}},false));
        QCOMPARE(ports.saved["payload"].toMap()["album"].toMap(),album);
        QCOMPARE(ports.saved["payload"].toMap()["saved"].toBool(),false);
        auto profile=vinyl::ProfileScopeId::parse("profile-a");
        vinyl::TasteProfileState taste(*profile.value);
        auto snapshot=vinyl::FeedbackTastePersistence::capture(taste);QVERIFY(snapshot.ok());
        QVERIFY(session.storeFeedbackSnapshot(QString::fromUtf8(*snapshot.value)));
        QVERIFY(session.feedbackState()["ok"].toBool());
        session.setProfileScope("profile-b");QVERIFY(!session.feedbackState()["ok"].toBool());
        QVERIFY(!session.storeFeedbackSnapshot(QString::fromUtf8(*snapshot.value)));
        QCOMPARE(ports.feedback,QString::fromUtf8(*snapshot.value));
    }
    void providerNativeArtistFeedbackPersistsAndRejectsInvalidSnapshotsAtomically() {
        const QString uuid = QStringLiteral("11111111-1111-4111-8111-111111111111");
        const auto provider = vinyl::ProviderId::parse(QStringLiteral("catalogue.provider"));
        const auto providerItemId = vinyl::ProviderItemId::parse(uuid);
        const auto artistMbid = vinyl::ArtistMbid::parse(uuid);
        QVERIFY(provider.ok());
        QVERIFY(providerItemId.ok());
        QVERIFY(artistMbid.ok());

        const vinyl::ProviderItemRef providerItem(*provider.value, *providerItemId.value);
        const auto providerArtist = vinyl::RecommendationEntityRef::fromProvider(
            vinyl::CanonicalEntityKind::Artist, providerItem);
        const auto canonicalArtist = vinyl::RecommendationEntityRef::fromCanonical(
            vinyl::CanonicalEntityRef::from(*artistMbid.value));
        QVERIFY(providerArtist != canonicalArtist);

        auto profile = vinyl::ProfileScopeId::parse(QStringLiteral("profile-a"));
        QVERIFY(profile.ok());
        vinyl::TasteProfileState taste(*profile.value);
        QVERIFY(taste.applyExplicitFeedback(providerArtist,
                    vinyl::ExplicitTasteFeedback::Positive, 1, 1000).ok);
        QVERIFY(taste.applyExplicitFeedback(canonicalArtist,
                    vinyl::ExplicitTasteFeedback::Negative, 1, 1000).ok);
        QCOMPARE(taste.size(), 2);

        const auto snapshot = vinyl::FeedbackTastePersistence::capture(taste);
        QVERIFY(snapshot.ok());
        const auto snapshotDocument = QJsonDocument::fromJson(*snapshot.value);
        QVERIFY(snapshotDocument.isObject());
        QCOMPARE(snapshotDocument.object().value(QStringLiteral("version")).toInt(), 3);

        PersonalPorts ports;
        VinylBackendSession session;
        session.setProfileScope(QStringLiteral("profile-a"));
        QVERIFY(session.setProperty("preferences", QVariant::fromValue<QObject *>(&ports)));
        const QString snapshotText = QString::fromUtf8(*snapshot.value);
        QVERIFY(session.storeFeedbackSnapshot(snapshotText));
        QCOMPARE(ports.feedback, snapshotText);
        QVERIFY(session.feedbackState().value(QStringLiteral("ok")).toBool());

        const auto restored = vinyl::FeedbackTastePersistence::restore(
            *profile.value, ports.feedback.toUtf8());
        QVERIFY(restored.ok());
        QCOMPARE(restored.value->size(), 2);
        const auto *restoredProviderArtist = restored.value->find(providerArtist);
        const auto *restoredCanonicalArtist = restored.value->find(canonicalArtist);
        QVERIFY(restoredProviderArtist != nullptr);
        QVERIFY(restoredCanonicalArtist != nullptr);
        QVERIFY(restoredProviderArtist != restoredCanonicalArtist);
        QVERIFY(restoredProviderArtist->explicitFeedback().feedback()
                == vinyl::ExplicitTasteFeedback::Positive);
        QVERIFY(restoredCanonicalArtist->explicitFeedback().feedback()
                == vinyl::ExplicitTasteFeedback::Negative);

        const QString savedBytes = ports.feedback;
        QJsonObject unknownVersionObject = snapshotDocument.object();
        unknownVersionObject.insert(QStringLiteral("version"), 4);
        const QString unknownVersionSnapshot = QString::fromUtf8(
            QJsonDocument(unknownVersionObject).toJson(QJsonDocument::Compact));
        QVERIFY(!session.storeFeedbackSnapshot(unknownVersionSnapshot));
        QCOMPARE(ports.feedback, savedBytes);

        auto foreignProfile = vinyl::ProfileScopeId::parse(QStringLiteral("profile-b"));
        QVERIFY(foreignProfile.ok());
        vinyl::TasteProfileState foreignTaste(*foreignProfile.value);
        QVERIFY(foreignTaste.applyExplicitFeedback(providerArtist,
                    vinyl::ExplicitTasteFeedback::Positive, 1, 1000).ok);
        const auto foreignSnapshot = vinyl::FeedbackTastePersistence::capture(foreignTaste);
        QVERIFY(foreignSnapshot.ok());
        QVERIFY(!session.storeFeedbackSnapshot(QString::fromUtf8(*foreignSnapshot.value)));
        QCOMPARE(ports.feedback, savedBytes);
    }
    void replacingAnUnresolvedOccurrenceRetiresCoordinatorIntent() {
        DeferredSource source;VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b"),row("vault:c")},0,false));
        QTRY_VERIFY(source.serial>0);const auto old=source.serial;
        session.playQueue(1);QCOMPARE(session.sourceState(),QString("finding"));QCOMPARE(session.error(),QString());
        QTRY_VERIFY(source.serial>old);const auto replaced=source.serial;
        session.removeQueue(1);QCOMPARE(session.sourceState(),QString("finding"));QCOMPARE(session.error(),QString());
        QCOMPARE(session.rows()[session.activeIndex()].toMap()["trackId"].toString(),QString("vault:c"));
        QTRY_VERIFY(source.serial>replaced);
    }
};
QTEST_MAIN(FacadeTest)
#include "test_facade.moc"
