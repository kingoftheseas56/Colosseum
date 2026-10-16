import QtQuick
import QtTest
TestCase {
    id:test
    name:"VinylLiveWorldPages"
    when:windowShown
    property var host
    property var world
    property var collection
    Component {id:storeFactory;QtObject {property var entries:[];function items(world){return entries}function has(world,id){return entries.some(function(e){return e.id===id})}}}
    property var source
    Component {id:windowFactory;Window {width:1280;height:720;visible:true}}
    Component {id:sourceFactory;QtObject {
        property int serial:0
        signal completed(string lane,int requestId,var data,string error)
        signal profileLeaving()
        signal profileEntered()
        signal checkpointRequested()
        signal shuttingDown()
        function request(lane,op,id){return ++serial}
        function cancel(lane){}
        function setAlbums(values){}
    }}
    function record(index,artistId) {return {id:"album-"+index,title:"Album "+("00"+index).slice(-3),origin:"local",artists:["Local Artist"],artistIds:[artistId || "artist-one"],year:2000+index%20,genre:index%2 ? "Folk" : "Jazz",tracks:[{id:"track-"+index,title:"Song "+index,artists:["Local Artist"],seconds:80,lyrics:[{text:"First supplied line"},{text:"Second supplied line"}],notes:{0:[{text:"Supplied note"}]},credits:[{role:"Performer",names:["Local Artist"]}]}]};}
    function seed(count) {var records=[];for(var i=0;i<count;i++)records.push({id:"album-"+i,payload:{album:record(i)}});collection.entries=records;world.session.refreshLibrary();wait(0);return records;}
    function item(name){return findChild(world,name)}
    function init(){host=windowFactory.createObject(test);source=sourceFactory.createObject(test);var c=Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");compare(c.status,Component.Ready,c.errorString());world=c.createObject(host.contentItem,{width:1280,height:720});world.session.source=source;collection=storeFactory.createObject(test);world.session.collectionStore=collection;wait(0);}
    function cleanup(){
        // Detach/destroy the item tree while its Window and injected owners still
        // exist; do not overlap the next case with deferred Window teardown.
        world.destroy();
        wait(0);
        tryCompare(test, "world", null, 1000,
                   "World must finish destruction before Window teardown");
        world=null;
        host.close();host.destroy();
        tryCompare(test, "host", null, 1000,
                   "Window must finish destruction before the next case");
        source.destroy();collection.destroy();wait(0);source=null;collection=null;
    }
    function test_emptyDiscoverHasOneTabBarAndNoEmptyShelf(){
        seed(0);world.worldView.setTab("discover");wait(0);
        var flowing=item("vinylFlowTabs"),docked=item("vinylDockedTabs"),empty=item("vinylDiscoverEmpty"),discover=item("vinylDiscoverPage");
        verify(flowing!==null && docked!==null && empty!==null);
        compare(Number(flowing.visible)+Number(docked.visible),1,"Exactly one tab bar can be painted");
        compare(discover.shelfCount,0);verify(empty.visible);verify(!item("vinylWorldRowIndexSidebar").visible);
        verify(item("vinylShelf_local-albums")===null,"Empty data must not produce a header/See all over the empty message");
        var point=empty.mapToItem(world,0,0);verify(point.y>=world.worldScroll.y,"Empty message lies below world controls");
        world.worldScroll.contentY=0;wait(0);compare(Number(flowing.visible)+Number(docked.visible),1);verify(flowing.visible);
        world.worldScroll.contentY=world.worldView.pickerParkY+1;wait(0);compare(Number(flowing.visible)+Number(docked.visible),1);verify(docked.visible);
        seed(1);wait(0);compare(discover.shelfCount,1);verify(!empty.visible);verify(item("vinylShelf_local-albums")!==null);
    }
    function test_pointerSeeAllThenKeyboardBack(){
        seed(8);world.worldData.skipPicker();world.worldView.setTab("home");wait(0);
        var more=item("vinylHomeSeeAll_local-albums");verify(more!==null);verify(more.visible);
        mouseClick(more);wait(0);compare(world.worldView.listKey,"local-albums");
        var list=item("vinylListPage");var focus=host.activeFocusItem;
        verify(focus!==null && focus.visible,"Opening a list must move focus away from its hidden origin");
        var ancestor=focus,inList=false;while(ancestor){if(ancestor===list){inList=true;break;}ancestor=ancestor.parent;}
        verify(inList,"List receives focus after pointer See all");
        keyClick(Qt.Key_Escape);wait(0);compare(world.worldView.listKey,"");verify(more.activeFocus,"Back returns focus to original See all");
    }
    function test_emptySingleLongRefreshed(){
        seed(0);compare(world.worldData.listItems("library").length,0);
        seed(1);compare(world.worldData.listItems("library").length,1);
        seed(80);compare(world.worldData.listItems("library").length,80);
        var list=item("vinylLibraryPage");list.currentIndex=30;list.selectedId="album-30";
        var reversed=world.session.library.slice().reverse();world.session.library=reversed;wait(0);
        compare(list.row.items[list.currentIndex].id,"album-30");
        world.worldData.query="Album 030";compare(world.worldData.listItems("library").length,1);
        world.worldData.query="missing";compare(world.worldData.listItems("library").length,0);
    }
    function test_canonicalArtistsAndTaste(){
        world.session.library=[record(0,"artist-one"),record(1,"artist-two")];wait(0);
        var artists=world.worldData.artistOptions("");compare(artists.length,2);verify(artists[0].id!==artists[1].id);
        world.worldData.answerPicker(["artist-two"]);compare(world.worldData.homeRows[0].items.length,1);compare(world.worldData.homeRows[0].items[0].albumId,"album-1");
        world.activateItem(artists[1]);compare(world.page,"artist");compare(world.artistData.artistId,"artist-two");
    }
    function test_nestedBackRetainsLibraryAndArtist(){
        seed(40);world.worldData.skipPicker();world.worldView.setTab("discover");world.worldData.setLens("genre","Jazz");world.worldData.openList("local-albums",false);wait(0);
        world.worldScroll.contentY=world.worldView.contentTop+100;var y=world.worldScroll.contentY;
        world.showArtist({name:"Local Artist",id:"artist-one"});world.artistData.activeTab="singles";
        world.openAlbum("album-2");source.completed("browse",world.session.browseRequest,record(2),"");compare(world.page,"album");
        world.showBrowseSong(record(2),0);compare(world.page,"song");verify(world.session.playingIndex<0,"Browsing a song must not start playback");
        verify(world.goBack());compare(world.page,"album");verify(world.goBack());compare(world.page,"artist");compare(world.artistData.activeTab,"singles");
        verify(world.goBack());compare(world.page,"discover");compare(world.worldView.listKey,"local-albums");compare(world.worldData.lens.genre,"Jazz");wait(0);compare(world.worldScroll.contentY,y);
        verify(world.goBack());compare(world.worldView.listKey,"");
    }
    function test_browsedAlbumBackRestoresOriginal(){
        seed(3);world.session.shownEdition=world.session.normalize(record(0));world.page="album";
        world.openAlbum("album-1");source.completed("browse",world.session.browseRequest,record(1),"");compare(world.session.shownAlbumId,"album-1");
        world.goBack();compare(world.session.shownAlbumId,"album-0");
    }
    function test_playbackIdentityUnaffectedByBrowse(){
        seed(3);world.session.playingEdition=world.session.normalize(record(0));world.session.playingIndex=0;
        world.showBrowseSong(record(2),0);compare(item("vinylSongPage").songTitle,"Song 2");compare(world.session.playingTrack.title,"Song 0");
        world.showSong();compare(item("vinylSongPage").songTitle,"Song 0");world.goBack();compare(item("vinylSongPage").songTitle,"Song 2");
    }
    function test_cardKeyboardAndResize(){
        seed(40);world.worldScroll.contentY=world.worldView.contentTop;wait(0);
        var list=item("vinylLibraryPage");list.focusCurrent();wait(0);keyClick(Qt.Key_Right);wait(0);compare(list.currentIndex,1);
        for(var i=0;i<8;i++)keyClick(Qt.Key_Down);wait(0);verify(list.currentIndex>1);verify(world.worldScroll.contentY>world.worldView.contentTop);
        world.width=900;host.width=900;wait(0);var card=findChild(list,"vinylListCard_0");verify(card.width>=100);verify(card.width<220);
    }
    function test_editionAndRepeatedTrackDetails(){
        var a=record(0);var t=a.tracks[0];
        a.editions=[{id:"standard",title:"Standard",tracks:[t]},{id:"expanded",title:"Expanded",tracks:[Object.assign({},t,{title:"Occurrence one",disc:1}),Object.assign({},t,{title:"Occurrence two",disc:2})]}];
        collection.entries=[{id:a.id,payload:{album:a}}];world.session.refreshLibrary();
        world.session.shownEdition=world.session.normalize(a,a.editions[1]);world.session.shownDisc=1;world.page="album";
        item("vinylAlbumPage").songRequested(0);var song=item("vinylSongPage");
        compare(song.songTitle,"Occurrence two");compare(song.pageData.playingEditionId,"expanded");compare(song.pageData.trackIndex,1);
        song.playRequested();compare(world.session.playingEditionId,"expanded");compare(world.session.playingDisc,1);compare(world.session.playingTrack.title,"Occurrence two");
    }
    function test_rapidDetailsTeardown(){
        seed(2);
        for(var i=0;i<12;i++){world.showBrowseSong(record(i%2),0);item("vinylSongPage").pageData.lyricsShown=true;world.goBack();}
        wait(0);compare(world.page,"library");
    }
    function test_browsedUntimedNotesHaveVisibleFallback(){
        seed(1);world.showBrowseSong(record(0),0);var song=item("vinylSongPage");
        verify(!song.pageData.isCurrent);compare(song.pageData.currentLineIndex,-1);
        item("vinylSongNotes").triggered();wait(0);
        compare(song.pageData.activeNoteLine,0,"Untimed or non-playing browse must use the supplied notes fallback");
        var notes=item("vinylNotesColumn");verify(notes.visible);compare(notes.noteCount,1);
        compare(notes.shownNotes[0].text,"Supplied note");
    }
    function test_canonicalTabTargetAlwaysVisible(){
        seed(0);world.worldView.setTab("discover");wait(0);
        var target=item("vinylTab_library");verify(target!==null);verify(target.visible,"Canonical action must resolve the visible bar");
        var bounds=target.mapToItem(world,0,0);verify(bounds.y>=world.worldScroll.y);
        mouseClick(target);compare(world.page,"library");
        world.worldScroll.contentY=0;wait(0);target=item("vinylTab_discover");verify(target.visible);mouseClick(target);compare(world.page,"discover");
    }
    function test_suppliedDetailsWideAndNarrow(){
        seed(1);world.showBrowseSong(record(0),0);var song=item("vinylSongPage");compare(song.pageData.lyrics.length,2);compare(song.pageData.playingCredits[0].names[0],"Local Artist");
        song.pageData.lyricsShown=true;wait(0);verify(item("vinylLyricLine_0").opacity>0);
        world.width=900;host.width=900;wait(0);verify(item("vinylCompactLyrics").visible);verify(item("vinylCompactLyrics").width>0);
        var notesOnly=record(1);delete notesOnly.tracks[0].lyrics;world.showBrowseSong(notesOnly,0);song.pageData.lyricsShown=true;wait(0);verify(song.pageData.hasNotes);verify(item("vinylCompactLyrics").visible);
    }
}
