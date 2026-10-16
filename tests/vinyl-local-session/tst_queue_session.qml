import QtQuick
import QtTest

// Queue/model tests with a fake MpvItem. These do not establish native decoder EOF.
TestCase {
    id: test
    name: "VinylQueueSessionModel"
    when: windowShown
    property var session
    property var source
    Component {
        id: transport
        QtObject {
            property int serial: 0
            property var records: []
            property bool completeOnCancel: false
            signal completed(string lane,int requestId,var data,string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function request(lane,operation,value) {return ++serial;}
            function cancel(lane) {if(completeOnCancel) completed(lane,serial,{trackId:"a1",url:"file:///late.wav"},"");}
            function setAlbums(entries) {records=entries;}
        }
    }
    function track(id,disc) {return {id:id,title:id,disc:disc || 1,artists:["Artist"],seconds:60,localPath:"/"+id+".wav"};}
    function album(id) {return {id:id,title:id,artists:["Artist"],origin:"local",tracks:[track(id+"1"),track(id+"2"),track(id+"3",2)]};}
    function show(id) {session.shownEdition=session.normalize(album(id));session.shownDisc=0;}
    function load() {
        source.completed("play",session.playRequest,{trackId:session.playingTrack.id,url:"file:///"+session.playingTrack.id+".wav"},"");
        verify(session.player!==null);session.player.fileLoaded();
    }
    function init() {
        source=transport.createObject(test);
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");compare(component.status,Component.Ready,component.errorString());
        session=component.createObject(test,{source:source});verify(session!==null);
    }
    function cleanup() {session.destroy();wait(0);source.destroy();wait(0);}
    function test_multidiscAndBrowseIdentity() {
        show("a");compare(session.tracks.length,2);session.selectDisc(1);compare(session.tracks.length,1);
        session.playTrack(0);compare(session.queue.length,3);compare(session.queueIndex,2);compare(session.playingDisc,1);
        show("b");compare(session.playingTrack.id,"a3");verify(!session.isPlayingTrack(0));session.showPlayingAlbum();compare(session.shownDisc,1);verify(session.isPlayingTrack(0));
    }
    function test_mixedReleaseAppendResolveRetainsSourceRecords() {
        show("a");session.playTrack(0);load();var player=session.player;
        show("b");session.enqueueTrack(1);compare(session.queue.length,4);compare(session.player,player);
        session.playQueue(3);compare(session.playingTrack.id,"b2");compare(session.shownAlbumId,"b");
        verify(source.records.some(r=>r.id==="a"));verify(source.records.some(r=>r.id==="b"));
        session.next(-1);compare(session.playingTrack.id,"a3");compare(session.playingDisc,1);
    }
    function test_duplicatesHaveDistinctQueueIdentity() {
        show("a");session.enqueueTrack(0);session.enqueueTrack(0);compare(session.queue.length,2);
        verify(session.queue[0].queueId!==session.queue[1].queueId);session.playQueue(1);session.removeQueue(0);compare(session.queueIndex,0);compare(session.playingTrack.id,"a1");
    }
    function test_currentRemovalStartsSuccessorPreservingPause() {
        show("a");session.playTrack(0);load();session.paused=true;
        session.removeQueue(0);compare(session.playingTrack.id,"a2");compare(session.queueIndex,0);verify(session.paused);
        load();verify(session.player.pause);session.removeQueue(1);compare(session.playingTrack.id,"a2");
        session.removeQueue(0);compare(session.queue.length,0);compare(session.player,null);compare(session.sourceState,"idle");compare(session.playingIndex,-1);
    }
    function test_removeLastCurrentSelectsRemainingTail() {
        show("a");session.playTrack(0);session.playQueue(2);session.removeQueue(2);
        compare(session.queueIndex,1);compare(session.playingTrack.id,"a2");
    }
    function test_moveCurrentDoesNotRestartPlayer() {
        show("a");session.playTrack(0);load();var player=session.player;
        session.moveQueue(0,2);compare(session.queueIndex,2);compare(session.player,player);compare(session.playingTrack.id,"a1");
        session.moveQueue(1,0);compare(session.queueIndex,2);compare(session.player,player);
    }
    function test_modelEofAdvancesAcrossDiscAndMixedRelease() {
        show("a");session.playTrack(1);show("b");session.enqueueTrack(0);load();
        session.player.endFile("eof");compare(session.playingTrack.id,"a3");load();session.player.endFile("eof");compare(session.playingTrack.id,"b1");
        load();session.player.endFile("eof");compare(session.sourceState,"ended");verify(session.paused);compare(session.position,120);
    }
    function test_modelRepeatOneOnlyRepeatsNaturalEof() {
        show("a");session.playTrack(0);session.repeatMode="one";load();var old=session.player;
        old.endFile("eof");compare(session.playingTrack.id,"a1");compare(session.queueIndex,0);verify(session.player!==old);
        session.next(1);compare(session.playingTrack.id,"a2");
    }
    function test_modelRepeatAllAndBoundaries() {
        show("a");session.playTrack(0);session.next(-1);compare(session.queueIndex,0);verify(!session.paused);
        session.repeatMode="all";session.next(-1);compare(session.queueIndex,2);load();session.player.endFile("eof");compare(session.queueIndex,0);
        session.repeatMode="off";session.playQueue(2);load();var player=session.player;session.next(1);compare(session.player,player);verify(!session.paused);
    }
    function test_shuffleKeepsCurrentAndPermutationAndRestoresOrder() {
        show("a");session.playTrack(0);load();var player=session.player, id=session.queue[0].queueId;
        session.setShuffle(true);compare(session.player,player);compare(session.queue[session.queueIndex].queueId,id);
        compare(session.queue.map(r=>r.trackId).sort().join(","),"a1,a2,a3");
        session.setShuffle(false);compare(session.queue.map(r=>r.trackId).join(","),"a1,a2,a3");compare(session.player,player);
    }
    function test_shuffleRemoveAppendAndReorderRemainValid() {
        show("a");session.playTrack(0);session.setShuffle(true);session.removeQueue(1);show("b");session.enqueueTrack(0,true);
        compare(session.queue[1].trackId,"b1");session.moveQueue(1,2);session.setShuffle(false);
        compare(session.queue.length,3);compare(session.queue[session.queueIndex].trackId,session.playingTrack.id);
    }
    function test_clearInvalidatesPendingAndCancelCallback() {
        show("a");session.playTrack(0);var request=session.playRequest;source.completeOnCancel=true;session.clearQueue();
        compare(session.player,null);source.completed("play",request,{trackId:"a1",url:"file:///late.wav"},"");compare(session.player,null);
    }
    function test_consumedResolveCallbackCannotRecreatePlayer() {
        show("a");session.playTrack(0);var request=session.playRequest;load();var player=session.player;
        source.completed("play",request,{trackId:"a1",url:"file:///again.wav"},"");compare(session.player,player);
    }
    function test_malformedBrowseDoesNotReplaceValidAlbum() {
        show("a");session.openAlbum("bad");source.completed("browse",session.browseRequest,{id:"bad",tracks:[null]},"");
        compare(session.shownAlbumId,"a");verify(session.browseError.length>0);
    }
    function test_queueRequestCancelledByProfile() {
        show("a");session.enqueueAlbum("b");var request=session.queueRequest;session.clearProfile();
        source.completed("queue",request,album("b"),"");compare(session.queue.length,0);compare(session.shownDisc,0);compare(session.playingDisc,0);
    }
    function test_rapidAlbumEnqueuePreservesBothCommands() {
        show("a");session.enqueueAlbum("b");var first=session.queueRequest;session.enqueueAlbum("c");
        compare(session.queueRequest,first);compare(session.pendingEnqueues.length,1);
        source.completed("queue",first,album("b"),"");compare(session.queue.length,3);verify(session.queueRequest>first);
        source.completed("queue",session.queueRequest,album("c"),"");compare(session.queue.length,6);
        compare(session.queue[0].albumId,"b");compare(session.queue[3].albumId,"c");compare(session.pendingEnqueues.length,0);
    }
    function test_failedEnqueueContinuesAndClearCancelsAll() {
        show("a");session.enqueueAlbum("bad");session.enqueueAlbum("b");source.completed("queue",session.queueRequest,null,"Missing release");
        verify(session.queueRequest>=0);session.enqueueAlbum("c");var pending=session.queueRequest;session.clearQueue();
        compare(session.pendingEnqueues.length,0);source.completed("queue",pending,album("b"),"");compare(session.queue.length,0);
    }
    function test_invalidIndexesAreNoOps() {
        show("a");session.playTrack(0);var id=session.playingTrack.id;
        session.playQueue(NaN);session.removeQueue(0.5);session.moveQueue(0,999);session.selectDisc(-1);session.playTrack("1");
        compare(session.playingTrack.id,id);compare(session.queue.length,3);compare(session.shownDisc,0);
    }
    function test_editionsMetadataAndNoPlaybackMutation() {
        var a=album("a");a.notes="Album notes";a.credits=["Engineer"];a.editions=[{id:"standard",title:"Standard",tracks:a.tracks},{id:"deluxe",title:"Deluxe",tracks:[track("bonus",2)]}];
        a.tracks[0].lyrics="First line\nSecond line";a.tracks[0].notes="Track notes";
        session.shownEdition=session.normalize(a);session.playTrack(0);load();var player=session.player;
        verify(session.hasLyrics);compare(session.lyrics.length,2);verify(!session.timedLyrics);verify(session.hasNotes);compare(session.playingCredits[0].names[0],"Engineer");
        session.selectEdition("deluxe");compare(session.shownEditionId,"deluxe");compare(session.shownAlbumId,"a");compare(session.playingEditionId,"standard");compare(session.player,player);verify(session.sectionVisible("editions"));
    }
    function test_explicitEditionPlaybackKeepsBrowseIdentity() {
        show("browse");var a=album("a");a.editions=[{id:"standard",tracks:a.tracks},{id:"deluxe",tracks:[track("bonus",2)]}];
        session.playAlbum(a,0,"deluxe");compare(session.playingTrack.id,"bonus");compare(session.playingEditionId,"deluxe");compare(session.shownAlbumId,"browse");
        session.playAlbum(a,0,"missing");compare(session.playingTrack.id,"bonus");
    }
    function test_repeatedRecordingUsesOccurrenceIndex() {
        var a=album("a");a.tracks=[track("same",1),track("same",2)];a.tracks[1].title="Disc two performance";
        session.playSongs([{album:a,index:1,track:a.tracks[1]}],false);
        compare(session.playingDisc,1);compare(session.playingTrack.title,"Disc two performance");
    }
    function test_artistSelectionPreservesBrowse() {
        show("a");session.playSongs([{album:album("b"),track:track("b3",2)},{album:album("c"),index:0}],false);
        compare(session.shownAlbumId,"a");compare(session.queue.length,2);compare(session.playingTrack.id,"b3");session.next(1);compare(session.playingTrack.id,"c1");
    }
}
