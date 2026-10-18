import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/vinyl"

TestCase {
    name: "VinylLiveArtist"
    when: windowShown
    width: 1280; height: 720; visible: true
    QtObject {
        id: session
        property var library: []
        property var playingTrack: ({id:""})
        property var playingEdition: ({id:""})
        property int playingDisc: 0
        property int playingIndex: -1
        property bool paused: true
        property string error: ""
        property string browseError: ""
        property real position: 0
        property real duration: 20
        property var command: null
        function playAlbum(album,index) { command = {album:album,index:index}; playingTrack = album.tracks[index]; playingEdition={id:album.id,albumId:album.id}; playingDisc=0; playingIndex=index; paused = false; }
        function playSongs(songs,shuffle) { command = {songs:songs,shuffle:shuffle}; }
        function next(delta) { command = {delta:delta}; }
        function togglePause() { paused = !paused; }
    }
    VinylLiveArtistData { id: data; session: session }
    QtObject { id: world; function artwork(url) { return url || ""; } }
    Rectangle { id: backdrop; anchors.fill: parent; color: "#101015" }
    VinylArtistPage { id: page; anchors.fill: parent; artistData:data; worldData:world; backdrop:backdrop }
    function album(id,kind,title) {
        return {id:id,origin:"local",title:title || id,kind:kind || "album",artists:["Eminem"],artistIds:["local:artist"],
            artist:{id:"local:artist",name:"Eminem",backdrops:[]},cover:"",year:2024,
            tracks:[{id:id+":track",title:"Track " + id,artists:["Eminem"],available:true,seconds:20}]};
    }
    function init() {
        session.library = []; session.playingTrack = {id:""}; session.paused = true; session.error = ""; session.command = null;
        data.open("Other"); data.open("Eminem"); data.reducedMotion = true;
        page.width = 1280; page.height = 720;
    }
    function test_emptyNeverLoadsCuratedEminem() {
        compare(data.record.releases.length,0); compare(data.singlesRows.length,0);
        compare(data.allDiscographyCards.length,0); compare(data.heroPhotos.length,0); compare(data.liveCards.length,0);
        compare(findChild(page,"vinylArtistPlay").enabled,false);
        compare(findChild(page,"vinylArtistSave").enabled,false);
        verify(findChild(page,"vinylArtistEmptyReleases").visible);
    }
    function test_ownedRecordsTabsFilteringAndRefresh() {
        session.library = [album("a"),album("s","single"),album("v","video"),album("l","live")];
        compare(data.record.releases.length,4); compare(data.allDiscographyCards.length,2);
        compare(data.singleCards.length,1); compare(data.media.videos.length,1); compare(data.liveCards.length,1);
        data.selectReleaseCategory("Single"); compare(data.discographyCards.length,1); compare(data.discographyCards[0].id,"s");
        page.switchTab("videos",true); tryCompare(data,"activeTab","videos");
        var card = findChild(page,"vinylArtistVideoCard_0"); compare(card.record.title,"v");
        session.library = [album("a"),album("s","single"),album("v","video","Changed"),album("l","live")];
        compare(card.record.title,"Changed"); compare(data.activeTab,"videos"); compare(data.categoryFilter,"Single");
        page.switchTab("live",true); compare(data.activeTab,"live");
        page.switchTab("singles",true); compare(data.activeTab,"singles");
    }
    function test_localReleaseLabelsAndAllMediaCounts() {
        var a=album("a"), b=album("b"), single=album("single","single"), video=album("video","video"), live=album("live","live");
        a.tracks=[a.tracks[0],Object.assign({},a.tracks[0],{id:"a2"}),Object.assign({},a.tracks[0],{id:"a3"})];
        b.tracks=[b.tracks[0],Object.assign({},b.tracks[0],{id:"b2"}),Object.assign({},b.tracks[0],{id:"b3"})];
        session.library=[a,b,single,video,live];
        compare(data.record.releases.length,5); compare(data.discographyCards.length,3);
        compare(data.media.videos.length,1); compare(data.liveCards.length,1); compare(data.singlesRows.length,9);
        compare(data.singleCards[0].category,"Single"); compare(data.singleCards[0].subtitle,"2024 · Single");
        compare(data.allDiscographyCards.filter(function(c){return c.id==="single";})[0].subtitle,"2024 · Single");
        compare(data.libraryCards.filter(function(c){return c.id==="video";})[0].subtitle,"2024 · Video");
        compare(data.libraryCards.filter(function(c){return c.id==="live";})[0].subtitle,"2024 · Live");
        compare(data.albumCards[0].subtitle,"2024 · Album");
        data.selectReleaseCategory("Single"); compare(data.discographyCards.length,1);
        compare(findChild(page,"vinylArtistDiscographyCard_0").record.subtitle,"2024 · Single");
    }
    function test_realSessionCommandsAndFailure() {
        session.library = [{payload:{album:album("a")}},album("b")];
        data.queueDemoSong(1); compare(session.command.album.id,"b"); compare(session.command.index,0);
        compare(data.queuedSongIndex,1); compare(data.demoPlaying,true);
        data.toggleDemoPlayback(); compare(session.paused,true); compare(data.demoPlaying,false);
        session.error = "Missing local file"; compare(data.error,session.error);
        data.stepDemoSong(-1); compare(session.command.delta,-1);
        data.shuffleDemoSong(); compare(session.command.songs.length,2); compare(session.command.shuffle,true);
        session.library = []; compare(data.queuedSongIndex,-1); compare(data.demoPlaying,false);
    }
    function test_longCatalogueAndRefreshKeepPresentationState() {
        var records=[];
        for(var i=0;i<85;++i) records.push(album("long-"+i));
        session.library=records; compare(data.allDiscographyCards.length,85);
        data.newestFirst=false; data.activeTab="singles"; page.contentY=120;
        var refreshed=records.slice(); refreshed[40]=album("long-40","album","Replacement"); session.library=refreshed;
        compare(data.allDiscographyCards.length,85); compare(data.songOrder[40].album.title,"Replacement");
        compare(data.activeTab,"singles"); compare(data.newestFirst,false);
        data.open("Eminem"); compare(data.activeTab,"singles");
    }
    function test_sameAudioDifferentOccurrencesAndEditions() {
        var a=album("a");
        a.tracks=[{id:"same",title:"Disc 1 first",disc:1},{id:"same",title:"Disc 1 reprise",disc:1},{id:"same",title:"Disc 2",disc:2}];
        a.editions=[{id:"standard",tracks:a.tracks},{id:"deluxe",tracks:a.tracks}];
        session.library=[a]; session.playingTrack=a.tracks[1];
        session.playingEdition={id:"standard",albumId:"a"}; session.playingDisc=0; session.playingIndex=1;
        compare(data.queuedSongIndex,1);
        session.playingDisc=1; session.playingIndex=0; compare(data.queuedSongIndex,2);
        session.playingEdition={id:"deluxe",albumId:"a"}; compare(data.queuedSongIndex,-1);
        session.playingEdition={id:"standard",albumId:"a"}; session.playingDisc=0; session.playingIndex=0; compare(data.queuedSongIndex,0);
    }
    function test_sameTrackInDifferentReleases() {
        var a=album("a"), b=album("b"); b.tracks=a.tracks;
        session.library=[a,b]; data.queueDemoSong(1); compare(data.queuedSongIndex,1);
        session.playingEdition={id:"unrelated",albumId:"unrelated"}; compare(data.queuedSongIndex,-1);
    }
    function test_identityDoesNotFoldNamesOrInventIds() {
        var a = album("a"); a.artists = ["A.R. Rahman"];
        session.library=[a]; data.open("A. R. Rahman"); compare(data.record.releases.length,0);
        data.open({name:"A.R. Rahman",id:"local:artist"}); compare(data.record.releases.length,1); compare(data.record.id,"local:artist");
    }
    function test_pickerFocusAndLocalPhotoRefresh() {
        var a=album("a"), b=album("b","single");
        var red="data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40"><rect width="40" height="40" fill="red"/></svg>');
        var blue="data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40"><rect width="40" height="40" fill="blue"/></svg>');
        a.artist.backdrops=[red,blue]; session.library=[a,b];
        compare(data.heroPhotos.length,2); compare(page.heroItem.rotationMode,"dual-crossfade");
        data.reducedMotion=false; compare(page.heroItem.slideshowEnabled,true);
        data.reducedMotion=true; compare(page.heroItem.slideshowEnabled,false);
        page.heroItem.setBackdrop("local-1"); compare(data.selectedWallpaper,"local-1"); compare(page.heroItem.rotationMode,"pinned");
        page.switchTab("albums",true); wait(30);
        var picker=findChild(page,"vinylArtistReleasePicker");
        var trigger=findChild(page,"vinylArtistReleasePickerAction");
        trigger.activate(); tryCompare(picker,"open",true);
        keyClick(Qt.Key_Escape); tryCompare(picker,"open",false); tryCompare(trigger,"activeFocus",true);
        picker.choose(2); compare(data.categoryFilter,"Single"); compare(data.discographyCards.length,1);
        var changed=album("a"); changed.artist.backdrops=[blue]; session.library=[changed,b];
        compare(data.heroPhotos.length,1); compare(data.categoryFilter,"Single");
        page.heroItem.setBackdrop("auto"); compare(page.heroItem.rotationMode,"static");
    }
    function test_missingArtworkAndResize() {
        var a=album("a"); a.cover="file:///not-present/vinyl-cover.jpg";
        session.library=[a];
        compare(data.heroPhotos.length,0); compare(page.heroItem.rotationMode,"fallback");
        for (var w of [1280,640]) {
            page.width=w; wait(30);
            verify(page.albumTileWidth > 0); verify(page.bodyWidth <= page.width);
            page.switchTab("singles",true); tryCompare(findChild(page,"vinylArtistTab_singles"),"activeFocus",true);
        }
        session.library=[album("replacement")]; compare(data.songOrder[0].album.id,"replacement");
    }
}
