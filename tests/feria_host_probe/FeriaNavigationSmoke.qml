import QtQuick
import QtQuick.Window
import "../../qml/feria"

Window {
    id: window
    width: 1280; height: 900; visible: true
    property int phase: 0
    property bool finished: false
    property real allY: 0
    property real watchY: 0
    property real allRailX: 0
    property var home: null
    property var scroll: null
    property var sidebar: null
    property var tabs: null
    property var dock: null
    function find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) {
            var found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }
    function check(ok, detail) {
        browserReporter.report("feria-navigation", ok, detail)
        if (!ok) finish()
        return ok
    }
    function finish() {
        if (finished) return
        finished = true; step.stop(); deadline.stop()
        Qt.callLater(function() { browserReporter.finish() })
    }
    FeriaWorld { id: feria; anchors.fill: parent; lifecycleActive: true }
    Component.onCompleted: FeriaAccount.clearHistory()
    Timer { id: deadline; interval: 15000; running: true; onTriggered: { window.check(false,"Navigation timed out"); window.finish() } }
    Timer {
        id: step
        interval: 400; repeat: true; running: true
        onTriggered: {
            if (window.phase === 0) {
                window.home = window.find(feria,"home-focus-variant")
                window.scroll = window.find(feria,"feriaHomeScroll")
                window.sidebar = window.find(feria,"feriaRowIndexSidebar")
                window.tabs = window.find(feria,"feriaTabBar")
                window.dock = window.find(feria,"feriaTabDock")
                if (!window.check(home && scroll && sidebar && tabs && dock,"Shared Feria navigation mounts")) return
                if (!window.check(sidebar.automationRows.indexOf("Featured\nApps") === 0
                    && sidebar.visibleRows.length === feria.shownShelves().length + 2,"Sidebar lists the actual sections in page order")) return
                tabs.forceActiveFocus()
                scroll.contentY = home.boundedContentY(1300)
                window.allY = scroll.contentY
            } else if (window.phase === 1) {
                if (!window.check(home.tabsDocked && dock.visible && tabs.opacity === 0
                    && dock.activeFocus,"Tabs dock under the mast and retain keyboard focus")) return
                var shelf = sidebar.visibleRows[2].target
                shelf.restoreRail(200)
                window.allRailX = shelf.railContentX
                feria.selectLens("watch")
            } else if (window.phase === 2) {
                if (!window.check(feria.lens === "watch" && feria.shownShelves().every(function(row){return row.v === "watch"})
                    && sidebar.visibleRows.length === feria.shownShelves().length + 2,"Watch filters content and sidebar together")) return
                scroll.contentY = home.boundedContentY(1600)
                window.watchY = scroll.contentY
                dock.requestIndex(2,Qt.TabFocusReason)
            } else if (window.phase === 3) {
                if (!window.check(feria.lens === "listen" && feria.shownShelves().every(function(row){return row.v === "listen"}),"Docked tabs use the same activation path")) return
                feria.selectLens("all")
            } else if (window.phase === 4) {
                if (!window.check(Math.abs(scroll.contentY - allY) < 1
                    && Math.abs(sidebar.visibleRows[2].target.railContentX - allRailX) < 1,"All restores vertical and shelf scroll positions: "
                    + JSON.stringify({y:scroll.contentY,expectedY:allY,x:sidebar.visibleRows[2].target.railContentX,expectedX:allRailX,positions:home.lensPositions}))) return
                feria.selectLens("watch")
            } else if (window.phase === 5) {
                if (!window.check(Math.abs(scroll.contentY - watchY) < 1,"Watch restores its own scroll position: "
                    + JSON.stringify({y:scroll.contentY,expectedY:watchY,positions:home.lensPositions}))) return
                feria.shiftLens(2)
            } else if (window.phase === 6) {
                if (!window.check(feria.lens === "read" && feria.shownShelves().every(function(row){return row.v === "read"}),"Bracket navigation follows the tab activation path")) return
                var last = sidebar.visibleRows[sidebar.visibleRows.length - 1]
                sidebar.rowRequested(last.target)
            } else if (window.phase === 7) {
                if (!window.check(sidebar.currentIndex === sidebar.visibleRows.length - 1,"Sidebar activation scrolls to and highlights the requested shelf")) return
                sidebar.collapsed = false
                window.width = 960
            } else if (window.phase === 8) {
                if (!window.check(Math.abs(sidebar.width - sidebar.openWidth) < 1
                    && scroll.x === sidebar.contentLeft && scroll.width > 0
                    && dock.x >= sidebar.x + sidebar.width,"Expanded sidebar reserves content space at a narrow viewport")) return
                sidebar.rowRequested(sidebar.visibleRows[0].target)
            } else if (window.phase === 9) {
                if (!window.check(!home.tabsDocked && !dock.visible && tabs.opacity === 1,"Tabs undock when returning to Featured")) return
                if (smokeProfileRoot) window.contentItem.grabToImage(function(result) { result.saveToFile(smokeProfileRoot + "/navigation.png") })
                feria.selectLens("watch")
                FeriaAccount.setRecording(true)
                FeriaAccount.clearHistory()
                FeriaAccount.beginVisit({pk:"youtube"})
                FeriaAccount.observe({href:"https://youtube.com/watch?v=navigation",title:"Navigation fixture",position:10,duration:100,paused:false,rate:1})
                FeriaAccount.endVisit()
            } else if (window.phase === 10) {
                if (!window.check(sidebar.automationRows.indexOf("Continue") >= 0,"Continue appears in the sidebar when the selected tab has progress")) return
                feria.selectLens("read")
            } else if (window.phase === 11) {
                if (!window.check(sidebar.automationRows.indexOf("Continue") < 0,"An empty Continue section leaves no sidebar entry")) return
                FeriaAccount.beginVisit({pk:"kindle"})
                FeriaAccount.saveReadingPlace("https://read.amazon.com/?asin=NAVIGATION","Reading fixture")
                FeriaAccount.endVisit()
            } else if (window.phase === 12) {
                if (!window.check(sidebar.automationRows.indexOf("Continue") >= 0,"Reading progress updates the Read sidebar")) return
                feria.activeApps = []
            } else if (window.phase === 13) {
                if (!window.check(feria.shownShelves().length === 0 && sidebar.visibleRows.length === 3,"Empty provider shelves leave only the real home sections")) return
                window.finish()
            }
            window.phase++
        }
    }
}
