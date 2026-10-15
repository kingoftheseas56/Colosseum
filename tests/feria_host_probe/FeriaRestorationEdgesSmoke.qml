// Plain QQmlApplicationEngine bootstrap, matching browser_main.cpp.
// Run only with a disposable Feria profile.
import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria"

Window {
    id: window
    width: 1280; height: 900; visible: true
    property var parked: null
    property bool completed: false
    property var ids: ["stranger","wednesday","squid","arcane","dune2","phm",
                       "stranger","wednesday","squid","arcane","dune2","phm"]
    property bool showShelf: true
    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: FeriaWorld {
            lifecycleActive: true
            function shownShelves() {
                return window.showShelf ? [{id:"repair",title:"Repair fixtures",apps:["netflix"],v:"watch"}] : []
            }
            function shelfItems(shelf) { return window.showShelf ? window.ids : [] }
        }
        onLoaded: if (window.parked) item.restoreNavigationSnapshot(window.parked)
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
    TestCase {
        id: probe
        when: windowShown
        function check(ok, detail) {
            browserReporter.report("feria-restoration-edges", !!ok, detail)
            verify(ok, detail)
        }
        function until(predicate, detail) {
            for (var i = 0; i < 50 && !predicate(); ++i) wait(40)
            check(predicate(), detail)
        }
        function home() { return window.find(loader.item, "home-focus-variant") }
        function page() { return window.find(loader.item, "feriaHomeScroll") }
        function see() { return window.find(loader.item, "feriaSeeAllView") }
        function grid() { return window.find(loader.item, "feriaSeeAllGrid") }
        function restoreHome(y) {
            home().restoreNavigationSnapshot({lensPositions:{all:{y:y,rails:{}}}})
        }
        function pendingGrid() {
            see().restoreReturn({index:5,y:120})
            check(!!see().pendingReturnSnapshot, "See All restoration explicitly pending")
        }
        function run() {
            until(function() { return loader.item && home() && page().contentHeight > page().height },
                  "Renderable Home fixture ready")
            mouseMove(loader.item, 1, 1)
            var savedY = Math.min(400, page().contentHeight - page().height)
            page().contentY = savedY
            window.parked = loader.item.navigationSnapshot()
            loader.active = false
            wait(40)
            check(loader.item === null, "Actual Loader destruction")
            loader.active = true
            until(function() { return loader.item && home() && home().pendingNavigationPosition },
                  "First recreation is still restoring")
            // Re-park before settlement; do not sample an incidental half-laid-out position.
            var reparking = loader.item.navigationSnapshot()
            check(Math.abs(reparking.home.lensPositions.all.y - savedY) < 1,
                  "Quick repark preserves intended Home position")
            window.parked = reparking
            loader.active = false; wait(40); loader.active = true
            until(function() { return loader.item && home() && !home().pendingNavigationPosition },
                  "Second recreation settles without input")
            check(Math.abs(page().contentY - Math.min(savedY, page().contentHeight - page().height)) < 1,
                  "No-input restoration survives actual repeated recreation")
            window.parked = null

            restoreHome(savedY)
            mouseWheel(page(), page().width / 2, page().height / 2, 0, -120)
            until(function() { return !home().pendingNavigationPosition },
                  "Real wheel cancels Home restoration")
            check(!home().pendingLensPosition, "Wheel also cancels a pending lens replay")

            restoreHome(savedY)
            mousePress(page(), page().width / 2, page().height / 2, Qt.LeftButton)
            mouseMove(page(), page().width / 2, page().height / 2 - 80, 80)
            mouseRelease(page(), page().width / 2, page().height / 2 - 80, Qt.LeftButton)
            until(function() { return !home().pendingNavigationPosition }, "Real drag cancels Home restoration")

            restoreHome(savedY)
            loader.item.selectLens("read")
            check(!home().pendingNavigationPosition && loader.item.lens === "read",
                  "New lens invalidates prior Home restoration")
            until(function() { return !home().pendingLensPosition }, "New lens position settles")
            loader.item.selectLens("all")
            until(function() { return !home().pendingLensPosition }, "All lens restored")

            loader.item.openSeeAll(loader.item.shownShelves()[0])
            until(function() { return grid() && grid().count >= 6 && grid().activeFocus },
                  "See All fixture ready")
            pendingGrid()
            grid().forceActiveFocus()
            keyClick(Qt.Key_Tab)
            until(function() { return !see().pendingReturnSnapshot }, "Real Tab cancels grid restoration")
            var tabTarget = window.activeFocusItem
            wait(160)
            check(window.activeFocusItem === tabTarget, "Restoration does not steal Tab focus")

            pendingGrid()
            mouseWheel(grid(), grid().width / 2, grid().height / 2, 0, -120)
            until(function() { return !see().pendingReturnSnapshot }, "Real wheel cancels grid restoration")

            pendingGrid()
            loader.item.openSearch("Dune")
            until(function() { return loader.item.viewState === "search" && !see().pendingReturnSnapshot },
                  "Search supersedes pending See All restoration")
            wait(160)
            check(!grid().activeFocus && loader.item.query === "Dune",
                  "Hidden See All cannot reclaim focus from Search")
            loader.item.back()
            until(function() { return loader.item.viewState === "seeAll" && grid().activeFocus },
                  "Search return origin remains See All")

            pendingGrid()
            // A real pointer click selects/opens a card, and wins over the old return.
            grid().contentY = grid().originY
            mouseClick(grid(), grid().cellWidth / 2, grid().cellHeight / 2, Qt.LeftButton)
            until(function() { return loader.item.viewState === "title" }, "Real pointer opens a selected title")
            wait(160)
            check(!see().pendingReturnSnapshot && !grid().activeFocus,
                  "Stale grid restoration cannot undo pointer route/focus")
            loader.item.back()
            until(function() { return loader.item.viewState === "seeAll" }, "Pointer title Back keeps origin")

            see().restoreReturn({index:999,y:99999})
            window.ids = ["stranger","phm"]
            window.width = 900
            until(function() { return !see().pendingReturnSnapshot }, "Shortened model and resized viewport settle")
            check(grid().currentIndex >= 0 && grid().currentIndex < grid().count,
                  "Removed selection clamps to a valid remaining card")
            check(grid().contentY <= Math.max(grid().originY - grid().topMargin,
                      grid().originY + grid().contentHeight - grid().height + grid().bottomMargin) + 1,
                  "Restored offset is bounded after resize")
            see().restoreReturn({index:5,y:1000})
            window.ids = []
            until(function() { return !see().pendingReturnSnapshot }, "Empty model terminates restoration")
            check(grid().currentIndex === -1, "Empty model has no invalid selected card")

            loader.item.goHome()
            window.showShelf = false
            loader.item.focusArea = "shelf"
            loader.item.shelfIndex = 999
            loader.item.shelfCardIndex = 999
            restoreHome(99999)
            until(function() { return !home().pendingNavigationPosition }, "Home removed shelves settle")
            check(loader.item.focusArea === "lens" && loader.item.shelfIndex === 0,
                  "Removed focused shelf falls back safely")
            window.completed = true
        }
        function cleanup() {
            browserReporter.report("feria-restoration-edges", window.completed,
                                   "All restoration edge assertions executed")
            browserReporter.finish()
        }
    }
    Timer {
        interval: 100; running: true
        onTriggered: { window.hide(); window.show(); window.requestActivate() }
    }
    Timer {
        interval: 500; running: true
        onTriggered: {
            try { probe.run() }
            finally { probe.cleanup() }
        }
    }
    Timer {
        interval: 45000; running: true
        onTriggered: {
            browserReporter.report("feria-restoration-edges", false, "Deadline exceeded")
            browserReporter.finish()
        }
    }
}
