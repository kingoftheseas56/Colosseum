import QtQuick
import QtTest
TestCase {
    id:test
    name:"VinylLifecyclePersistenceModel"
    when:windowShown
    property var session
    property var source
    property var progress
    Component {id:sourceFactory;QtObject {
        property int serial:0
        signal completed(string lane,int requestId,var data,string error)
        signal profileLeaving()
        signal profileEntered()
        signal checkpointRequested()
        signal shuttingDown()
        function request(lane,operation,value) {return ++serial;}
        function cancel(lane) {}
        function setAlbums(entries) {}
    }}
    Component {id:storeFactory;QtObject {
        property var entries:({})
        property bool healthy:true
        property string persistenceError:""
        property bool keepActivityHistory:true
        signal resetCommitted(int generation,double atMs)
        function newSessionId() {return "synthetic-session";}
        function recordSilent(entry) {if(healthy) {var next=Object.assign({},entries);next[entry.kind+"/"+entry.id]=JSON.parse(JSON.stringify(entry));entries=next;}}
        function get(kind,id) {return entries[kind+"/"+id] || {};}
        function recent(kind,count) {return Object.keys(entries).map(k=>entries[k]).filter(e=>e.kind===kind).reverse().slice(0,count);}
    }}
    function album() {return {id:"synthetic-album",title:"Synthetic",origin:"local",tracks:[{id:"one",title:"One",seconds:60},{id:"two",title:"Two",seconds:60}]};}
    function createSession() {var c=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");compare(c.status,Component.Ready,c.errorString());session=c.createObject(test,{source:source,progressStore:progress,collectionStore:null});verify(session!==null);}
    function init() {source=sourceFactory.createObject(test);progress=storeFactory.createObject(test);createSession();}
    function cleanup() {session.destroy();wait(0);source.destroy();progress.destroy();wait(0);}
    function restart() {session.destroy();wait(0);createSession();}
    function play() {session.playAlbum(album(),0);source.completed("play",session.playRequest,{trackId:"one",url:"file:///synthetic.wav"},"");session.player.fileLoaded();session.player.position=23;}
    function test_activityUsesDecodedIdentityAndStopsOnOptOut() {
        var component=Qt.createComponent("imports/Colosseum/Activity/ActivityPlaybackTracker.qml");compare(component.status,Component.Ready,component.errorString());
        session.activityRecorder=component.createObject(session);session.activitySink=progress;session.activityPreferences=progress;
        play();var tracker=session.activityRecorder;verify(tracker!==null);
        var calls=tracker.calls;var begins=calls.filter(c=>c.method==="begin");compare(begins.length,1);
        compare(begins[0].args[0].world,"vinyl");compare(begins[0].args[0].kind,"music");verify(!begins[0].args[0].syncable);
        compare(begins[0].args[0].titleKey,"synthetic-album");
        progress.resetCommitted(1,0);verify(!session.activityActive);wait(0);verify(session.activityActive);
        compare(tracker.calls.filter(c=>c.method==="begin").length,2);
        session.seek(40);verify(tracker.calls.some(c=>c.method==="discontinuity" && c.args[0]===40000));
        session.paused=true;verify(tracker.calls.some(c=>c.method==="state" && c.args[0]===false));
        progress.keepActivityHistory=false;compare(session.activityActive,false);compare(findChild(session,"vinylActivityTracker").sink,null);
        var count=tracker.calls.length;session.activitySample();compare(tracker.calls.length,count);
        progress.keepActivityHistory=true;wait(0);verify(session.activityActive);session.player.endFile("eof");
        compare(tracker.calls.filter(c=>c.method==="eof").length,1);
    }
    function test_absentActivitySinkDoesNotBreakPlayback() {play();session.activitySample();verify(session.loaded);session.seek(31);compare(session.position,31);}
    function test_corruptSingletonDoesNotFallbackToOldHistoryOrMutate() {
        play();session.snapshot();var entry=JSON.parse(JSON.stringify(progress.get("music-session","vinyl")));
        entry.resume.editionTable[0].discs[0][0]="malformed";
        progress.recordSilent(entry);restart();compare(session.queue.length,0);compare(session.player,null);
    }
    function test_emptyQueueKeepsRepeatAndShufflePolicy() {play();session.cycleRepeat();session.setShuffle(true);session.clearQueue();restart();compare(session.repeatMode,"all");verify(session.shuffleEnabled);compare(session.queue.length,0);}
    function test_profileStoresStaySeparateDuringPendingWork() {
        play();session.snapshot();source.profileLeaving();var first=progress.entries;
        progress.entries={};source.profileEntered();compare(session.queue.length,0);compare(session.player,null);
        session.playAlbum(album(),1);var pending=session.playRequest;source.profileLeaving();var second=progress.entries;
        progress.entries=first;source.profileEntered();source.completed("play",pending,{trackId:"two",url:"file:///late.wav"},"");
        compare(session.playingTrack.id,"one");compare(session.position,23);verify(session.paused);compare(session.player,null);
        compare(second["music-session/vinyl"].resume.trackId,"two");
    }
    function test_profileProgressObjectRebindingDoesNotWriteOldQueueToNewOwner() {
        play();source.profileLeaving();var old=progress, saved=JSON.stringify(old.entries);
        progress=storeFactory.createObject(test);session.progressStore=progress;
        session.snapshot();compare(Object.keys(progress.entries).length,0);
        source.profileEntered();compare(session.queue.length,0);verify(session.paused);compare(session.player,null);
        session.playAlbum(album(),1);source.profileLeaving();
        compare(JSON.stringify(old.entries),saved);compare(progress.get("music-session","vinyl").resume.trackId,"two");
        session.progressStore=old;source.profileEntered();compare(session.playingTrack.id,"one");compare(session.position,23);
        session.progressStore=progress;old.destroy();
    }
    function test_clearQueueSurvivesRestartWithoutHistoryResurrection() {play();session.clearQueue();restart();compare(session.queue.length,0);compare(session.playingIndex,-1);verify(session.paused);compare(session.player,null);}
    function test_unplayedQueueSurvivesRestartPaused() {session.shownEdition=session.normalize(album());session.enqueueTrack(1);restart();compare(session.queue.length,1);compare(session.queue[0].trackId,"two");compare(session.queueIndex,-1);verify(session.paused);compare(session.player,null);}
    function test_suspendPendingResolutionCannotStartAudio() {session.playAlbum(album(),0);var request=session.playRequest;session.suspendPlayback("other-world");source.completed("play",request,{trackId:"one",url:"file:///late.wav"},"");verify(session.paused);compare(session.player,null);compare(session.queue.length,2);}
    function test_suspendPreservesClockAndRestoresWithoutAutoplay() {play();var old=session.player;session.suspendPlayback("other-world");compare(session.position,23);verify(old.pause);compare(session.player,null);restart();compare(session.position,23);compare(session.queueIndex,0);verify(session.paused);compare(session.player,null);}
    function test_departedProfileRejectsNewWorkAndLateReplies() {play();source.profileLeaving();var serial=source.serial;session.playAlbum(album(),0);session.openAlbum("late");session.enqueueAlbum("late");compare(source.serial,serial);compare(session.queue.length,0);compare(session.player,null);source.profileEntered();session.playAlbum(album(),0);verify(source.serial>serial);}
    function test_failedCheckpointIsVisibleAndLastGoodSurvives() {play();session.snapshot();progress.healthy=false;progress.persistenceError="Synthetic write failure";session.player.position=41;session.snapshot();compare(session.persistenceError,"Synthetic write failure");restart();compare(session.position,23);verify(session.paused);}
    // Component-only: fake resolver/player/store, not native media or disk isolation proof.
    function test_allPendingLanesRejectRepliesAcrossProfileRoundTrip() {
        play();session.playAlbum(album(),1);
        session.openAlbum("synthetic-album");session.enqueueAlbum("synthetic-album");
        var pending={browse:session.browseRequest,queue:session.queueRequest,play:session.playRequest};
        verify(pending.browse>=0);verify(pending.queue>=0);verify(pending.play>=0);
        source.profileLeaving();var first=progress.entries;
        progress.entries={};source.profileEntered();
        var secondBefore=JSON.stringify(progress.entries), serial=source.serial;
        function deliverStale() {
            source.completed("browse",pending.browse,album(),"");
            source.completed("queue",pending.queue,album(),"");
            source.completed("play",pending.play,{trackId:"two",url:"file:///late.wav"},"");
            source.completed("browse",pending.browse,null,"late browse failure");
            source.completed("queue",pending.queue,null,"late queue failure");
            source.completed("play",pending.play,null,"late play failure");
        }
        deliverStale();wait(0);
        compare(session.queue.length,0);compare(session.player,null);verify(session.paused);
        compare(session.shownAlbumId,"");compare(session.browseError,"");compare(session.error,"");
        compare(JSON.stringify(progress.entries),secondBefore);compare(source.serial,serial);
        source.profileLeaving();progress.entries=first;source.profileEntered();
        var restored=JSON.stringify(progress.entries), queue=JSON.stringify(session.queue);
        deliverStale();wait(0);
        compare(session.playingTrack.id,"two");compare(JSON.stringify(session.queue),queue);
        compare(JSON.stringify(progress.entries),restored);compare(session.player,null);verify(session.paused);
        compare(source.serial,serial);
    }
    function test_musicCheckpointAndClearPreserveOtherMediaRecords() {
        var video={kind:"video",id:"theatre-sentinel",resume:{position:17,duration:91}};
        var book={kind:"book",id:"biblio-sentinel",resume:{position:29,duration:183}};
        progress.recordSilent(video);progress.recordSilent(book);
        play();session.snapshot();session.clearQueue();restart();
        compare(JSON.stringify(progress.get("video","theatre-sentinel")),JSON.stringify(video));
        compare(JSON.stringify(progress.get("book","biblio-sentinel")),JSON.stringify(book));
        compare(session.queue.length,0);compare(session.player,null);verify(session.paused);
        compare(progress.get("music-session","vinyl").resume.queue.length,0);
    }

}
