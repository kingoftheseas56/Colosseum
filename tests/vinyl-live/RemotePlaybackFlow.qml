import QtQuick
import "../../qml"

// Ordinary Main.qml, catalogue, acquisition providers, StreamServer and MpvItem.
// No fixture transport, player or catalogue is installed in this journey.
Main {
    id: root
    property string stage: "profile"
    property int ticks: 0
    property int stageTick: 0
    property string occurrence: ""
    property string trackId: ""
    property string videoId: ""
    property real startedAt: 0
    property real intentAtMs: 0
    property real loadedAtMs: 0
    property var selected: ({})
    property int stressCycles: 0
    property real tailTarget: 0
    property bool seekFallbackObserved: false
    property string currentProvider: ""
    readonly property string mode: Qt.application.arguments[2]
    readonly property string directory: Qt.application.arguments[3]
    readonly property bool fallback: mode.indexOf("fallback")===0
    readonly property bool stress: mode.indexOf("stress")>=0
    readonly property bool playingStress: mode==="fallback-playing-stress"
    readonly property bool genesis: mode.indexOf("genesis")>=0
    readonly property int catalogueIndex: fallback||genesis?0:1
    function fail(message){console.error("VINYL_REMOTE_FAIL",stage,message);cycle.stop();Qt.exit(19);}
    function identity(value){return String(value||"").toLowerCase().replace(/[._]+/g," ").replace(/\s+/g," ").trim();}
    function acceptSeekFallback(s,target){
        if(root.mode.indexOf("failover")<0||s.sourceKind!=="youtube"||!s.loaded||s.seekPending
            ||s.playingTrack.id!==root.trackId||s.queue[s.queueIndex].queueId!==root.occurrence
            ||root.selected.videoId!==root.videoId||!s.fallbackReason||Math.abs(s.position-target)>2)return false;
        var codec=s.player.mpvProperty("audio-codec"),output=s.player.getProperty("current-ao"),channels=Number(s.player.mpvProperty("audio-params/channel-count"));
        if(!codec||!output||output==="null"||channels<1)return false;
        root.currentProvider="youtube";root.seekFallbackObserved=true;
        console.log("VINYL_REMOTE_SEEK_FALLBACK_OK",JSON.stringify({trackId:root.trackId,occurrence:root.occurrence,position:s.position,target:target,reason:s.fallbackReason,videoId:root.selected.videoId,codec:codec,channels:channels,output:output}));
        return true;
    }
    function next(value){stage=value;stageTick=ticks;console.log("VINYL_REMOTE_STAGE",value);}
    function capture(target){next("capturing");if(!contentItem.grabToImage(function(result){if(!result.saveToFile(directory+"/"+mode+"-playback.png")){fail("capture");return;}next(target);}))fail("capture request");}
    Timer {
        id: cycle; interval:200; repeat:true; running:true
        onTriggered:{
            if(++root.ticks>1500){root.fail("deadline");return;}
            if(root.stage==="profile"){
                if(root.ticks<5||AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;if(!w)return;var s=w.session;
            if(s.error){root.fail(s.error);return;}
            if(root.stage==="world"){
                s.source.completed.connect(function(lane,id,data,error){
                    if(lane!=="play"||error)return;
                    root.selected={provider:data.provider,trackId:data.trackId,videoId:data.videoId || "",infoHash:data.infoHash || "",fileIdx:data.fileIdx,filePath:data.filePath || "",fallbackReason:data.fallbackReason || ""};
                    console.log("VINYL_REMOTE_SELECTION",JSON.stringify(root.selected));
                });
                w.openAlbum(root.fallback?"MPREb_DmBhgAVua1o":"MPREb_NA9bTHvhFmF");root.next("album");return;
            }
            if(root.stage==="album"&&!s.browsing){
                if(s.browseError||!s.tracks.length){root.fail("catalogue album "+s.browseError);return;}
                var index=root.catalogueIndex;
                if(!root.fallback&&s.tracks[index].title!==(root.genesis?"The Genesis":"N.Y. State of Mind")){root.fail("unexpected catalogue recording");return;}
                if(root.fallback&&s.tracks[index].title!=="Stan (feat. Dido)"){root.fail("unexpected fallback recording");return;}
                root.videoId=s.shownEdition.sourceAlbum.tracks[index].videoId;
                root.trackId=s.tracks[index].id;root.intentAtMs=Date.now();s.playTrack(index);root.occurrence=s.queue[s.queueIndex].queueId;
                console.log("VINYL_REMOTE_INTENT",JSON.stringify({title:s.playingTrack.title,trackId:root.trackId,occurrence:root.occurrence}));
                root.next("playing");return;
            }
            if(root.stage==="playing"&&s.loaded&&!root.loadedAtMs)root.loadedAtMs=Date.now();
            if(root.stage==="playing"&&s.loaded&&s.position>2){
                var expected=root.fallback?"youtube":"torrent";
                if(s.sourceKind!==expected){root.fail("expected "+expected+", got "+s.sourceKind+" "+s.fallbackReason);return;}
                root.currentProvider=expected;
                if(s.playingTrack.id!==root.trackId||s.queue[s.queueIndex].queueId!==root.occurrence){root.fail("canonical occurrence changed");return;}
                if(root.fallback&&!s.fallbackReason){root.fail("no torrent failure evidence");return;}
                if(root.fallback&&root.selected.videoId!==root.videoId){root.fail("fallback video identity changed");return;}
                var codec=s.player.mpvProperty("audio-codec"),channels=Number(s.player.mpvProperty("audio-params/channel-count")),output=s.player.getProperty("current-ao");
                if(!codec||channels<1||!output||output==="null"){root.fail("decoder/device output missing");return;}
                var rawMetadata=s.player.getProperty("metadata") || {},metadata={};
                for(var key in rawMetadata)metadata[key.toLowerCase()]=rawMetadata[key];
                if(!root.fallback&&(root.identity(metadata.title)!==root.identity(s.playingTrack.title)
                    ||root.identity(metadata.artist)!=="nas"||root.identity(metadata.album)!=="illmatic")) {
                    root.fail("decoded torrent tags do not prove the requested recording");return;
                }
                console.log("VINYL_REMOTE_OUTPUT_OK",JSON.stringify({source:s.sourceKind,title:s.playingTrack.title,codec:codec,channels:channels,output:output,position:s.position,loadedMs:root.loadedAtMs-root.intentAtMs,advancingMs:Date.now()-root.intentAtMs,selection:root.selected,tags:{title:metadata.title,album:metadata.album,artist:metadata.artist}}));
                s.player.nativePlaybackEvent.connect(function(kind,entry,position,duration,load,observed,observationSerial){
                    if(kind==="loaded" || kind==="seek-loaded" || kind==="seek-start" || kind==="restart" || kind.indexOf("ended-")===0)
                        console.log("VINYL_REMOTE_ENGINE_EVENT",JSON.stringify({stage:root.stage,kind:kind,entry:entry,position:position,duration:duration,load:load,observed:observed,observationSerial:observationSerial}));
                });
                s.player.trackedSeekReply.connect(function(serial,success,load){console.log("VINYL_REMOTE_SEEK_REPLY",serial,success,load);});
                s.seek(12);root.next("seek");return;
            }
            if(root.stage==="seek"&&s.loaded&&!s.seekPending&&s.position>=11.5){
                if(s.sourceKind!==root.currentProvider&&!root.acceptSeekFallback(s,12)){root.fail("unqualified source change during seek-to-12");return;}
                s.paused=true;root.startedAt=s.position;root.next("paused");return;
            }
            if(root.stage==="paused"&&root.ticks-root.stageTick>5){
                if(!s.paused||Math.abs(s.position-root.startedAt)>0.5){root.fail("pause drift");return;}
                s.paused=false;root.next("resumed");return;
            }
            if(root.stage==="resumed"&&s.position>root.startedAt+1){
                if(s.sourceKind!==root.currentProvider||s.playingTrack.id!==root.trackId||s.queue[s.queueIndex].queueId!==root.occurrence){root.fail("playback source or occurrence changed during seek");return;}
                console.log("VINYL_REMOTE_SEEK_PAUSE_RESUME_OK",s.position);
                if(root.mode.indexOf("review")>=0){s.paused=true;root.capture("hold");return;}
                if(root.stress&&!root.playingStress)s.paused=true;
                root.capture("eof");return;
            }
            if(root.stage==="eof"){
                root.tailTarget=Math.max(0,s.duration-(root.playingStress&&root.stressCycles<12?3:1));
                s.seek(root.tailTarget);root.next("ending");return;
            }
            if(root.stage==="ending"){
                if(root.stress && root.stressCycles<12 && !s.seekPending
                    &&s.queue[s.queueIndex].queueId===root.occurrence&&s.position>=s.duration-(root.playingStress?3.3:1.3)) {
                    console.log("VINYL_REMOTE_STRESS_END_SETTLED",root.stressCycles,s.position,s.duration);
                    s.seek(12);root.next("stress-back");return;
                }
                if(root.stress&&root.stressCycles===12&&!s.seekPending)s.paused=false;
                if(s.queue[s.queueIndex].queueId!==root.occurrence){
                    if(root.stress&&root.stressCycles!==12){root.fail("premature EOF during seek stress");return;}
                    if(s.queueIndex!==root.catalogueIndex+1){root.fail("unexpected next track");return;}
                    console.log("VINYL_REMOTE_EOF_QUEUE_OK",JSON.stringify({nextTrack:s.playingTrack.id,index:s.queueIndex}));
                    root.next("retire");return;
                }
                if(s.sourceKind!==root.currentProvider){
                    if(root.mode.indexOf("failover")<0||s.sourceKind!=="youtube") {root.fail("source changed before EOF");return;}
                    if(s.loaded&&!s.seekPending&&!root.seekFallbackObserved) {
                        if(!root.acceptSeekFallback(s,root.tailTarget)) {
                            root.fail("seek failover lost recording or target");return;
                        }
                    }
                }
            }
            if(root.stage==="stress-back"&&!s.seekPending&&s.position>=11.5&&s.position<20) {
                root.stressCycles++;
                console.log("VINYL_REMOTE_STRESS_CYCLE",root.stressCycles,s.position);
                root.next("eof");return;
            }
            if(root.stage==="ending"&&root.ticks%5===0&&s.player) {
                console.log("VINYL_REMOTE_SEEK_STATE",JSON.stringify({pending:s.seekPending,position:s.position,
                    physical:s.player.position,duration:s.duration,pause:s.paused,
                    seeking:s.player.getProperty("seeking"),buffering:s.player.getProperty("paused-for-cache"),
                    cache:s.player.getProperty("demuxer-cache-state")}));
            }
            if(root.stage==="retire"){
                s.clearProfile();root.next("retired");return;
            }
            if(root.stage==="retired"&&root.ticks-root.stageTick>5){
                if(s.player||s.queue.length||s.sourceKind||s.fallbackReason){root.fail("profile retirement");return;}
                console.log("VINYL_REMOTE_FLOW_OK",root.mode);cycle.stop();Qt.quit();return;
            }
            if(root.stage==="hold"){console.log("VINYL_REMOTE_REVIEW_READY");cycle.stop();}
        }
    }
}
