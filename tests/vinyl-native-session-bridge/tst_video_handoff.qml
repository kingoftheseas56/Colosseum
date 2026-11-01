import QtQuick
import QtTest
import Colosseum.Vinyl 1.0

TestCase {
    id: test
    name: "VinylNativeVideoHandoff"
    when: windowShown
    width: 960; height: 640
    visible: true
    property var session
    property var source
    property var videoRequest
    property string audioUrl
    Component { id: sourceFactory; VideoTestSource {} }
    Component { id: generationSpyFactory; SignalSpy {} }
    function init() {
        videoRequest=null;
        var imported=NativeSource.importFiles([ManifestPath]);compare(imported.errors.length,0);
        source=sourceFactory.createObject(test);
        source.requested.connect(function(lane,id,operation,value,options) {
            if(lane==="play")Qt.callLater(function(){source.reply(lane,id,{trackId:value,url:audioUrl,provider:"local"},"");});
            if(lane==="video-match")Qt.callLater(function(){source.reply(lane,id,{
                version:1,status:"ready",trackId:value,sourceVideoId:"tqxRidAWER8",videoId:"YVkUvmDQ3HY"},"");});
            if(lane==="video-stream")videoRequest={lane:lane,id:id,trackId:value,operation:operation};
        });
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(component.status,Component.Ready,component.errorString());
        session=component.createObject(test,{source:source,collectionStore:null,progressStore:null});
        var album=session.plainData(imported.albums[0]);
        function identify(track){track.videoId="tqxRidAWER8";track.artistIds=["ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ"];}
        album.tracks.forEach(identify);album.editions.forEach(function(e){e.tracks.forEach(identify);});
        var path=BridgeFixtures.createIdentityAudio();BridgeFixtures.changeIdentityAudio(path,120*8000);
        audioUrl="file:///"+path.replace(/\\/g,"/");
        session.shownEdition=session.normalize(album);session.playTrack(0);
        tryCompare(session,"loaded",true);session.paused=true;
        tryCompare(session,"hasVideo",true);
        verify(!session.videoSwitchCancellable);
    }
    function cleanup(){session.destroy();source.destroy();wait(0);}
    function beginVideo(paused) {
        var oldPlayer=session.player,occurrence=session.queue[session.queueIndex].queueId;
        session.toggleVideo();verify(session.videoBusy);verify(videoRequest!==null);
        verify(session.videoSwitchCancellable);
        compare(session.player,oldPlayer);verify(session.loaded);verify(!session.videoShown);
        compare(session.queue[session.queueIndex].queueId,occurrence);
        compare(session.paused,paused===undefined ? true : paused);
        return oldPlayer;
    }
    function test_failed_resolution_keeps_current_audio_and_pause() {
        var oldPlayer=beginVideo();
        source.reply(videoRequest.lane,videoRequest.id,{},"Not available");
        tryCompare(session,"videoBusy",false);compare(session.player,oldPlayer);
        verify(!session.videoSwitchCancellable);
        verify(session.loaded);verify(session.paused);verify(!session.videoShown);verify(session.videoError.length>0);
    }
    function test_cancel_then_late_result_cannot_replace_audio() {
        var oldPlayer=beginVideo(),pending=videoRequest;
        session.toggleVideo();verify(!session.videoBusy);
        verify(!session.videoSwitchCancellable);
        source.reply(pending.lane,pending.id,{version:1,trackId:pending.trackId,videoId:"YVkUvmDQ3HY",
            url:audioUrl,hasVideo:true,hasAudio:true},"");
        wait(50);compare(session.player,oldPlayer);verify(session.loaded);verify(!session.videoShown);
    }
    function test_video_loading_can_return_to_original_audio_data() {
        return [{tag:"paused-before-renderer",readyQueued:false,paused:true},
                {tag:"paused-late-ready",readyQueued:true,paused:true},
                {tag:"playing-before-renderer",readyQueued:false,paused:false},
                {tag:"playing-late-ready",readyQueued:true,paused:false}];
    }
    function test_video_loading_can_return_to_original_audio(data) {
        session.player.volume=37;session.player.mute=true;session.player.speed=1.25;
        tryCompare(session.player,"volume",37);tryCompare(session.player,"mute",true);tryCompare(session.player,"speed",1.25);
        session.seek(24);tryCompare(session,"seekPending",false);
        tryVerify(function(){return Math.abs(session.position-24)<0.2;});
        session.paused=data.paused;tryCompare(session.player,"pause",data.paused);
        var occurrence=session.queue[session.queueIndex].queueId,at=session.position;
        beginVideo(data.paused);
        source.reply(videoRequest.lane,videoRequest.id,{version:1,trackId:videoRequest.trackId,
            videoId:"YVkUvmDQ3HY",url:"https://media.googlevideo.com/videoplayback",
            headers:{},hasVideo:true,hasAudio:true},"");
        tryCompare(session,"videoShown",true);verify(session.videoBusy);verify(!session.loaded);
        verify(session.videoSwitchCancellable);
        // No video surface is supplied: the renderer has not opened this URL.
        var loading=session.player;compare(loading.playbackGeneration,0);
        var generationSpy=generationSpyFactory.createObject(test,{target:loading,signalName:"playbackGenerationChanged"});
        verify(generationSpy.valid);
        if(data.readyQueued)loading.ready();
        session.toggleVideo();
        tryCompare(session,"loaded",true);tryCompare(session,"seekPending",false);
        verify(!session.videoShown);verify(!session.videoBusy);verify(!session.videoSwitchCancellable);
        compare(session.paused,data.paused);compare(session.videoError,"");
        compare(session.queue[session.queueIndex].queueId,occurrence);
        verify(Math.abs(session.position-at)<(data.paused ? 0.2 : 1));
        tryCompare(session.player,"volume",37);tryCompare(session.player,"mute",true);tryCompare(session.player,"speed",1.25);
        var audio=session.player;
        wait(50);compare(session.player,audio);verify(session.loaded);verify(!session.videoShown);
        compare(session.player.volume,37);compare(session.player.mute,true);compare(session.player.speed,1.25);
        compare(generationSpy.count,0,"A retired video must never open after audio is restored");
        generationSpy.destroy();
    }
    function test_untrusted_video_resource_data() {
        return [{tag:"local-file",url:"file:///C:/untrusted.mp4",videoId:"YVkUvmDQ3HY"},
                {tag:"other-host",url:"https://example.com/video.mp4",videoId:"YVkUvmDQ3HY"},
                {tag:"wrong-id",url:"https://media.googlevideo.com/videoplayback",videoId:"abcdefghijk"},
                {tag:"credentials",url:"https://u:p@media.googlevideo.com/videoplayback",videoId:"YVkUvmDQ3HY"},
                {tag:"expired",url:"https://media.googlevideo.com/videoplayback?expire=1",videoId:"YVkUvmDQ3HY"}];
    }
    function test_untrusted_video_resource(data) {
        var oldPlayer=beginVideo();
        source.reply(videoRequest.lane,videoRequest.id,{version:1,trackId:videoRequest.trackId,videoId:data.videoId,
            url:data.url,headers:{},hasVideo:true,hasAudio:true},"");
        tryCompare(session,"videoBusy",false);compare(session.player,oldPlayer);verify(!session.videoShown);
    }
    function test_profile_departure_fences_pending_video() {
        beginVideo();var pending=videoRequest;
        session.clearProfile();compare(session.queue.length,0);
        source.reply(pending.lane,pending.id,{version:1,trackId:pending.trackId,videoId:"YVkUvmDQ3HY",
            url:audioUrl,hasVideo:true,hasAudio:true},"");
        wait(50);verify(!session.videoShown);verify(!session.hasVideo);verify(!session.loaded);compare(session.queue.length,0);
    }
    function test_skip_fences_pending_video_even_for_same_recording() {
        beginVideo();var pending=videoRequest,oldOccurrence=session.queue[session.queueIndex].queueId;
        var backend=findChild(session,"vinylBackendSession");verify(backend!==null);
        verify(backend.appendRows([session.queue[0]],false));
        session.next(1);tryCompare(session,"loaded",true);session.paused=true;
        verify(session.queue[session.queueIndex].queueId!==oldOccurrence);
        var player=session.player;
        source.reply(pending.lane,pending.id,{version:1,trackId:pending.trackId,videoId:"YVkUvmDQ3HY",
            url:audioUrl,hasVideo:true,hasAudio:true},"");
        wait(50);compare(session.player,player);verify(!session.videoShown);verify(!session.videoBusy);
    }
}
