import QtQuick
import "../../qml"

// Actual Main, native catalogue, stores and player; disposable app data.
// Arguments: unused fixture, evidence directory, live|inspect.
Main {
    id: root
    property string stage: "profile"
    property int ticks: 0
    property var firstIds: []
    property string expectedTrack: ""
    property string expectedTitle: ""
    property string savedTrack: ""
    property string priorQueue: ""
    property string longPlaylist: "6ee9ac10-e1b4-542c-97e9-46fc51c12f5d"
    function item(parent,name) {
        if(!parent)return null;if(parent.objectName===name && parent.visible!==false)return parent;
        var children=parent.children || [];
        for(var i=0;i<children.length;i++){var found=item(children[i],name);if(found)return found;}
        return null;
    }
    function control(name){return item(contentItem,name);}
    function check(ok,message){if(!ok){console.error("VINYL_SEARCH_FAIL",stage,message);Qt.exit(15);}return !!ok;}
    function next(value){stage=value;ticks=0;console.log("VINYL_SEARCH_STAGE",value);}
    function activate(name) {
        var c=name==="vinylListBack" ? item(control("vinylListPage"),name) : control(name);
        if(!check(c && c.visible && c.enabled,"Missing action: "+name))return false;
        var view=vinylWorld.worldView;
        if(name.indexOf("vinylListCard_")===0 || name.indexOf("vinylShelfSeeAll_")===0 || name.indexOf("More")>=0) {
            var y=c.mapToItem(view,0,0).y;
            view.parkPage(Math.max(0,y-view.dockedInset-12));
        }
        if(name.indexOf("vinylTab_")===0) {
            var tabs=c.parent;while(tabs && typeof tabs.requestIndex!=="function")tabs=tabs.parent;
            if(!check(!!tabs,"Missing shared tab action"))return false;
            tabs.requestIndex(c.index,Qt.MouseFocusReason);
        } else if(typeof c.activate==="function")c.activate(Qt.MouseFocusReason);
        else if(typeof c.triggered==="function")c.triggered();
        else return check(false,"Action has no activation handler: "+name);
        return true;
    }
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
            var s=w.session,d=w.worldData,v=w.worldView;
            if(root.stage==="start" && root.ticks>5) {
                if(Qt.application.arguments[4]==="inspect") {
                    root.activate("vinylTab_discover");d.setKind("playlists");d.setQuery("Nas Unreleased Songs");d.submitSearch();root.next("inspect-results");return;
                }
                root.activate("vinylTab_discover");d.setKind("songs");d.setQuery("Nas");d.submitSearch();root.next("songs-first");return;
            }
            if(root.stage==="inspect-results") {
                if(s.browsing)return;
                if(!root.check(s.cataloguePages.playlists && !s.cataloguePages.playlists.error,"Inspection search failed"))return;
                root.activate("vinylShelfSeeAll_search-playlists");root.next("inspect-open");return;
            }
            if(root.stage==="inspect-open" && root.ticks>3) {
                var target=s.cataloguePages.playlists.items.findIndex(i=>i.id===root.longPlaylist);
                if(!root.check(target>=0,"Inspection playlist no longer returned"))return;
                root.activate("vinylListCard_"+target);root.next("inspect-playlist");return;
            }
            if(root.stage==="inspect-playlist" && root.ticks>4) {
                if(s.browsing)return;
                if(!root.check(w.page==="album" && s.shownAlbumId===root.longPlaylist,"Inspection playlist failed to open"))return;
                s.paused=true;root.capture("playlist-open-for-review","finish");return;
            }
            if(root.stage==="songs-first") {
                if(s.browsing)return;
                var page=s.cataloguePages.songs;
                if(!root.check(page && !page.error && page.items.length>0 && Object.keys(page.nextCursor).length,"Song search missing first page: "+JSON.stringify(page)))return;
                root.firstIds=page.items.map(i=>i.id);
                console.log("VINYL_SEARCH_FIRST",JSON.stringify({kind:"songs",count:page.items.length,nextOffset:page.nextOffset}));
                root.activate("vinylShelfSeeAll_search-songs");root.next("songs-more");return;
            }
            if(root.stage==="songs-more" && root.ticks>3) {root.activate("vinylListMore");root.next("songs-appended");return;}
            if(root.stage==="songs-appended") {
                if(s.catalogueRequests.songs)return;
                var songs=s.cataloguePages.songs;
                if(!root.check(!songs.error && songs.items.length>root.firstIds.length && root.firstIds.every((id,i)=>songs.items[i].id===id) && new Set(songs.items.map(i=>i.id)).size===songs.items.length,"Song page append changed identities: "+songs.error))return;
                console.log("VINYL_SEARCH_APPEND",JSON.stringify({kind:"songs",count:songs.items.length,nextOffset:songs.nextOffset}));
                var chosen=songs.items[root.firstIds.length];root.expectedTrack=chosen.trackId;root.expectedTitle=chosen.title;
                root.savedTrack=root.expectedTrack;root.priorQueue=JSON.stringify(s.queue.map(r=>r.queueId));
                root.capture("search-songs-page2","song-open");return;
            }
            if(root.stage==="song-open") {root.activate("vinylListCard_"+root.firstIds.length);root.next("song");return;}
            if(root.stage==="song" && root.ticks>4) {
                if(!root.check(w.page==="song" && w.browsingSong && JSON.stringify(s.queue.map(r=>r.queueId))===root.priorQueue,"Search card replaced queue or opened wrong page"))return;
                if(!w.personalLibrary.savedSongs.some(song=>song.row.trackId===root.expectedTrack))root.activate("vinylSongSave");
                root.activate("vinylSongPlay");root.next("song-playing");return;
            }
            if(root.stage==="song-playing") {
                if(s.error){root.check(false,s.error);return;}if(!s.loaded || s.position<1)return;
                if(!root.check(s.playingTrack.id===root.expectedTrack && s.queue.length===1 && ["youtube","torrent"].indexOf(s.sourceKind)>=0,"Second-page song played wrong recording"))return;
                console.log("VINYL_SEARCH_PLAY",JSON.stringify({title:s.playingTrack.title,expected:root.expectedTitle,track:s.playingTrack.id,source:s.sourceKind,position:s.position}));
                s.paused=true;w.goBack();root.next("song-back");return;
            }
            if(root.stage==="song-back" && root.ticks>4) {
                if(!root.check(v.listKey==="search-songs" && root.control("vinylListPage").itemCount>root.firstIds.length && d.query==="Nas" && d.lens.kind==="songs","Back lost paged song search"))return;
                root.activate("vinylListBack");root.next("playlist-search");return;
            }
            if(root.stage==="playlist-search" && root.ticks>4) {
                root.activate("vinylKind_playlists");d.setQuery("Nas all songs");d.submitSearch();root.next("playlists-first");return;
            }
            if(root.stage==="playlists-first") {
                if(s.browsing)return;
                var playlists=s.cataloguePages.playlists;
                if(!root.check(playlists && !playlists.error && playlists.items.length && Object.keys(playlists.nextCursor).length,"Playlist search first page missing: "+JSON.stringify(playlists)))return;
                root.firstIds=playlists.items.map(i=>i.id);root.activate("vinylShelfSeeAll_search-playlists");root.next("playlists-more");return;
            }
            if(root.stage==="playlists-more" && root.ticks>3) {root.activate("vinylListMore");root.next("playlists-appended");return;}
            if(root.stage==="playlists-appended") {
                if(s.catalogueRequests.playlists)return;
                var values=s.cataloguePages.playlists;
                if(!root.check(!values.error && values.items.length>root.firstIds.length && root.firstIds.every((id,i)=>values.items[i].id===id),"Playlist search append failed: "+values.error))return;
                console.log("VINYL_SEARCH_APPEND",JSON.stringify({kind:"playlists",count:values.items.length,nextOffset:values.nextOffset}));
                root.capture("search-playlists-page2","playlist-target");return;
            }
            if(root.stage==="playlist-target") {
                root.activate("vinylListBack");d.setQuery("Nas Unreleased Songs");d.submitSearch();root.next("playlist-target-results");return;
            }
            if(root.stage==="playlist-target-results") {
                if(s.browsing)return;
                if(!root.check(s.cataloguePages.playlists && !s.cataloguePages.playlists.error,"Target playlist search failed"))return;
                root.activate("vinylShelfSeeAll_search-playlists");root.next("playlist-open");return;
            }
            if(root.stage==="playlist-open" && root.ticks>3) {
                var index=s.cataloguePages.playlists.items.findIndex(i=>i.id===root.longPlaylist);
                if(!root.check(index>=0,"Large public playlist no longer in live search results"))return;
                root.activate("vinylListCard_"+index);root.next("playlist-first");return;
            }
            if(root.stage==="playlist-first" && root.ticks>4) {
                if(s.browsing)return;
                if(!root.check(w.page==="album" && s.shownAlbumId===root.longPlaylist && s.playlistHasMore && s.tracks.length===100,"Large playlist first page failed: "+s.browseError))return;
                root.expectedTrack=s.tracks[0].id;
                console.log("VINYL_PLAYLIST_FIRST",JSON.stringify({loaded:s.tracks.length,total:s.shownEdition.sourceAlbum.totalTracks}));
                root.activate("vinylAlbumPlay");root.next("playlist-playing");return;
            }
            if(root.stage==="playlist-playing") {
                if(s.error || s.browseError){root.check(false,s.error || s.browseError);return;}if(!s.loaded || s.position<1 || s.queue.length<=100)return;
                if(!root.check(s.playingTrack.id===root.expectedTrack && s.playingEdition.sourceAlbum.complete===true && s.queue.length===127,"Partial playlist Play queued a prefix or wrong track"))return;
                console.log("VINYL_PLAYLIST_PLAY",JSON.stringify({queue:s.queue.length,title:s.playingTrack.title,id:s.playingTrack.id,position:s.position,source:s.sourceKind,omitted:s.playingEdition.sourceAlbum.omittedTracks}));
                s.paused=true;root.next("playlist-more");return;
            }
            if(root.stage==="playlist-more" && root.ticks>3) {
                if(s.playlistHasMore){root.activate("vinylPlaylistMore");root.next("playlist-appended");}
                else root.next("playlist-appended");return;
            }
            if(root.stage==="playlist-appended") {
                if(s.playlistPageLoading)return;
                if(!root.check(!s.playlistPageError && s.tracks.length===127 && !s.playlistHasMore && s.shownEdition.sourceAlbum.complete===true && s.shownEdition.sourceAlbum.omittedTracks===4,"Playlist continuation did not exhaust server pages: "+s.playlistPageError))return;
                console.log("VINYL_PLAYLIST_APPEND",JSON.stringify({tracks:s.tracks.length,total:s.shownEdition.sourceAlbum.totalTracks,omitted:s.shownEdition.sourceAlbum.omittedTracks}));
                root.capture("playlist-complete","clear");return;
            }
            if(root.stage==="clear") {w.goBack();root.next("clear-list");return;}
            if(root.stage==="clear-list" && root.ticks>3) {root.activate("vinylListBack");d.setQuery("");root.next("cleared");return;}
            if(root.stage==="cleared" && root.ticks>4) {
                if(s.discoveryLoading)return;
                if(!root.check(d.discoveryLanding && d.shelves.length===4,"Clear query did not return public Discover"))return;
                root.activate("vinylTab_home");root.next("home");return;
            }
            if(root.stage==="home" && root.ticks>4) {
                if(!root.check(w.page==="home" && d.query==="" && w.personalLibrary.savedSongs.some(song=>song.row.trackId===root.savedTrack),"Search state leaked into Home or saved song was lost"))return;
                // Saved song and playlist continue entries are owned by existing stores.
                root.activate("vinylTab_discover");root.next("finish");return;
            }
            if(root.stage==="finish" && root.ticks>4) {
                console.log("VINYL_SEARCH_PASS",Qt.application.arguments[4] || "live");running=false;
                if(Qt.application.arguments[4]!=="inspect")Qt.quit();
            }
        }
    }
}
