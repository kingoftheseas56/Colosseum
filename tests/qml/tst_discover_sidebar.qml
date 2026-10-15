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
        catalogs: function(type) { return [{ key: "book-top", title: "Top", section: "Biblio", attribution: "Biblio",
                                             sourceName: "Apple Books", addonId: "colosseum.catalogue.applebooks" }] },
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
        // Both pickers stay (Hemanth, 2026-09-30): categories top right, filters in their dropdown.
        compare(browser.automationLegacyPickerCount, 2)
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

    // ── Slice 2: the rail lists the SOURCES; a source's categories live in the picker (top right);
    // a category's genres in the filter dropdown; the type lens stays above the wall ──
    function openRail() {
        var rail = findChild(browser, "stubDiscoverSidebar")
        rail.collapsed = false
        tryCompare(rail, "width", 240, 2000)
        return rail
    }

    function test_slice2_rail_starts_closed_and_the_toggle_opens_it() {
        var fresh = createTemporaryObject(browserComponent, win,
            { width: 1100, height: 700, automationPrefix: "fresh", adapter: testCase.stubAdapter })
        var rail = findChild(fresh, "freshDiscoverSidebar")
        verify(rail.collapsed, "every Discover starts with the rail closed")
        compare(rail.width, 52)
        compare(fresh.contentLeft, 52 + 28)
        verify(findChild(fresh, "freshDiscoverSource_Cinemeta") !== null, "closed: one icon per source")
        mouseClick(findChild(fresh, "freshDiscoverSidebarCollapse"))
        verify(!rail.collapsed)
        tryCompare(rail, "width", 240, 2000)
        compare(fresh.contentLeft, 240 + 28)
        mouseClick(findChild(fresh, "freshDiscoverSidebarCollapse"))
        tryCompare(rail, "width", 52, 2000)
    }

    function test_slice2_rail_lists_sources_only() {
        var rail = openRail()
        compare(rail.automationRows, "Cinemeta
Streaming Catalogs", "one row per source, in order")
        compare(findChild(browser, "stubDiscoverCatalog_movie_netflix"), null, "no category rows in the rail")
        verify(findChild(browser, "stubDiscoverSource_Cinemeta").current, "the current catalogue's source is lit")
    }

    function test_slice2_a_source_click_switches_to_its_first_category() {
        openRail()
        testCase.fetches = []
        mouseClick(findChild(browser, "stubDiscoverSource_Streaming_Catalogs"))
        compare(browser.currentCatalogKey, "movie-netflix")
        compare(testCase.fetches.length, 1, "one page request per switch")
        compare(browser.currentSource, "Streaming Catalogs")
        verify(findChild(browser, "stubDiscoverSource_Streaming_Catalogs").current)
        mouseClick(findChild(browser, "stubDiscoverSource_Streaming_Catalogs"))
        compare(testCase.fetches.length, 1, "clicking the current source does not reload it")
        browser.selectCatalog("movie-popular")
    }

    function test_slice2_the_category_picker_lists_the_current_sources_categories() {
        browser.selectSource("Streaming Catalogs")
        compare(browser.sourceCatalogs.length, 2)
        compare(browser.sourceCatalogs[0].key, "movie-netflix")
        compare(browser.sourceCatalogs[1].key, "movie-hbo")
        verify(browser.categoryPickable)
        mouseClick(findChild(browser, "stubDiscoverCategoryPicker"))
        verify(browser.catalogMenuOpen)
        var hbo = findChild(browser, "stubDiscoverCategory_movie_hbo")
        verify(hbo !== null)
        compare(findChild(browser, "stubDiscoverCategory_movie_popular"), null, "another source's category is not offered")
        mouseClick(hbo)
        compare(browser.currentCatalogKey, "movie-hbo")
        verify(!browser.catalogMenuOpen)
        compare(findChild(browser, "stubDiscoverSummary").text, "HBO Max")
        browser.selectCatalog("movie-popular")
        verify(!browser.categoryPickable, "a one-category source has nothing to pick")
    }

    function test_slice2_the_filter_dropdown_stays() {
        var picker = findChild(browser, "discoverFilterPicker")
        verify(picker !== null && picker.visible, "the catalogue's filters keep their own dropdown")
    }

    function test_slice2_type_lens_above_the_wall_swaps_the_sources() {
        var lens = findChild(browser, "stubDiscoverType_series")
        verify(lens !== null && lens.visible)
        var rail = findChild(browser, "stubDiscoverSidebar")
        var p = lens.parent, inRail = false
        while (p) { if (p === rail) inRail = true; p = p.parent }
        verify(!inRail, "the type selector is not part of the sidebar")
        mouseClick(lens)
        compare(browser.currentType, "series")
        compare(browser.currentCatalogKey, "series-popular")
    }

    // Tankoban/Biblio built-ins name their owning addon (Colosseum Grand Database, Apple Books)
    function test_slice2_a_builtin_names_its_source_addon() {
        var one = createTemporaryObject(browserComponent, win,
            { width: 900, height: 600, automationPrefix: "books", adapter: testCase.singleTypeAdapter })
        compare(one.currentSource, "Apple Books")
        verify(findChild(one, "booksDiscoverSource_Apple_Books") !== null)
    }

    function test_slice2_content_sits_right_of_the_rail() {
        openRail()
        compare(browser.contentLeft, 240 + 28)
        var wall = findChild(browser, "stubDiscoverWall")
        verify(wall.mapToItem(browser, 0, 0).x >= browser.contentLeft, "the wall starts right of the rail")
        verify(findChild(browser, "stubDiscoverType_movie").mapToItem(browser, 0, 0).x >= browser.contentLeft)
    }
}
