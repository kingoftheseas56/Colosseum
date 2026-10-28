import QtQuick
import "../../qml"

// Actual Colosseum executable + Main; use a disposable COLOSSEUM_APPDATA_TAG.
// Arguments: tagged music folder, evidence directory. Leaves the app open on success.
Main {
    id: root
    property string flowStage: "profile"
    property int flowTicks: 0
    property var flowAlbum: null
    property var flowHomeIds: []
    property real flowLibraryY: 0
    property real flowPosition: 0
    property string flowTrack: ""
    property var flowSearchIds: []
    function item(parent,name) {
        if(!parent) return null;
        if(parent.objectName===name) return parent;
        var children=parent.children || [];
        for(var i=0;i<children.length;i++){var found=item(children[i],name);if(found)return found;}
        return null;
    }
    function next(stage) {flowStage=stage;flowTicks=0;console.log("VINYL_BROWSE_STAGE",stage);}
    function fail(message) {console.error("VINYL_BROWSE_FAIL",flowStage,message);Qt.exit(12);}
    function check(condition,message) {if(!condition)fail(message);return condition;}
    function capture(name,nextStage) {
        var directory=Qt.application.arguments[3];
        next("capture");
        contentItem.grabToImage(function(result){
            if(directory) result.saveToFile(directory+"/"+name+".png");
            root.next(nextStage);
        });
    }
    Timer {
        interval:150;repeat:true;running:true
        onTriggered: {
            root.flowTicks++;
            if(root.flowTicks>800){root.fail("stage timeout");return;}
            if(root.flowStage==="profile") {
                if(!root.visible || AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("import");return;
            }
            var w=root.vinylWorld;if(!w)return;
            var s=w.session,d=w.worldData,v=w.worldView;
            if(s.error){root.fail(s.error);return;}
            if(root.flowStage==="import") {
                if(!Qt.application.arguments[2]){root.fail("tagged music folder argument missing");return;}
                w.importLocalFiles([Qt.application.arguments[2]]);root.next("import-wait");return;
            }
            if(root.flowStage==="import-wait") {
                if(s.importing)return;
                root.flowAlbum=s.library.map(function(e){return e.payload.album;}).find(function(a){return a.title.indexOf("Illmatic")>=0;});
                if(!root.check(root.flowAlbum && root.flowAlbum.tracks.length>=10,"tagged Illmatic import: "+w.importStatus))return;
                d.skipPicker();
                root.flowHomeIds=d.homeRows.map(function(row){return row.items.map(function(i){return i.id;}).join("|");});
                d.setQuery("no-match-navigation-probe");
                if(!root.check(d.row("library",false).items.length===0,"Library filter did not apply"))return;
                v.setTab("home");root.next("home");return;
            }
            if(root.flowStage==="home" && root.flowTicks>3) {
                if(!root.check(d.query==="" && JSON.stringify(d.homeRows.map(function(row){return row.items.map(function(i){return i.id;}).join("|");}))===JSON.stringify(root.flowHomeIds),"Library filter changed Home"))return;
                root.capture("home-independent","leave-home");return;
            }
            if(root.flowStage==="leave-home") {v.setTab("library");root.next("library");return;}
            if(root.flowStage==="library" && root.flowTicks>2) {
                if(!root.check(d.query==="no-match-navigation-probe","Library query was lost"))return;
                d.setQuery("Illmatic");d.sortOrder="year";
                w.worldScroll.contentY=Math.min(w.worldScroll.contentHeight-w.worldScroll.height,Math.max(0,v.contentTop-v.dockedInset+30));
                root.next("open-album");return;
            }
            if(root.flowStage==="open-album" && root.flowTicks>3) {
                root.flowLibraryY=w.worldScroll.contentY;w.openAlbum(root.flowAlbum.id);root.next("album");return;
            }
            if(root.flowStage==="album") {
                if(w.page!=="album")return;
                var row=root.item(w,"vinylTrackRow_1") || root.item(w,"vinylTrackRow_0");
                if(!root.check(row!==null,"Album track row missing"))return;
                row.activate(Qt.MouseFocusReason);root.next("playing");return;
            }
            if(root.flowStage==="playing") {
                if(!s.loaded || s.position<0.5)return;
                if(!root.check(w.page==="album","Track row navigated away from Album"))return;
                root.flowTrack=s.playingTrack.id;root.flowPosition=s.position;
                root.item(w,"vinylTopBar").searchClicked();root.next("search-focus");return;
            }
            if(root.flowStage==="search-focus" && root.flowTicks>3) {
                var input=root.item(w,"vinylSearchInput");
                if(!root.check(w.page==="discover" && d.onlineMode && input.visible && input.activeFocus,"TopBar search failed to reach focused Discover query"))return;
                input.text="Nas";input.textEdited();input.accepted();root.next("catalogue");return;
            }
            if(root.flowStage==="catalogue") {
                if(s.browsing)return;
                if(!root.check(!s.browseError && d.discoverAlbums.length>0,"real Nas catalogue search: "+s.browseError))return;
                root.flowSearchIds=d.discoverAlbums.map(function(a){return a.id;});
                if(!root.check(s.playingTrack.id===root.flowTrack && s.position>root.flowPosition,"Browsing disturbed existing playback"))return;
                root.capture("discover-real-results","leave-catalogue");return;
            }
            if(root.flowStage==="leave-catalogue") {
                w.goBack();root.next("back-album");return;
            }
            if(root.flowStage==="back-album" && root.flowTicks>3) {
                if(!root.check(w.page==="album" && s.shownEdition.sourceAlbum.id===root.flowAlbum.id,"Search Back did not restore Album"))return;
                w.enterDiscoverSearch(false,true);root.next("local-focus");return;
            }
            if(root.flowStage==="local-focus" && root.flowTicks>3) {
                if(!root.check(w.page==="discover" && !d.onlineMode && root.item(w,"vinylSearchInput").activeFocus,"Local Discover request did not reach the visible query"))return;
                w.goBack();root.next("local-back");return;
            }
            if(root.flowStage==="local-back" && root.flowTicks>3) {
                if(!root.check(w.page==="album","Local Discover Back lost Album"))return;
                w.goBack();root.next("library-return");return;
            }
            if(root.flowStage==="library-return" && root.flowTicks>4) {
                if(!root.check(w.page==="library" && d.query==="Illmatic" && d.sortOrder==="year" && Math.abs(w.worldScroll.contentY-root.flowLibraryY)<2,"Library context/scroll was not restored"))return;
                v.setTab("discover");
                if(!root.check(d.query==="Nas" && !d.onlineMode,"Returning Library erased Discover query/mode"))return;
                d.setOnlineMode(true);
                if(!root.check(JSON.stringify(d.discoverAlbums.map(function(a){return a.id;}))===JSON.stringify(root.flowSearchIds),"Discover lost its previous catalogue results"))return;
                d.setKind("artists");root.next("discover-artists");return;
            }
            if(root.flowStage==="discover-artists" && root.flowTicks>3) {
                d.setOnlineMode(true);d.submitSearch();root.next("artist-results");return;
            }
            if(root.flowStage==="artist-results") {
                if(s.browsing)return;
                var artists=d.shelves[0].items;
                var nas=artists.find(function(a){return a.title==="Nas";});
                if(!root.check(nas!==undefined,"Nas artist result missing: "+s.browseError))return;
                v.showList("local-artists",false);w.activateItem(nas);root.next("artist");return;
            }
            if(root.flowStage==="artist" && root.flowTicks>4) {
                if(!root.check(w.page==="artist" && w.artistData.name==="Nas","artist destination failed"))return;
                root.item(w,"vinylTopBar").searchClicked();root.next("artist-search");return;
            }
            if(root.flowStage==="artist-search" && root.flowTicks>3) {
                if(!root.check(w.page==="discover" && !v.listKey && root.item(w,"vinylSearchInput").activeFocus,"Artist Search did not leave the list for query"))return;
                w.goBack();root.next("artist-return");return;
            }
            if(root.flowStage==="artist-return" && root.flowTicks>3) {
                if(!root.check(w.page==="artist","Artist search Back failed"))return;
                w.goBack();root.next("list-return");return;
            }
            if(root.flowStage==="list-return" && root.flowTicks>4) {
                if(!root.check(w.page==="discover" && v.listKey==="local-artists" && d.query==="Nas" && d.lens.kind==="artists","See all/search state not restored"))return;
                w.goBack();v.setTab("home");root.next("finish-home");return;
            }
            if(root.flowStage==="finish-home" && root.flowTicks>4) {
                if(!root.check(JSON.stringify(d.homeRows.map(function(row){return row.items.map(function(i){return i.id;}).join("|");}))===JSON.stringify(root.flowHomeIds),"Catalogue or artist browsing changed Home"))return;
                w.showSong();root.next("song");return;
            }
            if(root.flowStage==="song" && root.flowTicks>3) {
                root.item(w,"vinylTopBar").searchClicked();root.next("song-search");return;
            }
            if(root.flowStage==="song-search" && root.flowTicks>3) {
                if(!root.check(w.page==="discover" && root.item(w,"vinylSearchInput").activeFocus,"Song search navigation failed"))return;
                w.goBack();root.next("song-return");return;
            }
            if(root.flowStage==="song-return" && root.flowTicks>3) {
                if(!root.check(w.page==="song" && s.playingTrack.id===root.flowTrack,"Song Back or playing identity failed"))return;
                s.paused=true;w.goBack();root.next("done");return;
            }
            if(root.flowStage==="done" && root.flowTicks>4) {
                root.capture("final-home","pass");return;
            }
            if(root.flowStage==="pass") {
                console.log("VINYL_BROWSE_PASS",JSON.stringify({albums:s.library.length,tracks:root.flowAlbum.tracks.length,track:root.flowTrack,position:s.position,query:d.query,tab:v.currentTab,source:s.sourceKind}));
                running=false;
            }
        }
    }
}
