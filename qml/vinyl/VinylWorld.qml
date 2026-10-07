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
    property var tabOffsets: ({home:0,discover:0,library:0})
    readonly property real contentTop: body.y
    implicitHeight: body.y + body.height + 50
    function setTab(tab) {
        if (["home","discover","library"].indexOf(tab)<0 || tab===currentTab) return
        var offsets=Object.assign({},tabOffsets);offsets[currentTab]=listKey ? listReturnY : pageFlick.contentY;tabOffsets=offsets
        listKey="";currentTab=tab
        Qt.callLater(function(){root.pageFlick.contentY=root.tabOffsets[tab]||0})
    }
    function showList(key,home) {
        listReturnY=pageFlick.contentY
        listHome=home;listKey=key
        Qt.callLater(function(){root.pageFlick.contentY=root.contentTop})
    }
    function backFromList() {
        listKey=""
        Qt.callLater(function(){root.pageFlick.contentY=root.listReturnY})
    }
    Theme { id: theme }
    FeaturedCarousel {
        id: carousel
        objectName: "vinylCarousel"
        width: parent.width
        height: root.width<1300 ? 290 : 330
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
        y: carousel.height+26
        width: parent.width
        backdrop: root.backdrop
        track: root.pageFlick.contentY
        tabPrefix: "vinylTab"
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
        }
        Item {
            id: panels
            y: lens.visible ? lens.implicitHeight+30 : 0
            width: parent.width
            height: root.listKey ? list.implicitHeight : root.currentTab==="home" ? home.implicitHeight
                  : root.currentTab==="discover" ? discover.implicitHeight : 120
        VinylHomePage {
            id: home
            width: parent.width
            visible: !root.listKey && root.currentTab==="home"
            worldData: root.worldData
            backdrop: root.backdrop
        }
        VinylDiscoverPage {
            id: discover
            width: parent.width
            visible: !root.listKey && root.currentTab==="discover"
            worldData: root.worldData
            backdrop: root.backdrop
            pageFlick: root.pageFlick
        }
        Item {objectName:"vinylLibraryPage";visible:!root.listKey&&root.currentTab==="library"}
        VinylListPage {
            id: list
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
    Connections {
        target: root.worldData
        function onListRequested(key,home) {root.showList(key,home)}
    }
}
