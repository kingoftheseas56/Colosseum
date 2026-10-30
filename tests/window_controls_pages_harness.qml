// Offscreen page-level smoke: the real page tree must render/route visible buttons.
import QtQuick
import QtTest
import "../qml"

Item {
    id: harness
    width: 1280; height: 850
    property int actions: 0

    function findByName(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const children = item.children || []
        for (let i = 0; i < children.length; i++) {
            const hit = findByName(children[i], name)
            if (hit) return hit
        }
        return null
    }

    Loader {
        id: history
        anchors.fill: parent
        active: false
        source: "../qml/HistoryHighlightsStatsPage.qml"
    }
    Loader {
        id: tracker
        anchors.fill: parent
        active: false
        source: "../qml/TrackerSyncCenterPage.qml"
    }
    TestCase {
        name: "DesktopPageChrome"
        when: windowShown
        function init() { harness.actions = 0 }
        function cleanup() {
            history.active = false
            tracker.active = false
        }

        function test_history_header() {
            history.active = true
            tryCompare(history, "status", Loader.Ready, 6000)
            const page = history.item
            verify(page !== null)
            page.minimizeRequested.connect(function() { harness.actions++ })
            const icon = harness.findByName(page, "historyStatsWindowMinimize")
            verify(icon !== null && icon.visible)
            mouseClick(icon, 11, 11)
            compare(harness.actions, 1)
        }

        function test_tracker_header() {
            tracker.active = true
            tryCompare(tracker, "status", Loader.Ready, 6000)
            const page = tracker.item
            verify(page !== null)
            page.minimizeRequested.connect(function() { harness.actions++ })
            const icon = harness.findByName(page, "trackerWindowMinimize")
            verify(icon !== null && icon.visible)
            mouseClick(icon, 11, 11)
            compare(harness.actions, 1)
        }
    }
}
