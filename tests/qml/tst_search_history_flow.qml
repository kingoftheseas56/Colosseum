import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "SearchHistoryFlow"

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    QtObject {
        id: history
        property var entries: ({})
        signal changed(string scope)

        function scopeKey(scope) { return String(scope).trim().toLowerCase() }
        function list(scope) { return (entries[scopeKey(scope)] || []).slice(0) }
        function record(scope, query) {
            var key = scopeKey(scope)
            var q = String(query).trim()
            if (q.length < 2)
                return list(key)
            var folded = q.toLowerCase()
            var next = list(key).filter(function(item) { return item.toLowerCase() !== folded })
            next.unshift(q)
            entries[key] = next.slice(0, 6)
            changed(key)
            return list(key)
        }
        function remove(scope, query) {
            var key = scopeKey(scope)
            var folded = String(query).trim().toLowerCase()
            entries[key] = list(key).filter(function(item) { return item.toLowerCase() !== folded })
            changed(key)
            return list(key)
        }
        function clear(scope) {
            delete entries[scopeKey(scope)]
            changed(scopeKey(scope))
        }
    }

    Component {
        id: otherHistoryComponent
        QtObject {
            property var entries: ({ "tankoban": ["Fresh"] })
            signal changed(string scope)
            function list(scope) { return (entries[String(scope).trim().toLowerCase()] || []).slice(0) }
            function record(scope, query) { return list(scope) }
            function remove(scope, query) { return list(scope) }
        }
    }

    Component { id: genericSearch; Colosseum.SearchSurface {} }
    Component { id: biblioSearch; Colosseum.BiblioSearch {} }

    function createGeneric(dispatcher) {
        return genericSearch.createObject(testWindow.contentItem, {
            historyStore: history,
            searchDispatcher: dispatcher,
            searchMode: "Tankoban",
            width: testWindow.width,
            height: testWindow.height
        })
    }

    function findItem(root, objectName) {
        if (root.objectName === objectName)
            return root
        for (var i = 0; i < root.children.length; ++i) {
            var found = findItem(root.children[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function test_history_owner_rebind_refreshes_recent_immediately() {
        history.clear("tankoban")
        history.record("tankoban", "Old")
        var surface = createGeneric(function(mode, query, callback) { callback([]) })
        compare(surface.recent, ["Old"])
        var other = otherHistoryComponent.createObject(testWindow)
        verify(other !== null)
        surface.historyStore = other
        wait(0)
        compare(surface.recent, ["Fresh"])
        surface.destroy()
        other.destroy()
    }

    function test_provider_failure_is_not_reported_as_zero_results() {
        var surface = createGeneric(function(mode, query, callback) { callback([], "provider unavailable") })
        surface.fillAndSearch("Batman")
        wait(0)
        compare(surface.searchError, "provider unavailable")
        surface.destroy()
    }

    function test_neverReturningProviderRecordsAndReopens() {
        history.clear("tankoban")
        var neverReturns = function(mode, query, callback) { }
        var first = createGeneric(neverReturns)
        first.fillAndSearch("Offline Batman")
        compare(history.list("tankoban"), ["Offline Batman"])
        first.destroy()

        var recreated = createGeneric(neverReturns)
        compare(recreated.recent, ["Offline Batman"])
        recreated.destroy()
    }

    function test_zeroResultBiblioSearchRecordsAndRemovalSurvivesRecreation() {
        history.clear("biblio")
        var zeroResults = function(query, callback) { callback([]) }
        var first = biblioSearch.createObject(testWindow.contentItem, {
            historyStore: history,
            searchDispatcher: zeroResults,
            audioSearchDispatcher: zeroResults,
            width: testWindow.width,
            height: testWindow.height
        })
        first.fillAndSearch("Dune")
        compare(history.list("biblio"), ["Dune"])
        first.removeRecent("DUNE")
        compare(history.list("biblio"), [])
        first.destroy()

        var recreated = biblioSearch.createObject(testWindow.contentItem, {
            historyStore: history,
            searchDispatcher: zeroResults,
            audioSearchDispatcher: zeroResults,
            width: testWindow.width,
            height: testWindow.height
        })
        compare(recreated.recent, [])
        recreated.destroy()
    }

    function test_biblioRecentChipBodyAndRemoveHaveIndependentClickTargets() {
        history.clear("biblio")
        history.record("biblio", "Dune")
        var dispatches = 0
        var surface = biblioSearch.createObject(testWindow.contentItem, {
            historyStore: history,
            searchDispatcher: function(query, callback) { dispatches += 1; callback([]) },
            audioSearchDispatcher: function(query, callback) { callback([]) },
            width: testWindow.width,
            height: testWindow.height
        })
        wait(0)
        var removeTarget = findItem(surface, "biblioRecentRemove")
        verify(removeTarget !== null)
        mouseClick(removeTarget, removeTarget.width / 2, removeTarget.height / 2)
        compare(history.list("biblio"), [])
        compare(dispatches, 0)
        surface.destroy()

        history.record("biblio", "Dune")
        surface = biblioSearch.createObject(testWindow.contentItem, {
            historyStore: history,
            searchDispatcher: function(query, callback) { dispatches += 1; callback([]) },
            audioSearchDispatcher: function(query, callback) { callback([]) },
            width: testWindow.width,
            height: testWindow.height
        })
        wait(0)
        var bodyTarget = findItem(surface, "biblioRecentBody")
        verify(bodyTarget !== null)
        mouseClick(bodyTarget, bodyTarget.width / 2, bodyTarget.height / 2)
        compare(dispatches, 1)
        compare(history.list("biblio"), ["Dune"])
        surface.destroy()
    }

    function test_genericSearchCancelsSupersededRequestAndRejectsLateResult() {
        var callbacks = [], cancellations = 0
        var dispatcher = function(mode, query, callback) {
            callbacks.push(callback)
            return function() { cancellations += 1 }
        }
        var surface = createGeneric(dispatcher)
        surface.fillAndSearch("Batman")
        compare(callbacks.length, 1)
        surface.fillAndSearch("Dune")
        compare(cancellations, 1)
        callbacks[0]([{ title: "stale", data: { title: "stale" } }])
        compare(surface.results.length, 0)
        callbacks[1]([{ title: "fresh", data: { title: "fresh" } }])
        compare(surface.results.length, 1)
        compare(surface.results[0].title, "fresh")
        surface.destroy()
    }

    function test_searchDestructionCancelsBookAndAudioRequests() {
        var bookCallback, audioCallback, bookCancellations = 0, audioCancellations = 0
        var surface = biblioSearch.createObject(testWindow.contentItem, {
            historyStore: history,
            searchDispatcher: function(query, callback) {
                bookCallback = callback
                return function() { bookCancellations += 1 }
            },
            audioSearchDispatcher: function(query, callback) {
                audioCallback = callback
                return function() { audioCancellations += 1 }
            },
            width: testWindow.width,
            height: testWindow.height
        })
        surface.fillAndSearch("Dune")
        verify(bookCallback !== undefined)
        verify(audioCallback !== undefined)
        surface.destroy()
        compare(bookCancellations, 1)
        compare(audioCancellations, 1)
    }

    function test_runtime_observability_names_and_counts_match_state() {
        history.clear("tankoban")
        history.record("tankoban", "Recent")
        var surface = createGeneric(function(mode, query, callback) {
            callback([{ title: "Batman", data: { id: "batman" } }], "")
        })
        compare(surface.objectName, "tankobanSearchSurface")
        compare(surface.recentCount, 1)
        surface.fillAndSearch("Batman")
        compare(surface.resultCount, 1)
        compare(surface.showingProviderError, false)
        compare(surface.showingNoResults, false)
        surface.destroy()

        var biblio = biblioSearch.createObject(testWindow.contentItem, {
            historyStore: history,
            searchDispatcher: function(query, callback) { callback([{ title: "Dune" }], "") },
            audioSearchDispatcher: function(query, callback) { callback([{ title: "Dune Audio" }], "") },
            width: testWindow.width,
            height: testWindow.height
        })
        compare(biblio.objectName, "biblioSearchSurface")
        verify(findItem(biblio, "biblioSearchInput") !== null)
        biblio.fillAndSearch("Dune")
        compare(biblio.bookResultCount, 1)
        compare(biblio.audioResultCount, 1)
        compare(biblio.showingProviderError, false)
        compare(biblio.showingNoResults, false)
        biblio.destroy()
    }

    function test_provider_failure_and_legitimate_empty_are_distinct() {
        var failed = createGeneric(function(mode, query, callback) {
            callback([], "provider unavailable")
        })
        failed.fillAndSearch("Batman")
        compare(failed.searchError, "provider unavailable")
        compare(failed.showingProviderError, true)
        compare(failed.showingNoResults, false)
        failed.destroy()

        var empty = createGeneric(function(mode, query, callback) { callback([], "") })
        empty.fillAndSearch("Batman")
        compare(empty.searchError, "")
        compare(empty.showingProviderError, false)
        compare(empty.showingNoResults, true)
        empty.destroy()
    }

    function test_biblio_partial_failure_keeps_healthy_lane_and_error_truth() {
        var biblio = biblioSearch.createObject(testWindow.contentItem, {
            historyStore: history,
            searchDispatcher: function(query, callback) { callback([{ title: "Dune" }], "") },
            audioSearchDispatcher: function(query, callback) { callback([], "provider unavailable") },
            width: testWindow.width,
            height: testWindow.height
        })
        biblio.fillAndSearch("Dune")
        compare(biblio.bookResultCount, 1)
        compare(biblio.audioResultCount, 0)
        compare(biblio.searchError, "provider unavailable")
        compare(biblio.showingProviderError, true)
        compare(biblio.showingNoResults, false)
        biblio.destroy()
    }

    function test_null_history_owner_is_safe_and_recovers() {
        history.clear("tankoban")
        history.record("tankoban", "Back Again")
        var surface = createGeneric(function(mode, query, callback) { callback([]) })
        surface.historyStore = null
        surface.loadRecent()
        compare(surface.recent, [])
        surface.fillAndSearch("Batman")
        surface.removeRecent("Batman")
        surface.historyStore = history
        wait(0)
        compare(surface.recent, ["Back Again"])
        surface.destroy()
    }
}
