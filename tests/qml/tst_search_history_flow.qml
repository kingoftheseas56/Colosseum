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
}
