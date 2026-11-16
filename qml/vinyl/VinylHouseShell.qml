import QtQuick
import QtQuick.Controls
import ".."

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
    readonly property real contentTop: surface.y + body.y
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
        function position() {
            var maximum = Math.max(0, pageFlickable.contentHeight-pageFlickable.height);
            pageFlickable.contentY = Math.min(maximum, Math.max(0, Number(y) || 0));
        }
        position();
        Qt.callLater(function() { if (root && root.pageFlickable) position(); });
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
            parkPage(Math.max(0,contentTop-dockedInset));
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
        parkPage(Math.max(0, contentTop - dockedInset));
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
            if (root.currentTab === "home" && root.worldData.tastePickerOpen)
                Qt.callLater(function(){root.parkPage(Math.max(0,root.contentTop-root.dockedInset));root.focusArtistSearch();});
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
        color: "#06070b"
    }
    Column {
        id: surface
        visible: root.contentVisible
        width: parent.width
        spacing: 30
        Column {
            id: homeLoading
            objectName: "vinylHomeLoading"
            width: Math.min(420,parent.width)
            x: (parent.width-width)/2
            topPadding: Math.max(0,root.pageFlickable.height*0.4)
            spacing: 8
            visible: !root.gated && !root.worldData.tastePickerOpen && root.worldData.pickerAnswered === true
                && root.currentTab === "home" && !root.listKey
                && (loadingBar.indeterminate || !(root.worldData.heroSlides || []).length && !(root.worldData.crateItems || []).length
                    && !(root.worldData.v1HomeRows || []).some(row=>(row.items || []).length))
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                objectName: "vinylHomeLoadingMessage"
                text: loadingBar.indeterminate ? qsTr("Loading your music…") : root.worldData.homeError || qsTr("Could not load your music.")
                color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 13
            }
            ProgressBar {
                id: loadingBar
                objectName: "vinylHomeLoadingBar"
                width: parent.width; height: 6
                indeterminate: root.worldData.homeStatus==="loading" || root.worldData.homeStatus==="idle" || Number((root.worldData.homeSourcePool || {}).pendingCount)>0
                visible: indeterminate
                palette.dark: theme.gold
                palette.midlight: "#1affffff"
                Accessible.name: qsTr("Loading your music")
            }
            VinylWorldButton { x:(parent.width-width)/2; visible:!loadingBar.indeterminate; label:qsTr("Retry"); selected:true; backdrop:root.backdrop; onTriggered:root.worldData.retryHomeRecommendations() }
        }
        VinylHero {
            id: hero
            width: parent.width
            height: narrow ? implicitHeight : 300
            visible: slideCount>0 && !root.gated && !homeLoading.visible
            slides: root.worldData.heroSlides || []
            onPlayRequested: item => root.worldData.playHomeItem(item)
            onOpenRequested: item => root.worldData.openHomeItem(item)
            onSaveRequested: item => root.worldData.saveHomeItem(item)
            onRadioRequested: item => root.worldData.radioHomeItem(item)
            onHideRequested: item => root.worldData.hideHomeItem(item)
        }
        VinylCrate {
            id: crate
            visible: !root.gated && !homeLoading.visible
            width: parent.width; height: implicitHeight
            compact: true
            items: root.worldData.crateItems || []
            worldData: root.worldData; backdrop: root.backdrop; pageFlick: root.pageFlickable
            onActivateRequested: item => root.worldData.playHomeItem(item)
            onOpenRequested: item => root.worldData.openHomeItem(item)
            onPinRequested: (item,pinned) => root.worldData.pinCrateItem(item,pinned)
        }
        VinylSpin {
            id: spin
            visible: !root.gated && !homeLoading.visible
            width: parent.width; height: implicitHeight
            songs: root.worldData.spinSongs || []
            albums: root.worldData.spinAlbums || []
            artists: root.worldData.spinArtists || []
            worldData: root.worldData; backdrop: root.backdrop; pageFlick: root.pageFlickable
            onActivateRequested: item => root.worldData.playHomeItem(item)
            onOpenRequested: item => root.worldData.openHomeItem(item)
            onFeedbackRequested: (item,action) => root.worldData.feedbackHomeItem(item,action)
        }
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
                    // Stable slots preserve unaffected shelf/card objects as other sources finish.
                    model: ["mixes-radio", "new-from-your-artists", "albums-for-you",
                        "because-you-played", "deep-cuts", "rediscover", "artists-like-yours"]
                    delegate: VinylShelf {
                        required property string modelData
                        readonly property string homeKey: modelData
                        readonly property string rowSnapshot: {
                            var entry=(root.worldData.v1HomeRows || []).find(function(candidate) {
                                return (candidate.homeKey || candidate.key) === homeKey;
                            });
                            return JSON.stringify(entry || {key:"v1:"+homeKey,homeKey:homeKey,title:"",items:[]});
                        }
                        objectName: "vinylHomeRow_"+homeKey
                        readonly property int itemCount: (row.items || []).length
                        visible: itemCount>0 && !picker.visible
                        width: homeContent.width
                        row: JSON.parse(rowSnapshot); worldData: root.worldData
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
                worldData: root.worldData
                backdrop: root.backdrop
                pageFlick: root.pageFlickable
                onBackRequested: root.backFromList()
            }
        }
    }
}
