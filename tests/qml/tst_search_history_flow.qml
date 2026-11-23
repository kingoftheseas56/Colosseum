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

    Component {
        id: audiobookCatalog
        QtObject {
            property bool local: true
            property int revision: 0
            property string completedQuery: ""
            property var queries: []
            signal requestFailed(string method, string reason)

            function ready() { return local }
            function search(query, limit) {
                queries = queries.concat([query])
                if (!local && query !== completedQuery) return []
                return [{ id: "dune-english", title: query }, { id: "dune-italian", title: query }]
            }
            function work(id) {
                return { authors: [{ name: "Frank Herbert" }], recordings: [] }
            }
            function remoteQueryState(method, args) {
                return { pending: args[0] !== completedQuery }
            }
        }
    }

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
        waitForRendering(surface)
        var removeTarget = findItem(surface, "searchRecentRemove")
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
        waitForRendering(surface)
        var bodyTarget = findItem(surface, "searchRecentBody")
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
        wait(0)
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

    function test_biblio_empty_audiobook_group_stays_visible_with_book_matches() {
        var surface = createTemporaryObject(biblioSearch, testWindow.contentItem, {
            historyStore: history, audioFirst: true,
            width: testWindow.width, height: testWindow.height,
            searchDispatcher: function(query, done) {
                done([{ id: 1, title: "Book One" }, { id: 2, title: "Book Two" }])
            },
            audioSearchDispatcher: function(query, done) { done([]) }
        })
        verify(surface !== null)
        surface.fillAndSearch("Dune")
        var status = findItem(surface, "searchGroupStatus_audiobooks")
        verify(status !== null && status.visible)
        compare(status.text, "No audiobook matches")
        compare(surface.groupedResults().map(function(group) { return group.key }),
                ["books", "audiobooks"])
        compare(surface.bookResultCount, 2)
    }

    function test_biblio_books_section_precedes_audiobooks_data() {
        return [{ tag: "books", audioFirst: false }, { tag: "audiobooks", audioFirst: true }]
    }

    function test_biblio_books_section_precedes_audiobooks(data) {
        var surface = createTemporaryObject(biblioSearch, testWindow.contentItem, {
            historyStore: history, audioFirst: data.audioFirst,
            width: testWindow.width, height: testWindow.height,
            searchDispatcher: function(query, done) {
                done([{ id: 1, title: "Book One" }, { id: 2, title: "Book Two" }])
            },
            audioSearchDispatcher: function(query, done) {
                done([{ workId: "a1", title: "Audio One" }, { workId: "a2", title: "Audio Two" }])
            }
        })
        verify(surface !== null)
        surface.fillAndSearch("Dune")
        verify(findItem(surface, "searchSurfaceResults") !== null)
        compare(surface.topResult.title, data.audioFirst ? "Audio One" : "Book One")
        var sections = surface.groupedResults().map(function(group) { return group.key })
        compare(sections, ["books", "audiobooks"])
    }

    function test_biblio_enter_opens_current_match_and_restores_search_state() {
        testWindow.requestActivate()
        var bookQueries = []
        var audioQueries = []
        var surface = createTemporaryObject(biblioSearch, testWindow.contentItem, {
            historyStore: history, width: testWindow.width, height: testWindow.height,
            searchDispatcher: function(query, done) {
                bookQueries.push(query)
                done([{ id: 99, title: "Dune", author: "Frank Herbert" }])
            },
            audioSearchDispatcher: function(query, done) {
                audioQueries.push(query)
                done([{ workId: "dune", title: "Dune" }])
            }
        })
        var opened = null
        surface.bookRequested.connect(function(book) { opened = book; surface.enabled = false })
        surface.audiobookRequested.connect(function(workId) { opened = workId; surface.enabled = false })
        var query = findItem(surface, "biblioSearchInput")
        surface.fillAndSearch("Dune")
        query.forceActiveFocus()
        tryCompare(query, "activeFocus", true)
        keyClick(Qt.Key_Return)
        compare(opened.id, 99)
        compare(opened.author, "Frank Herbert")
        compare(surface.enabled, false)

        surface.enabled = true
        compare(surface.query, "Dune")
        compare(surface.bookResultCount, 1)
        compare(surface.audioResultCount, 1)
        tryCompare(query, "activeFocus", true)
        surface.audioFirst = true
        keyClick(Qt.Key_Return)
        compare(opened, "dune")
        compare(bookQueries, ["Dune"])
        compare(audioQueries, ["Dune"])
    }

    function test_biblio_cancels_stale_lanes_and_uses_preferred_top_match() {
        var requests = []
        function dispatch(lane) {
            return function(query, done) {
                var request = { lane: lane, query: query, done: done, cancelled: false }
                requests.push(request)
                return function() { request.cancelled = true }
            }
        }
        var surface = createTemporaryObject(biblioSearch, testWindow.contentItem, {
            historyStore: history, audioFirst: true, width: testWindow.width, height: testWindow.height,
            searchDispatcher: dispatch("book"), audioSearchDispatcher: dispatch("audio")
        })
        surface.fillAndSearch("Du")
        surface.fillAndSearch("Dune")
        compare(requests.length, 4)
        verify(requests[0].cancelled)
        verify(requests[1].cancelled)
        requests[0].done([{ id: 1, title: "Old book" }])
        requests[1].done([{ workId: "old", title: "Old audio" }])
        compare(surface.resultCount, 0)
        requests[2].done([{ id: 2, title: "Current book" }])
        compare(surface.topResult, null)
        compare(surface.groupedResults().find(function(group) { return group.key === "books" }).items[0].title,
                "Current book")
        requests[3].done([{ workId: "current", title: "Current audio" }])
        compare(surface.topResult.title, "Current audio")
        compare(surface.resultCount, 2)
    }

    function test_biblio_catalogue_result_opens_selected_audiobook_data() {
        return [
            { tag: "desktop click", width: 1280, phone: false, keyboard: false },
            { tag: "desktop Enter", width: 1280, phone: false, keyboard: true },
            { tag: "phone tap", width: 420, phone: true, keyboard: false }
        ]
    }

    function test_biblio_catalogue_result_opens_selected_audiobook(data) {
        testWindow.requestActivate()
        var catalog = createTemporaryObject(audiobookCatalog, testWindow)
        var surface = createTemporaryObject(biblioSearch, testWindow.contentItem, {
            historyStore: history, audiobookCatalog: catalog,
            searchDispatcher: function(query, callback) { callback([]) },
            androidHost: data.phone, width: data.width, height: testWindow.height
        })
        verify(surface !== null)
        var bookOpened = false
        var workOpened = ""
        surface.bookRequested.connect(function() { bookOpened = true })
        surface.audiobookRequested.connect(function(workId) { workOpened = workId })
        surface.fillAndSearch("Dune")
        compare(catalog.queries, ["Dune"])
        compare(surface.audioResultCount, 2)
        compare(surface.audioResults[0].author, "Frank Herbert")
        surface.expandedGroups = [qsTr("Audiobooks")]
        var grid = findItem(surface, "searchGroup_audiobooks")
        verify(grid !== null)
        if (data.keyboard) {
            tryCompare(testWindow, "active", true)
            grid.currentIndex = 0
            grid.forceActiveFocus()
            tryCompare(grid, "activeFocus", true)
            keyClick(Qt.Key_Return)
        } else {
            var row = findItem(grid, "searchResult_dune-italian")
            verify(row !== null)
            surface.keepSearchDelegateVisible(row)
            waitForRendering(surface)
            if (data.phone) {
                var touch = touchEvent(row)
                touch.press(0, row, row.width / 2, row.height / 2).commit()
                touch.release(0, row, row.width / 2, row.height / 2).commit()
            } else {
                mouseClick(row, row.width / 2, row.height / 2)
            }
        }
        compare(workOpened, "dune-italian")
        compare(bookOpened, false)
    }

    function test_biblio_catalogue_waits_for_the_current_remote_query() {
        var catalog = createTemporaryObject(audiobookCatalog, testWindow, { local: false })
        var surface = createTemporaryObject(biblioSearch, testWindow.contentItem, {
            historyStore: history, audiobookCatalog: catalog,
            searchDispatcher: function(query, callback) { callback([]) },
            width: testWindow.width, height: testWindow.height
        })
        verify(surface !== null)
        surface.fillAndSearch("Dun")
        surface.fillAndSearch("Dune")
        compare(surface.audioSearching, true)
        compare(surface.showingNoResults, false)
        catalog.completedQuery = "Dun"
        ++catalog.revision
        compare(surface.audioResultCount, 0)
        catalog.completedQuery = "Dune"
        ++catalog.revision
        compare(surface.audioSearching, false)
        compare(surface.audioResults[0].title, "Dune")
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
