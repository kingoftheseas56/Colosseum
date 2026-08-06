// Colosseum — HOME (v1, on the proven spine)
// Fullscreen-exclusive frameless OS surface: persistent wallpaper + frosted-glass chrome.
//   Top bar -> universal Continue -> per-medium widgets.
// Glass = proven material (see Glass.qml).
// Run:  C:/Qt/6.11.1/mingw_64/bin/qml.exe qml/Main.qml      (Esc / Ctrl+Q to quit)

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtCore
import "Catalog.js" as Catalog
import "TheatreApi.js" as TheatreApi
import "UniverseExtApi.js" as UniverseApi
import "ExtensionsCatalog.js" as ExtCatalog
import "LocgApi.js" as Locg
import "ComicsApi.js" as GcApi
import "ComicsDb.js" as ComicsDb
import "ComicResolve.js" as Resolve
import "AddonClient.js" as AddonClient
import "Subtitles.js" as Subtitles
import "Torrentio.js" as Torrentio
import "EpisodeBrowser.js" as EpisodeBrowser
import "BiblioApi.js" as BiblioApi
import "CollectionBackfill.js" as CollectionBackfill
import "WarmingQueue.js" as Warming

Window {
    id: win
    // Hidden until the native WindowModeStore chooses the startup presentation
    // (fullscreen by default; developer-windowed if that was the last stable mode).
    visible: false
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#05060a"
    title: "Colosseum"

    property string currentSurface: "Home"
    property string wallpaperSource: "../assets/wallpaper/cold-ripple.jpg"
    // Native living wallpapers (2026-07-18, ratified from the arena mock): a pick whose
    // image_url is "native:<id>" loads a QML scene instead of an Image. The registry is
    // the one honest map — an unknown id falls back to the default still.
    readonly property bool wallpaperIsNative: wallpaperSource.indexOf("native:") === 0
    // Mirrors WallpaperApi.nativeSceneFor — keep the two in sync when adding a scene.
    function nativeWallpaperFile(source) {
        if (source === "native:noirflow") return "wallpapers/NoirFlow.qml"
        if (source === "native:aurora-flow") return "wallpapers/AuroraFlow.qml"
        if (source === "native:mesh-twilight") return "wallpapers/MeshTwilight.qml"
        if (source === "native:mesh-ember") return "wallpapers/MeshEmber.qml"
        if (source === "native:mesh-mint") return "wallpapers/MeshMint.qml"
        if (source === "native:lowpoly") return "wallpapers/LowPoly.qml"
        return ""
    }

    Settings {
        id: wallpaperSettings
        location: Qt.resolvedUrl("../wallpapers.ini")
        category: "wallpapers"
        property string homePick: ""
        property string tankobanPick: ""
        property string biblioPick: ""
        property string theatrePick: ""
    }

    // The ONE global preference store the whole shell reads (Task 2). Production leaves
    // settingsLocation unset, so it uses the application QSettings store. Threading
    // showExplicit into Theatre/Tankoban/Biblio is Task 9 — here it is only set + surfaced.
    ContentPreferences { id: contentPreferences }

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
        if (appType === "biblio" || contentKind === "book" || contentKind === "audiobook") return "Biblio"
        return currentSurface || "Home"
    }
    function refreshWallpaper() {
        var pick = pickFor(currentSurface)
        wallpaperSource = pick && pick.image_url ? pick.image_url : "../assets/wallpaper/cold-ripple.jpg"
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

    // One-time migration: everything already downloaded becomes a Collection entry
    // (grouped to the series). Guarded by a hidden "_meta" marker so it runs once —
    // manual removals then stick. New downloads collect live via the detail pages.
    function runCollectionBackfill() {
        if (typeof Collection === "undefined" || typeof LocalDownloads === "undefined") return
        if (Collection.has("_meta", "backfill_v3")) return
        var ths = LocalDownloads.series("theatre") || []
        for (var i = 0; i < ths.length; i++) {
            var te = CollectionBackfill.entryForTheatreSeries(ths[i], LocalDownloads.items("theatre", ths[i].key))
            if (te && !Collection.has("theatre", String(te.id))) Collection.add("theatre", te)
        }
        var tks = LocalDownloads.series("tankoban") || []
        for (var j = 0; j < tks.length; j++) {
            var ke = CollectionBackfill.entryForTankobanSeries(tks[j])
            if (ke && !Collection.has("tankoban", String(ke.id))) Collection.add("tankoban", ke)
        }
        // One-time cleanup: an earlier backfill (pre title-dedup) could add the same
        // book twice — an authorless "title|" entry AND a proper authored "title|author"
        // entry (ebook + audiobook of the same title). Drop the authorless one when an
        // authored entry with the same title exists (the authored id matches a live save).
        var cleanupBiblio = Collection.items("biblio") || []
        var authoredTitles = {}
        for (var cc = 0; cc < cleanupBiblio.length; cc++) {
            var ce = cleanupBiblio[cc]
            var caut = (ce.payload && ce.payload.book && ce.payload.book.author) ? ce.payload.book.author : ""
            if (caut) authoredTitles[CollectionBackfill.titleKey(ce.title)] = true
        }
        for (var cd = 0; cd < cleanupBiblio.length; cd++) {
            var de = cleanupBiblio[cd]
            var daut = (de.payload && de.payload.book && de.payload.book.author) ? de.payload.book.author : ""
            if (!daut && authoredTitles[CollectionBackfill.titleKey(de.title)])
                Collection.remove("biblio", String(de.id))
        }
        // Biblio: dedup by BOTH exact id and normalized title, so a book already
        // saved as "title|author" isn't re-added as an authorless "title|" tile.
        var biblioTitles = {}
        var existingBiblio = Collection.items("biblio") || []
        for (var b0 = 0; b0 < existingBiblio.length; b0++)
            biblioTitles[CollectionBackfill.titleKey(existingBiblio[b0].title)] = true
        function addBook(entry) {
            if (!entry || !entry.id) return
            var tk = CollectionBackfill.titleKey(entry.title)
            if (Collection.has("biblio", String(entry.id)) || biblioTitles[tk]) return
            Collection.add("biblio", entry)
            biblioTitles[tk] = true
        }
        if (typeof Books !== "undefined") {
            var bks = Books.downloadedBooks() || []
            for (var k = 0; k < bks.length; k++)
                addBook(CollectionBackfill.entryForBook(bks[k], BiblioApi.pairKey(bks[k].title || "", bks[k].author || "")))
        }
        if (typeof Audiobooks !== "undefined") {
            var abs = Audiobooks.downloadedAudiobooks() || []
            for (var m = 0; m < abs.length; m++)
                addBook(CollectionBackfill.entryForBook(abs[m], abs[m].id))
        }
        Collection.add("_meta", { "id": "backfill_v3", "type": "flag", "title": "", "cover": "" })
    }

    // Fill covers for backfilled books that have no local cover anywhere. Books with a
    // Progress cover are filled reactively by the Biblio Collection row; this handles the
    // rest by fetching the cover from Apple Books by title and baking it into the entry.
    // Runs each startup but only touches cover-less entries, so it converges.
    function enrichBiblioCovers() {
        if (typeof Collection === "undefined" || typeof BiblioApi === "undefined") return
        var entries = Collection.items("biblio") || []
        if (!entries.length) return
        var progCovered = {}
        if (typeof Progress !== "undefined") {
            var prog = Progress.recent("book", 200) || []
            for (var i = 0; i < prog.length; i++)
                if (prog[i].cover) progCovered[CollectionBackfill.titleKey(prog[i].title)] = true
        }
        for (var j = 0; j < entries.length; j++) {
            var e = entries[j]
            if (e.cover && e.cover !== "") continue                              // already has a cover
            if (progCovered[CollectionBackfill.titleKey(e.title)]) continue      // Progress fills it (render-time)
            if (!e.title) continue
            ;(function(entry) {
                BiblioApi.lookupBook(entry.title, function(book) {
                    if (!book || !book.cover) return
                    var e2 = {}
                    for (var k in entry) e2[k] = entry[k]
                    e2.cover = book.cover
                    e2.art = book.cover
                    Collection.add("biblio", e2)   // upsert same id, now with a cover
                })
            })(e)
        }
    }

    Component.onCompleted: {
        // C++ owns the payload read (Qt blocks file:// XHR by default, and house doctrine keeps
        // transport off the GUI thread's JS). Installed once, here, because a .pragma library
        // holds one shared instance per QML engine. FIRST in this handler on purpose: the dev
        // harnesses below can `return` early, and a universe with no reader renders empty.
        if (typeof Extensions !== "undefined")
            UniverseApi.setReader(function (f) { return Extensions.universePayload(f) })
        // Efficiency gate: play a given local file through the real player as soon as the shell is
        // up, so both backends can be measured under identical conditions.
        if (typeof DevAbbaClip !== "undefined" && String(DevAbbaClip).length > 0) {
            var abbaPath = String(DevAbbaClip)
            abbaTimer.path = abbaPath
            abbaTimer.start()
        }
        // Which video backend this launch will use. Announced at startup so a run can be diagnosed
        // without playing anything (Task 17).
        console.log("[player] startup backend = " + (win.usePlayer2 ? "PLAYER 2" : "mpv (player 1)")
                    + "  (booted=" + Player2Available + ")")
        // Native state owns the startup presentation and shows the window; the fallback
        // keeps a bare QML run (harnesses) fullscreen as before.
        if (typeof WindowMode !== "undefined")
            WindowMode.initializeShell(win)
        else
            win.showFullScreen()
        refreshWallpaper()
        // The full comics catalog is intentionally absent here. TankobanWorld owns its generated
        // import and ingest so root startup never parses the multi-megabyte payload.
        // LOCG catalogue: real clock + polite request spacer + the resolve machine's deps
        Locg.nowFn = function() { return Date.now() }
        Locg.delayFn = function(ms, cb) { locgSpacer.fire(ms, cb) }
        Resolve.store = {
            get: function(k) { return comicMapStore.value(k, "") },
            set: function(k, v) { comicMapStore.setValue(k, v) }
        }
        // GetComics is the content lane (spec 2026-07-10).
        // Adapter shape: GC tag hits → {id: "<slug>|<tagId>", title}; the composite id
        // is what persists — the series page splits it (slug feeds gc: routing, tagId
        // feeds releases()). GC has no blocked signal: empty = plain no-match,
        // session-only, retry free next launch.
        Resolve.searchFn = function(q, cb) {
            GcApi.searchSeries(q, function(hits) {
                cb((hits || []).map(function(h) {
                    return { id: h.tag + "|" + h.tagId, title: h.title };
                }), { ok: true, blocked: false });
            });
        }
        // Slug-first lane (2026-07-12): WP's tag search floods popular titles out of its
        // own results (Batman 1417 buries Absolute Batman 27) — the exact slug never does.
        Resolve.slugFn = function(slug, cb) {
            GcApi.tagBySlug(slug, function(h) {
                cb(h ? { id: h.tag + "|" + h.tagId, title: h.title } : null);
            });
        }
        // Theatre reads the extension registry through a pushed copy — a .pragma
        // library can't reach context properties (extensions spec Phase 3)
        if (typeof Extensions !== "undefined") {
            TheatreApi.setExtensions(Extensions.installed())
            Subtitles.setExtensions(Extensions.installed())
        }
        // Task 9: push the global Explicit Content preference into TheatreApi so the boot-time
        // marquee rows (loadTheatre/loadHome, airing-anime top-10) honour it. Sexually-explicit
        // ONLY — Berserk/GoT/Ecchi/Mature/TV-MA stay visible; only EXPLICIT_TAGS gate.
        TheatreApi.setShowExplicit(contentPreferences.showExplicit)
        // dev harness (COLOSSEUM_OPEN_EXTENSIONS=1): boot straight into the store,
        // so smoke runs exercise the Loader (QML errors only surface on activation)
        if (typeof DevOpenExtensions !== "undefined" && DevOpenExtensions)
            win.openExtensionsPage()
        // dev harness (COLOSSEUM_OPEN_WORLD="Theatre"): boot straight into a world
        if (typeof DevOpenWorld !== "undefined" && String(DevOpenWorld).length)
            win.openWorld(String(DevOpenWorld))
        // bakeoff harness (COLOSSEUM_BAKEOFF_STRIP=<dir>): page-only production
        // MangaReader over the canonical fixture (long-strip bakeoff spec §10)
        if (typeof DevBakeoffStripPages !== "undefined" && DevBakeoffStripPages.length)
            bakeoffStripLayer.active = true
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
        win.runCollectionBackfill()
        win.enrichBiblioCovers()
    }

    // locg:<id> → "<gc-tag-slug>|<gc-tagId>", persisted forever (survives restarts)
    // V3: content lane settled on GetComics (2026-07-10) — the category bump orphans stale
    // prior-source mappings so they can never poison a GC attach (V2's own bump orphaned
    // the year-gate poison the same way)
    Settings { id: comicMapStore; category: "comicResolveV3" }
    // polite spacer for LOCG's request queue (Locg.delayFn)
    Timer {
        id: locgSpacer
        property var _cb: null
        function fire(ms, cb) { _cb = cb; interval = Math.max(1, ms); restart() }
        onTriggered: { var c = _cb; _cb = null; if (c) c() }
    }

    // Esc: close the series page if open, else leave a world page, else quit. Ctrl+Q always quits.
    Shortcut { sequences: ["Escape"]; onActivated: {
        if (win.playerOpen) win.closePlayer()
        else if (bookReaderLayer.active) win.closeBookReader()
        // Taskbar full-pages sit at z:56, above every browsing/detail page — so back must
        // close them BEFORE the pages they cover, else ESC "does nothing" visibly while
        // silently closing the page underneath. Only one of the three is ever active.
        else if (downloadsLayer.active) win.closeDownloadsPage()
        else if (extensionsLayer.active) win.closeExtensionsPage()
        else if (settingsLayer.active) win.closeSettingsPage()
        else if (bookLayer.active) win.closeBook()
        else if (biblioGenreLayer.active) win.closeBiblioGenre()
        else if (biblioGenreIndexLayer.active) win.closeBiblioGenreIndex()
        else if (searchLayer.active) win.closeSearch()
        else if (worldSearchLayer.active) win.closeWorldSearch()
        else if (theatreSeriesLayer.active) win.closeTheatreSeries()
        else if (seriesLayer.active) win.closeSeries()
        else if (westernLayer.active) win.closeWestern()
        else if (universeLayer.active) win.closeUniverse()
        else if (universeHallLayer.active) win.closeUniverseHall()
        else if (comicSeriesLayer.active) win.closeComicSeries()
        else if (locgPublisherLayer.active) win.closeLocgPublisher()
        else if (comicBoardLayer.active) win.closeComicArchiveBoard()
        else if (comicIndexLayer.active) win.closeComicArchive()
        else if (continueSeeAllLayer.active) win.closeContinueSeeAll()
        else if (theatreGenreLayer.active) win.closeTheatreGenre()
        else if (theatreGenreIndexLayer.active) win.closeTheatreGenreIndex()
        else if (genreLayer.active) win.closeGenre()
        else if (genreIndexLayer.active) win.closeGenreIndex()
        else if (worldStack.current !== "") win.closeWorld()
        else Qt.quit()
    } }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: Qt.quit() }

    // The secret developer door: F11 flips the whole shell between fullscreen (Colosseum's
    // public identity) and the frameless developer window. Application-scoped so it works on
    // home, world pages, readers, overlays, and active playback alike. The native store is the
    // single authority — it exits PiP first if needed, then toggles the base mode.
    Shortcut {
        sequences: ["F11"]
        context: Qt.ApplicationShortcut
        onActivated: win.toggleFullscreenShell()
    }

    // Minimize the OS surface to the taskbar — "get it off my screen" WITHOUT quitting (the shell
    // keeps running, art stays warm). Windows restores it to whatever base mode it held before
    // minimizing (fullscreen or the developer window), so no forced snap-back is needed.
    function minimizeShell() { win.showMinimized() }
    // Topbar fullscreen toggle — the same shell flip as the F11 developer door
    // (WindowModeStore stays the single native authority for the mode).
    function toggleFullscreenShell() {
        if (typeof WindowMode !== "undefined" && !fullscreenTransition.transitioning)
            fullscreenTransition.begin()
    }

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

    // ---- Continue see-all: the whole resume backlog, scoped per door (spec: haven
    //      docs/superpowers/specs/2026-07-11-colosseum-continue-see-all-design.md) ----
    function openContinueSeeAll(scope) {
        continueSeeAllLayer.scope = scope
        if (continueSeeAllLayer.active && continueSeeAllLayer.item) continueSeeAllLayer.item.scope = scope
        else continueSeeAllLayer.active = true
    }
    function closeContinueSeeAll() { continueSeeAllLayer.active = false }

    // Menu "Resume / Play": resume the series' current episode through the EXACT Continue path
    // when there's watch history; otherwise (fresh save / "Play") open the detail page to start.
    function resumeLibraryEntry(entry) {
        if (!entry) return
        var pid = String(entry.id || "")
        var list = (typeof Progress !== "undefined") ? Progress.recent("video", 0) : []
        for (var i = 0; i < list.length; i++) {
            var id = String(list[i].id || "")
            if (id === pid || id.indexOf(pid + ":") === 0) { win.resumeContinue(list[i]); return }
        }
        win.openCollectionEntry(entry)
    }
    // Menu "Mark watched / unwatched" (spec §4.3). Marking watched = "I'm done": clear Continue
    // AND Next Up (both derive from Progress) FIRST, then set the mark — forget() clears the mark
    // by design, so the ORDER matters. Unmark just reverses the flag; progress history stays.
    function markLibraryWatched(entry, watched) {
        if (!entry || typeof Progress === "undefined") return
        var id = String(entry.id || "")
        if (watched) { Progress.forget("video", id); Progress.setWatchedMark(id, true) }
        else { Progress.setWatchedMark(id, false) }
    }

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
        seriesLayer.resumeVolumeId = ""
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.openEntryKind = "manga"   // a reused item may still be in a volume read
            seriesLayer.item.openChapterId = ""        // leave the reader, show the chapter list
            seriesLayer.item.seriesTitle = title
        } else seriesLayer.active = true
    }
    // open a manga series AND jump straight into the reader at a saved chapter (Continue resume).
    function openSeriesAt(title, seriesId, chapterId) {
        seriesLayer.resumeSeriesId = seriesId || ""
        seriesLayer.resumeChapterId = chapterId || ""
        seriesLayer.resumeVolumeId = ""
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.seriesTitle = title
            if (seriesId) seriesLayer.item.seriesId = seriesId
            seriesLayer.item.openEntryKind = "manga"   // a reused item may still be in a volume read
            seriesLayer.item.openChapterId = chapterId || ""
        } else seriesLayer.active = true
    }
    function closeSeries() { seriesLayer.active = false }

    // ---- western-comics detail: the GetComics shelf (ComicSeries), parallel to the
    //      manga seriesLayer. Series id app-wide = "gc:<tag-slug>" — the prefix is how
    //      every shared kind:"comic" route below tells the two lanes apart. ----
    function openWestern(d) {
        westernLayer.baked = null
        if (westernLayer.active && westernLayer.item) westernLayer.item.bakedReleases = null
        // DB-first (Hemanth 2026-07-15): the catalog series view is THE series
        // view now. Resolve the title against our DB (ingesting it on demand);
        // the GetComics shelf remains only for series the catalog doesn't carry.
        var hit = win.comicsDbHit((d && d.title) || "")
        if (hit) {
            win.openComicSeries({ id: hit.locgId, title: hit.title,
                                  cover: hit.cover || (d && d.cover) || "" })
            return
        }
        // catalogue redirect (spec 2026-07-17): exactly ONE run bears this title ->
        // its run-scoped page. Multiple same-name runs -> the live shelf keeps the
        // whole-franchise view; never guess a run.
        if (typeof ComicsCatalog !== "undefined" && ComicsCatalog.ready()) {
            var ex = ComicsCatalog.exactMatches((d && d.title) || "")
            if (ex.length === 1) {
                win.openGcdSeries({ gcdId: ex[0].gcdId, title: (d && d.title) || "",
                                    cover: (d && d.cover) || "" })
                return
            }
        }
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
        westernLayer.baked = null
        if (westernLayer.active && westernLayer.item) westernLayer.item.bakedReleases = null
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

    // ---- catalogue run page: the western shelf in baked mode (spec 2026-07-17).
    //      d: { gcdId, title?, cover?, resumeChapterId? } ----
    function openGcdSeries(d) {
        if (typeof ComicsCatalog === "undefined" || !ComicsCatalog.ready()) {
            if (d && d.title) win.openWestern({ title: d.title })   // graceful: live shelf
            return
        }
        var gcdId = Number((d && d.gcdId) || 0)
        var s = ComicsCatalog.series(gcdId)
        if (!s || s.gcdId === undefined) {
            if (d && d.title) win.openWestern({ title: d.title })
            return
        }
        var rows = ComicsCatalog.downloadsFor(gcdId)
        var rel = []
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i]
            rel.push({ id: String(r.postId), url: r.link, name: r.title, cover: "",
                       year: r.yearStart || 0, sizeMB: 0, synopsis: "",
                       date: r.date || "", collection: r.kind !== "single" })
        }
        westernLayer.baked = { gcdId: gcdId, releases: rel,
                               cover: s.cover || (d && d.cover) || "" }
        westernLayer.title = s.title + (s.year ? " (" + s.year + ")" : "")
        westernLayer.tagSlug = ""; westernLayer.tagId = 0
        westernLayer.resumeChapterId = (d && d.resumeChapterId) || ""
        if (westernLayer.active && westernLayer.item) {
            var it = westernLayer.item
            it.bakedReleases = null                 // reset first so re-injection repaints
            it.seriesTitle = westernLayer.title
            it.poster = westernLayer.baked.cover || ""
            // Resolve the baked IDENTITY (gcdId + bakedReleases -> seriesId "gcd:<id>")
            // BEFORE opening the reader. Opening the reader first (as this did) mounts
            // ComicReaderShell while seriesId is still the transient "gc:<empty-slug>",
            // so its resume reads the wrong (empty) progress key and lands on page 1 —
            // then the identity flips to "gcd:<id>" and every save goes to the OTHER key,
            // and the shell's first presentation writes page 1 over the real record.
            // Confirmed via runtime trace 2026-08-06 (save under gcd:119237, restore under
            // gc:). Set identity first; open the reader last, when seriesId is stable.
            it.gcdId = westernLayer.baked.gcdId
            it.bakedReleases = westernLayer.baked.releases   // seriesId now "gcd:<id>" (non-null baked)
            it.tagId = 0; it.tagSlug = ""                    // resolve() guard true (baked non-null), no stray live lookup
            it.openChapterId = westernLayer.resumeChapterId || ""   // open the reader LAST — identity fully stable
        } else westernLayer.active = true
    }

    // ---- a universe's comic row: the entry pins VERIFIED GetComics post IDs, so there is
    //      no tag to resolve and no catalogue series. Mirrors openGcdSeries' baked injection
    //      (Main.qml:578) — an explicit release list, tagSlug/tagId deliberately empty.
    //      `?p=<id>` is GetComics' permalink; ComicSeries.qml:531 feeds it to downloadIssue.
    //      d: { title, posts:[Number], year? } ----
    function openUniverseComic(d) {
        var posts = (d && d.posts) || []
        if (!posts.length) { console.warn("universes: openUniverseComic — no posts for", (d && d.title) || "(untitled)"); return }
        var rel = []
        for (var i = 0; i < posts.length; i++)
            rel.push({ id: String(posts[i]),
                       url: "https://getcomics.org/?p=" + posts[i],
                       name: d.title || "", cover: "",
                       year: Number(d.year || 0), sizeMB: 0, synopsis: "",
                       date: "", collection: true })
        westernLayer.baked = { gcdId: 0, releases: rel, cover: "" }
        westernLayer.title = (d.title || "") + (d.year ? " (" + d.year + ")" : "")
        westernLayer.tagSlug = ""; westernLayer.tagId = 0
        westernLayer.resumeChapterId = ""
        if (westernLayer.active && westernLayer.item) {
            var it = westernLayer.item
            it.bakedReleases = null                 // reset first so re-injection repaints
            it.openChapterId = ""
            it.seriesTitle = westernLayer.title
            it.poster = ""
            it.gcdId = 0
            it.bakedReleases = westernLayer.baked.releases   // triggers paint (bakedReleases now non-null)
            it.tagId = 0; it.tagSlug = ""                    // reset LAST — resolve() guard is true, no stray live lookup
        } else westernLayer.active = true
    }

    // ---- comic series: a LOCG catalogue series' issue list (GetComics content attached).
    //      Opened from search (data.locg), the world Top-Comics row, or a publisher grid. ----
    // catalog lookup with on-demand ingest: Main never imports the multi-MB
    // gen.js itself; the first route that needs the catalog activates the tiny
    // ComicsDbLoader (synchronous Loader -> ingest completes before we return).
    function comicsDbHit(title) {
        if (!title || !String(title).length) return null
        if (!ComicsDb.ready()) comicsDbLoader.active = true
        return ComicsDb.ready() ? ComicsDb.seriesByTitle(title) : null
    }

    function openComicSeries(d) {
        comicSeriesLayer.locgSid = (d && d.id) || ""
        comicSeriesLayer.locgMeta = (d && d.locgMeta) || ({})
        comicSeriesLayer.title = (d && d.title) || ""
        comicSeriesLayer.cover = (d && d.cover) || ""
        if (comicSeriesLayer.active && comicSeriesLayer.item) {
            comicSeriesLayer.item.openChapterId = ""       // leave the reader, show the list
            comicSeriesLayer.item.seriesTitle = comicSeriesLayer.title
            comicSeriesLayer.item.cover = comicSeriesLayer.cover
            comicSeriesLayer.item.locgMeta = comicSeriesLayer.locgMeta
            comicSeriesLayer.item.locgId = comicSeriesLayer.locgSid   // set LAST — triggers attach()
        } else comicSeriesLayer.active = true
    }
    function closeComicSeries() { comicSeriesLayer.active = false }

    // ---- LOCG publisher grid: one publisher shelf (Marvel/DC/Image...) as a paginated
    //      series grid; tile → LOCG series list via openComicSeries. ----
    function openLocgPublisher(box) {
        locgPublisherLayer.box = box || ({})
        if (locgPublisherLayer.active && locgPublisherLayer.item) locgPublisherLayer.item.box = locgPublisherLayer.box
        else locgPublisherLayer.active = true
    }
    function closeLocgPublisher() { locgPublisherLayer.active = false }

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
    // Back to Sources (from the hosted player's unavailable panel): close the hosted session,
    // reveal the SAME Theatre detail underneath, and replay its Sources sheet for the exact
    // movie/episode the user came from. backRequested carries no arg, so the request is the
    // ACTIVE hosted session's target (or the optional one passed in).
    function reopenSources(request) {
        var req = request || ({})
        if (!req || !req.mediaId) {
            var rec = Sessions.get(Sessions.activeId)
            if (rec && rec.contentKind === "hosted-video") req = rec.target || ({})
        }
        // Drop the hosted surface so the detail page is the topmost Theatre surface again.
        if (hostedPlayerLayer.active) {
            if (hostedPlayerLayer.item) hostedPlayerLayer.item.stop()
            win.hostedPlayerOpen = false
            hostedPlayerLayer.active = false
        }
        // Close the hosted session so it leaves the taskbar; the Sources sheet is the new focus.
        if (req && req.mediaId) {
            var rec2 = Sessions.get(Sessions.activeId)
            if (rec2 && rec2.contentKind === "hosted-video") Sessions.close(rec2.id)
        }
        if (theatreSeriesLayer.active && theatreSeriesLayer.item
                && theatreSeriesLayer.item.reopenSources)
            theatreSeriesLayer.item.reopenSources(req)
    }

    // ---- video player: a fullscreen layer over everything; kept alive once opened so mpv
    //      isn't torn down/recreated each play (avoids the use-after-free teardown trap). ----
    property bool playerOpen: false
    // The movie session the player minimized while still loaded. Reopening it from the
    // taskbar finds the stream warm — we resume in place instead of re-streaming.
    property string warmPlayerSessionId: ""

    // ---- hosted (web-embed) player: a SEPARATE fullscreen layer beside playerLayer. Unlike
    //      mpv, the hosted page is DESTROYED on minimize/close (active = false) — its
    //      off-the-record WebEngine profile and the cross-origin iframe must never outlive
    //      their visible use (no warm hidden iframe). Restore rebuilds it. ----
    property bool hostedPlayerOpen: false

    // Gives the shell a moment to finish coming up before the player is opened on top of it.
    Timer {
        id: abbaTimer
        property string path: ""
        interval: 2500
        repeat: false
        onTriggered: {
            console.log("[abba] auto-playing " + abbaTimer.path)
            win.openLocalVideoSession({ "id": "abba:clip", "title": "ABBA measurement clip",
                                        "path": abbaTimer.path })
        }
    }

    // The backend is a BOOT fact: COLOSSEUM_PLAYER2 selects the D3D11 RHI in C++, and
    // Player2Available reports what this process actually booted on. There is no runtime
    // fallback in a Player 2 boot - mpv cannot render on D3D11 - so failures surface on the
    // player page's error screen instead of swapping engines.
    readonly property bool usePlayer2: Player2Available === true

    // Every reader/player surface that must suppress the OS-shell taskbar. There are THREE
    // comic/manga reader lanes (all share the reader chrome — see minimizeComicReader):
    // seriesLayer=manga, westernLayer=western comics, comicSeriesLayer=the LOCG catalogue.
    // comicSeriesLayer was missing here, so the taskbar rode in front of that reader while
    // the other two + book + player suppressed it correctly (Hemanth, 2026-07-16).
    readonly property bool immersiveSurfaceOpen: win.playerOpen
        || win.hostedPlayerOpen
        || bookReaderLayer.active
        || (seriesLayer.active && seriesLayer.item && seriesLayer.item.openChapterId.length > 0)
        || (westernLayer.active && westernLayer.item && westernLayer.item.openChapterId.length > 0)
        || (comicSeriesLayer.active && comicSeriesLayer.item && comicSeriesLayer.item.openChapterId.length > 0)

    // ---- season-download resolver: a promoted queue job carries only the episode's
    //      stream id; we pick the rank-best Torrentio stream and feed back the local
    //      engine URL. Deferred while the player streams (one engine, playback wins). ----
    property var pendingResolves: []
    property var pendingFeeds: ({})   // "hash:idx" -> job id, answered by onFetchReady
    // torrent-choice pin (spec 2026-07-11): a hand-picked job carries infoHash/fileIdx
    // in its request; jobs() exposes them. Pinned -> prefetch exactly that torrent, no
    // source search, and Retry retries the SAME pick (the choice is durable — never
    // silently swapped). fileIdx -1 = hash-only pin (season-pack checkout 2026-07-19):
    // the torrent is chosen, the episode's file inside it resolves via the source
    // search below. Unpinned (auto fallback, old queued jobs) -> rank-best below.
    function pinnedPickFor(id) {
        var js = Download.jobs()
        for (var i = 0; i < js.length; i++) {
            if (js[i].id !== id) continue
            var h = String(js[i].infoHash || "")
            if (h.length) return { "infoHash": h, "fileIdx": Number(js[i].fileIdx || 0) }
            return null
        }
        return null
    }
    function resolveDownloadJob(id, streamId, mediaType) {
        var pin = pinnedPickFor(id)
        if (pin && pin.fileIdx >= 0) {
            var pkey = pin.infoHash.toLowerCase() + ":" + pin.fileIdx
            win.pendingFeeds[pkey] = id
            Stream.prefetch(pin.infoHash, pin.fileIdx)
            return
        }
        var onRows = function(rows) {
            if (!rows || !rows.length) {
                Download.failJob(id, "No stream found for this episode.")
                return
            }
            var best = rows[0]
            // hash-only pin (fileIdx -1, the season-pack checkout 2026-07-19): the
            // torrent was hand-picked, the file inside it wasn't known at queue time.
            // Prefer THIS episode's row from the picked torrent; the pack not
            // carrying this episode -> the rank-best fallback above stands.
            if (pin) {
                for (var p = 0; p < rows.length; p++) {
                    if (String(rows[p].infoHash || "").toLowerCase() === pin.infoHash.toLowerCase()) {
                        best = rows[p]
                        break
                    }
                }
            }
            // prefetch (NOT play): the url arrives via onFetchReady once the engine
            // is genuinely up. The old synchronous streamUrl() read raced a cold
            // engine and fed "" — the job then sat "resolving" forever (the
            // nothing-downloads wedge, diagnosed 2026-07-05). play() is also the
            // player's signal — prefetch keeps downloads out of mpv's ears.
            var key = (best.infoHash || "").toLowerCase() + ":" + (best.fileIdx || 0)
            win.pendingFeeds[key] = id
            Stream.prefetch(best.infoHash, best.fileIdx || 0)
        }
        // Same source ladder as the player and the picker: every installed stream
        // extension in ask-order first, Torrentio only if it is still installed AND
        // enabled. This path used to call Torrentio.js directly and never look at the
        // store at all, so removing Torrentio did nothing here and a well ranked above
        // it was ignored outright. (2026-07-25, A5 — Torrentio-honesty fix.)
        var installed = (typeof Extensions !== "undefined") ? Extensions.installed() : []
        var exts = AddonClient.streamExtensions(installed, mediaType, streamId)
        var lastResort = function() {
            if (AddonClient.torrentioEnabled(installed))
                Torrentio.loadStreams(mediaType, streamId, onRows)
            else
                Download.failJob(id, "No source installed for this. Add one in Extensions.")
        }
        if (exts.length) {
            AddonClient.loadStreams(exts, mediaType, streamId, function() {}, function(rows) {
                if (rows && rows.length) onRows(rows)
                else lastResort()
            })
        } else {
            lastResort()
        }
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
        target: typeof Sessions !== "undefined" ? Sessions : null
        function onTargetReplaced(id) {
            // one-tab-per-show: the tile now points at NEW content — a warm-kept old
            // stream must not resume as if it were the new pick, and if the record is
            // somehow active (taskbar path) the live surface follows the new target.
            if (win.warmPlayerSessionId === id)
                win.warmPlayerSessionId = ""
            if (Sessions.activeId === id)
                win.activateSession(Sessions.get(id))
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
    // Downloads, Extensions and Settings are the three taskbar full-pages; opening any one
    // closes the other two so only one taskbar surface is ever the front page (Task 2).
    function openDownloadsPage() {
        extensionsLayer.active = false
        settingsLayer.active = false
        downloadsLayer.active = true
        taskbar.open = false
    }
    function closeDownloadsPage() { downloadsLayer.active = false }

    // ---- Extensions page: the store, entered from the taskbar beside Downloads ----
    // The installed roster, live. The universes rail derives from it rather than from a
    // baked list, so installing or removing a universe changes Home with no other edit.
    property var installedExtensions:
        (typeof Extensions !== "undefined") ? Extensions.installed() : []

    // The carousel and the Hall both derive from the ROSTER, so installing or removing a
    // universe is the only way either surface changes. Replaces the five baked universes.
    readonly property var installedUniverses: {
        var out = []
        for (var i = 0; i < win.installedExtensions.length; i++) {
            var e = win.installedExtensions[i]
            if (!ExtCatalog.isUniverse(e)) continue
            if (e.enabled !== true) continue
            var m = e.manifest || ({})
            out.push({ extensionId: e.id, name: m.name || e.id,
                       banner: m.background || "", logo: m.logo || "" })
        }
        return out
    }

    // A universe is opened BY EXTENSION ID: the extension supplies identity, and the payload
    // is looked up from the curation point by that id. That join is what makes an installed
    // universe a real page instead of a row in a list. (Universes design §5.3 — a universe
    // supplies identity and ordering, never sources.) The name rides along only as a label,
    // for the header band before the payload lands.
    // One renderer for every universe. The per-category dispatcher is gone: it existed to
    // pick between bespoke per-IP pages, and those are being deleted (next task).
    function openUniverse(extensionId, name) {
        if (!extensionId) return
        universeLayer.extensionId = extensionId
        universeLayer.universeName = name || ""
        if (universeLayer.item) {
            universeLayer.item.extensionId = extensionId
            universeLayer.item.universeName = name || ""
        }
        universeLayer.active = true
    }
    function closeUniverse() { universeLayer.active = false }
    function openUniverseHall() { universeHallLayer.active = true }
    function closeUniverseHall() { universeHallLayer.active = false }

    function openExtensionsPage() {
        downloadsLayer.active = false
        settingsLayer.active = false
        extensionsLayer.active = true
        taskbar.open = false
    }
    function closeExtensionsPage() { extensionsLayer.active = false }

    // ---- Settings page: the global preferences gear, entered from the taskbar ----
    function openSettingsPage() {
        downloadsLayer.active = false
        extensionsLayer.active = false
        settingsLayer.active = true
        taskbar.open = false
    }
    function closeSettingsPage() { settingsLayer.active = false }
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
            // comics open only via the gc:/gcd: lanes; a stale foreign-prefixed id (retired
            // source, cut 2026-07-12) is an honest no-op, not an empty western shelf (mirrors
            // the browse guard)
            if (String(item.seriesId || "").indexOf("gc:") === 0)
                win.openWesternAt(item.seriesTitle, String(item.seriesId).slice(3), item.id)
            else if (String(item.seriesId || "").indexOf("gcd:") === 0)
                win.openGcdSeries({ gcdId: Number(String(item.seriesId).slice(4)),
                                    title: item.seriesTitle, resumeChapterId: item.id })
            else
                console.log("[route] ignoring unknown comic id:", item.seriesId)
        } else {
            win.openSeriesAt(item.seriesTitle, item.seriesId, item.id)
        }
    }
    // Play-while-arriving (2026-07-20): a LIVE theatre job can be watched now — the
    // player streams the same resolved url the download is pulling. Progress shares
    // the video id, so the landed copy resumes where the live watch left off.
    function routeArrivingPlay(job) {
        if (!job || !String(job.url || "").length) return
        win.closeDownloadsPage()
        var prog = Progress.get("video", job.id || "")
        var pos = (prog && prog.resume && Number(prog.resume.position || 0) > 0)
                  ? Number(prog.resume.position) : 0
        // disk-first (2026-07-31): with enough of the file on disk to probe and play
        // (8MB floor), the session opens on the .part instead of re-streaming bytes we
        // already have. Below the floor the stream url behaves exactly as before.
        var part = (Number(job.received || 0) > 8 * 1024 * 1024) ? String(job.partPath || "") : ""
        Sessions.openOrSwitch({
            "appType": "theatre", "contentKind": "movie", "title": job.title || "Video",
            "target": { "showKey": EpisodeBrowser.seriesRootId(job.id || ""),
                        "streamUrl": job.url, "partPath": part, "id": job.id || "",
                        "title": job.title || "",
                        "art": job.art || "", "kind": job.kind || "", "position": pos }
        })
    }
    // Landed-restore guard: an Arriving session restored after the download finished
    // (app restart) must not chase a dead url when the file is already on disk.
    function downloadedVideoPath(id) {
        if (!id || typeof Download === "undefined") return ""
        var vids = Download.downloadedVideos() || []
        for (var i = 0; i < vids.length; i++)
            if (vids[i].id === id) return String(vids[i].path || "")
        return ""
    }
    function routeDownloadWorld(worldKey) {
        win.closeDownloadsPage()
        var medium = worldKey === "tankoban" ? "Tankoban"
                   : (worldKey === "biblio" || worldKey === "audiobook") ? "Biblio" : "Theatre"
        win.openWorld(medium)
    }
    function routeDownloadedAudiobook(item) {
        if (!item || !String(item.bookPath || "").length) return
        win.closeDownloadsPage()
        win.openBookSession(item.bookPath, {
            "id": item.bookId || item.bookPath,
            "title": item.title || "Book",
            "author": item.author || "",
            "pairKey": item.id || "",
            "openAudio": true
        })
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

    // ---- the reader: the FRESH reader (reader2 — native QML chrome over the vendored Anx
    //      foliate paper) over everything (download-fed, never a stream). bookMeta stays on
    //      the layer for the session/Continue records; the shell itself needs only the path
    //      (it derives identity + resumes through Reader2Bridge/BookStores). ----
    function openBookReader(path, book) {
        if (!path) return
        bookReaderLayer.bookPath = path
        bookReaderLayer.bookMeta = book || ({})
        if (bookReaderLayer.active && bookReaderLayer.item) {
            bookReaderLayer.item.bookMeta = bookReaderLayer.bookMeta   // fresh catalog identity per book
            bookReaderLayer.item.openBook(path)
        } else bookReaderLayer.active = true
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
        if (data && data.notice) return   // a cooldown/status notice row isn't clickable content
        win.closeWorldSearch()
        if (worldSearchLayer.searchMode === "Tankoban") {
            if (data && data.locg) win.openComicSeries(data)        // LOCG catalogue series → resolve+attach
            else if (data && data.gcd) win.openGcdSeries(data)      // catalogue run page
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
            // Hosted-player resume FIRST: if this entry was a VidKing watch, route it back to
            // the hosted session — but ONLY while net.vidking.player is still installed AND
            // enabled. A disabled/removed VidKing must NOT bypass the extension switch; fall
            // through to Theatre detail so the user sees the real source choice.
            if (r.hostedPlayerId) {
                var vidkingLive = false
                if (typeof Extensions !== "undefined") {
                    var installed = Extensions.installed()
                    for (var i = 0; i < installed.length; ++i) {
                        if (installed[i].id === "net.vidking.player" && installed[i].enabled === true) {
                            vidkingLive = true
                            break
                        }
                    }
                }
                if (vidkingLive) {
                    win.openHostedPlayerSession({
                        "providerId": r.hostedPlayerId,
                        "extensionId": r.extensionId || "net.vidking.player",
                        "type": r.subType || "movie",
                        "imdbId": r.imdbId || "",
                        "tmdbId": r.tmdbId || 0,
                        "season": r.season || 0,
                        "episode": r.episode || 0,
                        "mediaId": r.subId || entry.id || "",
                        "title": title,
                        "backdrop": entry.cover || "",
                        "position": r.position || 0
                    })
                    return
                }
                // disabled/removed: open the Theatre detail instead of bypassing the switch
                win.openTheatreSeries({ "id": (r.imdbId || String(entry.id || "").split(":")[0]),
                                        "type": r.subType === "series" ? "series" : "movie",
                                        "title": title, "cover": entry.cover || "" })
                return
            }
            // downloaded file first: resume the LOCAL copy at position, never a stream fetch
            if (r.localPath && String(r.localPath).length)
                win.openLocalVideoSession({ "path": r.localPath, "id": entry.id || "",
                                            "title": title, "art": entry.cover || "",
                                            "kind": r.subType === "series" ? "episode" : "movie",
                                            "position": r.position || 0 })
            else if (r.infoHash) win.openMovieSession(r.infoHash, r.fileIdx || 0, title, entry.cover || "", r.subType || "", r.subId || "", [], {}, r.position || 0)
        } else if (entry.kind === "tankoban") {
            // a saved VOLUME read: same manga series, Tankoban Mode ON, the saved
            // volume id rides in resume.chapterId (curChapterId of the volume reader).
            win.openComicSession(title, entry.id || "", r.chapterId || "", "tankoban")
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
        } else if (entry.kind === "tankoban") {
            // detail = the manga series page; Tankoban Mode restores itself from the
            // service's per-series flag once the id resolves.
            win.openSeries(title)
        } else if (entry.kind === "manga" || entry.kind === "comic") {
            if (String(entry.id || "").indexOf("gc:") === 0)
                win.openWestern({ title: title, tag: String(entry.id).slice(3) })
            else if (String(entry.id || "").indexOf("gcd:") === 0)
                win.openGcdSeries({ gcdId: Number(String(entry.id).slice(4)), title: title,
                                    cover: entry.cover || "" })
            else if (entry.kind === "comic")
                // retired-source or unknown comic id — honest no-op (preset-pages source cut 2026-07-12);
                // comics open only via the gc: lane, so a stale id never opens the manga page
                console.log("[route] ignoring unknown comic id:", entry.id)
            else win.openSeries(title)                                   // manga → the chapter-list series page
        } else if (entry.kind === "book") {
            win.openBook(entry.resume && entry.resume.book ? entry.resume.book : entry)
        }
    }

    // A Collection tile always opens the DETAIL surface (saved is a bookmark, not a
    // promise it's downloaded/started). Routes by world + saved snapshot; the gc:/gcd:/
    // locg: prefixes pick the comics lane; manga reopens BY TITLE.
    function openCollectionEntry(e) {
        if (!e || !e.world) return
        var id = String(e.id || "")
        if (e.world === "theatre") {
            win.openTheatreSeries({ "id": id, "type": e.type || "series", "title": e.title || "",
                                    "cover": e.cover || "", "art": (e.payload && e.payload.art) || "" })
        } else if (e.world === "biblio") {
            win.openBook((e.payload && e.payload.book) || e)
        } else if (e.world === "tankoban") {
            if (e.type === "manga") { win.openSeries(e.title || id); return }
            if (id.indexOf("locg:") === 0) {
                win.openComicSeries({ "id": id, "title": e.title || "", "cover": e.cover || "",
                                      "locgMeta": (e.payload && e.payload.locgMeta) || null })
            } else if (id.indexOf("gcd:") === 0) {
                win.openGcdSeries({ "gcdId": parseInt(id.substring(4)), "title": e.title || "", "cover": e.cover || "" })
            } else {
                win.openWestern({ "title": e.title || "", "tag": (e.payload && e.payload.tag) || "",
                                  "tagId": (e.payload && e.payload.tagId) || 0, "cover": e.cover || "" })
            }
        }
    }

    // ===== OS-shell session engine (Approach 2: only the active surface is instantiated) =====
    // The UI opens content by registering a SESSION; Sessions.activeChanged then drives the
    // capture -> teardown -> build -> restore switch. contentKind picks the surface.

    // UI entry points (replace direct open* calls from cards / world pages):
    function openMovieSession(infoHash, fileIdx, title, backdrop, subType, subId, streamCandidates, playbackContext, position) {
        // Library membership (spec §4.4): the moment playback starts, it joins the shelf.
        // The show root is EpisodeBrowser.seriesRootId (tt123:1:2 → tt123 ; kitsu:9:3:4 → kitsu:9);
        // a movie's subId IS its id. Downloads keep auto-adding; one shelf, no saved-vs-watched split.
        var joinId = (subType === "series" && subId) ? EpisodeBrowser.seriesRootId(subId) : subId
        if (joinId && typeof Collection !== "undefined" && !Collection.has("theatre", String(joinId)))
            Collection.add("theatre", { "id": String(joinId),
                "type": (subType === "series") ? "series" : "movie",
                "title": title || "", "cover": backdrop || "", "payload": ({}) })
        Sessions.openOrSwitch({
            "appType": "theatre", "contentKind": "movie", "title": title || "Movie",
            "target": { "showKey": EpisodeBrowser.seriesRootId(subId || ""),
                        "infoHash": infoHash, "fileIdx": fileIdx || 0, "title": title || "",
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
            "target": { "showKey": EpisodeBrowser.seriesRootId(v.id || ""),
                        "localPath": v.path, "id": v.id || "", "title": v.title || "",
                        "art": v.art || "", "kind": v.kind || "", "position": v.position || 0 }
        })
    }
    // entryKind "tankoban" marks a VOLUME read (the same manga series, Tankoban Mode
    // ON); anything else is a chapter read. It rides the target so restore/resume
    // rebuilds the right surface.
    function openComicSession(title, seriesId, chapterId, entryKind) {
        Sessions.openOrSwitch({
            "appType": "tankoban", "contentKind": "comic", "title": title || "Comic",
            "target": { "title": title || "", "seriesId": seriesId || "", "chapterId": chapterId || "",
                        "entryKind": entryKind || "" }
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
    // A hosted (web-embed) playback session. contentKind "hosted-video" keeps it distinct
    // from a "movie"/mpv session, so a VidKing session and a torrent session for the SAME
    // episode can coexist as two taskbar tiles. Dedup is by provider + mediaId (the episode
    // stream id for series, the title id for movies) — re-opening the same VidKing row
    // switches to its tile rather than spawning a second iframe. The full typed request
    // rides as `target`; activate/capture/teardown read it back.
    function openHostedPlayerSession(request) {
        if (!request || !request.providerId || !request.mediaId) return
        // Theatre collection membership: same one-tile-per-show join as mpv playback. The
        // series root is the imdb base id (movies: the title id itself).
        var joinId = (request.type === "series" && request.imdbId)
                     ? EpisodeBrowser.seriesRootId(request.imdbId) : (request.imdbId || request.mediaId)
        if (joinId && typeof Collection !== "undefined" && !Collection.has("theatre", String(joinId)))
            Collection.add("theatre", { "id": String(joinId),
                "type": request.type === "series" ? "series" : "movie",
                "title": request.title || "", "cover": request.backdrop || "", "payload": ({}) })
        Sessions.openOrSwitch({
            "appType": "theatre", "contentKind": "hosted-video", "title": request.title || "Video",
            "target": {
                // showKey = one tile per show across episodes; the provider+mediaId pair is the
                // exact-content key (two different VidKing choices on one episode replace, not stack).
                "showKey": EpisodeBrowser.seriesRootId(request.imdbId || request.mediaId),
                "hostedPlayerId": request.providerId,
                "extensionId": request.extensionId, "id": request.mediaId,
                "type": request.type, "imdbId": request.imdbId, "tmdbId": request.tmdbId,
                "season": request.season || 0, "episode": request.episode || 0,
                "mediaId": request.mediaId, "title": request.title || "", "backdrop": request.backdrop || "",
                "position": request.position || 0
            }
        })
    }
    // (openAudiobookSession retired 2026-07-18 — the standalone audiobook player is gone;
    // the READER is the one audiobook surface. AudiobookSession, the engine, lives on below.)

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
        if (rec && rec.contentKind === "hosted-video") {
            // close destroys the embed outright: stop() writes final progress, then the Loader
            // unloads (teardownSession already ran above if active, but a non-active hosted
            // session also needs its surface dropped). No warm iframe survives a close.
            if (hostedPlayerLayer.item) hostedPlayerLayer.item.stop()
            win.hostedPlayerOpen = false
            hostedPlayerLayer.active = false
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
    // Hosted-player chrome mirrors minimizePlayer/closePlayerSession but the surface is
    // a hosted-video session: minimize parks it (capture + teardown + UNLOAD), close ends
    // it. Both unload the Loader so the off-the-record WebEngine page/profile are destroyed
    // — no warm hidden iframe survives minimize or close.
    function minimizeHostedPlayer() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "hosted-video") Sessions.switchTo("")
        else win.minimizePlayer()                  // not the active session: defer to the movie path
    }
    function closeHostedPlayerSession() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "hosted-video") win.closeSession(rec.id)
        else win.closePlayerSession()
    }
    function minimizeComicReader() {
        var rec = Sessions.get(Sessions.activeId)
        if (!(rec && rec.contentKind === "comic")) {
            // reading began from a browse (no session yet) — register it from the live reader.
            // Check every comic lane (LOCG-catalogue / western / manga) — the reader chrome is shared.
            var x = comicSeriesLayer.active ? comicSeriesLayer.item : null
            var w = westernLayer.active ? westernLayer.item : null
            if (x && x.openChapterId) {
                win.openComicSession(x.seriesTitle, "gc:" + x.gcTag, x.openChapterId)   // LOCG page reads GetComics content
            } else if (w && w.openChapterId) {
                win.openComicSession(w.seriesTitle, w.seriesId, w.openChapterId)   // seriesId = "gc:<slug>" (live) or "gcd:<id>" (baked catalogue run)
            } else {
                var s = seriesLayer.item
                if (!s || !s.openChapterId) { win.closeSeries(); return }
                win.openComicSession(s.seriesTitle, s.seriesId, s.openChapterId, s.openEntryKind)
            }
        }
        Sessions.switchTo("")
    }
    function closeComicReader() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "comic") win.closeSession(rec.id)
        else if (comicSeriesLayer.active && comicSeriesLayer.item && comicSeriesLayer.item.openChapterId.length) win.closeComicSeries()
        else if (westernLayer.active && westernLayer.item && westernLayer.item.openChapterId.length) win.closeWestern()
        else win.closeSeries()
    }
    // Book minimize is BACK (2026-07-18, Hemanth — the swap had dropped the affordance):
    // the fresh reader's chrome carries a minimize icon → minimized() lands here. Every
    // live open path registers a session first (openBookSession), so this is normally just
    // a park; the register-if-missing arm mirrors minimizeComicReader's defensive shape.
    function minimizeBookReader() {
        var rec = Sessions.get(Sessions.activeId)
        if (!(rec && rec.contentKind === "book")) {
            if (!bookReaderLayer.active || !bookReaderLayer.bookPath.length) { win.closeBookReader(); return }
            win.openBookSession(bookReaderLayer.bookPath, bookReaderLayer.bookMeta)
        }
        Sessions.switchTo("")
    }
    function closeBookReaderSession() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "book") win.closeSession(rec.id)
        else win.closeBookReader()
    }
    // (minimizeAudiobook / closeAudiobookSession retired with the standalone player, 2026-07-18.)

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
                else if (t.streamUrl && String(t.streamUrl).length) {
                    // arriving download: watch the same url live; if it landed since
                    // (restart restore), prefer the local copy over a dead url.
                    var landed = win.downloadedVideoPath(t.id || "")
                    if (landed.length) {
                        t.localPath = landed
                        playerLayer.item.playLocalFile(t)
                    } else if (t.partPath && String(t.partPath).length) {
                        // disk-first (2026-07-31): the job's .part already holds real bytes —
                        // read those instead of re-streaming them. The player hands over to
                        // arrivingUrl only if the watcher outruns the download frontier.
                        t.localPath = t.partPath
                        t.arrivingUrl = t.streamUrl
                        playerLayer.item.playLocalFile(t)
                    } else {
                        playerLayer.item.playRemoteUrl(t)
                    }
                } else
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
        } else if (rec.contentKind === "hosted-video") {
            // Hosted playback always rebuilds the embed: the page + its off-the-record profile
            // were DESTROYED on minimize (active = false), so there is no warm iframe to resume
            // into. open(request) reloads the wrapper at the requested/captured position.
            if (!hostedPlayerLayer.active) hostedPlayerLayer.active = true
            win.hostedPlayerOpen = true
            if (hostedPlayerLayer.item) {
                hostedPlayerLayer.item.open(t)
                var hostedResume = (st && Number(st.position) > 0)
                                   ? st : { "position": Number(t.position) || 0 }
                if (hostedPlayerLayer.item.restoreState) hostedPlayerLayer.item.restoreState(hostedResume)
            }
        } else if (rec.contentKind === "comic") {
            if (String(t.seriesId || "").indexOf("gc:") === 0) {
                // GetComics content (western shelf OR LOCG-catalogue page) restores via the
                // GetComics shelf — same tag, same reader, resumed at the chapter.
                win.openWesternAt(t.title, String(t.seriesId).slice(3), (st.chapterId || t.chapterId || ""))
                if (westernLayer.item && westernLayer.item.restoreState) westernLayer.item.restoreState(st)
                return
            }
            if (String(t.seriesId || "").indexOf("gcd:") === 0) {
                // catalogue run page (baked mode, spec 2026-07-17) restores via the same
                // western shelf, baked branch — same run, same reader, resumed at the chapter.
                win.openGcdSeries({ gcdId: Number(String(t.seriesId).slice(4)), title: t.title,
                                    resumeChapterId: (st.chapterId || t.chapterId || "") })
                if (westernLayer.item && westernLayer.item.restoreState) westernLayer.item.restoreState(st)
                return
            }
            var savedComicId = (st.chapterId || t.chapterId || "")
            if (t.entryKind === "tankoban") {
                // a VOLUME session: restore the series with Tankoban Mode ON, then open the
                // saved volume through the shared reader (never the chapter path).
                seriesLayer.resumeSeriesId = t.seriesId || ""
                seriesLayer.resumeChapterId = ""
                seriesLayer.resumeVolumeId = savedComicId
                seriesLayer.title = t.title
                if (seriesLayer.active && seriesLayer.item) {
                    seriesLayer.item.seriesTitle = t.title
                    if (t.seriesId) seriesLayer.item.seriesId = t.seriesId
                    seriesLayer.item.resumeTankobanVolume(savedComicId)
                } else seriesLayer.active = true
                return
            }
            seriesLayer.resumeSeriesId = t.seriesId || ""
            seriesLayer.resumeChapterId = savedComicId
            seriesLayer.resumeVolumeId = ""
            seriesLayer.title = t.title
            if (seriesLayer.active && seriesLayer.item) {
                seriesLayer.item.seriesTitle = t.title
                if (t.seriesId) seriesLayer.item.seriesId = t.seriesId
                seriesLayer.item.openEntryKind = "manga"   // a reused item may still be in a volume read
                seriesLayer.item.openChapterId = savedComicId
            } else seriesLayer.active = true
            if (seriesLayer.item && seriesLayer.item.restoreState) seriesLayer.item.restoreState(st)  // Task 4
        } else if (rec.contentKind === "book") {
            bookReaderLayer.bookPath = t.path
            bookReaderLayer.bookMeta = t.book || ({})
            // Fresh-reader contract (the old item.open(path, book) survived the swap here and
            // THREW when the layer was already live — Continue resume into an open reader died).
            if (bookReaderLayer.active && bookReaderLayer.item) {
                bookReaderLayer.item.bookMeta = bookReaderLayer.bookMeta
                bookReaderLayer.item.openBook(t.path)
                if (bookReaderLayer.bookMeta.openAudio)
                    Qt.callLater(function() {
                        if (bookReaderLayer.item) bookReaderLayer.item.openAudioPanel()
                    })
            } else bookReaderLayer.active = true
            // book precision: the reader restores its own saved position on reopen (resume seam).
        }
        // ('audiobook' sessions retired with the standalone player, 2026-07-18 — a stale
        // taskbar record of that kind now activates to nothing and closes normally.)
    }
    // capture the live outgoing surface's state (called BEFORE teardown).
    function captureSession(rec) {
        if (!rec || !rec.id) return ({})
        if (rec.contentKind === "movie" && playerLayer.item && playerLayer.item.captureState) return playerLayer.item.captureState()
        if (rec.contentKind === "hosted-video" && hostedPlayerLayer.item && hostedPlayerLayer.item.captureState)
            return hostedPlayerLayer.item.captureState()
        if (rec.contentKind === "comic") {
            // one comic surface hosts the reader at a time — capture from whichever is live
            var lay = comicSeriesLayer.active ? comicSeriesLayer
                    : (westernLayer.active ? westernLayer : seriesLayer)
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
        } else if (rec.contentKind === "hosted-video") {
            // minimize writes final progress and halts the embed (suspendForMinimize), then
            // the Loader is UNLOADED — the WebEngine page and its off-the-record profile are
            // destroyed immediately. There is no warm hidden iframe; restore rebuilds it.
            if (hostedPlayerLayer.item) hostedPlayerLayer.item.suspendForMinimize()
            win.hostedPlayerOpen = false
            hostedPlayerLayer.active = false
        } else if (rec.contentKind === "comic") {
            // one comic surface hosts the reader at a time — drop whichever is live
            if (comicSeriesLayer.active) comicSeriesLayer.active = false
            else if (westernLayer.active) westernLayer.active = false
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

    // ---- reading serif: Literata statics for the FRESH reader's chrome (Appearance panel
    // card + serif UI). STATICS for the same reason as Inter above — the variable TTF would
    // register as "Literata Variable" and silently Tahoma-fall. The BOOK text gets Literata
    // separately via @font-face injected into the paper page (paper_glue.js FONT_FACE_CSS). ----
    FontLoader { source: "../assets/fonts/Literata-Regular.ttf" }
    FontLoader { source: "../assets/fonts/Literata-Italic.ttf" }

    // =====================================================================
    // BACKDROP — the persistent wallpaper everything composites over.
    // =====================================================================
    Item {
        id: wall
        anchors.fill: parent
        // Not painted while the player is up. The player layer is opaque (PlayerPage draws a
        // full-bleed black Rectangle at z:-1) and does not fade, so nothing below it is ever
        // seen — yet Qt kept rendering all of it every frame, on a GPU the film needs. Costs
        // nothing visually; the object and its caches stay alive, so returning from the player
        // re-shows instantly with no re-fetch. (2026-07-29 render-load diet.)
        visible: !win.immersiveSurfaceOpen
        // Real OS wallpaper — a placeholder PICK (Windows 11 "Captured Motion"; its translucent
        // glass-ribbon motif echoes our material, and it's dark enough for the glass to read).
        // Swap from the parked personalization gallery later. Glass composites over WHATEVER sits in
        // `wall`, so the Image "just works" — and it pops against the chrome instead of reading as an app.
        Image {
            anchors.fill: parent
            source: win.wallpaperIsNative ? "" : win.wallpaperSource
            visible: !win.wallpaperIsNative
            fillMode: Image.PreserveAspectCrop
            cache: true
        }
        // Native living wallpaper (the arena et al.): a QML scene in the Image's place.
        // Motion doctrine: it FREEZES whenever a reader/player owns the screen or the
        // window is minimized — ambient motion only while the shell is being looked at.
        Loader {
            anchors.fill: parent
            active: win.wallpaperIsNative && win.nativeWallpaperFile(win.wallpaperSource).length > 0
            source: active ? win.nativeWallpaperFile(win.wallpaperSource) : ""
            onLoaded: item.running = Qt.binding(function() {
                return !win.immersiveSurfaceOpen && win.visibility !== Window.Minimized
            })
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

    // (RowHeader — the old hover-reveal row header — is gone: its one user was the home
    //  Continue row, which now wears the SAME WidgetHeader as the world rows, so the
    //  "See all ›" affordance reads identically on every Continue surface.)

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
        visible: !win.immersiveSurfaceOpen   // see the note on `wall` — covered by the player, never seen
        backdrop: wall
        activeMedium: ""
        x: theme.margin; y: 30
        width: win.width - theme.margin * 2
        onMediumSelected: (medium) => win.openWorld(medium)
        onWallpaperClicked: win.openWallpaperSearch("Home")
        onFullscreenClicked: win.toggleFullscreenShell()
        onMinimizeClicked: win.minimizeShell()
        onPowerClicked: Qt.quit()
    }

    // Chrome-free desktop interaction for developer-windowed mode. Reuses the existing TopBar
    // as the drag surface (no titlebar added) and self-disables in fullscreen. See WindowBehavior.qml.
    WindowBehavior {
        shell: win
        dragSurface: topbar
        controller: WindowMode
    }

    // ---- pinned top bar is above; everything below SCROLLS (vertical wheel/drag) ----
    Flickable {
        id: page
        z: 0
        visible: !win.immersiveSurfaceOpen   // see the note on `wall` — covered by the player, never seen
        anchors.left: parent.left; anchors.right: parent.right
        y: 96
        height: win.height - 96
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 40
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }   // gold sliver, same as every page
        // the HOME page never had the eased wheel — the one surface scrolled most was the
        // one raw Flickable left (Hemanth: rough on the hand, 2026-07-12)
        ScrollGlide { flick: page }

        Column {
            id: contentCol
            x: theme.margin
            width: win.width - theme.margin * 2
            topPadding: 10
            spacing: 30

            // ---- 2. UNIVERSE HERO — full-bleed banner + left scrim (the original treatment,
            //      restored by Hemanth's call 2026-07-12: "should have a full banner, just like
            //      how it was initially"). What STAYS ratified-out from the redesign round:
            //      NO dots, NO tabs, NO timer bar, NO media-count chips. A pure carousel —
            //      auto-turns (6.5s) + native swipe. Spec trail: haven docs/superpowers/specs/
            //      2026-07-12-colosseum-universe-exhibit-hero-design.md (+ this final rev).
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
                        model: win.installedUniverses
                        delegate: Item {
                            id: slide
                            required property var modelData

                            // banner key-art full-bleed; a neutral plate stands in while it
                            // loads, then the left-weighted scrim keeps the words legible
                            // (proven look). The per-IP accent colour the baked list carried is
                            // gone with it — an installed universe supplies art, not a palette.
                            Rectangle {
                                anchors.fill: parent; radius: hero.radius; clip: true
                                color: "#1a1410"
                                Image {
                                    anchors.fill: parent
                                    source: slide.modelData.banner
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

                            // content over the scrim (chips row retired — ratified 2026-07-12)
                            Column {
                                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 44
                                spacing: 12
                                Text { text: "UNIVERSE"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
                                Text { text: slide.modelData.name; color: theme.ink; font.family: theme.display; font.pixelSize: 48 }
                                // (the blurb line went with the baked list — the roster carries
                                // identity and art, no prose, and a faked one would be a lie)
                                Row {
                                    spacing: 12; topPadding: 6
                                    Rectangle {
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
                                                transform: Translate { x: exMa.containsMouse ? 3 : 0 } }
                                        }
                                        MouseArea {
                                            id: exMa; anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: win.openUniverse(slide.modelData.extensionId,
                                                                        slide.modelData.name)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // gentle auto-advance through the collection (not visualized — ratified)
                Timer {
                    // Stands down while the player is up: this advances a carousel nobody can see
                    // behind the film, and every advance is an animation plus cover work on the
                    // GUI thread — the thread Qt Quick needs free to present video frames.
                    interval: 6500; running: !win.immersiveSurfaceOpen; repeat: true
                    // guarded: an empty roster would turn the modulo into NaN
                    onTriggered: if (heroView.count > 0)
                                     heroView.currentIndex = (heroView.currentIndex + 1) % heroView.count
                }

                // the door to the Hall of Worlds — quiet, top-right, gold on hover
                Item {
                    z: 5
                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 20
                    width: hallRow.implicitWidth + 8; height: 28
                    Row {
                        id: hallRow
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        spacing: 7
                        Text { text: win.installedUniverses.length + " worlds"
                               color: hallMa.containsMouse ? theme.gold : theme.inkDim
                               font.family: theme.display; font.pixelSize: 16
                               Behavior on color { ColorAnimation { duration: 120 } } }
                        Text { text: "›"
                               color: hallMa.containsMouse ? theme.gold : theme.inkDimmer
                               font.family: theme.display; font.pixelSize: 19
                               anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea {
                        id: hallMa; anchors.fill: parent
                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: win.openUniverseHall()
                    }
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
                    // 'audiobook' records persist ONLY as resume positions for the reader's
                    // read-along (Hemanth 2026-07-18: audio progress rides the BOOK — the
                    // book's own tile represents both). Never surface them as tiles.
                    var a = Progress.recent("", 12).filter(function(e) { return e.kind !== "audiobook" })
                    return a.filter(function(e) { return e.watched !== true })
                            .concat(a.filter(function(e) { return e.watched === true }))
                })())
                visible: contItems.length > 0
                // same header as the world Continue rows — "See all ›" visibly present, not hover-gated
                WidgetHeader {
                    width: parent.width; title: "Continue"
                    moreLabel: "See all"
                    onMoreClicked: win.openContinueSeeAll("home")
                }
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
    // ---- Stage 2 warming: after the home screen settles, quietly pre-build the world
    //      pages the user hasn't opened yet — one at a time, only while on Home — so the
    //      first click lands on an already-painted page instead of paying the cold build.
    //      Each append builds a hidden async Loader (worldStack) and pre-caches its covers.
    Timer {
        id: warmStart
        interval: 2500          // let the home page finish its own first paint first
        running: true
        repeat: false
        onTriggered: warmer.running = true
    }
    Timer {
        id: warmer
        interval: 1800          // stagger: one world at a time, easy on CPU/network
        running: false
        repeat: true
        readonly property var targets: ["Tankoban", "Theatre", "Biblio"]
        onTriggered: {
            // Yield while a world is open OR while the player is up. The player check was missing
            // and it was THE video stutter (2026-07-29): opening the player from Home leaves
            // worldStack.current === "", so this kept firing every 1.8s behind the film, and each
            // append builds a ~190-tile world page plus its cover pre-cache. Even with an async
            // Loader, component completion, bindings and image decode land on the GUI thread — and
            // Qt Quick cannot present a video frame while the GUI thread is busy. Measured: 130
            // GUI-thread stalls in a 76s playback, 10.3s blocked, worst single stall 1094ms, which
            // is exactly the hitch Hemanth reported. Everything else in this file already stands
            // down for immersiveSurfaceOpen (wallpaper animation :1423, download reveal :2328/:2337,
            // chrome :2345); the warmer was the one that did not.
            if (worldStack.current !== "" || win.immersiveSurfaceOpen) return   // → yield, retry next tick
            var names = []
            for (var i = 0; i < openModes.count; i++) names.push(openModes.get(i).mode)
            var next = Warming.nextWarmMode(names, warmer.targets)
            if (next === "") { warmer.running = false; return }   // everything warmed → stop
            openModes.append({ mode: next })               // builds its hidden async Loader
        }
    }
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
                // Stage 2: build off the GUI thread so instantiating a world (~190 tiles)
                // never freezes the app — the page fills in progressively, and warming
                // (below) builds it hidden ahead of the first click.
                asynchronous: true
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
                    var comicSeriesSignal = item["comicSeriesRequested"]
                    if (comicSeriesSignal) comicSeriesSignal.connect(win.openComicSeries)
                    var gcdSignal = item["gcdSeriesRequested"]
                    if (gcdSignal) gcdSignal.connect(win.openGcdSeries)
                    var locgPubSignal = item["locgPublisherRequested"]
                    if (locgPubSignal) locgPubSignal.connect(win.openLocgPublisher)
                    var comicBoardSignal = item["comicArchiveBoardRequested"]
                    if (comicBoardSignal) comicBoardSignal.connect(win.openComicArchiveBoard)
                    var biblioGenreSignal = item["biblio" + "GenreRequested"]
                    if (biblioGenreSignal) biblioGenreSignal.connect(win.openBiblioGenre)
                    var biblioGenreIndexSignal = item["biblio" + "GenreIndexRequested"]
                    if (biblioGenreIndexSignal) biblioGenreIndexSignal.connect(win.openBiblioGenreIndex)
                    if (item.continueResumeRequested) item.continueResumeRequested.connect(win.resumeContinue)
                    if (item.continueDetailRequested) item.continueDetailRequested.connect(win.detailContinue)
                    if (item.collectionOpenRequested) item.collectionOpenRequested.connect(win.openCollectionEntry)
                    if (item.libraryResumeRequested) item.libraryResumeRequested.connect(win.resumeLibraryEntry)
                    if (item.libraryMarkWatchedRequested) item.libraryMarkWatchedRequested.connect(win.markLibraryWatched)
                    if (item.continueSeeAllRequested) item.continueSeeAllRequested.connect(function() {
                        win.openContinueSeeAll(mode === "Theatre" ? "video"
                                             : mode === "Biblio"  ? "book" : "tankoban")
                    })
                    if (item.wallpaperClicked) item.wallpaperClicked.connect(function() { win.openWallpaperSearch(mode) })
                    // Next Up rows (spec 2026-07-18): Theatre's direct play walks the same
                    // openMovieSession door as the series page; Tankoban's read walks the
                    // same openComicSession door as Continue resume. Guarded — only the
                    // worlds that declare the signals connect.
                    var nextUpPlay = item["playRequested"]
                    if (nextUpPlay) nextUpPlay.connect(win.openMovieSession)
                    var nextUpRead = item["nextUpReadRequested"]
                    if (nextUpRead) nextUpRead.connect(win.openComicSession)
                    // Thread the global Explicit Content preference into every world's
                    // inherited WorldPage.showExplicitContent (Task 7 Step 4). Tankoban's
                    // Discover wall reads it now; Theatre/Biblio Discover walls read it
                    // via Task 9. The property is guarded so a world that pre-dates the
                    // inherited field (none currently, but defensive) loads cleanly.
                    if (item.showExplicitContent !== undefined)
                        item.showExplicitContent = Qt.binding(function() { return contentPreferences.showExplicit })
                    // Biblio's Discover/Explore split (Task 8) reads its OWN showExplicit
                    // property — distinct from the inherited WorldPage.showExplicitContent above
                    // (Tankoban's Discover wall) — same guarded-binding shape either way.
                    if (item.showExplicit !== undefined)
                        item.showExplicit = Qt.binding(function() { return contentPreferences.showExplicit })
                    if (mode === "Theatre") {
                        item.contentPreferences = contentPreferences   // the one global preference into the deep catalogue
                        var theatreSignal = item["theatre" + "ItemRequested"]
                        if (theatreSignal) theatreSignal.connect(win.openTheatreSeries)
                        var tgSignal = item["theatre" + "GenreRequested"]
                        if (tgSignal) tgSignal.connect(win.openTheatreGenre)
                        var tgiSignal = item["theatre" + "GenreIndexRequested"]
                        if (tgiSignal) tgiSignal.connect(win.openTheatreGenreIndex)
                    }
                    item.searchClicked.connect(win.openSearch)
                    if (item.fullscreenClicked) item.fullscreenClicked.connect(win.toggleFullscreenShell)
                    item.minimizeClicked.connect(win.minimizeShell)
                    item.powerClicked.connect(function() { Qt.quit() })
                }
            }
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
            item.showExplicitContent = Qt.binding(function() { return contentPreferences.showExplicit })
            item.backRequested.connect(win.closeGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
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
            item.showExplicitContent = Qt.binding(function() { return contentPreferences.showExplicit })
            item.backRequested.connect(win.closeGenreIndex)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
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
            item.showExplicitContent = Qt.binding(function() { return contentPreferences.showExplicit })
            item.backRequested.connect(win.closeBiblioGenreIndex)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
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
            item.showExplicitContent = Qt.binding(function() { return contentPreferences.showExplicit })
            item.backRequested.connect(win.closeBiblioGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
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
            item.showExplicitContent = Qt.binding(function() { return contentPreferences.showExplicit })
            item.backRequested.connect(win.closeTheatreGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
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
            item.showExplicitContent = Qt.binding(function() { return contentPreferences.showExplicit })
            item.backRequested.connect(win.closeTheatreGenreIndex)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
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
        z: 53     // above the universe overlay (z:52): a manga opened from a universe stacks on top, back returns to the universe
        active: false
        visible: active
        property string title: ""
        property string resumeSeriesId: ""    // Continue resume: jump straight to this chapter…
        property string resumeChapterId: ""   //   …in this series (set seriesId BEFORE the chapter)
        property string resumeVolumeId: ""    // Tankoban resume: open this VOLUME (Mode ON) instead
        source: "MangaSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = seriesLayer.title
            if (seriesLayer.resumeSeriesId) item.seriesId = seriesLayer.resumeSeriesId
            if (seriesLayer.resumeChapterId) item.openChapterId = seriesLayer.resumeChapterId
            if (seriesLayer.resumeVolumeId) item.resumeTankobanVolume(seriesLayer.resumeVolumeId)
            item.backRequested.connect(win.closeSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            // the READER's own chrome (not the page topbar): session verbs
            item.readerMinimizeRequested.connect(win.minimizeComicReader)
            item.readerFullscreenRequested.connect(win.toggleFullscreenShell)
            item.readerCloseRequested.connect(win.closeComicReader)
            item.readerBackRequested.connect(win.closeComicReader)
        }
    }

    // ---- western-comics detail layer: the GetComics shelf (ComicSeries), over the world ----
    Loader {
        id: westernLayer
        anchors.fill: parent
        z: 53     // above the universe overlay (z:52): a comic opened from a universe stacks on top, back returns to the universe
        active: false
        visible: active
        property string title: ""
        property string tagSlug: ""
        property int    tagId: 0
        property string resumeChapterId: ""   // Continue/session resume: straight into the reader
        property var    baked: null           // catalogue run page (spec 2026-07-17): {gcdId,releases,cover}
        source: "ComicSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = westernLayer.title
            item.tagId = westernLayer.tagId
            // Resolve the baked IDENTITY (-> seriesId "gcd:<id>") BEFORE opening the reader,
            // so ComicReaderShell mounts with a STABLE seriesId and its resume reads the RIGHT
            // progress key. Opening the reader first mounted it under the transient
            // "gc:<empty-slug>" identity -> restored page 1, then saved to the OTHER key and
            // wrote page 1 over the real record (runtime-confirmed 2026-08-06). Same fix as
            // openGcdSeries() above.
            if (westernLayer.baked) {
                item.poster = westernLayer.baked.cover || item.poster
                item.gcdId = westernLayer.baked.gcdId
                item.bakedReleases = westernLayer.baked.releases   // seriesId now "gcd:<id>"
            }
            item.tagSlug = westernLayer.tagSlug        // triggers resolve() (no-op when baked); live-mode identity
            if (westernLayer.resumeChapterId) item.openChapterId = westernLayer.resumeChapterId   // open reader LAST — identity stable
            item.backRequested.connect(win.closeWestern)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.readerMinimizeRequested.connect(win.minimizeComicReader)
            item.readerFullscreenRequested.connect(win.toggleFullscreenShell)
            item.readerCloseRequested.connect(win.closeComicReader)
            item.readerBackRequested.connect(win.closeComicReader)
        }
    }

    // ---- comic series layer: a LOCG-catalogue comic's issue list with GetComics content
    //      attached (peer of westernLayer) ----
    Loader {
        id: comicSeriesLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property string title: ""
        property string cover: ""
        property string locgSid: ""          // "locg:<id>" — the catalogue entry
        property var locgMeta: ({})          // {publisher, rating, startYear…} enriches the hero
        source: "ComicSeriesPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = comicSeriesLayer.title
            item.cover = comicSeriesLayer.cover
            item.backRequested.connect(win.closeComicSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.readerMinimizeRequested.connect(win.minimizeComicReader)
            item.readerFullscreenRequested.connect(win.toggleFullscreenShell)
            item.readerCloseRequested.connect(win.closeComicReader)
            item.readerBackRequested.connect(win.closeComicReader)
            item.locgMeta = comicSeriesLayer.locgMeta
            item.locgId = comicSeriesLayer.locgSid       // set LAST — triggers attach()
        }
    }

    // On-demand comics-catalog ingest (see comicsDbHit) — never active at startup.
    Loader {
        id: comicsDbLoader
        active: false
        source: "ComicsDbLoader.qml"
    }

    // Bakeoff-only page surface (COLOSSEUM_BAKEOFF_STRIP): topmost so the blind
    // trial shows nothing but pages — no world chrome, no identity tells.
    Loader {
        id: bakeoffStripLayer
        anchors.fill: parent
        z: 200
        active: false
        visible: active
        source: "BakeoffStripHost.qml"
    }

    // ---- Continue see-all layer: the whole resume backlog, scoped per door (home/world).
    //      z 49: above world pages, below the detail/series layers (z 50+) so a tapped tile
    //      opens its detail OVER the backlog and back returns here. ----
    Loader {
        id: continueSeeAllLayer
        anchors.fill: parent
        z: 49
        active: false
        visible: active
        property string scope: "home"
        source: "ContinueSeeAllPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.scope = continueSeeAllLayer.scope
            item.backRequested.connect(win.closeContinueSeeAll)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.resumeRequested.connect(win.resumeContinue)     // same sinks the rows use
            item.detailRequested.connect(win.detailContinue)
        }
    }


    // ---- LOCG publisher grid layer: one publisher shelf's paginated series grid
    //      (tile → LOCG series list via openComicSeries) ----
    Loader {
        id: locgPublisherLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property var box: ({})
        source: "LocgPublisherPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.box = locgPublisherLayer.box
            item.backRequested.connect(win.closeLocgPublisher)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.seriesRequested.connect(win.openComicSeries)   // tile → LOCG series list (over this grid)
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
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
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
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.westernPicked.connect(win.openWestern)
            item.allReleasesRequested.connect(win.openWestern)
        }
    }

    // ---- Theatre detail layer: opened from a Theatre tile, sits OVER the world page ----
    Loader {
        id: theatreSeriesLayer
        anchors.fill: parent
        z: 53     // above the universe overlay (z:52): a film/show opened from a universe stacks on top, back returns to the universe
        active: false
        visible: active
        property var pendingItem: ({})
        source: "TheatreSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.itemData = theatreSeriesLayer.pendingItem
            item.backRequested.connect(win.closeTheatreSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.playRequested.connect(win.openMovieSession)
            item.playLocalRequested.connect(win.openLocalVideoSession)
            item.playArrivingRequested.connect(win.routeArrivingPlay)
            item.openItemRequested.connect(win.openTheatreSeries)
            // A hosted-player selection from SourcesSheet arrives as a typed request; route it
            // to the hosted session (never playRequested — that is the mpv/torrent path).
            item.hostedPlayerRequested.connect(win.openHostedPlayerSession)
        }
    }

    // ---- video player layer: above every detail/series layer (mpv under house glass) ----
    Loader {
        id: playerLayer
        anchors.fill: parent
        z: 60
        active: false
        visible: win.playerOpen
        // The ONE production line that selects the backend. Player2Page.qml deliberately exposes the
        // same interface as PlayerPage.qml, so every playerLayer.item.* call site below stays as-is.
        source: win.usePlayer2 ? "player2host/Player2Page.qml" : "PlayerPage.qml"
        onLoaded: {
            // Say which engine is driving, every time the player opens. Without this, telling the two
            // apart means reading process paths after the fact - which is exactly what happened on the
            // first swap attempt (2026-07-25).
            console.log("[player] backend = " + (win.usePlayer2 ? "PLAYER 2" : "mpv (player 1)")
                        + "  (booted=" + Player2Available + ")")
            item.backdrop = wall
            item.backRequested.connect(win.minimizePlayer)
            item.minimizeRequested.connect(win.minimizePlayer)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(win.closePlayerSession)
            // Player-2-only seams; absent on the mpv page, hence the guards. Log only: there is
            // no backend to swap to in this boot, so the page itself shows the failure.
            if (item.backendFallback)
                item.backendFallback.connect(function(reason) {
                    console.warn("[player2] declined/failed before first frame: " + reason)
                })
            if (item.backendRestartRequired)
                item.backendRestartRequired.connect(function(reason) {
                    console.warn("[player2] failed after first frame: " + reason)
                })
        }
    }

    // ---- hosted (web-embed) player layer: beside playerLayer at the SAME immersive z-level
    //      (z:60), full-window. It is a SEPARATE Loader — never inside the native player layer,
    //      and never swapping usePlayer2. Unlike playerLayer it is UNLOADED (active = false) on
    //      minimize/close so the WebEngine page and its off-the-record profile are destroyed;
    //      no warm hidden iframe survives. visible follows hostedPlayerOpen (the session flag). ----
    Loader {
        id: hostedPlayerLayer
        anchors.fill: parent
        z: 60
        active: false
        visible: win.hostedPlayerOpen
        source: "HostedPlayerPage.qml"
        onLoaded: {
            item.backdrop = wall
            // Back to Sources: close the hosted session + replay the Theatre Sources sheet.
            item.backRequested.connect(win.reopenSources)
            item.minimizeRequested.connect(win.minimizeHostedPlayer)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(win.closeHostedPlayerSession)
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
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.readRequested.connect(win.openBookSession)
            // (listenRequested retired — the reader carries audiobook playback now)
        }
    }

    // ---- the ONE audiobook engine (read-along), hoisted at the window root (never in a
    // Loader) so playback survives the reader opening/closing. The READER is the only
    // audiobook surface (standalone player retired 2026-07-18): its HUD transport pill
    // and Audio tab both drive this session. ----
    AudiobookSession { id: audioSession }

    Loader {
        id: bookReaderLayer
        anchors.fill: parent
        z: 58
        active: false
        visible: active
        property string bookPath: ""
        property var bookMeta: ({})
        // The FRESH reader (Task 16 swap): reader2/ReaderShell replaces the imported TB2
        // foliate web-app. Contract mapping per the ratified plan: open(path, meta) →
        // openBook(path) (meta stays on this layer for session records), closed() → the same
        // session-aware close. minimized() (restored 2026-07-18 on Hemanth's call) parks the
        // book as a taskbar tile via minimizeBookReader — the resume seam reopens it in place.
        source: "reader2/ReaderShell.qml"
        onLoaded: {
            item.audioSession = audioSession   // the ONE shared audiobook engine → read-along Audio tab
            item.bookMeta = bookReaderLayer.bookMeta   // catalog identity → the pairing self-heal
            item.openBook(bookReaderLayer.bookPath)
            if (bookReaderLayer.bookMeta.openAudio)
                Qt.callLater(function() {
                    if (bookReaderLayer.item) bookReaderLayer.item.openAudioPanel()
                })
            item.closed.connect(win.closeBookReaderSession)
            item.minimized.connect(win.minimizeBookReader)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
        }
    }

    // (The standalone audiobook player layer is retired — 2026-07-18, Hemanth: the reader
    // IS the audiobook player. AudiobookSession above stays: it is the ENGINE the reader's
    // HUD transport and Audio tab drive.)

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
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
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
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- Biblio series detail layer: opened from a SERIES card (above search, below the book detail) ----
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
        z: 56     // taskbar full-page: ABOVE every browsing/detail page (universe 52, series/book 53)
                  // so clicking Downloads always lands on top — below only the immersive surfaces
                  // (book reader 58, player 60). Was 52: any detail page silently covered it.
        active: false
        visible: active
        source: "DownloadsPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeDownloadsPage)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.openRequested.connect(win.routeDownloadItem)
            item.openWorldRequested.connect(win.routeDownloadWorld)
            item.playArrivingRequested.connect(win.routeArrivingPlay)
            item.openAudiobookRequested.connect(win.routeDownloadedAudiobook)
        }
    }

    Connections {
        target: typeof Extensions !== "undefined" ? Extensions : null
        function onChanged() {
            TheatreApi.setExtensions(Extensions.installed())
            Subtitles.setExtensions(Extensions.installed())
            win.installedExtensions = Extensions.installed()
        }
    }

    // Task 9: re-push the global Explicit Content preference into TheatreApi whenever it
    // changes, so the boot-time marquee rows that read the module flag (not a Qt.binding)
    // pick up the new value on their next fetch. The page-bound properties re-evaluate on
    // their own (Qt.binding); this covers the .pragma-library side.
    Connections {
        target: contentPreferences
        function onChanged() { TheatreApi.setShowExplicit(contentPreferences.showExplicit) }
    }

    // ---- Universes: the ONE extension-driven page + the Hall of Worlds ----
    Loader {
        id: universeLayer
        anchors.fill: parent
        z: 52
        active: false
        visible: active
        asynchronous: true
        property string extensionId: ""
        property string universeName: ""
        source: "UniverseExtensionPage.qml"
        onLoaded: {
            // NO item.backdrop — UniverseExtensionPage has no such property; it paints its
            // own flat #0c0e11 instead of sampling the shared wallpaper.
            item.extensionId = universeLayer.extensionId
            item.universeName = universeLayer.universeName
            item.backRequested.connect(win.closeUniverse)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            // Works open ABOVE this overlay — series/theatre/western are z:53 (> this layer's 52),
            // book is z:53 — so a clicked work paints on top and the overlay stays loaded beneath.
            // Their Esc checks sit before closeUniverse, so back closes the work first, then the
            // universe. (Replaces an earlier close-on-click that broke back-nav to the universe.)
            item.watchRequested.connect(win.openTheatreSeries)
            item.bookRequested.connect(win.openBook)
            item.comicsArchiveRequested.connect(win.openUniverseComic)
            // manga → Tankoban. A weebcentral-sourced entry (One Piece digital-coloured) opens its
            // own series by ID; an anilist entry opens by title, as before.
            item.seriesRequested.connect(function(e) {
                if (e && e.provider === "weebcentral" && e.id) win.openSeriesAt(e.title || "", e.id)
                else win.openSeries((e && e.title) || e || "")
            })
        }
    }
    Loader {
        id: universeHallLayer
        anchors.fill: parent
        z: 51
        active: false
        visible: active
        asynchronous: true
        source: "UniverseHallPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.universes = win.installedUniverses
            item.backRequested.connect(win.closeUniverseHall)
            item.exploreRequested.connect(function (extensionId, name) {
                win.closeUniverseHall(); win.openUniverse(extensionId, name)
            })
        }
    }

    // ---- Extensions page: the store (Stremio-protocol addons), from the taskbar ----
    Loader {
        id: extensionsLayer
        anchors.fill: parent
        z: 56     // taskbar full-page, same rule as downloadsLayer (see its comment)
        active: false
        visible: active
        source: "ExtensionsPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeExtensionsPage)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
        }
    }

    // ---- Settings page: global preferences, entered from the taskbar gear (Task 2) ----
    Loader {
        id: settingsLayer
        anchors.fill: parent
        z: 56     // taskbar full-page, same rule as downloadsLayer (see its comment)
        active: false
        visible: active
        source: "SettingsPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.preferences = contentPreferences
            item.backRequested.connect(win.closeSettingsPage)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- download feedback (2026-07-18, Hemanth): clicking Download anywhere pops the
    //      taskbar out so the gold jobs badge is SEEN arriving — same auto-reveal (and same
    //      15s pull-back) as a minimize. Fires only when the live-job count GROWS (a finish
    //      or cancel never pops the bar). While a reader/player owns the screen the taskbar
    //      is suppressed, so the reveal is held and fires when the shell is back — unless
    //      every job already finished by then (no stale pop for nothing). ----
    property int lastActiveDownloads: 0
    property bool pendingDownloadReveal: false
    // ONE number for the badge AND the pop: every live download, whichever engine owns
    // it. Audiobooks ride their own engine (not LocalDownloads) — folding its activeCount
    // in here is what makes an audiobook download "behave the same way as any other
    // download" (Hemanth 2026-07-20: taskbar pops, gold chip counts it).
    readonly property int totalActiveDownloads:
        ((typeof LocalDownloads !== "undefined")
            ? (LocalDownloads.revision, Number(LocalDownloads.totals.active || 0)) : 0)
        + ((typeof Audiobooks !== "undefined") ? Audiobooks.activeCount : 0)
    onImmersiveSurfaceOpenChanged: {
        if (!immersiveSurfaceOpen && pendingDownloadReveal) {
            pendingDownloadReveal = false
            if (lastActiveDownloads > 0) taskbar.reveal()
        }
    }
    onTotalActiveDownloadsChanged: {
        var grew = totalActiveDownloads > lastActiveDownloads
        lastActiveDownloads = totalActiveDownloads
        if (!grew) return
        if (immersiveSurfaceOpen) { pendingDownloadReveal = true; return }
        taskbar.reveal()
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
        downloadsBadge: win.totalActiveDownloads
        downloadsActive: downloadsLayer.active
        onDownloadsClicked: downloadsLayer.active ? win.closeDownloadsPage() : win.openDownloadsPage()
        extensionsActive: extensionsLayer.active
        onExtensionsClicked: extensionsLayer.active ? win.closeExtensionsPage() : win.openExtensionsPage()
        settingsActive: settingsLayer.active
        onSettingsClicked: settingsLayer.active ? win.closeSettingsPage() : win.openSettingsPage()
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
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.quitRequested.connect(function() { Qt.quit() })
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
        // Automation identity (Lanista): scenarios wait for `visible == false` here before
        // driving anything — the first pilot run proved every click lands "green" on the
        // occluded tree while this splash still owns the screen (vacuous pass, 2026-08-06).
        objectName: "bootSplash"
        anchors.fill: parent
        z: 1000
        onFinished: bootFade.start()
        NumberAnimation { id: bootFade; target: boot; property: "opacity"; to: 0; duration: 400
            onFinished: boot.visible = false }
    }

    // One shell-wide cover turns the full-monitor geometry jump into a deliberate beat.
    // Every entry point (F11, global chrome, player, book, comic) reaches this same gate.
    FullscreenTransitionShield {
        id: fullscreenTransition
        anchors.fill: parent
        onApplyRequested: WindowMode.toggleShellMode(win)
    }
}
