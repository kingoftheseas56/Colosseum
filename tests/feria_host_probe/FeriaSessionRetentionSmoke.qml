import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria"

Window {
    id: window
    width: 1280; height: 900; visible: true

    property int phase: 0
    property bool finished: false
    property var snapshot: null
    property real homeY: 0
    property real railX: 0
    property int homeFocusIndex: 0
    property int seeAllIndex: -1
    property real seeAllY: 0

    TestCase {
        id: eventPump
        when: windowShown
        function run() {
            // A stationary physical pointer over a recreated card is outside
            // this no-input retention scenario; park it away from the cards.
            mouseMove(feriaLoader.item, 1, 1)
            while (!window.finished) wait(50)
        }
    }
    Timer {
        interval: 100; running: true
        onTriggered: { window.hide(); window.show(); window.requestActivate() }
    }
    Timer {
        interval: 500; running: true
        onTriggered: {
            try { eventPump.run() }
            finally {
                browserReporter.report("feria-session-retention", window.finished && window.phase >= 6,
                                       "Session retention reached every acceptance phase")
                browserReporter.finish()
            }
        }
    }

    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) {
            var found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }

    function check(ok, detail) {
        browserReporter.report("feria-session-retention", ok, detail)
        if (!ok) finish()
        return ok
    }

    function finish() {
        if (finished) return
        finished = true
        step.stop()
        deadline.stop()
        Qt.callLater(function() { browserReporter.finish() })
    }

    function recreate() {
        feriaLoader.active = false
        Qt.callLater(function() { feriaLoader.active = true })
    }

    Loader {
        id: feriaLoader
        anchors.fill: parent
        active: true
        sourceComponent: FeriaWorld {
            lifecycleActive: true
        }
        onLoaded: {
            if (window.snapshot && item.restoreNavigationSnapshot)
                item.restoreNavigationSnapshot(window.snapshot)
        }
    }
    Timer {
        id: deadline
        interval: 15000
        running: true
        onTriggered: {
            window.check(false, "Session retention smoke timed out")
            window.finish()
        }
    }

    Timer {
        id: step
        interval: 650
        repeat: true
        running: true
        onTriggered: {
            var feria = feriaLoader.item
            if (!feria) return

            if (window.phase === 0) {
                if (!window.check(typeof feria.navigationSnapshot === "function"
                    && typeof feria.restoreNavigationSnapshot === "function",
                    "Feria exposes navigation snapshot and restore contracts")) return
                feria.selectLens("watch")
                feria.selectApp(Math.min(2, feria.activeApps.length - 1))
            } else if (window.phase === 1) {
                var homeView = window.find(feria, "home-focus-variant")
                var home = window.find(feria, "feriaHomeScroll")
                var sidebar = window.find(feria, "feriaRowIndexSidebar")
                if (!window.check(homeView && home && sidebar && sidebar.visibleRows.length > 2,
                    "Home navigation surfaces are present")) return
                if (home.contentHeight <= home.height + 200) return
                // The fixture's programmatic position is authored only after the
                // preceding lens replay settles; it is not a user input event.
                if (homeView.pendingLensPosition || homeView.pendingNavigationPosition) return

                feria.focusArea = "shelf"
                feria.shelfIndex = Math.min(4, feria.shownShelves().length - 1)
                feria.shelfCardIndex = 2
                home.contentY = Math.min(900, Math.max(0, home.contentHeight - home.height))
                sidebar.visibleRows[2].target.restoreRail(180)

                window.homeY = home.contentY
                window.railX = sidebar.visibleRows[2].target.railContentX
                window.homeFocusIndex = feria.focusIndex
                if (!window.check(window.homeY > 0,
                    "Home snapshot captures a non-zero vertical position")) return
                window.snapshot = feria.navigationSnapshot()
                window.recreate()
            } else if (window.phase === 2) {
                var restoredHomeView = window.find(feria, "home-focus-variant")
                if (restoredHomeView.pendingNavigationPosition) return
                var restoredHome = window.find(feria, "feriaHomeScroll")
                var restoredSidebar = window.find(feria, "feriaRowIndexSidebar")
                var homeOk = feria.viewState === "home"
                    && feria.lens === "watch"
                    && feria.focusArea === "shelf"
                    && feria.focusIndex === window.homeFocusIndex
                    && Math.abs(restoredHome.contentY - window.homeY) < 1
                    && Math.abs(restoredSidebar.visibleRows[2].target.railContentX - window.railX) < 1
                if (!window.check(homeOk,
                    "Home lens, focus, vertical scroll and shelf rail survive recreation")) return

                feria.openTitle("stranger")
                feria.openSearch("Dune")
                window.snapshot = feria.navigationSnapshot()
                window.recreate()
            } else if (window.phase === 3) {
                if (!window.check(feria.viewState === "search"
                    && feria.query === "Dune"
                    && feria.searchReturnState === "title"
                    && feria.selectedTitle === "stranger",
                    "Search query and title origin survive recreation")) return
                feria.back()
                feria.back()
                feria.openSeeAll(feria.shownShelves()[0])
            } else if (window.phase === 4) {
                var grid = window.find(feria, "feriaSeeAllGrid")
                if (!window.check(grid && grid.count > 1,
                    "See all grid is available")) return
                window.seeAllIndex = grid.count - 1
                grid.keyboardMode = true
                grid.forceActiveFocus()
                grid.currentIndex = window.seeAllIndex
                grid.positionViewAtIndex(window.seeAllIndex, GridView.Contain)
                window.seeAllY = grid.contentY
                window.snapshot = feria.navigationSnapshot()
                if (!window.check(window.snapshot.seeAllReturnSnapshot
                    && window.snapshot.seeAllReturnSnapshot.index === window.seeAllIndex,
                    "Snapshot captures the selected See all card before destruction")) return
                window.recreate()
            } else if (window.phase === 5) {
                var restoredView = window.find(feria, "feriaSeeAllView")
                if (restoredView.pendingReturnSnapshot || !restoredView.hoverSelectionEnabled) return
                var restoredGrid = window.find(feria, "feriaSeeAllGrid")
                if (!window.check(feria.viewState === "seeAll"
                    && restoredGrid.currentIndex === window.seeAllIndex
                    && Math.abs(restoredGrid.contentY - window.seeAllY) < 1,
                    "See all selection and scroll survive recreation: "
                    + JSON.stringify({index:restoredGrid.currentIndex,expectedIndex:window.seeAllIndex,
                        y:restoredGrid.contentY,expectedY:window.seeAllY}))) return

                feria.openTitle(restoredGrid.items[restoredGrid.currentIndex])
                feria.hostReturnState = "title"
                feria.viewState = "host"
                window.snapshot = feria.navigationSnapshot()
                window.recreate()
            } else if (window.phase === 6) {
                if (!window.check(feria.viewState === "title"
                    && feria.hostUrl === ""
                    && feria.hostApp === "",
                    "Recreation collapses an embedded service to its owning Feria route")) return
                window.finish()
            }
            window.phase++
        }
    }
}
