// Slice E2/E3 — Account Centre Data & privacy clears wired to their real local owners.
// CPP-PORT-CONTRACT.md §16 "Deletion and user-control rules" + the account-centre
// implementation roadmap §9 (Phase E: Data & privacy backend, separate lane).
//
// Drives the REAL composed AccountCenter + embedded AccountDataPrivacyPage through the
// same confirm-gated click path tst_account_data_privacy.qml already proves emits the
// intent signals, and proves the NEW handlers this slice adds:
//   - "Clear search history" confirm invokes searchHistoryStore.clearAllScopes() with
//     EXACTLY the three real remembered scopes ("biblio", "tankoban", "theatre" — verified
//     by grepping every SearchHistoryStore record()/list() call site in qml/: BiblioSearch
//     hardcodes "biblio", SearchSurface derives searchMode.toLowerCase() and Main.qml only
//     ever sets searchMode to "Tankoban"/"Theatre" — no "all"/"home"/"world" scope exists);
//   - "Clear activity history" confirm invokes historyCoordinator.clearAll() exactly once;
//   - the two clears are independent of each other;
//   - neither clear ever reaches a ProgressStore/CollectionStore-shaped seam. Negative
//     control: fakes for both stay untouched, and AccountCenter exposes no
//     progressStore/collectionStore property or clearContinueRequested/
//     clearCollectionRequested signal at all — the same absence-proof idiom
//     tst_account_data_privacy.qml already uses for AccountDataPrivacyPage itself.
//
// searchHistoryStore/historyCoordinator are ordinary AccountCenter properties (default:
// the typeof-guarded native SearchHistory/ProfileConsumptionHistory context properties),
// so this Quick Test runner — which links no native C++ and therefore has neither context
// property — can inject fakes via
// createObject() the same way tst_account_center_composed.qml already injects
// recoveryPresenter/yourColosseumMonthName.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "AccountPrivacyClears"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 900
        visible: true
    }

    Component {
        id: fakeSearchHistoryComponent
        QtObject {
            property bool rememberEnabled: true
            property var clearAllScopesCalls: []
            function clearAllScopes(scopes) { clearAllScopesCalls.push(scopes) }
            // Present only so a stray future call doesn't fail with "not a function";
            // these handlers never call them.
            function clear(scope) { }
            function list(scope) { return [] }
            function record(scope, query) { return [] }
        }
    }

    Component {
        id: fakeHistoryCoordinatorComponent
        QtObject {
            property int clearAllCallCount: 0
            function clearAll() { clearAllCallCount = clearAllCallCount + 1; return true }
        }
    }

    Component {
        id: fakeProgressComponent
        QtObject {
            property int forgetCallCount: 0
            function forget(key) { forgetCallCount = forgetCallCount + 1 }
        }
    }

    Component {
        id: fakeCollectionComponent
        QtObject {
            property int clearCallCount: 0
            function clear(key) { clearCallCount = clearCallCount + 1 }
        }
    }

    Component {
        id: centerComponent
        Account.AccountCenter {}
    }

    property var center: null
    property var fakeSearchHistory: null
    property var fakeHistoryCoordinator: null
    property var fakeProgress: null
    property var fakeCollection: null

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
        fakeSearchHistory = fakeSearchHistoryComponent.createObject(testWindow)
        fakeHistoryCoordinator = fakeHistoryCoordinatorComponent.createObject(testWindow)
        fakeProgress = fakeProgressComponent.createObject(testWindow)
        fakeCollection = fakeCollectionComponent.createObject(testWindow)
        verify(fakeSearchHistory !== null)
        verify(fakeHistoryCoordinator !== null)
        verify(fakeProgress !== null)
        verify(fakeCollection !== null)

        center = centerComponent.createObject(testWindow.contentItem, {
            "width": 1280,
            "height": 900,
            "searchHistoryStore": fakeSearchHistory,
            "historyCoordinator": fakeHistoryCoordinator
        })
        verify(center !== null)
        wait(0)
    }

    function cleanup() {
        if (center)
            center.destroy()
        if (fakeSearchHistory)
            fakeSearchHistory.destroy()
        if (fakeHistoryCoordinator)
            fakeHistoryCoordinator.destroy()
        if (fakeProgress)
            fakeProgress.destroy()
        if (fakeCollection)
            fakeCollection.destroy()
        center = null
        fakeSearchHistory = null
        fakeHistoryCoordinator = null
        fakeProgress = null
        fakeCollection = null
    }

    function openPrivacyPage() {
        center.open("privacy")
        wait(0)
        var page = byName(center, "accountDataPrivacyPage")
        verify(page !== null)
        verify(page.visible)
        return page
    }

    // Mirrors tst_account_data_privacy.qml's own findFlickable/scrollIntoView: a prior
    // confirm box opening (e.g. Search's, left open on purpose — no backend success is
    // wired, so the page never auto-closes it) shifts the Column/implicitHeight chain
    // below it. Without this, a second button's mouseClick computes against stale
    // geometry and silently misses — the exact class of bug that helper was written for.
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

    function scrollIntoView(page, item) {
        var flick = findFlickable(page)
        if (!flick || !item)
            return
        wait(32)
        waitForRendering(page)
        var pos = item.mapToItem(flick.contentItem, 0, 0)
        var maxY = Math.max(0, flick.contentHeight - flick.height)
        var target = Math.max(0, Math.min(maxY, pos.y - 40))
        flick.contentY = target
        wait(0)
        waitForRendering(page)
    }

    function clickConfirm(page, openName, confirmName) {
        var open = byName(page, openName)
        var confirm = byName(page, confirmName)
        verify(open !== null)
        verify(confirm !== null)
        scrollIntoView(page, open)
        mouseClick(open, open.width / 2, open.height / 2)
        wait(0)
        scrollIntoView(page, confirm)
        mouseClick(confirm, confirm.width / 2, confirm.height / 2)
        wait(0)
    }

    function test_remember_search_history_request_updates_authoritative_store() {
        compare(center.privacyRememberSearchHistory, true)
        center.privacyRememberSearchHistoryChangeRequested(false)
        compare(fakeSearchHistory.rememberEnabled, false)
        compare(center.privacyRememberSearchHistory, false)
        center.privacyRememberSearchHistoryChangeRequested(true)
        compare(fakeSearchHistory.rememberEnabled, true)
        compare(center.privacyRememberSearchHistory, true)
    }

    function test_search_history_clear_confirm_click_invokes_aggregate_with_real_scopes() {
        var page = openPrivacyPage()
        clickConfirm(page, "privacyClearSearchButton", "privacyClearSearchCommit")

        compare(fakeSearchHistory.clearAllScopesCalls.length, 1)
        compare(JSON.stringify(fakeSearchHistory.clearAllScopesCalls[0]),
                JSON.stringify(["biblio", "tankoban", "theatre"]))

        // Independence: the search clear never touches the history coordinator.
        compare(fakeHistoryCoordinator.clearAllCallCount, 0)
    }

    function test_activity_history_clear_confirm_click_invokes_clear_all_exactly_once() {
        var page = openPrivacyPage()
        clickConfirm(page, "privacyClearActivityButton", "privacyClearActivityCommit")

        compare(fakeHistoryCoordinator.clearAllCallCount, 1)

        // Independence: the activity clear never touches the search-history store.
        compare(fakeSearchHistory.clearAllScopesCalls.length, 0)
    }

    // Negative control: no ProgressStore/CollectionStore seam exists to invoke in the
    // first place — AccountCenter declares no progressStore/collectionStore property and
    // no clearContinueRequested/clearCollectionRequested signal (same absence-proof idiom
    // tst_account_data_privacy.qml already uses on AccountDataPrivacyPage). The fakes are
    // wired in but structurally unreachable, and stay untouched through BOTH real clears.
    function test_no_progress_or_collection_seam_exists_and_fakes_stay_untouched() {
        compare(center["progressStore"], undefined)
        compare(center["collectionStore"], undefined)
        compare(center["clearContinueRequested"], undefined)
        compare(center["clearCollectionRequested"], undefined)

        var page = openPrivacyPage()
        clickConfirm(page, "privacyClearSearchButton", "privacyClearSearchCommit")
        clickConfirm(page, "privacyClearActivityButton", "privacyClearActivityCommit")

        // Sanity: the scenario above actually fired both real clears...
        compare(fakeSearchHistory.clearAllScopesCalls.length, 1)
        compare(fakeHistoryCoordinator.clearAllCallCount, 1)
        // ...yet the never-wired Progress/Collection fakes recorded nothing.
        compare(fakeProgress.forgetCallCount, 0)
        compare(fakeCollection.clearCallCount, 0)
    }

    function test_default_stores_are_null_without_a_native_context_property() {
        // Without an explicit override, this Quick Test process has neither a real
        // SearchHistoryStore nor ProfileConsumptionHistory context property — the same
        // typeof-guarded default colosseumEarliestMonthKey already relies on for ProfileActivity.
        var bare = centerComponent.createObject(testWindow.contentItem, {
            "width": 1280,
            "height": 900
        })
        verify(bare !== null)
        compare(bare.searchHistoryStore, null)
        compare(bare.historyCoordinator, null)

        // Triggering both requests with no store wired must not throw.
        bare.privacyClearSearchHistoryRequested()
        bare.privacyClearActivityHistoryRequested()
        wait(0)

        bare.destroy()
    }
}
