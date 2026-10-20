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
    property var selected: ({})
    readonly property string mode: Qt.application.arguments[2]
    readonly property string directory: Qt.application.arguments[3]
    readonly property bool fallback: mode==="fallback"
    function fail(message){console.error("VINYL_REMOTE_FAIL",stage,message);cycle.stop();Qt.exit(19);}
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
                var index=root.fallback?0:1;
                if(!root.fallback&&s.tracks[index].title!=="N.Y. State of Mind"){root.fail("unexpected catalogue recording");return;}
                if(root.fallback&&s.tracks[index].title!=="Stan (feat. Dido)"){root.fail("unexpected fallback recording");return;}
                root.videoId=s.shownEdition.sourceAlbum.tracks[index].videoId;
                root.trackId=s.tracks[index].id;s.playTrack(index);root.occurrence=s.queue[s.queueIndex].queueId;
                console.log("VINYL_REMOTE_INTENT",JSON.stringify({title:s.playingTrack.title,trackId:root.trackId,occurrence:root.occurrence}));
                root.next("playing");return;
            }
            if(root.stage==="playing"&&s.loaded&&s.position>2){
                var expected=root.fallback?"youtube":"torrent";
                if(s.sourceKind!==expected){root.fail("expected "+expected+", got "+s.sourceKind+" "+s.fallbackReason);return;}
                if(s.playingTrack.id!==root.trackId||s.queue[s.queueIndex].queueId!==root.occurrence){root.fail("canonical occurrence changed");return;}
                if(root.fallback&&!s.fallbackReason){root.fail("no torrent failure evidence");return;}
                if(root.fallback&&root.selected.videoId!==root.videoId){root.fail("fallback video identity changed");return;}
                var codec=s.player.mpvProperty("audio-codec"),channels=Number(s.player.mpvProperty("audio-params/channel-count")),output=s.player.getProperty("current-ao");
                if(!codec||channels<1||!output||output==="null"){root.fail("decoder/device output missing");return;}
                var metadata=s.player.getProperty("metadata") || {};
                console.log("VINYL_REMOTE_OUTPUT_OK",JSON.stringify({source:s.sourceKind,title:s.playingTrack.title,codec:codec,channels:channels,output:output,position:s.position,selection:root.selected,tags:{title:metadata.title,album:metadata.album,artist:metadata.artist}}));
                s.seek(12);root.next("seek");return;
            }
            if(root.stage==="seek"&&!s.seekPending&&s.position>=11.5){
                s.paused=true;root.startedAt=s.position;root.next("paused");return;
            }
            if(root.stage==="paused"&&root.ticks-root.stageTick>5){
                if(!s.paused||Math.abs(s.position-root.startedAt)>0.5){root.fail("pause drift");return;}
                s.paused=false;root.next("resumed");return;
            }
            if(root.stage==="resumed"&&s.position>root.startedAt+1){
                if(s.sourceKind!==(root.fallback?"youtube":"torrent")||s.playingTrack.id!==root.trackId||s.queue[s.queueIndex].queueId!==root.occurrence){root.fail("playback source or occurrence changed during seek");return;}
                console.log("VINYL_REMOTE_SEEK_PAUSE_RESUME_OK",s.position);
                if(root.mode==="review"){s.paused=true;root.capture("hold");return;}
                root.capture("eof");return;
            }
            if(root.stage==="eof"){
                s.seek(Math.max(0,s.duration-1));root.next("ending");return;
            }
            if(root.stage==="ending"){
                if(s.queue[s.queueIndex].queueId!==root.occurrence){
                    if(s.queueIndex!==2&& !root.fallback){root.fail("unexpected next track");return;}
                    if(s.queueIndex!==1&&root.fallback){root.fail("unexpected fallback next track");return;}
                    console.log("VINYL_REMOTE_EOF_QUEUE_OK",JSON.stringify({nextTrack:s.playingTrack.id,index:s.queueIndex}));
                    root.next("retire");return;
                }
                if(s.sourceKind!==(root.fallback?"youtube":"torrent")){root.fail("source changed before EOF");return;}
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
