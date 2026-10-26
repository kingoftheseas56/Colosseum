import QtQuick
Item {
    id:root
    required property var session
    property int phase:0
    property int ticks:0
    function check(condition,message) {
        if (!condition) {console.error("VINYL_CONTRACT_FAILURE",message);Qt.exit(9);return false;}
        return true;
    }
    Component.onCompleted: {
        session.search("slow");session.search("latest");
    }
    Timer {
        interval:100;repeat:true;running:true
        onTriggered: {
            var s=root.session;
            root.ticks++;
            if (root.phase===0 && !s.browsing) {
                if (!root.check(s.results.length===1 && s.results[0].id==="latest","stale search replaced latest")) return;
                s.shownEdition=s.normalize({id:"test-release",browseId:"test",title:"Same name",artists:["Artist"],year:"2026",cover:"",tracks:[
                    {id:"track-one",videoId:"InEXiAJCOuA",title:"Same name",artists:["A"],seconds:100,available:true},
                    {id:"track-two",videoId:"NKmGGkvqcuM",title:"Same name",artists:["B"],seconds:100,available:true}]});
                s.playTrack(0);root.phase=1;
            } else if (root.phase===1 && s.error) {
                if (!root.check(s.paused && !s.player && s.sourceState==="error","resolver failure pretended playback")) return;
                s.resumeSeconds=42;s.snapshot();s.toggleSaved();s.search("slow");
                ProbeControl.switchProfile(1);
                if (!root.check(s.playingIndex===-1 && s.results.length===0 && s.library.length===0,"profile state leaked")) return;
                root.phase=2;root.ticks=0;
            } else if (root.phase===2 && root.ticks>25) {
                if (!root.check(!s.browsing && s.results.length===0 && Progress.recent("music",1).length===0,"late reply crossed profile")) return;
                ProbeControl.switchProfile(0);
                if (!root.check(s.paused && s.position===42 && s.playingTrack.videoId==="InEXiAJCOuA" && s.library.length===1,"profile resume or save lost")) return;
                var bar=null;
                for (var child of s.parent.children) if (child.objectName==="vinylPlayerBar") bar=child;
                if (!root.check(bar && !bar.busy,"restored player cannot be resumed from transport")) return;
                var before=s.playingTrack.id;
                s.restore({resume:{version:1,edition:s.playingEdition,queue:[0,1],queueIndex:0,position:10,videoId:"wrong"}});
                if (!root.check(s.playingTrack.id===before && s.position===42,"corrupt resume accepted")) return;
                s.shuffle();
                if (!root.check(s.queue.length===2 && s.queue[0]!==s.queue[1],"shuffle did not permute queue")) return;
                s.next(-1);
                if (!root.check(s.queueIndex===0 && s.paused,"previous wrapped outside queue")) return;
                s.clearProfile();
                console.log("VINYL_CONTRACTS_OK latest-request failure profile-isolation cancelled-reply resume-validation library shuffle queue-boundary");
                Qt.quit();
            }
        }
    }
}
