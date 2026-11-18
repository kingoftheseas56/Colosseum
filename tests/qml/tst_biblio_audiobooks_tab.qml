import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

// Biblio Audiobooks tab: tab position, Discover rows in contract order, genre mosaic,
// signals and the no-catalogue state. The fake catalogue follows ROWS-CONTRACT.md.
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
                { genre: "fantasy", count: 120, covers: ["", "", "", ""] },
                { genre: "mystery", count: 80, covers: ["", "", "", ""] } ] },
            genre: function() { return { items: [], page: 0, hasMore: false } }
        }
    }
    Component {
        id: tabComponent
        Colosseum.BiblioAudiobooksTab {
            width: 1280
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
        var t = make({ catalog: fakeCatalog(), active: true })
        compare(t.catalogAvailable, true)
        compare(rowTitles(t), ["New releases", "Fully dramatized", "Narrated by Kate Reading",
                               "Narrated by Michael Kramer", "Narrated by Simon Vance"])
        compare(find(t, "audiobooksUnavailable").visible, false)
    }

    function test_rows_are_fetched_once_across_reactivation() {
        var cat = fakeCatalog()
        var t = make({ active: false })
        t.catalog = cat
        t.active = true
        var n = cat.calls.length
        compare(n, 5)
        t.active = false
        t.active = true
        compare(cat.calls.length, n)
    }

    function test_mosaic_renders_genres() {
        var t = make({ catalog: fakeCatalog(), active: true })
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
        var t = make({ catalog: fakeCatalog(), active: true })
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
        compare(genreSpy.signalArguments[0][0], "mystery")
        var narrator = find(t, "audiobooksRow_narrator_p2")
        narrator.seeAllRequested(narrator.seeAllPin)
        compare(shelfSpy.signalArguments[1][0], "narrator:p2")
    }

    function test_audiobooks_tab_has_no_subtab_switcher() {
        var t = make({ catalog: fakeCatalog(), active: true })
        compare(find(t, "audiobooksSubTabBar"), null)
        compare(find(t, "audiobooksLibraryGrid"), null)
        verify(find(t, "audiobooksGenreMosaic").visible)
    }

    function test_no_catalogue_shows_one_quiet_line_and_nothing_crashes() {
        var t = make({ catalog: null, active: true })
        compare(t.catalogAvailable, false)
        var line = find(t, "audiobooksUnavailable")
        verify(line.visible)
        compare(rowTitles(t).length, 0)
        compare(find(t, "audiobooksGenreMosaic").visible, false)
    }

    function test_inactive_tab_does_not_touch_the_catalogue() {
        var cat = fakeCatalog()
        var t = make({ active: false })
        t.catalog = cat
        compare(cat.calls.length, 0)
        t.active = true
        compare(cat.calls.length, 5)
    }
}
