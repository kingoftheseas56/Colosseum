import QtQuick
import QtQuick.Window
import "../../qml" as Colosseum
import "../../qml/feria"

Window {
    id: window
    width: 1280; height: 900; visible: true
    property int phase: 0
    property bool finished: false
    property int homeRequests: 0
    property int gridIndex: -1
    property real gridY: 0
    property var topbar: null

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
        browserReporter.report("feria-contextual-back", ok, detail)
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

    Item { id: defaultBackdrop; visible: false }
    Colosseum.TopBar {
        id: defaultWorldTopbar
        visible: false
        backdrop: defaultBackdrop
        activeMedium: "Biblio"
    }

    FeriaWorld {
        id: feria
        anchors.fill: parent
        lifecycleActive: true
        sharedAccount: true
        onHomeRequested: window.homeRequests++
    }

    Timer {
        id: deadline
        interval: 10000
        running: true
        onTriggered: {
            window.check(false, "Contextual Back smoke timed out")
            window.finish()
        }
    }

    Timer {
        id: step
        interval: 250
        repeat: true
        running: true
        onTriggered: {
            if (window.phase === 0) {
                if (!window.check(String(defaultWorldTopbar.backLabel) === "Home",
                    "Shared TopBar keeps Home as the default world destination")) return
                window.topbar = window.find(feria, "feriaWorldTopBar")
                if (!window.check(topbar && topbar.visible
                    && String(topbar.backLabel) === "Home",
                    "Feria root exposes Home as the world exit")) return
                topbar.homeRequested()
                if (!window.check(window.homeRequests === 1
                    && feria.viewState === "home",
                    "Home destination still exits Feria")) return
                feria.openTitle("stranger")
            } else if (window.phase === 1) {
                if (!window.check(feria.viewState === "title"
                    && String(topbar.backLabel) === "Feria",
                    "Title exposes Feria as the contextual Back destination")) return
                topbar.homeRequested()
                if (!window.check(feria.viewState === "home"
                    && window.homeRequests === 1,
                    "Title Back returns inside Feria without exiting the world")) return
                feria.openTitle("stranger")
                feria.openSearch("Dune")
            } else if (window.phase === 2) {
                if (!window.check(feria.viewState === "search"
                    && feria.searchReturnState === "title"
                    && String(topbar.backLabel) === "Feria",
                    "Search remembers the title that opened it")) return
                topbar.homeRequested()
                if (!window.check(feria.viewState === "title"
                    && feria.selectedTitle === "stranger",
                    "Search Back restores its title origin")) return
                topbar.homeRequested()
            } else if (window.phase === 3) {
                var row = feria.shownShelves()[0]
                if (!window.check(feria.viewState === "home" && !!row,
                    "Title Back restores Feria home before See all")) return
                feria.openSeeAll(row)
            } else if (window.phase === 4) {
                var grid = window.find(feria, "feriaSeeAllGrid")
                if (!window.check(feria.viewState === "seeAll"
                    && grid && grid.count > 0,
                    "See all is available for origin restoration")) return
                window.gridIndex = grid.count - 1
                grid.currentIndex = window.gridIndex
            } else if (window.phase === 5) {
                var settledGrid = window.find(feria, "feriaSeeAllGrid")
                window.gridY = settledGrid.contentY
                feria.openSearch("")
            } else if (window.phase === 6) {
                if (!window.check(feria.viewState === "search"
                    && feria.searchReturnState === "seeAll",
                    "Search remembers See all as its origin")) return
                topbar.homeRequested()
            } else if (window.phase === 7) {
                var restored = window.find(feria, "feriaSeeAllGrid")
                if (!window.check(feria.viewState === "seeAll"
                    && restored.currentIndex === window.gridIndex
                    && Math.abs(restored.contentY - window.gridY) < 1,
                    "Search Back restores the See all selection and scroll")) return
                topbar.homeRequested()
            } else if (window.phase === 8) {
                if (!window.check(feria.viewState === "home"
                    && String(topbar.backLabel) === "Home",
                    "See all Back returns to Feria home and restores Home")) return
                window.finish()
            }
            window.phase++
        }
    }
}
