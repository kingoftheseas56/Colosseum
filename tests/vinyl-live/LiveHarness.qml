import QtQuick
import "../../qml/vinyl"
Window {
    id:root
    visible:true;width:1280;height:800;color:"#171619"
    title:"Vinyl · live listening verification"
    property string stage: ""
    property real savedPosition: 0
    VinylLiveWorld {id:world;anchors.fill:parent;onPowerClicked:Qt.quit()}
    Loader {active:ProbeScenario==="contracts";sourceComponent:ContractChecks {session:world.session}}
    Component.onCompleted: Qt.callLater(function() {
        if (ProbeScenario==="play") {stage="search";world.session.search("Idhayathai Thirudathe Ilaiyaraaja");}
        if (ProbeScenario==="resume") {
            if (world.session.playingTrack.videoId!=="InEXiAJCOuA" || world.session.position<10 || !world.session.paused) {
                console.error("VINYL_PROBE_BAD_RESTORE");Qt.exit(4);return;
            }
            savedPosition=world.session.position;stage="resume";world.page="song";world.session.paused=false;
        }
    })
    Connections {
        target:world.session
        function onBrowsingChanged() {
            if (world.session.browsing) return;
            Qt.callLater(function() {
                if (root.stage!=="search") return;
                var match=world.session.results.find(a => a.browseId==="MPREb_IsOnA4Gotve");
                if (!match) {console.error("VINYL_PROBE_RELEASE_NOT_FOUND");Qt.exit(5);return;}
                console.log("VINYL_PROBE_SEARCH_OK",match.id);root.stage="album";world.session.openAlbum(match.browseId);
            });
        }
        function onAlbumOpened() {
            if (root.stage!=="album") return;
            if (world.session.tracks.length!==7 || world.session.tracks[0].videoId!=="InEXiAJCOuA") {
                console.error("VINYL_PROBE_WRONG_ALBUM");Qt.exit(6);return;
            }
            console.log("VINYL_PROBE_ALBUM_OK",world.session.tracks.length);root.stage="play";world.session.playTrack(0);
        }
        function onErrorChanged() {if(world.session.error && (ProbeScenario==="play" || ProbeScenario==="resume")){console.error("VINYL_PROBE_ERROR",world.session.error);Qt.exit(7);}}
    }
    Timer {
        interval:500;repeat:true;running:ProbeScenario!=="interactive"
        onTriggered: {
            var s=world.session;
            if (!s.loaded || !s.player) return;
            if (root.stage==="play" && s.position>2) {s.seek(12);root.stage="seek";}
            if (root.stage==="seek" && !s.seekPending && s.position>=13) {
                console.log("VINYL_PROBE_PLAY_OK",JSON.stringify({videoId:s.playingTrack.videoId,position:s.position,
                    codec:s.player.mpvProperty("audio-codec"),channels:s.player.mpvProperty("audio-params/channel-count"),quality:s.qualityLabel}));
                // No explicit snapshot: exercise the real aboutToQuit ordering.
                ProbeControl.capture();
                Qt.quit();
            }
            if (root.stage==="resume" && !s.seekPending && s.position>root.savedPosition+1) {
                console.log("VINYL_PROBE_RESUME_OK",JSON.stringify({videoId:s.playingTrack.videoId,from:root.savedPosition,to:s.position}));
                Qt.quit();
            }
        }
    }
}
