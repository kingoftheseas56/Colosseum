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
        fetchPage: function(state, cursor, generation, done) {
            testCase.fetches.push(JSON.parse(JSON.stringify(state)))
            done(generation, { items: testCase.makeItems(state.catalogKey, 12), nextCursor: null,
                               exhausted: true, freshness: "live", warning: "" })
            return function() {}
        }
    })

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
        testCase.fetches = []
        tryCompare(browser, "automationItemCount", 12, 3000)
    }

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
        // Slices 2 and 3 remove the catalogue popup and the filter picker; this count proves it.
        compare(browser.automationLegacyPickerCount, 2)
    }
}
