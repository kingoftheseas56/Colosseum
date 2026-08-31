// Account Centre Data & privacy presentation-contract regressions.
// Authoritative privacy values are host-owned. QML emits intent only.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "AccountDataPrivacy"
    when: windowShown

    Window {
        id: testWindow
        width: 1240
        height: 900
        visible: true
    }

    Component {
        id: pageComponent
        Account.AccountDataPrivacyPage {}
    }

    Component {
        id: centerComponent
        Account.AccountCenter {}
    }

    Component {
        id: fakePreferencesComponent
        QtObject {
            property bool rememberSearchHistory: true
            property bool keepActivityHistory: true
            property bool syncActivityHistory: true
            property int rememberCalls: 0
            property int keepCalls: 0
            property int syncCalls: 0
            function setRememberSearchHistory(value) {
                rememberCalls++
                rememberSearchHistory = value
            }
            function setKeepActivityHistory(value) {
                keepCalls++
                keepActivityHistory = value
            }
            function setSyncActivityHistory(value) {
                syncCalls++
                syncActivityHistory = value
            }
        }
    }

    property var page: null

    SignalSpy {
        id: searchToggleSpy
        target: testCase.page
        signalName: "rememberSearchHistoryChangeRequested"
    }

    SignalSpy {
        id: activityToggleSpy
        target: testCase.page
        signalName: "keepActivityHistoryChangeRequested"
    }

    SignalSpy {
        id: syncToggleSpy
        target: testCase.page
        signalName: "syncActivityHistoryChangeRequested"
    }

    SignalSpy {
        id: clearSearchSpy
        target: testCase.page
        signalName: "clearSearchHistoryRequested"
    }

    SignalSpy {
        id: clearActivitySpy
        target: testCase.page
        signalName: "clearActivityHistoryRequested"
    }

    SignalSpy {
        id: exportSpy
        target: testCase.page
        signalName: "dataExportRequested"
    }

    SignalSpy {
        id: deleteSpy
        target: testCase.page
        signalName: "accountDeletionFlowRequested"
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

    function findFlickable(root) {
        if (!root)
            return null
        if (root.contentY !== undefined
            && root.contentHeight !== undefined
            && root.boundsBehavior !== undefined) {
            return root
        }
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findFlickable(children[i])
            if (found)
                return found
        }
        return null
    }

    function scrollIntoView(item) {
        var flick = findFlickable(page)
        if (!flick || !item)
            return
        // A prior visibility toggle (e.g. a confirmation box just opened or
        // closed) can leave the Column/implicitHeight chain still settling.
        // Let it finish before measuring, or the computed target lands on
        // stale geometry and the click below misses its target silently.
        wait(32)
        waitForRendering(page)
        var pos = item.mapToItem(flick.contentItem, 0, 0)
        var maxY = Math.max(0, flick.contentHeight - flick.height)
        var target = Math.max(0, Math.min(maxY, pos.y - 40))
        flick.contentY = target
        wait(0)
        waitForRendering(page)
    }

    function clearSpies() {
        searchToggleSpy.clear()
        activityToggleSpy.clear()
        syncToggleSpy.clear()
        clearSearchSpy.clear()
        clearActivitySpy.clear()
        exportSpy.clear()
        deleteSpy.clear()
    }

    function init() {
        page = pageComponent.createObject(testWindow.contentItem, {
            "width": 980,
            "height": 820,
            "active": true,
            "rememberSearchHistory": true,
            "keepActivityHistory": true,
            "syncActivityHistory": true
        })
        verify(page !== null)
        wait(0)
        clearSpies()
    }

    function cleanup() {
        if (page)
            page.destroy()
        page = null
    }

    function test_locked_section_order_is_preserved() {
        var history = byName(page, "privacyHistoryGroup")
        var accountData = byName(page, "privacyAccountDataGroup")
        var map = byName(page, "privacyMap")
        var account = byName(page, "privacyDangerZone")

        verify(history !== null)
        verify(accountData !== null)
        verify(map !== null)
        verify(account !== null)
        verify(history.y < accountData.y)
        verify(accountData.y < map.y)
        verify(map.y < account.y)
    }

    function test_toggles_emit_requested_value_without_optimistic_state() {
        var search = byName(page, "privacySearchHistorySwitch")
        var activity = byName(page, "privacyActivityHistorySwitch")
        var sync = byName(page, "privacySyncHistorySwitch")
        verify(search !== null)
        verify(activity !== null)
        verify(sync !== null)

        mouseClick(search, search.width / 2, search.height / 2)
        compare(searchToggleSpy.count, 1)
        compare(searchToggleSpy.signalArguments[0][0], false)
        compare(page.rememberSearchHistory, true)

        mouseClick(activity, activity.width / 2, activity.height / 2)
        compare(activityToggleSpy.count, 1)
        compare(activityToggleSpy.signalArguments[0][0], false)
        compare(page.keepActivityHistory, true)

        mouseClick(sync, sync.width / 2, sync.height / 2)
        compare(syncToggleSpy.count, 1)
        compare(syncToggleSpy.signalArguments[0][0], false)
        compare(page.syncActivityHistory, true)

        // The three product concepts are independent. Updating one authoritative
        // value must not rewrite either of the other two.
        page.syncActivityHistory = false
        compare(page.keepActivityHistory, true)
        compare(page.rememberSearchHistory, true)
    }

    function test_search_clear_is_confirmation_gated() {
        var open = byName(page, "privacyClearSearchButton")
        var confirmBox = byName(page, "privacyClearSearchConfirm")
        var cancel = byName(page, "privacyClearSearchCancel")
        var confirm = byName(page, "privacyClearSearchCommit")
        verify(open !== null)
        verify(confirmBox !== null)
        verify(cancel !== null)
        verify(confirm !== null)

        mouseClick(open, open.width / 2, open.height / 2)
        wait(0)
        verify(confirmBox.visible)
        compare(clearSearchSpy.count, 0)

        scrollIntoView(cancel)
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        wait(0)
        verify(!confirmBox.visible)
        compare(clearSearchSpy.count, 0)

        scrollIntoView(open)
        mouseClick(open, open.width / 2, open.height / 2)
        wait(0)
        scrollIntoView(confirm)
        mouseClick(confirm, confirm.width / 2, confirm.height / 2)
        compare(clearSearchSpy.count, 1)

        // No backend success has been supplied, so QML does not fabricate one.
        verify(confirmBox.visible)
    }

    function test_activity_clear_is_confirmation_gated_and_separate_from_continue() {
        var open = byName(page, "privacyClearActivityButton")
        var confirmBox = byName(page, "privacyClearActivityConfirm")
        var cancel = byName(page, "privacyClearActivityCancel")
        var confirm = byName(page, "privacyClearActivityCommit")
        verify(open !== null)
        verify(confirmBox !== null)
        verify(cancel !== null)
        verify(confirm !== null)

        mouseClick(open, open.width / 2, open.height / 2)
        wait(0)
        verify(confirmBox.visible)
        compare(clearActivitySpy.count, 0)

        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        compare(clearActivitySpy.count, 0)

        scrollIntoView(open)
        mouseClick(open, open.width / 2, open.height / 2)
        wait(0)
        scrollIntoView(confirm)
        mouseClick(confirm, confirm.width / 2, confirm.height / 2)
        compare(clearActivitySpy.count, 1)

        // There is intentionally no Progress/Collection deletion seam on this page.
        compare(page["clearContinueRequested"], undefined)
        compare(page["clearCollectionRequested"], undefined)
    }

    function test_export_is_request_only() {
        var exportButton = byName(page, "privacyExportButton")
        verify(exportButton !== null)
        mouseClick(exportButton, exportButton.width / 2, exportButton.height / 2)
        compare(exportSpy.count, 1)

        // The presentation layer owns no accepted/succeeded result by itself.
        compare(page["exportSucceeded"], undefined)
    }

    function test_delete_requires_inline_handoff_confirmation() {
        var open = byName(page, "privacyDeleteButton")
        var confirmBox = byName(page, "privacyDeleteConfirm")
        var cancel = byName(page, "privacyDeleteCancel")
        var proceed = byName(page, "privacyDeleteContinue")
        verify(open !== null)
        verify(confirmBox !== null)
        verify(cancel !== null)
        verify(proceed !== null)

        scrollIntoView(open)
        mouseClick(open, open.width / 2, open.height / 2)
        wait(0)
        verify(confirmBox.visible)
        compare(deleteSpy.count, 0)

        scrollIntoView(cancel)
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        wait(0)
        verify(!confirmBox.visible)
        compare(deleteSpy.count, 0)

        scrollIntoView(open)
        mouseClick(open, open.width / 2, open.height / 2)
        wait(0)
        scrollIntoView(proceed)
        mouseClick(proceed, proceed.width / 2, proceed.height / 2)
        compare(deleteSpy.count, 1)

        // This is a future re-auth/final-confirmation handoff, never deletion itself.
        compare(page["accountDeleted"], undefined)
    }

    function test_leaving_page_closes_only_transient_confirmation_state() {
        page.openSearchClearConfirmation()
        page.openActivityClearConfirmation()
        page.openDeleteConfirmation()
        verify(page.searchClearConfirmationOpen)
        verify(page.activityClearConfirmationOpen)
        verify(page.deleteConfirmationOpen)

        page.active = false
        compare(page.searchClearConfirmationOpen, false)
        compare(page.activityClearConfirmationOpen, false)
        compare(page.deleteConfirmationOpen, false)

        // Authoritative values are host state, not transient UI state.
        compare(page.rememberSearchHistory, true)
        compare(page.keepActivityHistory, true)
        compare(page.syncActivityHistory, true)
    }

    function test_narrow_layout_keeps_controls_and_map_reachable() {
        page.width = 760
        wait(0)
        verify(page.compactRows)
        verify(page.compactMap)

        var search = byName(page, "privacySearchHistorySwitch")
        var activity = byName(page, "privacyActivityHistorySwitch")
        var sync = byName(page, "privacySyncHistorySwitch")
        var exportButton = byName(page, "privacyExportButton")
        var deleteButton = byName(page, "privacyDeleteButton")
        var mapGrid = byName(page, "privacyMapGrid")
        verify(search.visible)
        verify(activity.visible)
        verify(sync.visible)
        verify(exportButton.visible)
        verify(deleteButton.visible)
        verify(mapGrid !== null)
        compare(mapGrid.columns, 1)
    }

    function test_account_center_selects_data_privacy_surface() {
        var preferences = fakePreferencesComponent.createObject(testWindow.contentItem)
        var center = centerComponent.createObject(testWindow.contentItem, {
            "width": 1240,
            "height": 900,
            "preferencesStore": preferences
        })
        verify(center !== null)

        center.open("privacy")
        wait(0)
        compare(center.activeSection, "privacy")
        var embedded = byName(center, "accountDataPrivacyPage")
        verify(embedded !== null)
        verify(embedded.visible)

        var search = byName(embedded, "privacySearchHistorySwitch")
        verify(search !== null)
        mouseClick(search, search.width / 2, search.height / 2)
        compare(preferences.rememberCalls, 1)
        compare(embedded.rememberSearchHistory, false)

        var activity = byName(embedded, "privacyActivityHistorySwitch")
        var sync = byName(embedded, "privacySyncHistorySwitch")
        verify(activity !== null)
        verify(sync !== null)
        mouseClick(activity, activity.width / 2, activity.height / 2)
        mouseClick(sync, sync.width / 2, sync.height / 2)
        compare(preferences.keepCalls, 1)
        compare(preferences.syncCalls, 1)
        compare(embedded.keepActivityHistory, false)
        compare(embedded.syncActivityHistory, false)

        center.destroy()
        preferences.destroy()
    }
}
