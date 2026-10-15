// Run in feria_browser_harness with an isolated profile and the QtTest QML module.
// The fixture overrides title data only; input still traverses the actual QML handlers.
import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria"

Window {
    id: window
    width: 1280; height: 900; visible: true
    property var parked: null
    property bool completed: false
    property int retries: 0
    property bool retryForced: false
    property var retryTitle: null
    property var fixtureDoors: {
        var result = []
        for (var i = 0; i < 14; ++i)
            result.push({pk:"netflix", level:"s", fallback:true, label:"Fixture " + i})
        return result
    }
    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: FeriaWorld {
            function offers(it) { return window.fixtureDoors }
            function relatedTitles(it) {
                return ["dune2", "dunebook", "phm", "stranger", "dune2", "phm"]
            }
            function availabilityFor(it) { return {status:"error"} }
            function requestAvailability(it, force) {
                if (force === true) {
                    window.retries++
                    window.retryForced = force
                    window.retryTitle = it
                }
            }
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
        id: test
        name: "FeriaDirectionalNavigation"
        when: windowShown
        function check(ok, label) {
            browserReporter.report("feria-directional", !!ok, label)
            verify(ok, label)
        }
        function until(predicate, label) {
            for (var i = 0; i < 50 && !predicate(); ++i) wait(50)
            check(predicate(), label)
        }
        function press(key) { keyClick(key); wait(120) }
        function home() { return window.find(loader.item, "home-focus-variant") }
        function rail() { return window.find(loader.item, "feriaContinueRail") }
        function selectApp(index) {
            loader.item.selectApp(index)
            loader.item.keyboardTarget.forceActiveFocus()
        }
        function visibleHomeFocus() {
            var h = home()
            var r = h.focusedViewportRect()
            return r.height > 0 && r.y >= h.contentTopInset
                    && r.y + r.height <= h.height + 1
        }
        function seedContinue() {
            FeriaAccount.setRecording(true)
            FeriaAccount.clearHistory()
            for (var i = 0; i < 3; ++i) {
                FeriaAccount.beginVisit({pk:"youtube"})
                FeriaAccount.observe({href:"https://youtube.com/watch?v=61c-" + i,
                    title:"61-C fixture " + i, position:20, duration:100, paused:false, rate:1})
                FeriaAccount.endVisit()
            }
        }
        function test_directionalNavigation() {
            until(function() { return loader.item && home() }, "Home created")
            // Keep the physical pointer outside scrolling tiles during keyboard assertions.
            mouseMove(loader.item, 1, 1)
            loader.item.activeApps = ["netflix", "prime", "hbomax", "disney",
                "appletv", "crunchyroll", "youtube", "spotify", "kindle"]
            wait(250)
            var cols = loader.item.appColumns()
            check(cols === home().appColumnCount && cols > 1, "Uses actual responsive GridLayout columns")
            selectApp(cols - 1); press(Qt.Key_Right)
            check(loader.item.focusIndex === cols - 1, "Right cannot wrap to the next app row")
            selectApp(cols); press(Qt.Key_Left)
            check(loader.item.focusIndex === cols, "Left cannot wrap to the previous app row")
            selectApp(0); press(Qt.Key_Down)
            check(loader.item.focusIndex === cols, "Down moves one visual row")
            wait(300)
            check(visibleHomeFocus(), "Selected app is revealed")
            press(Qt.Key_Up)
            check(loader.item.focusIndex === 0, "Up moves one visual row")
            press(Qt.Key_Up)
            check(loader.item.focusArea === "feature", "Top app row Up enters Featured")
            selectApp(loader.item.activeApps.length)
            wait(100); press(Qt.Key_Down)
            check(loader.item.focusArea === "lens", "Final management cell Down enters lens")
            press(Qt.Key_Up)
            check(loader.item.focusArea === "app"
                  && loader.item.focusIndex === loader.item.activeApps.length,
                  "Lens Up retains management-cell selection")

            // Resize and use the newly measured count; no desktop constant.
            window.width = 980
            wait(300)
            cols = loader.item.appColumns()
            check(cols === home().appColumnCount, "Resized column count matches the visual grid")
            selectApp(0); press(Qt.Key_Down)
            check(loader.item.focusIndex === cols, "Vertical movement uses resized columns")
            window.width = 1280
            wait(300)

            seedContinue()
            until(function() { return home().hasContinue && rail().keyboardItems.length === 3 },
                  "Continue fixture is populated")
            home().focusLens(); press(Qt.Key_Down)
            check(loader.item.focusArea === "continue" && rail().activeFocus,
                  "Lens Down transfers real focus to Continue")
            wait(300)
            var continueRect = home().focusedViewportRect()
            check(visibleHomeFocus(), "Continue reveal " + JSON.stringify({
                y:continueRect.y, h:continueRect.height,
                inset:home().contentTopInset, homeHeight:home().height,
                pageY:window.find(loader.item, "feriaHomeScroll").contentY
            }))
            var before = rail().currentIndex
            press(Qt.Key_Right)
            check(rail().currentIndex === before + 1, "Shared Continue controller still owns Right")
            press(Qt.Key_Left)
            check(rail().currentIndex === before, "Shared Continue controller still owns Left")
            press(Qt.Key_Up)
            check(loader.item.focusArea === "lens", "Continue Up enters lens")
            press(Qt.Key_Down); press(Qt.Key_Down)
            check(loader.item.focusArea === "shelf" && loader.item.shelfIndex === 0,
                  "Continue Down enters first shelf")
            press(Qt.Key_Up)
            check(loader.item.focusArea === "continue" && rail().activeFocus,
                  "First shelf Up enters Continue")
            press(Qt.Key_Tab)
            check(!rail().activeFocus, "Tab remains a secondary focus route")
            home().focusContinue()
            press(Qt.Key_Right)
            var identity = rail().keyboardIdentityForIndex(rail().currentIndex)
            window.parked = loader.item.navigationSnapshot()
            check(window.parked.focusArea === "continue"
                  && window.parked.home.continueSelection.identity === identity,
                  "Snapshot records Continue location and selected identity")
            loader.active = false
            wait(80)
            check(loader.item === null, "Loader actually destroys Feria")
            loader.active = true
            until(function() { return loader.item && home() && !home().pendingNavigationPosition
                                      && rail() && rail().activeFocus },
                  "Recreated Feria restores actual Continue focus")
            check(loader.item.focusArea === "continue"
                  && rail().keyboardIdentityForIndex(rail().currentIndex) === identity,
                  "Continue identity and focus survive recreation")
            window.parked = null

            FeriaAccount.clearHistory()
            until(function() { return !home().hasContinue }, "Continue removed")
            home().focusLens(); press(Qt.Key_Down)
            check(loader.item.focusArea === "shelf" && loader.item.shelfIndex === 0,
                  "Without Continue, lens Down enters first shelf")
            press(Qt.Key_Up)
            check(loader.item.focusArea === "lens", "Without Continue, first shelf Up enters lens")

            loader.item.openTitle("stranger")
            wait(200)
            var title = window.find(loader.item, "porticoTitleView")
            var scroll = window.find(loader.item, "feriaTitleScroll")
            check(title && scroll, "Title presentation is present")
            for (var direction = 0; direction < 2; ++direction) {
                for (var j = 0; j < loader.item.titleActionCount() + 2; ++j) {
                    press(direction ? Qt.Key_Up : Qt.Key_Down)
                    var action = title.actionItem(loader.item.titleActionIndex)
                    check(!!action, "Indexed title action exists")
                    var pt = action.mapToItem(scroll, 0, 0)
                    check(pt.y >= 108 && pt.y + action.height <= scroll.height + 1
                          && pt.x >= -1 && pt.x + action.width <= scroll.width + 1,
                          "Repeated arrows reveal title action " + loader.item.titleActionIndex)
                }
            }
            var retry = window.find(loader.item, "feriaAvailabilityRetry")
            check(retry && retry.focusPolicy === Qt.TabFocus && retry.Accessible.focusable,
                  "Retry is Tab/accessibility focusable")
            retry.forceActiveFocus(Qt.TabFocusReason)
            wait(100)
            check(retry.activeFocus, "Retry receives actual keyboard focus")
            var requests = window.retries
            var expectedTitle = title.it
            press(Qt.Key_Return)
            check(window.retries === requests + 1 && window.retryForced
                  && window.retryTitle === expectedTitle,
                  "Enter invokes requestAvailability(it, true) exactly once")
            window.completed = true
        }
        function cleanupTestCase() {
            // Turn any QtTest exception/early exit into the native harness's failure status.
            browserReporter.report("feria-directional", window.completed,
                                   "Directional smoke reached all acceptance assertions")
            // Isolated harness profile only. Never run this smoke against a user's profile.
            FeriaAccount.clearHistory()
            browserReporter.finish()
        }
    }
    Timer {
        id: harnessRunner
        interval: 500
        running: true
        repeat: false
        onTriggered: {
            try { test.test_directionalNavigation() }
            finally { test.cleanupTestCase() }
        }
    }
    Timer {
        interval: 45000; running: true
        onTriggered: {
            browserReporter.report("feria-directional", false, "Directional smoke timed out")
            browserReporter.finish()
        }
    }
}
