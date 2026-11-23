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
            showTankorent: true,
            providerStates: { abb: { state: rows.length ? "results" : "empty", rows: rows },
                tankorent: { state: "disabled", rows: [] } }
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
        compare(page.rowDetails(offered), "Narrated by Scott Brick · M4B · 1 file · 1.5 GB · Bundle · 3 books · Different narration")
        verify(visual(page, "audiobookSourceOption_abb_1").accessibleDescription.includes("Recommended"))
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

    function test_compact_hero_leaves_room_for_a_complete_release() {
        const page = make([{ title: "Dune", request: { recordingKey: "dune-1" } }])
        page.width = 950
        page.height = 490
        const banner = visual(page, "audiobookSourcesBanner")
        const table = visual(page, "audiobookSourcesTable")
        const list = visual(page, "audiobookSourcesScroll")
        const release = visual(page, "audiobookSourceOption_abb_0")
        verify(banner, "Expected the same artwork banner pattern as the comic Sources page")
        verify(table, "Expected releases to use the glass table")
        verify(banner.height < 300)
        verify(list.height > release.height + 44)
    }

    function test_pending_provider_keeps_the_other_provider_release_selectable() {
        const page = make([])
        page.providerStates = {
            abb: { state: "searching", rows: [] },
            tankorent: { state: "results", rows: [{ title: "Dune", size: "200 MB", seeders: 5,
                request: { recordingKey: "tankorent-source" } }] }
        }
        compare(visual(page, "audiobookSourcesStatus_abb").text, "Searching…")
        const choice = visual(page, "audiobookSourceOption_tankorent_0")
        verify(choice && choice.enabled)
        verify(choice.accessibleDescription.includes("200 MB · 5 seeders"))
        const selected = []
        page.sourcePicked.connect(row => selected.push(row))
        compare(selected.length, 0)
        tryVerify(() => choice.height >= page.releaseHeight
            && choice.mapToItem(page, 0, choice.height).y <= page.height)
        mouseClick(choice)
        compare(selected.length, 1)
        compare(selected[0].request.recordingKey, "tankorent-source")
    }

    function test_audiobookbay_metadata_omits_seeders() {
        const page = make([{
            title: "Catching Fire", format: "mp3", fileCount: 27, size: "636.73 MBs",
            seeders: 0, reason: "Unconfirmed", request: { sourceKind: "abb" }
        }])
        page.showTankorent = false
        const choice = visual(page, "audiobookSourceOption_abb_0")
        compare(choice.accessibleDescription, "MP3 · 27 files · 636.73 MBs · Unconfirmed")
    }

    function test_provider_status_and_back() {
        const page = make([])
        compare(visual(page, "audiobookSourcesStatus_abb").text, "No matches found")
        compare(visual(page, "audiobookSourcesStatus_tankorent").text, "Disabled in Extensions")
        const extensions = visual(page, "audiobookSourcesExtensions")
        let extensionsOpened = 0
        page.extensionsRequested.connect(() => ++extensionsOpened)
        mouseClick(extensions)
        compare(extensionsOpened, 1)
        page.showTankorent = false
        verify(!extensions.visible)
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
