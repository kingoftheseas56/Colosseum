import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylCachedRecommendationPlayback"
    when: windowShown
    property var session: null
    property var source: null
    Component {
        id: transport
        QtObject {
            property int serial: 0
            property var records: []
            property var resolutions: []
            signal completed(string lane, int requestId, var data, string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function cancel(lane: string): void {}
            function setAlbums(entries) {records=entries;}
            function request(lane: string,operation: string,value: string): int {
                if(lane==="play")resolutions=resolutions.concat([{track:value,
                    registered:records.some(album=>(album.tracks || []).some(track=>track.id===value))}]);
                return ++serial;
            }
        }
    }
    function init() {
        source=transport.createObject(test);
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(component.status,Component.Ready,component.errorString());
        session=component.createObject(test,{source:source});verify(session!==null);
    }
    function cleanup() {session.destroy();wait(0);source.destroy();wait(0);}
    function test_registerSelectedSongBeforeItsFirstResolve_data() {
        return [{tag:"song-page",action:"album"},{tag:"songs",action:"songs"},{tag:"home-mix",action:"home"}];
    }
    function test_registerSelectedSongBeforeItsFirstResolve(data) {
        var cached={id:"cached-song-record",browseId:"ytm:track:abcdefghijk",type:"Song",origin:"youtube-music",
            title:"Cached song",artists:["Artist"],tracks:[{id:"cached-track",videoId:"abcdefghijk",
                title:"Cached song",artists:["Artist"],seconds:240,available:true}]};
        compare(source.records.length,0);compare(session.queue.length,0);
        if(data.action==="album")session.playAlbum(cached,0);
        else if(data.action==="songs")session.playSongs([{album:cached,index:0}],false);
        else verify(session.playHomeSongs([{album:cached,index:0}],0,false,{}));
        tryCompare(session,"playingIndex",0);
        tryVerify(()=>source.resolutions.length>0);
        compare(source.resolutions[0].track,"cached-track");
        verify(source.resolutions[0].registered,"The selected song must be registered before its first resolve request");
    }
}
