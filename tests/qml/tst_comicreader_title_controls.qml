// Comic reader chrome — physical-click regression for Back/Minimize/Fullscreen/Close and the
// command bar, against the REAL production ComicReaderHud.qml (not a hand-rolled harness that
// calls .tapped() directly — that pattern, used by the offscreen qml.exe chrome/overlay harnesses,
// never exercises Qt's actual mouse hit-testing and cannot prove or regress a geometry-overlap bug).
//
// Confirmed defect (fixed alongside this test): `hudNavLeft`/`hudNavRight` — the left/right
// page-turn strips — used to span the FULL chrome height (anchors.top/bottom: parent.top/bottom)
// and were declared AFTER the title bar and command bar, so Qt Quick's declaration-order stacking
// put their 60px-wide MouseAreas on top of: Back's left portion (title bar, left edge), Close
// entirely and most of Fullscreen (title bar, right edge), and the command bar's rightmost entry
// ("Comic order"/"Manga order") — all while paged (navBarsVisible === true). The fix anchors both
// strips between commandStrip.bottom and progressRail.top instead, which makes the overlap
// geometrically impossible rather than merely out-ordered.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/comicreader" as ComicReader

TestCase {
    id: testCase
    name: "ComicReaderTitleControls"

    Window { id: testWindow; width: 900; height: 600; visible: true }

    // The ComicReaderShell-facing seam ComicReaderHud reads/writes (mirrors the FakeShell shape
    // tests/comicreader_chrome_harness.qml already uses for this same component).
    component FakeShell: QtObject {
        property int currentPage: 3
        property int max: 10
        property string layout: "single_page"      // paged, so navBarsVisible is true
        property string order: "ltr"
        readonly property string mode: layout === "paired_pages" ? "double_page" : layout
        readonly property bool rtl: order === "rtl"
        property real stripFraction: 0
        property int zoomPercent: 100
        property bool hasNext: true
        property bool hasPrev: true
        property string curLabel: ""
        property string seriesTitle: "Smoke Series"
        property bool chromeVisible: true
        property bool modalOpen: false
        property var bookmarkPages: []
        property var core: null
        property string activeOverlay: ""
        signal backRequested()
        signal minimizeRequested()
        signal fullscreenRequested()
        signal closeRequested()
        signal openPages()
        signal openLoupe()
        signal openImage()
        signal openLayout()
        signal toggleOrder()
        signal toggleBookmark()
        signal seekRequested(int page)
        signal scrubFractionRequested(real fraction)
        signal prevRequested()
        signal nextRequested()
        signal advancePageRequested()
        signal retreatPageRequested()
    }

    Component { id: fakeShellComp; FakeShell {} }
    Component { id: hudComp; ComicReader.ComicReaderHud {} }

    function byName(root, objName) {
        if (!root) return null
        if (root.objectName === objName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = byName(kids[i], objName)
            if (found) return found
        }
        return null
    }

    // {x, y} of `item`'s centre, in `hud`'s coordinate space — what mouseClick(hud, x, y) needs.
    function centerInHud(hud, item) {
        return item.mapToItem(hud, item.width / 2, item.height / 2)
    }

    property var reader: null
    property var hud: null

    function init() {
        reader = fakeShellComp.createObject(testWindow.contentItem)
        hud = hudComp.createObject(testWindow.contentItem, {
            "width": testWindow.width, "height": testWindow.height, "reader": reader
        })
        wait(0)
        hud.refreshCommandAnchors()
    }

    function cleanup() {
        if (hud) hud.destroy()
        if (reader) reader.destroy()
        hud = null
        reader = null
    }

    // ---- spy scaffolding: connect once per test, read the counts, no shared state across tests ----
    function withSpies(fn) {
        var counts = { back: 0, min: 0, full: 0, close: 0, adv: 0, ret: 0, order: 0 }
        var connBack  = function () { counts.back  += 1 }
        var connMin   = function () { counts.min   += 1 }
        var connFull  = function () { counts.full  += 1 }
        var connClose = function () { counts.close += 1 }
        var connAdv   = function () { counts.adv   += 1 }
        var connRet   = function () { counts.ret   += 1 }
        var connOrder = function () { counts.order += 1 }
        hud.backRequested.connect(connBack)
        hud.minimizeRequested.connect(connMin)
        hud.fullscreenRequested.connect(connFull)
        hud.closeRequested.connect(connClose)
        hud.advancePageRequested.connect(connAdv)
        hud.retreatPageRequested.connect(connRet)
        hud.toggleOrder.connect(connOrder)
        fn(counts)
        hud.backRequested.disconnect(connBack)
        hud.minimizeRequested.disconnect(connMin)
        hud.fullscreenRequested.disconnect(connFull)
        hud.closeRequested.disconnect(connClose)
        hud.advancePageRequested.disconnect(connAdv)
        hud.retreatPageRequested.disconnect(connRet)
        hud.toggleOrder.disconnect(connOrder)
    }

    function test_back_center_is_one_click() {
        withSpies(function (c) {
            var back = byName(hud, "hudBackAction")
            verify(back !== null, "Back control must exist")
            var pt = centerInHud(hud, back)
            mouseClick(hud, pt.x, pt.y)
            compare(c.back, 1, "one click on Back's centre must fire backRequested once")
            compare(c.adv + c.ret, 0, "a Back click must never fire a page-turn signal")
        })
    }

    function test_back_left_edge_previously_stolen_by_navbar() {
        // x=20: inside Back's chevron (BackAction sits at x:14, chevron width 20), and inside the
        // OLD hudNavLeft band (0..60) that used to sit on top of it. The exact coordinate the
        // reported "click several positions until Back works" bug lived at.
        withSpies(function (c) {
            var titleBar = byName(hud, "readerTitleBar")
            verify(titleBar !== null, "title bar must exist")
            var pt = titleBar.mapToItem(hud, 20, titleBar.height / 2)
            mouseClick(hud, pt.x, pt.y)
            compare(c.back, 1, "a click at Back's previously-stolen left edge must fire backRequested")
            compare(c.adv + c.ret, 0, "that click must never leak to the page-turn strip")
        })
    }

    function test_close_center_is_one_click() {
        // The defect Fable/Opus found and I had missed: Close sat ENTIRELY inside hudNavRight's
        // old 60px-wide, full-height band (title bar, right edge) — a plain centre click on Close
        // was captured by the page-turn strip, not the Close button.
        withSpies(function (c) {
            var close = byName(hud, "hudCloseButton")
            verify(close !== null, "Close control must exist")
            var pt = centerInHud(hud, close)
            mouseClick(hud, pt.x, pt.y)
            compare(c.close, 1, "one click on Close's centre must fire closeRequested once")
            compare(c.adv + c.ret, 0, "a Close click must never fire a page-turn signal")
        })
    }

    function test_fullscreen_center_is_one_click() {
        withSpies(function (c) {
            var full = byName(hud, "hudFullscreenButton")
            verify(full !== null, "Fullscreen control must exist")
            var pt = centerInHud(hud, full)
            mouseClick(hud, pt.x, pt.y)
            compare(c.full, 1, "one click on Fullscreen's centre must fire fullscreenRequested once")
            compare(c.adv + c.ret, 0, "a Fullscreen click must never fire a page-turn signal")
        })
    }

    function test_minimize_center_is_one_click() {
        withSpies(function (c) {
            var min = byName(hud, "hudMinimizeButton")
            verify(min !== null, "Minimize control must exist")
            var pt = centerInHud(hud, min)
            mouseClick(hud, pt.x, pt.y)
            compare(c.min, 1, "one click on Minimize's centre must fire minimizeRequested once")
            compare(c.adv + c.ret, 0, "a Minimize click must never fire a page-turn signal")
        })
    }

    function test_command_bar_order_entry_previously_stolen_by_navbar() {
        // "order" is the LAST of the six commands (ComicReaderCommandBar.commands), and the Row
        // that holds all six is right-aligned with a 26px margin against the command strip's own
        // width — well inside hudNavRight's old 60px band. Deliberately NOT using
        // hud.commandAnchorX("order") here: that anchor is the CENTRE of the label, which shifts
        // left as the label's font-metric width grows, and in an offscreen run with no real font
        // available (QFontDatabase warns "Cannot find font directory") that centre can land far
        // enough left to miss the overlap band entirely — a false negative, not proof the bug is
        // gone. Clicking 2px inside the row's own right edge (commandStrip.width - 26 - 2) is
        // margin-only arithmetic, true regardless of font metrics, and it's still within the
        // "order" delegate's body (an icon + label is never under 28px wide) — a robust point
        // that is provably inside the stolen band whenever the geometry bug exists.
        withSpies(function (c) {
            var strip = byName(hud, "readerCommandStrip")
            verify(strip !== null, "command strip must exist")
            var pt = strip.mapToItem(hud, strip.width - 28, strip.height / 2)
            mouseClick(hud, pt.x, pt.y)
            compare(c.order, 1, "a click on the 'order' command must fire toggleOrder once")
            compare(c.adv + c.ret, 0, "an 'order' command click must never fire a page-turn signal")
        })
    }

    function test_left_right_navbars_still_turn_the_page_below_the_chrome() {
        withSpies(function (c) {
            var navLeft = byName(hud, "hudNavLeft")
            verify(navLeft !== null, "left page-turn strip must exist")
            var ptL = centerInHud(hud, navLeft)
            mouseClick(hud, ptL.x, ptL.y)
            compare(c.adv + c.ret, 1, "the left page-turn strip must still turn a page below the chrome")

            var navRight = byName(hud, "hudNavRight")
            verify(navRight !== null, "right page-turn strip must exist")
            var ptR = centerInHud(hud, navRight)
            mouseClick(hud, ptR.x, ptR.y)
            compare(c.adv + c.ret, 2, "the right page-turn strip must still turn a page below the chrome")
        })
    }

    function test_navbars_are_geometrically_below_the_chrome_and_above_the_rail() {
        var navLeft = byName(hud, "hudNavLeft")
        var strip = byName(hud, "readerCommandStrip")
        var rail = byName(hud, "readerProgressRail")
        verify(navLeft !== null && strip !== null && rail !== null,
               "hudNavLeft, readerCommandStrip and readerProgressRail must all be locatable")
        var navTop = navLeft.mapToItem(hud, 0, 0).y
        var stripBottom = strip.mapToItem(hud, 0, strip.height).y
        var navBottom = navLeft.mapToItem(hud, 0, navLeft.height).y
        var railTop = rail.mapToItem(hud, 0, 0).y
        verify(navTop >= stripBottom - 0.5,
               "the page-turn strip must begin at/below the command bar, got navTop=" + navTop
               + " stripBottom=" + stripBottom)
        verify(navBottom <= railTop + 0.5,
               "the page-turn strip must end at/above the gold rail, got navBottom=" + navBottom
               + " railTop=" + railTop)
    }
}
