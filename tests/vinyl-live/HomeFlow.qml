import QtQuick
import "../../qml"

// Real Main and native owners. Arguments: tagged music folder, evidence dir,
// record|restore|remote|inspect. Use a disposable COLOSSEUM_APPDATA_TAG.
Main {
    id: root
    property string stage:"profile"
    property int ticks:0
    property var album:null
    property string playlistId:""
    property var queueIds:[]
    property real previousPosition:0
    function item(parent,name) {
        if(!parent)return null;if(parent.objectName===name)return parent;
        var children=parent.children || [];
        for(var i=0;i<children.length;i++){var found=item(children[i],name);if(found)return found;}
        return null;
    }
    function control(name){return item(root.contentItem,name);}
    function check(ok,message){if(!ok){console.error("VINYL_HOME_FAIL",stage,message);Qt.exit(14);}return !!ok;}
    function next(value){stage=value;ticks=0;console.log("VINYL_HOME_STAGE",value);}
    function activate(name) {
        var c=control(name);if(!check(c && c.visible && c.enabled,"Missing action: "+name))return false;
        if(root.vinylWorld.worldVisible && name.indexOf("vinylCard_")===0) {
            var y=c.mapToItem(root.vinylWorld.worldView,0,0).y;
            root.vinylWorld.worldView.parkPage(Math.max(0,y-root.vinylWorld.worldView.dockedInset-12));
        }
        c.activate(Qt.MouseFocusReason);return true;
    }
    function row(key){return root.vinylWorld.worldData.homeRows.find(r=>r.key===key);}
    function capture(name,nextStage){next("capture");contentItem.grabToImage(function(result){result.saveToFile(Qt.application.arguments[3]+"/"+name+".png");root.next(nextStage);});}
    Timer {
        interval:180;repeat:true;running:true
        onTriggered: {
            if(++root.ticks>600){root.check(false,"Stage timeout");return;}
            if(root.stage==="profile") {
                if(!root.visible || AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("start");return;
            }
            var w=root.vinylWorld;if(!w)return;
            var s=w.session,p=w.personalLibrary,d=w.worldData,v=w.worldView;
            var phase=Qt.application.arguments[4] || "record";
            if(root.stage==="start" && root.ticks>4) {
                if(!root.check(w.page==="home" && v.currentTab==="home","Actual app did not land on Home"))return;
                if(phase==="inspect"){if(root.ticks<14)return;root.capture("home-review","finish");return;}
                if(phase==="restore"){root.next("restore");return;}
                if(phase==="remote"){root.next("remote");return;}
                ProfilePreferences.keepActivityHistory=false;
                if(!root.check(!d.pickerAnswered && !d.tastePickerOpen && root.control("vinylHomeEmpty").visible,"Fresh Home was gated by taste"))return;
                root.capture("home-fresh","cold-search");return;
            }
            if(root.stage==="cold-search") {root.activate("vinylHomeSearch");root.next("search");return;}
            if(root.stage==="search" && root.ticks>3) {
                if(!root.check(w.page==="discover" && root.control("vinylSearchInput").activeFocus,"Cold Home search is not usable"))return;
                w.goBack();root.next("search-back");return;
            }
            if(root.stage==="search-back" && root.ticks>3) {
                if(!root.check(w.page==="home","Search Back lost Home"))return;
                root.activate("vinylHomeLibrary");root.next("cold-library");return;
            }
            if(root.stage==="cold-library" && root.ticks>3) {
                if(!root.check(w.page==="library" && p.section==="albums","Cold Home Library action failed"))return;
                if(!root.activate("vinylLibraryImportFolder"))return;
                root.next("folder-dialog");return;
            }
            if(root.stage==="folder-dialog" && root.ticks>3) {
                var dialog=null;
                for(var f=0;f<w.data.length;f++)if(w.data[f].objectName==="vinylOpenFolderDialog")dialog=w.data[f];
                if(!root.check(dialog && dialog.visible,"Library did not open the existing folder picker"))return;
                // Exercise the real Main acceptance/import handler with a known
                // fixture folder. This does not automate the native OS picker.
                dialog.selectedFolder=encodeURI("file:///"+Qt.application.arguments[2].replace(/\\/g,"/")).replace(/#/g,"%23").replace(/\?/g,"%3F");
                dialog.accepted();dialog.close();root.next("import");return;
            }
            if(root.stage==="import") {
                if(s.importing)return;
                root.album=s.library.map(e=>e.payload.album).find(a=>a.title.indexOf("Illmatic")>=0);
                if(!root.check(root.album && root.album.tracks.length>=10,"Tagged import failed"))return;
                w.showHome();w.openAlbum(root.album.id);root.next("save");return;
            }
            if(root.stage==="save" && root.ticks>4) {
                root.activate("vinylTrackSave_1");root.activate("vinylTrackPlaylist_1");root.next("picker");return;
            }
            if(root.stage==="picker" && root.ticks>3) {
                root.control("vinylPlaylistPickerName").text="Home flow playlist";
                root.activate("vinylPlaylistPickerCreate");root.next("artist");return;
            }
            if(root.stage==="artist" && root.ticks>3) {
                if(!root.check(p.savedSongs.length===1 && p.playlists.length===1,"Prerequisite save/playlist creation failed"))return;
                root.playlistId=p.playlists[0].id;w.showRecordArtist("Nas",root.album);root.next("artist-save");return;
            }
            if(root.stage==="artist-save" && root.ticks>4) {
                if(!root.check(w.artistData.toggleSaved(),"Artist save failed"))return;
                w.goBack();w.goBack();root.next("home-saved");return;
            }
            if(root.stage==="home-saved" && root.ticks>3) {
                console.log("VINYL_HOME_SAVED",JSON.stringify({page:w.page,rows:d.homeRows.map(r=>r.key),counts:[p.homeCollections.songs.length,p.homeCollections.albums.length,p.homeCollections.artists.length,p.homeCollections.playlists.length],routes:w.routeStack.map(r=>r.page)}));
                if(!root.check(w.page==="home" && ["saved-songs","saved-albums","saved-artists","playlists"].every(k=>root.row(k) && root.row(k).items.length),"Saved Home shelves missing"))return;
                root.capture("home-saved","album-card");return;
            }
            if(root.stage==="album-card") {root.activate("vinylCard_saved-albums_0");root.next("album");return;}
            if(root.stage==="album" && root.ticks>4) {
                if(s.browsing)return;
                if(!root.check(w.page==="album" && s.shownAlbumId===root.album.id,"Home album route is incorrect"))return;
                w.goBack();root.next("song-card");return;
            }
            if(root.stage==="song-card" && root.ticks>3) {root.activate("vinylCard_saved-songs_0");root.next("song");return;}
            if(root.stage==="song" && root.ticks>3) {
                if(!root.check(w.page==="song" && s.queue.length===0,"Home song browse changed playback"))return;
                w.goBack();root.next("artist-card");return;
            }
            if(root.stage==="artist-card" && root.ticks>3) {root.activate("vinylCard_saved-artists_0");root.next("artist-route");return;}
            if(root.stage==="artist-route" && root.ticks>3) {
                if(!root.check(w.page==="artist" && w.artistData.name==="Nas","Home artist route failed"))return;
                w.goBack();root.next("playlist-card");return;
            }
            if(root.stage==="playlist-card" && root.ticks>3) {root.activate("vinylCard_playlists_0");root.next("playlist");return;}
            if(root.stage==="playlist" && root.ticks>3) {
                if(!root.check(w.page==="library" && p.selectedPlaylistId===root.playlistId,"Home playlist opened wrong detail"))return;
                root.activate("vinylPlaylistAdd");root.next("playlist-add");return;
            }
            if(root.stage==="playlist-add" && root.ticks>2) {root.activate("vinylPlaylistAvailableSong_2");root.next("playlist-play");return;}
            if(root.stage==="playlist-play" && root.ticks>2) {root.activate("vinylPlaylistItemPlay_1");root.next("playing");return;}
            if(root.stage==="playing") {
                if(s.error){root.check(false,s.error);return;}if(!s.loaded || s.position<1)return;
                if(!root.check(s.queue.length===2 && s.queueIndex===1 && s.sourceKind==="local","Home playlist failed to play selected local item"))return;
                root.queueIds=s.queue.map(r=>r.queueId);root.previousPosition=s.position;w.goBack();root.next("continue-card");return;
            }
            if(root.stage==="continue-card" && root.ticks>3) {
                if(!root.check(w.page==="home" && root.row("continue-listening"),"Continue missing after playback"))return;
                root.activate("vinylCard_continue-listening_0");root.next("continuing");return;
            }
            if(root.stage==="continuing" && root.ticks>3) {
                if(!root.check(w.page==="song" && s.queueIndex===1 && s.position>root.previousPosition && JSON.stringify(s.queue.map(r=>r.queueId))===JSON.stringify(root.queueIds),"Continue changed queue/order or restarted track"))return;
                w.goBack();root.next("see-all");return;
            }
            if(root.stage==="see-all" && root.ticks>3) {root.activate("vinylHomeSeeAll_playlists");root.next("playlist-list");return;}
            if(root.stage==="playlist-list" && root.ticks>3) {
                if(!root.check(w.page==="library" && p.section==="playlists" && !p.selectedPlaylistId,"Playlist See all reopened old detail"))return;
                d.setQuery("deliberately-no-match");w.goBack();root.next("filter-independent");return;
            }
            if(root.stage==="filter-independent" && root.ticks>3) {
                if(!root.check(w.page==="home" && root.row("playlists").items.length===1 && root.row("saved-songs").items.length===1,"Library filter altered Home"))return;
                s.paused=true;s.seek(7.25);root.next("checkpoint");return;
            }
            if(root.stage==="checkpoint" && root.ticks>4) {
                if(s.seekPending || Math.abs(s.position-7.25)>0.8)return;
                s.snapshot();w.showHome();root.capture("home-continue","finish");return;
            }
            if(root.stage==="restore" && root.ticks>4) {
                if(!root.check(root.row("continue-listening") && p.savedSongs.length===1 && p.playlists.length===1 && s.queue.length===2 && s.queueIndex===1 && Math.abs(s.position-7.25)<0.8,"Home restart lost queue, position or collections"))return;
                root.queueIds=s.queue.map(r=>r.queueId);root.activate("vinylCard_continue-listening_0");root.next("restored-playing");return;
            }
            if(root.stage==="restored-playing") {
                if(s.error){root.check(false,s.error);return;}if(!s.loaded || s.position<8.5)return;
                if(!root.check(s.queueIndex===1 && s.position<20 && JSON.stringify(s.queue.map(r=>r.queueId))===JSON.stringify(root.queueIds) && !s.activityEnabled,"Restored Continue lost identity/position/history-off behavior"))return;
                s.paused=true;w.goBack();root.capture("home-restart-resume","finish");return;
            }
            if(root.stage==="remote" && root.ticks>4) {
                if(!root.check(root.row("saved-songs") && root.row("playlists"),"Real remote Home data missing"))return;
                root.activate("vinylCard_playlists_0");root.next("remote-detail");return;
            }
            if(root.stage==="remote-detail" && root.ticks>4) {root.activate("vinylPlaylistPlay");root.next("remote-playing");return;}
            if(root.stage==="remote-playing") {
                if(s.error){root.check(false,s.error);return;}if(!s.loaded || s.position<1)return;
                if(!root.check(["youtube","torrent"].indexOf(s.sourceKind)>=0 && s.playingTrack.id===p.selectedPlaylist.items[0].row.trackId,"Home remote playlist identity/source mismatch"))return;
                console.log("VINYL_HOME_REMOTE",JSON.stringify({title:s.playingTrack.title,source:s.sourceKind,position:s.position}));
                s.paused=true;w.goBack();root.capture("home-remote","finish");return;
            }
            if(root.stage==="finish") {
                console.log("VINYL_HOME_PASS",phase,JSON.stringify({rows:d.homeRows.map(r=>r.key),queue:s.queue.map(r=>r.queueId),index:s.queueIndex,position:s.position,source:s.sourceKind,history:s.activityEnabled}));
                running=false;if(phase!=="inspect")Qt.quit();
            }
        }
    }
}
