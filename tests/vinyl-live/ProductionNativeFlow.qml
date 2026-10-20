import QtQuick
import "../../qml"

// Run by the real executable with a disposable COLOSSEUM_APPDATA_TAG.
// Arguments after this QML file: record|restore|empty, audio file A, audio file B.
Main {
    id: root
    property string flowStage: "profile"
    onFlowStageChanged: console.log("VINYL_NATIVE_STAGE",flowStage)
    property int flowTicks: 0
    property var firstAlbum
    property string currentOccurrence: ""
    property real flowPlayStartedAt: 0
    property int flowHistorySeconds: 0
    property bool flowReplaced: false
    property var repeatPlayer: null
    property int windowWarmupTicks: 0
    Connections {
        target: ProfileActivity
        function onIntegrityError(code,detail) { root.failFlow("Activity rejected: "+code+" "+detail); }
    }
    function listeningSeconds() {
        return Number(safeMonth().listenSeconds || 0);
    }
    function safeMonth() { return JSON.parse(JSON.stringify(ProfileActivity.projectMonth(Qt.formatDateTime(new Date(),"yyyy-MM")))); }
    function failFlow(message) { console.error("VINYL_NATIVE_FLOW_FAIL",flowStage,message);Qt.exit(12); }
    Timer {
        interval:100;repeat:true;running:true
        onTriggered: {
            root.flowTicks++;
            if(root.flowTicks>600){root.failFlow("timeout");return;}
            if(root.flowStage==="profile") {
                if(!root.visible)return;
                if(++root.windowWarmupTicks<4){root.requestActivate();return;}
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired) {AccountController.continueWithoutAccount();return;}
                root.openWorld("Vinyl");root.flowStage="world";return;
            }
            if(!root.vinylWorld)return;
            var s=root.vinylWorld.session;
            if(s.error || s.browseError){root.failFlow(s.error || s.browseError);return;}
            if(root.flowStage==="world") {
                if(Qt.application.arguments[2]==="empty") {
                    if(s.queue.length || s.player!==null || s.library.length!==2){root.failFlow("cleared queue resurrected or Collection lost");return;}
                    if(ProfilePreferences.keepActivityHistory!==false || s.activityEnabled || root.listeningSeconds()<10) {
                        root.failFlow("privacy preference or earlier Activity did not survive restart");return;
                    }
                    console.log("VINYL_NATIVE_EMPTY_RESTORE_OK");Qt.quit();return;
                }
                if(Qt.application.arguments[2]==="restore") {
                    if(s.queue.length!==3 || !s.paused || s.player!==null || s.position<3.5 || s.repeatMode!=="one") {
                        root.failFlow("cold restore: "+JSON.stringify({queue:s.queue.length,paused:s.paused,position:s.position,repeat:s.repeatMode}));return;
                    }
                    console.log("VINYL_NATIVE_RESTORE_OK",JSON.stringify({queue:s.queue.length,paused:s.paused,position:s.position,repeat:s.repeatMode}));
                    root.flowHistorySeconds=root.listeningSeconds();
                    if(root.flowHistorySeconds<10){root.failFlow("Activity did not survive restart");return;}
                    s.paused=false;root.flowStage="resumed";return;
                }
                var imported=s.importFiles([Qt.application.arguments[3],Qt.application.arguments[4]]);
                if(imported.imported!==2){root.failFlow("import "+JSON.stringify(imported));return;}
                if(s.library.length!==2){root.failFlow("Collection did not retain imports");return;}
                root.firstAlbum=s.library[0].payload.album;s.openAlbum(root.firstAlbum.id);
                root.flowStage="album";return;
            }
            if(root.flowStage==="album" && !s.browsing && s.tracks.length) {
                var wasSaved=s.saved;s.toggleSaved();
                if(s.saved===wasSaved || s.library.length!==2){root.failFlow("Collection save failed");return;}
                s.toggleSaved();
                if(s.saved!==wasSaved || s.library.length!==2){root.failFlow("unsaving lost the local catalogue");return;}
                console.log("VINYL_NATIVE_COLLECTION_OK");
                root.flowPlayStartedAt=Date.now();s.playTrack(0);root.flowStage="playing";return;
            }
            if(root.flowStage==="playing" && !root.flowReplaced && s.loaded && s.position>0.6) {
                root.flowReplaced=true;root.currentOccurrence=s.queue[s.queueIndex].queueId;
                root.flowPlayStartedAt=Date.now();s.playQueue(s.queueIndex);root.flowStage="replacing";return;
            }
            if(root.flowStage==="replacing" && s.loaded && s.position>0.6) {
                if(s.queue[s.queueIndex].queueId!==root.currentOccurrence){root.failFlow("replacement changed the occurrence");return;}
                console.log("VINYL_NATIVE_REPLACE_OK");root.flowStage="playing";return;
            }
            if(root.flowStage==="playing" && s.loaded && s.position>11 && root.listeningSeconds()>=10) {
                s.enqueueTrack(0,false);s.enqueueTrack(0,false);
                if(s.queue.length!==3 || s.queue[0].queueId===s.queue[1].queueId){root.failFlow("occurrences");return;}
                root.currentOccurrence=s.queue[s.queueIndex].queueId;
                s.moveQueue(0,1);
                if(s.queue[s.queueIndex].queueId!==root.currentOccurrence){root.failFlow("move retargeted playback");return;}
                s.setShuffle(true);s.setShuffle(false);
                root.repeatPlayer=s.player;
                s.cycleRepeat();s.cycleRepeat();s.seek(19.5);root.flowStage="repeat";return;
            }
            if(root.flowStage==="repeat" && s.loaded && s.player!==root.repeatPlayer && !s.seekPending && s.position<2) {
                if(s.queue[s.queueIndex].queueId!==root.currentOccurrence){root.failFlow("repeat-one changed the occurrence");return;}
                console.log("VINYL_NATIVE_REPEAT_OK");
                s.seek(3);s.seek(7);s.seek(4);root.flowStage="seek";return;
            }
            if(root.flowStage==="seek" && !s.seekPending && s.position>=3.7 && s.position<7) {
                s.paused=true;s.snapshot();
                var listened=root.listeningSeconds(),upper=Math.ceil((Date.now()-root.flowPlayStartedAt)/1000)+2;
                if(listened<10 || listened>upper){root.failFlow("Activity qualification/double count: "+listened+" max "+upper);return;}
                var codec=s.player.mpvProperty("audio-codec"),channels=s.player.mpvProperty("audio-params/channel-count"),
                    output=s.player.getProperty("current-ao");
                if(typeof codec!=="string" || !codec || Number(channels)!==1 || typeof output!=="string" || !output || output==="null") {
                    root.failFlow("decoder/device output not confirmed: "+JSON.stringify({codec:codec,channels:channels,output:output}));return;
                }
                console.log("VINYL_NATIVE_RECORD_OK",JSON.stringify({queue:s.queue.length,position:s.position,
                    repeat:s.repeatMode,codec:codec,channels:channels,audioOutput:output,listenSeconds:listened}));
                root.flowStage="finish";return;
            }
            if(root.flowStage==="finish")Qt.quit();
            if(root.flowStage==="resumed" && s.loaded && !s.seekPending && s.position>4.5) {
                console.log("VINYL_NATIVE_RESUME_OK",JSON.stringify({position:s.position,queue:s.queue.length}));
                ProfilePreferences.keepActivityHistory=false;root.flowStage="privacy";return;
            }
            if(root.flowStage==="privacy" && s.position>15) {
                if(root.listeningSeconds()!==root.flowHistorySeconds){root.failFlow("listening recorded with history disabled");return;}
                console.log("VINYL_NATIVE_PRIVACY_OK",root.flowHistorySeconds);
                s.clearQueue();s.snapshot();root.flowStage="cleared";return;
            }
            if(root.flowStage==="cleared") {
                if(s.queue.length || s.player!==null){root.failFlow("clear did not retire playback");return;}
                console.log("VINYL_NATIVE_CLEAR_OK");Qt.quit();
            }
        }
    }
}
