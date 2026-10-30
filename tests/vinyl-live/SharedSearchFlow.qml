import QtQuick
import "../../qml"

// Actual Main/catalogue. Semantic actions, not OS input automation.
Main {
    id: root
    readonly property string evidence: Qt.application.arguments[2]
    property string stage: "profile"
    property int ticks: 0
    property var firstSongs: []
    property string queueBefore: ""
    property string selectedId: ""
    property string tastesBefore: ""
    function fail(message) {console.error("VINYL_SHARED_SEARCH_FAIL",stage,message);cycle.stop();Qt.exit(25);}
    function check(ok,message) {if(!ok)fail(message);return !!ok;}
    function next(value) {stage=value;ticks=0;console.log("VINYL_SHARED_SEARCH_STAGE",value);}
    function find(item,name) {
        if(!item)return null;if(item.objectName===name)return item;
        var children=item.children || [];
        for(var i=0;i<children.length;i++){var match=find(children[i],name);if(match)return match;}return null;
    }
    function capture(name,target) {
        next("capture");contentItem.grabToImage(result=>{
            if(!result.saveToFile(evidence+"/"+name+".png")){root.fail("capture");return;}root.next(target);
        });
    }
    Timer {
        id:cycle;running:true;repeat:true;interval:200
        onTriggered: {
            if(++root.ticks>450){root.fail("deadline");return;}
            if(root.stage==="profile") {
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("home");return;
            }
            var w=root.vinylWorld;if(!w)return;var s=w.session,p=w.searchPage;
            if(root.stage==="home" && root.ticks>8) {
                if(w.worldData.tastePickerOpen)w.worldData.skipPicker();
                root.queueBefore=JSON.stringify(s.queue.map(row=>row.queueId));
                root.find(w,"vinylTopBar").searchClicked();root.next("search");return;
            }
            if(root.stage==="search" && p) {
                if(!root.check(w.page==="home" && w.worldView.currentTab==="home","Search redirected to Discover"))return;
                if(!root.check(p.objectName==="vinylSearchSurface" && !root.find(w,"vinylSearchInput"),"Prototype input survived"))return;
                p.fillAndSearch("Nas");
                console.log("VINYL_SHARED_SEARCH_DISPATCH",JSON.stringify({query:p.query,provider:p.searchProvider.currentQuery,catalogue:s.catalogueQuery,requests:s.catalogueRequests,active:s.profileActive,loading:p.searching,error:p.searchError}));
                root.next("results");return;
            }
            if(root.stage==="results") {
                if(!p || p.searching)return;
                var groups=p.searchProvider.groups;
                if(!root.check(groups.length===4 && groups.every(g=>g.items.length && !g.error),"Live groups missing: "+JSON.stringify(groups.map(g=>({kind:g.key,count:g.items.length,error:g.error})))))return;
                root.firstSongs=s.cataloguePages.songs.items.map(row=>row.id);
                if(!root.check(Object.keys(s.cataloguePages.songs.nextCursor).length,"No song continuation"))return;
                p.searchProvider.loadMore("songs");root.next("paged");return;
            }
            if(root.stage==="paged") {
                if(s.catalogueRequests.songs)return;
                var rows=s.cataloguePages.songs.items;
                if(!root.check(rows.length>root.firstSongs.length && root.firstSongs.every((id,i)=>rows[i].id===id) && new Set(rows.map(r=>r.id)).size===rows.length,"Page append identity/order"))return;
                p.expandedGroups=["Songs"];
                console.log("VINYL_SHARED_SEARCH_RESULTS",JSON.stringify(p.searchProvider.groups.map(g=>({kind:g.key,count:g.items.length,more:g.hasMore}))));
                root.capture("shared-search-results","song");return;
            }
            if(root.stage==="song") {
                p.openItem(p.searchProvider.groups.find(g=>g.key==="songs").items[root.firstSongs.length].data);
                root.next("song-open");return;
            }
            if(root.stage==="song-open" && root.ticks>4) {
                if(!root.check(w.page==="song" && w.browsingSong && JSON.stringify(s.queue.map(r=>r.queueId))===root.queueBefore,"Song route changed queue"))return;
                w.goBack();root.next("song-back");return;
            }
            if(root.stage==="song-back" && p && root.ticks>4) {
                if(!root.check(w.page==="home" && p.query==="Nas" && s.cataloguePages.songs.items.length>root.firstSongs.length && p.expandedGroups.indexOf("Songs")>=0,"Back lost paged results"))return;
                var album=p.searchProvider.groups.find(g=>g.key==="albums").items[0];
                root.selectedId=album.data.albumId;p.openItem(album.data);root.next("album");return;
            }
            if(root.stage==="album" && root.ticks>4) {
                if(s.browsing)return;
                if(!root.check(w.page==="album" && s.shownAlbumId===root.selectedId,"Album route: "+s.browseError))return;
                w.goBack();root.next("album-back");return;
            }
            if(root.stage==="album-back" && p && root.ticks>4) {
                var artist=p.searchProvider.groups.find(g=>g.key==="artists").items.find(i=>i.title==="Nas");
                if(!root.check(!!artist,"Canonical Nas missing"))return;
                root.selectedId=artist.data.artistId;p.openItem(artist.data);root.next("artist");return;
            }
            if(root.stage==="artist" && root.ticks>8) {
                if(!root.check(w.page==="artist" && w.artistData.artistId===root.selectedId,"Artist identity/route"))return;
                w.goBack();root.next("artist-back");return;
            }
            if(root.stage==="artist-back" && p && root.ticks>4) {
                var playlist=p.searchProvider.groups.find(g=>g.key==="playlists").items[0];
                root.selectedId=playlist.data.albumId;p.openItem(playlist.data);root.next("playlist");return;
            }
            if(root.stage==="playlist" && root.ticks>4) {
                if(s.browsing)return;
                if(!root.check(w.page==="album" && s.shownAlbumId===root.selectedId && s.tracks.length,"Playlist route: "+s.browseError))return;
                w.goBack();root.next("playlist-back");return;
            }
            if(root.stage==="playlist-back" && p && root.ticks>4){w.closeSearch();root.next("closed");return;}
            if(root.stage==="closed" && root.ticks>4) {
                if(!root.check(!w.searchOpen && w.page==="home" && w.worldData.query==="" && s.catalogueQuery==="","Close changed Home state"))return;
                root.openSearch();root.next("global");return;
            }
            if(root.stage==="global" && p) {
                if(!root.check(root.shellEscapeState().worldSearchActive,"Escape missed search"))return;
                root.closeWorldSearch();if(!root.check(!w.searchOpen,"Shell close missed search"))return;
                root.tastesBefore=JSON.stringify(w.recommendationService.selectedArtists.map(a=>a.key));
                w.worldData.beginTasteEdit();w.enterDiscoverSearch(true,false);root.next("artist-search");return;
            }
            if(root.stage==="artist-search") {
                var artistField=root.find(w,"vinylPickerArtistSearch");
                if(!root.check(!w.searchOpen && w.page==="home" && artistField && artistField.activeFocus,
                    "Artist picker lost its dedicated focused search: "+JSON.stringify({searchOpen:w.searchOpen,
                        page:w.page,picker:w.worldData.tastePickerOpen,exists:!!artistField,
                        visible:artistField ? artistField.visible : false,focused:artistField ? artistField.activeFocus : false,
                        focus:root.activeFocusItem ? root.activeFocusItem.objectName : ""})))return;
                artistField.text="Ilaiyaraaja";artistField.textEdited();root.next("artist-results");return;
            }
            if(root.stage==="artist-results") {
                if(w.worldData.artistSearchLoading)return;
                var choice=w.worldData.artistOptions(w.worldData.pickerQuery).find(item=>item.title==="Ilaiyaraaja");
                if(!root.check(!!choice,"Real artist search missing Ilaiyaraaja: "+w.worldData.artistSearchError))return;
                if(w.worldData.pickerDraft.indexOf(choice.key)<0)w.worldData.togglePickerArtist(choice);
                root.next("artist-selected");return;
            }
            if(root.stage==="artist-selected") {
                if(!root.check(!w.searchOpen && w.worldData.artistOptions(w.worldData.pickerQuery)
                    .some(i=>i.title==="Ilaiyaraaja" && w.worldData.pickerDraft.indexOf(i.key)>=0),"Artist selection did not remain in the dedicated picker"))return;
                root.capture("dedicated-artist-search-selection","artist-cancel");return;
            }
            if(root.stage==="artist-cancel") {
                w.closeSearch();w.worldData.cancelTasteEdit();
                if(!root.check(JSON.stringify(w.recommendationService.selectedArtists.map(a=>a.key))===root.tastesBefore,"Cancelled edit changed saved artists"))return;
                w.enterDiscoverSearch(true,false);root.next("review");return;
            }
            if(root.stage==="review" && p){p.fillAndSearch("Nas");root.next("review-results");return;}
            if(root.stage==="review-results" && p && !p.searching && root.ticks>10){root.capture("shared-search-open-for-review","pass");return;}
            if(root.stage==="pass"){console.log("VINYL_SHARED_SEARCH_PASS");cycle.stop();}
        }
    }
}
