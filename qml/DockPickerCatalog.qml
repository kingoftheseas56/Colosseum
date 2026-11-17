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
    property int readingGeneration: 0
    property double theatreRefreshedAt: 0
    property bool theatreLoading: false
    property int theatreGeneration: 0

    readonly property var manga: typeof MalCatalog !== "undefined" ? MalCatalog : null
    readonly property var comics: typeof ComicsCatalog !== "undefined" ? ComicsCatalog : null
    readonly property var biblio: typeof BiblioCatalog !== "undefined" ? BiblioCatalog : null
    readonly property var audio: typeof Audiobooks !== "undefined" ? Audiobooks : null

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
        if (!active || readingRequests[key] || !(key === "manga" ? manga : comics)) return
        readingRequests[key] = true
        const adapter = Tankoban.create(manga, comics, installedAddons, showExplicit)
        adapter.fetchPage({ type: key, catalogKey: shelf }, 0, readingGeneration, function(generation, page) {
            if (generation !== catalog.readingGeneration) return
            delete catalog.readingRequests[key]
            if (page && !page.failed) catalog.publish(key, page.items, item => item)
        })
    }
    function updateBooks() {
        if (!active || !biblio || !biblio.ready) return
        const page = biblio.discoverPage("popular", "", "", showExplicit, 0, 10)
        publish("books", page && page.items, Books.normalizeBook)
    }
    function updateAudio() {
        if (!audio) return
        const rows = audio.downloadedAudiobooks().filter(item => item.bookPath && !item.missing)
        rows.sort((a, b) => b.addedAt - a.addedAt)
        publish("audiobooks", rows, item => ({ id: item.id, title: item.title, cover: item.cover || "", raw: item }))
    }
    function updateTributes() {
        publish("tributes", Universes.tributes(installedUniverses), item => ({
            id: item.extensionId, title: item.name, cover: item.banner ? Qt.resolvedUrl(item.banner) : "", raw: item
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
            updateReading("manga", "trending")
            updateReading("comics", "new-releases")
            updateBooks()
            updateAudio()
            updateTributes()
            updateTheatre()
        } else if (Object.keys(pendingPicks).length) {
            picks = Object.assign({}, picks, pendingPicks)
            pendingPicks = ({})
        }
    }
    onShowExplicitChanged: {
        picks = ({})
        pendingPicks = ({})
        readingGeneration++
        readingRequests = ({})
        theatreGeneration++
        theatreLoading = false
        theatreRefreshedAt = 0
        if (active) {
            updateReading("manga", "trending")
            updateReading("comics", "new-releases")
            updateBooks()
            updateAudio()
            updateTributes()
            Qt.callLater(updateTheatre)
        }
    }
    onInstalledAddonsChanged: {
        theatreGeneration++
        theatreLoading = false
        theatreRefreshedAt = 0
        if (active) updateTheatre()
    }
    onInstalledUniversesChanged: if (active) updateTributes()
    Connections {
        target: catalog.manga
        function onRevisionChanged() { catalog.updateReading("manga", "trending") }
    }
    Connections {
        target: catalog.comics
        function onRevisionChanged() { catalog.updateReading("comics", "new-releases") }
    }
    Connections {
        target: catalog.biblio
        function onReadyChanged() { catalog.updateBooks() }
        function onRevisionChanged() { catalog.updateBooks() }
    }
}
