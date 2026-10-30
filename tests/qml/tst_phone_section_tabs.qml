import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneSectionTabs"
    when: windowShown

    Window {
        id: testWindow
        width: 354
        height: 792
        visible: true
        Rectangle { id: wallpaper; anchors.fill: parent; color: "#15151b" }
    }

    Component {
        id: tabsComponent
        App.TheatreTabBar { width: 318; phoneLayout: true; compact: true; backdrop: wallpaper }
    }
    Component {
        id: worldComponent
        App.WorldPage {
            id: world
            width: 354; height: 792
            androidHost: true
            medium: "Theatre"
            backdrop: wallpaper
            contentBottomInset: 72
            tabBarSource: inlineTabs
            Rectangle { width: parent.width; height: 100; color: "transparent" }
            App.TheatreTabBar { id: inlineTabs; backdrop: wallpaper }
            Rectangle { width: parent.width; height: 900; color: "transparent" }
        }
    }
    SignalSpy { id: tabSpy; signalName: "tabRequested" }

    function child(root, name) {
        if (root.objectName === name) return root
        for (let item of root.children) {
            const found = child(item, name)
            if (found) return found
        }
        return null
    }

    function test_labels_and_selected_tab_are_reachable() {
        const bar = createTemporaryObject(tabsComponent, testWindow.contentItem)
        verify(bar !== null)
        wait(0)
        tabSpy.target = bar
        const keys = ["discover", "movies", "shows", "anime", "library"]
        for (let key of keys) {
            const pill = child(bar, "theatreTab_" + key)
            const label = child(bar, "theatreTabLabel_" + key)
            verify(pill !== null && label !== null)
            verify(label.implicitWidth > 0, key + " must use real font metrics")
            verify(pill.width > label.implicitWidth, key + " text must fit its pill")
            verify(pill.height >= 48, key + " must have a usable touch target")
        }
        const viewport = child(bar, "theatreTabViewport")
        verify(viewport.contentWidth > viewport.width)
        compare(viewport.clip, true)
        bar.currentTab = "library"
        const library = child(bar, "theatreTab_library")
        tryVerify(function() {
            const point = library.mapToItem(viewport, 0, 0)
            return point.x >= -0.1 && point.x + library.width <= viewport.width + 0.1
        })
        tabSpy.clear()
        mouseClick(library)
        compare(tabSpy.count, 1)
        compare(tabSpy.signalArguments[0][0], "library")
    }

    function test_phone_tabs_reserve_viewport_in_both_orientations() {
        const world = createTemporaryObject(worldComponent, testWindow.contentItem)
        verify(world !== null)
        const scroll = child(world, "theatreWorldScroll")
        const dock = child(world, "theatreTabDock")
        const inline = child(world, "theatreTabBar")
        tryCompare(dock, "visible", true)
        compare(inline.visible, false)
        verify(scroll.y >= dock.y + dock.height)
        compare(scroll.y + scroll.height, world.height - world.contentBottomInset)
        world.width = 792; world.height = 354; world.contentBottomInset = 56
        testWindow.width=792;testWindow.height=354
        wait(0)
        compare(world.phoneLayout, true)
        verify(scroll.y >= dock.y + dock.height)
        verify(scroll.height >= 100, "short phone must retain a useful content viewport")
        compare(scroll.y + scroll.height, world.height - world.contentBottomInset)
        world.width = 1280; world.height = 800; scroll.contentY = 0
        testWindow.width=1280;testWindow.height=800
        tryCompare(world, "phoneLayout", false)
        tryCompare(inline, "visible", true)
        compare(dock.visible, false)
    }
}
