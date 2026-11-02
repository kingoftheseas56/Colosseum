import QtQuick
import "../../qml"

// Real Main/QML, Vinyl catalogue, native session and libmpv, no substituted provider.
// Args: phase, evidence-directory. Uses task-local guest/onboarding profile.
Main {
    id: root
    readonly property string phase: Qt.application.arguments[2] || "before"
    readonly property string evidence: Qt.application.arguments[3] || ""
    property string stage: "profile"
    property int ticks: 0
    property int stageTick: 0
    property real stageAt: Date.now()
    property var names: ["Nas", "Eminem", "Radiohead", "Adele", "The Weeknd"]
    property int nextArtist: 0
    property real albumStartMs: 0
    property real audioStartMs: 0
    property real audioLoadedMs: 0
    property real audioOutputMs: 0
    property real videoStartMs: 0
    property real videoLoadedMs: 0
    property real secondAudioStartMs: 0
    property real secondVideoStartMs: 0
    property int audioTrackIndex: -1
    property string audioTrackId: ""
    property string audioOccurrence: ""
    property int cachedStalls: 0
    property int sampledTicks: 0
    property bool screenshotPending: false
    function record(action, values) {
        console.log("VINYL_PERF", JSON.stringify(Object.assign({
            phase: phase, stage: stage, event: action, timeMs: Date.now()
        }, values || {})));
    }
    function fail(message) {
        record("FAIL", {reason: String(message).slice(0, 240)});
        tick.stop();
        Qt.exit(29);
    }
    function next(state) {
        stage=state;stageAt=Date.now();stageTick=ticks;
        record("STAGE");
    }
    function capture(name, destination) {
        screenshotPending=true;
        if (!contentItem.grabToImage(function(result) {
            if (!result.saveToFile(evidence + "/" + phase + "-" + name + ".png")) {
                root.fail("screenshot-save");return;
            }
            root.record("SCREENSHOT", {name:name});
            root.screenshotPending=false;root.next(destination);
        })) fail("screenshot-request");
    }
    Timer {
        id: tick
        running: true
        repeat: true
        interval: 200
        onTriggered: {
            root.ticks++;
            if(root.ticks>1800) {root.fail("global-timeout");return;}
            if(root.stage!=="profile" && Date.now()-root.stageAt>45000 && root.stage!=="sustain") {
                root.fail("stage-timeout-" + root.stage);return;
            }
            if(root.stage==="profile") {
                if(AccountController.busy) return;
                if(AccountController.onboardingRequired) {
                    AccountController.continueWithoutAccount();return;
                }
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;
            if(!w || !w.session)return;
            var s=w.session, recommendations=w.recommendationService;
            if(s.error) {root.fail("native-session-"+s.error);return;}
            if(root.stage==="world") {
                if(w.canEnterVinyl) {root.next("open-album");return;}
                if(recommendations.onboardingState!=="draft"){
                    if(root.ticks-root.stageTick>40)root.fail("onboarding-"+recommendations.onboardingState);
                    return;
                }
                w.worldData.setArtistSearchQuery(root.names[0]);
                root.record("ARTIST_SEARCH_INTENT",{name:root.names[0]});
                root.next("artists");return;
            }
            if(root.stage==="artists") {
                var state=recommendations.artistResults;
                if(state.status==="error"){root.fail("artist-search-provider-"+root.names[root.nextArtist]);return;}
                if(state.status!=="ready")return;
                var artist=root.names[root.nextArtist];
                var rows=state.items||[];
                var match=rows.find(function(a) {return String(a.name||"").toLowerCase()===artist.toLowerCase() && a.provider==="youtube-music";});
                if(!match) {root.fail("real-artist-not-found-"+artist);return;}
                root.record("ARTIST_SEARCH_READY",{name:artist,elapsedMs:Date.now()-root.stageAt,results:rows.length});
                w.worldData.togglePickerArtist(match);
                root.nextArtist++;
                if(root.nextArtist < root.names.length) {
                    var upcoming=root.names[root.nextArtist];
                    w.worldData.setArtistSearchQuery(upcoming);
                    root.next("artists");return;
                }
                root.record("ARTISTS_CHOSEN",{count:recommendations.draftArtistCount});
                if(!w.worldData.answerPicker()){root.fail("save-taste");return;}
                root.next("open-album");return;
            }
            if(root.stage==="open-album") {
                if(!w.canEnterVinyl) {root.fail("taste-not-active");return;}
                w.showLibrary();
                root.albumStartMs=Date.now();
                w.openAlbum("MPREb_nVOvQM1ODKM");
                root.next("album");return;
            }
            if(root.stage==="album") {
                if(s.browsing)return;
                if(s.browseError){root.fail("album-provider-"+s.browseError);return;}
                var tracks=s.tracks||[];
                if(tracks.length<2) {root.fail("album-no-tracks");return;}
                var index=tracks.findIndex(function(t) {return t.title==="Without Me";});
                if(index<0) {root.fail("without-me-not-found");return;}
                root.audioTrackIndex=index;
                root.audioTrackId=tracks[index].id;
                root.record("ALBUM_READY",{elapsedMs:Date.now()-root.albumStartMs,trackCount:tracks.length});
                root.audioStartMs=Date.now();
                s.playTrack(index);
                if(w.showSong)w.showSong();
                root.audioOccurrence=s.queue[s.queueIndex].queueId;
                root.next("audio");return;
            }
            if(root.stage==="audio" || root.stage==="audio-second") {
                if(s.loaded && !root.audioLoadedMs){
                    root.audioLoadedMs=Date.now();
                    root.record("AUDIO_LOADED",{elapsedMs:root.audioLoadedMs-root.audioStartMs,
                        source:s.sourceKind,quality:s.qualityLabel});
                }
                if(!s.loaded || !s.player)return;
                var ao=String(s.player.getProperty("current-ao")||"");
                var codec=String(s.player.mpvProperty("audio-codec")||"");
                var channels=Number(s.player.mpvProperty("audio-params/channel-count")||0);
                if(ao==="null" || !ao || !codec || channels<1 || s.position<0.7)return;
                root.audioOutputMs=Date.now();
                root.record("AUDIO_DECODED_OUTPUT",{elapsedMs:root.audioOutputMs-root.audioStartMs,ao:ao,
                    codec:codec,channels:channels,position:s.position,source:s.sourceKind,
                    torrentBusy:s.torrentBusy,identityUnchanged:s.queue[s.queueIndex].queueId===root.audioOccurrence});
                root.next(root.stage==="audio"?"audio-seek":"second-album");return;
            }
            if(root.stage==="audio-seek") {
                s.seek(32);root.next("audio-seek-wait");return;
            }
            if(root.stage==="audio-seek-wait") {
                if(s.seekPending || s.position<31.5)return;
                root.record("AUDIO_SEEK_OK",{elapsedMs:Date.now()-root.stageAt,position:s.position});
                s.paused=true;root.next("pause");return;
            }
            if(root.stage==="pause") {
                if(root.ticks-root.stageTick<6)return;
                if(!s.paused){root.fail("pause-not-observed");return;}
                root.record("PAUSE_OK",{position:s.position});
                s.paused=false;root.next("resume");return;
            }
            if(root.stage==="resume") {
                if(s.paused||s.position<33)return;
                root.record("RESUME_OK",{position:s.position});
                root.next("video-wait");return;
            }
            if(root.stage==="video-wait") {
                if(!s.hasVideo) {
                    if(root.ticks-root.stageTick>120){root.fail("no-qualified-exact-music-video");}
                    return;
                }
                root.record("VIDEO_MATCH_READY",{elapsedMs:Date.now()-root.audioStartMs});
                root.videoStartMs=Date.now();
                s.toggleVideo();root.next("video");return;
            }
            if(root.stage==="video") {
                if(s.videoError){root.fail("video-error-"+s.videoError);return;}
                if(!s.videoShown || !s.loaded || !s.player)return;
                var width=Number(s.player.getProperty("dwidth")||0);
                var height=Number(s.player.getProperty("dheight")||0);
                var vcodec=String(s.player.mpvProperty("video-codec")||"");
                var vtime=Number(s.player.getProperty("video-pts")||0);
                if(width<1||height<1||!vcodec)return;
                root.videoLoadedMs=Date.now();
                root.record("VIDEO_DECODED",{elapsedMs:root.videoLoadedMs-root.videoStartMs,
                    width:width,height:height,codec:vcodec,videoPts:vtime,quality:s.qualityLabel});
                root.capture("video-frame","video-seek");return;
            }
            if(root.stage==="video-seek") {
                s.seek(65);root.next("video-seek-wait");return;
            }
            if(root.stage==="video-seek-wait") {
                if(s.seekPending || s.position<64.5)return;
                root.record("VIDEO_SEEK_OK",{position:s.position});
                w.toggleVideoFullscreen();
                if(!w.videoFullscreen){root.fail("fullscreen-failed");return;}
                root.capture("video-fullscreen","video-back");return;
            }
            if(root.stage==="video-back") {
                w.exitVideoFullscreen();
                if(w.videoFullscreen){root.fail("fullscreen-exit-failed");return;}
                s.toggleVideo();root.next("audio-back");return;
            }
            if(root.stage==="audio-back") {
                if(s.videoError){root.fail("audio-restore-"+s.videoError);return;}
                if(s.videoShown || !s.loaded || !s.player)return;
                var restoredAo=String(s.player.getProperty("current-ao")||"");
                var restoredCodec=String(s.player.mpvProperty("audio-codec")||"");
                if(!restoredAo || restoredAo==="null" || !restoredCodec)return;
                root.record("AUDIO_RESTORED",{elapsedMs:Date.now()-root.stageAt,position:s.position,
                    source:s.sourceKind,codec:restoredCodec});
                root.next("sustain");return;
            }
            if(root.stage==="sustain") {
                if(s.player && s.player.getProperty("paused-for-cache")===true)root.cachedStalls++;
                root.sampledTicks++;
                if(root.sampledTicks<50)return;
                root.record("SUSTAIN",{seconds:root.sampledTicks*0.2,cachePausedSamples:root.cachedStalls,
                    advancing:!s.paused && s.position>65,source:s.sourceKind});
                s.next(1);root.audioStartMs=Date.now();root.audioLoadedMs=0;
                root.audioOccurrence=s.queue[s.queueIndex].queueId;
                root.next("audio-second");return;
            }
            if(root.stage==="second-album") {
                w.openAlbum("MPREb_dqWTncCjkSp");root.next("second-album-loading");return;
            }
            if(root.stage==="second-album-loading") {
                if(s.browsing)return;
                if(s.browseError){root.fail("second-album-"+s.browseError);return;}
                var secondIndex=(s.tracks||[]).findIndex(function(t){return t.title==="Billie Jean";});
                if(secondIndex<0){root.fail("billie-jean-unavailable");return;}
                root.secondAudioStartMs=Date.now();
                s.playTrack(secondIndex);
                if(w.showSong)w.showSong();
                root.next("second-audio");return;
            }
            if(root.stage==="second-audio") {
                if(!s.loaded||!s.player)return;
                var secondAo=String(s.player.getProperty("current-ao")||"");
                var secondCodec=String(s.player.mpvProperty("audio-codec")||"");
                if(!secondAo||secondAo==="null"||!secondCodec||s.position<0.7)return;
                root.record("SECOND_AUDIO_DECODED",{elapsedMs:Date.now()-root.secondAudioStartMs,
                    codec:secondCodec,source:s.sourceKind});
                root.next("second-video-match");return;
            }
            if(root.stage==="second-video-match") {
                if(!s.hasVideo)return;
                root.secondVideoStartMs=Date.now();
                s.toggleVideo();root.next("second-video");return;
            }
            if(root.stage==="second-video") {
                if(s.videoError){root.fail("second-video-"+s.videoError);return;}
                if(!s.videoShown||!s.loaded||!s.player)return;
                var secondWidth=Number(s.player.getProperty("dwidth")||0);
                var secondHeight=Number(s.player.getProperty("dheight")||0);
                var secondCodecVideo=String(s.player.mpvProperty("video-codec")||"");
                if(secondWidth<1||secondHeight<1||!secondCodecVideo)return;
                root.record("SECOND_VIDEO_DECODED",{elapsedMs:Date.now()-root.secondVideoStartMs,
                    width:secondWidth,height:secondHeight,codec:secondCodecVideo});
                s.seek(65);root.next("second-video-seek");return;
            }
            if(root.stage==="second-video-seek") {
                if(s.seekPending || s.position<64.5)return;
                root.record("SECOND_VIDEO_SEEK_OK",{position:s.position});
                root.next("second-video-settle");return;
            }
            if(root.stage==="second-video-settle") {
                if(Date.now()-root.stageAt<2000)return;
                root.capture("second-video-frame","done");return;
            }
            if(root.stage==="done") {
                root.record("PASS",{trackCount:s.queue.length,profileActive:s.profileActive});
                tick.stop();Qt.quit();
            }
        }
    }
}
