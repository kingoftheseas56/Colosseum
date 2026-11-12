import QtQuick
import QtTest
import Colosseum.Vinyl 1.0

TestCase {
    id: test
    name: "VinylExplicitTorrent"
    when: windowShown
    property var session
    property var source
    property var pending
    property var calls
    property var videoRequest
    property string audioProvider
    property string audioUrl
    Component { id: sourceFactory; VideoTestSource {} }
    function init() {
        pending=null;calls=[];videoRequest=null;audioProvider="youtube";
        var path=BridgeFixtures.createIdentityAudio();BridgeFixtures.changeIdentityAudio(path,120*8000);
        audioUrl="file:///"+path.replace(/\\/g,"/");
        source=sourceFactory.createObject(test);
        source.requested.connect(function(lane,id,operation,value,options) {
            if(lane==="video-match")Qt.callLater(function(){source.reply(lane,id,{
                version:1,status:"ready",trackId:value,sourceVideoId:"abcdefghijk",videoId:"YVkUvmDQ3HY"},"");});
            if(lane==="video-stream")videoRequest={lane:lane,id:id,trackId:value};
            if(lane!=="play")return;
            calls.push({operation:operation,trackId:value,options:options});
            if(options.source==="torrent")pending={lane:lane,id:id,trackId:value};
            else Qt.callLater(function(){source.reply(lane,id,{trackId:value,url:audioUrl,provider:audioProvider},"");});
        });
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(component.status,Component.Ready,component.errorString());
        session=component.createObject(test,{source:source,collectionStore:null,progressStore:null});
        session.shownEdition=session.normalize({id:"remote",origin:"youtube-music",title:"Album",artists:["Artist"],tracks:[{id:"remote-track",videoId:"abcdefghijk",title:"Song",artists:["Artist"],seconds:120,available:true}]});
        session.playTrack(0);tryCompare(session,"loaded",true);verify(session.torrentAvailable);
    }
    function cleanup(){session.destroy();source.destroy();wait(0);}
    function reply(provider,error) { source.reply(pending.lane,pending.id,{trackId:pending.trackId,url:audioUrl,provider:provider},error || ""); }
    function test_select_youtube_cancels_pending_and_fences_reply_data() {
        return [{tag:"queued",queued:true},{tag:"late",queued:false}];
    }
    function test_select_youtube_cancels_pending_and_fences_reply(data) {
        session.paused=true;session.seek(24);tryCompare(session,"seekPending",false);
        var player=session.player,occurrence=session.queue[0].queueId;
        session.findTorrent();verify(session.torrentBusy);
        if(data.queued)reply("torrent");
        session.selectYouTube();verify(!session.torrentBusy);
        if(!data.queued)reply("torrent");
        wait(50);compare(session.player,player);verify(session.loaded);verify(session.paused);
        compare(session.sourceKind,"youtube");compare(calls.length,2);
        compare(session.queue[0].queueId,occurrence);verify(Math.abs(session.position-24)<0.3);
    }
    function test_select_current_youtube_is_noop() {
        session.paused=true;
        var player=session.player,count=calls.length;
        session.selectYouTube();session.selectYouTube();wait(50);
        compare(session.player,player);compare(calls.length,count);verify(session.paused);verify(session.loaded);
    }
    function test_select_youtube_from_torrent_preserves_playback_data() {
        return [{tag:"paused",paused:true},{tag:"playing",paused:false}];
    }
    function test_select_youtube_from_torrent_preserves_playback(data) {
        session.paused=true;session.findTorrent();reply("torrent");
        tryCompare(session,"sourceKind","torrent");tryCompare(session,"loaded",true);
        session.seek(24);tryCompare(session,"seekPending",false);
        session.paused=data.paused;
        var occurrence=session.queue[0].queueId,at=session.position,count=calls.length;
        session.selectYouTube();session.selectYouTube();
        tryCompare(session,"sourceKind","youtube");tryCompare(session,"loaded",true);
        tryCompare(session,"seekPending",false);
        compare(calls.length,count+1);compare(calls[count].operation,"resolveFallback");
        compare(calls[count].trackId,calls[0].trackId);
        compare(session.queue[0].queueId,occurrence);compare(session.queueIndex,0);
        compare(session.paused,data.paused);verify(Math.abs(session.position-at)<(data.paused ? 0.3 : 1));
        compare(session.fallbackReason,"");compare(session.error,"");
    }
    function test_select_youtube_local_is_noop() {
        audioProvider="local";
        var rows=session.plainData(session.queue);rows[0].edition.origin="local";
        var backend=findChild(session,"vinylBackendSession");verify(backend!==null);
        verify(backend.replaceRows(rows,0,false));tryCompare(session,"loaded",true);
        compare(session.sourceKind,"local");session.paused=true;
        var player=session.player,count=calls.length;
        session.selectYouTube();wait(50);
        compare(session.player,player);compare(calls.length,count);verify(session.paused);
    }
    function test_select_youtube_after_retirement_is_noop_data() {
        return [{tag:"profile",action:"clearProfile"},{tag:"shutdown",action:"shutdown"},{tag:"queue",action:"clearQueue"}];
    }
    function test_select_youtube_after_retirement_is_noop(data) {
        var backend=findChild(session,"vinylBackendSession");verify(backend!==null);
        backend[data.action]();var count=calls.length;
        session.selectYouTube();wait(50);compare(calls.length,count);verify(!session.loaded);
    }
    function test_select_youtube_during_video_is_noop_data() {
        return [{tag:"resolving",commit:false},{tag:"loading",commit:true}];
    }
    function test_select_youtube_during_video_is_noop(data) {
        session.paused=true;session.findTorrent();reply("torrent");
        tryCompare(session,"sourceKind","torrent");tryCompare(session,"loaded",true);
        tryCompare(session,"hasVideo",true);session.toggleVideo();verify(session.videoBusy);
        verify(videoRequest!==null);
        if(data.commit){
            source.reply(videoRequest.lane,videoRequest.id,{version:1,trackId:videoRequest.trackId,
                videoId:"YVkUvmDQ3HY",url:"https://media.googlevideo.com/videoplayback",hasVideo:true,hasAudio:true},"");
            tryCompare(session,"videoShown",true);
        }
        var player=session.player,count=calls.length;
        session.selectYouTube();wait(50);
        compare(session.player,player);compare(calls.length,count);verify(session.videoBusy);verify(session.paused);
    }
    function test_pending_cancel_and_late_reply_leave_audio_running() {
        var player=session.player;
        session.findTorrent();verify(session.torrentBusy);verify(session.loaded);compare(session.player,player);
        var at=session.position;tryVerify(function(){return session.position>at+0.2;});
        session.toggleVideo();verify(!session.videoBusy);
        session.cancelTorrent();verify(!session.torrentBusy);reply("torrent");wait(50);
        compare(session.player,player);verify(session.loaded);compare(session.sourceKind,"youtube");
    }
    function test_no_result_is_nonfatal_data(){return [{tag:"fallback",provider:"youtube",error:""},{tag:"failure",provider:"",error:"No exact match"}];}
    function test_no_result_is_nonfatal(data) {
        var player=session.player;session.findTorrent();reply(data.provider,data.error);
        tryCompare(session,"torrentBusy",false);compare(session.player,player);verify(session.loaded);
        compare(session.error,"");verify(session.torrentError.length>0);
    }
    function test_commit_uses_latest_seek_pause_and_same_occurrence() {
        var occurrence=session.queue[0].queueId;session.findTorrent();
        session.paused=true;session.seek(24);tryCompare(session,"seekPending",false);
        reply("torrent");tryCompare(session,"sourceKind","torrent");tryCompare(session,"loaded",true);
        tryCompare(session,"seekPending",false);verify(session.paused);verify(Math.abs(session.position-24)<0.3);
        compare(session.queue[0].queueId,occurrence);compare(session.error,"");
    }
    function test_harmless_queue_edits_preserve_pending_lookup() {
        var backend=findChild(session,"vinylBackendSession");verify(backend!==null);
        var occurrence=session.queue[0].queueId,player=session.player;
        session.findTorrent();verify(session.torrentBusy);
        verify(backend.appendRows([session.queue[0],session.queue[0]],false));
        verify(session.torrentBusy);backend.moveQueue(2,1);verify(session.torrentBusy);
        backend.setShuffle(true);verify(session.torrentBusy);
        backend.setShuffle(false);verify(session.torrentBusy);
        backend.removeQueue(session.queueIndex===0 ? 1 : 0);verify(session.torrentBusy);
        compare(session.player,player);verify(session.loaded);
        compare(session.queue[session.queueIndex].queueId,occurrence);
        session.paused=true;reply("torrent");
        tryCompare(session,"sourceKind","torrent");tryCompare(session,"loaded",true);
        compare(session.queue[session.queueIndex].queueId,occurrence);verify(session.paused);
    }
    function test_skip_cancels_and_next_song_does_not_request_torrent() {
        var backend=findChild(session,"vinylBackendSession");verify(backend!==null);
        verify(backend.appendRows([session.queue[0]],false));
        session.findTorrent();var old=pending;backend.playQueue(1);
        tryCompare(session,"loaded",true);verify(!session.torrentBusy);compare(session.sourceKind,"youtube");
        source.reply(old.lane,old.id,{trackId:old.trackId,url:audioUrl,provider:"torrent"},"");
        wait(50);compare(session.sourceKind,"youtube");compare(session.queueIndex,1);
    }
    function test_torrent_seek_failure_still_falls_back() {
        session.paused=true;session.findTorrent();reply("torrent");
        tryCompare(session,"sourceKind","torrent");tryCompare(session,"loaded",true);
        var player=session.player;session.seek(24);verify(session.seekPending);
        player.trackedSeekReply(1,false,player.playbackGeneration,player.nativeObservationSerial());
        tryCompare(session,"sourceKind","youtube");tryCompare(session,"loaded",true);
        verify(session.paused);compare(session.error,"");verify(session.fallbackReason.length>0);
    }
    function test_source_replacement_rejects_old_completion() {
        session.findTorrent();var old=source,request=pending;
        source=sourceFactory.createObject(test);session.source=source;
        old.reply(request.lane,request.id,{trackId:request.trackId,url:audioUrl,provider:"torrent"},"");
        wait(50);verify(!session.torrentBusy);verify(!session.loaded);old.destroy();
    }
    function test_torrent_startup_failure_returns_to_youtube() {
        session.paused=true;session.findTorrent();
        source.reply(pending.lane,pending.id,{trackId:pending.trackId,url:"http://127.0.0.1:1/unreachable.wav",provider:"torrent"},"");
        tryCompare(session,"torrentBusy",false);
        tryVerify(function(){return session.loaded&&session.sourceKind==="youtube"&&session.fallbackReason.length>0;},15000);
        compare(session.error,"");verify(session.paused);
    }
    function test_profile_retirement_rejects_pending_data(){return [{tag:"queued",queued:true},{tag:"late",queued:false}];}
    function test_profile_retirement_rejects_pending(data) {
        session.findTorrent();if(data.queued)reply("torrent");
        session.clearProfile();if(!data.queued)reply("torrent");wait(50);
        verify(!session.loaded);verify(!session.torrentBusy);compare(session.queue.length,0);
    }
}
