import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// World-feel Slice 8, "rows park" (the halfway mock's park): after Up/Down lands focus, the world
// page glides so the landed row's section sits just under the docked tab bar. Landing on the tab
// bar parks the bar at the top; anything above the bar returns the page to its top.
TestCase {
    id: testCase
    name: "WorldRowsPark"
    when: windowShown

    Window {
        id: testWindow
        width: 900
        height: 720
        visible: true

        FocusScope {
            anchors.fill: parent
            focus: true

            Colosseum.WorldPage {
                id: world
                objectName: "parkWorld"
                anchors.fill: parent
                medium: "Park"
                focus: true
                tabBarSource: tabs

                Item { id: hero; width: 800; height: 260
                    Colosseum.KeyboardAction { id: heroAction; x: 20; y: 100; width: 120; height: 44; pointerEnabled: false }
                }
                Colosseum.WorldTabBar {
                    id: tabs
                    backdrop: hero
                    tabModel: [{ key: "a", label: "A" }, { key: "b", label: "B" }]
                    currentTab: "a"
                }
                Repeater {
                    model: 6
                    Item {
                        id: section
                        required property int index
                        objectName: "parkSection" + index
                        width: 800; height: 300
                        Text { text: "Row " + section.index; font.pixelSize: 24 }
                        Colosseum.KeyboardAction {
                            objectName: "parkCard" + section.index
                            x: 20; y: 60; width: 140; height: 200; pointerEnabled: false
                        }
                    }
                }
            }
        }
    }

    function find(name) {
        var queue = [world]
        while (queue.length) {
            var node = queue.shift()
            if (node.objectName === name)
                return node
            var kids = node.children || []
            for (var i = 0; i < kids.length; ++i)
                queue.push(kids[i])
        }
        return null
    }

    function sectionTop(item) {
        return item.mapToItem(world.pageFlickable.contentItem, 0, 0).y
    }

    function test_down_parks_each_row_under_the_dock() {
        var flick = world.pageFlickable
        var card0 = find("parkCard0")
        var card1 = find("parkCard1")
        verify(card0 && card1)
        testWindow.requestActivate()
        testWindow.raise()
        card0.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return card0.activeFocus })
        keyClick(Qt.Key_Down)
        tryVerify(function() { return card1.activeFocus })
        var expected = sectionTop(find("parkSection1")) - world.parkDockSpace - world.parkGap
        tryCompare(flick, "contentY", expected, 2000, "the landed row parks under the dock")
    }

    function test_park_targets_for_bar_and_above_bar() {
        var barTop = sectionTop(tabs)
        compare(world.parkY(heroAction), 0, "above the tab bar returns the page to its top")
        compare(world.parkY(tabs), barTop - 12, "the tab bar parks at the top")
        var section2 = find("parkSection2")
        compare(world.parkY(find("parkCard2")),
                sectionTop(section2) - world.parkDockSpace - world.parkGap,
                "a card parks its whole section, header included")
    }
}
