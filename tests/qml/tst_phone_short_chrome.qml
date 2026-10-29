import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneShortChrome"
    when: windowShown
    Window {
        id: testWindow
        width: 792; height: 354
        visible: true
        property bool androidHost: true
        Rectangle { id: wallpaper; anchors.fill: parent; color: "#15151b" }
    }
    Component {
        id: worldComponent
        App.WorldPage {
            id: world
            width: testWindow.width; height: testWindow.height
            androidHost: testWindow.androidHost
            medium: "Theatre"
            backdrop: wallpaper
            contentBottomInset: testWindow.height < 500 ? 56 : 72
            tabBarSource: inlineTabs
            Item {
                id: inlineTabs
                width: parent.width; height: 56
                property var tabModel: [
                    {key: "discover", label: "Discover"}, {key: "movies", label: "Movies"},
                    {key: "shows", label: "TV"}, {key: "anime", label: "Anime"},
                    {key: "library", label: "Library"}
                ]
                property string currentTab: "discover"
                signal tabRequested(string tab)
            }
            Rectangle { objectName: "firstContentRow"; width: parent.width; height: width > 600 ? 150 : 300; color: "#10131a" }
            Rectangle { objectName: "secondContentRow"; width: parent.width; height: 900; color: "#10131a" }
        }
    }

    function child(root, name) {
        if (root.objectName === name) return root
        for (let item of root.children) {
            const found = child(item, name)
            if (found) return found
        }
        return null
    }
    SignalSpy { id: actionSpy }
    function cleanup() {
        testWindow.width = 792; testWindow.height = 354; testWindow.androidHost = true
    }
    function test_all_utility_actions_keep_48dp_and_route_data() {
        return [
            {tag:"theatre", medium:"Theatre", keys:["search","stremio","trackers","account","wallpaper"]},
            {tag:"biblio", medium:"Biblio", keys:["search","trackers","account","wallpaper"]},
            {tag:"tankoban", medium:"Tankoban", keys:["search","trackers","account","wallpaper"]}
        ]
    }
    function test_all_utility_actions_keep_48dp_and_route(data) {
        testWindow.requestActivate()
        const world = createTemporaryObject(worldComponent,testWindow.contentItem,{medium:data.medium})
        tryCompare(world,"anchorWidth",792)
        const dock = child(world,data.tag+"TabDock")
        const chrome = child(world,data.tag+"ShortChrome")
        verify(dock.x + dock.width + 12 <= chrome.x + 0.5)
        for (const key of data.keys) {
            const button = child(chrome,data.tag+"ShortChrome_"+key)
            verify(button !== null && button.width >= 48 && button.height >= 48)
            actionSpy.target=world; actionSpy.signalName=key+"Clicked"; actionSpy.clear()
            mouseClick(button,24,24)
            compare(actionSpy.count,1,"Missing forwarded action: "+key)
        }
    }
    function test_portrait_and_desktop_keep_normal_header() {
        testWindow.width=354; testWindow.height=792
        const world = createTemporaryObject(worldComponent,testWindow.contentItem)
        wait(40)
        verify(!world.shortHeaderActive)
        verify(world.pageFlickable.y > 120)
        testWindow.androidHost=false; testWindow.width=1280; testWindow.height=354
        wait(40)
        verify(!world.shortHeaderActive)
    }
    function test_rotation_preserves_visible_board_section() {
        testWindow.width=354; testWindow.height=792
        const world = createTemporaryObject(worldComponent,testWindow.contentItem)
        tryCompare(world,"anchorWidth",354)
        const row = child(world,"secondContentRow")
        const scroll=world.pageFlickable
        scroll.contentY=row.mapToItem(scroll.contentItem,0,0).y+200
        const original=scroll.contentY
        testWindow.width=792; testWindow.height=354
        tryCompare(world,"anchorWidth",792)
        tryCompare(world,"anchorHeight",354)
        compare(Math.round(scroll.contentY-row.mapToItem(scroll.contentItem,0,0).y),200)
        testWindow.width=354; testWindow.height=792
        tryCompare(world,"anchorWidth",354)
        tryCompare(world,"anchorHeight",792)
        compare(Math.round(scroll.contentY),Math.round(original))
    }

    function test_landscape_keeps_two_hundred_dp_for_content() {
        const world = createTemporaryObject(worldComponent, testWindow.contentItem)
        verify(world !== null)
        wait(30)
        const scroll = child(world, "theatreWorldScroll")
        const dock = child(world, "theatreTabDock")
        verify(scroll !== null && dock !== null)
        verify(scroll.height >= 200, "Browsing must keep at least200dp, actual=" + scroll.height)
        verify(scroll.y >= dock.y + dock.height)
        compare(scroll.y + scroll.height, world.height - world.contentBottomInset)
    }
    function test_camera_cutout_does_not_cover_header_or_content() {
        testWindow.width=354; testWindow.height=792
        const world=createTemporaryObject(worldComponent,testWindow.contentItem,
            {safeInsets:{left:0,right:0,top:40,bottom:0}})
        wait(30)
        const header=child(world,"theatreWorldHeader")
        verify(header.y>=40,"Portrait camera overlaps the header")
        testWindow.width=792; testWindow.height=354
        world.safeInsets={left:40,right:0,top:0,bottom:0}
        wait(40)
        const dock=child(world,"theatreTabDock")
        const row=child(world,"firstContentRow")
        verify(dock.x>=40 && row.mapToItem(world,0,0).x>=40)
        world.safeInsets={left:0,right:40,top:0,bottom:0}
        wait(10)
        const utility=child(world,"theatreShortChrome")
        verify(utility.x+utility.width<=world.width-40)
        verify(row.mapToItem(world,0,0).x+row.width<=world.width-40)
    }
}
