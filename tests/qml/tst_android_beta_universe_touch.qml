import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

// Real production pages, with their existing offline data seams. No duplicate
// tab/map implementation. Qt desktop touch injection exercises event delivery;
// Android hardware gestures and frame timing still require device verification.
TestCase {
    id: testCase
    name: "AndroidBetaUniverseTouch"
    when: windowShown

    Window { id: window; width: 320; height: 720; visible: true }
    Component { id: sagaComponent; App.SagaUniversePage { touchInput: true; reducedMotion: true; previewProgress: [] } }
    Component { id: shelvesComponent; App.ShelvesUniversePage { touchInput: true; reducedMotion: true; previewProgress: [] } }
    Component { id: atlasComponent; App.OnePieceEastBlueAtlas { anchors.fill: parent; touchInput: true; reducedMotion: true } }
    property var page: null
    property var pendingTouch: null
    property int tabTriggers: 0
    property int previewOpenCount: 0
    property var lastPreviewOpen: null

    function tabCases() { return [{tag: "saga", kind: "saga"}, {tag: "shelves", kind: "shelves"}] }
    function fixtureShelves() {
        const result = []
        for (let i = 0; i < 6; ++i)
            result.push({key: "fixture-" + i, title: "Shelf " + i, tab: "Shelf " + i,
                         kind: "book", runs: [{works: [{id: "book-" + i, title: "Book " + i,
                                                       medium: "book", resolved: false, cover: ""}]}]})
        return result
    }
    function makePage(kind, touchInput) {
        window.width = kind === "atlas" ? 640 : 320
        window.height = 720
        window.requestActivate()
        tryCompare(window, "active", true)
        window.contentItem.forceActiveFocus(Qt.OtherFocusReason)
        mouseMove(window.contentItem, 2, 2)
        // Drain the preceding case's real mouse/focus events before creating badges
        // beneath the window. Otherwise delayed hover can contaminate a touch case.
        wait(25)
        const component = kind === "saga" ? sagaComponent : kind === "shelves" ? shelvesComponent : atlasComponent
        page = component.createObject(window.contentItem, {touchInput: touchInput === undefined ? true : touchInput})
        verify(page !== null)
        if (kind === "saga")
            page.uni = {name: "", blurb: "Offline fixture", banner: "", metaline: "", books: [], films: [], shows: [],
                        comics: {tag: "fixture", tagId: 0}}
        else if (kind === "shelves")
            page.uni = {name: "", category: "", blurb: "Offline fixture", banner: "", wallpaper: "", color: "",
                        shelves: fixtureShelves(), catalogueNotice: ""}
        waitForRendering(page)
        if (kind !== "atlas") {
            tryVerify(function() { return page.tabs.length >= 3 && tabScroller(kind).contentWidth > tabScroller(kind).width })
            for (let i = 0; i < page.tabs.length; ++i) {
                const action = findChild(page, kind + "TabAction_" + i)
                verify(action !== null)
                action.triggered.connect(function() { testCase.tabTriggers++ })
            }
        } else {
            tryVerify(function() { return markers(page).length > 0 })
            page.previewVisibleChanged.connect(function() {
                if (!page.previewVisible) return
                const marker = page.markerForTest(page.selectedMarkerId)
                testCase.lastPreviewOpen = {
                    id: page.selectedMarkerId,
                    hover: marker ? marker.markerHovered : false,
                    focus: marker ? marker.activeFocus : false,
                    focusItem: window.activeFocusItem ? window.activeFocusItem.objectName : "none"
                }
                testCase.previewOpenCount++
            })
        }
        return page
    }
    function tabScroller(kind) { return findChild(page, kind + "TabScroller") }
    function pageScroller(kind) { return findChild(page, kind + "Scroll") }
    function center(item) { return item.mapToItem(window.contentItem, item.width / 2, item.height / 2) }
    function beginTouch(point) {
        pendingTouch = touchEvent(window.contentItem)
        pendingTouch.press(0, window.contentItem, point.x, point.y).commit()
        return pendingTouch
    }
    function releaseTouch(x, y) {
        if (!pendingTouch) return
        pendingTouch.release(0, window.contentItem, x, y).commit()
        pendingTouch = null
    }
    function tap(item) {
        const p = center(item)
        beginTouch(p)
        wait(20)
        releaseTouch(p.x, p.y)
    }
    function cleanup() {
        releaseTouch(-50, -50)
        if (page) { page.visible = false; page.destroy() }
        page = null
        tabTriggers = 0
        previewOpenCount = 0
        lastPreviewOpen = null
        window.contentItem.forceActiveFocus(Qt.OtherFocusReason)
        mouseMove(window.contentItem, 2, 2)
        wait(25)
    }
    function markers(item) {
        let result = []
        if (String(item.objectName).indexOf("eastBlueBadge-") === 0) result.push(item)
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) result = result.concat(markers(children[i]))
        return result
    }

    function test_tab_swipe_does_not_activate_and_last_tab_is_reachable_data() { return tabCases() }
    function test_tab_swipe_does_not_activate_and_last_tab_is_reachable(data) {
        makePage(data.kind)
        const scroller = tabScroller(data.kind)
        const vertical = pageScroller(data.kind)
        verify(scroller.interactive)
        const originalY = vertical.contentY
        const originalActive = page.activeTab
        const last = findChild(page, data.kind + "Tab_" + (page.tabs.length - 1))
        for (let n = 0; n < 8 && scroller.contentX < scroller.contentWidth - scroller.width - 1; ++n) {
            const from = scroller.mapToItem(window.contentItem, scroller.width - 18, scroller.height / 2)
            beginTouch(from)
            for (let step = 1; step <= 4; ++step) {
                pendingTouch.move(0, window.contentItem, from.x - step * (scroller.width - 48) / 4, from.y).commit()
                wait(20)
            }
            releaseTouch(from.x - scroller.width + 48, from.y)
            tryVerify(function() { return !scroller.moving }, 2000)
        }
        verify(scroller.contentX > 0, "Dragging a tab must transfer the grab to the horizontal Flickable")
        compare(tabTriggers, 0, "A swipe over KeyboardAction tabs must not navigate")
        compare(vertical.contentY, originalY)
        compare(page.activeTab, originalActive)
        verify(last.x >= scroller.contentX - 1 && last.x + last.width <= scroller.contentX + scroller.width + 1,
               "The last tab must be wholly reachable by finger scrolling")
        tap(findChild(page, data.kind + "TabAction_" + (page.tabs.length - 1)))
        compare(tabTriggers, 1)
        verify(vertical.contentY > originalY, "A stationary tap still reveals its section")
    }
    function test_active_tab_reveal_and_desktop_geometry_data() { return tabCases() }
    function test_active_tab_reveal_and_desktop_geometry(data) {
        makePage(data.kind)
        // The deliberately small offline Saga has little content after About.
        // A short viewport lets that real section cross the unchanged active-tab
        // threshold before the page reaches its content-end clamp.
        window.height = 360
        waitForRendering(page)
        const scroller = tabScroller(data.kind)
        const bar = findChild(page, data.kind + "Tabs")
        const first = findChild(page, data.kind + "Tab_0")
        const lastIndex = page.tabs.length - 1
        page.revealSection(page.tabs[lastIndex].target)
        tryVerify(function() { return page.activeTab > 0 && scroller.contentX > 0 })
        const selected = findChild(page, data.kind + "Tab_" + page.activeTab)
        tryVerify(function() { return selected.x >= scroller.contentX - 1 && selected.x + selected.width <= scroller.contentX + scroller.width + 1 })
        verify(bar.x + bar.width <= page.width - page.gutter + 1)
        compare(first.height, 36); compare(first.radius, 18); compare(bar.height, 46); compare(bar.radius, 23)
        if (data.kind === "shelves") {
            const count = findChild(page, "shelvesTabCount_0")
            verify(count.visible); compare(String(count.text), "1"); compare(count.font.pixelSize, 11)
        }
        const firstWidth = first.width
        page.touchInput = false
        waitForRendering(page)
        tryCompare(scroller, "contentX", 0)
        verify(!scroller.interactive && !scroller.clip)
        compare(bar.width, scroller.contentWidth + 8)
        compare(first.width, firstWidth); compare(first.height, 36); compare(bar.height, 46)
        window.height = 720
        pageScroller(data.kind).contentY = 0
        waitForRendering(page)
        mouseClick(findChild(page, data.kind + "TabAction_0"))
        compare(tabTriggers, 1, "Desktop mouse activation remains on the original KeyboardAction")
    }
    function test_map_touch_taps_image_and_persists_for_actions() {
        makePage("atlas")
        const marker = markers(page)[0]
        const image = findChild(page, "eastBlueBadgeImage-" + marker.markerId)
        verify(marker.width >= 44 && marker.height >= 44)
        tap(image)
        compare(page.selectedMarkerId, marker.markerId)
        compare(page.previewVisible, true)
        wait(240)
        compare(page.previewVisible, true, "Finger release must leave the preview actions available")
        page.closePreviewSoon()
        wait(240)
        compare(page.previewVisible, true, "Touch previews close by action/Escape, not desktop hover grace")
        tap(findChild(page, "eastBlueClosePreview"))
        compare(page.previewVisible, false)
    }
    function test_map_swipe_does_not_hover_or_click_data() { return [{tag: "base-zoom", zoom: 1}, {tag: "pannable-zoom", zoom: 2}] }
    function test_map_swipe_does_not_hover_or_click(data) {
        makePage("atlas")
        page.zoom = data.zoom
        const viewport = findChild(page, "eastBlueMapViewport")
        const marker = markers(page)[0]
        waitForRendering(page)
        const inContent = marker.mapToItem(viewport.contentItem, marker.width / 2, marker.height / 2)
        viewport.contentX = Math.max(0, Math.min(inContent.x - viewport.width / 2, viewport.contentWidth - viewport.width))
        viewport.contentY = Math.max(0, Math.min(inContent.y - viewport.height / 2, viewport.contentHeight - viewport.height))
        const before = viewport.contentX
        const from = center(marker)
        const dx = viewport.contentX + 80 < viewport.contentWidth - viewport.width ? -80 : 80
        compare(previewOpenCount, 0, "The isolated atlas starts without hover or focus activation")
        beginTouch(from)
        wait(40)
        if (previewOpenCount !== 0) console.log("UNIVERSE_UNEXPECTED_PRESS_PREVIEW", JSON.stringify(lastPreviewOpen))
        compare(previewOpenCount, 0, "Touch press must not trigger hover preview")
        for (let step = 1; step <= 4; ++step) {
            pendingTouch.move(0, window.contentItem, from.x + step * dx / 4, from.y).commit()
            wait(20)
        }
        releaseTouch(from.x + dx, from.y)
        tryVerify(function() { return !viewport.moving }, 2000)
        compare(previewOpenCount, 0, "A swipe must not open a preview before, during or after grab transfer")
        compare(page.previewVisible, false)
        if (data.zoom > 1) verify(Math.abs(viewport.contentX - before) > 10, "The parent map must receive the drag")
    }
    function test_map_desktop_hover_and_keyboard_geometry() {
        makePage("atlas", false)
        const marker = markers(page)[0]
        const diameter = marker.width
        const p = center(marker)
        mouseMove(window.contentItem, p.x, p.y)
        tryCompare(page, "previewVisible", true)
        mouseMove(window.contentItem, 2, 2)
        tryCompare(page, "previewVisible", false)
        marker.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(page, "previewVisible", true)
        page.requestEscape()
        compare(page.previewVisible, false)
        page.touchInput = true
        compare(page.zoom, 1); compare(marker.width, diameter)
        for (let i = 0; i < 6; ++i) mouseClick(findChild(page, "eastBlueZoomIn"))
        compare(page.zoom, 2, "Beta's existing maximum zoom remains unchanged")
        mouseClick(findChild(page, "eastBlueZoomReset"))
        compare(page.zoom, 1)
        page.closePreview()
        mouseClick(findChild(page, "eastBlueBadgeImage-" + marker.markerId))
        compare(page.previewVisible, true, "A physical mouse still activates an Android badge by click")
        compare(page.selectedMarkerId, marker.markerId)
    }
}
