import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum
import "../../qml/AudiobookShelfApi.js" as ShelfApi

// Biblio Audiobooks tab: tab position, Discover rows in contract order, genre mosaic,
// signals, Your Library (+ empty state), and the no-catalogue quiet line. The catalogue,
// downloads and Progress are fakes shaped exactly per ROWS-CONTRACT.md.
TestCase {
    id: testCase
    name: "BiblioAudiobooksTab"
    when: windowShown
    width: 1280
    height: 800

    Window {
        id: win
        width: 1280
        height: 800
        visible: true
        color: "#0c0f18"
    }

    function item(n, extra) {
        var it = { workId: "w" + n, recordingId: "r" + n, title: "Book " + n,
                   authors: ["Author " + n], coverUrl: "" }
        for (var k in extra) it[k] = extra[k]
        return it
    }
    function fakeCatalog() {
        return {
            calls: [],
            shelves: function() { return [
                { key: "new-releases", title: "New releases" },
                { key: "dramatized", title: "Fully dramatized" },
                { key: "narrator:p1", title: "Narrated by Kate Reading" },
                { key: "narrator:p2", title: "Narrated by Michael Kramer" },
                { key: "narrator:p3", title: "Narrated by Simon Vance" } ] },
            shelf: function(key, page) {
                this.calls.push(key + "@" + page)
                var n = key === "dramatized" ? 100 : key === "new-releases" ? 0 : 200
                return { items: [item(n + 1, { dramatized: true }), item(n + 2)], page: page, hasMore: true }
            },
            genres: function() { return [
                { genre: "Fantasy", count: 120, covers: ["", "", "", ""] },
                { genre: "Mystery", count: 80, covers: ["", "", "", ""] } ] },
            genre: function() { return { items: [], page: 0, hasMore: false } }
        }
    }
    function fakeDownloads(list) {
        return { downloadedAudiobooks: function() { return list } }
    }
    function fakeProgress(map) {
        return { revision: 0, get: function(kind, id) { return map[kind + ":" + id] || {} } }
    }

    Component {
        id: tabComponent
        Colosseum.BiblioAudiobooksTab {
            width: 1280
            backdrop: null
        }
    }
    SignalSpy { id: itemSpy; signalName: "audiobookRequested" }
    SignalSpy { id: genreSpy; signalName: "audiobookGenreRequested" }
    SignalSpy { id: shelfSpy; signalName: "audiobookShelfRequested" }

    function make(props) {
        var t = createTemporaryObject(tabComponent, win.contentItem, props)
        verify(t !== null)
        itemSpy.target = t; itemSpy.clear()
        genreSpy.target = t; genreSpy.clear()
        shelfSpy.target = t; shelfSpy.clear()
        return t
    }
    function find(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        for (var i = 0; i < root.children.length; i++) {
            var f = find(root.children[i], name)
            if (f) return f
        }
        return null
    }
    function findAll(root, prefix, out) {
        if (!root) return out
        if (String(root.objectName).indexOf(prefix) === 0) out.push(root)
        for (var i = 0; i < root.children.length; i++) findAll(root.children[i], prefix, out)
        return out
    }
    function rowTitles(t) {
        var rows = findAll(t, "audiobooksRow_", [])
        rows.sort(function(a, b) { return a.mapToItem(t, 0, 0).y - b.mapToItem(t, 0, 0).y })
        return rows.map(function(r) { return r.title })
    }

    function test_world_tab_row_places_audiobooks_between_explore_and_library() {
        var world = createTemporaryObject(worldComponent, win.contentItem, { width: 1200, height: 900 })
        verify(world !== null)
        var bar = find(world, "biblioTabBar")
        verify(bar !== null)
        compare(bar.tabModel.map(function(t) { return t.key }),
                ["discover", "explore", "audiobooks", "library"])
        compare(bar.tabModel[2].label, "Audiobooks")
        verify(find(world, "biblioAudiobooksTab") !== null)
        compare(find(world, "biblioAudiobooksTab").visible, false)
    }
    Component { id: worldComponent; Colosseum.BiblioWorld {} }

    function test_discover_rows_in_contract_order_with_shelf_titles() {
        var t = make({ catalog: fakeCatalog(), downloads: fakeDownloads([]), progressStore: fakeProgress({}), active: true })
        compare(t.catalogAvailable, true)
        compare(rowTitles(t), ["New releases", "Fully dramatized", "Narrated by Kate Reading",
                               "Narrated by Michael Kramer", "Narrated by Simon Vance"])
        compare(find(t, "audiobooksUnavailable").visible, false)
    }

    function test_rows_are_fetched_once_and_not_on_tab_switch() {
        var cat = fakeCatalog()
        var t = make({ active: false })
        t.catalog = cat; t.downloads = fakeDownloads([]); t.progressStore = fakeProgress({})
        t.active = true
        var n = cat.calls.length
        compare(n, 5)
        t.subTab = "library"
        t.subTab = "discover"
        t.active = false
        t.active = true
        compare(cat.calls.length, n)
    }

    function test_mosaic_renders_genres() {
        var t = make({ catalog: fakeCatalog(), downloads: fakeDownloads([]), progressStore: fakeProgress({}), active: true })
        var mosaic = find(t, "audiobooksGenreMosaic")
        verify(mosaic.visible)
        compare(mosaic.genres.length, 2)
        compare(mosaic.genres[0].name, "Fantasy")
        compare(mosaic.genres[0].count, 120)
        // the mosaic sits below every row
        var rows = findAll(t, "audiobooksRow_", [])
        var lastRowBottom = 0
        for (var i = 0; i < rows.length; i++)
            lastRowBottom = Math.max(lastRowBottom, rows[i].mapToItem(t, 0, 0).y + rows[i].height)
        verify(mosaic.mapToItem(t, 0, 0).y >= lastRowBottom)
    }

    function test_signals_carry_the_right_payloads() {
        var t = make({ catalog: fakeCatalog(), downloads: fakeDownloads([]), progressStore: fakeProgress({}), active: true })
        var rail = find(t, "audiobooksRow_dramatized")
        verify(rail !== null)
        rail.seeAllRequested(rail.seeAllPin)
        compare(shelfSpy.count, 1)
        compare(shelfSpy.signalArguments[0][0], "dramatized")
        rail.itemRequested(rail.items[1])
        compare(itemSpy.count, 1)
        compare(itemSpy.signalArguments[0][0].recordingId, "r102")
        compare(itemSpy.signalArguments[0][0].title, "Book 102")
        find(t, "audiobooksGenreMosaic").genreClicked(1)
        compare(genreSpy.count, 1)
        compare(genreSpy.signalArguments[0][0], "Mystery")
        var narrator = find(t, "audiobooksRow_narrator_p2")
        narrator.seeAllRequested(narrator.seeAllPin)
        compare(shelfSpy.signalArguments[1][0], "narrator:p2")
    }

    function test_library_lists_downloads_with_progress_and_omits_unknowns() {
        var dl = [
            { id: "rec-a", recordingKey: "rec-a", pairKey: "pk-a", title: "Dune", author: "Frank Herbert", missing: false },
            { id: "rec-b", recordingKey: "rec-b", pairKey: "pk-b", title: "Emma", missing: false },
            { id: "rec-gone", recordingKey: "rec-gone", title: "Gone", missing: true }
        ]
        var prog = { "audiobook:rec-a": { progress: 0.4, cover: "" } }
        var t = make({ catalog: fakeCatalog(), downloads: fakeDownloads(dl), progressStore: fakeProgress(prog), active: true })
        t.subTab = "library"
        compare(t.libraryItems.length, 2)       // the missing download is not offered
        compare(t.libraryItems[0].title, "Dune")
        compare(t.libraryItems[0].progress, 0.4)
        compare(t.libraryItems[1].progress, undefined)   // never listened: absent
        compare(t.libraryItems[1].author, undefined)     // unknown author: absent
        compare(find(t, "audiobooksLibraryEmpty").visible, false)
        verify(find(t, "audiobooksLibraryGrid").visible)
        compare(findAll(t, "audiobooksLibraryProgress", []).filter(function(p) { return p.visible }).length, 1)
        // discover rows are not shown on the library sub-tab
        compare(rowTitles(t).length, 0)
        // opening a downloaded book emits the downloaded entry
        t.audiobookRequested(t.libraryItems[0].raw)
        compare(itemSpy.signalArguments[0][0].recordingKey, "rec-a")
    }

    function test_listeningState_null_is_new() {
        compare(ShelfApi.listeningState(null), "new")
        compare(ShelfApi.listeningState(undefined), "new")
    }

    function test_listeningState_zero_is_new() {
        compare(ShelfApi.listeningState({}), "new")
        compare(ShelfApi.listeningState({ progress: 0, resume: { position: 0, fileIndex: 0 } }), "new")
        // A resume point counts even before a positive overall fraction is saved.
        compare(ShelfApi.listeningState({ progress: 0, resume: { position: 12, fileIndex: 0 } }), "started")
        compare(ShelfApi.listeningState({ progress: 0, resume: { position: 0, fileIndex: 1 } }), "started")
    }

    function test_listeningState_mid_is_started() {
        compare(ShelfApi.listeningState({ progress: 0.4 }), "started")
    }

    function test_listeningState_completed_is_finished() {
        compare(ShelfApi.listeningState({ completed: true, progress: 0.4 }), "finished")
        compare(ShelfApi.listeningState({ completed: true, progress: 0 }), "finished")
    }

    function test_listeningState_0_98_is_finished() {
        compare(ShelfApi.listeningState({ progress: 0.98 }), "finished")
        compare(ShelfApi.listeningState({ progress: 1 }), "finished")
    }

    function test_listeningState_0_97_is_started() {
        compare(ShelfApi.listeningState({ progress: 0.97 }), "started")
    }

    function test_library_tiles_show_state_labels() {
        var dl = [
            { recordingKey: "rec:new", title: "New book" },
            { recordingKey: "rec-started", title: "Started book" },
            { recordingKey: "rec-finished", title: "Finished book" }
        ]
        var prog = {
            "audiobook:rec-started": { progress: 0.4 },
            "audiobook:rec-finished": { completed: true, progress: 1 }
        }
        var t = make({ catalog: fakeCatalog(), downloads: fakeDownloads(dl), progressStore: fakeProgress(prog), active: true })
        t.subTab = "library"
        compare(t.libraryItems.length, 3)
        var keys = ["rec:new", "rec-started", "rec-finished"]
        var labels = ["New", "Started", "Finished"]
        for (var i = 0; i < keys.length; i++) {
            var label = find(t, "abLibraryState_" + keys[i])
            verify(label !== null, "Missing state label for " + keys[i])
            verify(label.visible)
            compare(label.text, labels[i])
        }
        prog["audiobook:rec-started"] = { progress: 0.98 }
        t.refreshLibrary()
        compare(find(t, "abLibraryState_rec-started").text, "Finished")
    }

    function test_library_empty_state_is_one_calm_line() {
        var t = make({ catalog: fakeCatalog(), downloads: fakeDownloads([]), progressStore: fakeProgress({}), active: true })
        t.subTab = "library"
        var line = find(t, "audiobooksLibraryEmpty")
        verify(line.visible)
        compare(line.text, "Audiobooks you download will appear here.")
        compare(find(t, "audiobooksLibraryGrid").visible, false)
    }

    function test_sub_tab_control_switches_sections() {
        var t = make({ catalog: fakeCatalog(), downloads: fakeDownloads([]), progressStore: fakeProgress({}), active: true })
        var bar = find(t, "audiobooksSubTabBar")
        compare(bar.tabModel.map(function(x) { return x.key }), ["discover", "library"])
        bar.tabRequested("library")
        compare(t.subTab, "library")
        verify(find(t, "audiobooksLibraryEmpty").visible)
    }

    function test_no_catalogue_shows_one_quiet_line_and_nothing_crashes() {
        var t = make({ catalog: null, downloads: null, progressStore: null, active: true })
        compare(t.catalogAvailable, false)
        var line = find(t, "audiobooksUnavailable")
        verify(line.visible)
        compare(rowTitles(t).length, 0)
        compare(find(t, "audiobooksGenreMosaic").visible, false)
        t.subTab = "library"
        verify(find(t, "audiobooksLibraryEmpty").visible)
    }

    function test_inactive_tab_does_not_touch_the_catalogue() {
        var cat = fakeCatalog()
        var t = make({ active: false })
        t.catalog = cat; t.downloads = fakeDownloads([]); t.progressStore = fakeProgress({})
        compare(cat.calls.length, 0)
        t.active = true
        compare(cat.calls.length, 5)
    }
}
