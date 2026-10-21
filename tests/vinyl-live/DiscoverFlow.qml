import QtQuick
import "../../qml"

// Real Main/native catalogue/player. Args match HomeFlow: unused fixture,
// evidence directory, live|cached|failure|inspect. Use disposable app data.
Main {
    id: root
    property string stage: "profile"
    property int ticks: 0
    property string expectedId: ""
    property string expectedTrack: ""
    property string previousQueue: ""
    property real startedAt: 0
    function item(parent,name) {
        if(!parent)return null;if(parent.objectName===name)return parent;
        var children=parent.children || [];
        for(var i=0;i<children.length;i++){var found=item(children[i],name);if(found)return found;}
        return null;
    }
    function control(name){return item(contentItem,name);}
    function check(ok,message){if(!ok){console.error("VINYL_DISCOVER_FAIL",stage,message);Qt.exit(14);}return !!ok;}
    function next(value){stage=value;ticks=0;console.log("VINYL_DISCOVER_STAGE",value);}
    function activate(name) {
        var c=name==="vinylListBack" ? item(control("vinylListPage"),name) : control(name);
        if(!check(c && c.visible && c.enabled,"Missing action: "+name))return false;
        if(name.indexOf("vinylCard_")===0 || name.indexOf("vinylShelfSeeAll_")===0) {
            var v=vinylWorld.worldView,y=c.mapToItem(v,0,0).y;
            v.parkPage(Math.max(0,y-v.dockedInset-12));
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
            var s=w.session,d=w.worldData,v=w.worldView,phase=Qt.application.arguments[4] || "live";
            if(root.stage==="start" && root.ticks>5) {
                if(!root.check(w.page==="home","Vinyl did not land on Home"))return;
                root.startedAt=Date.now();root.activate("vinylTab_discover");root.next("landing");return;
            }
            if(root.stage==="landing") {
                if(s.discoveryLoading || !s.discoveryStarted)return;
                if(phase==="inspect" && root.ticks<18)return;
                if(phase==="failure") {
                    if(!root.check(!d.shelves.length && !!d.discoveryError && root.control("vinylDiscoverRetry").visible,"Cold failure was not recoverable"))return;
                    root.activate("vinylDiscoverRetry");root.next("failed-retry");return;
                }
                if(!root.check(d.shelves.length===4 && d.shelves.every(r=>r.items.length && r.source==="youtube-music"),"Public discovery shelves missing: "+d.discoveryError))return;
                console.log("VINYL_DISCOVER_CONTENT",JSON.stringify({ms:Date.now()-root.startedAt,rows:d.shelves.map(r=>({key:r.key,kind:r.kind,count:r.items.length,first:r.items[0].title})),cached:d.discoveryCached}));
                if(phase==="cached") {if(!root.check(d.discoveryCached,"Warm catalogue did not use cache"))return;root.activate("vinylDiscoverRetry");root.next("cached-retry");return;}
                root.capture("discover-"+phase,phase==="inspect" ? "finish" : "album-card");return;
            }
            if(root.stage==="failed-retry" || root.stage==="cached-retry") {
                if(s.discoveryLoading)return;
                if(!root.check(!!d.discoveryError && root.control("vinylDiscoverRetry").visible,"Failed refresh lost Retry"))return;
                if(!root.check(phase!=="cached" || d.shelves.length===4,"Failed refresh erased cached shelves"))return;
                root.capture("discover-"+phase+"-retry","home-safe");return;
            }
            if(root.stage==="home-safe") {
                root.activate("vinylTab_home");
                if(!root.check(w.page==="home" && !s.error,"Discovery error blocked Home"))return;
                root.next("finish");return;
            }
            if(root.stage==="album-card") {root.expectedId=d.row("discover-new-releases",false).items[0].id;root.activate("vinylCard_discover-new-releases_0");root.next("album");return;}
            if(root.stage==="album" && root.ticks>4) {
                if(s.browsing)return;
                if(!root.check(w.page==="album" && s.shownAlbumId===root.expectedId && s.tracks.length>0,"New release opened wrong album: "+s.browseError))return;
                root.capture("discover-album","album-back");return;
            }
            if(root.stage==="album-back") {w.goBack();root.next("artist-card");return;}
            if(root.stage==="artist-card" && root.ticks>3) {root.expectedId=d.row("discover-artists",false).items[0].id;root.activate("vinylCard_discover-artists_0");root.next("artist");return;}
            if(root.stage==="artist" && root.ticks>4) {
                if(!s.source.artist.channelId)return;
                console.log("VINYL_DISCOVER_ARTIST",JSON.stringify({expected:root.expectedId,page:w.page,artistId:w.artistData.artistId,name:s.source.artist.name,channel:s.source.artist.channelId,requested:s.source.artist.requestedChannelId}));
                if(!root.check(w.page==="artist" && w.artistData.artistId===root.expectedId && s.source.artist.requestedChannelId===root.expectedId.slice(11),"Chart artist identity drift"))return;
                w.goBack();root.next("see-all");return;
            }
            if(root.stage==="see-all" && root.ticks>3) {root.activate("vinylShelfSeeAll_discover-charts");root.next("list");return;}
            if(root.stage==="list" && root.ticks>3) {
                if(!root.check(v.listKey==="discover-charts" && control("vinylListPage").itemCount===d.row("discover-charts",false).items.length,"See all changed shelf"))return;
                root.activate("vinylListBack");root.next("chart-card");return;
            }
            if(root.stage==="chart-card" && root.ticks>3) {root.expectedId=d.row("discover-charts",false).items[0].id;root.activate("vinylCard_discover-charts_0");root.next("chart");return;}
            if(root.stage==="chart" && root.ticks>4) {
                if(s.browsing)return;
                if(!root.check(w.page==="album" && s.shownAlbumId===root.expectedId && s.shownEdition.sourceAlbum.type==="Playlist" && s.tracks.length>0,"Public playlist did not open complete listing: "+s.browseError))return;
                root.expectedTrack=s.tracks[0].id;root.activate("vinylAlbumPlay");root.next("chart-playing");return;
            }
            if(root.stage==="chart-playing") {
                if(s.error){root.check(false,s.error);return;}if(!s.loaded || s.position<1)return;
                if(!root.check(s.playingTrack.id===root.expectedTrack && ["youtube","torrent"].indexOf(s.sourceKind)>=0,"Playlist playback changed recording"))return;
                console.log("VINYL_DISCOVER_PLAYLIST",JSON.stringify({track:s.playingTrack.title,id:s.playingTrack.id,source:s.sourceKind,queue:s.queue.length,position:s.position}));
                s.paused=true;root.previousQueue=JSON.stringify(s.queue.map(r=>r.queueId));w.goBack();root.next("song-card");return;
            }
            if(root.stage==="song-card" && root.ticks>3) {root.expectedTrack=d.row("discover-trending",false).items[0].trackId;root.activate("vinylCard_discover-trending_0");root.next("song");return;}
            if(root.stage==="song" && root.ticks>3) {
                if(!root.check(w.page==="song" && w.browsingSong && JSON.stringify(s.queue.map(r=>r.queueId))===root.previousQueue,"Browsing a trending song replaced queue"))return;
                if(!w.personalLibrary.savedSongs.some(song=>song.row && song.row.trackId===root.expectedTrack))root.activate("vinylSongSave");
                root.activate("vinylSongPlay");root.next("song-playing");return;
            }
            if(root.stage==="song-playing") {
                if(s.error){root.check(false,s.error);return;}if(!s.loaded || s.position<1)return;
                if(!root.check(s.playingTrack.id===root.expectedTrack && s.queue.length===1 && w.personalLibrary.savedSongs.some(song=>song.row.trackId===root.expectedTrack),"Trending song playback/save identity mismatch"))return;
                console.log("VINYL_DISCOVER_SONG",JSON.stringify({track:s.playingTrack.title,id:s.playingTrack.id,source:s.sourceKind,position:s.position}));
                s.paused=true;w.goBack();d.setQuery("Nas");d.submitSearch();root.next("search");return;
            }
            if(root.stage==="search" && root.ticks>4) {
                if(s.browsing)return;
                if(!root.check(d.catalogueMatches.query==="Nas" && d.catalogueMatches.albums.length && d.catalogueMatches.artists.length,"Search failed after discovery"))return;
                d.setQuery("");root.next("search-clear");return;
            }
            if(root.stage==="search-clear" && root.ticks>4) {
                if(!root.check(d.discoveryLanding && d.shelves.length===4 && !root.control("vinylStatus").contextualBrowseStatus,"Clearing search lost public discovery or retained search notice"))return;
                root.capture("discover-return","finish");return;
            }
            if(root.stage==="finish") {console.log("VINYL_DISCOVER_PASS",phase);running=false;if(phase!=="inspect")Qt.quit();}
        }
    }
}
