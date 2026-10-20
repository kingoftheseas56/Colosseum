import QtQuick
import QtTest
TestCase {
    id:test
    name:"VinylNativeSourceSessionBridge"
    width:720; height:520
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
    Component {
        id: collectionFactory
        QtObject {
            property var rows: []
            signal changed()
            function items(world) { return rows; }
            function replaceItems(world,removed,added) {
                rows=rows.filter(r=>removed.indexOf(r.id)<0 && !added.some(a=>a.id===r.id)).concat(added);changed();return true;
            }
        }
    }
    function test_changedAudioSameMediaCancelAndProfileFence() {
        var store=collectionFactory.createObject(test);session.collectionStore=store;
        var path=BridgeFixtures.createIdentityAudio();compare(session.importFiles([path]).imported,1);
        var album=store.rows[0].payload.album;session.shownEdition=session.normalize(album);session.playTrack(0);
        tryVerify(()=>session.player!==null);var oldId=session.playingTrack.id;
        var queueId=session.queue[0].queueId;
        BridgeFixtures.changeIdentityAudio(path,320);
        compare(session.importFiles([path]).imported,0);verify(session.pendingImportCeremony!==null);
        var relationship=session.pendingImportCeremony.relationship;
        session.cancelImportIdentity();compare(session.pendingImportCeremony,null);compare(store.rows.length,1);
        session.importFiles([path]);compare(session.pendingImportCeremony.relationship,relationship);
        verify(session.decideImportIdentity(relationship,"same-media"));tryCompare(session,"browseError","");
        compare(store.rows[0].payload.album.tracks[0].id,oldId);compare(session.queue[0].queueId,queueId);
        session.retry();tryVerify(()=>session.player!==null);compare(session.error,"");compare(session.playingTrack.id,oldId);
        var second=BridgeFixtures.createIdentityAudio();session.importFiles([second]);BridgeFixtures.changeIdentityAudio(second,480);
        session.importFiles([second]);verify(session.pendingImportCeremony!==null);
        var pending=session.pendingImportCeremony.relationship;session.clearProfile();
        compare(session.pendingImportCeremony,null);verify(!session.decideImportIdentity(pending,"new-media"));
        store.destroy();
    }
    function test_physicalCancelDefersNativeIdentityAndReimportReprompts_data() {
        return [{tag:"escape",pointer:false},{tag:"pointer",pointer:true}];
    }
    function test_physicalCancelDefersNativeIdentityAndReimportReprompts(data) {
        var store=collectionFactory.createObject(test);session.collectionStore=store;
        var path=BridgeFixtures.createIdentityAudio();BridgeFixtures.changeIdentityAudio(path,data.pointer ? 1310 : 1110);
        compare(session.importFiles([path]).imported,1);var oldId=store.rows[0].payload.album.tracks[0].id;
        BridgeFixtures.changeIdentityAudio(path,data.pointer ? 1410 : 1210);
        session.importFiles([path]);var relationship=session.pendingImportCeremony.relationship;
        var component=Qt.createComponent("../../qml/VaultIdentityCeremonyDialog.qml");compare(component.status,Component.Ready,component.errorString());
        var dialog=component.createObject(test);verify(dialog!==null);
        dialog.cancelRequested.connect(function(){session.cancelImportIdentity(true);dialog.close();});
        dialog.openCeremony(session.pendingImportCeremony);tryCompare(dialog,"opened",true);wait(0);
        if(data.pointer) {
            var button=findChild(dialog.contentItem,"vaultCancelIdentity");verify(button!==null);mouseClick(button,button.width/2,button.height/2);
        } else keyClick(Qt.Key_Escape);
        tryCompare(dialog,"visible",false);compare(session.pendingImportCeremony,null);
        verify(session.browseError.indexOf("Open local music")>=0);compare(store.rows.length,1);compare(store.rows[0].payload.album.tracks[0].id,oldId);
        session.importFiles([path]);compare(session.pendingImportCeremony.relationship,relationship);
        compare(NativeSource.importFiles([path]).albums.length,0);
        dialog.destroy();store.destroy();
    }
    function test_pendingDecisionRejectsReboundSource() {
        var store=collectionFactory.createObject(test);session.collectionStore=store;
        var path=BridgeFixtures.createIdentityAudio();BridgeFixtures.changeIdentityAudio(path,920);
        compare(session.importFiles([path]).imported,1);BridgeFixtures.changeIdentityAudio(path,1020);
        session.importFiles([path]);var relationship=session.pendingImportCeremony.relationship;
        session.source=null;verify(!session.decideImportIdentity(relationship,"same-media"));
        verify(session.browseError.indexOf("source changed")>=0);
        compare(NativeSource.importFiles([path]).identityCeremonies.length,1);
        store.destroy();
    }
    function test_decisionRetryCannotCrossProfileDeparture() {
        var store=collectionFactory.createObject(test);session.collectionStore=store;
        var path=BridgeFixtures.createIdentityAudio();BridgeFixtures.changeIdentityAudio(path,710);
        compare(session.importFiles([path]).imported,1);BridgeFixtures.changeIdentityAudio(path,810);
        session.importFiles([path]);verify(session.pendingImportCeremony!==null);
        verify(session.decideImportIdentity(session.pendingImportCeremony.relationship,"new-media"));
        session.clearProfile();wait(0);compare(store.rows.length,1);compare(session.library.length,0);
        compare(session.pendingImportCeremony,null);store.destroy();
    }
    function test_changedAudioMultipleNewMediaDoesNotRetargetQueue() {
        var store=collectionFactory.createObject(test);session.collectionStore=store;
        var a=BridgeFixtures.createIdentityAudio(), b=BridgeFixtures.createIdentityAudio();
        // Distinct size avoids likely-copy ceremony in this changed-content test.
        BridgeFixtures.changeIdentityAudio(b,240);
        compare(session.importFiles([a,b]).imported,2);
        session.shownEdition=session.normalize(store.rows[0].payload.album);session.playTrack(0);tryVerify(()=>session.player!==null);
        var oldId=session.playingTrack.id, oldQueue=session.queue[0].queueId;
        BridgeFixtures.changeIdentityAudio(a,400);BridgeFixtures.changeIdentityAudio(b,500);
        session.importFiles([a,b]);var first=session.pendingImportCeremony.relationship;
        verify(session.decideImportIdentity(first,"new-media"));
        tryVerify(()=>session.pendingImportCeremony!==null && session.pendingImportCeremony.relationship!==first);
        verify(session.decideImportIdentity(session.pendingImportCeremony.relationship,"new-media"));
        tryCompare(session,"browseError","");compare(session.queue[0].queueId,oldQueue);compare(session.playingTrack.id,oldId);
        session.retry();tryCompare(session,"sourceState","error");verify(session.error.indexOf("changed")>=0);
        compare(store.rows.length,4);store.destroy();
    }
    function test_nativeSignalAlbumOpensAndQueues() {
        console.log("Bridge array diagnostics",Array.isArray(rawAlbum.tracks),Array.isArray(rawAlbum.artists),Array.isArray(rawAlbum.editions));
        session.openAlbum(rawAlbum.id);tryCompare(session,"browsing",false);
        compare(session.browseError,"");compare(session.shownAlbumId,rawAlbum.id);compare(session.tracks.length,1);
        session.playTrack(0);tryVerify(()=>session.player!==null);compare(String(session.player.currentUrl).indexOf("file:"),0);
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
