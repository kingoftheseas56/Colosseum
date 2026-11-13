import QtQuick
import ".."

WorldPage {
    id: root
    objectName: "vinylWorld"
    medium: "Vinyl"
    required property var worldData
    property bool navigationEnabled: !!worldData && worldData.canEnterVinyl !== false
    property bool contentVisible: true
    property bool runtimeReady: false
    property bool hostReady: false
    property bool componentReady: false
    readonly property bool ready: componentReady && runtimeReady && hostReady
    readonly property Item board: root
    readonly property string activeTab: currentTab === "discover" ? "Discover"
        : currentTab === "library" ? "Library" : "Home"
    property string currentTab: "home"
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
        var target = Math.max(0, Number(y) || 0);
        pageFlickable.contentY = target;
        Qt.callLater(function() { if (root && root.pageFlickable) root.pageFlickable.contentY = target; });
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
            listIndex:list.currentIndex, listSelectedId:list.selectedId};
    }
    function restoreRouteState(state) {
        if (!state || enforceHomeGate()) return;
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
        else { listKey = ""; listHome = false; list.currentIndex = 0; list.selectedId = ""; parkPage(0); }
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
    function backFromLibrary() { return false; }
    onNavigationEnabledChanged: if (componentReady) enforceHomeGate()
    onCurrentTabChanged: {
        if (!componentReady) return;
        if (!validTab(currentTab)) { currentTab = "home"; return; }
        if (!navigationEnabled && currentTab !== "home") { enforceHomeGate(); return; }
        worldData.selectTab(currentTab);
    }
    Component.onCompleted: {
        componentReady = true;
        if (!enforceHomeGate()) worldData.selectTab(currentTab);
    }
    Connections {
        target: root.worldData
        ignoreUnknownSignals: true
        function onListRequested(key, home) { root.showList(key, home); }
        function onCanEnterVinylChanged() { root.enforceHomeGate(); }
        function onTastePickerOpenChanged() {
            if (root.currentTab === "home" && root.worldData.tastePickerOpen) root.parkPage(0);
        }
        function onArtistSearchFocusRequested() { Qt.callLater(root.focusArtistSearch); }
    }
    Theme { id: theme }
    Column {
        id: surface
        visible: root.contentVisible
        width: parent.width
        spacing: root.boardSectionSpacing
        WorldTabBar {
            id: flowBar
            objectName: "vinylFlowTabs"
            width: parent.width
            backdrop: root.backdrop
            track: root.pageFlickable.contentY
            tabModel: root.tabModel
            tabPrefix: root.tabsDocked ? "vinylHiddenFlowTab" : "vinylTab"
            currentTab: root.currentTab
            onTabRequested: tab => root.setTab(tab)
        }
        Item {
            id: body
            width: parent.width
            height: root.listKey ? list.implicitHeight
                : root.currentTab === "home" ? home.implicitHeight
                : root.currentTab === "discover" ? discover.implicitHeight : library.implicitHeight
            Item {
                id: home
                objectName: "vinylHomePlaceholder"
                width: parent.width
                visible: !root.listKey && root.currentTab === "home"
                implicitHeight: picker.visible ? picker.implicitHeight : homeTitle.implicitHeight
                Text {
                    id: homeTitle
                    visible: !picker.visible
                    text: qsTr("Home")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 32
                }
                VinylTastePicker {
                    id: picker
                    width: parent.width
                    visible: root.worldData.tastePickerOpen === true
                    worldData: root.worldData
                    backdrop: root.backdrop
                    pageFlick: root.pageFlickable
                }
            }
            VinylDiscoverPage {
                id: discover
                width: parent.width
                visible: !root.listKey && root.currentTab === "discover"
                active: root.lifecycleActive && root.ready && visible
                worldData: root.worldData
                backdrop: root.backdrop
                pageFlick: root.pageFlickable
            }
            Text {
                id: library
                objectName: "vinylLibraryPlaceholder"
                width: parent.width
                visible: !root.listKey && root.currentTab === "library"
                text: qsTr("Library")
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 32
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
