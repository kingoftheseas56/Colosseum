import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylPlaybackActions"
    when: windowShown
    width: 960; height: 720
    property var session: null
    property var source: null
    Component {
        id: sourceFactory
        QtObject {
            property int serial: 0
            signal completed(string lane,int id,var data,string error)
            function request(lane,operation,value,options){return ++serial;}
            function cancel(lane: string): void {}
            function setAlbums(albums) {}
        }
    }
    Component {id: sessionFactory;Vinyl.VinylLiveSession {collectionStore:null;progressStore:null;activityPreferences:null}}
    function track(id,disc,available) {return {id:id,title:id,disc:disc,seconds:30,available:available!==false};}
    function album() {
        return {id:"album",title:"Release",artists:["Artist"],tracks:[track("a",1),track("b",2),track("c",1)],
            editions:[{id:"original",tracks:[track("a",1),track("b",2),track("c",1)]},
                {id:"bonus",tracks:[track("missing",1,false),track("same",2),track("same",1),track("tail",2)]}]};
    }
    function ids() {return session.queue.map(r=>r.trackId).join(",");}
    function init() {
        source=sourceFactory.createObject(test);
        session=sessionFactory.createObject(test,{source:source});
    }
    function cleanup(){session.shutdown();session.destroy();source.destroy();wait(0);}
    function test_exactRawTrackMapsToDiscAndEdition() {
        session.playAlbum(album(),1,"bonus");
        compare(session.playingEditionId,"bonus");compare(session.playingDisc,1);compare(session.playingIndex,0);
        compare(ids(),"same,same,tail");compare(session.queueIndex,1);
        compare(session.playingTrack.id,"same");
    }
    function test_bulkPlayRetainsShownEditionAndSkipsUnavailable() {
        session.shownEdition=session.normalize(album(),album().editions[1]);session.shownDisc=1;
        session.playShownAlbum();
        compare(session.playingEditionId,"bonus");compare(session.queue.length,3);
        compare(session.playingDisc,0);compare(session.playingIndex,1);
        verify(session.queue.every(r=>r.track.available));
    }
    function test_unavailableDirectSelectionDoesNotReplaceQueue() {
        session.playAlbum(album(),0,"original");var before=session.queue.map(r=>r.queueId).join(",");
        session.playAlbum(album(),0,"bonus");
        compare(session.queue.map(r=>r.queueId).join(","),before);verify(session.browseError.length>0);
        verify(!session.enqueueSong(album(),0,"bonus",true));compare(session.queue.length,3);
    }
    function test_songNextAndAppendPreserveDuplicateDiscContext() {
        session.playAlbum(album(),0,"original");
        verify(session.enqueueSong(album(),1,"bonus",true));
        compare(ids(),"a,same,c,b");compare(session.queue[1].editionId,"bonus");compare(session.queue[1].discIndex,1);
        verify(session.enqueuePlayingSong(false));compare(ids(),"a,same,c,b,a");
        verify(session.queue[0].queueId!==session.queue[4].queueId);
    }
    function test_queueShownEditionSkipsUnavailableWithoutReplacingActive() {
        session.playAlbum(album(),0,"original");var active=session.queue[session.queueIndex].queueId;
        session.shownEdition=session.normalize(album(),album().editions[1]);
        session.enqueueAlbum("album",true);
        compare(ids(),"a,same,same,tail,c,b");
        compare(session.queue[session.queueIndex].queueId,active);
    }
    function test_oversizedBulkPlayDoesNotReplaceOrTruncateQueue() {
        session.playAlbum(album(),0,"original");var before=session.queue.map(r=>r.queueId).join(",");
        session.playSongs(new Array(10001).fill({album:album(),index:0}),false);
        compare(session.queue.map(r=>r.queueId).join(","),before);verify(session.browseError.length>0);
    }
}
