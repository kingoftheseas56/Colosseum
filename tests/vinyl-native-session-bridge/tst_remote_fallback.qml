import QtQuick
import QtTest

// Actual native MpvItem failure and replacement, with deterministic transport
// replies. Public YouTube playback is qualified separately in Main.qml.
TestCase {
    id: test
    name: "VinylNativeRemoteFallback"
    when: windowShown
    property var session
    property var source
    property string wave
    property var observedNativeEvents: []
    property var observedSeekReplies: []
    property var loadedEntryId: null
    Connections {
        target: test.session ? test.session.player : null
        ignoreUnknownSignals: true
        function onNativePlaybackEvent(kind, entryId, position, duration, load, monotonic) {
            test.observedNativeEvents = test.observedNativeEvents.concat([
                {kind:kind,entryId:entryId,position:position,duration:duration,load:load,monotonic:monotonic}]);
            if (kind === "loaded") test.loadedEntryId = entryId;
        }
        function onTrackedSeekReply(serial, success, load) {
            test.observedSeekReplies = test.observedSeekReplies.concat([
                {serial:serial,success:success,load:load}]);
        }
    }
    Component {
        id: transport
        QtObject {
            property int serial: 0
            property var calls: []
            property string torrentUrl: "http://127.0.0.1:1/unreachable-audio.mp3"
            property string fallbackUrl: ""
            signal completed(string lane,int requestId,var data,string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function setAlbums(entries) {}
            function cancel(lane: string) {}
            function request(lane: string,operation: string,value: string): int {
                var id=++serial;
                calls=calls.concat([{operation:operation,track:value}]);
                Qt.callLater(function(){
                    completed(lane,id,{trackId:value,
                        url:operation==="resolve" ? torrentUrl : (fallbackUrl || test.wave),
                        provider:operation==="resolve" ? "torrent" : "youtube",qualityLabel:"Contract audio"},"");
                });
                return id;
            }
        }
    }
    function init() {
        observedNativeEvents=[];observedSeekReplies=[];loadedEntryId=null;
        var path=BridgeFixtures.createIdentityAudio();BridgeFixtures.changeIdentityAudio(path,16000);
        wave="file:///"+path.replace(/\\/g,"/");
        source=transport.createObject(test);
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(component.status,Component.Ready,component.errorString());
        session=component.createObject(test,{source:source});verify(session!==null);
        session.shownEdition=session.normalize({id:"remote-album",browseId:"remote-album",origin:"youtube-music",title:"Album",artists:["Artist"],tracks:[{id:"remote-track",videoId:"abcdefghijk",title:"Song",artists:["Artist"],seconds:2,available:true}]});
    }
    function cleanup(){session.destroy();wait(0);source.destroy();wait(0);}
    function test_tagsAreCheckedAgainstCanonicalQueue_data(){
        return [{tag:"matching",title:"Song",artist:"Artist",album:"Album",expected:"torrent"},
            {tag:"wrong-title",title:"Another Song",artist:"Artist",album:"Album",expected:"youtube"},
            {tag:"wrong-artist",title:"Song",artist:"Another Artist",album:"Album",expected:"youtube"},
            {tag:"wrong-album",title:"Song",artist:"Artist",album:"Another Album",expected:"youtube"}];
    }
    function test_tagsAreCheckedAgainstCanonicalQueue(data){
        source.torrentUrl="file:///"+BridgeFixtures.createTaggedAudio(data.title,data.artist,data.album).replace(/\\/g,"/");
        session.playTrack(0);session.paused=true;
        tryVerify(function(){return session.loaded;},15000);
        compare(session.error,"");compare(session.sourceKind,data.expected);
        compare(source.calls.length,data.expected==="torrent"?1:2);
    }
    function test_realDecoderFailureFallsBackOnceAndPreservesPauseAndOccurrence() {
        session.playTrack(0);session.paused=true;
        var occurrence=session.queue[0].queueId;
        tryVerify(function(){return session.loaded;},15000);
        compare(session.error,"");compare(session.sourceKind,"youtube");verify(session.fallbackReason.length>0);
        compare(source.calls.length,2);compare(source.calls[0].operation,"resolve");compare(source.calls[1].operation,"resolveFallback");
        compare(source.calls[0].track,source.calls[1].track);compare(session.queue[0].queueId,occurrence);compare(session.queueIndex,0);
        verify(session.paused);session.paused=false;
        tryVerify(function(){return session.position>0.2;},5000);
        compare(source.calls.length,2);
        session.clearProfile();compare(session.sourceKind,"");compare(session.fallbackReason,"");
    }
    function test_seekStartAndRestartBeforeAcceptedReplyCompleteSameOccurrence() {
        source.torrentUrl=wave;
        session.playTrack(0);
        tryVerify(function(){return session.loaded;},15000);
        compare(session.sourceKind,"torrent");
        tryVerify(function(){return loadedEntryId!==null;},5000);
        var queueId=session.queue[0].queueId;
        var player=session.player,load=player.playbackGeneration,entry=loadedEntryId;
        session.paused=true;
        compare(session.paused,true);
        session.seek(1.0);
        verify(session.seekPending,"The logical seek is active before native acknowledgements.");

        // Emit the decode-worker events in the same GUI turn as the seek command,
        // before the queued controller reply can reach the session.
        player.nativePlaybackEvent("seek-start",entry,1.0,2.0,load,BridgeFixtures.monotonicMs(),player.nativeObservationSerial()+1);
        player.nativePlaybackEvent("restart",entry,1.0,2.0,load,BridgeFixtures.monotonicMs(),player.nativeObservationSerial()+1);
        player.trackedSeekReply(1,true,load,player.nativeObservationSerial());

        compare(observedSeekReplies.length,1);
        compare(observedSeekReplies[0].serial,1);
        compare(observedSeekReplies[0].success,true);
        verify(!session.seekPending,
            "A seek-start/restart pair delivered before the accepted reply must still complete the seek.");
        compare(session.sourceKind,"torrent");
        compare(session.queue.length,1);
        compare(session.queue[0].queueId,queueId);
        compare(session.queueIndex,0);
        compare(session.playingTrack.id,"remote-track");
        verify(session.paused,"Acknowledgement ordering must not resume paused playback.");
        session.clearProfile();
    }
    function test_preDispatchDecodeEventsCannotCompleteNewSeek() {
        source.torrentUrl=wave;
        session.playTrack(0);session.paused=true;
        tryVerify(function(){return session.loaded&&loadedEntryId!==null;},15000);
        var player=session.player,load=player.playbackGeneration,entry=loadedEntryId;
        session.seek(1.0);
        // Even a fresh/equal-clock timestamp cannot redeem an observation
        // serial already behind the command fence. Entry/target is insufficient.
        player.nativePlaybackEvent("seek-start",entry,1.0,2.0,load,BridgeFixtures.monotonicMs(),player.nativeObservationSerial());
        player.nativePlaybackEvent("restart",entry,1.0,2.0,load,BridgeFixtures.monotonicMs(),player.nativeObservationSerial());
        player.trackedSeekReply(1,true,load,player.nativeObservationSerial());
        verify(session.seekPending,"Old decoder observations must not acknowledge a new seek.");
        session.clearProfile();
    }
    function test_controllerDrainFenceRejectsDecoderBacklogBeforeCommand() {
        source.torrentUrl=wave;
        session.playTrack(0);session.paused=true;
        tryVerify(function(){return session.loaded&&loadedEntryId!==null;},15000);
        var player=session.player,load=player.playbackGeneration,entry=loadedEntryId;
        session.seek(1.0);
        var drained=player.nativeObservationSerial()+1;
        player.nativePlaybackEvent("seek-start",entry,1.0,2.0,load,BridgeFixtures.monotonicMs(),drained);
        player.nativePlaybackEvent("restart",entry,1.0,2.0,load,BridgeFixtures.monotonicMs(),drained);
        // The controller drains old native events before issuing its command.
        // Those events may reach the GUI before the boundary reply does.
        player.trackedSeekReply(1,true,load,drained);
        verify(session.seekPending,"Native backlog before the command must not settle its seek.");
        player.nativePlaybackEvent("seek-start",entry,1.0,2.0,load,BridgeFixtures.monotonicMs(),drained+1);
        player.nativePlaybackEvent("restart",entry,1.0,2.0,load,BridgeFixtures.monotonicMs(),drained+2);
        verify(!session.seekPending);verify(session.paused);
        session.clearProfile();
    }
    function test_lateSeekReplyAfterProfileRetirementCannotRevivePlayback() {
        source.torrentUrl=wave;
        session.playTrack(0);
        session.paused=true;
        tryVerify(function(){return session.loaded;},15000);
        compare(session.sourceKind,"torrent");
        var oldPlayer=session.player,load=oldPlayer.playbackGeneration,entry=loadedEntryId;
        session.seek(1.0);
        verify(session.seekPending);

        session.clearProfile();
        verify(session.player===null);
        verify(!session.loaded);
        verify(!session.seekPending);
        compare(session.sourceState,"idle");
        compare(session.sourceKind,"");
        compare(session.queue.length,0);

        // The retired QObject is still alive until deleteLater runs. Its public
        // signals must no longer reach the cleared backend session.
        oldPlayer.nativePlaybackEvent("seek-start",entry,1.0,2.0,load,0,oldPlayer.nativeObservationSerial());
        oldPlayer.nativePlaybackEvent("restart",entry,1.0,2.0,load,0,oldPlayer.nativeObservationSerial());
        oldPlayer.trackedSeekReply(1,true,load,oldPlayer.nativeObservationSerial());
        verify(session.player===null);
        verify(!session.loaded);
        verify(!session.seekPending);
        compare(session.sourceState,"idle");
        compare(session.sourceKind,"");
        compare(session.queue.length,0);
        compare(session.error,"");
    }
    function test_seekRejectionFallsBackWithSameRecordingOccurrenceAndPause() {
        source.torrentUrl=wave;
        session.playTrack(0);
        session.paused=true;
        tryVerify(function(){return session.loaded;},15000);
        compare(session.sourceKind,"torrent");
        compare(session.paused,true);
        var occurrence=session.queue[0].queueId;
        var player=session.player,load=player.playbackGeneration;
        session.seek(1.0);
        verify(session.seekPending);
        player.trackedSeekReply(1,false,load,player.nativeObservationSerial());

        tryVerify(function(){return session.loaded && session.sourceKind==="youtube";},15000);
        compare(session.error,"");
        compare(session.fallbackReason,"Torrent audio seek failed.");
        compare(source.calls.length,2);
        compare(source.calls[0].operation,"resolve");
        compare(source.calls[1].operation,"resolveFallback");
        compare(source.calls[0].track,"remote-track");
        compare(source.calls[1].track,"remote-track");
        compare(session.queue.length,1);
        compare(session.queue[0].queueId,occurrence);
        compare(session.queueIndex,0);
        compare(session.playingTrack.id,"remote-track");
        verify(session.paused,"Seek fallback must keep the original paused intent.");
    }
    function test_failedFallbackDoesNotLoop(){
        source.fallbackUrl=source.torrentUrl;
        session.playTrack(0);
        tryVerify(function(){return session.error.length>0;},15000);
        compare(source.calls.length,2);compare(session.sourceKind,"youtube");
        wait(400);compare(source.calls.length,2);
    }
}
