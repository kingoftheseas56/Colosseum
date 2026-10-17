import QtQuick
import QtTest
TestCase {
    id:test
    name:"VinylNativeSourceSessionBridge"
    when:windowShown
    property var session
    property var rawAlbum
    function init() {
        var result=NativeSource.importFiles([ManifestPath]);compare(result.errors.length,0);compare(result.albums.length,1);
        rawAlbum=result.albums[0];NativeSource.setAlbums(result.albums);
        var c=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");compare(c.status,Component.Ready,c.errorString());
        session=c.createObject(test,{source:NativeSource,collectionStore:null,progressStore:null});verify(session!==null);
        session.library=[{id:rawAlbum.id,payload:{album:rawAlbum}}];
    }
    function cleanup() {session.destroy();wait(0);}
    function test_nativeSignalAlbumOpensAndQueues() {
        console.log("Bridge array diagnostics",Array.isArray(rawAlbum.tracks),Array.isArray(rawAlbum.artists),Array.isArray(rawAlbum.editions));
        session.openAlbum(rawAlbum.id);tryCompare(session,"browsing",false);
        compare(session.browseError,"");compare(session.shownAlbumId,rawAlbum.id);compare(session.tracks.length,1);
        session.playTrack(0);tryVerify(()=>session.player!==null);compare(session.player.loadedUrl.indexOf("file:"),0);
    }
    function test_nativeSearchAndQueuedAlbum() {
        session.search("");tryCompare(session,"browsing",false);compare(session.results.length,1);
        session.enqueueAlbum(rawAlbum.id);tryCompare(session,"queueRequest",-1);compare(session.queue.length,1);
    }
    function test_z_nativeRefreshRetainsQueuedOmittedSource() {
        session.openAlbum(rawAlbum.id);tryCompare(session,"browsing",false);compare(session.browseError,"");
        session.playTrack(0);tryVerify(()=>session.player!==null);var originalId=session.playingTrack.id;
        BridgeFixtures.refreshManifest();var refreshed=NativeSource.importFiles([ManifestPath]);compare(refreshed.errors.length,0);
        session.library=[{id:refreshed.albums[0].id,payload:{album:refreshed.albums[0]}}];session.refreshSource();
        session.retry();tryVerify(()=>session.player!==null);compare(session.playingTrack.id,originalId);compare(session.error,"");
        session.openAlbum(rawAlbum.id);tryCompare(session,"browsing",false);compare(session.shownEdition.name,"Fresh Native Release");
        verify(session.tracks[0].id!==originalId);compare(session.playingTrack.id,originalId);
    }
    function test_malformedOrdinaryObjectsRemainRejected() {
        verify(!session.validAlbum({id:"bad",tracks:[null]}));
        verify(!session.validAlbum({id:"bad",tracks:{0:{id:"t"},length:1}}));
        verify(!session.validAlbum({id:"bad",tracks:[{id:"t",artists:{0:"Artist",length:1}}]}));
    }
    function test_pagedNativeSearchUsesConsumerEnvelope() {
        var records=[];
        for(var i=0;i<70;i++) records.push(Object.assign({},rawAlbum,{id:"page-"+i,title:"Paged "+i}));
        session.library=records;session.search("");tryCompare(session,"browsing",false);
        compare(session.browseError,"");verify(session.searchComplete);compare(session.results.length,70);
        verify(session.searchRevision.length>0);verify(session.searchOffset>0);
    }

}
