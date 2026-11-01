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
    property var tabRoutes: ({})
    property Item listReturnFocus: null
    readonly property var personalData: root.worldData.personalLibrary || null
    readonly property Item libraryDestination: personalLibraryHost.item || library
    function focusArtistSearch() {return home.focusArtistSearch();}
    function focusCurrent() {
        if (listKey) list.focusCurrent();
        else if (currentTab === "library") libraryDestination.focusCurrent();
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
    function enforceHomeGate() {
        if(!root.worldData || root.worldData.canEnterVinyl!==false) return false;
        var hadAwayRoute=currentTab!=="home" || listKey.length>0;
        currentTab="home";
        listKey="";listHome=false;listReturnY=0;listReturnFocus=null;
        if(hadAwayRoute) {
            tabOffsets={};tabRoutes={};
            parkPage(Math.max(0,root.contentTop-root.dockedInset));
            return true;
        }
        return false;
    }
    function captureRouteState() {
        var currentSlide=carousel.slides[carousel.index];
        return {
            currentTab:currentTab,
            listKey:listKey,
            listHome:listHome,
            listReturnY:listReturnY,
            listReturnFocus:listReturnFocus,
            tabOffsets:Object.assign({},tabOffsets),
            scrollY:pageFlick.contentY,
            listIndex:list.currentIndex,
            listSelectedId:list.selectedId,
            carouselIndex:carousel.index,
            carouselSlideId:currentSlide ? String(currentSlide.id || currentSlide.key || "") : "",
            libraryIndex:libraryDestination.currentIndex,
            librarySelectedId:libraryDestination.selectedId,
            libraryView:personalLibraryHost.item ? personalLibraryHost.item.captureViewState() : null
        };
    }
    function restoreRouteState(state) {
        if(!state) return;
        var gated=root.worldData && root.worldData.canEnterVinyl===false;
        currentTab=gated ? "home" : (state.currentTab || "library");
        listKey=gated ? "" : (state.listKey || "");
        listHome=!gated && !!state.listHome;
        listReturnY=gated ? 0 : (Number(state.listReturnY) || 0);
        listReturnFocus=gated ? null : (state.listReturnFocus || null);
        var offsets=Object.assign({},tabOffsets);
        offsets[currentTab]=state.scrollY!==undefined ? state.scrollY : (state.tabOffsets || {})[currentTab];
        tabOffsets=offsets;
        if(state.listIndex!==undefined) list.currentIndex=gated ? 0 : state.listIndex;
        if(state.listSelectedId!==undefined) list.selectedId=gated ? "" : state.listSelectedId;
        if(state.carouselIndex!==undefined) {
            var restoredIndex=Number(state.carouselIndex);
            var slideId=String(state.carouselSlideId || "");
            if(slideId) {
                for(var slideIndex=0;slideIndex<carousel.slides.length;slideIndex++) {
                    var slide=carousel.slides[slideIndex];
                    if(String(slide.id || slide.key || "")===slideId) {
                        restoredIndex=slideIndex;
                        break;
                    }
                }
            }
            if(!isFinite(restoredIndex)) restoredIndex=0;
            carousel.index=Math.max(0,Math.min(Math.max(0,carousel.slides.length-1),Math.floor(restoredIndex)));
        }
        if(state.libraryView && personalLibraryHost.item)personalLibraryHost.item.restoreViewState(state.libraryView);
        if(state.libraryIndex!==undefined) libraryDestination.currentIndex=gated ? 0 : state.libraryIndex;
        if(state.librarySelectedId!==undefined) libraryDestination.selectedId=gated ? "" : state.librarySelectedId;
        if(gated) root.enforceHomeGate();
    }
    function resetRouteState(tab) {
        tabOffsets={};tabRoutes={};listKey="";listHome=false;listReturnY=0;listReturnFocus=null;
        list.currentIndex=0;list.selectedId="";library.currentIndex=0;library.selectedId="";
        currentTab=["home","discover","library"].indexOf(tab)>=0 ? tab : "library";
        parkPage(Math.max(0,root.contentTop-root.dockedInset));
    }
    function syncPickerPlacement() {
        if(currentTab==="home" && root.worldData.tastePickerOpen===true && !listKey)parkPage(root.pickerParkY);
    }
    function rememberCurrentTab() {
        var offsets=Object.assign({},tabOffsets)
        offsets[currentTab]=pageFlick.contentY
        tabOffsets=offsets
        var routes=Object.assign({},tabRoutes)
        routes[currentTab]=captureRouteState();tabRoutes=routes
    }
    function setTab(tab) {
        if (["home","discover","library"].indexOf(tab)<0) return
        if(root.worldData && root.worldData.canEnterVinyl===false) {
            root.enforceHomeGate();
            return;
        }
        if(tab===currentTab) return
        rememberCurrentTab()
        currentTab=tab
        var destination=tabRoutes[tab]
        if(destination) restoreRouteState(destination)
        else {
            listKey="";listHome=false;listReturnY=0;listReturnFocus=null
            list.currentIndex=0;list.selectedId="";library.currentIndex=0;library.selectedId=""
        }
        // First entry to a tab parks at the tab bar; later entries return to
        // where the listener left that tab, list detours included.
        parkPage(tabOffsets[tab] !== undefined ? tabOffsets[tab] : Math.max(0,root.contentTop-root.dockedInset))
        syncPickerPlacement()
    }
    onCurrentTabChanged: {
        if(root.worldData && typeof root.worldData.selectTab==="function")
            root.worldData.selectTab(currentTab)
    }
    function showList(key,home) {
        if(root.worldData && root.worldData.canEnterVinyl===false) {
            root.enforceHomeGate();
            return false;
        }
        listReturnY=pageFlick.contentY
        listReturnFocus=Window.window ? Window.window.activeFocusItem : null
        listHome=home;listKey=key
        Qt.callLater(function(){root.pageFlick.contentY=Math.max(0,root.contentTop-root.dockedInset);root.forceActiveFocus(Qt.OtherFocusReason)})
    }
    function backFromList() {
        listKey=""
        Qt.callLater(function(){root.pageFlick.contentY=root.listReturnY;var target=root.listReturnFocus;if(target && target.visible && target.enabled && target.accessibleName!==undefined)target.forceActiveFocus(Qt.TabFocusReason);else root.forceActiveFocus(Qt.OtherFocusReason)})
    }
    function backFromLibrary() {
        if(personalData && personalData.selectedPlaylistId){personalData.openPlaylist("");parkPage(Math.max(0,contentTop-dockedInset));return true;}
        return false;
    }
    Theme { id: theme }
    FeaturedCarousel {
        id: carousel
        objectName: "vinylCarousel"
        width: parent.width
        height: visible ? (root.width<1300 ? 290 : 330) : 0
        visible: slides.length > 0
        slides: root.worldData.carouselSlides.map(function(slide) {
            return Object.assign({},slide,{c1:theme.biblioWashTop,c2:theme.biblioWashBottom,
                artKind:slide.artKind==="poster" ? "square" : (slide.artKind || "square"),
                primaryIcon:slide.key==="resume" || (slide.item && slide.item.kind==="resume") ? "play" : "chevronRight"});
        })
        kicker: slides[index] ? slides[index].kicker : ""
        primaryLabel: slides[index] ? slides[index].cta : ""
        secondaryLabel: ""
        onPrimaryClicked: index => {
            var s=slides[index]
            if(s.item) {root.worldData.activate(s.item);return;}
            if(s.key==="resume") root.worldData.resume()
            else if(s.key==="local-albums" || s.key==="library") {
                root.setTab("library");root.listKey="";root.listHome=false
                root.parkPage(Math.max(0,root.contentTop-root.dockedInset))
            }
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
            visible: root.currentTab==="discover" && root.worldData.discoveryLanding!==true
            worldData: root.worldData
            backdrop: root.backdrop
            pageFlick: root.pageFlick
        }
        Item {
            id: panels
            y: lens.visible ? lens.implicitHeight+30 : 0
            width: parent.width
            height: root.listKey ? list.implicitHeight : root.currentTab==="home" ? home.implicitHeight
                  : root.currentTab==="discover" ? discover.implicitHeight : root.libraryDestination.implicitHeight
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
            visible:!root.personalData&&!root.listKey&&root.currentTab==="library"
            listKey: "library"
            showBack: false
            worldData: root.worldData
            backdrop: root.backdrop
            pageFlick: root.pageFlick
        }
        Loader {
            id:personalLibraryHost
            width:parent.width;active:!!root.personalData
            visible:active && !root.listKey && root.currentTab==="library"
            sourceComponent: Component {
                VinylPersonalLibraryPage {width:personalLibraryHost.width;libraryData:root.personalData;backdrop:root.backdrop;pageFlick:root.pageFlick}
            }
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
    Component.onCompleted: {
        enforceHomeGate();
        syncPickerPlacement();
    }
    Connections {
        target: root.worldData
        ignoreUnknownSignals: true
        function onListRequested(key,home) {root.showList(key,home)}
        function onPickerAnsweredChanged() {root.syncPickerPlacement()}
        function onTastePickerOpenChanged() {root.syncPickerPlacement()}
        function onArtistSearchFocusRequested() {
            Qt.callLater(function() {
                if(root.currentTab==="home" && !root.listKey
                        && root.worldData.tastePickerOpen===true)
                    root.focusArtistSearch();
            });
        }
        function onProfileChanged() {root.syncPickerPlacement()}
        function onCanEnterVinylChanged() {root.enforceHomeGate()}
        function onHomeSelectionRefreshed() {carousel.index=0}
    }
}
