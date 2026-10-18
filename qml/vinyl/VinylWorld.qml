import QtQuick
import ".."
Item {
    id: root
    objectName: "vinylWorld"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    property string currentTab: "home"
    property string listKey: ""
    property bool listHome: false
    property real listReturnY: 0
    property real dockedInset: 0
    property Item dockedTabBar: null
    property var tabOffsets: ({})
    property Item listReturnFocus: null
    function focusCurrent() {
        if (listKey) list.focusCurrent();
        else if (currentTab === "library") library.focusCurrent();
        else if(dockedTabBar && dockedTabBar.visible) dockedTabBar.forceActiveFocus();
        else tabs.forceActiveFocus();
    }
    readonly property real contentTop: body.y
    // Where the page sits while Home shows the picker: the tab bar at the
    // top of the page area, so the tab bar stays usable beside the picker.
    readonly property real pickerParkY: tabs.y
    // Local data arrives after construction. A newly available carousel changes
    // the picker anchor; keep the unanswered picker in the visible viewport.
    onPickerParkYChanged: Qt.callLater(root.syncPickerPlacement)
    implicitHeight: body.y + body.height + 50
    function parkPage(y) {
        // Set at once so a click in the same event-loop pass already sees the
        // parked page, then re-assert once heights have settled, because a
        // content-height change while a tab rebuilds can clamp contentY back.
        root.pageFlick.contentY=y
        Qt.callLater(function(){root.pageFlick.contentY=y})
    }
    function syncPickerPlacement() {
        if (currentTab==="home" && !root.worldData.pickerAnswered && !listKey) parkPage(root.pickerParkY)
    }
    function setTab(tab) {
        if (["home","discover","library"].indexOf(tab)<0 || tab===currentTab) return
        var offsets=Object.assign({},tabOffsets)
        offsets[currentTab]=listKey ? listReturnY : pageFlick.contentY
        tabOffsets=offsets
        listKey="";currentTab=tab
        // First entry to a tab parks at the tab bar; later entries return to
        // where the listener left that tab, list detours included.
        parkPage(tabOffsets[tab] !== undefined ? tabOffsets[tab] : Math.max(0,root.contentTop-root.dockedInset))
        syncPickerPlacement()
    }
    function showList(key,home) {
        listReturnY=pageFlick.contentY
        listReturnFocus=Window.window ? Window.window.activeFocusItem : null
        listHome=home;listKey=key
        Qt.callLater(function(){root.pageFlick.contentY=Math.max(0,root.contentTop-root.dockedInset);list.focusCurrent()})
    }
    function backFromList() {
        listKey=""
        Qt.callLater(function(){root.pageFlick.contentY=root.listReturnY;if(root.listReturnFocus)root.listReturnFocus.forceActiveFocus()})
    }
    Theme { id: theme }
    FeaturedCarousel {
        id: carousel
        objectName: "vinylCarousel"
        width: parent.width
        height: visible ? (root.width<1300 ? 290 : 330) : 0
        visible: slides.length > 0
        slides: root.worldData.carouselSlides.map(function(slide) {
            return Object.assign({},slide,{c1:theme.biblioWashTop,c2:theme.biblioWashBottom});
        })
        kicker: slides[index] ? slides[index].kicker : ""
        primaryLabel: slides[index] ? slides[index].cta : ""
        secondaryLabel: ""
        onPrimaryClicked: index => {
            var s=slides[index]
            if(s.key==="resume") root.worldData.resume()
            else {root.setTab("discover");root.worldData.openList(s.key,false)}
        }
    }
    WorldTabBar {
        id: tabs
        objectName: "vinylFlowTabs"
        visible: !root.dockedTabBar || !root.dockedTabBar.visible
        y: carousel.height+26
        width: parent.width
        backdrop: root.backdrop
        track: root.pageFlick.contentY
        tabPrefix: tabs.visible ? "vinylTab" : "vinylHiddenFlowTab"
        tabModel: [{key:"home",label:"Home"},{key:"discover",label:"Discover"},{key:"library",label:"Library"}]
        currentTab: root.currentTab
        onTabRequested: tab => root.setTab(tab)
    }
    Item {
        id: body
        y: tabs.y+tabs.height+36
        width: parent.width
        height: panels.y+panels.height
        VinylLens {
            id: lens
            width: parent.width
            visible: root.currentTab==="discover"
            worldData: root.worldData
            backdrop: root.backdrop
            pageFlick: root.pageFlick
        }
        Item {
            id: panels
            y: lens.visible ? lens.implicitHeight+30 : 0
            width: parent.width
            height: root.listKey ? list.implicitHeight : root.currentTab==="home" ? home.implicitHeight
                  : root.currentTab==="discover" ? discover.implicitHeight : library.implicitHeight
        VinylHomePage {
            id: home
            width: parent.width
            visible: !root.listKey && root.currentTab==="home"
            worldData: root.worldData
            backdrop: root.backdrop
            pageFlick: root.pageFlick
        }
        VinylDiscoverPage {
            id: discover
            width: parent.width
            visible: !root.listKey && root.currentTab==="discover"
            worldData: root.worldData
            backdrop: root.backdrop
            pageFlick: root.pageFlick
        }
        VinylListPage {
            id: library
            objectName:"vinylLibraryPage"
            width: parent.width
            visible:!root.listKey&&root.currentTab==="library"
            listKey: "library"
            showBack: false
            worldData: root.worldData
            backdrop: root.backdrop
            pageFlick: root.pageFlick
        }
        VinylListPage {
            id: list
            pageFlick: root.pageFlick
            width: parent.width
            visible: root.listKey.length>0
            listKey: root.listKey
            home: root.listHome
            worldData: root.worldData
            backdrop: root.backdrop
            onBackRequested: root.backFromList()
        }
        }
    }
    Component.onCompleted: syncPickerPlacement()
    Connections {
        target: root.worldData
        function onListRequested(key,home) {root.showList(key,home)}
        function onPickerAnsweredChanged() {root.syncPickerPlacement()}
        function onProfileChanged() {root.syncPickerPlacement()}
    }
}
