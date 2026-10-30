import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneComicSources"
    when: windowShown

    Window {
        id: host
        width: 354
        height: 792
        visible: true
        property bool androidHost: true
    }
    QtObject {
        id: api
        property int searches: 0
        function searchTorrentSources() { searches++ }
        function cancelTorrentSourceSearch() {}
    }
    Component { id: pageComponent; App.ComicTorrentSourcesPage {} }

    function byName(root, name) {
        if (root.objectName === name) return root
        for (const child of root.children || []) {
            const found = byName(child, name)
            if (found) return found
        }
        return null
    }

    function makePage(width, height) {
        host.width = width
        host.height = height
        const page = createTemporaryObject(pageComponent, host.contentItem,
            {width: width, height: height, comicsApi: api})
        verify(page !== null)
        page.show({issueId: "local-edition", seriesId: "local-series",
            seriesTitle: "Invincible", editionTitle: "Invincible Compendium #3",
            isbn: "9781534306868", collects: "Invincible #97–144", cover: ""})
        page.applySources("local-edition", [
            {sourceName: "Extra Torrents", title: "Invincible Compendium Three Collected Edition CBZ",
             infoHash: "local-1", magnetUri: "", confidence: "strong", coverage: true,
             uploader: "A long local uploader name", trustTier: 1, evidence: ["ISSUES"], sizeText: "18.8 GB", seeders: 12},
            {sourceName: "Torrents-CSV", title: "Invincible Collected Edition",
             infoHash: "local-2", magnetUri: "", confidence: "possible", coverage: false,
             evidence: [], sizeText: "4.8 GB", seeders: 11}
        ], true)
        wait(40)
        return page
    }

    function test_portrait_results_data() {
        return [{tag: "phone320", width: 320}, {tag: "phone354", width: 354}, {tag:"tablet800",width:800}]
    }

    function test_portrait_results(data) {
        const page = makePage(data.width, 792)
        verify(page.phonePortrait)
        const title = byName(page, "comicTorrentSourcesTitle")
        const query = byName(page, "comicTorrentSourcesQueryInput")
        const search = byName(page, "comicTorrentSourcesSearch")
        const table = byName(page, "comicTorrentSourcesTable")
        const row = byName(page, "comicTorrentSourceRow_0")
        const copy = byName(page, "comicTorrentSourceCopy_0")
        const choose = byName(page, "comicTorrentSourceChoose_0")
        verify(title !== null && query !== null && row !== null && copy !== null)
        verify((data.width>=600 || title.lineCount >= 2) && !title.truncated,
               "Hero: " + JSON.stringify({text: title.text, width: title.width,
                   height: title.height, implicitHeight: title.implicitHeight,
                   lines: title.lineCount, truncated: title.truncated}))
        compare(table.width, data.width - page.pageInset*2)
        verify(query.width >= 240, "The search field uses the available phone width")
        compare(search.height, 48)
        const fieldBottom = query.mapToItem(page, 0, query.height).y
        verify(search.mapToItem(page, 0, 0).y >= fieldBottom,
               "Search action has its own row")
        verify(copy.width >= 200, "Result metadata keeps a readable column")
        const copyTop = copy.mapToItem(row, 0, 0)
        const actionTop = choose.mapToItem(row, 0, 0)
        verify(actionTop.y >= copyTop.y + copy.height)
        verify(actionTop.x >= 0 && actionTop.x + choose.width <= row.width)
        verify(actionTop.y + choose.height <= row.height)
        compare(choose.height, 48)
        for (const child of copy.children) {
            if (child.visible && child.width > 0)
                verify(child.x >= 0 && child.x + child.width <= copy.width + 0.1,
                       "Each metadata block is bounded by the result text column")
        }
        compare(page.visibleRows.length, 2)
        compare(page.selectionState, "results")
    }

    function test_wider_layout_is_retained_data() {
        return [{tag: "wide", width: 1280, height: 900}]
    }

    function test_wider_layout_is_retained(data) {
        const page = makePage(data.width, data.height)
        verify(!page.phonePortrait)
        compare(byName(page, "comicTorrentSourcesQuery").height, 56)
        compare(byName(page, "comicTorrentSourcesTitle").font.pixelSize, 56)
        compare(byName(page, "comicTorrentSourceRow_0").height, 150)
    }
    function test_short_landscape_leaves_room_for_source_rows() {
        const page=makePage(792,354)
        verify(page.phonePortrait)
        verify(byName(page,"comicTorrentSourcesResults").height>=96)
        verify(byName(page,"comicTorrentSourceChoose_0").height>=48)
    }
}
