import QtQuick
import QtQuick.Controls
import ".."
import "../ScrollGlideRegistry.js" as ScrollGlides

WorldPage {
    id: root
    objectName: "vinylWorld"
    medium: "Vinyl"
    boardTopPadding: 0
    headerTopInset: shortDesktop ? 8 : 24
    readonly property bool shortDesktop: width>=760 && height<=760
    required property var worldData
    property bool navigationEnabled: !!worldData && worldData.canEnterVinyl !== false
    property bool contentVisible: true
    property bool showPageBacking: true
    property Image wallpaperImage: null
    property bool hostedHomeLoading: false
    // The finishing beat (needle drop + fade) still counts as loading for the host's loader.
    readonly property bool homeLoadingIndeterminate: homeLoading.visible && (homeLoading.indeterminate || homeLoading.finishing)
    readonly property bool homeLoadingFinishing: homeLoading.finishing
    property bool runtimeReady: false
    property bool hostReady: false
    property bool componentReady: false
    readonly property bool ready: componentReady && runtimeReady && hostReady
    readonly property Item board: root
    readonly property string activeTab: currentTab === "discover" ? "Discover"
        : currentTab === "explore" ? "Explore" : currentTab === "library" ? "Library" : "Home"
    property string currentTab: "home"
    property bool discoverVisited: false
    property bool exploreVisited: false
    property bool explorePrimed: false
    readonly property bool exploreWarmEligible: ready && lifecycleActive && visible && contentVisible && navigationEnabled
        && currentTab === "home" && !listKey && !worldData.tastePickerOpen && !homeLoading.visible
        && !!worldData.session && !!worldData.session.source && worldData.session.profileActive !== false
        && !worldData.session.shuttingDown && pageFlickable.height > 0
        && worldData.homeStatus !== "loading" && worldData.homeStatus !== "idle"
        && !(Number((worldData.homeSourcePool || {}).pendingCount) > 0)
    Timer {
        interval: 600
        running: root.exploreWarmEligible && !root.exploreVisited && !root.explorePrimed
        onTriggered: root.explorePrimed = true
    }
    property string listKey: ""
    property bool listHome: false
    property real listReturnY: 0
    property Item listReturnFocus: null
    property var tabOffsets: ({})
    property var tabRoutes: ({})
    // Tab content starts right under the in-page bar (Crate and Spin are Home content).
    readonly property real contentTop: surface.y + flowBar.y + flowBar.height
    readonly property real dockedInset: parkDockSpace
    readonly property Item flowTabs: flowBar
    readonly property var tabModel: [
        {key:"home", label:qsTr("Home"), automationKey:"Home"},
        {key:"discover", label:qsTr("Discover"), automationKey:"Discover"},
        {key:"explore", label:qsTr("Explore"), automationKey:"explore"},
        {key:"library", label:qsTr("Library"), automationKey:"Library"}
    ]
    tabBarSource: contentVisible && navigationEnabled ? flowBar : null
    dockTabPrefix: tabsDocked ? "vinylTab" : "vinylHiddenDockTab"
    dockContentLeft: discover.visible ? discover.contentLeft : explore.visible && explore.item ? explore.item.contentLeft : 0

    function validTab(tab) { return tabModel.some(entry => entry.key === tab); }
    function finishPageLayout() {
        // Tab/list changes dirty nested positioners. Finish them inside-out so
        // scroll bounds and pointer hit targets both belong to the new route.
        if (listKey) list.forceLayout();
        else if (currentTab === "library" && library.visible) library.forceLayout();
        else if (currentTab === "home") homeContent.forceLayout();
        else if (currentTab === "explore" && explore.item) explore.item.forceLayout();
        surface.forceLayout();
        surface.parent.forceLayout(); // WorldPage's board
    }
    function parkPage(y) {
        finishPageLayout();
        const target = y === undefined ? contentTop - dockedInset : y;
        const maximum = Math.max(0, pageFlickable.contentHeight - pageFlickable.height);
        pageFlickable.contentY = Math.min(maximum, Math.max(0, Number(target) || 0));
    }
    function restorePageOffset(y) {
        // Back replays the captured viewport, rather than parking at a new target.
        finishPageLayout();
        pageFlickable.contentY = y;
    }
    // Provider replies can insert, remove or resize sections above a reader.
    // Preserve the first surviving section at the viewport edge, not the old absolute Y.
    property var homeLayout: []
    // The closed rail fits mostly in the existing gutter. Keep the usual row
    // width when there is room, and let the existing row scrollers take over when expanded.
    readonly property real homeLeft: homeIndex.visible
        ? Math.max(0, homeIndexHost.x + homeIndex.width + 18 - outerPageMargin) : 0
    readonly property real homeWidth: homeIndex.visible
        ? Math.max(0, Math.min(surface.width, width - outerPageMargin - homeLeft - 12)) : surface.width
    readonly property bool trackHomeLayout: componentReady && lifecycleActive && visible && contentVisible && navigationEnabled
        && currentTab === "home" && !listKey && !worldData.tastePickerOpen && !homeLoading.visible
    onTrackHomeLayoutChanged: {
        homeLayout = [];
        if (trackHomeLayout) updateHomeLayout();
    }
    function updateHomeLayout() {
        if (!trackHomeLayout) return;
        var previous = homeLayout;
        var sections = [hero, flowBar, crate, spin, homeTitle];
        for (var i = 0; i < homeShelves.count; ++i)
            sections.push(homeShelves.itemAt(i));
        homeLayout = sections.filter(item => item && item.visible && item.height > 0).map(function(item) {
            var y = item.mapToItem(pageFlickable.contentItem, 0, 0).y;
            return {item: item, y: y, bottom: y + item.height};
        });
        var top = pageFlickable.contentY;
        if (top <= 0) return;
        for (var old of previous) {
            if (old.bottom <= top) continue;
            var section = homeLayout.find(entry => entry.item === old.item);
            if (!section) continue;
            var delta = section.y - old.y;
            if (delta !== 0) {
                var glide = ScrollGlides.glideFor(pageFlickable);
                // Update the WorldPage board's extent as part of the same rebase.
                if (glide) glide.shiftForLayout(delta, function() { surface.parent.forceLayout(); });
            }
            break;
        }
    }
    function resetUnavailableNavigation() {
        if (navigationEnabled) return false;
        currentTab = "home";
        listKey = ""; listHome = false; listReturnY = 0; listReturnFocus = null;
        tabOffsets = ({}); tabRoutes = ({});
        parkPage(0);
        return true;
    }
    function captureRouteState() {
        return {currentTab:currentTab, listKey:listKey, listHome:listHome,
            listReturnY:listReturnY, listReturnFocus:listReturnFocus,
            tabOffsets:Object.assign({}, tabOffsets), scrollY:pageFlickable.contentY,
            libraryState:worldData.personalLibrary && typeof worldData.personalLibrary.captureViewState==="function"
                ? worldData.personalLibrary.captureViewState() : null,
            listIndex:list.currentIndex, listSelectedId:list.selectedId};
    }
    function restoreRouteState(state) {
        if (!state || resetUnavailableNavigation()) return;
        if(state.libraryState && worldData.personalLibrary && typeof worldData.personalLibrary.restoreViewState==="function")
            worldData.personalLibrary.restoreViewState(state.libraryState);
        currentTab = validTab(state.currentTab) ? state.currentTab : "home";
        listKey = state.listKey || ""; listHome = !!state.listHome;
        listReturnY = Number(state.listReturnY) || 0;
        listReturnFocus = state.listReturnFocus || null;
        tabOffsets = Object.assign({}, state.tabOffsets || tabOffsets);
        list.currentIndex = Number(state.listIndex) || 0;
        list.selectedId = state.listSelectedId || "";
        parkPage(state.scrollY !== undefined ? state.scrollY : tabOffsets[currentTab]);
    }
    function resetRouteState(tab) {
        tabOffsets = ({}); tabRoutes = ({});
        listKey = ""; listHome = false; listReturnY = 0; listReturnFocus = null;
        list.currentIndex = 0; list.selectedId = "";
        currentTab = validTab(tab) ? tab : "home";
        if (!resetUnavailableNavigation()) parkPage(0);
    }
    function rememberCurrentTab() {
        var offsets = Object.assign({}, tabOffsets);
        offsets[currentTab] = pageFlickable.contentY; tabOffsets = offsets;
        var routes = Object.assign({}, tabRoutes);
        routes[currentTab] = captureRouteState(); tabRoutes = routes;
    }
    function setTab(tab) {
        if (!validTab(tab) || resetUnavailableNavigation() || tab === currentTab) return;
        rememberCurrentTab();
        currentTab = tab;
        if (tabRoutes[tab]) restoreRouteState(tabRoutes[tab]);
        else {
            listKey = ""; listHome = false; list.currentIndex = 0; list.selectedId = "";
            parkPage();
        }
    }
    function focusCurrent() {
        if (listKey) list.focusCurrent();
        else if (currentTab === "home" && worldData.tastePickerOpen) focusArtistSearch();
        else (tabsDocked ? dockedTabBar : flowBar).forceActiveFocus(Qt.TabFocusReason);
    }
    function focusArtistSearch() { return worldData.tastePickerOpen ? picker.focusArtistSearch() : false; }
    function showList(key, home) {
        if (resetUnavailableNavigation()) return;
        listReturnY = pageFlickable.contentY;
        listReturnFocus = root.Window.window ? root.Window.window.activeFocusItem : null;
        listHome = home; listKey = key;
        parkPage();
        // A pointer-opened list must not leave keyboard focus on its hidden shelf.
        root.forceActiveFocus(Qt.OtherFocusReason);
    }
    function backFromList() {
        if (!listKey) return false;
        listKey = "";
        parkPage(listReturnY);
        var target = listReturnFocus;
        Qt.callLater(function() {
            if (target && target.visible && target.enabled) target.forceActiveFocus(Qt.TabFocusReason);
            else root.forceActiveFocus(Qt.OtherFocusReason);
        });
        return true;
    }
    function backFromLibrary() {
        if(!playlistDetail.active || !playlistDetail.item)return false;
        playlistDetail.item.showPlaylistList();return true;
    }
    onNavigationEnabledChanged: if (componentReady) resetUnavailableNavigation()
    onCurrentTabChanged: {
        if(currentTab==="discover")discoverVisited=true;
        if(currentTab==="explore")exploreVisited=true;
        if (!componentReady) return;
        if (!validTab(currentTab)) { currentTab = "home"; return; }
        if (!navigationEnabled && currentTab !== "home") { resetUnavailableNavigation(); return; }
        worldData.selectTab(currentTab);
    }
    Component.onCompleted: {
        backdrop = vinylBacking;
        componentReady = true;
        if (!resetUnavailableNavigation()) worldData.selectTab(currentTab);
    }
    Connections {
        target: root.worldData
        ignoreUnknownSignals: true
        function onListRequested(key, home) { root.showList(key, home); }
        function onCanEnterVinylChanged() { root.resetUnavailableNavigation(); }
        function onTastePickerOpenChanged() {
            if (root.currentTab !== "home" || !root.worldData.tastePickerOpen) return;
            Qt.callLater(function() {
                if (!root.visible || !root.contentVisible || !picker.visible) return;
                root.parkPage(root.pageFlickable.contentY);
                const pickerTop = picker.mapToItem(root.pageFlickable.contentItem, 0, 0).y;
                root.parkPage(pickerTop - root.dockedInset);
                root.focusArtistSearch();
            });
        }
        function onArtistSearchFocusRequested() { Qt.callLater(root.focusArtistSearch); }
    }
    Theme { id: theme }
    Item {
        id: homeIndexHost
        parent: root.pageFlickable.contentItem
        x: 8
        y: surface.parent.y + surface.y + crate.y
        z: 2
        height: body.y + body.height - crate.y
        visible: root.trackHomeLayout && root.worldData.pickerAnswered === true
        RowIndexSidebar {
            id: homeIndex
            pageFlick: root.pageFlickable
            flowHost: homeIndexHost
            visible: homeIndexHost.visible && visibleRows.length > 0
            pinLine: root.parkDockSpace
            currentOffset: root.parkDockSpace + root.parkGap
            backdrop: root.backdrop
            worldName: "Vinyl"
            automationPrefix: "vinylHomeRowIndex"
            rows: {
                const entries = [];
                if (crate.count > 0)
                    entries.push({key: "crate", title: qsTr("The Crate"), target: crate,
                        iconPaths: '<path d="M3 6h18v14H3zM3 10h18m-18 6h18M9 6V4h6v2"/>'});
                if (spin.items.length > 0)
                    entries.push({key: "spin", title: qsTr("The Spin"), target: spin,
                        iconPaths: '<path d="M9 18V5l11-2v12M9 9l11-2"/><ellipse cx="6" cy="18" rx="3" ry="2"/><ellipse cx="17" cy="15" rx="3" ry="2"/>'});
                for (let i = 0; i < homeShelves.count; ++i) {
                    const shelf = homeShelves.itemAt(i);
                    if (shelf && shelf.itemCount > 0)
                        entries.push({key: shelf.homeKey, title: shelf.row.title, target: shelf,
                            iconPaths: shelf.modelData.iconPaths});
                }
                return entries;
            }
            onRowRequested: target => root.parkRow(target)
        }
    }
    Rectangle {
        id: vinylBacking
        objectName: "vinylPageBacking"
        parent: root
        anchors.fill: parent
        z: -1
        visible: root.showPageBacking
        color: theme.vinylBackground
    }
    Column {
        id: surface
        visible: root.contentVisible
        width: parent.width
        readonly property bool compactIntro: root.shortDesktop && !hero.narrow
        VinylWallpaperFrost {
            id: homeFrost
            wallpaper: root.wallpaperImage
        }
        // Use explicit gaps so only the spaces around the Home tab bar get tighter.
        onPositioningComplete: root.updateHomeLayout()
        VinylHomeLoadingIndicator {
            id: homeLoading
            objectName: "vinylHomeLoading"
            readonly property bool hasContent: (root.worldData.heroSlides || []).length > 0
                || (root.worldData.crateItems || []).length > 0
                || (root.worldData.v1HomeRows || []).some(row=>(row.items || []).length)
            property bool contentShown: false
            // Optional enrichment (charts, identities, discographies) has no native
            // deadline, so once Home has content wait for it only this long.
            property int optionalWaitMs: 8000
            property bool optionalWaitExpired: false
            readonly property bool waitingOnOptional: hasContent && !contentShown && !optionalWaitExpired
                && Number((root.worldData.homeSourcePool || {}).pendingCount)>0
                && root.worldData.homeStatus!=="loading" && root.worldData.homeStatus!=="idle"
            Timer {
                interval: homeLoading.optionalWaitMs
                running: homeLoading.waitingOnOptional && homeLoading.visible
                onTriggered: homeLoading.optionalWaitExpired = true
            }
            readonly property bool canPresentContent: hasContent && !indeterminate
                && root.contentVisible && root.navigationEnabled && !root.worldData.tastePickerOpen
                && root.currentTab === "home" && !root.listKey
            // Latch only after the first-load wait has let content through, not as rows arrive.
            function rememberContent() {
                if (!canPresentContent || contentShown) return;
                // First reveal after a visible load gets the needle-drop beat before Home appears.
                if (loadedVisibly) { finishing = true; finishTimer.restart(); }
                loadedVisibly = false;
                contentShown = true;
            }
            property bool finishing: false
            // Set while the loader is on screen loading; read when content arrives (visibility drops first).
            property bool loadedVisibly: false
            onVisibleChanged: if (visible && indeterminate) loadedVisibly = true
            onIndeterminateChanged: if (visible && indeterminate) loadedVisibly = true
            dropping: finishing
            Timer { id: finishTimer; interval: 780; onTriggered: homeLoading.finishing = false }
            onCanPresentContentChanged: rememberContent()
            onHasContentChanged: if (!hasContent) { contentShown = false; optionalWaitExpired = false; finishing = false; }
            Component.onCompleted: rememberContent()
            Connections {
                target: root
                function onWorldDataChanged() {
                    homeLoading.contentShown = false;
                    homeLoading.optionalWaitExpired = false;
                    homeLoading.rememberContent();
                }
            }
            topPadding: Math.max(0,root.pageFlickable.height*0.4)
            // Cold Home is usable while charts arrive. Keep the existing first-load
            // presentation for returning listeners with saved choices.
            visible: root.navigationEnabled && !root.worldData.tastePickerOpen && root.worldData.pickerAnswered === true
                && root.worldData.savedArtistCount !== 0 && root.currentTab === "home" && !root.listKey
                && (finishing || (!contentShown && (indeterminate || !hasContent)))
            indeterminate: root.worldData.homeStatus === "loading" || root.worldData.homeStatus === "idle"
                || (Number((root.worldData.homeSourcePool || {}).pendingCount) > 0 && !optionalWaitExpired)
            showIndicator: !root.hostedHomeLoading || (!indeterminate && !finishing)
            error: root.worldData.homeError || ""
            VinylWorldButton { x:(parent.width-width)/2; visible:!homeLoading.indeterminate && !homeLoading.finishing; label:qsTr("Retry"); selected:true; backdrop:root.backdrop; onTriggered:root.worldData.retryHomeRecommendations() }
        }
        VinylHero {
            id: hero
            objectName: "vinylHero"
            width: parent.width
            height: surface.compactIntro ? 260 : implicitHeight
            visible: slideCount>0 && root.navigationEnabled && !homeLoading.visible
            slides: root.worldData.heroSlides || []
            session: root.worldData.session || null
            onPlayRequested: item => root.worldData.playHomeItem(item)
            onOpenRequested: item => root.worldData.openHomeItem(item)
            onSaveRequested: item => root.worldData.saveHomeItem(item)
            onRadioRequested: item => root.worldData.radioHomeItem(item)
            onHideRequested: item => root.worldData.hideHomeItem(item)
        }
        Item { width: 1; height: surface.compactIntro ? 4 : 30; visible: hero.visible && flowBar.visible }
        WorldTabBar {
            id: flowBar
            objectName: "vinylFlowTabs"
            visible: root.navigationEnabled && !homeLoading.visible
            width: parent.width
            height:54
            backdrop: root.backdrop
            track: root.pageFlickable.contentY
            tabModel: root.tabModel
            tabPrefix: root.tabsDocked ? "vinylHiddenFlowTab" : "vinylTab"
            currentTab: root.currentTab
            onTabRequested: tab => root.setTab(tab)
        }
        // Short windows: frosted Crate frames add 12 px; VinylCrate/VinylItemCard trim 4+4+4 inside the row.
        Item { width: 1; height: surface.compactIntro ? 12 : 30; visible: crate.visible }
        VinylCrate {
            id: crate
            objectName: "vinylCrate"
            visible: root.navigationEnabled && !homeLoading.visible && root.currentTab === "home" && !root.listKey
            showTasteSetup: !root.worldData.tastePickerOpen && root.worldData.savedArtistCount === 0
            onTasteSetupRequested: root.worldData.beginTasteEdit()
            x: root.homeLeft
            width: root.homeWidth; height: implicitHeight
            tileSizingWidth: surface.width
            compact: true
            items: root.worldData.crateItems || []
            frost: homeFrost
            worldData: root.worldData; backdrop: root.backdrop; pageFlick: root.pageFlickable
            onActivateRequested: item => root.worldData.playHomeItem(item)
            onOpenRequested: item => root.worldData.openHomeItem(item)
            onRadioRequested: item => root.worldData.radioHomeItem(item)
            onPinRequested: (item,pinned) => root.worldData.pinCrateItem(item,pinned)
        }
        Item { width: 1; height: 30; visible: spin.visible }
        VinylSpin {
            id: spin
            objectName: "vinylSpin"
            visible: root.navigationEnabled && !homeLoading.visible && root.currentTab === "home" && !root.listKey
            x: root.homeLeft
            width: root.homeWidth; height: implicitHeight
            songs: root.worldData.spinSongs || []
            albums: root.worldData.spinAlbums || []
            artists: root.worldData.spinArtists || []
            frost: homeFrost
            worldData: root.worldData; backdrop: root.backdrop; pageFlick: root.pageFlickable
            onActivateRequested: item => root.worldData.playHomeItem(item)
            onOpenRequested: item => root.worldData.openHomeItem(item)
            onRadioRequested: item => root.worldData.radioHomeItem(item)
            onFeedbackRequested: (item,action) => root.worldData.feedbackHomeItem(item,action)
        }
        Item { id: tabBodyGap; width: 1; height: 30; visible: flowBar.visible && body.visible && body.height > 0 }
        Item {
            id: body
            visible: root.navigationEnabled && !homeLoading.visible
            x: root.homeLeft
            width: root.homeWidth
            height: root.listKey ? list.implicitHeight
                : root.currentTab === "home" ? homeContent.implicitHeight
                : root.currentTab === "discover" ? discover.height
                : root.currentTab === "explore" ? explore.height
                : playlistDetail.active && playlistDetail.item ? playlistDetail.item.implicitHeight : library.implicitHeight
            Column {
                id: homeContent
                objectName: "vinylHome"
                readonly property string rowKeys: (root.worldData.v1HomeRows || []).map(function(row){return row.homeKey || row.key;}).join(",")
                width: parent.width
                visible: !root.listKey && root.currentTab === "home"
                spacing: 30
                onPositioningComplete: if (root.trackHomeLayout) {
                    surface.forceLayout();
                    root.updateHomeLayout();
                }
                Text {
                    id: homeTitle
                    visible: !picker.visible && !homeLoading.visible && !(root.worldData.v1HomeRows || []).length
                    text: qsTr("Your picks will grow as you listen.")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 22
                }
                VinylTastePicker {
                    id: picker
                    width: parent.width
                    visible: root.worldData.tastePickerOpen === true
                    worldData: root.worldData
                    backdrop: root.backdrop
                    pageFlick: root.pageFlickable
                }
                Repeater {
                    id: homeShelves
                    // Stable slots preserve unaffected shelf/card objects as other sources finish.
                    model: [
                        {key: "mixes-radio", iconPaths: '<rect x="2" y="5" width="20" height="14" rx="2"/><circle cx="7" cy="11" r="2"/><circle cx="17" cy="11" r="2"/><path d="M7 9h10M7 19l2-4h6l2 4"/>'},
                        // The adjacent album rows use distinct record variants.
                        {key: "new-from-your-artists", iconPaths: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="3"/><path d="M6 12a6 6 0 0 1 6-6m6 6a6 6 0 0 1-6 6"/>'},
                        {key: "albums-for-you", iconPaths: '<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="12" cy="12" r="6"/><circle cx="12" cy="12" r="1"/>'},
                        {key: "because-you-played", iconPaths: '<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9z"/>'},
                        {key: "deep-cuts", iconPaths: '<path d="m12 2 8 10-8 10-8-10Z"/>'},
                        {key: "rediscover", iconPaths: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>'},
                        {key: "artists-like-yours", iconPaths: '<circle cx="12" cy="7" r="4"/><path d="M4 21v-2a5 5 0 0 1 5-5h6a5 5 0 0 1 5 5v2Z"/>'}
                    ]
                    delegate: VinylShelf {
                        required property var modelData
                        readonly property string homeKey: modelData.key
                        readonly property var emptyRow: ({key:"v1:"+homeKey,homeKey:homeKey,title:"",items:[]})
                        objectName: "vinylHomeRow_"+homeKey
                        readonly property int itemCount: (row.items || []).length
                        visible: itemCount>0 && !picker.visible
                        width: homeContent.width
                        row: (root.worldData.v1HomeRows || []).find(function(candidate) {
                            return (candidate.homeKey || candidate.key) === homeKey;
                        }) || emptyRow
                        worldData: root.worldData
                        homePageVisible: root.contentVisible && homeContent.visible
                            && root.currentTab==="home" && !root.listKey && !picker.visible
                        backdrop: root.backdrop; pageFlick: root.pageFlickable
                        // Seven across at 1280 (Hemanth, 2026-11-28: "everything under mixes is too big").
                        cardSize: row.kind === "artists" ? 124
                            : Math.max(0, Math.floor((surface.width - 6 * 22) / 7))
                        squareSpacing: 22
                        home: true
                    }
                }
            }
            Loader {
                id: discover
                width: parent.width
                active: root.discoverVisited || root.currentTab==="discover"
                height: item ? item.implicitHeight : 0
                readonly property real contentLeft: item ? item.contentLeft : 0
                visible: !root.listKey && root.currentTab === "discover"
                sourceComponent: VinylDiscoverPage {
                    width: discover.width
                    active: root.lifecycleActive && root.ready && discover.visible
                    worldData: root.worldData
                    backdrop: root.backdrop
                    pageFlick: root.pageFlickable
                }
            }
            Loader {
                id: explore
                width: parent.width
                active: root.explorePrimed || root.exploreVisited || root.currentTab === "explore"
                height: item ? item.implicitHeight : 0
                visible: !root.listKey && root.currentTab === "explore"
                sourceComponent: VinylExplorePage {
                    width: explore.width
                    active: root.lifecycleActive && root.ready && root.visible && root.contentVisible && explore.visible
                    warmUp: root.exploreWarmEligible
                    // A first visit parks the bottom of the tab bar at dockedInset.
                    warmTopInset: root.boardTopPadding + root.dockedInset + tabBodyGap.height
                    worldData: root.worldData
                    backdrop: root.backdrop
                    pageFlick: root.pageFlickable
                    onRowRequested: target => root.parkRow(target)
                    onListRequested: row => {
                        root.worldData.homeListSnapshot = row;
                        root.showList(row.key, true);
                    }
                }
            }
            VinylLibraryTab {
                id: library
                width: parent.width
                visible: !root.listKey && root.currentTab === "library" && !playlistDetail.active
                rows: root.worldData.libraryRows || []
                worldData: root.worldData; backdrop: root.backdrop; pageFlick: root.pageFlickable
                onItemActivated: item => root.worldData.openHomeItem(item)
                onListRequested: row => {root.worldData.homeListSnapshot=row;root.showList(row.key,true);}
            }
            Loader {
                id: playlistDetail
                width: parent.width
                active: !root.listKey && root.currentTab==="library" && !!root.worldData.personalLibrary
                    && !!root.worldData.personalLibrary.selectedPlaylistId
                visible: active
                sourceComponent: VinylPersonalLibraryPage {
                    width: playlistDetail.width
                    detailOnly: true
                    libraryData: root.worldData.personalLibrary
                    backdrop: root.backdrop;pageFlick:root.pageFlickable
                }
            }
            VinylListPage {
                id: list
                width: parent.width
                visible: root.listKey.length > 0
                listKey: root.listKey
                home: root.listHome
                homeArtistActions: root.listHome && root.currentTab === "home"
                worldData: root.worldData
                backdrop: root.backdrop
                pageFlick: root.pageFlickable
                onBackRequested: root.backFromList()
            }
        }
    }
}
