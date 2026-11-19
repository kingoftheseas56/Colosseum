import QtQuick
import "TheatreApi.js" as Theatre
import "AddonClient.js" as Addons
import "TankobanDiscoverApi.js" as Tankoban
import "BiblioDiscoverApi.js" as Books
import "Universes.js" as Universes

Item {
    id: catalog
    visible: false
    property bool active: false
    property bool showExplicit: false
    property var installedAddons: []
    property var installedUniverses: []
    property var picks: ({})
    property var pendingPicks: ({})
    property var readingRequests: ({})
    property var readingRefreshedAt: ({})
    property int readingGeneration: 0
    property double theatreRefreshedAt: 0
    property bool theatreLoading: false
    property int theatreGeneration: 0
    property int refreshStep: 0

    readonly property var manga: typeof MalCatalog !== "undefined" ? MalCatalog : null
    readonly property var comics: typeof ComicsCatalog !== "undefined" ? ComicsCatalog : null
    readonly property var biblio: typeof BiblioCatalog !== "undefined" ? BiblioCatalog : null

    function publish(key, rows, convert) {
        const seen = new Set()
        const entries = []
        for (const row of rows || []) {
            const entry = convert(row)
            if (!entry.title) continue
            const identity = String(entry.id || entry.title).toLowerCase()
            if (seen.has(identity)) continue
            seen.add(identity)
            entries.push(entry)
            if (entries.length >= 10) break
        }
        const before = picks[key] || []
        if (JSON.stringify(before) === JSON.stringify(entries)) {
            delete pendingPicks[key]
            return
        }
        if (active && before.length) {
            // An asynchronous refresh must not change the cover under the pointer.
            pendingPicks[key] = entries
        } else {
            picks = Object.assign({}, picks, { [key]: entries })
        }
    }
    function updateReading(key, shelf) {
        if (!active || readingRequests[key] || !(key === "comics" ? comics : manga)) return
        if (Date.now() - (readingRefreshedAt[key] || 0) < 300000) return
        readingRequests[key] = true
        const adapter = Tankoban.create(manga, comics, installedAddons, showExplicit)
        adapter.fetchPage({ type: key, catalogKey: shelf }, 0, readingGeneration, function(generation, page) {
            if (generation !== catalog.readingGeneration) return
            delete catalog.readingRequests[key]
            if (page && !page.failed) {
                if (page.items && page.items.length) catalog.readingRefreshedAt[key] = Date.now()
                catalog.publish(key, page.items, item => item)
            }
        })
    }
    function updateBooks() {
        if (!active || !biblio || !biblio.ready) return
        const page = biblio.discoverPage("popular", "", "", showExplicit, 0, 10)
        publish("books", page && page.items, Books.normalizeBook)
    }
    function updateUniverses() {
        const roster = Universes.worldRoster(installedUniverses).filter(item => item.logo)
        const colored = roster.filter(item => item.logoColored)
        const monochrome = roster.filter(item => !item.logoColored)
        publish("universes", colored.concat(monochrome), item => ({
            id: item.extensionId || item.name, title: item.name, cover: Qt.resolvedUrl(item.logo), raw: item
        }))
    }
    function updateTributes() {
        const roster = installedUniverses.filter(item => item.extensionId)
        publish("tributes", roster, item => ({
            id: item.extensionId, title: item.name, cover: item.banner || "",
            coverCandidates: item.logo ? [item.logo] : [], raw: item
        }))
    }
    function updateTheatre() {
        if (theatreLoading || Date.now() - theatreRefreshedAt < 300000) return
        theatreLoading = true
        const request = ++theatreGeneration
        Theatre.loadTheatre(function(rows) {
            if (request !== catalog.theatreGeneration) return
            catalog.theatreLoading = false
            if (rows.movies.length || rows.series.length || rows.anime.length)
                catalog.theatreRefreshedAt = Date.now()
            const anime = new Set(rows.anime.map(item => item.id))
            catalog.publishVideo("movies", "movie", rows.movies, request)
            catalog.publishVideo("tv", "series", rows.series.filter(item => !anime.has(item.id)), request)
            catalog.publishVideo("anime", "anime", rows.anime, request)
        })
    }
    function publishVideo(key, type, rows, generation) {
        const ranked = rows.filter(item => Theatre.visibleTheatreItem(item, item, showExplicit))
        const spec = Addons.catalogSpecs(installedAddons, type).find(
            shelf => /trending|popular|new[ _-]+releases|recent/i.test(shelf.catalogId + " " + shelf.title))
        if (!spec) {
            if (ranked.length) publish(key, ranked, item => item)
            return
        }
        Addons.fetchCatalog(spec, function(items) {
            if (generation !== catalog.theatreGeneration) return
            const extra = items.map((meta, index) => {
                if (!meta || !meta.id) return null
                const item = Theatre.mapCinemeta(Object.assign({ type: type === "movie" ? "movie" : "series" }, meta), index)
                return Theatre.visibleTheatreItem(meta, item, catalog.showExplicit) ? item : null
            }).filter(item => item !== null)
            const mixed = []
            for (let i = 0; i < Math.min(10, Math.max(extra.length, ranked.length)); i++) {
                if (extra[i]) mixed.push(extra[i])
                if (ranked[i]) mixed.push(ranked[i])
            }
            if (mixed.length) catalog.publish(key, mixed, item => item)
        })
    }
    onActiveChanged: {
        if (active) {
            refreshStep = 0
        } else {
            openRefresh.stop()
            if (Object.keys(pendingPicks).length) {
                picks = Object.assign({}, picks, pendingPicks)
                pendingPicks = ({})
            }
        }
    }
    Connections {
        target: catalog.active && catalog.refreshStep === 0 ? catalog.Window.window : null
        function onFrameSwapped() {
            catalog.refreshStep = 1
            catalog.updateTheatre()
            openRefresh.start()
        }
    }
    Timer {
        id: openRefresh
        interval: 48
        repeat: true
        onTriggered: {
            // The native discover queries run synchronously. Give rendering a
            // turn between them and avoid starting another query after dismissal.
            switch (catalog.refreshStep++) {
            case 1: catalog.updateUniverses(); break
            case 2: catalog.updateTributes(); break
            case 3: catalog.updateReading("manga", "trending"); break
            case 4: catalog.updateReading("manhwa", "trending"); break
            case 5: catalog.updateReading("comics", "new-releases"); break
            case 6: catalog.updateBooks(); stop(); break
            }
        }
    }
    onShowExplicitChanged: {
        picks = ({})
        pendingPicks = ({})
        readingGeneration++
        readingRequests = ({})
        readingRefreshedAt = ({})
        theatreGeneration++
        theatreLoading = false
        theatreRefreshedAt = 0
        if (active) {
            openRefresh.stop()
            refreshStep = 0
        }
    }
    onInstalledAddonsChanged: {
        theatreGeneration++
        theatreLoading = false
        theatreRefreshedAt = 0
        if (active && refreshStep) updateTheatre()
    }
    onInstalledUniversesChanged: if (active && refreshStep) {
        updateUniverses()
        updateTributes()
    }
    Connections {
        target: catalog.manga
        function onRevisionChanged() {
            delete catalog.readingRefreshedAt.manga
            delete catalog.readingRefreshedAt.manhwa
            if (catalog.refreshStep) {
                catalog.updateReading("manga", "trending")
                catalog.updateReading("manhwa", "trending")
            }
        }
    }
    Connections {
        target: catalog.comics
        function onRevisionChanged() {
            delete catalog.readingRefreshedAt.comics
            if (catalog.refreshStep) catalog.updateReading("comics", "new-releases")
        }
    }
    Connections {
        target: catalog.biblio
        function onReadyChanged() { if (catalog.refreshStep) catalog.updateBooks() }
        function onRevisionChanged() { if (catalog.refreshStep) catalog.updateBooks() }
    }
}
