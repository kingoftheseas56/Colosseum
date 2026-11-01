import QtQuick
import "../../qml"

// Run with the actual executable and an isolated COLOSSEUM_APPDATA_TAG.
// Arguments: tagged music folder, evidence directory, optional "restore".
Main {
    id: root
    property string stage: "profile"
    property int ticks: 0
    property var album: null
    property string playlistId: ""
    property string duplicateId: ""
    property string savedId: ""
    function item(parent,name) {
        if(!parent)return null;
        if(parent.objectName===name)return parent;
        var children=parent.children || [];
        for(var i=0;i<children.length;i++){var found=item(children[i],name);if(found)return found;}
        return null;
    }
    function control(name){return item(root.contentItem,name);}
    function activate(name){
        if(/^vinylSong(Save|Playlist|Next|Queue|Play)$/.test(name))control("vinylBarSongActions").openActionMenu();
        var c=control(name);if(!check(c && c.visible && c.enabled,"Unavailable control: "+name))return false;
        if(typeof c.activate==="function")c.activate(Qt.MouseFocusReason);else if(typeof c.triggered==="function")c.triggered();else return check(false,"No action handler: "+name);return true;
    }
    function next(value){stage=value;ticks=0;console.log("VINYL_PERSONAL_STAGE",value);}
    function check(ok,message){if(!ok){console.error("VINYL_PERSONAL_FAIL",stage,message);Qt.exit(12);}return !!ok;}
    function capture(name,nextStage){next("capture");contentItem.grabToImage(function(result){result.saveToFile(Qt.application.arguments[3]+"/"+name+".png");root.next(nextStage);});}
    Timer {
        interval:180;repeat:true;running:true
        onTriggered: {
            if(++root.ticks>500){root.check(false,"Stage timeout");return;}
            if(root.stage==="profile") {
                if(!root.visible || AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("start");return;
            }
            var w=root.vinylWorld;if(!w)return;
            var s=w.session,p=w.personalLibrary,v=w.worldView;
            if(!p)return;
            if(root.stage==="start") {
                w.worldData.skipPicker();
                if(Qt.application.arguments[4]==="inspect") {
                    v.setTab("library");
                    if(p.playlists.length)p.openPlaylist(p.playlists[0].id);else p.setSection("songs");
                    v.parkPage(Math.max(0,v.contentTop-v.dockedInset));root.next("finish");return;
                }
                if(Qt.application.arguments[4]==="restore") {root.next("restore");return;}
                if(Qt.application.arguments[4]==="remote") {
                    w.enterDiscoverSearch(true,false);w.setQuery("Nas Illmatic");w.submitSearch();root.next("remote-search");return;
                }
                w.importLocalFiles([Qt.application.arguments[2]]);root.next("import");return;
            }
            if(root.stage==="remote-search") {
                if(s.browsing)return;
                var remote=w.worldData.discoverAlbums.find(a=>a.title.toLowerCase().indexOf("illmatic")>=0 && (a.artists || []).indexOf("Nas")>=0);
                if(!root.check(remote,"Real YouTube Music album search failed: "+s.browseError))return;
                w.openAlbum(remote.id);root.next("remote-album");return;
            }
            if(root.stage==="remote-album") {
                if(s.browsing)return;
                if(!root.check(w.page==="album" && s.tracks.length>=2 && s.shownEdition.origin==="youtube-music","Real catalogue album did not open"))return;
                root.activate("vinylTrackSave_1");root.activate("vinylTrackPlaylist_1");root.next("remote-picker");return;
            }
            if(root.stage==="remote-picker" && root.ticks>2) {
                root.control("vinylPlaylistPickerName").text="Remote catalogue proof";
                root.activate("vinylPlaylistPickerCreate");root.next("remote-library");return;
            }
            if(root.stage==="remote-library" && root.ticks>3) {
                w.goBack();v.setTab("library");
                var created=p.playlists.find(i=>i.name==="Remote catalogue proof");
                if(!root.check(created && p.savedSongs.length===1 && p.albums.length===0,"Remote save membership is incorrect"))return;
                root.playlistId=created.id;p.openPlaylist(created.id);
                v.parkPage(Math.max(0,v.contentTop-v.dockedInset));root.next("remote-play");return;
            }
            if(root.stage==="remote-play" && root.ticks>3) {
                if(!root.check(p.selectedPlaylist.items.length===1 && !p.selectedPlaylist.items[0].unavailable,"Remote playlist hydration failed"))return;
                root.activate("vinylPlaylistPlay");root.next("remote-playing");return;
            }
            if(root.stage==="remote-playing") {
                if(s.error){root.check(false,s.error);return;}
                if(!s.loaded || s.position<0.5)return;
                if(!root.check(s.playingTrack.id===p.selectedPlaylist.items[0].row.trackId && ["youtube","torrent"].indexOf(s.sourceKind)>=0,"Remote playlist played the wrong identity/source"))return;
                console.log("VINYL_PERSONAL_REMOTE",JSON.stringify({title:s.playingTrack.title,artist:s.playingTrack.artist,source:s.sourceKind,position:s.position}));
                s.paused=true;root.capture("remote-playlist","finish");return;
            }
            if(root.stage==="import") {
                if(s.importing)return;
                root.album=s.library.map(e=>e.payload.album).find(a=>a.title.indexOf("Illmatic")>=0);
                if(!root.check(root.album && root.album.tracks.length>=10,"Tagged album import failed: "+w.importStatus))return;
                w.openAlbum(root.album.id);root.next("save");return;
            }
            if(root.stage==="save" && root.ticks>3) {
                if(!root.activate("vinylTrackSave_1"))return;
                if(!root.check(p.savedSongs.length===1,"Album row save did not reach Collection"))return;
                root.savedId=p.savedSongs[0].id;
                w.showBrowseSong(root.album,2,s.shownEditionId);root.next("song-save");return;
            }
            if(root.stage==="song-save" && root.ticks>3) {
                if(!root.activate("vinylSongSave"))return;
                if(!root.check(p.savedSongs.length===2,"Song page save failed"))return;
                w.goBack();w.goBack();v.setTab("library");p.setSection("playlists");root.next("create");return;
            }
            if(root.stage==="create" && root.ticks>3) {
                if(!root.activate("vinylLibraryCreatePlaylist"))return;
                root.next("create-dialog");return;
            }
            if(root.stage==="create-dialog" && root.ticks>2) {
                var field=root.control("vinylLibraryPlaylistName");
                if(!root.check(field && field.visible,"Create dialog missing"))return;
                field.text="Slice 2 mix";root.activate("vinylLibraryPlaylistConfirm");root.next("created");return;
            }
            if(root.stage==="created" && root.ticks>3) {
                if(!root.check(p.playlists.length===1 && p.selectedPlaylist.items.length===0,"Empty playlist was not created"))return;
                root.playlistId=p.selectedPlaylistId;
                root.activate("vinylPlaylistAdd");root.next("add");return;
            }
            if(root.stage==="add" && root.ticks>2) {
                root.activate("vinylPlaylistAvailableSong_1");root.next("add-duplicate");return;
            }
            if(root.stage==="add-duplicate" && root.ticks>2) {
                root.activate("vinylPlaylistAvailableSong_1");root.next("duplicates");return;
            }
            if(root.stage==="duplicates" && root.ticks>2) {
                var refs=p.selectedPlaylist.items;
                if(!root.check(refs.length===2 && refs[0].itemId!==refs[1].itemId,"Duplicate occurrence identity lost"))return;
                root.duplicateId=refs[1].itemId;
                root.activate("vinylPlaylistMoveUp_1");root.next("moved");return;
            }
            if(root.stage==="moved" && root.ticks>2) {
                if(!root.check(p.selectedPlaylist.items[0].itemId===root.duplicateId,"Reordering moved wrong duplicate"))return;
                root.activate("vinylPlaylistRemove_1");root.next("removed");return;
            }
            if(root.stage==="removed" && root.ticks>2) {
                if(!root.check(p.selectedPlaylist.items.length===1 && p.selectedPlaylist.items[0].itemId===root.duplicateId,"Removing targeted wrong duplicate"))return;
                root.activate("vinylPlaylistRename");root.next("rename");return;
            }
            if(root.stage==="rename" && root.ticks>2) {
                root.control("vinylLibraryPlaylistName").text="Slice 2 persisted";
                root.activate("vinylLibraryPlaylistConfirm");root.next("renamed");return;
            }
            if(root.stage==="renamed" && root.ticks>2) {
                if(!root.check(p.selectedPlaylist.name==="Slice 2 persisted","Rename did not persist"))return;
                root.activate("vinylPlaylistBack");root.next("back-list");return;
            }
            if(root.stage==="back-list" && root.ticks>2) {
                if(!root.check(!p.selectedPlaylistId,"Playlist Back did not close detail"))return;
                w.openAlbum(root.album.id);root.next("picker");return;
            }
            if(root.stage==="picker" && root.ticks>2) {
                root.activate("vinylTrackPlaylist_2");root.next("pick-playlist");return;
            }
            if(root.stage==="pick-playlist" && root.ticks>2) {
                root.activate("vinylPlaylistPickerPlaylist_0");root.next("picker-done");return;
            }
            if(root.stage==="picker-done" && root.ticks>2) {
                if(!root.check(!p.pendingRows.length,"Picker did not clear pending selection"))return;
                w.goBack();p.openPlaylist(root.playlistId);root.next("playlist-play");return;
            }
            if(root.stage==="playlist-play" && root.ticks>3) {
                if(!root.check(p.selectedPlaylist.items.length===2,"Album picker did not append track"))return;
                v.parkPage(Math.max(0,v.contentTop-v.dockedInset));root.activate("vinylPlaylistPlay");root.next("playing");return;
            }
            if(root.stage==="playing") {
                if(s.error){root.check(false,s.error);return;}
                if(!s.loaded || s.position<0.6)return;
                if(!root.check(s.queue.length===2 && s.sourceKind==="local" && s.playingTrack.id===p.selectedPlaylist.items[0].row.trackId,"Playlist did not use existing local player"))return;
                root.capture("playlist-playing","queue");return;
            }
            if(root.stage==="queue") {root.activate("vinylPlaylistQueue");root.next("queued");return;}
            if(root.stage==="queued" && root.ticks>2) {
                if(!root.check(s.queue.length===4,"Playlist queue append failed"))return;
                root.activate("vinylPlaylistShuffle");root.next("shuffled");return;
            }
            if(root.stage==="shuffled") {
                if(!s.loaded || s.position<0.4)return;
                if(!root.check(s.queue.length===2,"Playlist shuffle did not replace queue"))return;
                p.setSection("songs");v.parkPage(Math.max(0,v.contentTop-v.dockedInset));root.next("saved-list");return;
            }
            if(root.stage==="saved-list" && root.ticks>3) {
                root.activate("vinylLibrarySavedSongRemove_0");root.next("unsaved");return;
            }
            if(root.stage==="unsaved" && root.ticks>2) {
                if(!root.check(p.savedSongs.length===1,"Saved song removal failed"))return;
                root.activate("vinylLibraryPlaySavedSongs");root.next("saved-playing");return;
            }
            if(root.stage==="saved-playing") {
                if(!s.loaded || s.position<0.5)return;
                if(!root.check(s.queue.length===1 && s.playingTrack.id===p.savedSongs[0].row.trackId,"Saved songs play failed"))return;
                s.paused=true;root.capture("saved-songs","finish");return;
            }
            if(root.stage==="restore" && root.ticks>5) {
                console.log("VINYL_PERSONAL_RESTORE",JSON.stringify({songs:p.savedSongs.length,playlists:p.playlists.map(i=>i.name),nativeSongCount:s.personalRecords.songs.length,active:s.profileActive}));
                if(!root.check(p.savedSongs.length===1 && p.playlists.length===1 && p.playlists[0].name==="Slice 2 persisted","Restart lost saved songs or playlist"))return;
                root.playlistId=p.playlists[0].id;p.openPlaylist(root.playlistId);
                if(!root.check(p.selectedPlaylist.items.length===2 && p.selectedPlaylist.items.every(i=>!i.unavailable),"Restart lost track references"))return;
                v.parkPage(Math.max(0,v.contentTop-v.dockedInset));root.activate("vinylPlaylistItemPlay_1");root.next("restore-play");return;
            }
            if(root.stage==="restore-play") {
                if(!s.loaded || s.position<0.5)return;
                if(!root.check(s.queueIndex===1 && s.playingTrack.id===p.selectedPlaylist.items[1].row.trackId,"Restored playlist selected item playback failed"))return;
                s.paused=true;root.capture("restart-persisted","delete");return;
            }
            if(root.stage==="delete") {root.activate("vinylPlaylistDelete");root.next("deleted");return;}
            if(root.stage==="deleted" && root.ticks>3) {
                if(!root.check(p.playlists.length===0 && !p.selectedPlaylistId && p.savedSongs.length===1 && s.library.length>0,"Delete affected other library records"))return;
                root.next("finish");return;
            }
            if(root.stage==="finish") {
                console.log("VINYL_PERSONAL_PASS",Qt.application.arguments[4] || "record",JSON.stringify({savedSongs:p.savedSongs.length,playlists:p.playlists.length,position:s.position,source:s.sourceKind}));
                running=false;if(["review","inspect"].indexOf(Qt.application.arguments[4])<0)Qt.quit();
            }
        }
    }
}
