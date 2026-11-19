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
    property var serverGroups: ({})
    readonly property var providers: providerProjection()
    property var allItems: []
    property var counts: ({ movies: movies.length, shows: shows.length, books: books.length, comics: comics.length,
                              unmatched: unmatchedItems.length, hidden: hiddenItems.length })
    readonly property bool indexing: !!(library && library.scanning)
    readonly property bool firstFolderIndexing: indexing && !!library.firstFolderIndexing
    readonly property bool indexingCancellable: indexing && !!library.scanCanCancel
    readonly property bool busy: projectionBuild !== null || !!(metadataTimer && metadataTimer.running)
        || indexing || !!(homeServers && homeServers.busy)
    readonly property bool empty: roots.length === 0 && servers.length === 0 && allItems.length === 0 && unmatchedItems.length === 0
    readonly property string indexingPhase: indexing ? (library.scanPhase || "discovering") : ""
    readonly property string indexingStage: {
        if (indexingPhase === "privacy") return qsTr("Applying Personal choices")
        if (indexingPhase === "publishing") return qsTr("Updating shelves")
        if (indexingPhase === "processing") return qsTr("Preparing files")
        return indexing ? qsTr("Finding media") : ""
    }
    readonly property string indexingCount: {
        if (indexingPhase !== "discovering" && indexingPhase !== "processing") return ""
        const files = Number(library.scanDone) || 0
        if (files <= 0) return ""
        if (indexingPhase === "processing")
            return files === 1 ? qsTr("1 file prepared") : qsTr("%L1 files prepared").arg(files)
        return files === 1 ? qsTr("1 file found") : qsTr("%L1 files found").arg(files)
    }
    readonly property string indexingStatus: indexingCount
        ? qsTr("%1 · %2").arg(indexingStage).arg(indexingCount) : indexingStage
    readonly property string status: {
        if (indexingStatus.length) return indexingStatus
        var offline = roots.filter(function(r) { return r.available === false }).length
                    + servers.filter(function(s) { return s.offline || s.authRequired || s.disabled }).length
        var total = roots.length + servers.length
        if (!total) return "No sources connected"
        return offline ? offline + " source" + (offline === 1 ? "" : "s") + " unavailable"
                       : total + (total === 1 ? " source · " : " sources · ") + (busy ? "Updating…" : servers.length ? "Libraries loaded" : "Folders connected")
    }
    property var gaps: []
    property string actionError: ""
    property bool ready: false
    property var pendingArtwork: ({})
    property var artworkTargets: ({})
    property var admission: ({})
    property var projectionBuild: null
    property int projectionGeneration: 0
    property var metadataTimer: Timer { interval: 0; onTriggered: adapter.refreshLocal() }
    property var buildTimer: Timer { interval: 0; onTriggered: adapter.buildNext() }
    property var artworkTimer: Timer { interval: 0; onTriggered: adapter.flushArtwork() }
    property var progressTimer: Timer { interval: 0; onTriggered: adapter.rejoinProgress() }
    property var libraryConnection: Connections {
        target: adapter.library
        ignoreUnknownSignals: true
        function onChanged() { adapter.invalidateProjection() }
        function onPersonalPathsChanged() {
            adapter.unmatchedItems = []
            adapter.hiddenItems = []
            adapter.admission = ({})
            adapter.publishLocal([])
            adapter.invalidateProjection()
        }
        function onBrowseArtworkReady(rowKey, coverUrl) {
            adapter.pendingArtwork[rowKey] = coverUrl
            adapter.artworkTimer.start()
        }
    }
    property var progressConnection: Connections {
        target: adapter.progressStore
        ignoreUnknownSignals: true
        function onChanged() { adapter.progressTimer.start() }
    }
    property var serverConnection: Connections {
        target: adapter.homeServers
        ignoreUnknownSignals: true
        function onChanged() { adapter.refreshServers() }
        function onProfileChanged() { adapter.refreshServers() }
    }
    onLibraryChanged: if (ready) refreshLocal()
    onProgressStoreChanged: if (ready) progressTimer.start()
    onHomeServersChanged: if (ready) refreshServers()
    Component.onCompleted: { ready = true; refresh() }
    function call(name, args, fallback) {
        if (!library || typeof library[name] !== "function") return fallback
        return library[name].apply(library, args || [])
    }
    function refresh() { refreshLocal(); refreshServers() }
    function invalidateProjection() {
        ++projectionGeneration
        buildTimer.stop(); artworkTimer.stop()
        projectionBuild = null; pendingArtwork = ({})
        metadataTimer.start()
    }
    function sourceIsCurrent(source, revision, generation) {
        return library === source && projectionGeneration === generation
                && (!source || source.revision === revision)
    }
    function buildIsCurrent(build) {
        return projectionBuild === build && sourceIsCurrent(build.source, build.revision, build.generation)
    }
    function rejectBuild(build) {
        if (projectionBuild === build) { buildTimer.stop(); projectionBuild = null }
        metadataTimer.start()
    }
    function refreshLocal() {
        metadataTimer.stop(); buildTimer.stop(); artworkTimer.stop()
        pendingArtwork = ({})
        const source = library, revision = source ? source.revision : undefined, generation = ++projectionGeneration
        var projectedRoots = call("rootsDetail", [], []).map(function(r) {
            var root = Data.copy(r)
            root.offline = r.available === false
            root.count = Number(r.itemCount) || 0
            root.watching = r.available !== false && !r.watcherDegraded
            root.state = root.offline ? "Drive not connected" : r.watcherDegraded ? "Rescan on open" : "Watching"
            var fileCount = Number(r.fileCount) || 0
            root.subtitle = fileCount + (fileCount === 1 ? " file" : " files")
            return root
        })
        var projectedAdmission = call("admissionById", [], {})
        var groups = []
        ;["video", "book", "comic"].forEach(function(kind) { groups = groups.concat(call("series", [kind], [])) })
        var hidden = call("hiddenSeries", [], [])
        if (!sourceIsCurrent(source, revision, generation)) { metadataTimer.start(); return }
        roots = projectedRoots; admission = projectedAdmission
        projectionBuild = {source:source, revision:revision, generation:generation,
            groups:groups, hidden:hidden, cursor:0, local:[], unmatched:[], hiddenRows:[]}
        buildNext()
    }
    function buildNext() {
        var build = projectionBuild
        if (!build) return
        if (!buildIsCurrent(build)) { rejectBuild(build); return }
        var total = build.groups.length + build.hidden.length
        var end = Math.min(total, build.cursor + 8)
        for (; build.cursor < end; ++build.cursor) {
            var hidden = build.cursor >= build.groups.length
            var group = hidden ? build.hidden[build.cursor - build.groups.length] : build.groups[build.cursor]
            var files = call("items", [group.kind, group.key], [])
            if (!buildIsCurrent(build)) { rejectBuild(build); return }
            var structure = group.kind === "video" ? call("seasonFactsForShow", [group.key], {}) : {}
            if (!buildIsCurrent(build)) { rejectBuild(build); return }
            var detail = group.kind === "video" ? call("presentationDetail", [group.key], {}) : {}
            if (!buildIsCurrent(build)) { rejectBuild(build); return }
            var row = Data.normalize(group, files, structure, hidden, detail)
            row.supplementalCount = Number(group.supplementalCount) || 0
            if (hidden) build.hiddenRows.push(row)
            else if (row.identified && row.kind !== "unknown") build.local.push(row)
            else build.unmatched.push(row)
        }
        if (build.cursor < total) { buildTimer.start(); return }
        const local = Data.deduplicate(build.local).map(joinProgress)
        if (!buildIsCurrent(build)) { rejectBuild(build); return }
        projectionBuild = null
        unmatchedItems = build.unmatched; hiddenItems = build.hiddenRows
        publishLocal(local)
        indexArtwork()
        if (Object.keys(pendingArtwork).length) artworkTimer.start()
    }
    function joinProgress(row) {
        var clean = Object.assign({}, row, {progress:0, progressFraction:0, progressText:"", lastReadMs:0,
            finished:false, completed:false, hasProgress:false, progressRecord:null, resumePath:"", resumeOffline:false})
        return Data.joinProgress(clean, progressStore, admission)
    }
    function publishLocal(rows) {
        allItems = rows
        publishShelf("movies", rows.filter(function(r) { return r.kind === "movie" }))
        publishShelf("shows", rows.filter(function(r) { return r.kind === "show" }))
        publishShelf("books", rows.filter(function(r) { return r.kind === "book" }))
        publishShelf("comics", rows.filter(function(r) { return r.kind === "comic" }))
        updateHighlights()
    }
    function publishShelf(name, rows) {
        const previous = adapter[name]
        if (rows.length !== previous.length || rows.some((row, index) => row !== previous[index]))
            adapter[name] = rows
    }
    function rejoinProgress() {
        var mapped = {}, grouped = {}
        Object.keys(serverRows).forEach(function(id) { mapped[id] = serverRows[id].map(joinProgress) })
        Object.keys(serverGroups).forEach(function(id) {
            grouped[id] = serverGroups[id].map(function(group) {
                return Object.assign({}, group, {items:group.items.map(joinProgress)})
            })
        })
        serverRows = mapped; serverGroups = grouped
        publishLocal(allItems.map(joinProgress))
    }
    function indexArtwork() {
        var targets = {}
        ;[allItems, unmatchedItems, hiddenItems].forEach(function(rows, bucket) {
            rows.forEach(function(row, index) {
                var keys = [row.seriesKey]
                ;(row.copies || []).forEach(function(copy) { keys.push(copy.key) })
                ;(row.items || []).forEach(function(file) { keys.push(file.id); keys.push(file.path) })
                keys.forEach(function(key) {
                    if (!key) return
                    var target = bucket + ":" + index
                    if (!targets[key]) targets[key] = {}
                    targets[key][target] = [bucket, index]
                })
            })
        })
        artworkTargets = targets
    }
    function patchArtwork(row, updates) {
        var out = Data.copy(row)
        function group(copy) { return updates[copy.key] ? Object.assign({}, copy, {coverUrl:updates[copy.key]}) : copy }
        function file(item) { var cover = updates[item.id] || updates[item.path]; return cover ? Object.assign({}, item, {coverUrl:cover}) : item }
        out.payload = group(row.payload || {})
        out.copies = (row.copies || []).map(group)
        out.items = (row.items || []).map(file)
        out.progressItems = (row.progressItems || []).map(file)
        var covered = out.copies.find(function(copy) { return !!copy.coverUrl })
        var cover = out.payload.coverUrl || (covered ? covered.coverUrl : "") || (out.items[0] || {}).coverUrl || ""
        out.cover = cover; out.poster = cover; out.coverUrl = cover
        out.identity = Object.assign({}, row.identity, {coverUrl:cover})
        return out
    }
    function flushArtwork() {
        if (projectionBuild || metadataTimer.running) return
        var updates = pendingArtwork, affected = {}
        pendingArtwork = ({})
        Object.keys(updates).forEach(function(key) {
            var targets = artworkTargets[key] || {}
            Object.keys(targets).forEach(function(target) { affected[target] = targets[target] })
        })
        const buckets = [allItems, unmatchedItems, hiddenItems]
        const patched = []
        Object.keys(affected).forEach(function(target) {
            var location = affected[target]
            const bucket = location[0], index = location[1]
            if (!patched[bucket]) patched[bucket] = buckets[bucket].slice()
            patched[bucket][index] = patchArtwork(buckets[bucket][index], updates)
        })
        if (patched[1]) unmatchedItems = patched[1]
        if (patched[2]) hiddenItems = patched[2]
        if (patched[0]) publishLocal(patched[0])
    }
    function refreshServers() {
        var connections = homeServers ? homeServers.serverTabs || [] : [], mapped = {}, grouped = {}, remote = [], tabs = []
        for (var s = 0; s < connections.length; ++s) {
            var connection = Data.copy(connections[s])
            connection.profileGeneration = homeServers.profileGeneration
            connection.authRequired = connection.state === "needsReconnect"
            connection.disabled = connection.state === "disabled" || connection.enabled === false
            connection.offline = connection.state === "unavailable"
            connection.stateLabel = ({cached:"Cached library", pending:"Waiting to refresh", refreshing:"Refreshing",
                ready:"Library ready", unavailable:"Server unavailable", needsReconnect:"Reconnect in Sync Center", disabled:"Disabled in Sync Center"})[connection.state] || "Cached library"
            var libraries = !connection.disabled && typeof homeServers.serverLibraries === "function" ? homeServers.serverLibraries(connection.id) : []
            var projected = [], groups = [], seen = {}
            for (var l = 0; l < libraries.length; ++l) {
                var manifest = libraries[l], items = [], titles = manifest.titles || []
                for (var t = 0; t < titles.length; ++t) {
                    var title = Data.copy(titles[t])
                    title.state = manifest.state || connection.state; title.dimmed = manifest.dimmed || connection.dimmed
                    var item = Data.joinProgress(Data.serverRow(title, connection), progressStore)
                    items.push(item)
                    if (!seen[item.id]) { seen[item.id] = true; projected.push(item) }
                }
                groups.push({id:manifest.id, name:manifest.name, kind:manifest.kind, items:items,
                    state:manifest.state || connection.state, dimmed:!!manifest.dimmed || !!connection.dimmed})
            }
            connection.count = projected.length
            tabs.push(connection); mapped[connection.id] = projected; grouped[connection.id] = groups; remote = remote.concat(projected)
        }
        servers = tabs; serverRows = mapped; serverGroups = grouped
        updateHighlights()
    }
    function updateHighlights() {
        var local = allItems, remote = []
        Object.keys(serverRows).forEach(function(id) { remote = remote.concat(serverRows[id]) })
        continueItems = Data.continuing(local.concat(remote))
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
    function providerProjection() {
        return ["jellyfin", "emby", "plex"].map(function(key) {
            var connections = adapter.servers.filter(server => server.provider === key)
            var enabled = connections.filter(server => server.enabled !== false && server.state !== "disabled")
            var rows = [], groups = [], byId = {}
            enabled.forEach(function(server) {
                ;(adapter.serverRows[server.id] || []).forEach(function(row) {
                    var existing = byId[row.id]
                    if (existing) {
                        var provenance = existing.sourceServerIds.concat([server.id])
                        if ((existing.offline && !row.offline) || (existing.dimmed && !row.dimmed)) Object.assign(existing, row)
                        existing.sourceServerIds = provenance
                    } else {
                        var projected = Object.assign({}, row, { sourceServerIds: [server.id] })
                        byId[row.id] = projected
                        rows.push(projected)
                    }
                })
                ;(adapter.serverGroups[server.id] || []).forEach(function(group) {
                    var label = (connections.length > 1 ? server.name + " · " : "") + (group.name || group.title || qsTr("Library"))
                    groups.push(Object.assign({}, group, {
                        id: server.id + ":" + group.id,
                        sourceLibraryId: group.id,
                        serverId: server.id,
                        name: label, title: label
                    }))
                })
            })
            var unavailable = enabled.filter(server => server.offline || server.state === "unavailable")
            var reconnect = enabled.filter(server => server.authRequired || server.needsReconnect || server.state === "needsReconnect")
            var state = !connections.length ? "disconnected" : !enabled.length ? "disabled"
                : enabled.some(server => server.state === "ready") ? "ready"
                : enabled.some(server => server.state === "refreshing" || server.state === "pending") ? "refreshing"
                : reconnect.length ? "needsReconnect" : unavailable.length ? "unavailable" : "cached"
            var status = []
            if (state === "disabled") status.push(qsTr("Connections disabled · Manage in Sources"))
            else {
                if (unavailable.length) status.push(unavailable.length === 1 ? qsTr("1 server unavailable") : qsTr("%1 servers unavailable").arg(unavailable.length))
                if (reconnect.length) status.push(reconnect.length === 1 ? qsTr("1 server needs sign-in") : qsTr("%1 servers need sign-in").arg(reconnect.length))
                if (!status.length && state === "cached") status.push(qsTr("Saved libraries · Refresh in Sync Center"))
                if (state === "refreshing") status.push(qsTr("Updating libraries…"))
            }
            return { id: key, provider: key, name: key === "jellyfin" ? "Jellyfin" : key === "emby" ? "Emby" : "Plex",
                connected: connections.length > 0, connectionCount: connections.length, enabled: enabled.length > 0,
                state: state, offline: enabled.length > 0 && unavailable.length === enabled.length,
                authRequired: enabled.length > 0 && reconnect.length === enabled.length,
                dimmed: enabled.length > 0 && enabled.every(server => server.dimmed || server.offline || server.authRequired),
                statusText: status.join(" · "), count: rows.length, rows: rows, groups: groups }
        })
    }
    function providerKey(id) {
        var server = servers.find(row => row.id === id)
        return server ? server.provider : id
    }
    function rowsForServer(id) {
        var provider = providers.find(row => row.id === id)
        return provider ? provider.rows : serverRows[id] || []
    }
    function groupsForServer(id) {
        var provider = providers.find(row => row.id === id)
        return provider ? provider.groups : serverGroups[id] || []
    }
    function attachExtras(row, show) {
        if (!row || !show || row.nativeKind !== "video" || show.kind !== "show") return false
        var ok = call("attachExtrasToShow", [row.seriesKey, show.seriesKey], false)
        refresh(); return ok
    }
    function extrasForShow(show) { return show ? call("extrasForShow", [show.seriesKey], []) : [] }
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
    function cancelScan() { if (indexingCancellable) call("cancelScan", [], null) }
    function refreshServer(id) { if (homeServers && typeof homeServers.refresh === "function") homeServers.refresh(id) }
}
