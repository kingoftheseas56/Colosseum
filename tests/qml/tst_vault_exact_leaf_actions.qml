import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

// These routes belong to the shipped VaultPage, not a mirrored test state machine.
// No VaultLibrary is registered here, which also exercises stale/unresolved file details.
TestCase {
    name: "VaultExactLeafActions"
    when: windowShown

    Window { id: testWindow; width: 1100; height: 820; visible: true }
    Component { id: pageComponent; Colosseum.VaultPage {} }
    property var page: null
    SignalSpy { id: openSpy; signalName: "openMediaRequested" }

    function init() {
        page = pageComponent.createObject(testWindow.contentItem, {
            width: testWindow.width, height: testWindow.height
        })
        verify(page !== null)
        openSpy.target = page
        openSpy.clear()
        wait(40)
    }

    function cleanup() {
        openSpy.target = null
        if (page) page.destroy()
        page = null
        wait(10)
    }

    function row(type) {
        return { key: type === "film" ? "/root/Film" : "/root/Show/S01E02.mkv",
            path: type === "film" ? "/root/Film/film.mkv" : "/root/Show/S01E02.mkv",
            nodeType: type, kind: "video", state: "identified",
            id: type === "film" ? "vault:film" : "vault:episode2",
            displayTitle: type === "film" ? "Film" : "Episode 2", counts: { items: 0 }, away: false }
    }

    function test_details_context_action_opens_exact_key_data() {
        return [ { tag: "episode", type: "episode" }, { tag: "clip", type: "clip" },
                 { tag: "film", type: "film" } ]
    }

    function test_details_context_action_opens_exact_key(data) {
        var selected = row(data.type)
        page.openCardContextMenu(selected, false)
        var menu = findChild(page, "vaultBrowseContextMenu")
        verify(menu !== null)
        tryCompare(menu, "opened", true)
        var action = findChild(page, "vaultBrowseContextDetails")
        verify(action !== null, "The production context menu must expose Details")
        compare(action.visible, true)
        mouseClick(action)
        tryCompare(page, "detailSheetVisible", true)
        compare(page.detailSheetKey, selected.key)
        compare(page.detailSheetRowVaultId, selected.id)
        compare(openSpy.count, 0)
    }

    function test_keyboard_context_details_keeps_focus_in_sheet() {
        page.openCardContextMenu(row("episode"), true)
        var menu = findChild(page, "vaultBrowseContextMenu")
        tryCompare(menu, "opened", true)
        var action = findChild(page, "vaultBrowseContextDetails")
        verify(action !== null)
        testWindow.requestActivate()
        menu.currentIndex = 0
        action.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(action, "activeFocus", true)
        keyClick(Qt.Key_Return)
        tryCompare(page, "detailSheetVisible", true)
        tryCompare(menu, "opened", false)
        var sheet = findChild(page, "vaultBrowseSheet")
        var watched = findChild(sheet, "vaultBrowseSheetMarkWatched")
        wait(30)
        verify(watched.keyboardAction.activeFocus, "Closing the context menu must not steal detail-sheet focus")
    }

    function test_exact_file_sheet_rejects_group_signal_forwarding() {
        page.openDetailSheet(row("episode"))
        var sheet = findChild(page, "vaultBrowseSheet")
        var identify = findChild(page, "vaultIdentifyDialog")
        verify(sheet !== null)
        verify(identify !== null)
        sheet.identifyRequested(page.detailSheetKey)
        compare(identify.opened, false)
        sheet.hideRequested(page.detailSheetKey)
        compare(page.detailSheetVisible, true)
    }

    function test_episode_and_clip_primary_open_still_plays_data() {
        return [ { tag: "episode", type: "episode" }, { tag: "clip", type: "clip" } ]
    }

    function test_episode_and_clip_primary_open_still_plays(data) {
        var selected = row(data.type)
        page.handleBrowseCardOpen(selected)
        compare(page.detailSheetVisible, false)
        compare(openSpy.count, 1)
        compare(openSpy.signalArguments[0][0], selected.path)
    }

    function findDetailsAction(item) {
        if (item.text === "Details" && item.keyboardAction !== undefined) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findDetailsAction(children[i])
            if (found) return found
        }
        return null
    }

    function test_carousel_details_opens_exact_leaf_data() {
        return [ { tag: "episode", type: "episode" }, { tag: "clip", type: "clip" },
                 { tag: "film", type: "film" } ]
    }

    function test_carousel_details_opens_exact_leaf(data) {
        var selected = row(data.type)
        var carousel = findChild(page, "vaultBrowseCarousel")
        verify(carousel !== null)
        // Seed only the production ListView's input model. Its delegates and handlers are
        // the real page's, not a copied carousel implementation.
        carousel.model = [{ __row: selected, art: "" }]
        for (var ancestor = carousel; ancestor && ancestor !== page; ancestor = ancestor.parent)
            ancestor.visible = true
        tryCompare(carousel, "count", 1)
        wait(40)
        var action = findDetailsAction(carousel)
        verify(action !== null)
        compare(action.visible, true)
        mouseClick(action)
        tryCompare(page, "detailSheetVisible", true)
        compare(page.detailSheetKey, selected.key)
        compare(openSpy.count, 0)
    }

    function test_stale_exact_file_detail_does_not_offer_group_actions() {
        page.openDetailSheet(row("episode"))
        tryCompare(page, "detailSheetVisible", true)
        var sheet = findChild(page, "vaultBrowseSheet")
        verify(sheet !== null)
        compare(sheet.found, false)
        compare(findChild(sheet, "vaultBrowseSheetUnidentify").visible, false)
        compare(findChild(sheet, "vaultBrowseSheetHide").visible, false)
    }
}
