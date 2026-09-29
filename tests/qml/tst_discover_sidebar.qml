import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Discover sidebar (spec docs/superpowers/specs/2026-09-29-discover-sidebar-design.md, plan
// .../plans/2026-09-29-discover-sidebar-plan.md). Drives the PRODUCTION DiscoverBrowser with a
// stub adapter: two types, three catalogues in two sections, two filter groups, synchronous pages.
TestCase {
    id: testCase
    name: "DiscoverSidebar"
    when: windowShown

    property var fetches: []
    property bool stubCombines: true

    function makeItems(tag, n) {
        var out = []
        for (var i = 0; i < n; ++i)
            out.push({ id: tag + "-" + i, title: tag + " " + i, type: "movie", poster: "" })
        return out
    }

    property var stubAdapter: ({
        types: function() { return [{ key: "movie", label: "Movie" }, { key: "series", label: "Series" }] },
        catalogs: function(type) {
            return [
                { key: type + "-popular", title: "Popular", section: "Cinemeta", attribution: "Cinemeta" },
                { key: type + "-netflix", title: "Netflix", section: "Streaming Catalogs", attribution: "Streaming Catalogs" },
                { key: type + "-hbo", title: "HBO Max", section: "Streaming Catalogs", attribution: "Streaming Catalogs" }
            ]
        },
        defaultCatalog: function(type) { return type + "-popular" },
        filters: function(type, catalogKey) {
            return [
                { group: "Genres", options: [{ key: "action", label: "Action" }, { key: "drama", label: "Drama" }] },
                { group: "Formats", options: [{ key: "omnibus", label: "Omnibus" }] }
            ]
        },
        resolvePin: function() { return { missing: true } },
        combinesFilters: function(type, catalogKey) { return testCase.stubCombines },
        fetchPage: function(state, cursor, generation, done) {
            testCase.fetches.push(JSON.parse(JSON.stringify(state)))
            done(generation, { items: testCase.makeItems(state.catalogKey, 12), nextCursor: null,
                               exhausted: true, freshness: "live", warning: "" })
            return function() {}
        }
    })

    property var singleTypeAdapter: ({
        types: function() { return [{ key: "book", label: "Books" }] },
        catalogs: function(type) { return [{ key: "book-top", title: "Top", section: "Biblio", attribution: "Biblio" }] },
        defaultCatalog: function(type) { return "book-top" },
        filters: function() { return [] },
        resolvePin: function() { return { missing: true } },
        fetchPage: function(state, cursor, generation, done) {
            done(generation, { items: [], nextCursor: null, exhausted: true, freshness: "", warning: "" })
        }
    })

    Component {
        id: browserComponent
        Colosseum.DiscoverBrowser { }
    }

    Window {
        id: win
        width: 1280
        height: 720
        visible: true

        Colosseum.DiscoverBrowser {
            id: browser
            anchors.fill: parent
            automationPrefix: "stub"
            adapter: testCase.stubAdapter
            fallbackType: "movie"
        }
    }

    function init() {
        testCase.stubCombines = true
        if (browser.currentType !== "movie") browser.selectType("movie")
        if (browser.activeFilterCount > 0) browser.clearFilters()
        testCase.fetches = []
        tryCompare(browser, "automationItemCount", 12, 3000)
    }

    function lastFetch() { return testCase.fetches[testCase.fetches.length - 1] }

    function test_slice0_names_are_world_namespaced() {
        compare(browser.objectName, "stubDiscoverBrowser")
        var wall = null
        function walk(n) { if (!n || wall) return; if (n.objectName === "stubDiscoverWall") { wall = n; return }
                           var k = n.children || []; for (var i = 0; i < k.length; ++i) walk(k[i]) }
        walk(browser)
        verify(wall !== null, "the wall carries the prefixed name")
    }

    function test_slice0_automation_state_tracks_selection() {
        compare(browser.automationType, "movie")
        compare(browser.automationCatalogKey, "movie-popular")
        browser.selectCatalog("movie-netflix")
        compare(browser.automationCatalogKey, "movie-netflix")
        tryCompare(browser, "automationItemCount", 12, 3000)
        compare(browser.automationFilterSummary, "")
        browser._applyFilterKey("Genres" + browser._filterSep + "action")
        compare(browser.automationFilterSummary, "Genres=action")
        browser.clearFilter()
        compare(browser.automationFilterSummary, "")
        browser.selectCatalog("movie-popular")
    }

    function test_slice0_legacy_pickers_are_counted() {
        // Slice 2 removed the catalogue popup; Slice 3 removes the filter picker.
        compare(browser.automationLegacyPickerCount, 1)
    }

    // ── Slice 1: one filter per group; the source decides combine vs replace ──
    function test_slice1_combining_source_keeps_both_groups() {
        testCase.stubCombines = true
        browser.setFilter("Genres", "action")
        browser.setFilter("Formats", "omnibus")
        compare(browser.automationFilterSummary, "Formats=omnibus;Genres=action")
        tryCompare(browser, "automationItemCount", 12, 3000)
        var f = lastFetch().filters
        compare(f.Genres, "action", "the fetch receives both groups")
        compare(f.Formats, "omnibus")
        compare(lastFetch().filterGroup, "Formats", "first active pair for single-filter sources")
    }

    function test_slice1_single_filter_source_replaces() {
        testCase.stubCombines = false
        browser.setFilter("Genres", "action")
        browser.setFilter("Formats", "omnibus")
        compare(browser.automationFilterSummary, "Formats=omnibus")
        tryCompare(browser, "automationItemCount", 12, 3000)
        compare(Object.keys(lastFetch().filters).length, 1)
    }

    function test_slice1_clearing_one_group_keeps_the_other() {
        browser.setFilter("Genres", "drama")
        browser.setFilter("Formats", "omnibus")
        browser.clearFilter("Formats")
        compare(browser.automationFilterSummary, "Genres=drama")
        browser.setFilter("Genres", "")
        compare(browser.automationFilterSummary, "", "an empty key clears that group")
    }

    function test_slice1_each_type_remembers_its_filters() {
        browser.setFilter("Genres", "action")
        browser.selectType("series")
        compare(browser.automationFilterSummary, "", "a fresh type starts unfiltered")
        tryCompare(browser, "automationItemCount", 12, 3000)
        browser.selectType("movie")
        compare(browser.automationFilterSummary, "Genres=action", "the movie filters come back")
    }

    function test_slice1_legacy_picker_still_chooses_one() {
        browser.setFilter("Genres", "action")
        browser._applyFilterKey("Formats" + browser._filterSep + "omnibus")
        compare(browser.automationFilterSummary, "Formats=omnibus", "the old picker replaces the selection")
    }

    // ── Slice 2: the rail — type switch and catalogues grouped by source ──
    function test_slice2_rail_groups_catalogues_by_source_in_order() {
        var rail = findChild(browser, "stubDiscoverSidebar")
        verify(rail !== null, "the rail carries the prefixed name")
        verify(rail.visible)
        var r = rail.rows
        compare(r.length, 5, "two source headers + three catalogues")
        compare(r[0].header, "Cinemeta")
        compare(r[1].key, "movie-popular")
        compare(r[2].header, "Streaming Catalogs")
        compare(r[3].key, "movie-netflix")
        compare(r[4].key, "movie-hbo")
        verify(findChild(browser, "stubDiscoverCatalog_movie_netflix") !== null)
    }

    function test_slice2_clicking_a_catalogue_switches_the_wall_with_one_request() {
        var row = findChild(browser, "stubDiscoverCatalog_movie_hbo")
        verify(row !== null)
        verify(!row.current)
        testCase.fetches = []
        mouseClick(row)
        compare(browser.currentCatalogKey, "movie-hbo")
        compare(testCase.fetches.length, 1, "one page request per switch")
        compare(testCase.fetches[0].catalogKey, "movie-hbo")
        verify(row.current, "the chosen row carries the active state")
        verify(!findChild(browser, "stubDiscoverCatalog_movie_popular").current)
        compare(findChild(browser, "stubDiscoverSummary").text, "HBO Max", "the summary line names the catalogue")
        mouseClick(row)
        compare(testCase.fetches.length, 1, "clicking the current catalogue does not reload it")
        browser.selectCatalog("movie-popular")
    }

    function test_slice2_type_switch_swaps_the_catalogue_list() {
        var seg = findChild(browser, "stubDiscoverType_series")
        verify(seg !== null && seg.visible)
        mouseClick(seg)
        compare(browser.currentType, "series")
        verify(findChild(browser, "stubDiscoverCatalog_series_popular") !== null)
        compare(findChild(browser, "stubDiscoverCatalog_movie_popular"), null)
        verify(findChild(browser, "stubDiscoverType_series").current)
    }

    function test_slice2_single_type_world_hides_the_switch() {
        var one = createTemporaryObject(browserComponent, win,
            { width: 900, height: 600, automationPrefix: "one", adapter: testCase.singleTypeAdapter })
        verify(one !== null)
        var seg = findChild(one, "oneDiscoverType_book")
        verify(seg !== null)
        verify(!seg.visible, "one type: no switch")
        verify(findChild(one, "oneDiscoverCatalog_book_top") !== null)
    }

    function test_slice2_content_sits_right_of_the_rail() {
        var rail = findChild(browser, "stubDiscoverSidebar")
        compare(rail.width, 184)
        compare(browser.contentLeft, 184 + 28)
        var wall = findChild(browser, "stubDiscoverWall")
        verify(wall.mapToItem(browser, 0, 0).x >= browser.contentLeft, "the wall starts right of the rail")
        verify(findChild(browser, "stubDiscoverSummary").mapToItem(browser, 0, 0).x >= browser.contentLeft)
    }
}
