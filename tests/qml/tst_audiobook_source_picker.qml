import QtQuick
import QtTest
import "../../qml/audiobook" as Audiobook

TestCase {
    id: testCase
    name: "AudiobookSourcesPage"
    width: 800
    height: 600
    visible: true
    when: windowShown

    Component { id: sourcePage; Audiobook.AudiobookSourcesPage {} }

    function make(rows) {
        const page = createTemporaryObject(sourcePage, testCase, {
            width: width, height: height, workTitle: "Dune",
            providerStates: { abb: { state: rows.length ? "results" : "empty", rows: rows },
                librivox: { state: "disabled", rows: [] } }
        })
        verify(page)
        return page
    }

    function visual(item, name) {
        if (item.objectName === name) return item
        for (const child of item.children) {
            const found = visual(child, name)
            if (found) return found
        }
        return null
    }

    function test_evidence_and_explicit_mouse_choice() {
        const offered = { title: "Dune collection", bundle: true, bundleCount: 3, reason: "Different narration",
            fileCount: 1, format: "m4b", size: "1.5 GB", narrators: ["Scott Brick"], recommended: true,
            request: { infoHash: "chosen" } }
        const page = make([{ title: "Unverified", reason: "Unconfirmed" }, offered])
        compare(page.rowDetails(offered), "Narrated by Scott Brick · M4B · 1 file · 1.5 GB · Bundle · 3 books · Recommended · Different narration")
        const selections = []
        page.sourcePicked.connect(row => selections.push(row))
        const unknown = visual(page, "audiobookSourceOption_abb_0")
        verify(unknown && !unknown.enabled)
        mouseClick(unknown)
        compare(selections.length, 0)
        mouseClick(visual(page, "audiobookSourceOption_abb_1"))
        compare(selections.length, 1)
        compare(selections[0].request.infoHash, "chosen")
    }

    function test_back_stays_visible_and_keyboard_scrolls_long_sources_list() {
        const page = make(Array.from({ length: 12 }, (_, index) => ({
            title: "Upload " + index, request: { recordingKey: String(index) }
        })))
        const list = visual(page, "audiobookSourcesScroll")
        const back = visual(page, "audiobookSourcesBack")
        const last = visual(page, "audiobookSourceOption_abb_11")
        verify(list.contentHeight > list.height)
        const backAtTop = back.mapToItem(page, 0, 0).y
        last.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(() => list.contentY > 0)
        compare(back.mapToItem(page, 0, 0).y, backAtTop)
        const selections = []
        page.sourcePicked.connect(row => selections.push(row))
        keyClick(Qt.Key_Return)
        compare(selections.length, 1)
        compare(selections[0].request.recordingKey, "11")
    }

    function test_provider_status_and_back() {
        const page = make([])
        compare(visual(page, "audiobookSourcesStatus_abb").text, "No matches found")
        compare(visual(page, "audiobookSourcesStatus_librivox").text, "Disabled in Extensions")
        const back = visual(page, "audiobookSourcesBack")
        let returns = 0
        page.backRequested.connect(() => ++returns)
        mouseClick(back)
        compare(returns, 1)
        back.focusControl(Qt.TabFocusReason)
        keyClick(Qt.Key_Escape)
        compare(returns, 2)
    }
}
