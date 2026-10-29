import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
import "../../qml/+android" as Android

TestCase {
    id: testCase
    name: "PhoneTouchChrome"
    when: windowShown
    Window {
        id: host
        width: 792; height: 354; visible: true
        property bool androidHost: true
    }
    Component { id: backComponent; App.BackAction {} }
    Component { id: heroComponent; Android.ExtensionsHouseHero { width: 354 } }
    Component {
        id: navigationComponent
        App.PhoneWorldTabBar {
            width: host.width
            worlds: [{name: "Theatre", icon: ""}, {name: "Tankoban", icon: ""}]
        }
    }
    SignalSpy { id: worldSpy; signalName: "worldRequested" }
    function named(root, name) {
        if (root.objectName === name) return root
        for (let item of root.children) {
            const found = named(item, name)
            if (found) return found
        }
        return null
    }
    function test_back_has_own_48dp_layout_space_data() {
        return [{tag:"plain", variant:"plain"}, {tag:"capsule", variant:"capsule"},
                {tag:"immersive", variant:"immersive"}]
    }
    function test_back_has_own_48dp_layout_space(data) {
        const back = createTemporaryObject(backComponent, host.contentItem, {variant:data.variant})
        verify(back.width >= 48 && back.height >= 48, "Back host must reserve48dp without overlapping neighbours")
    }
    function test_extensions_search_has_48dp_target() {
        const hero = createTemporaryObject(heroComponent, host.contentItem)
        const search = named(hero, "extensionsSearchButton")
        verify(search !== null)
        verify(search.width >= 48 && search.height >= 48)
    }
    function test_navigation_press_and_drag_cancel() {
        host.requestActivate()
        const nav = createTemporaryObject(navigationComponent, host.contentItem)
        worldSpy.target = nav
        worldSpy.clear()
        const hit = named(nav, "phoneWorldTabHitArea_Theatre")
        const feedback = named(nav, "phoneWorldTabFeedback_Theatre")
        verify(hit !== null && feedback !== null)
        verify(hit.width >= 48 && hit.height >= 48)
        mousePress(hit, hit.width / 2, hit.height / 2)
        verify(feedback.opacity > 0, "Feedback must be visible while the finger is down")
        mouseMove(hit, hit.width / 2 + 28, hit.height / 2, 30)
        mouseRelease(hit, hit.width / 2 + 28, hit.height / 2)
        compare(worldSpy.count, 0, "A moved press must not switch worlds")
        mouseClick(hit, hit.width / 2, hit.height / 2)
        compare(worldSpy.count, 1)
        compare(worldSpy.signalArguments[0][0], "Theatre")
    }
}
