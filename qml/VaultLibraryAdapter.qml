import QtQuick
import "VaultLibraryData.js" as Data
import "CatalogDelivery.js" as CatalogDelivery

// Read-model only. No persistence, filesystem parsing, or parallel identity store.
QtObject {
    id: adapter
    property var library: typeof VaultLibrary !== "undefined" ? VaultLibrary : null
    property var progressStore: typeof Progress !== "undefined" ? Progress : null
    property var homeServers: typeof HomeServers !== "undefined" ? HomeServers : null
    property var movies: []
    property var shows: []
    property var books: []
    property var comics: []
    property var continueItems: []
    property var heroItems: []
    property var unmatchedItems: []
    property var hiddenItems: []
    property var roots: []
    property var servers: []
    property var serverRows: ({})
    property var allItems: []
    property var counts: ({ movies: movies.length, shows: shows.length, books: books.length, comics: comics.length,
                              unmatched: unmatchedItems.length, hidden: hiddenItems.length })
    readonly property bool busy: !!((library && library.scanning) || (homeServers && homeServers.busy))
    readonly property bool empty: roots.length === 0 && servers.length === 0 && allItems.length === 0 && unmatchedItems.length === 0
    readonly property string status: {
        if (library && library.scanning) return "Scanning " + (library.scanningRoot || "folder")
                + (library.scanTotal > 0 ? " · " + library.scanDone + " of " + library.scanTotal : "…")
        var offline = roots.filter(function(r) { return r.available === false }).length
                    + servers.filter(function(s) { return s.offline || s.authRequired || s.disabled }).length
        var total = roots.length + servers.length
        if (!total) return "No sources connected"
        return offline ? offline + " source" + (offline === 1 ? "" : "s") + " unavailable"
                       : total + (total === 1 ? " source · " : " sources · ") + (busy ? "Updating…" : servers.length ? "Libraries loaded" : "Folders indexed")
    }
    property var gaps: ["Server API does not expose library grouping, poster artwork, or structured network reachability.",
        "Signed-out server titles are unavailable from the native shelf API; no retained QML cache is invented.",
        "Vault metadata does not expose backdrop or logo artwork.",
        "Unified book Progress does not expose chapter position; only percentage is shown.",
        "Embedded EPUB metadata without an adopted title cannot prove the title is not a filename.",
        "Flat video groups without season structure do not expose a reliable movie/show identity type.",
        "Filing extras under another show is not exposed by VaultLibrary."]
    property string actionError: ""
    property var libraryConnection: Connections {
        target: adapter.library
        ignoreUnknownSignals: true
        function onChanged() { adapter.refresh() }
        function onBrowseArtResolved() { adapter.refresh() }
    }
    property var progressConnection: Connections {
        target: adapter.progressStore
        ignoreUnknownSignals: true
        function onChanged() { adapter.refresh() }
    }
    property var serverConnection: Connections {
        target: adapter.homeServers
        ignoreUnknownSignals: true
        function onChanged() { adapter.refresh() }
        function onProfileChanged() { adapter.refresh() }
    }
    Component.onCompleted: refresh()
    function call(name, args, fallback) {
        if (!library || typeof library[name] !== "function") return fallback
        return library[name].apply(library, args || [])
    }
    function refresh() {
        var local = [], unmatched = [], hidden = [], kinds = ["video", "book", "comic"]
        roots = call("rootsDetail", [], []).map(function(r) {
            var root = Data.copy(r)
            root.offline = r.available === false
            root.count = Number(r.itemCount) || 0
            root.watching = r.available !== false && !r.watcherDegraded
            root.state = root.offline ? "Drive not connected" : r.watcherDegraded ? "Rescan on open" : "Watching"
            var fileCount = Number(r.fileCount) || 0
            root.subtitle = fileCount + (fileCount === 1 ? " file" : " files")
            return root
        })
        for (var k = 0; k < kinds.length; ++k) {
            var groups = call("series", [kinds[k]], [])
            for (var i = 0; i < groups.length; ++i) {
                var g = groups[i], files = call("items", [g.kind, g.key], [])
                var structure = g.kind === "video" ? call("seasonFactsForShow", [g.key], {}) : {}
                var detail = g.kind === "video" ? call("browseDetail", [g.key], {}) : {}
                var row = Data.normalize(g, files, structure, false, detail)
                if (row.identified) local.push(row); else unmatched.push(row)
            }
        }
        var hiddenGroups = call("hiddenSeries", [], [])
        for (var h = 0; h < hiddenGroups.length; ++h) {
            var hg = hiddenGroups[h]
            hidden.push(Data.normalize(hg, call("items", [hg.kind, hg.key], []),
                hg.kind === "video" ? call("seasonFactsForShow", [hg.key], {}) : {}, true))
        }
        var admission = call("admissionById", [], {})
        local = Data.deduplicate(local).map(function(r) { return Data.joinProgress(r, progressStore, admission) })
        allItems = local
        movies = local.filter(function(r) { return r.kind === "movie" })
        shows = local.filter(function(r) { return r.kind === "show" })
        books = local.filter(function(r) { return r.kind === "book" })
        comics = local.filter(function(r) { return r.kind === "comic" })
        unmatchedItems = unmatched; hiddenItems = hidden
        var connections = homeServers ? homeServers.connections || [] : [], mapped = {}, remote = [], tabs = []
        for (var s = 0; s < connections.length; ++s) {
            var connection = Data.copy(connections[s])
            connection.profileGeneration = homeServers.profileGeneration
            connection.authRequired = !!connection.needsReconnect
            connection.disabled = connection.enabled === false
            // Current native connections has no structured reachability field. Do not
            // classify translated status strings as network health or claim online.
            connection.offline = connection.offline === true || connection.available === false || connection.health === "offline"
            connection.state = connection.authRequired ? "Reconnect in Sync Center"
                : connection.disabled ? "Disabled in Sync Center"
                : connection.offline ? "Server offline" : connection.status || "Library available"
            var titles = typeof homeServers.shelfTitles === "function" ? homeServers.shelfTitles(connection.id) : []
            var projected = []
            for (var t = 0; t < titles.length; ++t) projected.push(Data.joinProgress(Data.serverRow(titles[t], connection), progressStore))
            connection.count = projected.length
            tabs.push(connection); mapped[connection.id] = projected; remote = remote.concat(projected)
        }
        servers = tabs; serverRows = mapped
        continueItems = Data.continuing(local.concat(remote))
        // Continue leads; identified newcomers follow, without invented artwork or progress.
        var heroes = continueItems.slice(0, 5), seen = {}
        heroes.forEach(function(r) { seen[r.id] = true })
        var newest = local.slice().sort(function(a,b) { return b.addedAt - a.addedAt })
        for (var n = 0; n < newest.length && heroes.length < 5; ++n)
            if (!seen[newest[n].id]) { heroes.push(newest[n]); seen[newest[n].id] = true }
        heroItems = heroes
    }
    function rowsForKind(kind) {
        if (kind === "movie" || kind === "movies") return movies
        if (kind === "show" || kind === "shows" || kind === "series") return shows
        if (kind === "book" || kind === "books") return books
        if (kind === "comic" || kind === "comics") return comics
        if (kind === "hidden") return hiddenItems
        if (kind === "unmatched" || kind === "needs-match") return unmatchedItems
        return allItems
    }
    function rowsForServer(id) { return serverRows[id] || [] }
    function search(query, view, serverId) { return Data.search(serverId ? rowsForServer(serverId) : rowsForKind(view), query) }
    function rescan(path) {
        if (busy) return false
        var rootPath = path || (roots.length ? roots[0].path : "")
        if (!rootPath) return false
        // Native rescanRoot republishes the union of all roots, not only the supplied root.
        call("rescanRoot", [rootPath], null); return true
    }
    function groupAction(method, row) {
        if (!row || row.serverId) return false
        var copies = row.copies || [row.payload || row], ok = true
        for (var i = 0; i < copies.length; ++i) if (!call(method, [copies[i].key], false)) ok = false
        refresh(); return ok
    }
    function hide(row) { return groupAction("hideGroup", row) }
    function restore(row) { return groupAction("restoreGroup", row) }
    function identify(row, result) {
        if (!row || row.serverId) return false
        var ok = result ? call("identifyGroupWith", [row.seriesKey, result], false) : call("identifyGroup", [row.seriesKey], false)
        refresh(); return ok
    }
    // Candidate UI can also use the existing VaultIdentifyDialog (including explicit network fallback).
    function matchingCandidates(query, kind) {
        var provider = null
        if (kind === "video" || kind === "movie" || kind === "show") provider = typeof ImdbCatalog !== "undefined" ? ImdbCatalog : null
        else if (kind === "comic") provider = typeof ComicsCatalog !== "undefined" ? ComicsCatalog : null
        return provider && CatalogDelivery.available(provider) ? provider.search(query, 20) : []
    }
    function addFolder(path) { call("addFolder", [path], null) }
    function forgetRoot(path) { call("forgetRoot", [path], null) }
    function reveal(path) { return call("revealInExplorer", [path], false) }
    function scanIgnore() { return call("scanIgnore", [], []) }
    function setScanIgnore(patterns) { call("setScanIgnore", [patterns], null) }
    function cancelScan() { call("cancelScan", [], null) }
    function refreshServer(id) { if (homeServers && typeof homeServers.refresh === "function") homeServers.refresh(id) }
}
