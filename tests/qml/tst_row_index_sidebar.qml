import QtQuick
import QtQuick.Window
import QtTest
import "../../qml"

TestCase {
    id: testCase
    name: "RowIndexSidebar"
    when: windowShown
    property var fixture: null

    Window {
        id: win
        width: 980
        height: 700
        visible: true
    }

    Component {
        id: fixtureComponent

        Item {
            id: fixtureRoot
            width: 900
            height: 620

            property Item requestedTarget: null
            property alias rail: rail
            property alias scroller: scroller
            property alias firstRow: firstRow
            property alias secondRow: secondRow
            property alias thirdRow: thirdRow
            property alias flowHost: flowHost

            Item {
                id: flowHost
                y: 180
                width: 500
                height: 1200
            }

            Flickable {
                id: scroller
                x: rail.contentLeft
                width: fixtureRoot.width - x
                height: fixtureRoot.height
                contentWidth: width
                contentHeight: rows.height
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: rows
                    width: scroller.width
                    spacing: 40

                    Item { id: firstRow; objectName: "fixtureFirstRow"; width: rows.width; height: 260 }
                    Item { id: secondRow; objectName: "fixtureSecondRow"; width: rows.width; height: 260 }
                    Item { id: thirdRow; objectName: "fixtureThirdRow"; width: rows.width; height: 260 }
                    Item { id: hiddenRow; width: rows.width; height: 260; visible: false }
                    Item { width: rows.width; height: 300 }
                }
            }

            Item {
                width: 0
                height: 0
                RowIndexSidebar {
                    id: rail
                    parent: fixtureRoot
                    pageFlick: scroller
                    worldName: "Theatre"
                    automationPrefix: "theatreRowIndex"
                    rows: [
                        { key: "alpha", title: "Alpha", target: firstRow },
                        { key: "beta", title: "Beta", target: secondRow },
                        { key: "gamma", title: "Gamma", target: thirdRow },
                        { key: "hidden", title: "Hidden", target: hiddenRow }
                    ]
                    onRowRequested: (target) => fixtureRoot.requestedTarget = target
                }
            }
        }
    }

    function init() {
        fixture = createTemporaryObject(fixtureComponent, win.contentItem)
        verify(fixture !== null)
        win.requestActivate()
        wait(0)
    }

    function cleanup() {
        if (fixture)
            fixture.destroy()
        fixture = null
    }

    function test_starts_closed_and_toggle_changes_content_offset() {
        verify(fixture.rail.collapsed)
        compare(fixture.rail.width, 52)
        compare(fixture.rail.contentLeft, 80)

        mouseClick(findChild(fixture, "theatreRowIndexCollapseAction"), 8, 14)
        verify(!fixture.rail.collapsed)
        tryCompare(fixture.rail, "width", 240, 1000)
        compare(fixture.rail.contentLeft, 268)
    }

    function test_lists_rows_in_page_order_with_accessible_actions() {
        compare(fixture.rail.automationRows, "Alpha\nBeta\nGamma")
        verify(findChild(fixture, "theatreRowIndexRow_alpha") !== null)
        compare(findChild(fixture, "theatreRowIndexRow_hidden"), null)
        verify(findChild(fixture, "theatreRowIndexList") !== null)
        compare(findChild(fixture, "theatreRowIndexRow_betaAction").accessibleName,
                "Row Beta")
    }

    function test_current_row_tracks_the_scroll_position() {
        compare(fixture.rail.currentIndex, 0)
        compare(fixture.rail.automationCurrentRow, "Alpha")
        fixture.scroller.contentY = 225
        tryCompare(fixture.rail, "currentIndex", 1)
        compare(fixture.rail.automationCurrentRow, "Beta")
        verify(findChild(fixture, "theatreRowIndexRow_beta").current)
    }

    function test_expanded_long_names_wrap_without_truncation() {
        const title = "Community Collections and Complete Comic Runs"
        fixture.rail.rows = [{ key: "long", title: title, target: fixture.firstRow }]
        fixture.rail.collapsed = false
        tryCompare(fixture.rail, "width", 240, 1000)
        const label = findChild(fixture, "theatreRowIndexRow_longLabel")
        const row = findChild(fixture, "theatreRowIndexRow_long")
        verify(label !== null)
        compare(label.text, title)
        verify(label.lineCount > 1)
        verify(!label.truncated)
        verify(row.height >= label.implicitHeight + 20)
        fixture.rail.collapsed = true
        compare(row.height, 46)
    }

    function test_reparented_rail_follows_host_visibility() {
        fixture.rail.flowHost = fixture.flowHost
        verify(fixture.rail.visible)
        fixture.flowHost.visible = false
        verify(!fixture.rail.visible)
        fixture.flowHost.visible = true
        verify(fixture.rail.visible)
    }

    function test_flow_position_updates_when_content_above_host_changes() {
        fixture.rail.flowHost = fixture.flowHost
        compare(fixture.rail.y, 180)
        fixture.flowHost.y = 260
        compare(fixture.rail.y, 260)
    }

    function test_last_row_is_current_at_the_end_of_the_page() {
        fixture.scroller.contentY = fixture.scroller.contentHeight - fixture.scroller.height
        tryCompare(fixture.rail, "automationCurrentRow", "Gamma")
    }

    function test_click_routes_the_real_row_target() {
        mouseClick(findChild(fixture, "theatreRowIndexRow_gammaAction"), 10, 23)
        compare(fixture.requestedTarget, fixture.thirdRow)
    }

    function test_down_arrow_moves_focus_to_the_next_row() {
        var firstAction = findChild(fixture, "theatreRowIndexRow_alphaAction")
        var secondAction = findChild(fixture, "theatreRowIndexRow_betaAction")
        firstAction.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return firstAction.activeFocus })
        keyClick(Qt.Key_Down)
        tryVerify(function() { return secondAction.activeFocus })
    }
}
