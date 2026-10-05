import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneWorldTabBar"
    when: windowShown

    Window {
        id: testWindow
        width: 412
        height: 915
        visible: true
    }

    Component {
        id: barComponent
        App.PhoneWorldTabBar {
            width: 412
            worlds: [
                { name: "Theatre",  icon: "" },
                { name: "Tankoban", icon: "" },
                { name: "Biblio",   icon: "" }
            ]
        }
    }

    SignalSpy { id: worldSpy; signalName: "worldRequested" }
    SignalSpy { id: profileSpy; signalName: "profileRequested" }

    property var bar: null

    function init() {
        bar = barComponent.createObject(testWindow.contentItem)
        verify(bar !== null)
        worldSpy.target = bar
        profileSpy.target = bar
        wait(0)
    }

    function cleanup() {
        worldSpy.clear()
        profileSpy.clear()
        if (bar)
            bar.destroy()
        bar = null
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
        compare(bar.navigationBottomPadding, 48)
        compare(bar.height, 120)
        var background = findChild(bar, "phoneWorldTabBarBackground")
        compare(background.color.a, 1)
        var names = ["Theatre", "Tankoban", "Biblio", "Profile"]
        for (var i = 0; i < names.length; ++i) {
            var tab = findChild(bar, "phoneWorldTab_" + names[i])
            verify(tab !== null)
            verify(tab.width >= 48, names[i] + " width must be at least 48")
            verify(tab.height >= 48, names[i] + " height must be at least 48")
            var hitArea = findChild(tab, "phoneWorldTabHitArea_" + names[i])
            verify(hitArea !== null, names[i] + " must own a consuming hit area")
            compare(hitArea.width, tab.width)
            compare(hitArea.height, tab.height)
            compare(hitArea.preventStealing, true)
            compare(hitArea.propagateComposedEvents, false)
        }
    }

    function test_world_and_profile_intent() {
        var theatre = findChild(bar, "phoneWorldTab_Theatre")
        var profile = findChild(bar, "phoneWorldTab_Profile")
        theatre.activate()
        compare(worldSpy.count, 1)
        compare(worldSpy.signalArguments[0][0], "Theatre")
        profile.activate()
        compare(profileSpy.count, 1)
    }
}
