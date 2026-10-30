import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Colosseum.Vinyl 1.0
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
    property bool videoFullscreen: false
    readonly property bool videoDisplayed: page==="song" && !!song.pageData && song.pageData.videoShown===true
    onVideoDisplayedChanged: {
        if(!videoDisplayed){exitVideoFullscreen();videoHudVisible=true;videoHudTimer.stop();}
        else Qt.callLater(root.revealVideoHud);
    }
    property bool videoHudVisible: true
    property var videoReturnFocus: null
    property alias session: session
    property alias worldData: worldData
    property alias artistData: artistData
    property alias worldScroll: worldScroll
    property alias worldView: world
    property alias personalLibrary: personalData
    readonly property var recommendationService: recommendations
    readonly property bool canEnterVinyl: componentReady && session.profileActive && !session.shuttingDown
        && recommendations.profileActive && recommendations.profileScope === session.profileScope
        && recommendations.profileGeneration === session.profileGeneration
        && recommendations.canEnterVinyl === true
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
    property var namedCollectionPending: null
    property var namedCollectionReturn: null
    property int namedCollectionRequestId: -1
    signal namedCollectionOpened(bool success, string message)
    signal namedCollectionBackRequested()
    property real shellDockRight: 0
    property real shellDockBottomExtent: 0
    property bool searchOpen: false
    property bool artistSearchOpen: false
    property string homeListeningError: ""
    readonly property string homeRadioStatus: !canEnterVinyl || !recommendations.homeRadioActive ? ""
        : recommendations.homeRadioState==="loading" ? "Loading radio…"
        : recommendations.homeRadioState==="error" ? "Radio could not load more music."
        : recommendations.homeRadioState==="exhausted" && session.sourceState==="ended" ? "Radio finished." : ""
    property var searchSnapshot: null
    property var searchOriginCatalogue: null
    readonly property var searchPage: searchLayer.item
    readonly property real footerLeft: Math.max(theme.margin, shellDockRight > 0 ? shellDockRight + 12 : 0)
    readonly property real contentBottom: bar.visible ? bar.y - (videoDisplayed ? 14 : 54) : height - Math.max(62, shellDockBottomExtent)
    readonly property bool worldVisible: canEnterVinyl
        ? ["home","discover","library"].indexOf(page)>=0 : worldData.tastePickerOpen===true
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
    signal dlnaClicked(real anchorRight, real anchorBottom)
    property bool componentReady: false
    property bool enforcingTasteGate: false
    onCanEnterVinylChanged: if (componentReady && !canEnterVinyl) Qt.callLater(root.enforceTasteGate)
    onPageChanged: if (componentReady && !canEnterVinyl && page !== "home") enforceTasteGate()
    function enforceTasteGate() {
        if(!root.componentReady)return false;
        if (root.canEnterVinyl) return true;
        if(!session.paused)session.paused=true;
        if(recommendations.homeRadioActive)recommendations.stopHomeRadio();
        if (root.page === "home" && world.currentTab === "home"
                && worldData.activeTab === "home" && worldData.tastePickerOpen
                && (!root.searchOpen || root.artistSearchOpen))
            return false;
        if (root.enforcingTasteGate) return false;
        root.enforcingTasteGate = true;
        try {
            root.cancelNamedCollectionOpen();
            if (session.browsing) session.cancelBrowse();
            pendingAlbumRoute = null;
            routeStack = [];
            searchOpen = false;
            artistSearchOpen = false;

            if(queuePanel)queuePanel.close();
            if(playlistPicker)playlistPicker.close();
            if(creditsPopup)creditsPopup.close();
            if(audioDialog)audioDialog.close();
            if(audioFolderDialog)audioFolderDialog.close();
            world.listKey = "";
            if (world.currentTab !== "home") world.setTab("home");
            if (worldData.activeTab !== "home" || recommendations.onboardingState === "unanswered")
                worldData.selectTab("home");
            page = "home";
        } finally {
            root.enforcingTasteGate = false;
        }
        Qt.callLater(root.parkFocus);
        return false;
    }
    function captureRoute() {
        var focusItem=Window.window ? Window.window.activeFocusItem : null;
        return {page:page,searchOpen:searchOpen,searchSnapshot:searchSnapshot,searchOriginCatalogue:searchOriginCatalogue,focus:focusItem,keyboardFocus:!!focusItem && focusItem.accessibleName!==undefined,
            worldY:worldScroll.contentY,albumY:album.contentY,artistY:artist.contentY,
            browseState:typeof worldData.captureBrowseState==="function" ? worldData.captureBrowseState() : null,
            worldState:world.captureRouteState(),
            edition:session.shownEdition,disc:session.shownDisc,
            artistName:artistData.name,artistId:artistData.artistId,artistTab:artistData.activeTab,artistCategory:artist.selectedCategory,artistFilter:artistData.filter,artistCategoryFilter:artistData.categoryFilter,artistNewest:artistData.newestFirst,artistAllSongs:artistData.allSongs,
            browsingSong:browsingSong,songAlbum:browseSong.album,songIndex:browseSong.trackIndex,songEdition:browseSong.editionId};
    }
    function pushRoute() {routeStack=routeStack.concat([captureRoute()]);}
    function openArtistOnboarding() {
        if(lifecycleActive && page==="home" && worldData.tastePickerOpen
            && worldData.onboardingState==="draft" && !searchOpen)
            enterDiscoverSearch(true,false);
    }
    onLifecycleActiveChanged: {
        if(lifecycleActive){Qt.callLater(root.openArtistOnboarding);revealVideoHud();}
        else {exitVideoFullscreen();videoHudVisible=true;videoHudTimer.stop();}
    }
    function focusInside(item) {
        var focused=Window.window ? Window.window.activeFocusItem : null;
        while(focused){if(focused===item)return true;focused=focused.parent;}
        return false;
    }
    function videoShortcutAllowed() {
        var focused=Window.window ? Window.window.activeFocusItem : null;
        return (!focused || typeof focused.cursorPosition!=="number") && (!focused || focusInside(root));
    }
    function revealVideoHud() {
        videoHudVisible=true;
        if(videoDisplayed && visible && lifecycleActive && !searchOpen
                && !session.paused && !videoHudHover.hovered && !focusInside(bar))videoHudTimer.restart();
        else videoHudTimer.stop();
    }
    function toggleVideoFullscreen() {
        if(videoFullscreen){exitVideoFullscreen();return;}
        if(!videoDisplayed)return;
        videoReturnFocus=Window.window ? Window.window.activeFocusItem : null;
        videoFullscreen=true;revealVideoHud();
        Qt.callLater(function(){if(root.videoFullscreen)song.forceActiveFocus(Qt.ShortcutFocusReason);});
    }
    function exitVideoFullscreen() {
        if(!videoFullscreen)return;
        videoFullscreen=false;revealVideoHud();
        var previous=videoReturnFocus;videoReturnFocus=null;
        Qt.callLater(function(){
            if(!root.visible || !root.lifecycleActive || root.page!=="song")return;
            try{if(previous && previous.visible && previous.enabled){previous.forceActiveFocus(Qt.BacktabFocusReason);return;}}catch(error){}
            song.focusBackControl();
        });
    }
    Timer {
        id: videoHudTimer
        interval: 2500
        running: root.videoDisplayed && root.visible && root.lifecycleActive && !root.searchOpen
            && root.videoHudVisible && !session.paused
            && !videoHudHover.hovered && !root.focusInside(bar)
        onTriggered: root.videoHudVisible=false
    }
    Shortcut {
        sequence: "F"
        enabled: root.visible && root.lifecycleActive && root.videoDisplayed
            && !root.searchOpen && root.videoShortcutAllowed()
        onActivated: root.toggleVideoFullscreen()
    }
    Connections {
        target: root
        function onPageChanged(){if(root.page!=="song")root.exitVideoFullscreen();}
        function onSearchOpenChanged(){if(root.searchOpen)root.exitVideoFullscreen();else if(root.videoDisplayed)root.revealVideoHud();}
    }
    Connections {
        target: session
        function onVideoShownChanged(){if(!session.videoShown)root.exitVideoFullscreen();}
        function onPausedChanged(){if(root.videoDisplayed)root.revealVideoHud();}
        function onProfileGenerationChanged(){root.exitVideoFullscreen();}
    }
    function enterDiscoverSearch(online,modeToggle) {
        if(searchOpen) return;
        var pickingArtists=page==="home" && world.currentTab==="home" && worldData.tastePickerOpen===true;
        if(pickingArtists) {world.focusArtistSearch();return;}
        if(!pickingArtists && !root.canEnterVinyl){root.enforceTasteGate();return;}
        if(session.browsing && session.browseOperation==="album") {
            session.cancelBrowse();pendingAlbumRoute=null;
        }
        searchOriginCatalogue=session.captureCatalogue();
        searchSnapshot=null;
        artistSearchOpen=false;
        searchOpen=true;
    }
    function closeSearch() {
        if(!searchOpen) return;
        if(searchLayer.item) searchLayer.item.commitCurrentQuery();
        if(session.browsing && session.browseOperation==="album") {
            session.cancelBrowse();pendingAlbumRoute=null;
        }
        if(!artistSearchOpen && searchOriginCatalogue) session.restoreCatalogue(searchOriginCatalogue);
        searchOpen=false;artistSearchOpen=false;searchSnapshot=null;
        Qt.callLater(root.parkFocus);
    }
    function submitSearch() {
        if(!artistSearchOpen && !root.canEnterVinyl){root.enforceTasteGate();return;}
        if(searchLayer.item) searchLayer.item.fillAndSearch(searchLayer.item.query);
        else worldData.submitSearch();
    }
    function setQuery(value) {
        if(!artistSearchOpen && !root.canEnterVinyl){root.enforceTasteGate();return;}
        if(searchLayer.item) searchLayer.item.query=String(value || "");
        else worldData.setQuery(value);
    }
    function activateSearchItem(item) {
        if(artistSearchOpen) {worldData.togglePickerArtist(item);return;}
        if(!root.canEnterVinyl){root.enforceTasteGate();return;}
        searchSnapshot=searchLayer.item.captureState();
        if(item.kind==="songs" && item.record)
            showBrowseSong(item.record,item.trackIndex);
        else activateItem(item);
    }
    // A destination starts at a silent focus origin, as in Main's keyboard ignition.
    // Tab still enters the shared focus chain; opening a page never selects Back/Play.
    function parkFocus() {if(!searchOpen)root.forceActiveFocus(Qt.OtherFocusReason);}
    function focusLibrary() {Qt.callLater(root.parkFocus);}
    function showHome() {
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
        var alreadyOnHome=world.currentTab==="home";
        searchOpen=false;artistSearchOpen=false;
        session.cancelBrowse();pendingAlbumRoute=null;routeStack=[];world.listKey="";world.setTab("home");
        if(alreadyOnHome) {worldData.selectTab("home");worldData.refreshHome();}
        page="home";
        world.parkPage(0);Qt.callLater(root.parkFocus);
        Qt.callLater(root.openArtistOnboarding);
    }
    function openHomeDestination(tab,section,id) {
        if(tab==="discover"){root.enterDiscoverSearch(true,false);return;}
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
        root.pushRoute();
        searchOpen=false;artistSearchOpen=false;
        session.cancelBrowse();pendingAlbumRoute=null;world.listKey="";world.setTab("library");page="library";
        worldData.setQuery("");personalData.setSection(section || "albums");
        if(id)personalData.openPlaylist(id);
        else if(section==="playlists")personalData.openPlaylist("");
        world.parkPage(Math.max(0,world.contentTop-world.dockedInset));Qt.callLater(root.parkFocus);
    }
    function showLibrary() {
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
        searchOpen=false;artistSearchOpen=false;
        session.cancelBrowse();pendingAlbumRoute=null;session.refreshLibrary();routeStack=[];world.listKey="";world.setTab("library");
        if(typeof worldData.selectTab==="function") worldData.selectTab("library");
        page="library";world.parkPage(Math.max(0,world.contentTop-world.dockedInset));focusLibrary();
    }
    function openAlbum(id) {
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
        pendingAlbumRoute=captureRoute();session.openAlbum(id);
    }
    // A separate catalogue read lane keeps collection browsing independent of
    // the player, queue, Library, and the session's ordinary browse request.
    function cancelNamedCollectionOpen() {
        collectionOpenTimeout.stop()
        if (session.source && typeof session.source.cancel === "function")
            session.source.cancel("named-collections")
        namedCollectionPending = null
        namedCollectionRequestId = -1
    }
    function finishNamedCollectionOpen(success, message) {
        collectionOpenTimeout.stop()
        namedCollectionPending = null
        namedCollectionRequestId = -1
        if (success) namedCollectionReturn = {page:page, depth:routeStack.length}
        namedCollectionOpened(success, message || "")
    }
    function openNamedCollectionReference(reference) {
        if(!root.canEnterVinyl) {root.enforceTasteGate();namedCollectionOpened(false,"Choose at least five artists to open Vinyl.");return;}
        cancelNamedCollectionOpen()
        var p = reference.payload || {}
        if (!session.profileActive || !session.source) {
            namedCollectionOpened(false, "Music is unavailable for this profile.")
            return
        }
        if (reference.route === "vinyl-artist-detail") {
            if (reference.source === "vinyl-local") {
                var found = (artistData.localAlbums || []).some(function(record) {
                    if ((record.artistIds || []).indexOf(reference.sourceId) >= 0) return true
                    return (record.tracks || []).some(function(track) {
                        return (track.artistIds || []).indexOf(reference.sourceId) >= 0
                    })
                })
                if (!found) { namedCollectionOpened(false, "This local artist is unavailable. Reconnect or import its source, then try again."); return }
            }
            showArtist({id:reference.sourceId, name:reference.title})
            finishNamedCollectionOpen(true, "")
            return
        }
        namedCollectionPending = reference
        namedCollectionRequestId = session.source.request("named-collections", "album", p.browseId)
        if (namedCollectionRequestId < 0) {
            finishNamedCollectionOpen(false, "This music source could not be opened. The saved item has been kept.")
            return
        }
        collectionOpenTimeout.restart()
    }
    function acceptNamedCollectionAlbum(data, failure) {
        if(!root.canEnterVinyl) {root.enforceTasteGate();root.namedCollectionOpened(false,"Choose at least five artists to open Vinyl.");return;}
        var reference = namedCollectionPending
        if (!reference) return
        data = session.plainData(data)
        var p = reference.payload || {}
        if (failure || !session.validAlbum(data) || String(data.id) !== p.albumId) {
            finishNamedCollectionOpen(false, failure || "This exact album is unavailable. The saved item has been kept.")
            return
        }
        var editions = session.normalizeEditions(data)
        var selected = editions.find(function(edition) { return edition.id === reference.editionId })
        if (!selected) {
            finishNamedCollectionOpen(false, "The saved music edition is unavailable. Other editions have not been substituted.")
            return
        }
        if (reference.route === "vinyl-song-detail") {
            var raw = (data.editions || []).find(function(edition) { return edition.id === reference.editionId })
            if (!raw && String(data.id) === reference.editionId) raw = data
            var tracks = raw ? raw.tracks || [] : []
            var index = tracks.findIndex(function(track) { return String(track.id) === reference.sourceId })
            if (index < 0) {
                finishNamedCollectionOpen(false, "This exact song is unavailable in the saved edition. The saved item has been kept.")
                return
            }
            showBrowseSong(data, index, reference.editionId)
        } else {
            session.cancelBrowse()
            pushRoute()
            session.shownEdition = selected
            session.shownDisc = 0
            searchOpen = false
            artistSearchOpen = false

            page = "album"
            Qt.callLater(root.parkFocus)
        }
        finishNamedCollectionOpen(true, "")
    }
    Timer {
        id: collectionOpenTimeout
        interval: 30000
        onTriggered: {
            root.cancelNamedCollectionOpen()
            root.namedCollectionOpened(false, "This music source did not answer. The saved item is still available to retry.")
        }
    }
    Connections {
        target: session.source
        function onCompleted(lane, requestId, data, failure) {
            if (lane === "named-collections" && requestId === root.namedCollectionRequestId)
                root.acceptNamedCollectionAlbum(data, failure)
        }
    }
    function showSong() {
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
        if(session.playingIndex<0) return;
        session.cancelBrowse();
        if(page!=="song" || browsingSong) pushRoute();
        searchOpen=false;artistSearchOpen=false;
        browsingSong=false;page="song";
        Qt.callLater(root.parkFocus);
    }
    function showBrowseSong(record,index,editionId) {
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
        session.cancelBrowse();pushRoute();searchOpen=false;artistSearchOpen=false;browseSong.album=record;browseSong.editionId=editionId || "";browseSong.trackIndex=index;
        browsingSong=true;page="song";Qt.callLater(root.parkFocus);
    }
    function showArtist(name) {
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
        session.cancelBrowse();pushRoute();searchOpen=false;artistSearchOpen=false;artistData.open(name);artist.contentY=0;artist.selectedCategory="";page="artist";
        Qt.callLater(root.parkFocus);
    }
    function showRecordArtist(name,record) {
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
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
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
        if(item.kind==="mixes") {
            root.pushRoute();searchOpen=false;artistSearchOpen=false;
            session.browseError="";homeListeningError="";
            homeMix.savedPlaylistId="";recommendations.openHomeMix(String(item.mixId || "personal"));
            page="mix";homeMix.contentY=0;root.parkFocus();return;
        }
        if(item.kind==="radio") {
            homeListeningError="";
            recommendations.startHomeRadio(item.seedArtist || {});return;
        }
        if(item.kind==="resume") {
            if(item.unavailable){session.browseError="This song is no longer available.";return;}
            if(item.resumeCurrent){if(session.playingIndex<0)return;session.paused=false;root.showSong();}
            else if(session.resumeSavedCheckpoint(item.resumeEntry))root.showSong();
            return;
        }
        var destination=item.destination || {};
        if(destination.kind==="album") {
            // The existing catalogue owns stable album UUIDs; its browse route
            // accepts the provider browse ID before that record is registered.
            var destinationAlbumId=String(destination.provider==="youtube-music"
                ? destination.providerId || "" : destination.id || destination.providerId || "");
            if(destinationAlbumId) openAlbum(destinationAlbumId);
            return;
        }
        if(destination.kind==="artist") {
            showArtist({id:String(destination.id || destination.providerId || ""),
                name:String(item.name || item.artist || item.title || "")});
            return;
        }
        if(destination.kind==="song") {
            var record=item.record || null, tracks=record && Array.isArray(record.tracks) ? record.tracks : [];
            var trackIndex=Number(item.trackIndex);
            if(!Number.isInteger(trackIndex) || trackIndex<0 || trackIndex>=tracks.length) {
                var trackIds=[String(item.trackId || ""),String(destination.id || ""),String(destination.providerId || "")];
                trackIndex=tracks.findIndex(function(track){return track && trackIds.indexOf(String(track.id || ""))>=0;});
            }
            if(record && trackIndex>=0 && trackIndex<tracks.length)
                showBrowseSong(record,trackIndex,String(item.editionId || ""));
            else session.browseError="Song details are not available.";
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
    function queuedAudioIds() {
        return session.queue.map(row=>String((row.track || {}).videoId || row.trackId || (row.track || {}).id || "")).filter(id=>!!id);
    }
    function restoreHomeRadio() {
        if(!root.canEnterVinyl || !session.radioContext || session.radioContext.kind!=="artist-radio")return;
        if(!recommendations.homeRadioActive)recommendations.restoreHomeRadio(session.radioContext,root.queuedAudioIds());
    }
    function refillHomeRadio() {
        if(!root.canEnterVinyl || session.paused || !recommendations.homeRadioActive)return;
        if(session.queue.length-session.queueIndex<=3 && recommendations.homeRadioState==="ready")
            recommendations.refillHomeRadio(root.queuedAudioIds());
    }
    function goBack() {
        if(videoFullscreen){exitVideoFullscreen();return true;}
        if(searchOpen) {closeSearch();return true;}
        if(!root.canEnterVinyl) {
            if(artistSearchOpen) {artistSearchOpen=false;searchOpen=false;return true;}
            root.enforceTasteGate();return true;
        }
        if (namedCollectionReturn && page === namedCollectionReturn.page
                && routeStack.length === namedCollectionReturn.depth) {
            namedCollectionReturn = null
            root.goBack()
            namedCollectionBackRequested()
            return true
        }
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
            searchSnapshot=state.searchSnapshot || null;
            searchOriginCatalogue=state.searchOriginCatalogue || null;
            artistSearchOpen=false;
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
        if(session.browsing) {session.cancelBrowse();pendingAlbumRoute=null;if(worldVisible && searchOpen){searchOpen=false;artistSearchOpen=false;}return true;}
        if(worldVisible && searchOpen) {searchOpen=false;artistSearchOpen=false;return true;}
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
        if(!root.canEnterVinyl) {root.enforceTasteGate();return;}
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
        if(root.videoDisplayed)root.revealVideoHud();
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
        videoSurface: song.videoSurface
        onImportFinished: outcome => root.updateImportStatus(outcome)
        onAlbumOpened: {
            if(!root.canEnterVinyl){session.cancelBrowse();root.pendingAlbumRoute=null;root.enforceTasteGate();return;}
            if(!root.restoringRoute)root.routeStack=root.routeStack.concat([root.pendingAlbumRoute || root.captureRoute()]);
            root.pendingAlbumRoute=null;root.searchSnapshot=null;root.searchOriginCatalogue=null;root.searchOpen=false;root.artistSearchOpen=false;root.page="album";Qt.callLater(root.parkFocus);
        }
        onPlaybackRequested: root.playbackRequested()
        onArtistRequested: name => root.showRecordArtist(name,session.shownEdition.sourceAlbum || {})
        onQueueRequested: if(root.canEnterVinyl)queuePanel.open()
        onProfileGenerationChanged: {
            root.cancelNamedCollectionOpen();root.namedCollectionReturn=null;root.importStatus="";
            pendingAlbumRoute=null;root.searchSnapshot=null;root.searchOriginCatalogue=null;root.searchOpen=false;root.artistSearchOpen=false;
            artistData.open({id:"",name:""});root.routeStack=[];root.page="home";world.resetRouteState("home");
            Qt.callLater(function(){
                if(!root)return;
                if(root.canEnterVinyl){if(root.homeLanding)root.showHome();else root.showLibrary();}
                else root.enforceTasteGate();
            });
        }
        onImportingChanged: {if(!session.importing && root.importStatus==="Opening local music…") root.importStatus="Import cancelled. Your library was kept.";}
    }
    VinylLiveWorldData {id:worldData;session:session;personalLibrary:personalData;recommendationService:recommendations;onItemRequested: item => root.activateItem(item);onResumeRequested:root.activateItem({kind:"resume",resumeCurrent:true});onHomeDestinationRequested:(tab,section,id)=>root.openHomeDestination(tab,section,id)}
    Connections {
        target:recommendations
        function onStateChanged() {
            if(root.componentReady && !recommendations.canEnterVinyl)root.enforceTasteGate();
        }
        function onHomeChanged() {Qt.callLater(root.restoreHomeRadio);}
        function onHomeRadioBatchReady(songs,append) {
            if(!root.canEnterVinyl || !recommendations.homeRadioActive)return;
            var values=session.plainData(songs);
            var accepted=append ? session.appendHomeSongs(values,true)
                :session.playHomeSongs(values,0,false,session.plainData(recommendations.homeRadioContext));
            if(!accepted){
                root.homeListeningError=session.browseError || "Radio could not start.";
                recommendations.stopHomeRadio();session.setRadioContext({});return;
            }
            if(!append)root.showSong();
        }
        function onListeningChanged() {
            if(recommendations.homeRadioActive && recommendations.homeRadioState==="error")session.browseError="Radio could not load more music. Open the station again to retry.";
            Qt.callLater(root.refillHomeRadio);
        }
        ignoreUnknownSignals:true
    }
    Connections {
        target:session
        function onManualPlaybackStarted() {root.homeListeningError="";recommendations.stopHomeRadio();}
        function onRadioContextChanged() {Qt.callLater(root.restoreHomeRadio);}
        function onQueueIndexChanged() {Qt.callLater(root.refillHomeRadio);}
        function onPausedChanged() {Qt.callLater(root.refillHomeRadio);}
    }
    VinylRecommendationService {
        id:recommendations
        source:session.source
        preferences:worldData.preferencesStore
        activityStore:session.profileActive ? session.activitySink : null
        historyStore:session.profileActive && typeof ProfileHistory!=="undefined" ? ProfileHistory : null
        keepHistory:!session.activityPreferences || session.activityPreferences.keepActivityHistory!==false
        homeCollections: {
            var boundProfileScope=recommendations.profileScope;
            var boundProfileGeneration=recommendations.profileGeneration;
            return recommendations.profileActive
                && boundProfileScope===session.profileScope
                && boundProfileGeneration===session.profileGeneration
                ? personalData.homeCollections : ({});
        }
        suggestions:worldData.project(worldData.libraryAlbums,"artists")
            .concat((personalData.homeCollections && personalData.homeCollections.artists) || [])
            .concat(recommendations.selectedArtists || [])
        profileScope:session.profileScope
        profileGeneration:session.profileGeneration
        profileActive:session.profileActive && !session.shuttingDown
    }
    Connections {
        target:worldData
        function onTastePickerOpenChanged() {
            if(worldData.tastePickerOpen) Qt.callLater(root.openArtistOnboarding);
            if(!worldData.tastePickerOpen && root.artistSearchOpen) {
                root.artistSearchOpen=false;
                root.searchOpen=false;

            }
        }
        function onActiveTabChanged() {
            if(!root.canEnterVinyl && worldData.activeTab!=="home")root.enforceTasteGate();
        }
    }
    VinylPersonalLibraryData {
        id:personalData;session:session
        query:worldData.browseStates.library.query;sortOrder:worldData.browseStates.library.sortOrder
        onAlbumRequested:id=>root.openAlbum(id)
        onArtistRequested:item=>root.activateItem(item)
        onSongRequested:song=>root.showBrowseSong(song.album,song.index,song.editionId)
        onImportFolderRequested: if(root.canEnterVinyl)audioFolderDialog.open()
    }
    Connections {
        target:session
        function onPlaylistAddRequested(rows){if(root.canEnterVinyl)personalData.openAddPicker(rows);}
        function onProfileActiveChanged(){if(!root.canEnterVinyl)Qt.callLater(root.enforceTasteGate);}
    }
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
        visible: !root.videoFullscreen
        anchors {left:parent.left;right:parent.right;top:parent.top;margins:theme.margin}
        backdrop:root.chromeBackdrop;activeMedium:"Vinyl";lifecycleActive:root.lifecycleActive
        onAccountClicked:(anchorRight,anchorBottom)=>root.accountClicked(anchorRight,anchorBottom)
        onDlnaClicked:(anchorRight,anchorBottom)=>root.dlnaClicked(anchorRight,anchorBottom)
        onHomeRequested:root.homeRequested()
        onMediumSelected:medium=>root.mediumSelected(medium)
        onSearchClicked:root.enterDiscoverSearch(true,false)
        onMinimizeClicked:root.minimizeClicked();onFullscreenClicked:root.fullscreenClicked();onPowerClicked:root.powerClicked()
    }
    Flickable {
        id:worldScroll;objectName:"vinylWorldScroll";x:theme.margin;y:top.y+top.height+38;width:parent.width-2*x;height:Math.max(0,root.contentBottom-y)
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
            VinylWorld {
                id:world;y:-worldViewport.y;width:parent.width;worldData:worldData;backdrop:root.chromeBackdrop;pageFlick:worldScroll;dockedInset:70;dockedTabBar:dockedTabs;currentTab:"library"
                onCurrentTabChanged: {
                    if(!root.canEnterVinyl && currentTab!=="home"){root.enforceTasteGate();return;}
                    if(root.worldVisible){root.page=currentTab;root.searchOpen=false;root.artistSearchOpen=false;}
                }
            }
        }
    }
    Loader {
        id:searchLayer
        anchors.fill:parent;z:30
        active:root.searchOpen
        visible:active && root.lifecycleActive
        sourceComponent: VinylSearchPage {
            session:root.session;worldData:root.worldData;backdrop:root.backdrop
            artistPicker:root.artistSearchOpen;initialState:root.searchSnapshot
            onBackRequested:root.closeSearch()
            onItemRequested:item=>root.activateSearchItem(item)
            onMinimizeRequested:root.minimizeClicked()
            onFullscreenRequested:root.fullscreenClicked()
            onCloseRequested:root.powerClicked()
        }
    }
    WorldTabBar {
        id:dockedTabs
        objectName:"vinylDockedTabs";x:theme.margin;y:worldScroll.y;width:worldScroll.width;z:5
        visible:root.canEnterVinyl && root.worldVisible && worldScroll.contentY>world.pickerParkY
        compact:true;backdrop:root.chromeBackdrop;track:worldScroll.contentY;tabPrefix:visible ? "vinylTab" : "vinylHiddenDockedTab"
        tabModel:[{key:"home",label:"Home"},{key:"discover",label:"Discover"},{key:"library",label:"Library"}]
        currentTab:world.currentTab;onTabRequested:tab=>world.setTab(tab)
    }
    BackAction {
        focusOnPointer: false
        id:albumBack;objectName:"vinylAlbumBack";x:theme.margin;y:top.y+top.height+24;height:44;label:"Back";visible:root.canEnterVinyl && root.page==="album";onTriggered:root.goBack();Keys.onReturnPressed:root.goBack();Keys.onEnterPressed:root.goBack()}
    VinylAlbumPage {
        id:album;x:0;y:albumBack.y+albumBack.height+8;width:parent.width;height:Math.max(0,root.contentBottom-y)
        visible:root.canEnterVinyl && root.page==="album";pageData:session;backdrop:root.chromeBackdrop;artistNavigationEnabled:true;shuffleEnabled:true;saveEnabled:true;scrollbarVisible:false;songNavigationEnabled:true;queueActionsEnabled:true
        onSongRequested: index => {var record=session.shownEdition.sourceAlbum;var flat=index;for(var d=0;d<session.shownDisc;d++)flat+=session.shownEdition.discs[d].length;if(flat>=0)root.showBrowseSong(record,flat,session.shownEditionId);}
    }
    VinylArtistPage {
        id:artist;x:0;y:top.y+top.height+24;width:parent.width;height:Math.max(0,root.contentBottom-y)
        visible:root.canEnterVinyl && root.page==="artist";artistData:artistData;worldData:worldData;backdrop:root.chromeBackdrop
        onBackRequested:root.goBack();onArtistRequested:name=>root.showArtist(name)
        onAlbumRequested:record=>root.openAlbum(record.id)
        onSongRequested:value=>root.showBrowseSong(value.album,value.index)
    }
    VinylSongPage {
        id:song;x:0;y:root.videoFullscreen ? 0 : top.y+top.height+(root.videoDisplayed ? 8 : 24);width:parent.width
        height:root.videoFullscreen ? root.height : Math.max(0,(root.videoDisplayed ? root.height : root.contentBottom)-y)
        visible:root.canEnterVinyl && root.page==="song";pageData:root.browsingSong && !browseSong.isCurrent ? browseSong : session;browseMode:root.browsingSong;artistNavigationEnabled:true
        videoFullscreen: root.videoFullscreen
        videoControlsVisible: root.videoHudVisible
        onVideoFullscreenRequested: root.toggleVideoFullscreen()
        onVideoInteraction: if(root.videoDisplayed)root.revealVideoHud()
        backLabel:"Back";onBackRequested:root.goBack();onArtistRequested:name=>root.showRecordArtist(name,root.browsingSong ? browseSong.album : session.playingEdition.sourceAlbum || {})
        onPlayRequested:if(browseSong.playingTrack.available!==false)browseSong.play()
        onCreditsRequested:{creditsPopup.pageData=song.pageData;creditsPopup.open()}
        onAlbumRequested:{if(!root.canEnterVinyl){root.enforceTasteGate();return;}if(root.browsingSong)root.openAlbum(browseSong.album.id);else {root.pushRoute();session.showPlayingAlbum();root.page="album";root.parkFocus();}}
    }
    VinylHomeMixPage {
        id:homeMix;x:0;y:top.y+top.height+24;width:parent.width;height:Math.max(0,root.contentBottom-y)
        visible:root.canEnterVinyl && root.page==="mix";session:session;backdrop:root.chromeBackdrop
        mixView:session.plainData(recommendations.homeMixView)
        onBackRequested:root.goBack()
        onPlayRequested:(index,shuffle)=>session.playHomeSongs(homeMix.songs,index,shuffle,{})
        onQueueRequested:session.appendHomeSongs(homeMix.songs,false)
        onSaveRequested:homeMix.savedPlaylistId=personalData.createPlaylist(homeMix.mixView.title,session.rowsForSongs(homeMix.songs))
        onRetryRequested:recommendations.openHomeMix(String(homeMix.mixView.id || "personal"))
    }
    Row {
        objectName:"vinylStatusRow";x:root.footerLeft;y:root.contentBottom+8;width:parent.width-x-theme.margin;spacing:12
        Text {
            objectName:"vinylStatus"
            width:parent.width-(cancelImport.visible ? cancelImport.width+12 : 0)-(retry.visible ? retry.width+12 : 0)-(catalogueRetry.visible ? catalogueRetry.width+12 : 0)-(radioRetry.visible ? radioRetry.width+12 : 0)
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
            text:session.persistenceError || session.personalError || personalData.error || artistData.persistenceError || worldData.persistenceError || (root.importing ? root.importProgressText : session.error || root.homeListeningError || root.homeRadioStatus || (root.page==="artist" ? artistData.catalogueStatus : contextualBrowseStatus || root.importStatus))
            color:!root.importing && (session.error || contextualBrowseStatus) ? theme.gold : theme.inkDim
            font.family:theme.ui;font.pixelSize:12;wrapMode:Text.WordWrap
        }
        VinylWorldButton {id:cancelImport;objectName:"vinylCancelImport";label:"Cancel import";symbol:"cancel";backdrop:root.chromeBackdrop;visible:root.importing;onTriggered:{session.cancelImport();root.importStatus="Import cancelled. Your library was kept."}}
        VinylWorldButton {id:catalogueRetry;objectName:"vinylCatalogueRetry";label:"Retry catalogue";symbol:"retry";backdrop:root.chromeBackdrop;visible:root.page==="artist" && artistData.online && !artistData.loading && !!artistData.catalogueStatus;onTriggered:if(artistData.catalogueService)artistData.catalogueService.retryArtist()}
        VinylWorldButton {id:retry;objectName:"vinylRetryTrack";label:"Retry track";symbol:"retry";backdrop:root.chromeBackdrop;visible:session.error!=="";onTriggered:session.retry()}
        VinylWorldButton {id:radioRetry;objectName:"vinylRadioRetry";label:"Retry radio";symbol:"retry";backdrop:root.chromeBackdrop;visible:root.canEnterVinyl && recommendations.homeRadioActive && recommendations.homeRadioState==="error";onTriggered:{session.browseError="";recommendations.refillHomeRadio(root.queuedAudioIds());}}
    }
    VinylCreditsPopup {id:creditsPopup;pageData:session;backdrop:root.chromeBackdrop;x:Math.max(0,root.width-width-theme.margin);y:Math.max(0,bar.y-height-12)}
    VinylPlaylistPicker {id:playlistPicker;libraryData:personalData;backdrop:root.chromeBackdrop}
    Connections {target:session;function onProfileGenerationChanged(){playlistPicker.close();}}
    VinylQueuePanel {id:queuePanel;pageData:session;backdrop:root.chromeBackdrop;onSongRequested:root.showSong()}
    VinylPlayerBar {
        id:bar;x:root.footerLeft;y:root.height-height-18;width:root.width-x-theme.margin
        visible:root.canEnterVinyl && (session.playingIndex>=0 || root.page==="song") && opacity>0.01
        opacity:root.videoDisplayed && !root.videoHudVisible ? 0 : 1
        enabled:visible
        Behavior on opacity { NumberAnimation { duration: 140 } }
        pageData:session;backdrop:root.chromeBackdrop;page:root.page==="song" ? "song" : "album";queueEnabled:true;trackNavigationEnabled:true
        videoFullscreen: root.videoFullscreen
        onVideoFullscreenRequested: root.toggleVideoFullscreen()
        HoverHandler {
            id: videoHudHover
            onHoveredChanged: if(hovered && root.videoDisplayed)root.revealVideoHud()
        }
        songPageData:root.page==="song" ? song.pageData : session
        showSongPlay:root.page==="song" && root.browsingSong && !browseSong.isCurrent
        onSelectedSongPlayRequested:if(browseSong.playingTrack.available!==false)browseSong.play()
        onSongRequested:root.showSong();onCreditsRequested:{creditsPopup.pageData=bar.songData;creditsPopup.open()}
    }
    Component.onCompleted:{root.componentReady=true;session.refreshLibrary();if(root.canEnterVinyl)root.focusLibrary();else root.enforceTasteGate();}
}
