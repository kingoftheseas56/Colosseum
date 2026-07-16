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
import "LocgApi.js" as Locg
import "ComicsApi.js" as GcApi
import "ComicsDb.js" as ComicsDb
import "ComicResolve.js" as Resolve
import "AddonClient.js" as AddonClient
import "Subtitles.js" as Subtitles
import "Torrentio.js" as Torrentio
import "EpisodeBrowser.js" as EpisodeBrowser

Window {
    id: win
    // Hidden until the native WindowModeStore chooses the startup presentation
    // (fullscreen by default; developer-windowed if that was the last stable mode).
    visible: false
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
        if (appType === "biblio" || contentKind === "book" || contentKind === "audiobook") return "Biblio"
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
        else if (bookLayer.active) win.closeBook()
        else if (biblioGenreLayer.active) win.closeBiblioGenre()
        else if (biblioGenreIndexLayer.active) win.closeBiblioGenreIndex()
        else if (searchLayer.active) win.closeSearch()
        else if (worldSearchLayer.active) win.closeWorldSearch()
        else if (downloadsLayer.active) win.closeDownloadsPage()
        else if (extensionsLayer.active) win.closeExtensionsPage()
        else if (theatreSeriesLayer.active) win.closeTheatreSeries()
        else if (seriesLayer.active) win.closeSeries()
        else if (comicSeriesLayer.active) win.closeComicSeries()
        else if (comicCatalogLayer.active) win.closeComicCatalog()
        else if (westernLayer.active) win.closeWestern()
        else if (locgPublisherLayer.active) win.closeLocgPublisher()
        else if (comicBoardLayer.active) win.closeComicArchiveBoard()
        else if (comicIndexLayer.active) win.closeComicArchive()
        else if (continueSeeAllLayer.active) win.closeContinueSeeAll()
        else if (theatreGenreLayer.active) win.closeTheatreGenre()
        else if (theatreGenreIndexLayer.active) win.closeTheatreGenreIndex()
        else if (genreLayer.active) win.closeGenre()
        else if (genreIndexLayer.active) win.closeGenreIndex()
        else if (universeLayer.active) win.closeUniverse()
        else if (universeHallLayer.active) win.closeUniverseHall()
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
        onActivated: if (typeof WindowMode !== "undefined")
            WindowMode.toggleShellMode(win)
    }

    // Minimize the OS surface to the taskbar — "get it off my screen" WITHOUT quitting (the shell
    // keeps running, art stays warm). Windows restores it to whatever base mode it held before
    // minimizing (fullscreen or the developer window), so no forced snap-back is needed.
    function minimizeShell() { win.showMinimized() }

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
        return category === "cinematic" ? "CinematicPage.qml"
             : category === "onepiece"  ? "OnePieceUniversePage.qml"   // One Piece — the Grand Line voyage
             : category === "dragonball" ? "DragonBallUniversePage.qml" // Dragon Ball — the seven-star saga
             : category === "cosmere"   ? "CosmereUniversePage.qml"  // Cosmere — newcomer portals + planetary atlas
             : category === "saga"      ? "SagaUniversePage.qml"      // book-first IPs (HP/LOTR/ASOIAF/Dune/Witcher/Sherlock/…)
             : category === "magazine"  ? "MagazineUniversePage.qml"  // Weekly Shonen Jump — manga only
             : category === "galaxy"    ? "GalaxyUniversePage.qml"    // Star Wars — trilogy triptych
             : category === "eras"      ? "EraUniversePage.qml"       // Bond/Trek/DCAU/Avatar — epoch columns
             : category === "studio"    ? "StudioUniversePage.qml"    // Ghibli — the filmography wall
             : "UniversePage.qml"
    }
    function openUniverse(name) {
        universeLayer.universeName = name
        universeLayer.universeSource = win.universeSourceFor(Universes.categoryFor(name))
        if (universeLayer.item) universeLayer.item.universeName = name
        universeLayer.active = true
        topbar.visible = false
        page.visible = false
    }
    // the Hall of Worlds — the universe collection's see-all (z below universeLayer, so
    // entering a world paints OVER the hall and back returns to it)
    function openUniverseHall() {
        universeHallLayer.active = true
        topbar.visible = false
        page.visible = false
    }
    function closeUniverseHall() {
        universeHallLayer.active = false
        // only restore the home chrome if no world took over above us
        if (!universeLayer.active) { topbar.visible = true; page.visible = true }
    }
    function closeUniverse() {
        universeLayer.active = false
        // if the Hall of Worlds sits beneath, back lands there — not on home
        if (!universeHallLayer.active) { topbar.visible = true; page.visible = true }
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
        // DB-first (Hemanth 2026-07-15): the catalog series view is THE series
        // view now. Resolve the title against our DB (ingesting it on demand);
        // the GetComics shelf remains only for series the catalog doesn't carry.
        var hit = win.comicsDbHit((d && d.title) || "")
        if (hit) {
            win.openComicSeries({ id: hit.locgId, title: hit.title,
                                  cover: hit.cover || (d && d.cover) || "" })
            return
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

    // ---- complete ranked comics catalog: opened from Top Comics Explore and kept alive
    //      underneath ComicSeriesPage so Back returns to the same wall state. ----
    function openComicCatalog(rows, genre) {
        comicCatalogLayer.rows = rows || []
        comicCatalogLayer.genre = genre || ""
        if (comicCatalogLayer.active && comicCatalogLayer.item) {
            comicCatalogLayer.item.rows = comicCatalogLayer.rows
            comicCatalogLayer.item.genre = comicCatalogLayer.genre
        } else comicCatalogLayer.active = true
    }
    function closeComicCatalog() { comicCatalogLayer.active = false }

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

    // ---- video player: a fullscreen layer over everything; kept alive once opened so mpv
    //      isn't torn down/recreated each play (avoids the use-after-free teardown trap). ----
    property bool playerOpen: false
    // The movie session the player minimized while still loaded. Reopening it from the
    // taskbar finds the stream warm — we resume in place instead of re-streaming.
    property string warmPlayerSessionId: ""
    // Every reader/player surface that must suppress the OS-shell taskbar. There are THREE
    // comic/manga reader lanes (all share the reader chrome — see minimizeComicReader):
    // seriesLayer=manga, westernLayer=western comics, comicSeriesLayer=the LOCG catalogue.
    // comicSeriesLayer was missing here, so the taskbar rode in front of that reader while
    // the other two + book + player suppressed it correctly (Hemanth, 2026-07-16).
    readonly property bool immersiveSurfaceOpen: win.playerOpen
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
    // silently swapped). Unpinned (season checkout, old queued jobs) -> rank-best below.
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
        if (pin) {
            var pkey = pin.infoHash.toLowerCase() + ":" + pin.fileIdx
            win.pendingFeeds[pkey] = id
            Stream.prefetch(pin.infoHash, pin.fileIdx)
            return
        }
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
            // comics open only via the gc: lane; a stale foreign-prefixed id (retired source,
            // cut 2026-07-12) is an honest no-op, not an empty western shelf (mirrors the browse guard)
            if (String(item.seriesId || "").indexOf("gc:") === 0)
                win.openWesternAt(item.seriesTitle, String(item.seriesId).slice(3), item.id)
            else
                console.log("[route] ignoring unknown comic id:", item.seriesId)
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
        if (data && data.notice) return   // a cooldown/status notice row isn't clickable content
        win.closeWorldSearch()
        if (worldSearchLayer.searchMode === "Tankoban") {
            if (data && data.locg) win.openComicSeries(data)        // LOCG catalogue series → resolve+attach
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
            else if (entry.kind === "comic")
                // retired-source or unknown comic id — honest no-op (preset-pages source cut 2026-07-12);
                // comics open only via the gc: lane, so a stale id never opens the manga page
                console.log("[route] ignoring unknown comic id:", entry.id)
            else win.openSeries(title)                                   // manga → the chapter-list series page
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
    // A2 audiobook session: the paired audiobook rides the biblio appType like the reader,
    // so it gets a taskbar tile + Continue presence. id = pairKey (its pairing identity).
    function openAudiobookSession(pairKey, book) {
        if (!pairKey) return
        var b = book || ({})
        Sessions.openOrSwitch({
            "appType": "biblio", "contentKind": "audiobook", "title": b.title || "Audiobook",
            "target": { "pairKey": pairKey, "book": b, "id": pairKey }
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
            // reading began from a browse (no session yet) — register it from the live reader.
            // Check every comic lane (LOCG-catalogue / western / manga) — the reader chrome is shared.
            var x = comicSeriesLayer.active ? comicSeriesLayer.item : null
            var w = westernLayer.active ? westernLayer.item : null
            if (x && x.openChapterId) {
                win.openComicSession(x.seriesTitle, "gc:" + x.gcTag, x.openChapterId)   // LOCG page reads GetComics content
            } else if (w && w.openChapterId) {
                win.openComicSession(w.seriesTitle, w.seriesId, w.openChapterId)   // seriesId = "gc:<slug>"
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
    // audiobook always registers a session before its player shows, so minimize = switch away
    // (teardown hides the layer, tile stays), close = end the session.
    function minimizeAudiobook() { Sessions.switchTo("") }
    function closeAudiobookSession() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "audiobook") win.closeSession(rec.id)
        else if (audiobookPlayerLayer.active) audiobookPlayerLayer.active = false
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
                // GetComics content (western shelf OR LOCG-catalogue page) restores via the
                // GetComics shelf — same tag, same reader, resumed at the chapter.
                win.openWesternAt(t.title, String(t.seriesId).slice(3), (st.chapterId || t.chapterId || ""))
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
            if (bookReaderLayer.active && bookReaderLayer.item) bookReaderLayer.item.open(t.path, t.book || ({}))
            else bookReaderLayer.active = true
            // book precision: foliate auto-restores its own CFI on reopen of the same path (Task 6).
        } else if (rec.contentKind === "audiobook") {
            if (!audiobookPlayerLayer.active) audiobookPlayerLayer.active = true
            var abp = audiobookPlayerLayer.item
            if (abp) {
                abp.start(t.pairKey, t.book || ({}))
                // resume: in-session capture (minimize) wins; else the ProgressStore position.
                var abSt = (st && st.position !== undefined) ? st : null
                if (!abSt && typeof Progress !== 'undefined') {
                    var pg = Progress.get("audiobook", t.pairKey || "")
                    if (pg && pg.resume) abSt = { "fileIndex": Number(pg.resume.fileIndex) || 0,
                                                  "position": Number(pg.resume.position) || 0 }
                }
                if (abSt && abp.restoreState) abp.restoreState(abSt)
            }
        }
    }
    // capture the live outgoing surface's state (called BEFORE teardown).
    function captureSession(rec) {
        if (!rec || !rec.id) return ({})
        if (rec.contentKind === "movie" && playerLayer.item && playerLayer.item.captureState) return playerLayer.item.captureState()
        if (rec.contentKind === "comic") {
            // one comic surface hosts the reader at a time — capture from whichever is live
            var lay = comicSeriesLayer.active ? comicSeriesLayer
                    : (westernLayer.active ? westernLayer : seriesLayer)
            return (lay.item && lay.item.captureState) ? lay.item.captureState() : ({})
        }
        if (rec.contentKind === "book"  && bookReaderLayer.item && bookReaderLayer.item.captureState) return bookReaderLayer.item.captureState()
        if (rec.contentKind === "audiobook" && audiobookPlayerLayer.item && audiobookPlayerLayer.item.captureState) return audiobookPlayerLayer.item.captureState()
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
            // one comic surface hosts the reader at a time — drop whichever is live
            if (comicSeriesLayer.active) comicSeriesLayer.active = false
            else if (westernLayer.active) westernLayer.active = false
            else seriesLayer.active = false
        } else if (rec.contentKind === "book")  {
            bookReaderLayer.active = false
        } else if (rec.contentKind === "audiobook") {
            audiobookPlayerLayer.active = false
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
        backdrop: wall
        activeMedium: ""
        x: theme.margin; y: 30
        width: win.width - theme.margin * 2
        onMediumSelected: (medium) => win.openWorld(medium)
        onWallpaperClicked: win.openWallpaperSearch("Home")
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
                        model: Universes.universes
                        delegate: Item {
                            id: slide
                            required property var modelData

                            // banner key-art full-bleed; the IP color stands in while it loads,
                            // then the left-weighted scrim keeps the words legible (proven look)
                            Rectangle {
                                anchors.fill: parent; radius: hero.radius; clip: true
                                color: slide.modelData.c1 ? slide.modelData.c1 : "#1a1410"
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
                                Text {
                                    text: slide.modelData.blurb
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; width: 500; wrapMode: Text.WordWrap
                                }
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
                                            onClicked: win.openUniverse(Universes.universes[heroView.currentIndex].name)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // gentle auto-advance through the collection (not visualized — ratified)
                Timer {
                    interval: 6500; running: true; repeat: true
                    onTriggered: heroView.currentIndex = (heroView.currentIndex + 1) % Universes.universes.length
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
                        Text { text: Universes.universes.length + " worlds"
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
                    var a = Progress.recent("", 12)
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
                    var comicSeriesSignal = item["comicSeriesRequested"]
                    if (comicSeriesSignal) comicSeriesSignal.connect(win.openComicSeries)
                    var comicCatalogSignal = item["comicCatalogRequested"]
                    if (comicCatalogSignal) comicCatalogSignal.connect(win.openComicCatalog)
                    var comicGenreSignal = item["comicGenreRequested"]
                    if (comicGenreSignal) comicGenreSignal.connect(function(payload) {
                        win.openComicCatalog((payload || {}).rows, (payload || {}).genre)
                    })
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
                    if (item.continueSeeAllRequested) item.continueSeeAllRequested.connect(function() {
                        win.openContinueSeeAll(mode === "Theatre" ? "video"
                                             : mode === "Biblio"  ? "book" : "tankoban")
                    })
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
    // ---- the Hall of Worlds: the universe collection's see-all. z BELOW universeLayer so
    //      a spine's world opens over the hall; closing it falls back here. ----
    Loader {
        id: universeHallLayer
        anchors.fill: parent
        z: 38
        active: false
        visible: active
        source: "UniverseHallPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeUniverseHall)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.exploreRequested.connect(win.openUniverse)
        }
    }

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
            if (item.bookRequested) item.bookRequested.connect(win.openBook)          // saga template: novels → Biblio
            if (item.comicsArchiveRequested) item.comicsArchiveRequested.connect(win.openComicArchive)  // eras: COMICS column → GC archive index
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
            item.closeRequested.connect(function() { Qt.quit() })
            item.readerMinimizeRequested.connect(win.minimizeComicReader)
            item.readerCloseRequested.connect(win.closeComicReader)
            item.locgMeta = comicSeriesLayer.locgMeta
            item.locgId = comicSeriesLayer.locgSid       // set LAST — triggers attach()
        }
    }

    // ---- complete Top Comics catalog wall. z 49 keeps it above TankobanWorld but below
    //      ComicSeriesPage (z 50), preserving filters and scroll while a series is open. ----
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

    Loader {
        id: comicCatalogLayer
        anchors.fill: parent
        z: 49
        active: false
        visible: active
        property var rows: []
        property string genre: ""
        source: "ComicCatalogPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.rows = comicCatalogLayer.rows
            item.genre = comicCatalogLayer.genre
            item.backRequested.connect(win.closeComicCatalog)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.seriesRequested.connect(win.openComicSeries)
        }
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
            item.listenRequested.connect(win.openAudiobookSession)
        }
    }

    // ---- the reader: foliate EPUB reader (WebEngine), over the book detail ----
    // The ONE audiobook engine for read-along, hoisted at the window root (never in a
    // Loader) so the stream survives the reader opening/closing. The reader's docked
    // listen strip binds to this; the standalone AudiobookPlayer keeps its own engine.
    AudiobookSession { id: audioSession }

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

    // ---- audiobook player layer: A2's audio-session surface (over the world, below the reader) ----
    Loader {
        id: audiobookPlayerLayer
        anchors.fill: parent
        z: 57
        active: false
        visible: active
        source: "AudiobookPlayer.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.minimizeAudiobook)
            item.minimizeRequested.connect(win.minimizeAudiobook)
            item.closeRequested.connect(win.closeAudiobookSession)
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
