import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "AndroidModalTouch"
    when: windowShown

    Window { id: testWindow; width: 1000; height: 650; visible: true }
    Component { id: setupComponent; App.ExtensionsSetupSheetAndroid {} }
    Component { id: syncComponent; App.StremioSyncPanel {} }
    QtObject {
        id: syncState
        property string status: "synced"
        property bool linkedAccount: true
        property string accountDisplayName: "Test account"
        property double lastSuccessAt: 0
        property string lastResultSummary: ""
        property int calls: 0
        function connectAccount() { calls++ }
        function switchAccount() { calls++ }
        function disconnectCurrentProfile() { calls++ }
    }
    QtObject { id: actions; function syncStremioNow() { syncState.calls++ } }
    QtObject {
        id: inputMethod
        property bool visible: false
        property rect keyboardRectangle: Qt.rect(0, 350, 1000, 300)
        function hide() { visible = false }
    }
    SignalSpy { id: installs; signalName: "installRequested" }
    SignalSpy { id: closes; signalName: "closeRequested" }
    property var modal: null
    property var pendingTouch: null

    function cases() { return [{ tag: "setup", kind: "setup" }, { tag: "sync", kind: "sync" }] }
    function createModal(data, width, height) {
        testWindow.width = width || 1000
        testWindow.height = height || 650
        testWindow.requestActivate()
        const factory = data.kind === "setup" ? setupComponent : syncComponent
        modal = factory.createObject(testWindow.contentItem,
                                     { width: testWindow.width, height: testWindow.height })
        verify(modal !== null)
        if (modal.touchInput !== undefined) modal.touchInput = true
        if (data.kind === "setup") {
            modal.addon = { title: "Test add-on", setupRequired: true,
                            manifestUrl: "https://example.invalid/manifest.json" }
            installs.target = modal; installs.clear()
        } else {
            modal.syncState = syncState
            modal.actions = actions
            modal.shown = true
        }
        closes.target = modal; closes.clear()
        syncState.calls = 0
        waitForRendering(modal)
        wait(30)
    }
    function panel(data) {
        const named = findChild(modal, data.kind === "setup" ? "extensionsSetupPanel" : "stremioSyncPanel")
        if (named) return named
        // Original setup panel has no objectName: baseline coverage still measures its real geometry.
        const items = modal.children || []
        for (let i = 0; i < items.length; ++i)
            if (items[i].radius === 24) return items[i]
        return null
    }
    function actionByName(item, name) {
        if (!item) return null
        if (item.accessibleName === name) return item
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = actionByName(children[i], name)
            if (found) return found
        }
        return null
    }
    function scroll(data) {
        return findChild(modal, data.kind === "setup" ? "extensionsSetupScroll" : "stremioSyncScroll")
    }
    function beginTouch(item, x, y) {
        const p = item.mapToItem(testWindow.contentItem, x, y)
        pendingTouch = touchEvent(testWindow.contentItem)
        pendingTouch.press(0, testWindow.contentItem, p.x, p.y).commit()
        return p
    }
    function releaseTouch(x, y) {
        if (!pendingTouch) return
        pendingTouch.release(0, testWindow.contentItem, x, y).commit()
        pendingTouch = null
    }
    function drag(item, x, y, dx, dy) {
        const p = beginTouch(item, x, y)
        wait(25)
        for (let i = 1; i <= 4; ++i) {
            pendingTouch.move(0, testWindow.contentItem, p.x + dx * i / 4, p.y + dy * i / 4).commit()
            wait(30)
        }
        releaseTouch(p.x + dx, p.y + dy)
    }
    function tap(item) {
        const p = beginTouch(item, item.width / 2, item.height / 2)
        wait(25)
        releaseTouch(p.x, p.y)
    }
    function cleanup() {
        releaseTouch(-50, -50)
        installs.target = null; closes.target = null
        if (modal) modal.destroy()
        modal = null
        inputMethod.visible = false
        syncState.calls = 0
        syncState.lastResultSummary = ""
        wait(20)
    }

    function test_fitting_layout_is_preserved_data() { return cases() }
    function test_fitting_layout_is_preserved(data) {
        createModal(data)
        const box = panel(data)
        verify(box !== null)
        compare(box.width, data.kind === "setup" ? 520 : 620)
        compare(box.x, (modal.width - box.width) / 2)
        compare(box.y, (modal.height - box.height) / 2)
        compare(box.radius, 24)
        if (data.kind === "setup") {
            const open = actionByName(modal, "Open the setup page in the browser")
            const close = actionByName(modal, "Close setup")
            verify(open !== null && close !== null)
            compare(open.height, 44)
            compare(open.width, 476)
            compare(open.mapToItem(box, 0, 0).x, 22)
            compare(close.width, 34)
            compare(close.height, 34)
        } else {
            compare(box.height, 440)
            const first = findChild(modal, "stremioSyncPrimaryAction")
            const second = findChild(modal, "stremioSwitchAccountAction")
            verify(first !== null && second !== null)
            compare(first.height, 42)
            compare(first.mapToItem(box, 0, 0).x, 34)
            compare(second.mapToItem(box, 0, 0).y, first.mapToItem(box, 0, 0).y)
            compare(second.mapToItem(box, 0, 0).x - first.mapToItem(box, 0, 0).x - first.width, 12)
        }
    }

    function test_short_viewport_scrolls_without_activation_data() { return cases() }
    function test_short_viewport_scrolls_without_activation(data) {
        createModal(data, 1000, 260)
        const box = panel(data)
        verify(box.y >= 0 && box.y + box.height <= modal.height,
               "The modal must fit the available height instead of placing actions outside it")
        const owner = scroll(data)
        verify(owner !== null, "Overflow requires an active finger-scroll viewport")
        verify(owner.contentHeight > owner.height)
        owner.contentY = 0
        const before = owner.contentY
        drag(owner, owner.width / 2, owner.height - 16, 0, -Math.min(120, owner.height - 32))
        tryVerify(function() { return owner.contentY > before + 10 })
        compare(installs.count, 0)
        compare(syncState.calls, 0)
        compare(closes.count, 0)
    }

    function test_setup_keyboard_reveals_focused_field() {
        createModal({ kind: "setup" })
        if (modal.inputMethod === undefined)
            skip("The original component has no injectable input method; baseline overflow is tested directly")
        modal.inputMethod = inputMethod
        const field = findChild(modal, "extensionsSetupLink")
        verify(field !== null)
        field.forceActiveFocus()
        inputMethod.visible = true
        wait(60)
        const position = field.mapToItem(testWindow.contentItem, 0, 0)
        verify(position.y >= 0 && position.y + field.height <= inputMethod.keyboardRectangle.y,
               "The focused link field must remain above the on-screen keyboard")
        compare(installs.count, 0)
    }

    function test_sync_narrow_row_can_pan_without_reflow_or_activation() {
        createModal({ kind: "sync" }, 400, 650)
        const first = findChild(modal, "stremioSyncPrimaryAction")
        const last = findChild(modal, "stremioCancelAction")
        verify(first !== null && last !== null)
        compare(first.mapToItem(modal, 0, 0).y, last.mapToItem(modal, 0, 0).y,
                "Beta's actions remain in one desktop row")
        const before = last.mapToItem(modal, 0, 0).x
        verify(before + last.width > modal.width, "This fixture must exercise the overflowing original Row")
        drag(first, first.width / 2, first.height / 2, -130, 0)
        tryVerify(function() { return last.mapToItem(modal, 0, 0).x < before - 10 },
                  1000, "Dragging the retained action Row must reach its initially off-screen controls")
        const pan = findChild(modal, "stremioSyncActionScroll")
        if (pan) {
            pan.cancelFlick()
            pan.contentX = pan.contentWidth - pan.width
        }
        wait(30)
        const lastPoint = last.mapToItem(modal, 0, 0)
        verify(lastPoint.x >= 0 && lastPoint.x + last.width <= modal.width)
        compare(syncState.calls, 0)
        compare(closes.count, 0)
    }

    function test_sync_focus_reveals_lower_action() {
        syncState.lastResultSummary = Array(12).join("A long status message with details about recently synchronized items. ")
        createModal({ kind: "sync" }, 1000, 260)
        const owner = scroll({ kind: "sync" })
        if (owner) owner.contentY = 0
        const last = findChild(modal, "stremioCancelAction")
        verify(last !== null)
        last.focusInput()
        wait(60)
        const viewport = owner || modal
        const position = last.mapToItem(viewport, 0, 0)
        verify(position.y >= 0 && position.y + last.height <= viewport.height,
               "Keyboard focus must reveal the same retained action row")
    }

    function test_sync_vertical_drag_from_narrow_action_row_scrolls_outer_viewport() {
        syncState.lastResultSummary = Array(12).join("A long synchronization result. ")
        createModal({ kind: "sync" }, 400, 300)
        const owner = scroll({ kind: "sync" })
        if (!owner) skip("The original panel has no viewport; its real overflow is covered separately")
        const first = findChild(modal, "stremioSyncPrimaryAction")
        first.focusInput()
        wait(40)
        const before = owner.contentY
        verify(before > 60)
        drag(first, first.width / 2, first.height / 2, 0, 60)
        tryVerify(function() { return owner.contentY < before - 10 }, 1000,
                  "A vertical gesture over the horizontal action Row belongs to the outer scroller")
        compare(syncState.calls, 0)
        compare(closes.count, 0)
    }

    function test_existing_close_and_confirmation_actions_are_preserved_data() { return cases() }
    function test_existing_close_and_confirmation_actions_are_preserved(data) {
        createModal(data)
        if (data.kind === "setup") {
            tap(actionByName(modal, "Close setup"))
            compare(closes.count, 1)
            compare(installs.count, 0)
        } else {
            modal.confirmation = "disconnect"
            wait(30)
            const close = findChild(modal, "stremioCancelAction")
            tap(close)
            compare(modal.confirmation, "")
            compare(closes.count, 0)
            compare(syncState.calls, 0)
            // Beta's confirmation transition changes both panel height and Row membership.
            waitForRendering(modal)
            const viewport = findChild(modal, "stremioSyncActionScroll")
            verify(viewport !== null)
            tryVerify(function() {
                const point = close.mapToItem(viewport, 0, 0)
                return point.x >= -0.5 && point.x + close.width <= viewport.width + 0.5
            }, 1000, "The focused Close action must stay visible when confirmation changes Row membership")
            tap(close)
            compare(closes.count, 1)
        }
    }
}
