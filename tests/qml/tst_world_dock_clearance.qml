import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "WorldDockClearance"
    when: windowShown

    property int detailsClicks: 0

    Window {
        id: shell
        visible: true
        width: 430
        height: 435

        Colosseum.WorldPage {
            id: world
            anchors.fill: parent
            medium: "Theatre"
            contentBottomInset: taskbar.contentBottomInset

            Colosseum.FeaturedCarousel {
                id: hero
                height: implicitHeight
                slides: [{ title: "Featured film" }]
                onSecondaryClicked: testCase.detailsClicks++
            }
        }

        Colosseum.Taskbar {
            id: taskbar
            z: 900
        }
    }

    function test_detailsCanBeTappedAboveDock_data() {
        return [
            { tag: "compact-closed", width: 430, height: 435, dockOpen: false },
            { tag: "wide-open", width: 1280, height: 465, dockOpen: true },
            { tag: "roomy", width: 1280, height: 720, dockOpen: false }
        ]
    }

    function test_detailsCanBeTappedAboveDock(data) {
        shell.width = data.width
        shell.height = data.height
        taskbar.open = data.dockOpen
        detailsClicks = 0
        const page = world.pageFlickable
        page.contentY = 0

        const dock = findChild(taskbar, "taskbarDock")
        const details = findChild(hero, "featuredSecondaryAction_0")
        verify(dock && details)
        tryVerify(function() { return page.y + page.height <= dock.y },
                  1000, "the world viewport must end above the dock")

        const detailsBottom = details.mapToItem(shell.contentItem, 0, details.height).y
        page.contentY = Math.max(0, detailsBottom - dock.y + 4)
        const point = details.mapToItem(shell.contentItem, details.width / 2, details.height / 2)
        verify(point.y < dock.y)
        mouseClick(shell.contentItem, point.x, point.y)
        compare(detailsClicks, 1)
    }
}
