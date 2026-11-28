import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Colosseum.Vinyl 1.0
import ".."
import "VinylCardPresentation.js" as Presentation

// One Vinyl session, detail router and player, shared by the house and legacy views.
Item {
    id: root
    objectName: "vinylRuntime"
    required property WorldPage browseShell
    property bool hostReady: true
    readonly property Item world: browseShell ? browseShell.board : null
    property string medium: "Vinyl"
    property bool lifecycleActive: true
    property bool homeLanding: false
    property bool deferInitialRestore: false
    property bool resumePresentationPending: false
    visible: !resumePresentationPending
    property Item backdrop
    property Item blackBrowseBackdrop
    readonly property Item browseBackdrop: blackBrowseBackdrop || backdrop
    property string page: "library"
    property bool videoFullscreen: false
    property var pendingVideoOpen: null
    readonly property string videoOpenContextKey: JSON.stringify([session.profileScope,session.profileGeneration,session.activeQueueRow ? session.activeQueueRow.queueId : ""])
    onVideoOpenContextKeyChanged: root.cancelPendingVideoOpen(false)
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
    readonly property Flickable worldScroll: browseShell ? browseShell.pageFlickable : null
    property alias worldView: root.world
    property alias personalLibrary: personalData
    readonly property var recommendationService: recommendations
    readonly property bool canEnterVinyl: componentReady && session.profileActive && !session.shuttingDown
        && session.profileScope !== "" && !!session.source
    property string importStatus: ""
    readonly property bool importing: session.importing
    property var routeStack: []
    onRouteStackChanged: root.cancelPendingVideoOpen(true)
    property var feedbackRequest: null
    property string feedbackError: ""
    readonly property var recommendationPage: feedbackLayer.item
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
    property Item shellHudButton: null
    property bool searchOpen: false
    property bool artistSearchOpen: false
    property string homeListeningError: ""
    property var pendingMixPlay: null
    readonly property string homeRadioStatus: !canEnterVinyl || !recommendations.homeRadioActive ? ""
        : recommendations.homeRadioState==="error" ? "Radio could not load more music."
        : recommendations.homeRadioState==="exhausted" && session.sourceState==="ended" ? "Radio finished." : ""
    property var searchSnapshot: null
    property var searchOriginCatalogue: null
    readonly property var searchPage: searchLayer.item
    readonly property real footerLeft: Math.max(theme.margin, shellDockRight > 0 ? shellDockRight + 12 : 0)
    property bool hudOpen: false
    property string lastHudPlaybackOccurrence: ""
    readonly property bool hudHasMedia: canEnterVinyl && session.playingIndex >= 0
    onHudHasMediaChanged: if (!hudHasMedia) hudOpen = false
    function toggleHud() {
        if (hudHasMedia) hudOpen = !hudOpen
    }
    property real contentBottomInset: 0
    // The song/video viewport and status row still reserve space above the player.
    readonly property bool statusRowShown: statusText.text.length > 0 || cancelImport.visible
        || retry.visible || catalogueRetry.visible || radioRetry.visible
    readonly property real playerBottomMargin: 18
    readonly property real playerContentGap: videoDisplayed || !statusRowShown ? 14 : 54
    readonly property real contentBottom: bar.visible
        ? bar.y - playerContentGap
        : height - Math.max(62, shellDockBottomExtent)
    readonly property bool worldVisible: canEnterVinyl
        && ["home","songs","albums","artists","discover","explore","library"].indexOf(page)>=0
    property bool fullWindowDetails: false
    // Show the album shell immediately, without committing the route until the
    // requested release arrives. Cancel/error leaves the original route intact.
    readonly property bool openingAlbum: canEnterVinyl && !!pendingAlbumRoute && !pendingAlbumRoute.homeAction
        && session.browsing && session.browseOperation === "album"
    readonly property Item detailView: openingAlbum || page === "album" ? album
        : page === "artist" ? artist : page === "song" ? song : page === "mix" ? homeMix : null
    readonly property bool fullWindowDetail: fullWindowDetails && canEnterVinyl && !!detailView
    readonly property Item detailTitleBar: detailView === album ? albumTitle : detailView === artist ? artistTitle
        : detailView === song ? songTitle : detailView === homeMix ? mixTitle : null
    readonly property string detailBackLabel: {
        const previous = openingAlbum ? pendingAlbumRoute : routeStack[routeStack.length - 1];
        if (!previous) return qsTr("Library");
        if (previous.searchOpen) return qsTr("Search");
        switch (previous.page) {
        case "artist": return qsTr("Artist");
        case "album": return qsTr("Album");
        case "song": return qsTr("Now Playing");
        case "mix": return qsTr("Mix");
        case "feedback": return qsTr("Recommendations");
        default: return qsTr("Vinyl");
        }
    }
    readonly property bool detailPage: canEnterVinyl && !!detailView
    readonly property bool detailBackdrop: detailPage && (detailView !== artist || !blackBrowseBackdrop)
    readonly property bool browsingShell: !detailPage && page!=="feedback"
    readonly property Item chromeBackdrop: fullWindowDetail && detailView === artist ? artist.portraitBackdrop
        : detailBackdrop || !browseBackdrop ? vinylBackdrop : browseBackdrop
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
    property bool resettingUnavailableSession: false
    onCanEnterVinylChanged: if (componentReady && !canEnterVinyl) Qt.callLater(root.resetUnavailableSession)
    onPageChanged: if (componentReady && !canEnterVinyl && page !== "home") resetUnavailableSession()
    function resetUnavailableSession() {
        if(!root.componentReady)return false;
        if (root.canEnterVinyl) return true;
        if(!session.paused)session.paused=true;
        if(recommendations.homeRadioActive)recommendations.stopHomeRadio();
        if (root.resettingUnavailableSession) return false;
        root.resettingUnavailableSession = true;
        try {
            root.cancelNamedCollectionOpen();
            if (session.browsing) session.cancelBrowse();
            pendingAlbumRoute = null;
            routeStack = [];
            feedbackRequest = null;
            feedbackError = "";
            searchOpen = false;
            artistSearchOpen = false;

            if(queuePanel)queuePanel.close();
            if(playlistPicker)playlistPicker.close();
            if(audioDialog)audioDialog.close();
            if(audioFolderDialog)audioFolderDialog.close();
            world.listKey = "";
            if (world.currentTab !== "home") world.setTab("home");
            if (worldData.activeTab !== "home")
                worldData.selectTab("home");
            page = "home";
        } finally {
            root.resettingUnavailableSession = false;
        }
        Qt.callLater(root.parkFocus);
        return false;
    }
    function captureRoute() {
        var focusItem=Window.window ? Window.window.activeFocusItem : null;
        return {page:page,feedbackRequest:feedbackRequest,searchOpen:searchOpen,searchSnapshot:searchSnapshot,searchOriginCatalogue:searchOriginCatalogue,focus:focusItem,keyboardFocus:!!focusItem && focusItem.accessibleName!==undefined,
            worldY:worldScroll.contentY,albumY:album.contentY,artistY:artist.contentY,
            browseState:typeof worldData.captureBrowseState==="function" ? worldData.captureBrowseState() : null,
            worldState:world.captureRouteState(),
            edition:session.shownEdition,disc:session.shownDisc,
            artistName:artistData.name,artistId:artistData.artistId,artistCover:artistData.openingCover,artistTab:artistData.activeTab,artistCategory:artist.selectedCategory,artistFilter:artistData.filter,artistCategoryFilter:artistData.categoryFilter,artistNewest:artistData.newestFirst,artistAllSongs:artistData.allSongs,
            browsingSong:browsingSong,songAlbum:browseSong.album,songIndex:browseSong.trackIndex,songEdition:browseSong.editionId};
    }
    function pushRoute() {root.cancelPendingVideoOpen(true);routeStack=routeStack.concat([captureRoute()]);}
    function showRecommendationActions(request) {
        if(!canEnterVinyl || !request || request.generation!==worldData.recommendationProfileGeneration()) return false;
        session.cancelBrowse();pendingAlbumRoute=null;
        pushRoute();feedbackRequest=request;feedbackError="";
        searchOpen=false;artistSearchOpen=false;page="feedback";
        Qt.callLater(root.parkFocus);
        return true;
    }
    function applyRecommendationAction(ref,action,generation) {
        if(page!=="feedback" || !feedbackRequest || generation!==feedbackRequest.generation) return false;
        if(!worldData.setRecommendationFeedback(ref,action,generation)) {
            feedbackError=worldData.persistenceError || qsTr("Could not update recommendations. Try again.");
            return false;
        }
        return goBack();
    }
    onLifecycleActiveChanged: {
        if (lifecycleActive) {
            hudOpen = false;
            revealVideoHud();
        }
        else {root.cancelPendingVideoOpen(true);exitVideoFullscreen();videoHudVisible=true;videoHudTimer.stop();}
    }
    function cancelPendingVideoOpen(cancelLookup) {
        const pending=pendingVideoOpen;
        pendingVideoOpen=null;
        if(!cancelLookup || !pending || pending.context!==videoOpenContextKey
                || !session.videoBusy || session.videoShown) return;
        if(pending.startsPlayback) session.suspendPlayback();
        else if(session.videoSwitchCancellable) session.toggleVideo();
    }
    function finishPendingVideoOpen() {
        const pending=pendingVideoOpen;
        if(!pending)return;
        if(pending.context!==videoOpenContextKey){cancelPendingVideoOpen(false);return;}
        if(!canEnterVinyl || !visible || !lifecycleActive || searchOpen || artistSearchOpen){
            cancelPendingVideoOpen(true);return;
        }
        if(session.videoShown){pendingVideoOpen=null;showSong();return;}
        if(!session.videoBusy)cancelPendingVideoOpen(false);
    }
    function requestHudVideo() {
        if(!canEnterVinyl || !visible || !lifecycleActive || searchOpen || artistSearchOpen)return;
        if(pendingVideoOpen){cancelPendingVideoOpen(true);return;}
        if(session.videoBusy || session.videoShown || (page==="song" && (!browsingSong || browseSong.isCurrent))){
            session.toggleVideo();return;
        }
        if(!session.hasVideo || !session.loaded || !session.activeQueueRow)return;
        pendingVideoOpen={context:videoOpenContextKey};
        session.toggleVideo();
        finishPendingVideoOpen();
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
        if(videoFullscreen && videoDisplayed && visible && lifecycleActive && !searchOpen
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
            if (root.fullWindowDetail && songTitle.visible) songTitle.backItem.focusControl(Qt.BacktabFocusReason);
            else song.focusBackControl();
        });
    }
    Timer {
        id: videoHudTimer
        interval: 2500
        running: root.videoFullscreen && root.videoDisplayed && root.visible && root.lifecycleActive && !root.searchOpen
            && root.videoHudVisible && !session.paused
            && !videoHudHover.hovered && !root.focusInside(bar)
        onTriggered: if(root.videoFullscreen)root.videoHudVisible=false
    }
    Shortcut {
        sequence: "F"
        enabled: root.visible && root.lifecycleActive && root.videoDisplayed
            && !root.searchOpen && root.videoShortcutAllowed()
        onActivated: root.toggleVideoFullscreen()
    }
    Connections {
        target: root
        function onPageChanged(){root.pendingMixPlay=null;root.cancelPendingVideoOpen(true);if(root.page!=="song")root.exitVideoFullscreen();}
        function onSearchOpenChanged(){if(root.searchOpen){root.cancelPendingVideoOpen(true);root.exitVideoFullscreen();}else if(root.videoDisplayed)root.revealVideoHud();}
        function onArtistSearchOpenChanged(){if(root.artistSearchOpen)root.cancelPendingVideoOpen(true);}
        function onVisibleChanged(){if(!root.visible)root.cancelPendingVideoOpen(true);}
        function onCanEnterVinylChanged(){if(!root.canEnterVinyl)root.cancelPendingVideoOpen(true);}
    }
    Connections {
        target: session
        function onVideoShownChanged(){root.finishPendingVideoOpen();if(!session.videoShown)root.exitVideoFullscreen();}
        function onVideoBusyChanged(){root.finishPendingVideoOpen();}
        function onErrorChanged(){
            if(session.error && root.pendingVideoOpen && root.pendingVideoOpen.startsPlayback)
                root.cancelPendingVideoOpen(false);
        }
        function onPausedChanged(){if(root.videoDisplayed)root.revealVideoHud();}
        function onProfileGenerationChanged(){root.cancelPendingVideoOpen(false);root.exitVideoFullscreen();}
    }
    function enterDiscoverSearch(online,modeToggle) {
        if(searchOpen) return;
        if(!root.canEnterVinyl){root.resetUnavailableSession();return;}
        if(page==="home" && worldData.tastePickerOpen) {
            Qt.callLater(function() {
                if(root.page==="home" && !root.searchOpen && worldData.tastePickerOpen)
                    world.focusArtistSearch();
            });
            return;
        }
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
        if(!artistSearchOpen && !root.canEnterVinyl){root.resetUnavailableSession();return;}
        if(searchLayer.item) searchLayer.item.fillAndSearch(searchLayer.item.query);
        else worldData.submitSearch();
    }
    function setQuery(value) {
        if(!artistSearchOpen && !root.canEnterVinyl){root.resetUnavailableSession();return;}
        if(searchLayer.item) searchLayer.item.query=String(value || "");
        else worldData.setQuery(value);
    }
    function activateSearchItem(item) {
        if(artistSearchOpen) {worldData.togglePickerArtist(item);return;}
        if(!root.canEnterVinyl){root.resetUnavailableSession();return;}
        searchSnapshot=searchLayer.item.captureState();
        if(item.kind==="songs" && item.record)
            showBrowseSong(item.record,item.trackIndex);
        else activateItem(item);
    }
    // A destination starts at a silent focus origin, as in Main's keyboard ignition.
    // Tab still enters the shared focus chain; opening a page never selects Back/Play.
    function parkFocus() {
        if (!searchOpen || openingAlbum) (fullWindowDetail ? detailView : root).forceActiveFocus(Qt.OtherFocusReason);
    }
    function focusLibrary() {Qt.callLater(root.parkFocus);}
    function showHome() {
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        var alreadyOnHome=world.currentTab==="home";
        var homeWasVisible=worldData.homePageVisible;
        searchOpen=false;artistSearchOpen=false;
        session.cancelBrowse();pendingAlbumRoute=null;routeStack=[];world.listKey="";world.setTab("home");
        if(alreadyOnHome) {
            if(homeWasVisible)worldData.selectTab("home");
            worldData.refreshHome();
        }
        page="home";
        world.parkPage(0);Qt.callLater(root.parkFocus);
    }
    function openHomeDestination(tab,section,id) {
        if(tab==="discover"){root.enterDiscoverSearch(true,false);return;}
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        root.pushRoute();
        searchOpen=false;artistSearchOpen=false;
        session.cancelBrowse();pendingAlbumRoute=null;world.listKey="";world.setTab("library");page="library";
        worldData.setQuery("");personalData.setSection(section || "albums");
        if(id)personalData.openPlaylist(id);
        else if(section==="playlists")personalData.openPlaylist("");
        world.parkPage(Math.max(0,world.contentTop-world.dockedInset));Qt.callLater(root.parkFocus);
    }
    function showLibrary() {
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        searchOpen=false;artistSearchOpen=false;
        session.cancelBrowse();pendingAlbumRoute=null;session.refreshLibrary();routeStack=[];world.listKey="";world.setTab("library");
        if(typeof worldData.selectTab==="function") worldData.selectTab("library");
        page="library";world.parkPage(Math.max(0,world.contentTop-world.dockedInset));focusLibrary();
    }
    function restoreAlbumOrigin(origin) {
        Qt.callLater(function() {
            if (root.openingAlbum || !origin || root.page !== origin.page || root.searchOpen !== origin.searchOpen) return;
            if (origin.focus && origin.focus.visible && origin.focus.enabled)
                origin.focus.forceActiveFocus(Qt.OtherFocusReason);
            else root.parkFocus();
        });
    }
    function openAlbum(id,action,item) {
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        pendingAlbumRoute=Object.assign(captureRoute(),{homeAction:action || "",homeItem:item || {}});session.openAlbum(id);
        if (openingAlbum) Qt.callLater(root.parkFocus);
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
        if(!root.canEnterVinyl) {root.resetUnavailableSession();namedCollectionOpened(false,qsTr("Music is unavailable for this profile."));return;}
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
        if(!root.canEnterVinyl) {root.resetUnavailableSession();root.namedCollectionOpened(false,qsTr("Music is unavailable for this profile."));return;}
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
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        if(session.playingIndex<0) return;
        session.cancelBrowse();
        if(page!=="song" || browsingSong) pushRoute();
        searchOpen=false;artistSearchOpen=false;
        browsingSong=false;page="song";
        Qt.callLater(root.parkFocus);
    }
    function showBrowseSong(record,index,editionId) {
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        session.cancelBrowse();pushRoute();searchOpen=false;artistSearchOpen=false;browseSong.album=record;browseSong.editionId=editionId || "";browseSong.trackIndex=index;
        browsingSong=true;page="song";Qt.callLater(root.parkFocus);
    }
    function showArtist(name) {
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        session.cancelBrowse();pushRoute();searchOpen=false;artistSearchOpen=false;artistData.open(name);artist.contentY=0;artist.selectedCategory="";page="artist";
        Qt.callLater(root.parkFocus);
    }
    function showRecordArtist(name,record) {
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        var at=(record.artists || []).indexOf(name);
        if(at>=0 && (record.artistIds || [])[at]) {showArtist({name:name,id:record.artistIds[at]});return;}
        var tracks=record.tracks || [];
        for(var i=0;i<tracks.length;i++) {
            var index=(tracks[i].artists || []).indexOf(name);
            if(index>=0 && (tracks[i].artistIds || [])[index]) {showArtist({name:name,id:tracks[i].artistIds[index]});return;}
        }
        showArtist(name);
    }
    property var pendingDiscoverItem: null
    property string discoverStatus: ""
    readonly property bool discoverActivationAllowed: page==="discover" && canEnterVinyl
        && visible && lifecycleActive && !searchOpen && !artistSearchOpen
    readonly property bool homeActivationAllowed: worldVisible && canEnterVinyl
        && visible && lifecycleActive && !searchOpen && !artistSearchOpen
    readonly property bool exploreActivationAllowed: homeActivationAllowed && !!world && world.currentTab === "explore"
    onDiscoverActivationAllowedChanged: if(!discoverActivationAllowed && pendingDiscoverItem && !pendingDiscoverItem.fromHome) root.cancelDiscoverItem()
    onHomeActivationAllowedChanged: if(!homeActivationAllowed && pendingDiscoverItem && pendingDiscoverItem.fromHome) root.cancelDiscoverItem()
    onExploreActivationAllowedChanged: if (!exploreActivationAllowed && pendingDiscoverItem && pendingDiscoverItem.fromExplore) root.cancelDiscoverItem()
    Component.onDestruction: root.cancelDiscoverItem()

    function cancelDiscoverItem() {
        var pending=pendingDiscoverItem;
        pendingDiscoverItem=null;
        discoverStatus="";
        if(pending) pending.source.cancel("catalogue-item");
    }

    function resolveDiscoverItem(item,action,fromHome) {
        if(!(fromHome ? homeActivationAllowed : discoverActivationAllowed) || pendingDiscoverItem) return;
        var source=session.source;
        session.browseError="";
        var id=source ? source.request("catalogue-item","resolveCatalogueItem",item.id,{item:item}) : -1;
        if(id<0) {
            discoverStatus=qsTr("Couldn't look up “%1”. Select it again to retry.").arg(item.title);
            return;
        }
        pendingDiscoverItem={id:id,item:item,source:source,generation:session.profileGeneration,
            action:action || "open",fromHome:fromHome===true,fromExplore:exploreActivationAllowed};
        discoverStatus="";
    }

    function acceptDiscoverItem(id,data,error) {
        var pending=pendingDiscoverItem;
        if(!pending || pending.id!==id) return;
        pendingDiscoverItem=null;
        discoverStatus="";
        if(!(pending.fromExplore ? exploreActivationAllowed : pending.fromHome ? homeActivationAllowed : discoverActivationAllowed)
            || session.source!==pending.source
            || session.profileGeneration!==pending.generation) return;
        var item=pending.item;
        if(error || !data || data.version!==1 || data.id!==item.id || data.kind!==item.kind
            || typeof data.matched!=="boolean") {
            discoverStatus=qsTr("Couldn't look up “%1”. Select it again to retry.").arg(item.title);
            return;
        }
        if(!data.matched) {
            discoverStatus=qsTr("Couldn't find “%1” on YouTube Music.").arg(item.title);
            return;
        }
        if(pending.fromHome && pending.action!=="open") {
            if(item.kind==="albums" && pending.action==="save" && data.albumId)openAlbum(data.albumId,"save",{});
            else if(item.kind==="artists" && pending.action==="radio" && data.artistId)
                recommendations.startHomeRadio({provider:"youtube-music",providerId:String(data.artistId).replace(/^ytm:artist:/,""),name:item.title});
            else if(data.record) performHomeRecordAction(data.record,{},pending.action);
            else discoverStatus=qsTr("This pick is unavailable. Try another one.");
            return;
        }
        if(item.kind==="songs" && data.record && data.record.tracks && data.record.tracks.length) {
            session.playAlbum(data.record,0);
            root.showSong();
        } else if(item.kind==="artists" && data.artistId) {
            showArtist({id:data.artistId,name:item.title,cover:data.cover || ""});
        } else if(item.kind==="albums" && data.albumId) {
            openAlbum(data.albumId);
        } else {
            discoverStatus=qsTr("Couldn't open “%1”. Select it again to retry.").arg(item.title);
        }
    }

    Connections {
        target: session.source
        ignoreUnknownSignals: true
        function onCompleted(lane,id,data,error) {
            if(lane==="catalogue-item") root.acceptDiscoverItem(id,data,error);
        }
        function onProfileLeaving() {root.cancelDiscoverItem();}
        function onShuttingDown() {root.cancelDiscoverItem();}
    }

    Connections {
        target: session
        function onProfileGenerationChanged() {root.cancelDiscoverItem();}
        function onSourceChanged() {root.cancelDiscoverItem();}
    }

    function activateItem(item) {
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        if(item.catalogueCard===true || (item.requiresResolution && item.catalogueItem)) {
            root.resolveDiscoverItem(item.catalogueItem || item,"open",page!=="discover");
            return;
        }
        if(item.kind==="mixes") {
            root.pushRoute();searchOpen=false;artistSearchOpen=false;
            session.browseError="";homeListeningError="";
            homeMix.savedPlaylistId="";recommendations.openHomeMix(String(item.mixId || "personal"));
            page="mix";homeMix.contentY=0;root.parkFocus();return;
        }
        if(Presentation.kind(item)==="radio") {
            homeListeningError="";
            recommendations.startHomeRadio(item.seedArtist || item);return;
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
    function performHomeRecordAction(record,item,action) {
        if(!record || !session.profileActive || session.shuttingDown)return;
        if(action==="radio") {
            var ids=record.artistIds || ((record.tracks || [])[0] || {}).artistIds || [];
            var id=ids.find(function(value){return String(value).indexOf("ytm:artist:")===0;});
            if(id)recommendations.startHomeRadio({provider:"youtube-music",providerId:id.slice(11),name:(record.artists || [])[0] || ""});
            else session.browseError=qsTr("Artist radio is unavailable for this pick.");
            return;
        }
        var index=Number.isInteger(item.resumeTrack) ? item.resumeTrack-1 : Number(item.trackIndex) || 0;
        if(index>=0 && index<(record.tracks || []).length)session.playAlbum(record,index);
    }
    function activateHomeItem(item,action) {
        if(!item || !homeActivationAllowed || item.available===false || item.unavailable===true)return;
        if(["open","play","save","radio"].indexOf(action)<0)return;
        if(action==="play" && Presentation.kind(item)==="artists")action="open";
        if(item.libraryItem && action==="open") {
            if(item.libraryAction==="openPlaylist")root.openHomeDestination("library","playlists",item.libraryItem.id);
            else if(item.libraryAction==="openDownloadedSong")showBrowseSong(item.libraryItem.album,item.libraryItem.index);
            else if(["activateAlbum","activateArtist","openSong"].indexOf(item.libraryAction)>=0)personalData[item.libraryAction](item.libraryItem);
            return;
        }
        if(item.requiresResolution && item.catalogueItem || item.catalogueCard===true) {
            resolveDiscoverItem(item.catalogueItem || item,action,true);return;
        }
        var kind=String(item.kind || "");
        if(action==="open"){activateItem(item);return;}
        if(action==="play" && kind==="albums" && item.resumeEntry){session.resumeSavedCheckpoint(item.resumeEntry);return;}
        if(action==="play" && item.row && item.album){
            session.playSongs([{album:item.album,track:item.row.track,index:item.index,editionId:item.editionId}],false);return;
        }
        if(action==="radio" || Presentation.kind(item)==="radio") {
            if(kind==="songs"){performHomeRecordAction(item.record || {},item,"radio");return;}
            var seed=item.seedArtist || item;
            if(seed.provider==="local"){
                var localSongs=personalData.availableSongs.filter(function(song){
                    var row=song.row || {};
                    var ids=(row.track || {}).artistIds || (row.edition || {}).artistIds || (song.album || {}).artistIds || [];
                    return ids.indexOf(seed.providerId || seed.id || seed.artistId)>=0;
                });
                if(localSongs.length)session.playSongs(localSongs,true);
                else session.browseError=qsTr("No local songs are available for this artist.");
                return;
            }
            recommendations.startHomeRadio(item.seedArtist || item);return;
        }
        if(kind==="mixes") {
            pendingMixPlay={id:item.mixId || "personal",generation:session.profileGeneration};
            recommendations.openHomeMix(pendingMixPlay.id);return;
        }
        var record=item.record || item.album || (item.libraryItem || {}).album || worldData.findAlbum(item.albumId || item.id);
        if(action==="save") {openAlbum((item.destination || {}).providerId || item.albumId || item.id,"save",item);return;}
        if(record){performHomeRecordAction(record,item,action);return;}
        if(kind==="albums")openAlbum((item.destination || {}).providerId || item.albumId || item.id,"play",item);
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
        if(pendingDiscoverItem) {root.cancelDiscoverItem();return true;}
        root.cancelPendingVideoOpen(true);
        if(videoFullscreen){exitVideoFullscreen();return true;}
        if (openingAlbum) {
            const origin = pendingAlbumRoute;
            pendingAlbumRoute = null;
            session.cancelBrowse();
            restoreAlbumOrigin(origin);
            return true;
        }
        if(searchOpen) {closeSearch();return true;}
        if(!root.canEnterVinyl) {
            if(artistSearchOpen) {artistSearchOpen=false;searchOpen=false;return true;}
            root.resetUnavailableSession();return true;
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
            if(artistData.name!==state.artistName || artistData.artistId!==state.artistId) artistData.open({name:state.artistName,id:state.artistId,cover:state.artistCover});
            artistData.activeTab=state.artistTab;artist.selectedCategory=state.artistCategory;artistData.filter=state.artistFilter;artistData.categoryFilter=state.artistCategoryFilter;artistData.newestFirst=state.artistNewest;artistData.allSongs=state.artistAllSongs;
            browsingSong=state.browsingSong;browseSong.album=state.songAlbum;browseSong.editionId=state.songEdition;browseSong.trackIndex=state.songIndex;
            feedbackRequest=state.feedbackRequest || null;feedbackError="";
            page=state.page;restoringRoute=false;
            Qt.callLater(function(){
                if(!root) return;
                // Finish the now-visible browse layout before restoring its offset.
                // Home's layout anchoring would otherwise rebase against positions
                // left over from when the browse shell was hidden.
                if (root.worldVisible && typeof world.restorePageOffset === "function")
                    world.restorePageOffset(state.worldY);
                else worldScroll.contentY=state.worldY;
                album.contentY=state.albumY;artist.contentY=state.artistY;
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
        if(!root.canEnterVinyl) {root.resetUnavailableSession();return;}
        if (!paths || !paths.length || session.importing) return;
        showLibrary();
        importStatus = "";
        try {
            var importId = session.importFilesAsync(paths);
            if (importId <= 0) importStatus = "Could not start the local import.";
        } catch (error) {
            importStatus = "Could not open local music. " + String(error);
        }
    }
    Keys.onEscapePressed: event => {event.accepted=root.goBack();}
    Keys.onTabPressed: event => {
        event.accepted = root.fullWindowDetail && root.detailView.activeFocus && root.detailTitleBar.visible;
        if (event.accepted) root.detailTitleBar.backItem.focusControl(Qt.TabFocusReason);
    }
    Keys.onPressed: event => {
        if(root.videoDisplayed)root.revealVideoHud();
        if (!world || !(root.activeFocus || world.activeFocus || (root.fullWindowDetail && root.detailView.activeFocus))
                || [Qt.Key_Up,Qt.Key_Down,Qt.Key_Left,Qt.Key_Right].indexOf(event.key)<0) return;
        if (root.openingAlbum) return;
        if (root.worldVisible) world.focusCurrent();
        else if (root.page==="album") album.focusPrimaryControl();
        else if (root.page==="song") {
            if (root.fullWindowDetail && songTitle.visible) songTitle.backItem.focusControl(Qt.TabFocusReason);
            else song.focusBackControl();
        }
        else if (root.page==="artist") artist.forceActiveFocus(Qt.TabFocusReason);
        event.accepted=true;
    }
    Theme {id:theme}
    VinylLiveSession {
        id:session
        deferInitialRestore: root.deferInitialRestore
        videoSurface: song.videoSurface
        onImportFinished: outcome => root.updateImportStatus(outcome)
        onBrowseErrorChanged: {
            if (root.pendingAlbumRoute && browseError && browseOperation === "album") {
                const origin = root.pendingAlbumRoute;
                root.pendingAlbumRoute = null;
                if (!origin.homeAction) root.restoreAlbumOrigin(origin);
            }
        }
        onAlbumOpened: {
            if(!root.canEnterVinyl){session.cancelBrowse();root.pendingAlbumRoute=null;root.resetUnavailableSession();return;}
            var pending=root.pendingAlbumRoute;
            if(pending && pending.homeAction) {
                root.pendingAlbumRoute=null;
                if(pending.homeAction==="save"){if(!session.saved)session.toggleSaved();}
                else root.performHomeRecordAction(session.shownEdition.sourceAlbum,pending.homeItem,pending.homeAction);
                return;
            }
            if(!root.restoringRoute)root.routeStack=root.routeStack.concat([root.pendingAlbumRoute || root.captureRoute()]);
            root.pendingAlbumRoute=null;root.searchSnapshot=null;root.searchOriginCatalogue=null;root.searchOpen=false;root.artistSearchOpen=false;root.page="album";Qt.callLater(root.parkFocus);
        }
        onPlaybackRequested: {
            const occurrence = session.activeQueueRow ? session.activeQueueRow.queueId : "";
            if (occurrence && occurrence !== root.lastHudPlaybackOccurrence) {
                root.lastHudPlaybackOccurrence = occurrence;
                if (root.hudHasMedia && root.lifecycleActive && root.page !== "song") root.hudOpen = true;
            }
            if(root.canEnterVinyl && root.visible && root.lifecycleActive && !root.searchOpen
                    && !root.artistSearchOpen && session.activeQueueRow && session.activeQueueRow.startWithVideo === true)
                root.pendingVideoOpen={context:root.videoOpenContextKey, startsPlayback:true};
            root.playbackRequested();
        }
        onArtistRequested: name => root.showRecordArtist(name,session.shownEdition.sourceAlbum || {})
        onQueueRequested: if(root.canEnterVinyl)queuePanel.open()
        onProfileGenerationChanged: {
            root.lastHudPlaybackOccurrence = "";
            root.cancelNamedCollectionOpen();root.namedCollectionReturn=null;root.importStatus="";
            root.feedbackRequest=null;root.feedbackError="";
            root.pendingMixPlay=null;
            pendingAlbumRoute=null;root.searchSnapshot=null;root.searchOriginCatalogue=null;root.searchOpen=false;root.artistSearchOpen=false;
            artistData.open({id:"",name:""});root.routeStack=[];root.page="home";
            if(world)world.resetRouteState("home");
            Qt.callLater(function(){
                if(!root)return;
                if(root.deferInitialRestore || root.page!=="home")return;
                if(root.canEnterVinyl){if(root.homeLanding)root.showHome();else root.showLibrary();}
                else root.resetUnavailableSession();
            });
        }
    }
    VinylLiveWorldData {id:worldData;session:session;personalLibrary:personalData;recommendationService:recommendations;
        canEnterVinyl: root.canEnterVinyl
        legacyCarouselEnabled: false
        homePageVisible: root.componentReady && root.visible && root.lifecycleActive && root.canEnterVinyl
            && root.page === "home" && root.world && root.world.currentTab === "home" && !root.world.listKey
            && !root.searchOpen && !root.artistSearchOpen && !worldData.tastePickerOpen
        onItemRequested: item => root.activateItem(item);onResumeRequested:root.activateItem({kind:"resume",resumeCurrent:true});onHomeDestinationRequested:(tab,section,id)=>root.openHomeDestination(tab,section,id);onRecommendationActionsRequested:request=>root.showRecommendationActions(request)}
    Connections {
        target:recommendations
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
            if (!append && root.page === "song") root.showSong();
        }
        function onListeningChanged() {
            var pending=root.pendingMixPlay,view=session.plainData(recommendations.homeMixView);
            if(pending && pending.generation===session.profileGeneration && view.id===pending.id && view.songs && view.songs.length) {
                root.pendingMixPlay=null;session.playHomeSongs(view.songs,0,false,{});
            }
            if(recommendations.homeRadioActive && recommendations.homeRadioState==="error")session.browseError="Radio could not load more music. Open the station again to retry.";
            Qt.callLater(root.refillHomeRadio);
        }
        ignoreUnknownSignals:true
    }
    Connections {
        target:session
        function onManualPlaybackStarted() {root.pendingMixPlay=null;root.homeListeningError="";recommendations.stopHomeRadio();}
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
                ? Object.assign({},personalData.homeCollections,{catalogueAlbums:worldData.libraryAlbums,catalogueSongs:personalData.availableSongs}) : ({});
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
        function onHomeActionRequested(item,action){root.activateHomeItem(item,action);}
        function onActiveTabChanged() {
            if(!root.canEnterVinyl && worldData.activeTab!=="home")root.resetUnavailableSession();
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
        function onProfileActiveChanged(){if(!root.canEnterVinyl)Qt.callLater(root.resetUnavailableSession);}
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
    VinylBackdrop {id:vinylBackdrop;anchors.fill:parent;visible:root.detailBackdrop || !root.browseBackdrop;source:root.openingAlbum ? "" : root.page==="song" ? song.pageData.playingEdition.cover : root.page==="album" ? session.shownEdition.cover : ""}
    TopBar {
        id:top;objectName:root.browsingShell ? "" : "vinylTopBar"
        visible: !root.browsingShell && !root.fullWindowDetail && !root.videoFullscreen
        anchors {left:parent.left;right:parent.right;top:parent.top;margins:theme.margin}
        backdrop:root.chromeBackdrop;activeMedium:"Vinyl";lifecycleActive:root.lifecycleActive
        onAccountClicked:(anchorRight,anchorBottom)=>root.accountClicked(anchorRight,anchorBottom)
        onDlnaClicked:(anchorRight,anchorBottom)=>root.dlnaClicked(anchorRight,anchorBottom)
        onHomeRequested:root.homeRequested()
        onMediumSelected:medium=>root.mediumSelected(medium)
        onSearchClicked:root.enterDiscoverSearch(true,false)
        onMinimizeClicked:root.minimizeClicked();onFullscreenClicked:root.fullscreenClicked();onPowerClicked:root.powerClicked()
    }
    // Keep the supplied view at the original browse layer. Derived QML children
    // otherwise stack after the player and intercept its pointer controls.
    Item { id: browseLayer; anchors.fill: parent }
    Binding { target: root.browseShell; property: "parent"; value: browseLayer; when: root.browseShell !== null }
    Binding { target: root.browseShell; property: "visible"; value: root.browsingShell; when: root.browseShell !== null }
    Binding { target: root.browseShell; property: "lifecycleActive"; value: root.lifecycleActive && root.browsingShell && !root.searchOpen; when: root.browseShell !== null }
    Binding { target: root.browseShell; property: "medium"; value: root.medium; when: root.browseShell !== null }
    Binding { target: root.browseShell; property: "backdrop"; value: root.chromeBackdrop; when: root.browseShell !== null }
    Binding { target: root.browseShell; property: "topBarObjectName"; value: root.browsingShell ? "vinylTopBar" : ""; when: root.browseShell !== null }
    VinylScrollInsets {
        id: scrollInsets
        viewportHeight: root.height
        hostBottomInset: root.contentBottomInset
        dockBottomExtent: root.shellDockBottomExtent
        playerBar: bar
        browseShell: root.browseShell
    }
    Binding { target: root.browseShell; property: "topBarTrackersEnabled"; value: false; when: root.browseShell !== null }
    Binding { target: root.browseShell; property: "navigationEnabled"; value: root.canEnterVinyl; when: root.browseShell !== null }
    Binding { target: root.browseShell; property: "contentVisible"; value: root.worldVisible; when: root.browseShell !== null }
    Connections {
        target: root.browseShell
        function onAccountClicked(anchorRight, anchorBottom) { root.accountClicked(anchorRight, anchorBottom); }
        function onDlnaClicked(anchorRight, anchorBottom) { root.dlnaClicked(anchorRight, anchorBottom); }
        function onHomeRequested() { root.homeRequested(); }
        function onMediumSelected(medium) { root.mediumSelected(medium); }
        function onSearchClicked() { root.enterDiscoverSearch(true, false); }
        function onMinimizeClicked() { root.minimizeClicked(); }
        function onFullscreenClicked() { root.fullscreenClicked(); }
        function onPowerClicked() { root.powerClicked(); }
    }
    Connections {
        target: root.world
        function onCurrentTabChanged() {
            if (!root.componentReady || !root.world) return;
            if (!root.canEnterVinyl && root.world.currentTab !== "home") { root.resetUnavailableSession(); return; }
            if (root.worldVisible) { root.page = root.world.currentTab; root.searchOpen = false; root.artistSearchOpen = false; }
        }
    }
    Loader {
        id:searchLayer
        anchors.fill:parent;z:30
        active:root.searchOpen
        visible:active && root.lifecycleActive && !root.openingAlbum
        sourceComponent: VinylSearchPage {
            session:root.session;worldData:root.worldData;backdrop:root.browseBackdrop
            artistPicker:root.artistSearchOpen;initialState:root.searchSnapshot
            onBackRequested:root.closeSearch()
            onItemRequested:item=>root.activateSearchItem(item)
            onMinimizeRequested:root.minimizeClicked()
            onFullscreenRequested:root.fullscreenClicked()
            onCloseRequested:root.powerClicked()
        }
    }
    Loader {
        id:feedbackLayer
        x:0;y:top.y+top.height+24;width:parent.width;height:Math.max(0,scrollInsets.viewportBottom-y)
        active:root.canEnterVinyl && root.page==="feedback" && !!root.feedbackRequest
        visible:active && !root.openingAlbum
        sourceComponent: VinylRecommendationPage {
            request:root.feedbackRequest;worldData:root.worldData;backdrop:root.chromeBackdrop;error:root.feedbackError
            contentEndPadding:scrollInsets.endPadding
            onBackRequested:root.goBack()
            onActionRequested:(ref,action,generation)=>root.applyRecommendationAction(ref,action,generation)
        }
    }
    // The runtime already fills Main's world loader, so the same page instances can
    // take over its canvas without moving the session, history, dock or player HUD.
    component DetailBar: TitlePageBar {
        objectName: visible ? "titlePageBar" : ""
        width: root.width
        z: 40
        inlineWindowControls: true
        visible: root.fullWindowDetail && !root.videoFullscreen && (!root.searchOpen || root.openingAlbum)
        backLabel: root.detailBackLabel
        world: "Vinyl"
        onBackRequested: {
            if (root.detailView === artist && artist.selectedCategory.length > 0) artist.closeCategory();
            else root.goBack();
        }
        onSearchRequested: {
            if (root.openingAlbum) root.goBack();
            root.enterDiscoverSearch(true, false);
        }
        onMinimizeRequested: root.minimizeClicked()
        onFullscreenRequested: root.fullscreenClicked()
        onQuitRequested: root.powerClicked()
    }
    BackAction {
        focusOnPointer: false
        id:albumBack;objectName:root.fullWindowDetails ? "" : "vinylAlbumBack";x:theme.margin;y:top.y+top.height+24;height:44;label:"Back";visible:!root.fullWindowDetails && root.canEnterVinyl && root.detailView===album;onTriggered:root.goBack();Keys.onReturnPressed:root.goBack();Keys.onEnterPressed:root.goBack()}
    VinylAlbumPage {
        id:album;x:0;y:root.fullWindowDetails ? 0 : albumBack.y+albumBack.height+8;width:parent.width;height:Math.max(0,scrollInsets.viewportBottom-y)
        titleBarHeight: root.fullWindowDetails ? albumTitle.height : 0
        contentEndPadding:scrollInsets.endPadding
        opening: root.openingAlbum
        visible:root.canEnterVinyl && root.detailView===album;pageData:session;backdrop:root.chromeBackdrop;artistNavigationEnabled:true;shuffleEnabled:true;saveEnabled:true;scrollbarVisible:false;songNavigationEnabled:true;queueActionsEnabled:true
        onSongRequested: index => {var record=session.shownEdition.sourceAlbum;var flat=index;for(var d=0;d<session.shownDisc;d++)flat+=session.shownEdition.discs[d].length;if(flat>=0)root.showBrowseSong(record,flat,session.shownEditionId);}
        DetailBar {
            id: albumTitle
            y: album.contentY
            backObjectName: root.fullWindowDetails ? "vinylAlbumBack" : ""
            solid: album.contentY > 40 || album.opening
        }
    }
    VinylArtistPage {
        id:artist;x:0;y:root.fullWindowDetails ? 0 : top.y+top.height+24;width:parent.width;height:Math.max(0,scrollInsets.viewportBottom-y)
        standalone: false
        sharedTitleBar: root.fullWindowDetails ? artistTitle : null
        contentEndPadding:scrollInsets.endPadding
        visible:root.canEnterVinyl && root.detailView===artist;artistData:artistData;worldData:worldData;backdrop:root.chromeBackdrop
        onBackRequested:root.goBack();onArtistRequested:name=>root.showArtist(name)
        onAlbumRequested:record=>root.openAlbum(record.id)
        onSongRequested:value=>root.showBrowseSong(value.album,value.index)
        onPlaySongRequested:value=>session.playSongs([value],false)
        onPlayVideoRequested: record => {
            root.cancelDiscoverItem();
            session.playArtistVideo(record,artistData.name,artistData.canonicalArtistId);
        }
        DetailBar {
            id: artistTitle
            y: artist.contentY
            backObjectName: root.fullWindowDetails ? "vinylArtistHeaderBack" : ""
            backLabel: artist.selectedCategory.length > 0 ? qsTr("Artist") : root.detailBackLabel
            solid: artist.contentY > 40 || artistData.loading
        }
    }
    VinylSongPage {
        id:song;x:0;y:root.fullWindowDetails || root.videoFullscreen ? 0 : top.y+top.height+24;width:parent.width
        // Keep the reader above the tall song HUD even while another route compacts it.
        readonly property real playerInset: root.videoFullscreen ? 0 : root.playerBottomMargin + bar.songHeight + root.playerContentGap
        height: root.fullWindowDetails ? root.height : Math.max(0, root.height - playerInset - y)
        contentBottomInset: root.fullWindowDetails ? playerInset : 0
        titleBarHeight: root.fullWindowDetails && !root.videoFullscreen ? songTitle.height : 0
        titleActionsHost: titleBarHeight > 0 ? songTitle.pageActionsHost : null
        visible:root.canEnterVinyl && root.detailView===song;pageData:root.browsingSong && !browseSong.isCurrent ? browseSong : session;browseMode:root.browsingSong && !browseSong.isCurrent;artistNavigationEnabled:true
        videoFullscreen: root.videoFullscreen
        videoControlsVisible: !root.videoFullscreen || root.videoHudVisible
        onVideoFullscreenRequested: root.toggleVideoFullscreen()
        onVideoInteraction: if(root.videoDisplayed)root.revealVideoHud()
        backLabel:"Back";onBackRequested:root.goBack();onArtistRequested:name=>root.showRecordArtist(name,root.browsingSong ? browseSong.album : session.playingEdition.sourceAlbum || {})
        onPlayRequested:if(browseSong.playingTrack.available!==false)browseSong.play()
        onAlbumRequested:{if(!root.canEnterVinyl){root.resetUnavailableSession();return;}if(root.browsingSong)root.openAlbum(browseSong.album.id);else {root.pushRoute();session.showPlayingAlbum();root.page="album";root.parkFocus();}}
        DetailBar {
            id: songTitle
            backObjectName: song.titleBarHeight > 0 ? "vinylSongBack" : ""
        }
    }
    VinylHomeMixPage {
        id:homeMix;x:0;y:root.fullWindowDetails ? 0 : top.y+top.height+24;width:parent.width;height:Math.max(0,scrollInsets.viewportBottom-y)
        titleBarHeight: root.fullWindowDetails ? mixTitle.height : 0
        contentEndPadding:scrollInsets.endPadding
        visible:root.canEnterVinyl && root.detailView===homeMix;session:session;backdrop:root.chromeBackdrop
        mixView:session.plainData(recommendations.homeMixView)
        onBackRequested:root.goBack()
        onPlayRequested:(index,shuffle)=>session.playHomeSongs(homeMix.songs,index,shuffle,{})
        onQueueRequested:session.appendHomeSongs(homeMix.songs,false)
        onSaveRequested:homeMix.savedPlaylistId=personalData.createPlaylist(homeMix.mixView.title,session.rowsForSongs(homeMix.songs))
        onRetryRequested:recommendations.openHomeMix(String(homeMix.mixView.id || "personal"))
        DetailBar {
            id: mixTitle
            y: homeMix.contentY
            backObjectName: root.fullWindowDetails ? "vinylHomeMixBack" : ""
            solid: homeMix.contentY > 40 || homeMix.mixView.status === "loading"
        }
    }
    BusyIndicator {
        objectName: "vinylLookupIndicator"
        x: root.footerLeft
        y: root.contentBottom - height
        width: 24; height: 24
        running: visible
        visible: !!root.pendingDiscoverItem || (!!root.pendingAlbumRoute && !!root.pendingAlbumRoute.homeAction
            && session.browsing && session.browseOperation === "album")
    }
    Row {
        z: root.searchOpen && session.browseError ? searchLayer.z + 1 : 0
        objectName:"vinylStatusRow";x:root.footerLeft;y:root.contentBottom+8;width:parent.width-x-theme.margin;spacing:12
        Text {
            id: statusText
            objectName:"vinylStatus"
            width:parent.width-(importProgress.visible ? importProgress.width+12 : 0)-(cancelImport.visible ? cancelImport.width+12 : 0)-(retry.visible ? retry.width+12 : 0)-(catalogueRetry.visible ? catalogueRetry.width+12 : 0)-(radioRetry.visible ? radioRetry.width+12 : 0)
            readonly property string catalogueNotice: root.page === "discover" && worldData.onlineMode
                && worldData.query.trim() && !session.browsing ? session.catalogueNotice : ""
            text: session.persistenceError || session.personalError || personalData.error || artistData.persistenceError
                || worldData.persistenceError || root.discoverStatus || session.error || session.browseError
                || root.homeListeningError || root.homeRadioStatus
                || (bar.slim && root.hudOpen ? session.torrentError || session.videoError || session.skippedNotice : "")
                || (root.page === "artist" ? artistData.catalogueStatus : catalogueNotice || session.downloadNotice || (root.importing ? "" : root.importStatus))
            color:session.error || session.browseError || catalogueNotice ? theme.gold : theme.inkDim
            font.family:theme.ui;font.pixelSize:12;wrapMode:Text.WordWrap
        }
        ProgressBar {
            id: importProgress
            objectName: "vinylImportProgress"
            visible: root.importing
            width: 100
            anchors.verticalCenter: cancelImport.verticalCenter
            indeterminate: session.importTotal <= 0
            // One fraction: separate to/value bindings clamp value against a stale `to`.
            value: session.importTotal > 0 ? Math.min(1, session.importProcessed / session.importTotal) : 0
        }
        VinylWorldButton {id:cancelImport;objectName:"vinylCancelImport";label:"Cancel import";symbol:"cancel";backdrop:root.chromeBackdrop;visible:root.importing;onTriggered:{session.cancelImport();root.importStatus="Import cancelled. Your library was kept."}}
        VinylWorldButton {id:catalogueRetry;objectName:"vinylCatalogueRetry";label:"Retry catalogue";symbol:"retry";backdrop:root.chromeBackdrop;visible:root.page==="artist" && artistData.online && !artistData.loading && !!artistData.catalogueError;onTriggered:if(artistData.catalogueService)artistData.catalogueService.retryArtist()}
        VinylWorldButton {id:retry;objectName:"vinylRetryTrack";label:"Retry track";symbol:"retry";backdrop:root.chromeBackdrop;visible:session.error!=="";onTriggered:session.retry()}
        VinylWorldButton {id:radioRetry;objectName:"vinylRadioRetry";label:"Retry radio";symbol:"retry";backdrop:root.chromeBackdrop;visible:root.canEnterVinyl && recommendations.homeRadioActive && recommendations.homeRadioState==="error";onTriggered:{session.browseError="";recommendations.refillHomeRadio(root.queuedAudioIds());}}
    }
    VinylPlaylistPicker {id:playlistPicker;libraryData:personalData;backdrop:root.chromeBackdrop}
    Connections {target:session;function onProfileGenerationChanged(){playlistPicker.close();}}
    VinylQueuePanel {id:queuePanel;pageData:session;actionData:root.page==="song" ? song.pageData : session;backdrop:root.chromeBackdrop;onSongRequested:root.showSong()}
    VinylPlayerBar {
        id:bar;x:root.footerLeft;y:root.height-height-root.playerBottomMargin;width:root.width-x-theme.margin
        readonly property int transitionDuration: 140
        property real dockProgress: root.hudOpen ? 0 : 1
        property point dockTarget: Qt.point(0, 0)
        // Snapshot the current icon center once per transition instead of tying
        // the scroll inset or hit geometry to every animation frame.
        Connections {
            target: root
            function onHudOpenChanged() {
                const button = root.shellHudButton;
                bar.dockTarget = button && button.visible
                    ? root.mapFromItem(button, button.width / 2, button.height / 2)
                    : Qt.point(Math.max(0, root.footerLeft - 12), root.height - root.playerBottomMargin);
            }
        }
        Behavior on dockProgress {
            enabled: bar.slim && root.hudHasMedia && root.lifecycleActive
            NumberAnimation { duration: bar.transitionDuration; easing.type: Easing.OutCubic }
        }
        transform: Scale {
            origin.x: bar.dockTarget.x - bar.x
            origin.y: bar.dockTarget.y - bar.y
            xScale: bar.slim ? 1 - bar.dockProgress * (1 - Math.min(1, 46 / Math.max(1, bar.width))) : 1
            yScale: xScale
        }
        visible: root.canEnterVinyl && (!slim || root.hudHasMedia) && opacity > 0.01
        opacity: slim ? 1 - dockProgress : root.videoFullscreen && root.videoDisplayed && !root.videoHudVisible ? 0 : 1
        enabled: visible && (!slim || root.hudOpen)
        Behavior on opacity {
            enabled: !bar.slim
            NumberAnimation { duration: bar.transitionDuration }
        }
        pageData:session;backdrop:root.chromeBackdrop;page:root.page==="song" ? "song" : "album";queueEnabled:true;trackNavigationEnabled:true
        videoFullscreen: root.videoFullscreen
        onMinimiseRequested: viaPointer => {
            root.hudOpen = false;
            const button = root.shellHudButton;
            if (!viaPointer && button && button.visible && button.enabled) button.forceActiveFocus(Qt.TabFocusReason);
            else root.parkFocus();
        }
        onVideoRequested: {root.requestHudVideo();}
        onVideoFullscreenRequested: root.toggleVideoFullscreen()
        HoverHandler {
            id: videoHudHover
            onHoveredChanged: if(hovered && root.videoDisplayed)root.revealVideoHud()
        }
        songPageData:root.page==="song" ? song.pageData : session
        showSongPlay:root.page==="song" && root.browsingSong && !browseSong.isCurrent
        onSelectedSongPlayRequested:if(browseSong.playingTrack.available!==false)browseSong.play()
        onSongRequested:root.showSong()
    }
    Component.onCompleted:{root.componentReady=true;session.refreshLibrary();if(root.canEnterVinyl)root.focusLibrary();else root.resetUnavailableSession();}
}
