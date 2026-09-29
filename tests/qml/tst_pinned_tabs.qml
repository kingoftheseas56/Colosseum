import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "PinnedTabs"
    when: windowShown
    property string requestedTab: ""
    property var world: null

    Window { id: testWindow; width: 1000; height: 650; visible: true }
    Component {
        id: worldComponent
        Colosseum.WorldPage {
            width: 1000; height: 650; medium: "Theatre"
            property alias sourceForTest: sourceTabs
            property string activeTabForTest: "discover"
            tabBarSource: sourceTabs
            Rectangle { width: parent.width; height: 520; color: "transparent" }
            Colosseum.WorldTabBar {
                id: sourceTabs
                objectName: "sourceTabs"
                tabPrefix: "theatreTab"
                backdrop: null
                tabModel: [{ key: "discover", label: "Discover" }, { key: "library", label: "Library" }]
                currentTab: activeTabForTest
                onTabRequested: (tab) => { testCase.requestedTab = tab; activeTabForTest = tab }
            }
            Rectangle { width: parent.width; height: 1400; color: "transparent" }
        }
    }
    function findChild(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findChild(children[i], name)
            if (found) return found
        }
        return null
    }
    function init() {
        requestedTab = ""
        world = worldComponent.createObject(testWindow)
        verify(world !== null)
        wait(80)
    }
    function cleanup() {
        if (world) world.destroy()
        world = null
    }
    function test_dock_visibility_click_and_focus_handoff() {
        var page = world.pageFlickable
        var source = world.sourceForTest
        var dock = findChild(world, "theatreTabDock")
        verify(dock !== null)
        compare(world.tabsDocked, false)
        compare(dock.visible, false)
        compare(source.opacity, 1)
        source.keyboardIndex = 1
        testWindow.requestActivate()
        wait(80)
        source.forceActiveFocus()
        tryCompare(source, "activeFocus", true)
        var crossing = source.mapToItem(page.contentItem, 0, 0).y
        page.contentY = crossing - 2
        compare(world.tabsDocked, false)
        page.contentY = crossing + 2
        tryCompare(world, "tabsDocked", true)
        compare(dock.visible, true)
        compare(source.opacity, 0)
        tryCompare(dock, "activeFocus", true)
        compare(dock.keyboardIndex, 1)
        var pill = findChild(dock, "theatreTabDock_library")
        verify(pill !== null)
        mouseClick(pill, pill.width / 2, pill.height / 2)
        compare(requestedTab, "library")
        page.contentY = 0
        tryCompare(world, "tabsDocked", false)
        compare(dock.visible, false)
        compare(source.opacity, 1)
        tryCompare(source, "activeFocus", true)
    }
}
