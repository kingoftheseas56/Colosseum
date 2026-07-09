// Colosseum — HOME (v1, on the proven spine)
// Fullscreen-exclusive frameless OS surface: persistent wallpaper + frosted-glass chrome.
//   Top bar (clock·pills·system) → Universe hero → unified Continue row → per-medium trending rows.
// Mock data only (no Universe data engine yet). Glass = proven material (see Glass.qml).
// Run:  C:/Qt/6.11.1/mingw_64/bin/qml.exe qml/Main.qml      (Esc / Ctrl+Q to quit)

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtCore
import "Catalog.js" as Catalog
import "Universes.js" as Universes
import "UniverseApi.js" as UniverseApi
import "McuApi.js" as Mcu
import "TheatreApi.js" as TheatreApi
import "AddonClient.js" as AddonClient
import "Subtitles.js" as Subtitles
import "Torrentio.js" as Torrentio

Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#05060a"
    title: "Colosseum"

    property string currentSurface: "Home"
    property string wallpaperSource: "../assets/wallpaper/captured-motion.jpg"

    Settings {
        id: wallpaperSettings
        location: Qt.resolvedUrl("../wallpapers.ini")
        category: "wallpapers"
        property string homePick: ""
        property string tankobanPick: ""
        property string biblioPick: ""
        property string theatrePick: ""
    }

    function wallpaperKey(world) {
        if (world === "Tankoban") return "tankobanPick"
        if (world === "Biblio") return "biblioPick"
        if (world === "Theatre") return "theatrePick"
        return "homePick"
    }
    function parsePick(raw) {
        if (!raw) return null
        if (typeof raw === "object") return raw
        try {
            var parsed = JSON.parse(raw)
            if (typeof parsed === "string") return { image_url: parsed }
            return parsed
        } catch (e) {
            return { image_url: raw }
        }
    }
    function pickFor(world) {
        return parsePick(wallpaperSettings[wallpaperKey(world)])
    }
    function wallpaperWorldForSession(rec) {
        var appType = rec && rec.appType ? ("" + rec.appType).toLowerCase() : ""
        var contentKind = rec && rec.contentKind ? ("" + rec.contentKind).toLowerCase() : ""
        if (appType === "theatre" || contentKind === "movie") return "Theatre"
        if (appType === "tankoban" || contentKind === "comic") return "Tankoban"
        if (appType === "biblio" || contentKind === "book") return "Biblio"
        return currentSurface || "Home"
    }
    function refreshWallpaper() {
        var pick = pickFor(currentSurface)
        wallpaperSource = pick && pick.image_url ? pick.image_url : "../assets/wallpaper/captured-motion.jpg"
    }
    function setWallpaperPick(world, pick) {
        wallpaperSettings[wallpaperKey(world)] = typeof pick === "string" ? pick : JSON.stringify(pick || {})
        if (currentSurface === world) refreshWallpaper()
    }
    function setWallpaperEverywhere(pick) {
        var raw = typeof pick === "string" ? pick : JSON.stringify(pick || {})
        wallpaperSettings.homePick = raw
        wallpaperSettings.tankobanPick = raw
        wallpaperSettings.biblioPick = raw
        wallpaperSettings.theatrePick = raw
        refreshWallpaper()
    }

    Component.onCompleted: {
        refreshWallpaper()
        // Theatre reads the extension registry through a pushed copy — a .pragma
        // library can't reach context properties (extensions spec Phase 3)
        if (typeof Extensions !== "undefined") {
            TheatreApi.setExtensions(Extensions.installed())
            Subtitles.setExtensions(Extensions.installed())
        }
        // dev harness (COLOSSEUM_OPEN_EXTENSIONS=1): boot straight into the store,
        // so smoke runs exercise the Loader (QML errors only surface on activation)
        if (typeof DevOpenExtensions !== "undefined" && DevOpenExtensions)
            win.openExtensionsPage()
        // dev harness (COLOSSEUM_OPEN_WORLD="Theatre"): boot straight into a world
        if (typeof DevOpenWorld !== "undefined" && String(DevOpenWorld).length)
            win.openWorld(String(DevOpenWorld))
        // dev harness (COLOSSEUM_SUBS_SELFTEST="movie|tt0111161"): headless proof
        // of the multi-well subtitle pipeline — logs subtitle rows per source
        if (typeof DevSubsSelfTest !== "undefined" && String(DevSubsSelfTest).length) {
            var sp = String(DevSubsSelfTest).split("|")
            if (sp[0] === "MERGETEST") {
                var m = Subtitles.selfTestMerge()
                var mBySrc = ({})
                for (var mi = 0; mi < m.length; mi++)
                    mBySrc[m[mi].source] = (mBySrc[m[mi].source] || 0) + 1
                console.log("[subs-selftest] MERGE:", m.length, "rows (expect 5: 1 shared url deduped)")
                for (var ms in mBySrc) console.log("[subs-selftest]   via", ms + ":", mBySrc[ms])
                for (var mj = 0; mj < m.length; mj++)
                    console.log("[subs-selftest]   row", m[mj].langName, m[mj].source, m[mj].url)
                return
            }
            Subtitles.fetch(sp[0], sp[1], function(list) {
                var bySource = ({})
                for (var i = 0; i < list.length; i++)
                    bySource[list[i].source] = (bySource[list[i].source] || 0) + 1
                console.log("[subs-selftest]", list.length, "subtitles for", sp[0], sp[1])
                for (var src in bySource)
                    console.log("[subs-selftest]   via", src + ":", bySource[src])
            })
        }
        // dev harness (COLOSSEUM_CATALOG_SELFTEST="movies"): headless proof of the
        // extension-shelf pipeline — logs every row the tab would render
        if (typeof DevCatalogSelfTest !== "undefined" && String(DevCatalogSelfTest).length) {
            TheatreApi.loadCatalogPage(String(DevCatalogSelfTest), function(result) {
                console.log("[catalog-selftest] tab", result.pageKey, "->", result.rows.length, "rows")
                for (var i = 0; i < result.rows.length; i++)
                    console.log("[catalog-selftest] row", i, JSON.stringify(result.rows[i].title),
                                result.rows[i].sub || "(house)", result.rows[i].items.length, "items,",
                                "first:", result.rows[i].items.length ? result.rows[i].items[0].id : "-")
            })
        }
        // dev harness (COLOSSEUM_STREAMS_SELFTEST="movie|tt123"): headless proof of
        // the multi-extension stream pipeline — logs per-extension answers and rows
        if (typeof DevStreamsSelfTest !== "undefined" && String(DevStreamsSelfTest).length) {
            var st = String(DevStreamsSelfTest).split("|")
            var exts = AddonClient.streamExtensions(Extensions.installed(), st[0], st[1])
            console.log("[streams-selftest] asking", exts.length, "extensions for", st[0], st[1])
            AddonClient.loadStreams(exts, st[0], st[1],
                function(rows) { console.log("[streams-selftest] partial:", rows.length, "rows") },
                function(rows, names) {
                    console.log("[streams-selftest] DONE:", rows.length, "rows via", names.join(","))
                    for (var i = 0; i < Math.min(rows.length, 6); i++)
                        console.log("[streams-selftest] #" + i, rows[i].addonName,
                                    rows[i].quality, rows[i].streamKind,
                                    String(rows[i].infoHash).substring(0, 24))
                })
        }
    }

    // Esc: close the series page if open, else leave a world page, else quit. Ctrl+Q always quits.
    Shortcut { sequences: ["Escape"]; onActivated: {
        if (win.playerOpen) win.closePlayer()
        else if (bookReaderLayer.active) win.closeBookReader()
        else if (bookLayer.active) win.closeBook()
        else if (biblioGenreLayer.active) win.closeBiblioGenre()
        else if (biblioGenreIndexLayer.active) win.closeBiblioGenreIndex()
        else if (searchLayer.active) win.closeSearch()
        else if (worldSearchLayer.active) win.closeWorldSearch()
        else if (downloadsLayer.active) win.closeDownloadsPage()
        else if (extensionsLayer.active) win.closeExtensionsPage()
        else if (theatreSeriesLayer.active) win.closeTheatreSeries()
        else if (seriesLayer.active) win.closeSeries()
        else if (westernLayer.active) win.closeWestern()
        else if (comicIndexLayer.active) win.closeComicArchive()
        else if (theatreGenreLayer.active) win.closeTheatreGenre()
        else if (theatreGenreIndexLayer.active) win.closeTheatreGenreIndex()
        else if (genreLayer.active) win.closeGenre()
        else if (genreIndexLayer.active) win.closeGenreIndex()
        else if (universeLayer.active) win.closeUniverse()
        else if (worldStack.current !== "") win.closeWorld()
        else Qt.quit()
    } }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: Qt.quit() }

    // Minimize the OS surface to the taskbar — "get it off my screen" WITHOUT quitting (the shell keeps
    // running, art stays warm). A frameless fullscreen window has no normal frame to land in, so when
    // Windows restores it from the taskbar we snap it straight back to fullscreen — never a stray bare
    // rectangle stuck with no titlebar to grab.
    function minimizeShell() { win.showMinimized() }
    onVisibilityChanged: if (win.visibility === Window.Windowed) win.visibility = Window.FullScreen

    // ---- navigation: open a medium's world page over the persistent wallpaper ----
    // Each visited mode keeps ONE live Loader (created on first entry, never destroyed); navigating
    // Home or between modes just toggles visibility. So returning to a mode shows the already-loaded
    // world with its covers INTACT instead of re-downloading them. Real mode pages route to their
    // own QML; unbuilt modes fall back to DemoWorld.qml.
    function worldSourceFor(medium) {
        if (medium === "Tankoban") return "TankobanWorld.qml"
        if (medium === "Theatre") return "TheatreWorld.qml"
        if (medium === "Biblio") return "BiblioWorld.qml"
        return "DemoWorld.qml"
    }
    function openWorld(medium) {
        var found = false
        for (var i = 0; i < openModes.count; i++)
            if (openModes.get(i).mode === medium) { found = true; break }
        if (!found) openModes.append({ mode: medium })   // first visit → create its keep-alive Loader
        worldStack.current = medium
        currentSurface = medium
        refreshWallpaper()
        topbar.visible = false
        page.visible = false
    }
    function closeWorld() {
        worldStack.current = ""                           // hide all worlds; none destroyed
        currentSurface = "Home"
        refreshWallpaper()
        topbar.visible = true
        page.visible = true
    }

    // ---- universe page: a cross-medium destination over the wallpaper, from the home hero ----
    //      The shell picks the right TEMPLATE by the universe's category (anime vs cinematic);
    //      the Loader reloads onto that source, so Marvel opens the CinematicPage, One Piece the
    //      anime UniversePage. ----
    function universeSourceFor(category) {
        return category === "cinematic" ? "CinematicPage.qml" : "UniversePage.qml"
    }
    function openUniverse(name) {
        universeLayer.universeName = name
        universeLayer.universeSource = win.universeSourceFor(Universes.categoryFor(name))
        if (universeLayer.item) universeLayer.item.universeName = name
        universeLayer.active = true
        topbar.visible = false
        page.visible = false
    }
    function closeUniverse() {
        universeLayer.active = false
        topbar.visible = true
        page.visible = true
    }

    function openGenre(name) {
        genreLayer.genreName = name
        if (genreLayer.active && genreLayer.item) genreLayer.item.genreName = name
        else genreLayer.active = true
    }
    function closeGenre() { genreLayer.active = false }

    // ---- genre INDEX (the "Explore" directory of all genres) — a layer below the genre page so a
    //      picked genre opens its GenrePage over the index. Reached from a genre widget's "Explore"
    //      or the genre page's "Explore" pill. ----
    function openGenreIndex() { genreIndexLayer.active = true }
    function closeGenreIndex() { genreIndexLayer.active = false }

    function openBiblioGenre(name) {
        biblioGenreLayer.genreName = name
        if (biblioGenreLayer.active && biblioGenreLayer.item) biblioGenreLayer.item.genreName = name
        else biblioGenreLayer.active = true
    }
    function closeBiblioGenre() { biblioGenreLayer.active = false }

    // ---- Biblio genre INDEX (the books "Explore" directory) — sits below BiblioGenrePage so a
    //      picked genre opens its page over the index (same layering law as the manga pair). ----
    function openBiblioGenreIndex() { biblioGenreIndexLayer.active = true }
    function closeBiblioGenreIndex() { biblioGenreIndexLayer.active = false }

    // ---- Theatre genre page + index: Theatre-owned twins of the manga pair above. The index
    //      layer sits BELOW the page layer so a picked genre opens its page over the index. ----
    function openTheatreGenre(kind, name) {
        theatreGenreLayer.mediaKind = kind
        theatreGenreLayer.genreName = name
        if (theatreGenreLayer.active && theatreGenreLayer.item) {
            theatreGenreLayer.item.mediaKind = kind
            theatreGenreLayer.item.genreName = name
        } else theatreGenreLayer.active = true
    }
    function closeTheatreGenre() { theatreGenreLayer.active = false }
    function openTheatreGenreIndex(kind) {
        theatreGenreIndexLayer.mediaKind = kind
        if (theatreGenreIndexLayer.active && theatreGenreIndexLayer.item)
            theatreGenreIndexLayer.item.mediaKind = kind
        theatreGenreIndexLayer.active = true
    }
    function closeTheatreGenreIndex() { theatreGenreIndexLayer.active = false }

    // ---- series detail: a layer over the current world page (opened from a Top-10 title tile) ----
    function openSeries(title) {
        seriesLayer.resumeSeriesId = ""
        seriesLayer.resumeChapterId = ""
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.openChapterId = ""        // leave the reader, show the chapter list
            seriesLayer.item.seriesTitle = title
        } else seriesLayer.active = true
    }
    // open a manga series AND jump straight into the reader at a saved chapter (Continue resume).
    function openSeriesAt(title, seriesId, chapterId) {
        seriesLayer.resumeSeriesId = seriesId || ""
        seriesLayer.resumeChapterId = chapterId || ""
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.seriesTitle = title
            if (seriesId) seriesLayer.item.seriesId = seriesId
            seriesLayer.item.openChapterId = chapterId || ""
        } else seriesLayer.active = true
    }
    function closeSeries() { seriesLayer.active = false }

    // ---- western-comics detail: the GetComics shelf (ComicSeries), parallel to the
    //      manga seriesLayer. Series id app-wide = "gc:<tag-slug>" — the prefix is how
    //      every shared kind:"comic" route below tells the two lanes apart. ----
    function openWestern(d) {
        westernLayer.resumeChapterId = ""
        westernLayer.title = (d && d.title) || ""
        westernLayer.tagId = (d && d.tagId) || 0
        westernLayer.tagSlug = (d && d.tag) || ""
        if (westernLayer.active && westernLayer.item) {
            westernLayer.item.openChapterId = ""       // leave the reader, show the shelf
            westernLayer.item.seriesTitle = westernLayer.title
            westernLayer.item.tagId = westernLayer.tagId
            westernLayer.item.tagSlug = westernLayer.tagSlug
        } else westernLayer.active = true
        // title-only open (Top-10 / genre tile): no tag change fires, ask the page to resolve
        if (!westernLayer.tagSlug.length && westernLayer.item) westernLayer.item.resolve()
    }
    // open a western series AND jump straight into the reader (Continue / session resume)
    function openWesternAt(title, tagSlug, chapterId) {
        westernLayer.title = title || ""
        westernLayer.tagId = 0
        westernLayer.tagSlug = tagSlug || ""
        westernLayer.resumeChapterId = chapterId || ""
        if (westernLayer.active && westernLayer.item) {
            westernLayer.item.seriesTitle = title || ""
            westernLayer.item.tagSlug = tagSlug || ""
            westernLayer.item.openChapterId = chapterId || ""
        } else westernLayer.active = true
    }
    function closeWestern() { westernLayer.active = false }

    // ---- xoxo comics: an xoxo series' issue list (peer of ComicSeries). Opened from
    //      search (data.xoxo), the world Top-Comics row, or a genre grid. ----
    function openXoxoSeries(d) {
        xoxoSeriesLayer.sid = (d && d.id) || ""
        xoxoSeriesLayer.title = (d && d.title) || ""
        xoxoSeriesLayer.cover = (d && d.cover) || ""
        if (xoxoSeriesLayer.active && xoxoSeriesLayer.item) {
            xoxoSeriesLayer.item.openChapterId = ""       // leave the reader, show the list
            xoxoSeriesLayer.item.seriesTitle = xoxoSeriesLayer.title
            xoxoSeriesLayer.item.cover = xoxoSeriesLayer.cover
            xoxoSeriesLayer.item.seriesId = xoxoSeriesLayer.sid   // set LAST — triggers reload()
        } else xoxoSeriesLayer.active = true
    }
    function closeXoxoSeries() { xoxoSeriesLayer.active = false }

    // ---- xoxo genre grid: one genre/shelf box as a paginated series grid ----
    function openXoxoGenre(box) {
        xoxoGenreLayer.box = box || ({})
        if (xoxoGenreLayer.active && xoxoGenreLayer.item) xoxoGenreLayer.item.box = xoxoGenreLayer.box
        else xoxoGenreLayer.active = true
    }
    function closeXoxoGenre() { xoxoGenreLayer.active = false }

    // ---- GetComics Archives board: the publisher/franchise taxonomy, full page ----
    function openComicArchiveBoard() { comicBoardLayer.active = true }
    function closeComicArchiveBoard() { comicBoardLayer.active = false }

    // ---- western-comics archive index: the SERIES ARCHIVES under an explore box
    //      (a publisher/franchise tag holds raw release posts — this is the middle
    //      layer that shows the /tag/ archives inside it, Hemanth's 2026-07-04 call) ----
    function openComicArchive(box) {
        comicIndexLayer.boxTitle = (box && box.name) || (box && box.title) || ""
        comicIndexLayer.tagSlug = (box && box.tag) || ""
        comicIndexLayer.boxCount = (box && box.count) || 0
        comicIndexLayer.tagId = (box && box.tagId) || 0
        if (comicIndexLayer.active && comicIndexLayer.item) {
            comicIndexLayer.item.boxTitle = comicIndexLayer.boxTitle
            comicIndexLayer.item.tagSlug = comicIndexLayer.tagSlug
            comicIndexLayer.item.boxCount = comicIndexLayer.boxCount
            comicIndexLayer.item.tagId = comicIndexLayer.tagId   // change fires resolve()
        } else comicIndexLayer.active = true
    }
    function closeComicArchive() { comicIndexLayer.active = false }


    // ---- Theatre detail: its own layer (Cinemeta meta + Torrentio sources), parallel to series ----
    function openTheatreSeries(item) {
        theatreSeriesLayer.pendingItem = item
        if (theatreSeriesLayer.active && theatreSeriesLayer.item) theatreSeriesLayer.item.itemData = item
        else theatreSeriesLayer.active = true
    }
    function closeTheatreSeries() { theatreSeriesLayer.active = false }

    // ---- video player: a fullscreen layer over everything; kept alive once opened so mpv
    //      isn't torn down/recreated each play (avoids the use-after-free teardown trap). ----
    property bool playerOpen: false
    // The movie session the player minimized while still loaded. Reopening it from the
    // taskbar finds the stream warm — we resume in place instead of re-streaming.
    property string warmPlayerSessionId: ""
    readonly property bool immersiveSurfaceOpen: win.playerOpen
        || bookReaderLayer.active
        || (seriesLayer.active && seriesLayer.item && seriesLayer.item.openChapterId.length > 0)
        || (westernLayer.active && westernLayer.item && westernLayer.item.openChapterId.length > 0)

    // ---- season-download resolver: a promoted queue job carries only the episode's
    //      stream id; we pick the rank-best Torrentio stream and feed back the local
    //      engine URL. Deferred while the player streams (one engine, playback wins). ----
    property var pendingResolves: []
    property var pendingFeeds: ({})   // "hash:idx" -> job id, answered by onFetchReady
    function resolveDownloadJob(id, streamId, mediaType) {
        Torrentio.loadStreams(mediaType, streamId, function(rows) {
            if (!rows || !rows.length) {
                Download.failJob(id, "No stream found for this episode.")
                return
            }
            var best = rows[0]
            // prefetch (NOT play): the url arrives via onFetchReady once the engine
            // is genuinely up. The old synchronous streamUrl() read raced a cold
            // engine and fed "" — the job then sat "resolving" forever (the
            // nothing-downloads wedge, diagnosed 2026-07-05). play() is also the
            // player's signal — prefetch keeps downloads out of mpv's ears.
            var key = (best.infoHash || "").toLowerCase() + ":" + (best.fileIdx || 0)
            win.pendingFeeds[key] = id
            Stream.prefetch(best.infoHash, best.fileIdx || 0)
        })
    }
    Connections {
        target: typeof Stream !== "undefined" ? Stream : null
        function onFetchReady(url, infoHash, fileIdx) {
            var key = infoHash.toLowerCase() + ":" + fileIdx
            var id = win.pendingFeeds[key]
            if (id === undefined)
                return
            delete win.pendingFeeds[key]
            Download.feedUrl(id, url)
        }
        function onStreamError(message) {
            // Engine went away mid-warmup: fail the waiting jobs honestly (Retry
            // re-resolves) instead of leaving them wedged in "resolving".
            var any = false
            for (var key in win.pendingFeeds) {
                Download.failJob(win.pendingFeeds[key], message)
                any = true
            }
            if (any)
                win.pendingFeeds = ({})
        }
    }
    Connections {
        target: typeof Download !== "undefined" ? Download : null
        function onNeedResolve(id, streamId, mediaType) {
            if (win.playerOpen) { win.pendingResolves.push({ id: id, sid: streamId, mt: mediaType }); return }
            win.resolveDownloadJob(id, streamId, mediaType)
        }
    }
    Connections {
        target: win
        function onPlayerOpenChanged() {
            if (win.playerOpen || !win.pendingResolves.length)
                return
            var p = win.pendingResolves.shift()
            win.resolveDownloadJob(p.id, p.sid, p.mt)
        }
    }

    // ---- Downloads page: the taskbar's own full page over everything non-immersive ----
    function openDownloadsPage() {
        downloadsLayer.active = true
        taskbar.open = false
    }
    function closeDownloadsPage() { downloadsLayer.active = false }

    // ---- Extensions page: the store, entered from the taskbar beside Downloads ----
    function openExtensionsPage() {
        extensionsLayer.active = true
        taskbar.open = false
    }
    function closeExtensionsPage() { extensionsLayer.active = false }
    function routeDownloadItem(item) {
        win.closeDownloadsPage()
        if (item.world === "theatre") {
            // Downloaded videos take the SAME check-in as streams (spec 2026-07-06 parity):
            // a real session (taskbar tile, honest minimize) with identity (Continue store,
            // online subtitles). Resume where a previous watch left off, if the store knows one.
            var prog = Progress.get("video", item.id || "")
            var pos = (prog && prog.resume && Number(prog.resume.position || 0) > 0)
                      ? Number(prog.resume.position) : 0
            win.openLocalVideoSession({ "path": item.path, "id": item.id || "",
                                        "title": item.title || "", "art": item.art || "",
                                        "kind": item.kind || "", "position": pos })
        } else if (item.world === "biblio") {
            win.openBookSession(item.path, { "title": item.title || "" })
        } else if (item.kind === "comic") {
            win.openWesternAt(item.seriesTitle, String(item.seriesId).replace(/^gc:/, ""), item.id)
        } else {
            win.openSeriesAt(item.seriesTitle, item.seriesId, item.id)
        }
    }
    function routeDownloadWorld(worldKey) {
        win.closeDownloadsPage()
        var medium = worldKey === "tankoban" ? "Tankoban"
                   : worldKey === "biblio" ? "Biblio" : "Theatre"
        win.openWorld(medium)
    }

    function openPlayer(infoHash, fileIdx, title, backdrop, subType, subId, streamCandidates, playbackContext) {
        if (!playerLayer.active) playerLayer.active = true
        win.playerOpen = true
        // `backdrop` is the poster url; subType/subId (e.g. "movie"/"tt123" or "series"/"tt123:1:2")
        // let the player fetch online subtitles for this exact title/episode.
        playerLayer.item.playTorrent(infoHash, fileIdx, title, backdrop, subType, subId, streamCandidates || [], playbackContext || ({}))
    }
    function closePlayer() {
        if (playerLayer.item) playerLayer.item.stop()
        win.playerOpen = false
    }

    // ---- book detail: Biblio's own dust-jacket page, a layer over the world ----
    function openBook(b) {
        bookLayer.book = b
        bookLayer.active = true
    }
    function closeBook() { bookLayer.active = false }

    // ---- the reader: foliate EPUB reader over everything (download-fed, never a stream) ----
    function openBookReader(path, book) {
        if (!path) return
        bookReaderLayer.bookPath = path
        bookReaderLayer.bookMeta = book || ({})
        if (bookReaderLayer.active && bookReaderLayer.item) bookReaderLayer.item.open(path, book || ({}))
        else bookReaderLayer.active = true
    }
    function closeBookReader() { bookReaderLayer.active = false }

    // ---- search: a layer over the world. Biblio has its own rich surface; Tankoban + Theatre use the
    //      generic SearchSurface fed by their own source (AniList / Cinemeta). ----
    function openSearch() {
        var w = worldStack.current
        if (w === "Biblio") { searchLayer.active = true; return }
        if (w === "Tankoban") {
            worldSearchLayer.searchMode = "Tankoban"
            worldSearchLayer.placeholder = "Search manga…"
            worldSearchLayer.active = true
        } else if (w === "Theatre") {
            worldSearchLayer.searchMode = "Theatre"
            worldSearchLayer.placeholder = "Search movies & series…"
            worldSearchLayer.active = true
        }
    }
    function closeSearch() { searchLayer.active = false }
    function closeWorldSearch() { worldSearchLayer.active = false }
    function routeWorldSearchItem(data) {
        win.closeWorldSearch()
        if (worldSearchLayer.searchMode === "Tankoban") {
            if (data && data.xoxo) win.openXoxoSeries(data)   // xoxo issue list (peer source)
            else if (data && data.western) win.openWestern(data)   // GetComics shelf, not WeebCentral
            else win.openSeries(data.title)
        } else if (worldSearchLayer.searchMode === "Theatre") win.openTheatreSeries(data)
    }

    function openWallpaperSearch(world) {
        wallpaperLayer.targetWorld = world || currentSurface || "Home"
        wallpaperLayer.active = true
    }
    function closeWallpaperSearch() { wallpaperLayer.active = false }

    // ---- Continue card has TWO actions: the center icon RESUMES into the content; clicking
    //      elsewhere opens the SERIES / DETAIL view. Both use the resume payload each world wrote. ----
    //  resume (center play/read icon):
    function resumeContinue(entry) {
        if (!entry) return
        var r = entry.resume || ({})
        var title = entry.title || entry.caption || ""
        if (entry.kind === "video") {
            // downloaded file first: resume the LOCAL copy at position, never a stream fetch
            if (r.localPath && String(r.localPath).length)
                win.openLocalVideoSession({ "path": r.localPath, "id": entry.id || "",
                                            "title": title, "art": entry.cover || "",
                                            "kind": r.subType === "series" ? "episode" : "movie",
                                            "position": r.position || 0 })
            else if (r.infoHash) win.openMovieSession(r.infoHash, r.fileIdx || 0, title, entry.cover || "", r.subType || "", r.subId || "", [], {}, r.position || 0)
        } else if (entry.kind === "manga" || entry.kind === "comic") {
            win.openComicSession(title, entry.id || "", r.chapterId || "")
        } else if (entry.kind === "book") {
            if (r.path) win.openBookSession(r.path, r.book ? r.book : entry)
            else win.openBook(r.book ? r.book : entry)
        }
    }
    //  detail (click anywhere else on the card): the series / movie / book page.
    function detailContinue(entry) {
        if (!entry) return
        var title = entry.title || entry.caption || ""
        if (entry.kind === "video") {
            var id = (entry.id || "").split(":")[0]                      // base tt id (strip episode suffix)
            if (id.indexOf("tt") !== 0) {
                // Non-Cinemeta id: anime comes from Jikan (id "mal:<malId>") and resolves through
                // the Kitsu addon. Its episode id is PREFIX:NUM(:season:episode); the series id is
                // PREFIX:NUM. Open the SAME series view shows/movies use (TheatreSeries), not the
                // universe landing page.
                var p = String(entry.id || "").split(":")
                if (p.length >= 2 && /^(mal|kitsu|anilist|anidb)$/.test(p[0])) {
                    win.openTheatreSeries({ id: p[0] + ":" + p[1],
                                            type: entry.type === "movie" ? "movie" : "series",
                                            title: title, cover: entry.cover || "" })
                    return
                }
                win.resumeContinue(entry); return   // raw torrent, no detail page
            }
            // resolve movie vs series live from Cinemeta (probe series first; a hit → series, else movie),
            // then open the Theatre detail. No stored type needed, so existing entries work too.
            TheatreApi.loadMeta("series", id, function(meta) {
                win.openTheatreSeries({ id: id, type: meta ? "series" : "movie",
                                        title: title, cover: entry.cover || "" })
            })
        } else if (entry.kind === "manga" || entry.kind === "comic") {
            if (String(entry.id || "").indexOf("gc:") === 0)
                win.openWestern({ title: title, tag: String(entry.id).slice(3) })
            else win.openSeries(title)                                   // the series page (chapter list)
        } else if (entry.kind === "book") {
            win.openBook(entry.resume && entry.resume.book ? entry.resume.book : entry)
        }
    }

    // ===== OS-shell session engine (Approach 2: only the active surface is instantiated) =====
    // The UI opens content by registering a SESSION; Sessions.activeChanged then drives the
    // capture -> teardown -> build -> restore switch. contentKind picks the surface.

    // UI entry points (replace direct open* calls from cards / world pages):
    function openMovieSession(infoHash, fileIdx, title, backdrop, subType, subId, streamCandidates, playbackContext, position) {
        Sessions.openOrSwitch({
            "appType": "theatre", "contentKind": "movie", "title": title || "Movie",
            "target": { "infoHash": infoHash, "fileIdx": fileIdx || 0, "title": title || "",
                        "backdrop": backdrop || "", "subType": subType || "", "subId": subId || "",
                        "streamCandidates": streamCandidates || [], "playbackContext": playbackContext || ({}),
                        "position": position || 0 }
        })
    }
    // A downloaded video's session: dedup key = target.id (the stream id, e.g. "tt123:1:2"),
    // so re-opening the same episode reuses its tile. `position` rides along for first-open
    // resume; a fresher captured position (minimize) wins via restoreState.
    function openLocalVideoSession(v) {
        Sessions.openOrSwitch({
            "appType": "theatre", "contentKind": "movie", "title": v.title || "Video",
            "target": { "localPath": v.path, "id": v.id || "", "title": v.title || "",
                        "art": v.art || "", "kind": v.kind || "", "position": v.position || 0 }
        })
    }
    function openComicSession(title, seriesId, chapterId) {
        Sessions.openOrSwitch({
            "appType": "tankoban", "contentKind": "comic", "title": title || "Comic",
            "target": { "title": title || "", "seriesId": seriesId || "", "chapterId": chapterId || "" }
        })
    }
    function openBookSession(path, book) {
        if (!path) return
        var b = book || ({})
        Sessions.openOrSwitch({
            "appType": "biblio", "contentKind": "book", "title": b.title || "Book",
            "target": { "path": path, "book": b, "id": (b.id !== undefined ? ("" + b.id) : path) }
        })
    }

    // ---- window-verbs for the reader/player chrome (Windows-taskbar vocabulary, 2026-07-04) ----
    // minimize = capture the exact spot, drop the surface, KEEP the session in the taskbar,
    // land on the world behind. close = the session is gone. Back keeps its old meaning.
    function closeSession(id) {
        if (!id) return
        var rec = Sessions.get(id)
        // the store removes the record BEFORE the switch glue can look it up, so the live
        // surface must come down here when closing the active one (else it lingers on screen).
        if (id === Sessions.activeId) win.teardownSession(rec)
        // a real close ends the stream for good — minimize keeps the movie warm, close does not.
        if (rec && rec.contentKind === "movie") {
            if (playerLayer.item) playerLayer.item.stop()
            if (win.warmPlayerSessionId === id) win.warmPlayerSessionId = ""
        }
        Sessions.close(id)
    }
    function minimizePlayer() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "movie") Sessions.switchTo("")
        else win.closePlayer()                       // not session-run (legacy path): just drop it
    }
    function closePlayerSession() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "movie") win.closeSession(rec.id)
        else win.closePlayer()
    }
    function minimizeComicReader() {
        var rec = Sessions.get(Sessions.activeId)
        if (!(rec && rec.contentKind === "comic")) {
            // reading began from a browse (no session yet) — register it from the live reader
            var w = westernLayer.active ? westernLayer.item : null
            if (w && w.openChapterId) {
                win.openComicSession(w.seriesTitle, w.seriesId, w.openChapterId)   // seriesId = "gc:<slug>"
            } else {
                var s = seriesLayer.item
                if (!s || !s.openChapterId) { win.closeSeries(); return }
                win.openComicSession(s.seriesTitle, s.seriesId, s.openChapterId)
            }
        }
        Sessions.switchTo("")
    }
    function closeComicReader() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "comic") win.closeSession(rec.id)
        else if (westernLayer.active && westernLayer.item && westernLayer.item.openChapterId.length) win.closeWestern()
        else win.closeSeries()
    }
    function minimizeBookReader() {
        var rec = Sessions.get(Sessions.activeId)
        if (!(rec && rec.contentKind === "book")) {
            if (!bookReaderLayer.bookPath) { win.closeBookReader(); return }
            win.openBookSession(bookReaderLayer.bookPath, bookReaderLayer.bookMeta)
        }
        Sessions.switchTo("")
    }
    function closeBookReaderSession() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "book") win.closeSession(rec.id)
        else win.closeBookReader()
    }

    // dispatcher: build the active surface from a record (+ restore its saved state).
    function activateSession(rec) {
        if (!rec || !rec.id) return
        var t = rec.target || ({})
        var st = rec.savedState || ({})
        currentSurface = wallpaperWorldForSession(rec)
        refreshWallpaper()
        if (rec.contentKind === "movie") {
            if (!playerLayer.active) playerLayer.active = true
            win.playerOpen = true
            if (win.warmPlayerSessionId === rec.id && playerLayer.item) {
                // warm: the stream was kept alive on minimize — resume in place, no re-stream.
                playerLayer.item.resumeFromMinimize()
            } else {
                if (t.localPath && String(t.localPath).length)
                    playerLayer.item.playLocalFile(t)   // downloaded file: stream-grade identity
                else
                    playerLayer.item.playTorrent(t.infoHash, t.fileIdx || 0, t.title, t.backdrop, t.subType, t.subId,
                                                 t.streamCandidates || [], t.playbackContext || ({}))
                // Continue-Watching first-open resume: a fresh open has NO savedState, so the saved
                // position rides in target.position (the tile's ProgressStore value). A fresher
                // in-session captured position (minimize) still wins. [fix 2026-07-07: streamed CW
                // resumed at 0 because playTorrent takes no position and savedState was empty.]
                var resumeSt = (st && Number(st.position) > 0) ? st : { "position": Number(t.position) || 0 }
                if (playerLayer.item.restoreState) playerLayer.item.restoreState(resumeSt)   // precision: Task 5
            }
            win.warmPlayerSessionId = rec.id
        } else if (rec.contentKind === "comic") {
            if (String(t.seriesId || "").indexOf("gc:") === 0) {
                // western: the GetComics shelf hosts the reader
                win.openWesternAt(t.title, String(t.seriesId).slice(3), (st.chapterId || t.chapterId || ""))
                if (westernLayer.item && westernLayer.item.restoreState) westernLayer.item.restoreState(st)
                return
            }
            seriesLayer.resumeSeriesId = t.seriesId || ""
            seriesLayer.resumeChapterId = (st.chapterId || t.chapterId || "")
            seriesLayer.title = t.title
            if (seriesLayer.active && seriesLayer.item) {
                seriesLayer.item.seriesTitle = t.title
                if (t.seriesId) seriesLayer.item.seriesId = t.seriesId
                seriesLayer.item.openChapterId = (st.chapterId || t.chapterId || "")
            } else seriesLayer.active = true
            if (seriesLayer.item && seriesLayer.item.restoreState) seriesLayer.item.restoreState(st)  // Task 4
        } else if (rec.contentKind === "book") {
            bookReaderLayer.bookPath = t.path
            bookReaderLayer.bookMeta = t.book || ({})
            if (bookReaderLayer.active && bookReaderLayer.item) bookReaderLayer.item.open(t.path, t.book || ({}))
            else bookReaderLayer.active = true
            // book precision: foliate auto-restores its own CFI on reopen of the same path (Task 6).
        }
    }
    // capture the live outgoing surface's state (called BEFORE teardown).
    function captureSession(rec) {
        if (!rec || !rec.id) return ({})
        if (rec.contentKind === "movie" && playerLayer.item && playerLayer.item.captureState) return playerLayer.item.captureState()
        if (rec.contentKind === "comic") {
            var lay = String((rec.target || ({})).seriesId || "").indexOf("gc:") === 0 ? westernLayer : seriesLayer
            return (lay.item && lay.item.captureState) ? lay.item.captureState() : ({})
        }
        if (rec.contentKind === "book"  && bookReaderLayer.item && bookReaderLayer.item.captureState) return bookReaderLayer.item.captureState()
        return ({})
    }
    // tear the outgoing surface down. Player: stop media but KEEP the mpv host (use-after-free guard).
    function teardownSession(rec) {
        if (!rec || !rec.id) return
        if (rec.contentKind === "movie") {
            // minimize keeps the stream WARM: pause + hide, never stop(), so reopening from
            // the taskbar resumes instantly with no re-stream (Hemanth 2026-07-07, option a).
            // The hard stop lives in closeSession — only a real close ends the stream.
            if (playerLayer.item) playerLayer.item.suspendForMinimize()
            win.warmPlayerSessionId = rec.id
            win.playerOpen = false
        } else if (rec.contentKind === "comic") {
            if (String((rec.target || ({})).seriesId || "").indexOf("gc:") === 0) westernLayer.active = false
            else seriesLayer.active = false
        } else if (rec.contentKind === "book")  {
            bookReaderLayer.active = false
        }
    }

    // ---- design tokens (the skin: glass is the constant; gold is sparing) ----
    Theme { id: theme }

    // ---- bundled editorial serif (the theme's target display face: theme.display = "Fraunces") ----
    FontLoader { source: "../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { source: "../assets/fonts/Fraunces-Italic.ttf" }

    // ---- player HUD face: Switzer (Harbor-parity), bundled weights (theme.hud) ----
    FontLoader { source: "../assets/fonts/Switzer-Regular.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Medium.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Semibold.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Bold.otf" }

    // ---- HUD fallback face: Inter statics (theme.hud flip target). STATICS ON PURPOSE:
    // a variable TTF registers under its typographic name ("Inter Variable"), so asking
    // for "Inter" silently falls back to Tahoma on Windows (probe-proven 2026-07-08).
    // The statics register as plain "Inter" and weight-match across files. ----
    FontLoader { source: "../assets/fonts/Inter-Regular.otf" }
    FontLoader { source: "../assets/fonts/Inter-Medium.otf" }
    FontLoader { source: "../assets/fonts/Inter-SemiBold.otf" }
    FontLoader { source: "../assets/fonts/Inter-Bold.otf" }

    // =====================================================================
    // BACKDROP — the persistent wallpaper everything composites over.
    // =====================================================================
    Item {
        id: wall
        anchors.fill: parent
        // Real OS wallpaper — a placeholder PICK (Windows 11 "Captured Motion"; its translucent
        // glass-ribbon motif echoes our material, and it's dark enough for the glass to read).
        // Swap from the parked personalization gallery later. Glass composites over WHATEVER sits in
        // `wall`, so the Image "just works" — and it pops against the chrome instead of reading as an app.
        Image {
            anchors.fill: parent
            source: win.wallpaperSource
            fillMode: Image.PreserveAspectCrop
            cache: true
        }
        // gentle global vignette so chrome + text read against the wallpaper, bright or dark
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.34) }
                GradientStop { position: 0.5; color: Qt.rgba(0,0,0,0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.46) }
            }
        }
    }

    // ---- reusable: a clickable row header (the nav-in to a world). Continue isn't a world,
    //      so it opts out with navigable:false (no chevron, no click). ----
    component RowHeader: Item {
        id: rh
        property string title
        property bool navigable: true
        signal clicked()
        implicitWidth: rhRow.implicitWidth
        implicitHeight: rhRow.implicitHeight
        Row {
            id: rhRow
            spacing: 8
            Text {
                text: rh.title
                color: (rh.navigable && rhMa.containsMouse) ? theme.ink : theme.inkDim
                font.family: theme.display; font.pixelSize: 23
            }
            Text {
                text: "›"
                visible: rh.navigable
                color: theme.gold; font.pixelSize: 22
                opacity: rhMa.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }
        MouseArea {
            id: rhMa; anchors.fill: parent
            hoverEnabled: rh.navigable
            cursorShape: rh.navigable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (rh.navigable) rh.clicked()
        }
    }

    // (The unified Continue card now lives in ContinueTile.qml — one component worn two ways,
    //  shared with the world pages' ContinueRow. Spec: haven docs/superpowers/specs/
    //  2026-07-05-colosseum-continue-tiles-design.md.)

    // (PortraitTile · Pill · SysIcon · the top bar now live in shared sibling files:
    //  PortraitTile.qml and TopBar.qml — reused by the world-page template.)

    // =====================================================================
    // FOREGROUND
    // =====================================================================

    // ---- 1. TOP BAR (fixed, glass over wallpaper) — shared shell chrome.
    //      activeMedium "" → HOME: no pill selected (the no-selection rule). Tapping a pill
    //      enters that world. ----
    TopBar {
        id: topbar
        z: 20
        backdrop: wall
        activeMedium: ""
        x: theme.margin; y: 30
        width: win.width - theme.margin * 2
        onMediumSelected: (medium) => win.openWorld(medium)
        onWallpaperClicked: win.openWallpaperSearch("Home")
        onMinimizeClicked: win.minimizeShell()
        onPowerClicked: Qt.quit()
    }

    // ---- pinned top bar is above; everything below SCROLLS (vertical wheel/drag) ----
    Flickable {
        id: page
        z: 0
        anchors.left: parent.left; anchors.right: parent.right
        y: 96
        height: win.height - 96
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 40
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            x: theme.margin
            width: win.width - theme.margin * 2
            topPadding: 10
            spacing: 30

            // ---- 2. UNIVERSE HERO — a real cycling carousel of the universe collection ----
            //      Swipe/drag between universes · dots track + jump · auto-advances. Real banner
            //      key-art (TMDB / AniList, disk-cached). Data: Universes.universes.
            Glass {
                id: hero
                backdrop: wall
                track: page.contentY
                width: parent.width; height: 340; radius: 20
                tint: 0.06

                SwipeView {
                    id: heroView
                    anchors.fill: parent
                    clip: true
                    Repeater {
                        model: Universes.universes
                        delegate: Item {
                            required property var modelData

                            // banner key-art (solid content), rounded to the panel; the IP color
                            // stands in while it loads, then a left-weighted scrim keeps text legible.
                            Rectangle {
                                anchors.fill: parent; radius: hero.radius; clip: true
                                color: modelData.c1 ? modelData.c1 : "#1a1410"
                                Image {
                                    anchors.fill: parent
                                    source: modelData.banner
                                    asynchronous: true; cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    opacity: status === Image.Ready ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: 300 } }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.86) }
                                        GradientStop { position: 0.52; color: Qt.rgba(0,0,0,0.42) }
                                        GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.06) }
                                    }
                                }
                            }

                            // content (chrome over the art)
                            Column {
                                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 44
                                spacing: 12
                                Text { text: "UNIVERSE"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
                                Text { text: modelData.name; color: theme.ink; font.family: theme.display; font.pixelSize: 48 }
                                Text {
                                    text: modelData.blurb
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; width: 500; wrapMode: Text.WordWrap
                                }
                                // medium counts as an inline editorial metadata line (bright count · dim
                                // medium) — NOT glass pills, NO gold separators. Transparent "tablet" chips
                                // read cheap over busy banner art (Hemanth, 2026-06-27).
                                Row {
                                    spacing: 22
                                    Repeater {
                                        model: modelData.chips
                                        delegate: Text {
                                            required property var modelData
                                            textFormat: Text.StyledText
                                            font.family: theme.ui; font.pixelSize: 15
                                            text: {
                                                var s = modelData.t
                                                var i = s.indexOf(" ")
                                                var first = i < 0 ? s : s.substring(0, i)
                                                // bold the leading COUNT only; a medium name with no
                                                // number (incl. multi-word like "Graphic Novel") stays
                                                // one uniform weight — never half-bold.
                                                if (!/^\d/.test(first))
                                                    return "<font color='#c9c8d0'>" + s + "</font>"
                                                return "<b><font color='#f7f7f5'>" + first +
                                                       "</font></b> <font color='#c9c8d0'>" + s.substring(i + 1) + "</font>"
                                            }
                                        }
                                    }
                                }
                                Row {
                                    spacing: 12; topPadding: 6
                                    Rectangle {
                                        id: exploreBtn
                                        radius: 12; height: 46; width: exploreRow.implicitWidth + 44
                                        gradient: Gradient {
                                            GradientStop { position: 0; color: exMa.containsMouse ? Qt.rgba(1,1,1,0.23) : Qt.rgba(1,1,1,0.14) }
                                            GradientStop { position: 1; color: exMa.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05) }
                                        }
                                        border.width: 1
                                        border.color: exMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.85) : Qt.rgba(1,1,1,0.26)
                                        Behavior on border.color { ColorAnimation { duration: 160 } }
                                        Row {
                                            id: exploreRow; anchors.centerIn: parent; spacing: 10
                                            Text { text: "Explore the universe"; color: theme.ink
                                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                                anchors.verticalCenter: parent.verticalCenter }
                                            Text { text: "→"; color: theme.gold; font.pixelSize: 16
                                                anchors.verticalCenter: parent.verticalCenter
                                                transform: Translate {
                                                    x: exMa.containsMouse ? 3 : 0
                                                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                                                }
                                            }
                                        }
                                        MouseArea {
                                            id: exMa; anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: win.openUniverse(Universes.universes[heroView.currentIndex].name)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // dots — track the current universe, click to jump (overlay above the SwipeView)
                Row {
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 30
                    spacing: 8; z: 5
                    Repeater {
                        model: Universes.universes.length
                        delegate: Rectangle {
                            required property int index
                            width: index === heroView.currentIndex ? 22 : 8; height: 8; radius: 4
                            color: index === heroView.currentIndex ? theme.gold : Qt.rgba(1,1,1,0.35)
                            Behavior on width { NumberAnimation { duration: 150 } }
                            MouseArea {
                                anchors.fill: parent; anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: heroView.currentIndex = index
                            }
                        }
                    }
                }

                // gentle auto-advance through the collection
                Timer {
                    interval: 6500; running: true; repeat: true
                    onTriggered: heroView.currentIndex = (heroView.currentIndex + 1) % Universes.universes.length
                }
            }

            // ---- 3. CONTINUE (one unified row, all mediums mixed; scrolls horizontally) ----
            //      Real resume data from the Progress store; hidden entirely until there's
            //      something to resume. (Naming Progress.revision keeps the binding live.)
            Column {
                id: contCol
                width: parent.width
                spacing: 14
                // watched episodes sink below unfinished entries (both halves keep recency order)
                property var contItems: (Progress.revision, (function() {
                    var a = Progress.recent("", 12)
                    return a.filter(function(e) { return e.watched !== true })
                            .concat(a.filter(function(e) { return e.watched === true }))
                })())
                visible: contItems.length > 0
                RowHeader { title: "Continue"; navigable: false }   // unified resume row, not a world
                Flickable {
                    id: contFlick
                    width: parent.width; height: 148
                    contentWidth: contRow.width; contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: contRow
                        spacing: 18
                        Repeater {
                            model: contCol.contItems
                            delegate: ContinueTile {
                                required property var modelData
                                variant: "home"
                                entry: modelData
                                backdrop: wall
                                track: page.contentY + contFlick.contentX
                                onResumeRequested: win.resumeContinue(modelData)
                                onDetailRequested: win.detailContinue(modelData)
                                onRemoveRequested: Progress.forget(modelData.kind, modelData.id)
                            }
                        }
                    }
                }
            }

            // ---- 4. MODE-INTRO WIDGETS — the board that introduces each app AND shows what's inside.
            //      First prototype: Tankoban as a BOOKSHELF (manga covers standing on a shelf ledge).
            //      The other modes get their own widget forms next; this is the shape to react to.
            Bookshelf {
                backdrop: wall
                track: page.contentY
                width: parent.width
                mangaBooks: Catalog.topManga
                comicsBooks: Catalog.topComics
                onClicked: win.openWorld("Tankoban")
                onBookClicked: win.openWorld("Tankoban")
            }

            // Theatre = the film-strip, Biblio = the reading desk (mock-reviewed 2026-07-04;
            // both self-load their data, so the board wiring stays declarative).
            TheatreStrip {
                backdrop: wall
                track: page.contentY
                width: parent.width
                onClicked: win.openWorld("Theatre")
            }

            ReadingDesk {
                backdrop: wall
                track: page.contentY
                width: parent.width
                onClicked: win.openWorld("Biblio")
                onGenrePicked: (name) => { win.openWorld("Biblio"); win.openBiblioGenre(name) }
            }

            Item { width: 1; height: 16 }   // bottom breathing room
        }
    }

    // ---- world pages: one keep-alive Loader PER visited mode, stacked over the home on the SAME
    //      wallpaper. worldStack.current picks which is visible; "" = home. Kept alive so covers
    //      don't re-fetch on return (the home's top bar + scroll hide while a world is up). ----
    ListModel { id: openModes }
    Item {
        id: worldStack
        anchors.fill: parent
        property string current: ""                      // "" = home; else the visible mode
        Repeater {
            model: openModes
            delegate: Loader {
                required property string mode
                anchors.fill: parent
                visible: worldStack.current === mode
                active: true
                source: win.worldSourceFor(mode)
                onLoaded: {
                    item.medium = mode
                    item.backdrop = wall
                    item.homeRequested.connect(win.closeWorld)
                    item.mediumSelected.connect(win.openWorld)
                    item.seriesRequested.connect(win.openSeries)
                    item.bookRequested.connect(win.openBook)
                    item.genreRequested.connect(win.openGenre)
                    if (item.genreIndexRequested) item.genreIndexRequested.connect(win.openGenreIndex)
                    var westernSignal = item["westernRequested"]
                    if (westernSignal) westernSignal.connect(function(title) { win.openWestern({ title: title }) })
                    var westernExploreSignal = item["westernExploreRequested"]
                    if (westernExploreSignal) westernExploreSignal.connect(win.openComicArchive)
                    var xoxoSeriesSignal = item["xoxoSeriesRequested"]
                    if (xoxoSeriesSignal) xoxoSeriesSignal.connect(win.openXoxoSeries)
                    var xoxoGenreSignal = item["xoxoGenreRequested"]
                    if (xoxoGenreSignal) xoxoGenreSignal.connect(win.openXoxoGenre)
                    var comicBoardSignal = item["comicArchiveBoardRequested"]
                    if (comicBoardSignal) comicBoardSignal.connect(win.openComicArchiveBoard)
                    var biblioGenreSignal = item["biblio" + "GenreRequested"]
                    if (biblioGenreSignal) biblioGenreSignal.connect(win.openBiblioGenre)
                    var biblioGenreIndexSignal = item["biblio" + "GenreIndexRequested"]
                    if (biblioGenreIndexSignal) biblioGenreIndexSignal.connect(win.openBiblioGenreIndex)
                    if (item.continueResumeRequested) item.continueResumeRequested.connect(win.resumeContinue)
                    if (item.continueDetailRequested) item.continueDetailRequested.connect(win.detailContinue)
                    if (item.wallpaperClicked) item.wallpaperClicked.connect(function() { win.openWallpaperSearch(mode) })
                    if (mode === "Theatre") {
                        var theatreSignal = item["theatre" + "ItemRequested"]
                        if (theatreSignal) theatreSignal.connect(win.openTheatreSeries)
                        var tgSignal = item["theatre" + "GenreRequested"]
                        if (tgSignal) tgSignal.connect(win.openTheatreGenre)
                        var tgiSignal = item["theatre" + "GenreIndexRequested"]
                        if (tgiSignal) tgiSignal.connect(win.openTheatreGenreIndex)
                    }
                    item.searchClicked.connect(win.openSearch)
                    item.minimizeClicked.connect(win.minimizeShell)
                    item.powerClicked.connect(function() { Qt.quit() })
                }
            }
        }
    }

    // ---- universe page layer: opened from the home hero "Explore the universe". Its source is the
    //      per-category template (anime UniversePage / cinematic CinematicPage), chosen in
    //      openUniverse(). Signal sets differ per template, so each optional connect is guarded. ----
    Loader {
        id: universeLayer
        anchors.fill: parent
        z: 40
        active: false
        visible: active
        property string universeName: ""
        property string universeSource: "UniversePage.qml"
        source: universeSource
        onLoaded: {
            item.backdrop = wall
            item.universeName = universeLayer.universeName
            item.backRequested.connect(win.closeUniverse)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            if (item.searchClicked) item.searchClicked.connect(win.openSearch)
            if (item.seriesRequested) item.seriesRequested.connect(win.openSeries)   // anime template only
            if (item.watchRequested) item.watchRequested.connect(win.openTheatreSeries)
        }
    }

    Loader {
        id: genreLayer
        anchors.fill: parent
        z: 45
        active: false
        visible: active
        property string genreName: ""
        source: "GenrePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.genreName = genreLayer.genreName
            item.backRequested.connect(win.closeGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.seriesRequested.connect(win.openSeries)
            item.exploreRequested.connect(function() { win.closeGenre(); win.openGenreIndex() })
        }
    }

    // ---- genre INDEX layer (the "Explore" directory). z below genreLayer so picking a genre opens
    //      its page over the index. ----
    Loader {
        id: genreIndexLayer
        anchors.fill: parent
        z: 44
        active: false
        visible: active
        source: "GenreIndex.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeGenreIndex)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.genrePicked.connect(win.openGenre)
        }
    }

    // Biblio genre INDEX layer — same z as the page layer but declared FIRST, so the
    // later-declared BiblioGenrePage paints over it when both are up.
    Loader {
        id: biblioGenreIndexLayer
        anchors.fill: parent
        z: 46
        active: false
        visible: active
        source: "BiblioGenreIndex.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeBiblioGenreIndex)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.genrePicked.connect(win.openBiblioGenre)
        }
    }

    Loader {
        id: biblioGenreLayer
        anchors.fill: parent
        z: 46
        active: false
        visible: active
        property string genreName: ""
        source: "BiblioGenrePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.genreName = biblioGenreLayer.genreName
            item.backRequested.connect(win.closeBiblioGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.bookRequested.connect(win.openBook)
            if (item.exploreRequested) item.exploreRequested.connect(function() {
                win.closeBiblioGenre(); win.openBiblioGenreIndex()
            })
        }
    }

    Loader {
        id: theatreGenreLayer
        anchors.fill: parent
        z: 48
        active: false
        visible: active
        property string mediaKind: "movie"
        property string genreName: ""
        source: "TheatreGenrePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.mediaKind = theatreGenreLayer.mediaKind
            item.genreName = theatreGenreLayer.genreName
            item.backRequested.connect(win.closeTheatreGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.itemRequested.connect(win.openTheatreSeries)
            item.exploreRequested.connect(function() {
                win.closeTheatreGenre(); win.openTheatreGenreIndex(theatreGenreLayer.mediaKind)
            })
        }
    }

    Loader {
        id: theatreGenreIndexLayer
        anchors.fill: parent
        z: 47
        active: false
        visible: active
        property string mediaKind: "movie"
        source: "TheatreGenreIndex.qml"
        onLoaded: {
            item.backdrop = wall
            item.mediaKind = theatreGenreIndexLayer.mediaKind
            item.backRequested.connect(win.closeTheatreGenreIndex)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.genrePicked.connect(function(name) {
                win.openTheatreGenre(theatreGenreIndexLayer.mediaKind, name)
            })
        }
    }

    // ---- series detail layer: opened from a Top-10 title tile, sits OVER the world page ----
    Loader {
        id: seriesLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property string title: ""
        property string resumeSeriesId: ""    // Continue resume: jump straight to this chapter…
        property string resumeChapterId: ""   //   …in this series (set seriesId BEFORE the chapter)
        source: "MangaSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = seriesLayer.title
            if (seriesLayer.resumeSeriesId) item.seriesId = seriesLayer.resumeSeriesId
            if (seriesLayer.resumeChapterId) item.openChapterId = seriesLayer.resumeChapterId
            item.backRequested.connect(win.closeSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            // the READER's own chrome (not the page topbar): session verbs
            item.readerMinimizeRequested.connect(win.minimizeComicReader)
            item.readerCloseRequested.connect(win.closeComicReader)
        }
    }

    // ---- western-comics detail layer: the GetComics shelf (ComicSeries), over the world ----
    Loader {
        id: westernLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property string title: ""
        property string tagSlug: ""
        property int    tagId: 0
        property string resumeChapterId: ""   // Continue/session resume: straight into the reader
        source: "ComicSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = westernLayer.title
            item.tagId = westernLayer.tagId
            item.tagSlug = westernLayer.tagSlug        // set LAST — assigning it triggers resolve()
            if (westernLayer.resumeChapterId) item.openChapterId = westernLayer.resumeChapterId
            item.backRequested.connect(win.closeWestern)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.readerMinimizeRequested.connect(win.minimizeComicReader)
            item.readerCloseRequested.connect(win.closeComicReader)
        }
    }

    // ---- xoxo series layer: an xoxo comic's issue list (peer of westernLayer) ----
    Loader {
        id: xoxoSeriesLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property string sid: ""
        property string title: ""
        property string cover: ""
        source: "XoxoSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = xoxoSeriesLayer.title
            item.cover = xoxoSeriesLayer.cover
            item.seriesId = xoxoSeriesLayer.sid        // set LAST — assigning it triggers reload()
            item.backRequested.connect(win.closeXoxoSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.readerMinimizeRequested.connect(win.minimizeComicReader)
            item.readerCloseRequested.connect(win.closeComicReader)
        }
    }

    // ---- xoxo genre grid layer: a genre/shelf box's paginated series grid ----
    Loader {
        id: xoxoGenreLayer
        anchors.fill: parent
        z: 49
        active: false
        visible: active
        property var box: ({})
        source: "XoxoGenrePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.box = xoxoGenreLayer.box
            item.backRequested.connect(win.closeXoxoGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.seriesRequested.connect(win.openXoxoSeries)   // tile → issue list (over this grid)
        }
    }

    // ---- GetComics Archives board layer: the publisher/franchise taxonomy ----
    Loader {
        id: comicBoardLayer
        anchors.fill: parent
        z: 49
        active: false
        visible: active
        source: "ComicArchiveBoard.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeComicArchiveBoard)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.boxRequested.connect(win.openComicArchive)    // box → existing archive index
        }
    }

    // ---- western-comics archive index layer: series archives under an explore box
    //      (below the series detail, so a picked series opens OVER it) ----
    Loader {
        id: comicIndexLayer
        anchors.fill: parent
        z: 49
        active: false
        visible: active
        property string boxTitle: ""
        property string tagSlug: ""
        property int    tagId: 0
        property int    boxCount: 0
        source: "ComicArchiveIndex.qml"
        onLoaded: {
            item.backdrop = wall
            item.boxTitle = comicIndexLayer.boxTitle
            item.tagSlug = comicIndexLayer.tagSlug
            item.boxCount = comicIndexLayer.boxCount
            item.tagId = comicIndexLayer.tagId          // set LAST — assigning it triggers resolve()
            item.backRequested.connect(win.closeComicArchive)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.westernPicked.connect(win.openWestern)
            item.allReleasesRequested.connect(win.openWestern)
        }
    }

    // ---- Theatre detail layer: opened from a Theatre tile, sits OVER the world page ----
    Loader {
        id: theatreSeriesLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property var pendingItem: ({})
        source: "TheatreSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.itemData = theatreSeriesLayer.pendingItem
            item.backRequested.connect(win.closeTheatreSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.playRequested.connect(win.openMovieSession)
        }
    }

    // ---- video player layer: above every detail/series layer (mpv under house glass) ----
    Loader {
        id: playerLayer
        anchors.fill: parent
        z: 60
        active: false
        visible: win.playerOpen
        source: "PlayerPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.minimizePlayer)
            item.minimizeRequested.connect(win.minimizePlayer)
            item.closeRequested.connect(win.closePlayerSession)
        }
    }

    // ---- book detail layer: Biblio's OWN dust-jacket page over the world (above series) ----
    Loader {
        id: bookLayer
        anchors.fill: parent
        z: 53
        active: false
        visible: active
        property var book: ({})
        source: "BiblioBook.qml"
        onLoaded: {
            item.backdrop = wall
            item.book = bookLayer.book
            item.backRequested.connect(win.closeBook)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.readRequested.connect(win.openBookSession)
        }
    }

    // ---- the reader: foliate EPUB reader (WebEngine), over the book detail ----
    Loader {
        id: bookReaderLayer
        anchors.fill: parent
        z: 58
        active: false
        visible: active
        property string bookPath: ""
        property var bookMeta: ({})
        source: "BookReader.qml"
        onLoaded: {
            item.open(bookReaderLayer.bookPath, bookReaderLayer.bookMeta)
            item.closed.connect(win.closeBookReaderSession)
            item.minimizeRequested.connect(win.minimizeBookReader)
        }
    }

    // ---- generic world search layer: Tankoban + Theatre (SearchSurface + their own source) ----
    Loader {
        id: worldSearchLayer
        anchors.fill: parent
        z: 51
        active: false
        visible: active
        property string searchMode: ""
        property string placeholder: "Search…"
        source: "SearchSurface.qml"
        onLoaded: {
            item.backdrop = wall
            item.searchMode = worldSearchLayer.searchMode
            item.placeholder = worldSearchLayer.placeholder
            item.backRequested.connect(win.closeWorldSearch)
            item.itemRequested.connect(win.routeWorldSearchItem)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- search layer: Biblio's search surface over the world (below the book detail) ----
    Loader {
        id: searchLayer
        anchors.fill: parent
        z: 51
        active: false
        visible: active
        source: "BiblioSearch.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeSearch)
            item.homeRequested.connect(function() { win.closeSearch(); win.closeWorld() })
            item.bookRequested.connect(win.openBook)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- Biblio series detail layer: opened from a SERIES card (above search, below the book detail) ----
    // ---- universe art warmer: once the shell is up, quietly pull the BUILT universes' art into the
    //      disk cache so opening "Explore" shows it INSTANTLY (the app's download-once-then-instant
    //      model). Idle work — runs after boot, off the critical path; hidden Images do the warming.
    //      Bounded to the two built exemplars (One Piece anime, Marvel cinematic). ----
    Item {
        id: universeWarmer
        property var opUrls: []
        property var mcuUrls: []
        property var warmUrls: opUrls.concat(mcuUrls)
        function warm() {
            UniverseApi.loadUniverse("One Piece", function(u) { universeWarmer.opUrls = UniverseApi.imageUrls(u) })
            Mcu.loadMcu(function(d) { universeWarmer.mcuUrls = Mcu.imageUrls(d) })
        }
        Repeater {
            model: universeWarmer.warmUrls
            delegate: Image {
                required property string modelData
                source: modelData
                asynchronous: true; cache: true; visible: false
            }
        }
    }

    // ---- session switch glue: capture the outgoing surface, tear it down, build + restore the next ----
    Connections {
        target: Sessions
        function onActiveChanged(prevId, nextId) {
            var prev = Sessions.get(prevId)
            if (prev && prev.id) {
                Sessions.saveState(prevId, win.captureSession(prev))
                win.teardownSession(prev)
            }
            var next = Sessions.get(nextId)
            if (next && next.id) win.activateSession(next)
            else {
                // no next session: land on the world behind (Windows-like), not always home
                currentSurface = worldStack.current || "Home"
                refreshWallpaper()
                // a MINIMIZE (record kept, nothing became active): pop the taskbar out so the
                // user sees where the session went; it pulls back after 15 idle seconds.
                if (prevId && Sessions.get(prevId).id) taskbar.reveal()
            }
        }
    }

    // ---- Downloads page: unified local-media vault, entered from the taskbar ----
    Loader {
        id: downloadsLayer
        anchors.fill: parent
        z: 52
        active: false
        visible: active
        source: "DownloadsPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeDownloadsPage)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.openRequested.connect(win.routeDownloadItem)
            item.openWorldRequested.connect(win.routeDownloadWorld)
        }
    }

    Connections {
        target: typeof Extensions !== "undefined" ? Extensions : null
        function onChanged() {
            TheatreApi.setExtensions(Extensions.installed())
            Subtitles.setExtensions(Extensions.installed())
        }
    }

    // ---- Extensions page: the store (Stremio-protocol addons), from the taskbar ----
    Loader {
        id: extensionsLayer
        anchors.fill: parent
        z: 52
        active: false
        visible: active
        source: "ExtensionsPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeExtensionsPage)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
        }
    }

    // ---- the OS-shell taskbar: auto-hidden switcher over everything (under the boot splash) ----
    Taskbar {
        id: taskbar
        z: 900
        visible: !win.immersiveSurfaceOpen
        enabled: visible
        onVisibleChanged: if (!visible) open = false
        onSwitchRequested: (id) => Sessions.switchTo(id)
        onCloseRequested: (id) => win.closeSession(id)
        onStartClicked: { /* Start menu is a later spec - placeholder */ }
        downloadsBadge: (typeof LocalDownloads !== "undefined")
                        ? (LocalDownloads.revision, LocalDownloads.totals.active || 0) : 0
        downloadsActive: downloadsLayer.active
        onDownloadsClicked: downloadsLayer.active ? win.closeDownloadsPage() : win.openDownloadsPage()
        extensionsActive: extensionsLayer.active
        onExtensionsClicked: extensionsLayer.active ? win.closeExtensionsPage() : win.openExtensionsPage()
    }

    Loader {
        id: wallpaperLayer
        anchors.fill: parent
        z: 920
        active: false
        visible: active
        property string targetWorld: "Home"
        source: "WallpaperSearch.qml"
        onLoaded: {
            item.backdrop = wall
            item.targetWorld = wallpaperLayer.targetWorld
            item.inheritedImageUrl = win.wallpaperSource
            item.closeRequested.connect(win.closeWallpaperSearch)
            item.applyRequested.connect(function(scope, world, pick) {
                if (scope === "all") win.setWallpaperEverywhere(pick)
                else win.setWallpaperPick(world, pick)
                item.inheritedImageUrl = win.wallpaperSource
                win.closeWallpaperSearch()
            })
            item.forceActiveFocus()
        }
        onActiveChanged: if (active && item) {
            item.targetWorld = wallpaperLayer.targetWorld
            item.inheritedImageUrl = win.wallpaperSource
            item.forceActiveFocus()
        }
    }

    // ---- OS-style boot loader: prefetch covers, then fade away to reveal the shell with art warm ----
    BootSplash {
        id: boot
        anchors.fill: parent
        z: 1000
        onFinished: { bootFade.start(); universeWarmer.warm() }
        NumberAnimation { id: bootFade; target: boot; property: "opacity"; to: 0; duration: 400
            onFinished: boot.visible = false }
    }
}
