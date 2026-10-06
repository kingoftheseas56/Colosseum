import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "WideWorldRail"
    when: windowShown

    Window {
        id: testWindow
        width: 1143
        height: 686
        visible: true
    }

    Component {
        id: railComponent
        App.WideWorldRail {
            height: 686
            worlds: [
                { name: "Theatre",  icon: "" },
                { name: "Tankoban", icon: "" },
                { name: "Biblio",   icon: "" }
            ]
            activeTab: "Tankoban"
        }
    }

    SignalSpy { id: worldSpy; signalName: "worldRequested" }
    SignalSpy { id: profileSpy; signalName: "profileRequested" }

    property var rail: null

    function init() {
        rail = railComponent.createObject(testWindow.contentItem)
        verify(rail !== null)
        worldSpy.target = rail
        profileSpy.target = rail
        wait(0)
    }

    function cleanup() {
        worldSpy.clear()
        profileSpy.clear()
        if (rail)
            rail.destroy()
        rail = null
    }

    function findChild(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findChild(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function test_mock_geometry_and_targets() {
        compare(rail.width, 92)
        var background = findChild(rail, "wideWorldRailBackground")
        verify(background !== null)
        compare(background.color.a, 1)

        var names = ["Theatre", "Tankoban", "Biblio", "Profile"]
        for (var i = 0; i < names.length; ++i) {
            var tab = findChild(rail, "wideWorldRailTab_" + names[i])
            verify(tab !== null)
            compare(tab.width, 92)
            verify(tab.height >= 48, names[i] + " height must be at least 48")
            var hitArea = findChild(tab, "wideWorldRailHitArea_" + names[i])
            verify(hitArea !== null, names[i] + " must own a consuming hit area")
            compare(hitArea.width, tab.width)
            compare(hitArea.height, tab.height)
            compare(hitArea.preventStealing, true)
            compare(hitArea.propagateComposedEvents, false)
        }

        compare(findChild(rail, "wideWorldRailTab_Tankoban").active, true)
        compare(findChild(rail, "wideWorldRailTab_Theatre").active, false)
    }

    function test_world_and_profile_intent() {
        findChild(rail, "wideWorldRailTab_Theatre").activate()
        compare(worldSpy.count, 1)
        compare(worldSpy.signalArguments[0][0], "Theatre")

        findChild(rail, "wideWorldRailTab_Profile").activate()
        compare(profileSpy.count, 1)
    }
}
