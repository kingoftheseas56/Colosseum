import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria"

Window {
    id: window
    width: 1280; height: 900; visible: true
    property var snapshot: null
    property bool completed: false
    property var indexTrace: []
    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: FeriaWorld { lifecycleActive: true }
        onLoaded: if (window.snapshot) item.restoreNavigationSnapshot(window.snapshot)
    }
    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) {
            var result = find(item.children[i], name)
            if (result) return result
        }
        return null
    }
    TestCase {
        id: probe
        when: windowShown
        function home() { return window.find(loader.item, "home-focus-variant") }
        function page() { return window.find(loader.item, "feriaHomeScroll") }
        function run() {
            wait(700)
            mouseMove(loader.item, 1, 1)
            page().contentY = Math.min(800, page().contentHeight - page().height)
            window.snapshot = loader.item.navigationSnapshot()
            loader.active = false
            wait(80)
            loader.active = true
            wait(100)
            browserReporter.report("review-setup", !!home().pendingNavigationPosition,
                "Home restoration is pending after actual Loader recreation")
            home().snapReveal = true
            loader.item.selectApp(0)
            loader.item.keyboardTarget.forceActiveFocus()
            keyClick(Qt.Key_Up)
            var inputY = page().contentY
            var inputFocus = loader.item.focusArea
            wait(150)
            var rect = home().focusedViewportRect()
            browserReporter.report("review-home-input", rect.y >= home().contentTopInset,
                JSON.stringify({inputY:inputY, laterY:page().contentY,
                    area:inputFocus, pending:!!home().pendingNavigationPosition,
                    rect:home().focusedViewportRect()}))
            wait(1100)
            window.snapshot = null

            loader.item.openSeeAll(loader.item.shownShelves()[0])
            wait(300)
            var grid = window.find(loader.item, "feriaSeeAllGrid")
            grid.currentIndex = Math.min(5, grid.count - 1)
            wait(150)
            var savedIndex = grid.currentIndex
            loader.item.openTitle(grid.items[savedIndex])
            wait(100)
            loader.item.back()
            wait(100)
            grid.forceActiveFocus()
            grid.currentIndexChanged.connect(function() { window.indexTrace = window.indexTrace.concat([grid.currentIndex]) })
            keyClick(Qt.Key_Left)
            var inputIndex = grid.currentIndex
            browserReporter.report("review-setup", window.indexTrace.indexOf(savedIndex - 1) >= 0,
                "Real Left key changes See All selection while return restoration is pending")
            wait(150)
            browserReporter.report("review-see-all-input", grid.currentIndex === savedIndex - 1,
                JSON.stringify({savedIndex:savedIndex, inputIndex:inputIndex,
                    laterIndex:grid.currentIndex,
                    pending:!!window.find(loader.item,"feriaSeeAllView").pendingReturnSnapshot,
                    trace:window.indexTrace}))
            window.completed = true
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
            finally {
                browserReporter.report("review-completion", window.completed, "Reviewer edge probes completed")
                browserReporter.finish()
            }
        }
    }
    Timer {
        interval: 45000; running: true
        onTriggered: { browserReporter.report("review-timeout", false, "Reviewer probes timed out"); browserReporter.finish() }
    }
}
