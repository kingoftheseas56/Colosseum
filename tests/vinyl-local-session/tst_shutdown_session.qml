import QtQuick
import QtTest
TestCase {
    id:test
    name:"VinylShutdownSessionModel"
    when:windowShown
    property var session
    property var source
    property var progress
    Component {
        id:transportFactory
        QtObject {
            property int serial:0
            property var cancellations:[]
            property bool replyOnCancel:false
            signal completed(string lane,int requestId,var data,string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function request(lane,operation,value) {return ++serial;}
            function cancel(lane) {cancellations=cancellations.concat([lane]);if(replyOnCancel) completed(lane,serial,{trackId:"one",url:"file:///late.wav"},"");}
            function setAlbums(records) {}
        }
    }
    Component {
        id:progressFactory
        QtObject {
            property int writes:0
            property var last:null
            function recordSilent(entry) {writes++;last=JSON.parse(JSON.stringify(entry));}
            function recent(kind,count) {return [];}
        }
    }
    function album() {return {id:"album",title:"Album",origin:"local",tracks:[{id:"one",title:"One",seconds:60},{id:"two",title:"Two",seconds:60}]};}
    function init() {
        source=transportFactory.createObject(test);progress=progressFactory.createObject(test);
        var c=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");compare(c.status,Component.Ready,c.errorString());
        session=c.createObject(test,{source:source,progressStore:progress,collectionStore:null});verify(session!==null);
        session.shownEdition=session.normalize(album());session.playTrack(0);
    }
    function cleanup() {session.destroy();wait(0);source.destroy();progress.destroy();wait(0);}
    function load() {source.completed("play",session.playRequest,{trackId:"one",url:"file:///one.wav"},"");verify(session.player!==null);session.player.fileLoaded();}
    function test_shutdownRejectsLatePendingResolution() {
        var request=session.playRequest;source.shuttingDown();
        source.completed("play",request,{trackId:"one",url:"file:///late.wav"},"");
        compare(session.player,null,"A pending reply must not create a player after shutdown");
        compare(session.playRequest,-1);
    }
    function test_shutdownStopsLoadedPlayerAndRejectsLateEof() {
        load();var old=session.player, serial=source.serial;source.shuttingDown();
        // destroy() is deferred; exercise an already-queued EOF before deletion.
        old.endFile("eof");old.fileLoaded();old.playbackError("late","late");
        compare(source.serial,serial,"Late EOF must not resolve the next track during shutdown");
        compare(session.queueIndex,0);compare(session.player,null);verify(old.pause);verify(!session.loaded);
        verify(!findChild(session,"vinylProgressTimer").running);verify(!findChild(session,"vinylStartupTimer").running);
    }
    function test_shutdownSnapshotsOncePreservesResumeAndCancelsEveryLane() {
        load();session.player.position=27;session.openAlbum("pending");session.enqueueAlbum("pending");session.enqueueAlbum("later");
        var browse=session.browseRequest, queue=session.queueRequest, before=progress.writes;
        source.replyOnCancel=true;source.shuttingDown();
        compare(progress.writes,before+1);compare(progress.last.resume.position,27);compare(progress.last.resume.trackId,"one");compare(progress.last.resume.queue.length,2);
        compare(session.browseRequest,-1);compare(session.queueRequest,-1);compare(session.playRequest,-1);compare(session.pendingEnqueues.length,0);
        verify(source.cancellations.indexOf("browse")>=0);verify(source.cancellations.indexOf("queue")>=0);verify(source.cancellations.indexOf("play")>=0);
        source.completed("browse",browse,album(),"");source.completed("queue",queue,album(),"");source.shuttingDown();source.checkpointRequested();
        compare(progress.writes,before+1);compare(session.queue.length,2);compare(session.player,null);verify(session.paused);
    }
}
