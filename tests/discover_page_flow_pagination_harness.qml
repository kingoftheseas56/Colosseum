// Real DiscoverBrowser regression: page-flow walls shorter than the outer viewport
// must page from outer scrolling even though the inner GridView cannot scroll.
// Run: qmltestrunner -input tests/discover_page_flow_pagination_harness.qml
import QtQuick
import QtTest
import "../qml" as UI

Item {
    width: 1680; height: 904
    Component {
        id: fixture
        Flickable {
            id: page
            width: 1680; height: 904
            contentHeight: browser.y + browser.height + 24
            property alias browser: browser
            property int calls: 0
            property var replies: []
            QtObject {
                id: adapter
                function types() { return [{key:"manga",label:"Manga"}] }
                function catalogs(type) { return [{key:"popular",title:"Popular",sourceKind:"builtin",section:"Tankoban"}] }
                function filters(type, catalog) { return [] }
                function defaultCatalog(type) { return "popular" }
                function fetchPage(state, cursor, generation, done) {
                    page.calls++
                    page.replies.push(function(result) { done(generation, result) })
                }
            }
            UI.DiscoverBrowser {
                id: browser
                y: 600; width: page.width; height: flowHeight
                pageFlick: page
                adapter: adapter
                active: false
                automationPrefix: "regression"
                posterVisualProfile: "gallery"
            }
        }
    }
    TestCase {
        name: "DiscoverPageFlowPagination"
        when: windowShown
        property var page
        property var browser
        property var wall
        function rows(n) {
            var result = []
            for (var i=0; i<n; i++) result.push({id:String(i),type:"manga",title:"Row "+i,cover:""})
            return result
        }
        function answer(index, count, exhausted) {
            page.replies[index]({items:rows(count),nextCursor:index+1,exhausted:exhausted})
            wait(30)
        }
        function scrollToEnd() { page.contentY = Math.max(0, page.contentHeight - page.height) }
        function scrollAgain() { page.contentY = 0; scrollToEnd(); wait(30) }
        function init() {
            page = createTemporaryObject(fixture, parent)
            verify(page !== null)
            browser = page.browser
            browser.active = true
            compare(page.calls, 1)
            wall = findChild(browser, "regressionDiscoverWall")
            verify(wall !== null)
            answer(0, 24, false)
            verify(wall.contentHeight <= wall.height, "Reproduce non-overflowing inner grid")
            compare(wall.contentY, 0)
            compare(page.calls, 1, "Initial/layout delivery does not auto-fetch")
        }
        function test_outerScrollPagesOnce() {
            scrollToEnd()
            compare(wall.contentY, 0, "Outer scroll leaves the fitted inner grid at zero")
            tryCompare(page, "calls", 2)
            scrollAgain()
            compare(page.calls, 2, "Only one pending page")
            answer(1, 0, false)
            wait(60)
            compare(page.calls, 2, "No empty-page auto-fetch loop")
            scrollAgain()
            compare(page.calls, 3)
            answer(2, 0, true)
            scrollAgain()
            compare(page.calls, 3, "Exhaustion blocks paging")
        }
        function test_outerViewportBoundaryAndTallWall() {
            page.contentY = 1
            wait(30)
            compare(page.calls, 1, "Outer movement before the wall boundary does not page")
            browser.reloadCurrent()
            answer(1, 72, false)
            verify(wall.contentHeight > wall.height, "Tall page-flow wall")
            scrollToEnd()
            tryCompare(page, "calls", 3)
            answer(2, 24, false)
            wait(60)
            compare(page.calls, 3, "Successful append does not auto-chain")
        }
        function test_selfScrollingWallStillPages() {
            browser.pageFlick = null
            browser.height = 600
            browser.reloadCurrent()
            answer(1, 48, false)
            verify(wall.contentHeight > wall.height)
            wall.contentY = wall.contentHeight - wall.height
            tryCompare(page, "calls", 3)
            answer(2, 24, true)
        }
        function test_failedPageRequiresExplicitRetry() {
            scrollToEnd(); tryCompare(page, "calls", 2)
            page.replies[1]({failed:true,retryable:true})
            wait(30)
            scrollAgain()
            compare(page.calls, 2)
            compare(browser.items.length, 24)
            compare(browser.cursor, 1)
            browser.retryPage()
            compare(page.calls, 3)
            answer(2, 24, true)
            compare(browser.items.length, 48)
        }
        function test_inactiveRetainedAndStaleCallback() {
            browser.active = false
            scrollAgain()
            compare(page.calls, 1)
            browser.active = true
            wait(30)
            compare(page.calls, 1, "Settled retained route does not auto-page")
            scrollAgain()
            compare(page.calls, 2)
            browser.active = false
            page.replies[1]({items:rows(24),nextCursor:2,exhausted:false})
            compare(browser.items.length, 24, "Inactive callback fenced")
            browser.active = true
            compare(page.calls, 3, "Canceled pending page resumes once")
            answer(2, 24, true)
        }
        function test_deferredAdmissionGenerationFence() {
            scrollToEnd() // enqueue pagination, then invalidate before the deferred call
            browser.reloadCurrent()
            compare(page.calls, 2)
            answer(1, 24, false)
            compare(page.calls, 2, "Old scroll cannot page the new generation")
        }
        function test_emptyWallDoesNotPageOnScroll() {
            browser.reloadCurrent()
            answer(1, 0, false)
            scrollAgain()
            compare(page.calls, 2)
        }
    }
}
