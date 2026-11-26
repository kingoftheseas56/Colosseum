import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault execution Slice 11 — the founding-ceremony confirmation card. Drives the PRODUCTION
// VaultConfirmCard with a seeded candidate model and proves: a seeded model renders one slice row
// per subtree (plus the vaultCard sliceCount/leftoverCount contract), the leftover line sums the
// honest leftovers, a chip reassignment updates that row's live kind AND is carried in the
// kindOverrides that Shelve emits, and Shelve/Not-now emit their signals.
TestCase {
    name: "VaultConfirmCard"
    when: windowShown

    Window { id: testWindow; width: 820; height: 700; visible: true }

    Component { id: cardComp; Colosseum.VaultConfirmCard {} }
    property var card: null

    SignalSpy { id: shelveSpy; signalName: "shelveRequested" }
    SignalSpy { id: dismissSpy; signalName: "dismissRequested" }

    readonly property var seed: [
        { "subtreePath": "D:/Media/Manga", "groupTitle": "Manga", "kind": "comic",
          "count": 214, "mixed": false, "loose": false, "leftoverCount": 0 },
        { "subtreePath": "D:/Media/TV", "groupTitle": "TV", "kind": "video",
          "count": 87, "mixed": false, "loose": false, "leftoverCount": 0 },
        { "subtreePath": "D:/Media/Mixed", "groupTitle": "Mixed", "kind": "comic",
          "count": 5, "mixed": true, "loose": false, "leftoverCount": 17 }
    ]

    function init() {
        card = cardComp.createObject(testWindow, { model: seed, rootPath: "D:/Media" })
        verify(card !== null)
        shelveSpy.target = card
        dismissSpy.target = card
        wait(40)
    }
    function cleanup() {
        shelveSpy.clear(); dismissSpy.clear()
        shelveSpy.target = null; dismissSpy.target = null
        if (card) card.destroy()
        card = null
        testWindow.height = 700
    }

    function test_seeded_model_renders_one_row_per_slice() {
        compare(card.sliceCount, 3)
        compare(card.leftoverCount, 17)
        verify(findChild(card, "vaultCardRow_0") !== null)
        verify(findChild(card, "vaultCardRow_1") !== null)
        verify(findChild(card, "vaultCardRow_2") !== null)
        verify(findChild(card, "vaultCardShelveAll") !== null)
        verify(findChild(card, "vaultCardNotNow") !== null)
    }

    function test_row_reports_its_census_kind() {
        compare(findChild(card, "vaultCardRow_0").kind, "comic")
        compare(findChild(card, "vaultCardRow_1").kind, "video")
    }

    function test_chip_reassignment_updates_kind_and_is_carried_by_shelve() {
        var chip = findChild(card, "vaultCardRow_2_chip")
        verify(chip !== null)
        mouseClick(chip)                                 // open the kind picker
        wait(20)
        var pickBook = findChild(card, "vaultCardRow_2_pick_book")
        verify(pickBook !== null)
        mouseClick(pickBook)                             // reassign the mixed leaf → Books
        wait(20)
        compare(findChild(card, "vaultCardRow_2").kind, "book") // the row's live kind updated

        mouseClick(findChild(card, "vaultCardShelveAll"))
        compare(shelveSpy.count, 1)
        compare(shelveSpy.signalArguments[0][0]["D:/Media/Mixed"], "book") // override carried
    }

    function test_shelve_emits_once() {
        mouseClick(findChild(card, "vaultCardShelveAll"))
        compare(shelveSpy.count, 1)
        compare(dismissSpy.count, 0)
        compare(Object.keys(shelveSpy.signalArguments[0][0]).length, 0)
    }

    function test_many_shelves_scroll_with_fixed_actions() {
        testWindow.height = 520
        let rows = []
        for (let i = 0; i < 12; ++i)
            rows.push({subtreePath:"C:/Media/Film" + i, groupTitle:"Film " + i,
                       kind:"video", count:1, mixed:false, loose:false, leftoverCount:0})
        card.model = rows
        wait(40)
        const button = findChild(card, "vaultCardShelveAll")
        const initial = button.mapToItem(card, 0, 0)
        verify(initial.y >= 0 && initial.y + button.height <= card.height,
               "Shelving must remain visible with a long folder list")
        const flick = findChild(card, "vaultCardScroll")
        verify(flick !== null)
        const bar = findChild(card, "vaultCardScrollBar")
        verify(bar !== null && bar.visible)
        verify(flick.contentHeight > flick.height)
        mouseWheel(flick, flick.width / 2, flick.height / 2, 0, -360)
        tryVerify(function() { return flick.contentY > 0 })
        flick.cancelFlick()
        testWindow.requestActivate()
        wait(20)
        flick.forceActiveFocus()
        verify(flick.activeFocus)
        keyClick(Qt.Key_End)
        wait(20)
        const last = findChild(card, "vaultCardRow_11")
        const lastPos = last.mapToItem(flick, 0, 0)
        verify(lastPos.y >= -0.5 && lastPos.y + last.height <= flick.height + 0.5,
               "Last row at " + lastPos.y + ", height " + last.height + ", viewport " + flick.height
               + ", contentY " + flick.contentY + ", contentHeight " + flick.contentHeight)
        mouseClick(findChild(card, "vaultCardRow_11_chip"))
        const correction = findChild(card, "vaultCardRow_11_pick_book")
        const correctionPos = correction.mapToItem(card, 0, 0)
        verify(correctionPos.y >= 0 && correctionPos.y + correction.height <= card.height)
        mouseClick(correction)
        compare(last.kind, "book")
        // A correction is optional; the fixed action is still reachable after scrolling.
        card.kindOverrides = ({})
        compare(button.mapToItem(card, 0, 0).y, initial.y)
        mouseClick(button)
        compare(shelveSpy.count, 1)
        compare(Object.keys(shelveSpy.signalArguments[0][0]).length, 0)
    }

    function test_not_now_emits_dismiss() {
        mouseClick(findChild(card, "vaultCardNotNow"))
        compare(dismissSpy.count, 1)
        compare(shelveSpy.count, 0)
    }

    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }
}
