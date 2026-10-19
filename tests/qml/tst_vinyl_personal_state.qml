import QtQuick
import QtTest
import "../../qml/vinyl"
TestCase {
 name:"VinylPersonalState"; when:windowShown
 width:1280;height:720;visible:true
 QtObject {
  id: store
  property int revision:0
  property bool healthy:true
  property string persistenceError:""
  property var entries:[]
  property bool fail:false
  function has(world,id){return entries.some(function(e){return e.id===id;});}
  function setVinylArtistSaved(artist,saved){if(fail){persistenceError="Disk unavailable";return false;}entries=entries.filter(function(e){return e.id!==artist.id;});if(saved)entries=entries.concat([{id:artist.id,type:"artist"}]);revision++;return true;}
  function vinylPlaylists(id){return entries.filter(function(e){return e.type==="playlist"&&e.payload.artistId===id;});}
  function createVinylPlaylist(title,id,refs){if(fail){persistenceError="Disk unavailable";return "";}entries=entries.concat([{id:"playlist:1",type:"playlist",title:title,payload:{artistId:id,occurrences:refs}}]);revision++;return "playlist:1";}
 }
 QtObject {
  id: prefs
  property bool vinylTasteAnswered:false
  property var vinylTasteArtistIds:[]
  property string vinylPersistenceError:""
  property bool fail:false
  signal vinylTasteChanged()
  function setVinylTaste(answered,ids){if(fail){vinylPersistenceError="Disk unavailable";return false;}vinylTasteAnswered=answered;vinylTasteArtistIds=ids;vinylTasteChanged();return true;}
 }
 QtObject {id: session;property var library:[];property var collectionStore:store;property int playingIndex:-1;property var playingTrack:({});property var playingEdition:({});property string error:"";property string browseError:"";signal profileGenerationChanged()}
 VinylLiveArtistData{id:artist;session:session}
 Rectangle{id:backdrop;anchors.fill:parent;color:"#101015"}
 VinylArtistPage{id:page;anchors.fill:parent;artistData:artist;worldData:world;backdrop:backdrop}
 VinylLiveWorldData{id:world;session:session;preferencesStore:prefs;searchHistoryStore:null}
 function init(){store.entries=[];store.revision++;store.fail=false;store.persistenceError="";prefs.fail=false;prefs.vinylPersistenceError="";prefs.setVinylTaste(false,[]);world.preferencesStore=prefs;artist.personalError="";session.library=[{id:"album:a",title:"Synthetic album",artists:["Synthetic artist"],artistIds:["artist:a"],artist:{id:"artist:a",name:"Synthetic artist"},tracks:[{id:"same",title:"First",disc:1},{id:"same",title:"Second",disc:1}],origin:"local"}];artist.open({id:"artist:a",name:"Synthetic artist"});}
 function test_createAndReopenThroughControls(){
  var more=findChild(page,"vinylArtistMoreButton");verify(more);more.forceActiveFocus();keyClick(Qt.Key_Space);
  var create=findChild(page,"vinylArtistCreatePlaylistMenu");verify(create.visible);create.forceActiveFocus();keyClick(Qt.Key_Space);wait(0);
  var name=findChild(page,"vinylArtistPlaylistName");tryCompare(name,"activeFocus",true);keyClick(Qt.Key_A);
  var confirm=findChild(page,"vinylArtistPlaylistConfirm");verify(confirm);confirm.forceActiveFocus();keyClick(Qt.Key_Space);wait(0);
  compare(artist.playlists.length,1);compare(artist.playlists[0].name,"a");
  var songs=findChild(page,"vinylArtistPlaylistSongsList");compare(songs.count,2);
  keyClick(Qt.Key_Escape);wait(0);more.forceActiveFocus();keyClick(Qt.Key_Space);var savedMenu=findChild(page,"vinylArtistSavedPlaylistsMenu");savedMenu.forceActiveFocus();keyClick(Qt.Key_Space);wait(0);
  var list=findChild(page,"vinylArtistSavedPlaylistsList");compare(list.count,1);tryCompare(findChild(page,"vinylArtistSavedPlaylistsDialog"),"visible",true);verify(list.width>0 && list.height>0,"list geometry "+list.width+" x "+list.height);
  tryVerify(function(){return list.itemAtIndex(0)!==null;});compare(list.itemAtIndex(0).objectName,"vinylArtistSavedPlaylist_0");list.itemAtIndex(0).forceActiveFocus();keyClick(Qt.Key_Space);wait(0);compare(songs.count,2);keyClick(Qt.Key_Escape);
 }
 function test_saveFailurePreservesFacts(){verify(artist.toggleSaved());verify(artist.saved);store.fail=true;verify(!artist.toggleSaved());verify(artist.saved);compare(artist.persistenceError,"Disk unavailable");}
 function test_failedPlaylistKeepsDialogAndInput(){
  store.fail=true;
  var more=findChild(page,"vinylArtistMoreButton");more.forceActiveFocus();keyClick(Qt.Key_Space);
  var create=findChild(page,"vinylArtistCreatePlaylistMenu");create.forceActiveFocus();keyClick(Qt.Key_Space);
  var name=findChild(page,"vinylArtistPlaylistName");tryCompare(name,"activeFocus",true);keyClick(Qt.Key_B);
  var confirm=findChild(page,"vinylArtistPlaylistConfirm");confirm.forceActiveFocus();keyClick(Qt.Key_Space);
  compare(artist.playlists.length,0);tryCompare(findChild(page,"vinylArtistCreatePlaylistDialog"),"visible",true);
  compare(name.text,"b");compare(findChild(page,"vinylArtistPlaylistError").text,"Disk unavailable");
  keyClick(Qt.Key_Escape);wait(0);
 }
 function test_orderedPlaylistAndMissingSource(){verify(artist.createPlaylist("Ordered"));compare(artist.playlists.length,1);compare(artist.playlists[0].songs[1].track.title,"Second");compare(artist.playlists[0].songs[1].trackIndex,1);session.library=[];compare(artist.playlists[0].songs.length,2);verify(artist.playlists[0].songs[0].unavailable);}
 function test_tasteFailureAndProfileReplacement(){verify(world.answerPicker(["artist:a"]));compare(world.picks[0],"artist:a");prefs.fail=true;verify(!world.resetPicker());compare(world.picks[0],"artist:a");world.preferencesStore=null;compare(world.picks.length,0);verify(!world.pickerAnswered);}
}
