import QtQuick
Item {
    id: root
    required property var session
    QtObject {
        id: source
        property int serial: 0
        property int cancellations: 0
        property var lastTrack: ({})
        signal completed(int requestId, var data, string error)
        function resolve(track) { lastTrack=track; return ++serial; }
        function cancel() { cancellations++; }
    }
    function check(condition,message) {
        if (!condition) throw new Error(message);
    }
    Component.onCompleted: Qt.callLater(function() {
        try {
            var s=session;s.torrentProvider=source;
            s.shownEdition=s.normalize({id:"album",browseId:"album",title:"Album",artists:["Artist"],year:"",cover:"",tracks:[
                {id:"first",videoId:"InEXiAJCOuA",title:"First",artists:["Artist"],seconds:100,available:true},
                {id:"second",videoId:"NKmGGkvqcuM",title:"Second",artists:["Artist"],seconds:100,available:true}]});
            s.playTrack(0);
            check(s.sourceKind==="torrent" && s.playRequest===-1,"YouTube resolved before preferred torrent");
            check(source.lastTrack.album==="Album" && source.lastTrack.title==="First","catalogue identity lost");
            var old=s.torrentRequest;s.next(1);var current=s.torrentRequest;
            source.completed(old,{url:"http://invalid.example"},"");
            check(s.torrentRequest===current && !s.player,"stale torrent reply started playback");
            s.resumeSeconds=42;s.paused=true;
            source.completed(current,{},"No exact file");
            check(s.sourceKind==="youtube" && s.playRequest>0 && s.position===42 && s.paused,
                  "fallback lost position/pause or did not start secondary resolver");
            check(s.playingTrack.id==="second" && s.queueIndex===1,"fallback changed canonical track or queue");
            s.beginTrack(42);var leaving=s.torrentRequest;s.clearProfile();
            source.completed(leaving,{url:"http://invalid.example"},"");
            check(s.playingIndex===-1 && !s.player && s.torrentRequest===-1,"late torrent reply crossed profile");
            check(source.cancellations>=3,"torrent operations not cancelled");
            console.log("VINYL_TORRENT_SESSION_CONTRACTS_OK preferred-source fallback-position pause queue stale-replies profile");
            Qt.quit();
        } catch (error) {console.error("VINYL_TORRENT_SESSION_CONTRACT_FAILURE",error);Qt.exit(19);}
    })
}
