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
    readonly property bool gated: !!worldData && worldData.tasteGateLocked === true
    property bool contentVisible: true
    property bool showPageBacking: true
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
        : currentTab === "library" ? "Library" : "Home"
    property string currentTab: "home"
    property bool discoverVisited: false
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
        {key:"library", label:qsTr("Library"), automationKey:"Library"}
    ]
    tabBarSource: contentVisible && navigationEnabled ? flowBar : null
    dockTabPrefix: tabsDocked ? "vinylTab" : "vinylHiddenDockTab"
    dockContentLeft: discover.visible ? discover.contentLeft : 0

    function validTab(tab) { return ["home", "discover", "library"].indexOf(tab) >= 0; }
    function parkPage(y) {
        // Tab/list changes dirty nested positioners. Finish them inside-out so
        // scroll bounds and pointer hit targets both belong to the new route.
        if (listKey) list.forceLayout();
        else if (currentTab === "library" && library.visible) library.forceLayout();
        else if (currentTab === "home") homeContent.forceLayout();
        surface.forceLayout();
        surface.parent.forceLayout(); // WorldPage's board
        const target = y === undefined ? contentTop - dockedInset : y;
        const maximum = Math.max(0, pageFlickable.contentHeight - pageFlickable.height);
        pageFlickable.contentY = Math.min(maximum, Math.max(0, Number(target) || 0));
    }
    // Provider replies can insert, remove or resize sections above a reader.
    // Preserve the first surviving section at the viewport edge, not the old absolute Y.
    property var homeLayout: []
    readonly property bool trackHomeLayout: componentReady && lifecycleActive && visible && contentVisible && !gated
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
    function enforceHomeGate() {
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
        if (!state || enforceHomeGate()) return;
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
        if (!enforceHomeGate()) parkPage(0);
    }
    function rememberCurrentTab() {
        var offsets = Object.assign({}, tabOffsets);
        offsets[currentTab] = pageFlickable.contentY; tabOffsets = offsets;
        var routes = Object.assign({}, tabRoutes);
        routes[currentTab] = captureRouteState(); tabRoutes = routes;
    }
    function setTab(tab) {
        if (!validTab(tab) || enforceHomeGate() || tab === currentTab) return;
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
        if (enforceHomeGate()) return;
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
    onNavigationEnabledChanged: if (componentReady) enforceHomeGate()
    onCurrentTabChanged: {
        if(currentTab==="discover")discoverVisited=true;
        if (!componentReady) return;
        if (!validTab(currentTab)) { currentTab = "home"; return; }
        if (!navigationEnabled && currentTab !== "home") { enforceHomeGate(); return; }
        worldData.selectTab(currentTab);
    }
    Component.onCompleted: {
        backdrop = vinylBacking;
        componentReady = true;
        if (!enforceHomeGate()) worldData.selectTab(currentTab);
    }
    Connections {
        target: root.worldData
        ignoreUnknownSignals: true
        function onListRequested(key, home) { root.showList(key, home); }
        function onCanEnterVinylChanged() { root.enforceHomeGate(); }
        function onTastePickerOpenChanged() {
            if (root.currentTab !== "home" || !root.worldData.tastePickerOpen) return;
            Qt.callLater(function() {
                if (root.currentTab !== "home" || !root.worldData.tastePickerOpen) return;
                // Onboarding has no tabs. The hidden bar retains its old y, which
                // can park the page below the picker header and its Save button.
                root.parkPage(root.gated ? 0 : undefined);
                root.focusArtistSearch();
            });
        }
        function onArtistSearchFocusRequested() { Qt.callLater(root.focusArtistSearch); }
    }
    Theme { id: theme }
    Rectangle {
        id: vinylBacking
        objectName: "vinylPageBacking"
        parent: root
        anchors.fill: parent
        z: -1
        visible: root.showPageBacking
        color: "#06070b"
    }
    Column {
        id: surface
        visible: root.contentVisible
        width: parent.width
        readonly property bool compactIntro: root.shortDesktop && !hero.narrow
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
                && root.contentVisible && !root.gated && !root.worldData.tastePickerOpen
                && root.worldData.pickerAnswered === true && root.currentTab === "home" && !root.listKey
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
            visible: !root.gated && !root.worldData.tastePickerOpen && root.worldData.pickerAnswered === true
                && root.currentTab === "home" && !root.listKey
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
            visible: slideCount>0 && !root.gated && !homeLoading.visible
            slides: root.worldData.heroSlides || []
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
            visible: !root.gated && !homeLoading.visible
            width: parent.width
            height:54
            backdrop: root.backdrop
            track: root.pageFlickable.contentY
            tabModel: root.tabModel
            tabPrefix: root.tabsDocked ? "vinylHiddenFlowTab" : "vinylTab"
            currentTab: root.currentTab
            onTabRequested: tab => root.setTab(tab)
        }
        Item { width: 1; height: surface.compactIntro ? 12 : 30; visible: crate.visible }
        VinylCrate {
            id: crate
            objectName: "vinylCrate"
            visible: !root.gated && !homeLoading.visible && root.currentTab === "home" && !root.listKey
            width: parent.width; height: implicitHeight
            compact: true
            items: root.worldData.crateItems || []
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
            visible: !root.gated && !homeLoading.visible && root.currentTab === "home" && !root.listKey
            width: parent.width; height: implicitHeight
            songs: root.worldData.spinSongs || []
            albums: root.worldData.spinAlbums || []
            artists: root.worldData.spinArtists || []
            worldData: root.worldData; backdrop: root.backdrop; pageFlick: root.pageFlickable
            onActivateRequested: item => root.worldData.playHomeItem(item)
            onOpenRequested: item => root.worldData.openHomeItem(item)
            onRadioRequested: item => root.worldData.radioHomeItem(item)
            onFeedbackRequested: (item,action) => root.worldData.feedbackHomeItem(item,action)
        }
        Item { width: 1; height: 30; visible: flowBar.visible && body.visible && body.height > 0 }
        Item {
            id: body
            visible: !homeLoading.visible
            width: parent.width
            height: root.listKey ? list.implicitHeight
                : root.currentTab === "home" ? homeContent.implicitHeight
                : root.currentTab === "discover" ? discover.height
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
                    model: ["mixes-radio", "new-from-your-artists", "albums-for-you",
                        "because-you-played", "deep-cuts", "rediscover", "artists-like-yours"]
                    delegate: VinylShelf {
                        required property string modelData
                        readonly property string homeKey: modelData
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
                        cardSize: homeKey==="albums-for-you" ? 132 : 130; home: true
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
