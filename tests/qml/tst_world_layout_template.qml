import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: tests
    name: "WorldLayoutTemplate"
    when: windowShown
    Window { id: host; visible: true; width:800; height:1280; color:"#080a10"; property bool androidHost:true }
    Component {
        id: worldComponent
        App.WorldPage {
            id: world
            anchors.fill: parent
            androidHost: host.androidHost
            medium: "Biblio"
            backdrop: host.contentItem
            tabBarSource: tabs
            contentBottomInset: 72
            Rectangle { width:parent.width; height:150; color:"transparent" }
            App.WorldTabBar { id:tabs; backdrop:host.contentItem; tabModel:[{key:"explore",label:"Explore"},{key:"library",label:"Library"}]; currentTab:"explore" }
            App.BiblioExplorePage {
                objectName:"fixtureExplore"
                width:parent.width
                height:flowPage ? implicitHeight : 700
                pageFlick:world.pageFlickable
                active:false
            }
        }
    }
    function find(item,name) {
        if (item.objectName===name) return item
        for(const child of item.children || []) { const found=find(child,name); if(found) return found }
        return null
    }
    function test_world_classes_data() {
        return [{tag:"narrow",w:354,h:792,compact:true},{tag:"phone",w:412,h:900,compact:true},
            {tag:"tablet-portrait",w:800,h:1280,compact:true},{tag:"tablet-landscape",w:1280,h:800,compact:false},
            {tag:"short-landscape",w:792,h:354,compact:true},{tag:"desktop",w:1280,h:700,compact:false,desktop:true}]
    }
    function test_world_classes(data) {
        host.width=data.w;host.height=data.h;host.androidHost=!data.desktop
        const world=createTemporaryObject(worldComponent,host.contentItem)
        verify(world!==null)
        wait(50)
        compare(world.phoneLayout,data.compact)
        const dock=find(world,"biblioTabDock")
        const explore=find(world,"fixtureExplore")
        const inner=find(explore,"biblioExploreFlick")
        compare(explore.flowPage,data.compact)
        compare(inner.interactive,!data.compact)
        if(data.compact) {
            verify(dock.visible)
            verify(world.pageFlickable.y>=dock.y+dock.height)
            compare(world.pageFlickable.y+world.pageFlickable.height,data.h-72)
            verify(world.pageFlickable.contentHeight>world.pageFlickable.height)
            world.pageFlickable.contentY=120
            compare(inner.contentY,0)
        }
    }
}
