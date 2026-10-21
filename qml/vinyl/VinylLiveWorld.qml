import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import ".."

// Approved Vinyl destinations over the one local library and music session.
Item {
    id: root
    objectName: "vinylLiveWorld"
    property string medium: "Vinyl"
    property bool lifecycleActive: true
    property bool homeLanding: false
    property Item backdrop
    property string page: "library"
    property alias session: session
    property alias worldData: worldData
    property alias artistData: artistData
    property alias worldScroll: worldScroll
    property alias worldView: world
    property alias personalLibrary: personalData
    property string importStatus: ""
    readonly property bool importing: session.importing
    readonly property string importProgressText: {
        var processed = Number(session.importProcessed) || 0;
        var total = Number(session.importTotal) || 0;
        return total > 0 ? "Opening local music (" + processed + " of " + total + " files)…" : "Opening local music…";
    }
    property var routeStack: []
    property bool browsingSong: false
    property bool restoringRoute: false
    property var pendingAlbumRoute: null
    property real shellDockRight: 0
    property real shellDockBottomExtent: 0
    property bool searchOpen: false
    readonly property real footerLeft: Math.max(theme.margin, shellDockRight > 0 ? shellDockRight + 12 : 0)
    readonly property real contentBottom: bar.visible ? bar.y - 54 : height - Math.max(62, shellDockBottomExtent)
    readonly property bool worldVisible: ["home","discover","library"].indexOf(page)>=0
    readonly property Item chromeBackdrop: worldVisible && root.backdrop ? root.backdrop : vinylBackdrop
    readonly property bool discoverOnlineMode: {
        if(worldData.activeTab==="discover") return !!worldData.onlineMode;
        var states=worldData.browseStates;
        return states && states.discover ? !!states.discover.onlineMode : !!worldData.onlineMode;
    }
    signal homeRequested()
    signal mediumSelected(string medium)
    signal minimizeClicked()
    signal fullscreenClicked()
    signal powerClicked()
    signal playbackRequested()
    signal accountClicked(real anchorRight, real anchorBottom)
    function captureRoute() {
        var focusItem=Window.window ? Window.window.activeFocusItem : null;
        return {page:page,searchOpen:searchOpen,focus:focusItem,keyboardFocus:!!focusItem && focusItem.accessibleName!==undefined,
            worldY:worldScroll.contentY,albumY:album.contentY,artistY:artist.contentY,
            browseState:typeof worldData.captureBrowseState==="function" ? worldData.captureBrowseState() : null,
            worldState:world.captureRouteState(),
            edition:session.shownEdition,disc:session.shownDisc,
            artistName:artistData.name,artistId:artistData.artistId,artistTab:artistData.activeTab,artistCategory:artist.selectedCategory,artistFilter:artistData.filter,artistCategoryFilter:artistData.categoryFilter,artistNewest:artistData.newestFirst,artistAllSongs:artistData.allSongs,
            browsingSong:browsingSong,songAlbum:browseSong.album,songIndex:browseSong.trackIndex,songEdition:browseSong.editionId};
    }
    function pushRoute() {routeStack=routeStack.concat([captureRoute()]);}
    function enterDiscoverSearch(online,modeToggle) {
        var alreadySearching=page==="discover" && world.currentTab==="discover" && !world.listKey;
        if(!alreadySearching) pushRoute();
        // A pending album request has not pushed its detail route yet. Cancel it
        // before leaving so its late completion cannot replace the search page.
        if(session.browsing && session.browseOperation==="album") {
            session.cancelBrowse();pendingAlbumRoute=null;
        } else if(modeToggle && session.browseOperation==="catalogueSearch") {
            session.cancelBrowse();
        }
        world.setTab("discover");
        if(typeof worldData.setOnlineMode==="function") worldData.setOnlineMode(!!online);
        searchOpen=true;
        if(!alreadySearching) {
            world.listKey="";
            page="discover";
            world.parkPage(Math.max(0,world.contentTop-world.dockedInset));
        }
        Qt.callLater(function(){if(query.visible)query.forceActiveFocus();else root.parkFocus();});
    }
    function submitSearch() {
        if(typeof worldData.submitSearch==="function") worldData.submitSearch();
        else {
            worldData.rememberSearch();
            if(worldData.onlineMode) session.searchCatalogue(worldData.query);
            else world.focusCurrent();
        }
    }
    function setQuery(value) {
        if(typeof worldData.setQuery==="function") worldData.setQuery(value);
        else worldData.query=value;
    }
    // A destination starts at a silent focus origin, as in Main's keyboard ignition.
    // Tab still enters the shared focus chain; opening a page never selects Back/Play.
    function parkFocus() {root.forceActiveFocus(Qt.OtherFocusReason);}
    function focusLibrary() {Qt.callLater(root.parkFocus);}
    function showHome() {
        searchOpen=false;recentSearchMenu.close();
        session.cancelBrowse();pendingAlbumRoute=null;routeStack=[];world.listKey="";world.setTab("home");
        worldData.selectTab("home");worldData.refreshHome();page="home";
        world.parkPage(0);Qt.callLater(root.parkFocus);
    }
    function openHomeDestination(tab,section,id) {
        if(tab==="discover"){root.enterDiscoverSearch(true,false);return;}
        root.pushRoute();
        searchOpen=false;recentSearchMenu.close();
        session.cancelBrowse();pendingAlbumRoute=null;world.listKey="";world.setTab("library");page="library";
        worldData.setQuery("");personalData.setSection(section || "albums");
        if(id)personalData.openPlaylist(id);
        else if(section==="playlists")personalData.openPlaylist("");
        world.parkPage(Math.max(0,world.contentTop-world.dockedInset));Qt.callLater(root.parkFocus);
    }
    function showLibrary() {
        searchOpen=false;recentSearchMenu.close();
        session.cancelBrowse();pendingAlbumRoute=null;session.refreshLibrary();routeStack=[];world.listKey="";world.setTab("library");
        if(typeof worldData.selectTab==="function") worldData.selectTab("library");
        page="library";world.parkPage(Math.max(0,world.contentTop-world.dockedInset));focusLibrary();
    }
    function openAlbum(id) {pendingAlbumRoute=captureRoute();session.openAlbum(id);}
    function showSong() {
        if(session.playingIndex<0) return;
        session.cancelBrowse();
        if(page!=="song" || browsingSong) pushRoute();
        searchOpen=false;recentSearchMenu.close();
        browsingSong=false;page="song";
        Qt.callLater(root.parkFocus);
    }
    function showBrowseSong(record,index,editionId) {
        session.cancelBrowse();pushRoute();searchOpen=false;recentSearchMenu.close();browseSong.album=record;browseSong.editionId=editionId || "";browseSong.trackIndex=index;
        browsingSong=true;page="song";Qt.callLater(root.parkFocus);
    }
    function showArtist(name) {
        session.cancelBrowse();pushRoute();searchOpen=false;recentSearchMenu.close();artistData.open(name);artist.contentY=0;artist.selectedCategory="";page="artist";
        Qt.callLater(root.parkFocus);
    }
    function showRecordArtist(name,record) {
        var at=(record.artists || []).indexOf(name);
        if(at>=0 && (record.artistIds || [])[at]) {showArtist({name:name,id:record.artistIds[at]});return;}
        var tracks=record.tracks || [];
        for(var i=0;i<tracks.length;i++) {
            var index=(tracks[i].artists || []).indexOf(name);
            if(index>=0 && (tracks[i].artistIds || [])[index]) {showArtist({name:name,id:tracks[i].artistIds[index]});return;}
        }
        showArtist(name);
    }
    function activateItem(item) {
        if(item.kind==="resume") {
            if(item.unavailable){session.browseError="This song is no longer available.";return;}
            if(item.resumeCurrent){if(session.playingIndex<0)return;session.paused=false;root.showSong();}
            else if(session.resumeSavedCheckpoint(item.resumeEntry))root.showSong();
            return;
        }
        if(item.kind==="playlists"){root.openHomeDestination("library","playlists",item.id);return;}
        if(item.kind==="saved-song") {
            var saved=personalData.homeCollections.songs.find(song=>song.id===item.id);
            if(saved)personalData.openSong(saved);
            return;
        }
        if(item.kind==="artists") {showArtist({id:item.artistId || "",name:item.artist || item.title});return;}
        var albumId=item.albumId || item.id;
        var record=typeof worldData.findAlbum==="function" ? worldData.findAlbum(albumId)
            : worldData.catalogue.albums.find(function(a){return a.id===albumId});
        if(item.kind==="songs" && record) showBrowseSong(record,item.trackIndex);
        else openAlbum(albumId);
    }
    function goBack() {
        // A pending detail request has not pushed its route yet, so Back first
        // cancels only that request. Other browse work must not mask a saved
        // route return (for example, a catalogue search during a detour).
        if(session.browsing && session.browseOperation==="album") {session.cancelBrowse();pendingAlbumRoute=null;return true;}
        if(routeStack.length) {
            if(session.browsing) session.cancelBrowse();
            if(root.worldVisible) world.rememberCurrentTab();
            var next=routeStack.slice(),state=next.pop();routeStack=next;restoringRoute=true;
            if(state.browseState && typeof worldData.restoreBrowseState==="function")
                worldData.restoreBrowseState(state.browseState);
            world.restoreRouteState(state.worldState || {});
            searchOpen=state.searchOpen===true;
            session.shownEdition=state.edition;session.shownDisc=state.disc;session.refreshLibrary();
            if(artistData.name!==state.artistName || artistData.artistId!==state.artistId) artistData.open({name:state.artistName,id:state.artistId});
            artistData.activeTab=state.artistTab;artist.selectedCategory=state.artistCategory;artistData.filter=state.artistFilter;artistData.categoryFilter=state.artistCategoryFilter;artistData.newestFirst=state.artistNewest;artistData.allSongs=state.artistAllSongs;
            browsingSong=state.browsingSong;browseSong.album=state.songAlbum;browseSong.editionId=state.songEdition;browseSong.trackIndex=state.songIndex;
            page=state.page;restoringRoute=false;
            Qt.callLater(function(){
                if(!root) return;
                worldScroll.contentY=state.worldY;album.contentY=state.albumY;artist.contentY=state.artistY;
                // Page visibility and delegated list items settle after the route
                // mutation; focus can only be restored once the destination is visible.
                Qt.callLater(function(){
                    if(!root) return;
                    if(state.focus && state.focus.visible && state.focus.enabled && state.focus.accessibleName!==undefined)
                        state.focus.forceActiveFocus(Qt.TabFocusReason);
                    else if(root.worldVisible && state.keyboardFocus) world.focusCurrent();
                    else root.parkFocus();
                });
            });
            return true;
        }
        if(session.browsing) {session.cancelBrowse();pendingAlbumRoute=null;if(worldVisible && searchOpen){searchOpen=false;recentSearchMenu.close();}return true;}
        if(worldVisible && searchOpen) {searchOpen=false;recentSearchMenu.close();return true;}
        if(worldVisible && world.listKey) {world.backFromList();return true;}
        if(worldVisible && page==="library" && world.backFromLibrary())return true;
        if(!worldVisible) {showLibrary();return true;}
        return false;
    }
    function updateImportStatus(outcome) {
        var count=outcome ? Math.max(0, Number(outcome.imported) || 0) : 0;
        var albums=outcome ? Math.max(0, Number(outcome.albums) || 0) : 0;
        var errors=outcome && outcome.errors || [];
        importStatus=count>0 ? "Imported " + count + (count===1 ? " track" : " tracks") : "";
        if(albums>0) importStatus += (importStatus ? "; updated " : "Updated ") + albums + (albums===1 ? " album." : " albums.");
        else if(importStatus) importStatus += ".";
        if(outcome && outcome.pendingIdentity)
            importStatus += (importStatus ? "\n" : "") + "Choose an identity option to finish importing.";
        if(errors.length) importStatus += (importStatus ? "\n" : "") + errors.join("\n");
        if(!importStatus) importStatus=albums>0 ? "Updated " + albums + (albums===1 ? " album." : " albums.") : "No tracks were added.";
    }
    function importLocalFiles(paths) {
        if (!paths || !paths.length || session.importing) return;
        showLibrary();
        importStatus = "Opening local music…";
        try {
            var importId = session.importFilesAsync(paths);
            if (importId <= 0) importStatus = "Could not start the local import.";
        } catch (error) {
            importStatus = "Could not open local music. " + String(error);
        }
    }
    Keys.onEscapePressed: event => {event.accepted=root.goBack();}
    Keys.onPressed: event => {
        if (!(root.activeFocus || world.activeFocus) || [Qt.Key_Up,Qt.Key_Down,Qt.Key_Left,Qt.Key_Right].indexOf(event.key)<0) return;
        if (root.worldVisible) world.focusCurrent();
        else if (root.page==="album") album.focusPrimaryControl();
        else if (root.page==="song") song.focusBackControl();
        else if (root.page==="artist") artist.forceActiveFocus(Qt.TabFocusReason);
        event.accepted=true;
    }
    Theme {id:theme}
    VinylLiveSession {
        id:session
        onImportFinished: outcome => root.updateImportStatus(outcome)
        onAlbumOpened: {if(!root.restoringRoute)root.routeStack=root.routeStack.concat([root.pendingAlbumRoute || root.captureRoute()]);root.pendingAlbumRoute=null;root.searchOpen=false;recentSearchMenu.close();root.page="album";Qt.callLater(root.parkFocus);}
        onPlaybackRequested: root.playbackRequested()
        onArtistRequested: name => root.showRecordArtist(name,session.shownEdition.sourceAlbum || {})
        onQueueRequested: queuePanel.open()
        onProfileGenerationChanged: {root.importStatus="";pendingAlbumRoute=null;root.searchOpen=false;recentSearchMenu.close();artistData.open({id:"",name:""});root.routeStack=[];root.page="library";world.resetRouteState("library");if(typeof worldData.selectTab==="function")worldData.selectTab("library");root.focusLibrary();if(root.homeLanding)Qt.callLater(root.showHome);}
        onImportingChanged: {if(!session.importing && root.importStatus==="Opening local music…") root.importStatus="Import cancelled. Your library was kept.";}
    }
    VinylLiveWorldData {id:worldData;session:session;personalLibrary:personalData;onItemRequested: item => root.activateItem(item);onResumeRequested:root.activateItem({kind:"resume",resumeCurrent:true});onHomeDestinationRequested:(tab,section,id)=>root.openHomeDestination(tab,section,id)}
    VinylPersonalLibraryData {
        id:personalData;session:session
        query:worldData.browseStates.library.query;sortOrder:worldData.browseStates.library.sortOrder
        onAlbumRequested:id=>root.openAlbum(id)
        onArtistRequested:item=>root.activateItem(item)
        onSongRequested:song=>root.showBrowseSong(song.album,song.index,song.editionId)
        onImportFolderRequested: audioFolderDialog.open()
    }
    Connections {target:session;function onPlaylistAddRequested(rows){personalData.openAddPicker(rows);}}
    VinylLiveArtistData {id:artistData;session:session}
    VinylBrowseSongData {id:browseSong;session:session}
    FileDialog {
        id:audioDialog;objectName:"vinylOpenFilesDialog";title:"Open local music";fileMode:FileDialog.OpenFiles
        nameFilters:["Local music (*.vinyl.json *.flac *.mp3 *.m4a *.m4b *.aac *.ogg *.opus *.wav *.aiff *.aif *.wma *.ape *.alac)","All files (*)"]
        onAccepted: root.importLocalFiles(selectedFiles.map(function(file){return String(file)}))
        onRejected: root.parkFocus()
    }
    FolderDialog {
        id:audioFolderDialog;objectName:"vinylOpenFolderDialog";title:"Open a local music folder"
        onAccepted: root.importLocalFiles([String(selectedFolder)])
        onRejected: root.parkFocus()
    }
    VinylBackdrop {id:vinylBackdrop;anchors.fill:parent;visible:!root.worldVisible || !root.backdrop;source:root.page==="song" ? song.pageData.playingEdition.cover : root.page==="album" ? session.shownEdition.cover : ""}
    TopBar {
        id:top;objectName:"vinylTopBar"
        anchors {left:parent.left;right:parent.right;top:parent.top;margins:theme.margin}
        backdrop:root.chromeBackdrop;activeMedium:"Vinyl";lifecycleActive:root.lifecycleActive
        onAccountClicked:(anchorRight,anchorBottom)=>root.accountClicked(anchorRight,anchorBottom)
        onHomeRequested:root.homeRequested()
        onMediumSelected:medium=>root.mediumSelected(medium)
        onSearchClicked:root.enterDiscoverSearch(true,false)
        onMinimizeClicked:root.minimizeClicked();onFullscreenClicked:root.fullscreenClicked();onPowerClicked:root.powerClicked()
    }
    Item {
        id:libraryHeader;objectName:"vinylLibraryHeader";x:theme.margin;y:top.y+top.height+24;width:parent.width-2*x;height:visible ? 44 : 0;visible:root.worldVisible && root.searchOpen && !(root.page==="library" && personalData.selectedPlaylistId)
        TextField {
            id:query;objectName:"vinylSearchInput";width:Math.max(140,Math.min(600,parent.width-sortButton.width-12-(recentSearchButton.visible ? recentSearchButton.width+12 : 0)));height:44
            text:worldData.query;onTextEdited:root.setQuery(text)
            color:theme.ink;placeholderTextColor:theme.inkDimmer;font.family:theme.ui;font.pixelSize:14;padding:14;selectByMouse:true
            placeholderText:worldData.onlineMode ? "Search artists and albums" : "Filter your local music";onAccepted:root.submitSearch()
            background:Glass {objectName:"vinylSearchGlass";backdrop:root.chromeBackdrop;radius:12;tint:0.06;scrim:0.18}
            FocusRing {anchors.fill:parent;shown:query.activeFocus && [Qt.TabFocusReason,Qt.BacktabFocusReason,Qt.ShortcutFocusReason].indexOf(query.focusReason)>=0;radius:12}
        }
        VinylWorldButton {id:sortButton;x:query.width+12;anchors.verticalCenter:parent.verticalCenter;objectName:"vinylSort";visible:!worldData.discoveryLanding;label:worldData.sortOrder==="title" ? "Sort: Title" : "Sort: Year";symbol:"sort";backdrop:root.chromeBackdrop;onTriggered:worldData.sortOrder=worldData.sortOrder==="title" ? "year" : "title"}
        VinylWorldButton {
            id:recentSearchButton; objectName:"vinylRecentSearches"
            x:sortButton.x+sortButton.width+12; anchors.verticalCenter:parent.verticalCenter
            visible:worldData.recentSearches.length>0
            label:"Recent searches";symbol:"search";backdrop:root.chromeBackdrop
            onTriggered:recentSearchMenu.open()
        }
        Menu {
            id:recentSearchMenu
            x:Math.min(recentSearchButton.x,parent.width-width); y:query.height+6
            onClosed: if(root.worldVisible && root.searchOpen) query.forceActiveFocus()
            Instantiator {
                model:worldData.recentSearches
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    objectName:"vinylRecentSearch_"+index
                    text:modelData
                    onTriggered:{root.setQuery(modelData);root.submitSearch();}
                }
                onObjectAdded:(index,object)=>recentSearchMenu.insertItem(index,object)
                onObjectRemoved:(index,object)=>recentSearchMenu.removeItem(object)
            }
        }
    }
    Connections { target:worldData; function onSearchHistoryStoreChanged(){recentSearchMenu.close();} }
    Flickable {
        id:worldScroll;objectName:"vinylWorldScroll";x:theme.margin;y:libraryHeader.y+libraryHeader.height+14;width:parent.width-2*x;height:Math.max(0,root.contentBottom-y)
        visible:root.worldVisible;clip:true;contentWidth:width;contentHeight:world.implicitHeight;boundsBehavior:Flickable.StopAtBounds
        ScrollGlide {flick:worldScroll}
        // Keep scrolled content out of the sticky tab strip without changing
        // the page's content coordinates or its keyboard/scroll restoration.
        Item {
            id:worldViewport
            y:worldScroll.contentY+(dockedTabs.visible ? world.dockedInset : 0)
            width:worldScroll.width
            height:Math.max(0,worldScroll.height-(dockedTabs.visible ? world.dockedInset : 0))
            clip:true
            VinylWorld {id:world;y:-worldViewport.y;width:parent.width;worldData:worldData;backdrop:root.chromeBackdrop;pageFlick:worldScroll;dockedInset:70;dockedTabBar:dockedTabs;currentTab:"library";onCurrentTabChanged:if(root.worldVisible){root.page=currentTab;root.searchOpen=false;recentSearchMenu.close();}}
        }
    }
    WorldTabBar {
        id:dockedTabs
        objectName:"vinylDockedTabs";x:theme.margin;y:worldScroll.y;width:worldScroll.width;z:5
        visible:root.worldVisible && worldScroll.contentY>world.pickerParkY
        compact:true;backdrop:root.chromeBackdrop;track:worldScroll.contentY;tabPrefix:visible ? "vinylTab" : "vinylHiddenDockedTab"
        tabModel:[{key:"home",label:"Home"},{key:"discover",label:"Discover"},{key:"library",label:"Library"}]
        currentTab:world.currentTab;onTabRequested:tab=>world.setTab(tab)
    }
    BackAction {
        focusOnPointer: false
        id:albumBack;objectName:"vinylAlbumBack";x:theme.margin;y:top.y+top.height+24;height:44;label:"Back";visible:root.page==="album";onTriggered:root.goBack();Keys.onReturnPressed:root.goBack();Keys.onEnterPressed:root.goBack()}
    VinylAlbumPage {
        id:album;x:0;y:albumBack.y+albumBack.height+8;width:parent.width;height:Math.max(0,root.contentBottom-y)
        visible:root.page==="album";pageData:session;backdrop:root.chromeBackdrop;artistNavigationEnabled:true;shuffleEnabled:true;saveEnabled:true;scrollbarVisible:false;songNavigationEnabled:true;queueActionsEnabled:true
        onSongRequested: index => {var record=session.shownEdition.sourceAlbum;var flat=index;for(var d=0;d<session.shownDisc;d++)flat+=session.shownEdition.discs[d].length;if(flat>=0)root.showBrowseSong(record,flat,session.shownEditionId);}
    }
    VinylArtistPage {
        id:artist;x:0;y:top.y+top.height+24;width:parent.width;height:Math.max(0,root.contentBottom-y)
        visible:root.page==="artist";artistData:artistData;worldData:worldData;backdrop:root.chromeBackdrop
        onBackRequested:root.goBack();onArtistRequested:name=>root.showArtist(name)
        onAlbumRequested:record=>root.openAlbum(record.id)
        onSongRequested:value=>root.showBrowseSong(value.album,value.index)
    }
    VinylSongPage {
        id:song;x:0;y:top.y+top.height+24;width:parent.width;height:Math.max(0,root.contentBottom-y)
        visible:root.page==="song";pageData:root.browsingSong ? browseSong : session;browseMode:root.browsingSong;artistNavigationEnabled:true
        backLabel:"Back";onBackRequested:root.goBack();onArtistRequested:name=>root.showRecordArtist(name,root.browsingSong ? browseSong.album : session.playingEdition.sourceAlbum || {})
        onPlayRequested:browseSong.play()
        onCreditsRequested:{creditsPopup.pageData=song.pageData;creditsPopup.open()}
        onAlbumRequested:{if(root.browsingSong)root.openAlbum(browseSong.album.id);else {root.pushRoute();session.showPlayingAlbum();root.page="album";root.parkFocus();}}
    }
    Row {
        objectName:"vinylStatusRow";x:root.footerLeft;y:root.contentBottom+8;width:parent.width-x-theme.margin;spacing:12
        Text {
            objectName:"vinylStatus"
            width:parent.width-(cancelImport.visible ? cancelImport.width+12 : 0)-(retry.visible ? retry.width+12 : 0)-(catalogueRetry.visible ? catalogueRetry.width+12 : 0)
            readonly property string contextualBrowseStatus: {
                if(root.pendingAlbumRoute && session.browseOperation==="album")
                    return session.browseError || (session.browsing ? "Opening album…" : "");
                if(root.page==="discover" && worldData.onlineMode)
                    return !worldData.query.trim() ? "" : session.browseOperation==="catalogueSearch" ? session.browseError || (session.browsing ? "Loading catalogue…" : session.catalogueNotice) : session.catalogueNotice;
                if(root.page==="discover" && session.browseOperation==="search")
                    return session.browseError || (session.browsing ? "Searching local music…" : "");
                if(root.page==="library" && world.currentTab==="library" && session.browseOperation==="search")
                    return session.browseError || (session.browsing ? "Searching local music…" : "");
                return "";
            }
            text:session.persistenceError || session.personalError || personalData.error || artistData.persistenceError || worldData.persistenceError || (root.importing ? root.importProgressText : session.error || (root.page==="artist" ? artistData.catalogueStatus : contextualBrowseStatus || root.importStatus))
            color:!root.importing && (session.error || contextualBrowseStatus) ? theme.gold : theme.inkDim
            font.family:theme.ui;font.pixelSize:12;wrapMode:Text.WordWrap
        }
        VinylWorldButton {id:cancelImport;objectName:"vinylCancelImport";label:"Cancel import";symbol:"cancel";backdrop:root.chromeBackdrop;visible:root.importing;onTriggered:{session.cancelImport();root.importStatus="Import cancelled. Your library was kept."}}
        VinylWorldButton {id:catalogueRetry;objectName:"vinylCatalogueRetry";label:"Retry catalogue";symbol:"retry";backdrop:root.chromeBackdrop;visible:root.page==="artist" && artistData.online && !artistData.loading && !!artistData.catalogueStatus;onTriggered:if(artistData.catalogueService)artistData.catalogueService.retryArtist()}
        VinylWorldButton {id:retry;objectName:"vinylRetryTrack";label:"Retry track";symbol:"retry";backdrop:root.chromeBackdrop;visible:session.error!=="";onTriggered:session.retry()}
    }
    VinylCreditsPopup {id:creditsPopup;pageData:session;backdrop:root.chromeBackdrop;x:Math.max(0,root.width-width-theme.margin);y:Math.max(0,bar.y-height-12)}
    VinylPlaylistPicker {id:playlistPicker;libraryData:personalData;backdrop:root.chromeBackdrop}
    Connections {target:session;function onProfileGenerationChanged(){playlistPicker.close();}}
    VinylQueuePanel {id:queuePanel;pageData:session;backdrop:root.chromeBackdrop;onSongRequested:root.showSong()}
    VinylPlayerBar {
        id:bar;x:root.footerLeft;y:root.height-height-18;width:root.width-x-theme.margin
        visible:session.playingIndex>=0;pageData:session;backdrop:root.chromeBackdrop;page:root.page==="song" ? "song" : "album";queueEnabled:true;trackNavigationEnabled:true
        onSongRequested:root.showSong();onCreditsRequested:{creditsPopup.pageData=session;creditsPopup.open()}
    }
    Component.onCompleted:{session.refreshLibrary();root.focusLibrary();}
}
