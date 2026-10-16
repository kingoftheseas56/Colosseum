import QtQuick
import QtTest
TestCase {
    id:test
    name:"VinylSessionStorageSeams"
    when:windowShown
    property var session
    property var source
    property var collection
    property var progress
    Component {
        id:storeFactory
        QtObject {
            property var entries:[]
            property bool fail:false
            property int batches:0
            property int singles:0
            property var recorded:null
            function items(world) {return entries;}
            function has(world,id) {return entries.some(e=>e.id===id);}
            function add(world,entry) {singles++;return false;}
            function replaceItems(world,ids,added) {
                batches++;if(fail) return false;
                entries=entries.filter(e=>ids.indexOf(e.id)<0 && !added.some(a=>a.id===e.id)).concat(added);return true;
            }
            function recordSilent(entry) {recorded=entry;}
            function recent(kind,count) {return [];}
        }
    }
    Component {
        id:sourceFactory
        QtObject {
            property var records:[]
            property var importResult:({})
            property int serial:0
            property var resolvedTrack:null
            function projectedAlbum(id) {
                var found=null;records.forEach(entry=>{var a=entry.payload && entry.payload.album ? entry.payload.album : entry;if(a.id===id) found=a;});return found;
            }
            function projectedTrack(id) {
                // Match native setAlbums: accumulate tracks, last album/track ID wins.
                var found=null;records.forEach(entry=>{var a=entry.payload && entry.payload.album ? entry.payload.album : entry;
                    var tracks=(a.tracks || []).slice();(a.editions || []).forEach(e=>tracks=tracks.concat(e.tracks || []));
                    tracks.forEach(t=>{if(t.id===id) found=t;});});return found;
            }
            signal completed(string lane,int requestId,var data,string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function setAlbums(entries) {records=entries;}
            function importFiles(paths) {return importResult;}
            function request(lane,operation,value) {if(operation==="resolve") resolvedTrack=projectedTrack(value);return ++serial;}
            function cancel(lane) {}
        }
    }
    function album(id,title) {return {id:id,title:title || id,artists:["Artist"],origin:"local",manifestPath:"/album.vinyl.json",tracks:[{id:id+"1",title:"Track",seconds:60}]};}
    function entry(a) {return {id:a.id,title:a.title,payload:{album:a}};}
    function init() {
        collection=storeFactory.createObject(test);progress=storeFactory.createObject(test);source=sourceFactory.createObject(test);
        collection.entries=[entry(album("old"))];
        var c=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");compare(c.status,Component.Ready,c.errorString());
        session=c.createObject(test,{source:source,collectionStore:collection,progressStore:progress});verify(session!==null);
    }
    function cleanup() {session.destroy();wait(0);collection.destroy();progress.destroy();source.destroy();wait(0);}
    function test_failedBatchKeepsOldLibraryAndSource() {
        collection.fail=true;source.importResult={albums:[album("new")],replacedManifestPaths:["/album.vinyl.json"],errors:[]};
        var result=session.importFiles(["/album.vinyl.json"]);compare(result.imported,0);compare(collection.entries[0].id,"old");
        compare(collection.batches,1);compare(collection.singles,0);compare(session.library[0].id,"old");verify(result.errors.length>0);
    }
    function test_successfulBatchReplacesReleaseSetOnce() {
        source.importResult={albums:[album("new")],replacedManifestPaths:["/album.vinyl.json"],errors:[]};
        var result=session.importFiles(["/album.vinyl.json"]);compare(result.imported,1);compare(collection.entries.length,1);compare(collection.entries[0].id,"new");compare(collection.batches,1);compare(collection.singles,0);
    }
    function test_authoritativeLibraryWinsOverPlayingSnapshot() {
        session.shownEdition=session.normalize(album("old","Stale"));session.playTrack(0);
        collection.entries=[entry(album("old","Updated"))];session.refreshLibrary();
        compare(source.projectedAlbum("old").title,"Updated");compare(session.playingEdition.name,"Stale");
    }
    function test_refreshRetainsQueuedTrackRemovedFromCurrentRelease() {
        var old=album("old","Original");old.tracks.push({id:"removed",title:"Queued original",seconds:60,localPath:"/valid/removed.wav"});
        session.shownEdition=session.normalize(old);session.playTrack(0);
        var fresh=album("old","Fresh title");collection.entries=[entry(fresh)];session.refreshLibrary();
        session.next(1);
        compare(session.playingTrack.id,"removed");verify(source.resolvedTrack!==null,"Queued valid source must survive refreshed album omission");
        compare(source.resolvedTrack.localPath,"/valid/removed.wav");compare(source.projectedAlbum("old").title,"Fresh title");
        compare(source.projectedAlbum("old").tracks.length,1);compare(collection.entries[0].payload.album.tracks.length,1);
    }
    function test_refreshRetainsQueuedRemovedEditionAndFreshTrackAuthority() {
        var old=album("old","Original");old.editions=[{id:"standard",tracks:old.tracks},{id:"deluxe",tracks:[{id:"bonus",title:"Bonus",seconds:60,localPath:"/valid/bonus.wav"}]}];
        session.shownEdition=session.normalize(old);session.selectEdition("deluxe");session.enqueueTrack(0);
        var fresh=album("old","Fresh title");fresh.tracks[0].localPath="/current/source.wav";collection.entries=[entry(fresh)];session.refreshLibrary();
        session.playQueue(0);verify(source.resolvedTrack!==null,"Removed edition's queued track must remain resolvable");compare(source.resolvedTrack.id,"bonus");
        compare(source.projectedAlbum("old").title,"Fresh title");compare(source.projectedAlbum("old").editions,undefined);
        compare(source.projectedTrack("old1").localPath,"/current/source.wav");
    }
    function test_versionOneResumeMigratesWithoutAutoplay() {
        var edition=session.normalize(album("old"));
        session.restore({resume:{version:1,edition:edition,queue:[0],queueIndex:0,position:24,duration:60,trackId:"old1"}});
        compare(session.queue.length,1);compare(session.queue[0].trackId,"old1");compare(session.position,24);verify(session.paused);compare(session.player,null);
    }
    function test_mixedQueueVersionTwoRoundTripNoAutoplay() {
        session.shownEdition=session.normalize(album("old"));session.playTrack(0);
        session.shownEdition=session.normalize(album("second"));session.enqueueTrack(0);session.playQueue(1);session.snapshot();
        var saved=progress.recorded;compare(saved.resume.version,2);session.clearQueue();session.restore(saved);
        compare(session.queue.length,2);compare(session.playingTrack.id,"second1");compare(session.queueIndex,1);verify(session.paused);compare(session.player,null);
        session.setShuffle(true);session.setShuffle(false);compare(session.queue[session.queueIndex].trackId,"second1");
    }
    function test_snapshotStoresEditionOnceForLargeQueue() {
        var a=album("large");a.tracks=[];for(var i=0;i<1000;++i) a.tracks.push({id:"track:"+i,title:"Track "+i,seconds:60});
        session.playSongs(a.tracks.map((t,i)=>({album:a,track:t,index:i})),false);session.snapshot();
        var resume=progress.recorded.resume;compare(resume.queue.length,1000);compare(resume.editionTable.length,1);
        verify(resume.queue.every(row=>row.edition===undefined && row.track===undefined));
        verify(JSON.stringify(resume).length<1000000);
    }
    function test_invalidResumeDoesNotMutateLiveSelection() {
        session.shownEdition=session.normalize(album("old"));session.playTrack(0);var id=session.playingTrack.id;
        session.restore({resume:{version:2,edition:{},queue:[{trackId:"bad",edition:{discs:[[]]},discIndex:0,trackIndex:0}],queueIndex:0,position:0,trackId:"bad"}});
        compare(session.playingTrack.id,id);
    }
    function test_suppliedTimestampAndMapNotesNormalized() {
        var a=album("old");a.notes={0:"Release note"};a.tracks[0].lyrics=[{text:"Hello",at:12},{text:"World",at:20}];a.tracks[0].notes={0:"Supplied note",1:[{text:"Second note"}],bad:42};
        session.shownEdition=session.normalize(a);session.playTrack(0);
        verify(session.timedLyrics);compare(session.lyrics[0].time,12);compare(session.notes[0][0].text,"Supplied note");compare(session.notes[1][0].text,"Second note");compare(session.shownEdition.about,"");
    }
}
