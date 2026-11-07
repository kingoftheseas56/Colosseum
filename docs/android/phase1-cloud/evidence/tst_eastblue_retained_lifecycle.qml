import QtQuick
import QtQuick.Window
import QtTest
import "file:///D:/Colosseum-Android-Touch-20261102/source/qml" as App

// Real production pages, with their existing offline data seams. No duplicate
// tab/map implementation. Qt desktop touch injection exercises event delivery;
// Android hardware gestures and frame timing still require device verification.
TestCase {
    id: testCase
    name: "EastBlueRetainedLifecycle"
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

    function test_retained_press_cancel_and_recovery_data() {
        return [{tag: "area-disable-reenable", mode: "area"},
                {tag: "atlas-hide-show", mode: "visible"},
                {tag: "retained-loader-hide-disable-show-enable", mode: "both"}]
    }
    function test_retained_press_cancel_and_recovery(data) {
        makePage("atlas")
        const marker = markers(page)[0]
        const area = findChild(page, "eastBlueBadgeTouch-" + marker.markerId)
        verify(area !== null)
        const p = center(marker)
        compare(previewOpenCount, 0)
        beginTouch(p)
        wait(30)
        verify(area.pressed, "The actual production MouseArea must own this touch")
        compare(previewOpenCount, 0, "No focus or hover preview on initial press")
        if (data.mode === "area") area.enabled = false
        else { page.visible = false; if (data.mode === "both") page.enabled = false }
        wait(30)
        console.log("LIFECYCLE_DISABLED", data.mode, "pressed", area.pressed, "moved", area.moved)
        if (data.mode === "area") area.enabled = true
        else { page.visible = true; if (data.mode === "both") page.enabled = true }
        wait(30)
        releaseTouch(p.x, p.y)
        wait(40)
        const lateOpened = page.previewVisible
        const lateCount = previewOpenCount
        console.log("LIFECYCLE_LATE_RESULT", data.mode, "previewVisible", lateOpened, "previewOpenCount", lateCount, "pressed", area.pressed, "moved", area.moved)
        page.closePreview()
        wait(25)
        const beforeFresh = previewOpenCount
        tap(marker)
        wait(40)
        const recovery = page.previewVisible && page.selectedMarkerId === marker.markerId && previewOpenCount === beforeFresh + 1
        console.log("LIFECYCLE_FRESH_TAP", data.mode, "recovery", recovery, "previewVisible", page.previewVisible, "previewOpenCount", previewOpenCount)
        verify(recovery, "A fresh tap after the lifecycle interruption still opens one preview")
        compare(lateCount, 0, "A released touch from before the lifecycle interruption must not open a preview")
        compare(lateOpened, false)
    }
}
