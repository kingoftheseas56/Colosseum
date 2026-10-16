import QtQuick
import QtTest
TestCase {
    id: test
    name: "VinylLocalSessionContracts"
    when: windowShown
    property var session
    property var transport
    Component {
        id: sourceFactory
        QtObject {
            property int serial: 0
            property int cancellations: 0
            property var calls: []
            property var records: []
            signal completed(string lane, int requestId, var data, string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function request(lane, operation, value) {
                calls=calls.concat([{lane:lane,operation:operation,value:value}]); return ++serial;
            }
            function cancel(lane) { cancellations++; }
            function setAlbums(entries) { records=entries; }
            function importFiles(paths) { return {albums:[test.album("A")],errors:[]}; }
        }
    }
    function album(id) {
        return {id:id,browseId:id,origin:"local",title:"Album "+id,artists:["Local artist"],tracks:[
            {id:"vault:"+id,title:"Track "+id,artists:["Local artist"],seconds:120,available:true,localPath:"/test/"+id+".wav"}]};
    }
    function init() {
        transport=sourceFactory.createObject(test);
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(component.status,Component.Ready,component.errorString());
        session=component.createObject(test,{source:transport});
        verify(session!==null);
    }
    function cleanup() {
        session.destroy();wait(0);transport.destroy();wait(0);
    }
    function start(id) {
        session.shownEdition=session.normalize(album(id));
        session.playTrack(0);
        var request=session.playRequest;
        transport.completed("play",request,{trackId:"vault:"+id,url:"file:///test/"+id+".wav",headers:{},qualityLabel:"Local audio"},"");
        verify(session.player!==null);
        session.player.fileLoaded();
    }
    function test_localResolutionUsesCoreTrackAndOneOwner() {
        start("A");
        compare(transport.calls[0].operation,"resolve");compare(transport.calls[0].value,"vault:A");
        compare(session.player.objectName,"vinylMpvPlayer");compare(session.sourceKind,"local");
        compare(session.sourceState,"playing");verify(session.loaded);
        compare(session.player.loadedUrl,"file:///test/A.wav");
    }
    function test_browsingDoesNotReplacePlayback() {
        start("A");var player=session.player;
        session.openAlbum("B");transport.completed("browse",session.browseRequest,album("B"),"");
        compare(session.shownEditionId,"B");compare(session.playingEditionId,"A");compare(session.player,player);
        compare(session.playingTrack.id,"vault:A");verify(!session.isPlayingTrack(0));
    }
    function test_lateResolutionCannotPlayWrongSong() {
        session.shownEdition=session.normalize(album("A"));session.playTrack(0);var old=session.playRequest;
        session.shownEdition=session.normalize(album("B"));session.playTrack(0);var current=session.playRequest;
        transport.completed("play",old,{trackId:"vault:A",url:"file:///wrong.wav"},"");compare(session.player,null);
        transport.completed("play",current,{trackId:"vault:A",url:"file:///wrong.wav"},"");
        compare(session.player,null);compare(session.sourceState,"error");verify(session.paused);
    }
    function test_pauseSeekAndResumeUsePlayer() {
        start("A");session.paused=true;verify(session.player.pause);
        session.seek(37);compare(session.player.lastSeek,37);compare(session.player.position,37);
        verify(!session.seekPending);compare(session.position,37);
        session.paused=false;verify(!session.player.pause);
    }
    function test_missingFileDoesNotFallBackOnlineAndCanRecover() {
        session.shownEdition=session.normalize(album("A"));session.playTrack(0);
        transport.completed("play",session.playRequest,null,"This audio file no longer exists.");
        compare(session.sourceState,"error");verify(session.paused);compare(transport.calls.length,1);
        start("B");compare(session.error,"");compare(session.sourceState,"playing");
    }
    function test_decoderErrorIsVisible() {
        start("A");session.player.playbackError("unknown","error");
        compare(session.sourceState,"error");verify(session.error.indexOf("decoded")>=0);verify(session.player.pause);
    }
    function test_profileClearInvalidatesPendingAndPlayer() {
        start("A");session.openAlbum("B");var request=session.browseRequest;
        transport.profileLeaving();compare(session.player,null);compare(session.playingIndex,-1);
        transport.completed("browse",request,album("B"),"");compare(session.shownEditionId,"");compare(session.library.length,0);
    }
    function test_noCollectionReportsImportFailure() {
        var result=session.importFiles(["/test/A.wav"]);
        compare(result.imported,0);compare(result.errors.length,1);verify(session.browseError.length>0);
    }
    function test_eofRetainsMeasuredDurationForUntaggedFile() {
        start("A");
        session.playingEdition=session.normalize({id:"A",origin:"local",title:"A",tracks:[{id:"vault:A",title:"A",seconds:0}]});
        session.player.endFile("eof");
        compare(session.sourceState,"ended");verify(session.paused);
        compare(session.duration,120);compare(session.position,120);
    }
    function test_cancelBrowsePreservesPlaybackAndInvalidatesReply() {
        start("A");var player=session.player;var generation=session.profileGeneration;
        session.openAlbum("B");var pending=session.browseRequest;
        session.cancelBrowse();
        compare(session.browseRequest,-1);verify(!session.browsing);
        transport.completed("browse",pending,album("B"),"");
        compare(session.shownEditionId,"A");compare(session.player,player);verify(!session.paused);
        compare(session.profileGeneration,generation);
    }
    function test_profileGenerationChangesOnlyAtProfileClear() {
        var initial=session.profileGeneration;
        start("A");compare(session.profileGeneration,initial);
        start("B");compare(session.profileGeneration,initial);
        session.clearProfile();compare(session.profileGeneration,initial+1);
        session.clearProfile();compare(session.profileGeneration,initial+2);
    }
    function test_metadataAbsentIsUnavailable() {
        start("A");verify(!session.hasLyrics);verify(!session.hasNotes);verify(!session.hasVideo);
        compare(session.playingCredits.length,0);verify(!session.sectionVisible("lyrics"));
    }
}
