import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

// Actual SubtitleMenu, including its existing lists, controls and pending-selection
// routing. Offline rows and recording signals only; no subtitle download or style edit.
TestCase {
    id: testCase
    name: "AndroidSubtitleViewport"
    when: windowShown

    Window { id: window; width: 1000; height: 800; visible: true }
    Component { id: menuComponent; App.SubtitleMenu {} }
    SignalSpy { id: picked; signalName: "trackPicked" }
    SignalSpy { id: online; signalName: "onlinePicked" }
    property var menu: null
    property var panel: null
    property var body: null
    property var pendingTouch: null

    function find(item, predicate) {
        if (predicate(item)) return item
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) {
            const child = find(children[i], predicate)
            if (child) return child
        }
        return null
    }
    function control(label) {
        const found = find(panel, function(item) {
            return item.accessibleName === label && typeof item.clicked === "function"
        })
        verify(found !== null, label)
        return found
    }
    function textButton(label) {
        const found = find(panel, function(item) { return item.text === label && typeof item.clicked === "function" })
        verify(found !== null, label)
        return found
    }
    function label(text) {
        const found = find(panel, function(item) { return item.text === text && item.font !== undefined })
        verify(found !== null, text)
        return found
    }
    function list(search) {
        const found = find(body, function(item) {
            return item.y === (search ? 76 : 126) && item.contentY !== undefined && item.count !== undefined
        })
        verify(found !== null, search ? "Search results ListView" : "Tracks ListView")
        return found
    }
    function rows() {
        const out = []
        for (let i = 0; i < 40; ++i)
            out.push({id: "track-" + i, label: "Subtitle " + i, title: "Subtitle " + i,
                      lang: "en", external: i % 2 === 1, hearingImpaired: i % 3 === 0,
                      forced: i % 4 === 0, url: "https://example.invalid/subtitle-" + i + ".vtt"})
        return out
    }
    function makeMenu(width, height, touchInput) {
        window.width = width; window.height = height
        window.requestActivate()
        window.contentItem.forceActiveFocus(Qt.OtherFocusReason)
        mouseMove(window.contentItem, 2, 2)
        menu = menuComponent.createObject(window.contentItem, {
            overlayParent: window.contentItem, x: width - 100, y: height - 60,
            tracks: rows(), selectedId: "track-0", searchId: "fixture", panelWidth: 760, panelHeight: 520
        })
        verify(menu !== null)
        // Baseline has no gate: still instantiate it and fail the actual bounds check.
        if (menu.touchInput !== undefined) menu.touchInput = touchInput
        menu.panelOpen = true
        waitForRendering(menu)
        panel = find(window.contentItem, function(item) {
            return item.parent === window.contentItem && item.z === 40 && item.radius === 22
        })
        verify(panel !== null, "The production overlay panel exists")
        body = find(panel, function(item) { return item.parent === panel && item.y === 72 })
        verify(body !== null, "The production body follows the unchanged 72px header")
        picked.target = menu; online.target = menu
        picked.clear(); online.clear()
    }
    function inside(item, viewport) {
        const p = item.mapToItem(viewport, 0, 0)
        return p.x >= -1 && p.y >= -1 && p.x + item.width <= viewport.width + 1
            && p.y + item.height <= viewport.height + 1
    }
    function beginTouch(point) {
        pendingTouch = touchEvent(window.contentItem)
        pendingTouch.press(0, window.contentItem, point.x, point.y).commit()
    }
    function releaseTouch(x, y) {
        if (!pendingTouch) return
        pendingTouch.release(0, window.contentItem, x, y).commit()
        pendingTouch = null
    }
    function tap(item) {
        const p = item.mapToItem(window.contentItem, item.width / 2, item.height / 2)
        beginTouch(p); wait(20); releaseTouch(p.x, p.y)
    }
    function dragFrom(point, dx, dy) {
        beginTouch(point)
        for (let n = 1; n <= 5; ++n) {
            pendingTouch.move(0, window.contentItem, point.x + dx * n / 5, point.y + dy * n / 5).commit()
            wait(20)
        }
        releaseTouch(point.x + dx, point.y + dy)
    }
    function settle(flick) {
        tryVerify(function() { return !flick.moving }, 2500)
    }
    function cleanup() {
        releaseTouch(-50, -50)
        picked.target = null; online.target = null
        if (menu) { menu.panelOpen = false; menu.destroy() }
        menu = null; panel = null; body = null
        window.contentItem.forceActiveFocus(Qt.OtherFocusReason)
        mouseMove(window.contentItem, 2, 2)
        wait(25)
    }

    function test_desktop_metrics_and_pending_confirmation_remain() {
        makeMenu(1000, 800, false)
        compare(panel.width, 760); compare(panel.height, 520)
        compare(body.y, 72); compare(body.height, 448)
        compare(control("Subtitle appearance").height, 44)
        compare(textButton("Forced").height, 32)
        const variants = list(false)
        compare(variants.width, 720); compare(variants.y, 126)
        menu.pickTrack("track-1")
        compare(picked.count, 1); compare(menu.pendingId, "track-1")
        compare(menu.panelOpen, true, "A request must still wait for backend confirmation")
        menu.selectedId = "track-1"
        compare(menu.panelOpen, false)
        menu.panelOpen = true; menu.appearanceOpen = true
        wait(180)
        compare(label("Appearance").parent.width, 280)
        compare(variants.width, 440)
    }
    function test_small_landscape_filters_drag_without_activation_and_footer_is_reachable() {
        makeMenu(792, 354, true)
        verify(inside(panel, window.contentItem), "The whole panel must be bounded by the Android viewport")
        verify(inside(control("Close subtitles"), window.contentItem))
        const filter = textButton("Forced")
        verify(inside(filter, body), "Existing filter controls start within the body viewport")
        const p = filter.mapToItem(window.contentItem, filter.width / 2, filter.height / 2)
        dragFrom(p, 0, -150)
        settle(body)
        verify(body.contentY > 0, "A drag on a filter must transfer to the body scroll owner")
        compare(menu.filterMode, "all", "Scrolling a filter must not toggle it on release")
        compare(picked.count, 0); compare(online.count, 0)
        const more = textButton("Find more")
        verify(inside(more, body), "The unchanged footer must be reachable by scrolling")
        tap(more)
        compare(menu.searching, true)
        tryCompare(body, "contentY", 0)
        verify(inside(control("Close subtitles"), window.contentItem))
    }
    function test_nested_lists_keep_their_own_drag_without_selection_data() {
        return [{tag: "tracks", search: false}, {tag: "search", search: true}]
    }
    function test_nested_lists_keep_their_own_drag_without_selection(data) {
        makeMenu(792, 354, true)
        verify(inside(panel, window.contentItem))
        menu.searching = data.search
        menu.searchResults = rows()
        waitForRendering(panel)
        const results = list(data.search)
        const originalBodyY = body.contentY
        const p = results.mapToItem(window.contentItem, 90, 80)
        dragFrom(p, 0, -65)
        settle(results); settle(body)
        verify(results.contentY > 0, "The nested production ListView must receive the row drag")
        compare(body.contentY, originalBodyY)
        compare(picked.count, 0); compare(online.count, 0)
        compare(menu.pending, false)
    }
    function test_appearance_keeps_desktop_drawer_and_all_controls_reachable() {
        makeMenu(600, 354, true)
        verify(inside(panel, window.contentItem))
        tap(control("Subtitle appearance"))
        compare(menu.appearanceOpen, true)
        wait(180)
        const drawer = label("Appearance").parent
        compare(drawer.width, 280)
        verify(inside(label("Appearance"), body))
        const p = body.mapToItem(window.contentItem, body.width - 270, body.height - 15)
        dragFrom(p, 0, -180)
        settle(body)
        const position = find(drawer, function(item) { return item.label === "VERTICAL POSITION" && item.height === 54 })
        verify(position !== null)
        verify(inside(position, body))
        verify(inside(label("Subtitle preview").parent, body))
        compare(picked.count, 0); compare(online.count, 0)
        tap(control("Subtitle appearance"))
        compare(menu.appearanceOpen, false)
        tryCompare(body, "contentX", 0); tryCompare(body, "contentY", 0)
    }
    function test_resize_and_keyboard_focus_reveal_the_existing_controls() {
        makeMenu(1000, 800, true)
        window.width = 600; window.height = 354
        waitForRendering(panel)
        tryVerify(function() { return inside(panel, window.contentItem) })
        const footer = textButton("Find more")
        const key = find(footer, function(item) { return item.activeFocusOnTab && typeof item.triggered === "function" })
        verify(key !== null, "The existing footer KeyboardAction remains the keyboard owner")
        key.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return inside(footer, body) })
        verify(body.contentY > 0)
        compare(picked.count, 0); compare(online.count, 0)
    }
}
