// Account Centre Devices regressions against production QML.
// The page identifies the current device by the server device `id` and waits
// for controller-owned device-list reconciliation after revoke.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "AccountDevicesCentre"

    readonly property string currentId: "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
    readonly property string newerId: "cccccccc-cccc-4ccc-8ccc-cccccccccccc"
    readonly property string olderId: "dddddddd-dddd-4ddd-8ddd-dddddddddddd"

    Window {
        id: testWindow
        width: 1180
        height: 900
        visible: true
    }

    Component {
        id: controllerComponent
        QtObject {
            property string mode: "signedIn"
            property var devices: []
            property int deviceCount: devices.length
            property int refreshCalls: 0
            property int revokeCalls: 0
            property string lastRevokedId: ""

            signal deviceListRefreshSucceeded()
            signal deviceListRefreshFailed(string message)
            signal deviceRevokeSucceeded(string deviceId)
            signal deviceRevokeFailed(string deviceId, string message)
            signal signedOut()
            signal currentDeviceLocked()

            function deviceId() { return testCase.currentId }
            function refreshDevices() { refreshCalls += 1 }
            function revokeDevice(id) {
                revokeCalls += 1
                lastRevokedId = id
            }
        }
    }

    Component {
        id: pageComponent
        Account.AccountDevicesPage {}
    }

    property var controller: null
    property var page: null

    function device(id, label, platform, seen) {
        return {
            "id": id,
            "install_id": "install-" + id,
            "label": label,
            "platform": platform,
            "last_seen_at": seen
        }
    }

    function populatedDevices() {
        return [
            device(olderId, "Older laptop", "Linux", "2026-08-01T10:00:00Z"),
            device(currentId, "Current desktop", "Windows", "2026-08-17T05:55:00Z"),
            device(newerId, "Newer laptop", "macOS", "2026-08-16T10:00:00Z")
        ]
    }

    function byName(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = byName(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function init() {
        controller = controllerComponent.createObject(testWindow, {
            "devices": populatedDevices()
        })
        verify(controller !== null)
        page = pageComponent.createObject(testWindow.contentItem, {
            "width": 980,
            "height": 820,
            "controller": controller,
            "active": true
        })
        verify(page !== null)
        wait(0)
        waitForRendering(page)
        controller.refreshCalls = 0
    }

    function cleanup() {
        if (page)
            page.destroy()
        if (controller)
            controller.destroy()
        page = null
        controller = null
    }

    function test_server_current_device_id_sorts_current_first() {
        compare(page.visibleDeviceCount, 3)
        compare(page.deviceAt(0).id, currentId)
        compare(page.deviceAt(1).id, newerId)
        compare(page.deviceAt(2).id, olderId)

        // install_id is deliberately different. Current identity must be server id.
        verify(page.deviceAt(0).install_id !== currentId)
        verify(page.isCurrentDevice(page.deviceAt(0)))
    }

    function test_current_device_exposes_no_visible_revoke_action() {
        verify(byName(page, "deviceCurrentMark_" + currentId) !== null)
        var currentRevoke = byName(page, "deviceRevoke_" + currentId)
        verify(currentRevoke !== null)
        verify(!currentRevoke.visible)
        verify(byName(page, "deviceRevoke_" + newerId).visible)
    }

    function test_first_revoke_click_only_opens_confirmation() {
        var revoke = byName(page, "deviceRevoke_" + newerId)
        verify(revoke !== null)
        mouseClick(revoke, revoke.width / 2, revoke.height / 2)
        wait(0)
        waitForRendering(page)

        compare(page.revokeTargetId, newerId)
        compare(controller.revokeCalls, 0)
        verify(byName(page, "deviceConfirm_" + newerId).visible)
    }

    function test_cancel_does_not_revoke() {
        page.openRevoke(newerId)
        wait(0)
        waitForRendering(page)
        var cancel = byName(page, "deviceRevokeCancel_" + newerId)
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        compare(controller.revokeCalls, 0)
        compare(page.revokeTargetId, "")
    }

    function test_confirm_uses_server_device_id_once() {
        page.openRevoke(newerId)
        wait(0)
        waitForRendering(page)
        var confirm = byName(page, "deviceRevokeConfirm_" + newerId)
        mouseClick(confirm, confirm.width / 2, confirm.height / 2)

        compare(controller.revokeCalls, 1)
        compare(controller.lastRevokedId, newerId)
        compare(page.revokeRequestPending, true)

        mouseClick(confirm, confirm.width / 2, confirm.height / 2)
        compare(controller.revokeCalls, 1)
    }

    function test_revoke_failure_preserves_row_and_confirmation() {
        page.openRevoke(newerId)
        page.confirmRevoke(newerId)
        verify(page.revokeRequestPending)

        controller.deviceRevokeFailed(newerId, "Device service unavailable.")
        wait(0)

        compare(page.revokeRequestPending, false)
        compare(page.revokeTargetId, newerId)
        verify(byName(page, "deviceRow_" + newerId) !== null)
        verify(byName(page, "deviceConfirm_" + newerId).visible)
        compare(page.operationErrorMessage, "Device service unavailable.")
    }

    function test_authoritative_list_update_removes_revoked_row_and_count() {
        page.openRevoke(newerId)
        page.confirmRevoke(newerId)

        // Real controller order: refreshed devices are reconciled first, then
        // remote deviceRevokeSucceeded is emitted for that refresh result.
        controller.devices = [
            device(currentId, "Current desktop", "Windows", "2026-08-17T05:55:00Z"),
            device(olderId, "Older laptop", "Linux", "2026-08-01T10:00:00Z")
        ]
        wait(0)
        controller.deviceRevokeSucceeded(newerId)
        controller.deviceListRefreshSucceeded()
        wait(0)

        compare(page.visibleDeviceCount, 2)
        compare(page.revokeTargetId, "")
        compare(page.revokeRequestPending, false)
        compare(byName(page, "deviceRow_" + newerId), null)
    }

    function test_reconciled_success_never_removes_a_row_still_in_authoritative_list() {
        page.openRevoke(newerId)
        page.confirmRevoke(newerId)
        verify(page.revokeRequestPending)

        controller.deviceRevokeSucceeded(newerId)
        wait(0)

        compare(page.revokeRequestPending, false)
        compare(page.revokeTargetId, newerId)
        verify(byName(page, "deviceRow_" + newerId) !== null)
        verify(byName(page, "deviceConfirm_" + newerId).visible)
    }

    function test_only_one_confirmation_is_open() {
        page.openRevoke(newerId)
        compare(page.revokeTargetId, newerId)
        page.openRevoke(olderId)
        compare(page.revokeTargetId, olderId)
        verify(!byName(page, "deviceConfirm_" + newerId).visible)
        verify(byName(page, "deviceConfirm_" + olderId).visible)
    }

    function test_refresh_uses_real_controller_path_and_failure_keeps_rows() {
        page.requestRefresh()
        compare(controller.refreshCalls, 1)
        verify(page.refreshPending)

        controller.deviceListRefreshFailed("Could not refresh devices.")
        wait(0)
        compare(page.refreshPending, false)
        compare(page.visibleDeviceCount, 3)
        compare(page.operationErrorMessage, "Could not refresh devices.")
    }

    function test_missing_metadata_degrades_without_fabrication() {
        controller.devices = [
            device(currentId, "", "", "not-a-time"),
            device(newerId, "", "", "")
        ]
        wait(0)

        compare(page.visibleDeviceCount, 2)
        compare(page.deviceLabel(page.deviceAt(0)), "Unnamed device")
        compare(page.deviceMeta(page.deviceAt(0)), "—")
        compare(page.deviceLabel(page.deviceAt(1)), "Unnamed device")
        compare(page.deviceMeta(page.deviceAt(1)), "—")
    }

    function test_narrow_layout_keeps_summary_rows_and_confirmation_reachable() {
        page.width = 760
        page.openRevoke(newerId)
        wait(0)

        verify(page.compact)
        verify(byName(page, "devicesSummary").visible)
        verify(byName(page, "deviceRow_" + newerId).visible)
        verify(byName(page, "deviceRevokeConfirm_" + newerId).visible)
    }

    function test_keyboard_escape_restores_the_same_device_action_focus() {
        testWindow.requestActivate()
        wait(20)
        var scroller = byName(page, "devicesScrollRegion")
        verify(scroller !== null)
        verify(scroller.activeFocusOnTab)

        var revoke = byName(page, "deviceRevoke_" + newerId)
        verify(revoke !== null)
        revoke.forceActiveFocus()
        wait(0)
        verify(revoke.activeFocus)
        compare(page.keyboardFocusedDeviceId, newerId)

        page.openRevoke(newerId)
        wait(0)
        verify(byName(page, "deviceConfirm_" + newerId).visible)

        keyClick(Qt.Key_Escape)
        wait(0)
        wait(0)

        compare(page.revokeTargetId, "")
        verify(revoke.activeFocus)
    }
}
