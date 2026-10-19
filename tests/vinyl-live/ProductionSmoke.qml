import QtQuick
import "../../qml"

// Explicit QML override, launched only with an isolated COLOSSEUM_APPDATA_TAG.
// Exercises the actual Main.qml loader and native context registration.
Main {
    id:root
    property string probeStage:"open"
    property int probeTicks:0
    property bool sawTorrent:false
    Timer {
        interval:250;running:true;repeat:true
        onTriggered: {
            root.probeTicks++;
            if (root.probeTicks>480) {console.error("VINYL_PRODUCTION_TIMEOUT");Qt.exit(8);return;}
            if (root.probeStage==="open") {root.openWorld("Vinyl");root.probeStage="loaded";return;}
            if (!root.vinylWorld) return;
            var s=root.vinylWorld.session;
            if(s.sourceKind==="torrent") root.sawTorrent=true;
            if (s.error || s.browseError) {console.error("VINYL_PRODUCTION_FAILURE",s.error || s.browseError);Qt.exit(9);return;}
            if (root.probeStage==="loaded") {s.search("Idhayathai Thirudathe Ilaiyaraaja");root.probeStage="search";}
            else if (root.probeStage==="search" && !s.browsing) {
                var release=s.results.find(a => a.browseId==="MPREb_IsOnA4Gotve");
                if (!release) {console.error("VINYL_PRODUCTION_NO_RELEASE");Qt.exit(10);return;}
                s.openAlbum(release.browseId);root.probeStage="album";
            } else if (root.probeStage==="album" && !s.browsing) {
                if (s.tracks.length!==7 || s.tracks[0].videoId!=="InEXiAJCOuA") {Qt.exit(11);return;}
                s.playTrack(0);root.probeStage="play";
            } else if (root.probeStage==="play" && s.loaded && s.position>2) {
                console.log("VINYL_PRODUCTION_OK",JSON.stringify({videoId:s.playingTrack.videoId,position:s.position,
                    codec:s.player.mpvProperty("audio-codec"),channels:s.player.mpvProperty("audio-params/channel-count"),
                    source:s.sourceKind,torrentAttempted:root.sawTorrent,fallbackReason:s.fallbackReason}));
                Qt.quit();
            }
        }
    }
}
