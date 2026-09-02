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
import QtQuick.Dialogs
import "account"
import "Catalog.js" as Catalog
import "TheatreApi.js" as TheatreApi
import "UniverseExtApi.js" as UniverseApi
import "ExtensionsCatalog.js" as ExtCatalog
import "LocgApi.js" as Locg
import "ComicsApi.js" as GcApi
import "ComicsDb.js" as ComicsDb
import "ComicResolve.js" as Resolve
import "ComicDownloadRoute.js" as ComicDownloadRoute
import "AddonClient.js" as AddonClient
import "Subtitles.js" as Subtitles
import "Torrentio.js" as Torrentio
import "EpisodeBrowser.js" as EpisodeBrowser
import "BiblioApi.js" as BiblioApi
import "CollectionBackfill.js" as CollectionBackfill
import "WarmingQueue.js" as Warming
import "ShellBackPolicy.js" as ShellBackPolicy

Window {
    id: win
    // Hidden until the native WindowModeStore chooses the startup presentation
    // (fullscreen by default; developer-windowed if that was the last stable mode).
    visible: false
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#05060a"
    title: "Colosseum"

    property string currentSurface: "Home"
    readonly property bool worldWarmerEnabled: (typeof DevWorldWarmer !== "undefined") && DevWorldWarmer
    property var pendingIdentityRoute: null
    property bool reducedMotion: false     // single shell motion preference seam for Update surfaces
    property string wallpaperSource: "../assets/wallpaper/cold-ripple.jpg"
    function setGuiStallContext(operation, surface) {
        if (typeof GuiStallProbe !== "undefined" && GuiStallProbe)
            GuiStallProbe.setContext(operation, surface)
    }

    // Collection backfill and Biblio cover enrichment are intentionally post-frame work. Both
    // functions are idempotent, but their initial local-model scans can be large enough to hold
    // the GUI inside Component.onCompleted. Start only after native startup has observed the first
    // frame, then give the shell a short settling window so the first interaction is responsive.
    Timer {
        id: postFrameStartupWork
        interval: 350
        repeat: false
        onTriggered: {
            win.runCollectionBackfill()
            win.enrichBiblioCovers()
        }
    }
    property bool startupIdleWorkArmed: false
    Timer {
        id: startupIdleGate
        interval: 2500
        repeat: false
        onTriggered: {
            if (worldStack.current !== "" || win.immersiveSurfaceOpen) {
                startupIdleWorkArmed = false
                win.armStartupIdleWork()
                return
            }
            postFrameStartupWork.start()
            win.armHomeIntroWidgets()
            win.armHomeContinueRail()
        }
    }
    function armStartupIdleWork() {
        if (startupIdleWorkArmed || boot.visible) return
        startupIdleWorkArmed = true
        startupIdleGate.start()
    }
    // The Home intro board is below the first viewport and each widget owns its own
    // catalogue requests/poster delegates. Keep their object trees out of Main.qml's
    // pre-first-frame critical path, then instantiate them asynchronously once the
    // shell has had a chance to present and accept input.
    property bool homeIntroWidgetsLoaded: false
    property int homeIntroWidgetCursor: 0
    property var homeIntroPendingLoader: null
    Timer {
        id: homeIntroWidgetsTimer
        interval: 850
        repeat: false
        onTriggered: win.loadHomeIntroWidgets()
    }
    function armHomeIntroWidgets() {
        if (homeIntroWidgetsLoaded || homeIntroPendingLoader) return
        if (boot.visible || worldStack.current !== "" || win.immersiveSurfaceOpen) return
        homeIntroWidgetsTimer.start()
    }

    // Continue is the only Home rail that queries the Progress store while its delegates are
    // created. Keep a fixed reservation so loading it later cannot reflow the Home board, but move
    // the query and ContinueTile object tree behind the post-splash idle boundary.
    property bool homeContinueRailRequested: false
    property bool homeContinueRailLoaded: false
    readonly property int homeContinueRailPlaceholderHeight: 192
    Timer {
        id: homeContinueRailTimer
        interval: 500
        repeat: false
        onTriggered: win.loadHomeContinueRail()
    }
    function armHomeContinueRail() {
        if (homeContinueRailLoaded || homeContinueRailRequested) return
        if (boot.visible || worldStack.current !== "" || win.immersiveSurfaceOpen) return
        homeContinueRailRequested = true
        homeContinueRailTimer.start()
    }
    function loadHomeContinueRail() {
        if (homeContinueRailLoaded) return
        if (worldStack.current !== "" || win.immersiveSurfaceOpen) {
            homeContinueRailRequested = false
            return
        }
        homeContinueRailLoader.sourceComponent = homeContinueRailComponent
        homeContinueRailLoader.active = true
    }
    function loadHomeIntroWidgets() {
        if (homeIntroWidgetsLoaded || homeIntroPendingLoader) return
        if (worldStack.current !== "" || win.immersiveSurfaceOpen) return
        var loaders = [homeBookshelfLoader, homeTheatreStripLoader,
                       homeReadingDeskLoader, homeVaultWidgetLoader]
        var sources = ["Bookshelf.qml", "TheatreStrip.qml",
                       "ReadingDesk.qml", "VaultHomeWidget.qml"]
        if (homeIntroWidgetCursor >= loaders.length) {
            homeIntroWidgetsLoaded = true
            return
        }
        var loader = loaders[homeIntroWidgetCursor]
        homeIntroWidgetCursor++
        homeIntroPendingLoader = loader
        loader.setSource(sources[homeIntroWidgetCursor - 1], { "backdrop": wall })
        loader.active = true
    }
    function homeIntroWidgetSettled(loader) {
        if (homeIntroPendingLoader !== loader) return
        homeIntroPendingLoader = null
        if (homeIntroWidgetCursor >= 4) {
            homeIntroWidgetsLoaded = true
            return
        }
        homeIntroWidgetsTimer.restart()
    }
    Connections {
        target: (typeof GuiStallProbe !== "undefined") ? GuiStallProbe : null
        function onFirstFrameReady() {
            win.armStartupIdleWork()
        }
    }
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

    // The C++ extension store gates adult manifests on direct preview/install. It reads the
    // same one preference, so the Settings switch governs both ingress paths (community
    // Browse is filtered in ExtensionsCatalog.js) rather than only the one.
    Binding {
        target: typeof Extensions !== "undefined" ? Extensions : null
        property: "showExplicit"
        value: contentPreferences.showExplicit
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
        // dev harness (COLOSSEUM_OPEN_UNIVERSE="starwars"): boot straight into a universe
        if (typeof DevOpenUniverse !== "undefined" && String(DevOpenUniverse).length) {
            var devUniverse = String(DevOpenUniverse).toLowerCase()
            if (devUniverse === "starwars" || devUniverse === "star-wars")
                win.openUniverse("com.colosseum.universe.starwars", "Star Wars")
        }
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
        // The production native entry point starts this timer from its first-frame signal. A
        // bare QML harness has no bridge, so retain a deterministic fallback for those fixtures.
        if (typeof GuiStallProbe === "undefined" || !GuiStallProbe) {
            win.armStartupIdleWork()
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

    // Escape has one shell entry point. ShellBackPolicy owns precedence; each selected
    // surface still owns the meaning of Back/Escape so session, progress and overlay
    // semantics cannot be bypassed by a parallel root-level close path.
    function shellEscapeState() {
        var rec = (typeof Sessions !== "undefined" && Sessions && Sessions.activeId)
                ? Sessions.get(Sessions.activeId) : ({})
        return {
            transitioning: fullscreenTransition.transitioning,
            bootVisible: boot.visible,
            accountHostVisible: accountHost.visible,
            identityCeremonyOpen: identityCeremonyDialog.opened,
            watchPartyJoinOpen: watchPartyJoinSheet.opened,
            wallpaperActive: wallpaperLayer.active,
            accountFlyoutVisible: accountFlyout.visible,
            accountCenterVisible: accountCenter.visible,
            openRecentOpen: openRecentPanel.open,
            taskbarOpen: taskbar.open,
            activeSessionKind: rec && rec.contentKind ? String(rec.contentKind) : "",
            playerOpen: win.playerOpen,
            bookReaderActive: bookReaderLayer.active,
            vaultComicActive: vaultComicLayer.active,
            comicReaderActive: win.embeddedComicReaderOpen(),
            updateActive: updateLayer.active,
            keyboardGuideActive: keyboardGuideLayer.active,
            settingsActive: settingsLayer.active,
            extensionsActive: extensionsLayer.active,
            vaultActive: vaultLayer.active,
            downloadsActive: downloadsLayer.active,
            bookActive: bookLayer.active,
            theatreSeriesActive: theatreSeriesLayer.active,
            westernActive: westernLayer.active,
            seriesActive: seriesLayer.active,
            universeActive: universeLayer.active,
            universeHallActive: universeHallLayer.active,
            searchActive: searchLayer.active,
            worldSearchActive: worldSearchLayer.active,
            comicSeriesActive: comicSeriesLayer.active,
            locgPublisherActive: locgPublisherLayer.active,
            comicIndexActive: comicIndexLayer.active,
            comicBoardActive: comicBoardLayer.active,
            continueSeeAllActive: continueSeeAllLayer.active,
            theatreGenreActive: theatreGenreLayer.active,
            theatreGenreIndexActive: theatreGenreIndexLayer.active,
            biblioGenreActive: biblioGenreLayer.active,
            biblioGenreIndexActive: biblioGenreIndexLayer.active,
            genreActive: genreLayer.active,
            genreIndexActive: genreIndexLayer.active,
            worldOpen: worldStack.current !== ""
        }
    }
    function embeddedComicReaderOpen() {
        return !!((seriesLayer.active && seriesLayer.item && String(seriesLayer.item.openChapterId || "").length)
               || (westernLayer.active && westernLayer.item && String(westernLayer.item.openChapterId || "").length)
               || (comicSeriesLayer.active && comicSeriesLayer.item && String(comicSeriesLayer.item.openChapterId || "").length))
    }
    function requestPlayerEscape() {
        var rec = Sessions.get(Sessions.activeId)
        if (playerLayer.active && playerLayer.item && playerLayer.item.requestEscape) {
            playerLayer.item.requestEscape()
            return
        }
        if (rec && rec.contentKind === "movie") { win.minimizePlayer(); return }
        if (win.playerOpen) win.closePlayer()
    }
    function requestBookReaderEscape() {
        var rec = Sessions.get(Sessions.activeId)
        if (bookReaderLayer.active && bookReaderLayer.item && bookReaderLayer.item.requestEscape) {
            bookReaderLayer.item.requestEscape()
            return
        }
        if (rec && rec.contentKind === "book") { win.closeBookReaderSession(); return }
        if (bookReaderLayer.active) win.closeBookReader()
    }
    function requestComicReaderEscape() {
        if (vaultComicLayer.active && vaultComicLayer.item && vaultComicLayer.item.requestEscape) {
            vaultComicLayer.item.requestEscape(); return
        }
        if (comicSeriesLayer.active && comicSeriesLayer.item
                && String(comicSeriesLayer.item.openChapterId || "").length
                && comicSeriesLayer.item.requestReaderEscape) {
            comicSeriesLayer.item.requestReaderEscape(); return
        }
        if (westernLayer.active && westernLayer.item
                && String(westernLayer.item.openChapterId || "").length
                && westernLayer.item.requestReaderEscape) {
            westernLayer.item.requestReaderEscape(); return
        }
        if (seriesLayer.active && seriesLayer.item
                && String(seriesLayer.item.openChapterId || "").length
                && seriesLayer.item.requestReaderEscape) {
            seriesLayer.item.requestReaderEscape(); return
        }
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "comic") win.closeSession(rec.id)
    }
    function cancelPendingIdentityCeremony() {
        win.pendingIdentityRoute = null
        identityCeremonyDialog.close()
    }
    function handleEscape() {
        var action = ShellBackPolicy.actionFor(win.shellEscapeState())
        switch (action) {
        case "consume": return
        case "cancelIdentityCeremony": identityCeremonyDialog.cancelRequested(); return
        case "watchPartyJoin": watchPartyJoinSheet.close(); return
        case "wallpaper": win.closeWallpaperSearch(); return
        case "accountFlyout": accountFlyout.close(); return
        case "accountCenter": accountCenter.close(); return
        case "openRecent": openRecentPanel.open = false; return
        case "taskbar": taskbar.open = false; return
        case "player": win.requestPlayerEscape(); return
        case "bookReader": win.requestBookReaderEscape(); return
        case "comicReader": win.requestComicReaderEscape(); return
        case "update": win.closeUpdatePage(); return
        case "keyboardGuide": win.closeKeyboardGuide(); return
        case "settings": win.closeSettingsPage(); return
        case "extensions": win.closeExtensionsPage(); return
        case "vault": if (vaultLayer.item && vaultLayer.item.handleBack) vaultLayer.item.handleBack(); else win.closeVaultPage(); return
        case "downloads": win.closeDownloadsPage(); return
        case "book": win.closeBook(); return
        case "theatreSeries": win.closeTheatreSeries(); return
        case "western": win.closeWestern(); return
        case "series": win.closeSeries(); return
        case "universe": win.closeUniverse(); return
        case "universeHall": win.closeUniverseHall(); return
        case "search": win.closeSearch(); return
        case "worldSearch": win.closeWorldSearch(); return
        case "comicSeries": win.closeComicSeries(); return
        case "locgPublisher": win.closeLocgPublisher(); return
        case "comicIndex": win.closeComicArchive(); return
        case "comicBoard": win.closeComicArchiveBoard(); return
        case "continueSeeAll": win.closeContinueSeeAll(); return
        case "theatreGenre": win.closeTheatreGenre(); return
        case "theatreGenreIndex": win.closeTheatreGenreIndex(); return
        case "biblioGenre": win.closeBiblioGenre(); return
        case "biblioGenreIndex": win.closeBiblioGenreIndex(); return
        case "genre": win.closeGenre(); return
        case "genreIndex": win.closeGenreIndex(); return
        case "world": win.closeWorld(); return
        default: Qt.quit(); return
        }
    }
    Shortcut { sequences: ["Escape"]; onActivated: win.handleEscape() }
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
        setGuiStallContext("navigate", String(medium))
        refreshWallpaper()
        topbar.visible = false
        page.visible = false
    }
    // Vault identity door: reuse the already-owned world entry points. The Vault never edits a
    // world page or invents a second reader route; it only hands over a certain identity.
    function openVaultIdentity(identity) {
        if (!identity || !identity.world || !identity.identityId) return
        win.closeVaultPage()
        var source = String(identity.source || "")
        var sid = String(identity.identityId || "")
        if (identity.world === "Tankoban") {
            win.openWorld("Tankoban")
            if (source === "COMICS")
                win.openGcdSeries({ gcdId: Number(sid.replace(/^comics:/, "")), title: identity.title || "" })
            else
                win.openSeries(identity.title || "")
        } else if (identity.world === "Theatre") {
            win.openWorld("Theatre")
            win.openTheatreSeries({ id: sid.replace(/^imdb:/, ""), type: "movie",
                                    title: identity.title || "", cover: identity.coverUrl || "" })
        } else if (identity.world === "Biblio") {
            win.openWorld("Biblio")
            win.openBook({ id: sid, title: identity.title || "", synopsis: identity.synopsis || "" })
        }
    }
    function closeWorld() {
        worldStack.current = ""                           // hide all worlds; none destroyed
        currentSurface = "Home"
        setGuiStallContext("navigate", "Home")
        refreshWallpaper()
        topbar.visible = true
        page.visible = true
        win.armHomeIntroWidgets()
        win.armHomeContinueRail()
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
    function clearSeriesEditionProfile() {
        seriesLayer.malId = ""
        seriesLayer.seriesIdOverride = ""
        seriesLayer.sourceSearchTitle = ""
        seriesLayer.sourceSearchAliases = []
        seriesLayer.sourceRequiredMarkers = []
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.malId = ""
            seriesLayer.item.seriesIdOverride = ""
            seriesLayer.item.sourceSearchTitle = ""
            seriesLayer.item.sourceSearchAliases = []
            seriesLayer.item.sourceRequiredMarkers = []
        }
    }
    function applySeriesEditionProfile(profile, fallbackMalId) {
        clearSeriesEditionProfile()
        var p = profile || ({})
        seriesLayer.malId = String(p.malId || fallbackMalId || "")
        seriesLayer.seriesIdOverride = String(p.seriesId || "")
        seriesLayer.sourceSearchTitle = String(p.sourceSearchTitle || "")
        seriesLayer.sourceSearchAliases = p.sourceSearchAliases || []
        seriesLayer.sourceRequiredMarkers = p.sourceRequiredMarkers || []
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.malId = seriesLayer.malId
            seriesLayer.item.seriesIdOverride = seriesLayer.seriesIdOverride
            seriesLayer.item.sourceSearchTitle = seriesLayer.sourceSearchTitle
            seriesLayer.item.sourceSearchAliases = seriesLayer.sourceSearchAliases
            seriesLayer.item.sourceRequiredMarkers = seriesLayer.sourceRequiredMarkers
        }
    }
    function restoreSeriesEditionProfile(seriesId) {
        var sid = String(seriesId || "")
        if (sid !== "mal:13:color") { clearSeriesEditionProfile(); return }
        applySeriesEditionProfile({
            malId: "13", seriesId: sid, sourceSearchTitle: "One Piece Colored",
            sourceSearchAliases: ["One Piece Digital Colored Comics"],
            sourceRequiredMarkers: ["colored", "full color", "full colour"]
        }, "13")
    }
    function openSeries(title, malId, profile, requestedVolumeNumber) {
        if (seriesLayer.legacyWeebCentral) { seriesLayer.active = false; seriesLayer.legacyWeebCentral = false }
        seriesLayer.resumeSeriesId = ""
        seriesLayer.resumeChapterId = ""
        seriesLayer.resumeVolumeId = ""
        seriesLayer.requestedVolumeNumber = String(requestedVolumeNumber || "")
        applySeriesEditionProfile(profile, malId)
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.openEntryKind = "manga"   // a reused item may still be in a volume read
            seriesLayer.item.openChapterId = ""        // leave the reader, show the chapter list
            seriesLayer.item.requestedVolumeNumber = seriesLayer.requestedVolumeNumber
            seriesLayer.item.seriesTitle = title
        } else seriesLayer.active = true
    }
    // Direct provider compatibility for the returning WeebCentral colored edition.
    function openWeebCentralSeries(title, requestedVolumeNumber) {
        if (seriesLayer.active) seriesLayer.active = false
        clearSeriesEditionProfile()
        seriesLayer.legacyWeebCentral = true
        seriesLayer.resumeSeriesId = ""
        seriesLayer.resumeChapterId = ""
        seriesLayer.resumeVolumeId = ""
        seriesLayer.requestedVolumeNumber = String(requestedVolumeNumber || "")
        seriesLayer.title = title || ""
        seriesLayer.active = true
    }

    // open a manga series AND jump straight into the reader at a saved chapter (Continue resume).
    function openSeriesAt(title, seriesId, chapterId) {
        if (seriesLayer.legacyWeebCentral) { seriesLayer.active = false; seriesLayer.legacyWeebCentral = false }
        seriesLayer.resumeSeriesId = seriesId || ""
        seriesLayer.resumeChapterId = chapterId || ""
        seriesLayer.resumeVolumeId = ""
        seriesLayer.requestedVolumeNumber = ""
        restoreSeriesEditionProfile(seriesId)
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
    function _openPinnedGcdSeries(d) {
        var gcdId = Number((d && d.gcdId) || 0)
        var posts = (d && d.posts) || []
        if (!gcdId || !posts.length) return false
        var rel = []
        for (var i = 0; i < posts.length; i++)
            rel.push({ id: String(posts[i]), url: "https://getcomics.org/?p=" + posts[i],
                       name: (d && d.title) || "", cover: "", year: Number((d && d.year) || 0),
                       sizeMB: 0, synopsis: "", date: "", collection: true })
        westernLayer.baked = { gcdId: gcdId, releases: rel, cover: (d && d.cover) || "" }
        westernLayer.title = (d && d.title) || ""
        westernLayer.tagSlug = ""; westernLayer.tagId = 0
        westernLayer.resumeChapterId = (d && d.resumeChapterId) || ""
        if (westernLayer.active && westernLayer.item) {
            var it = westernLayer.item
            it.bakedReleases = null
            it.seriesTitle = westernLayer.title
            it.poster = westernLayer.baked.cover || ""
            it.gcdId = gcdId
            it.bakedReleases = rel
            it.tagId = 0; it.tagSlug = ""
            it.openChapterId = westernLayer.resumeChapterId || ""
        } else westernLayer.active = true
        return true
    }

    function openGcdSeries(d) {
        if (typeof ComicsCatalog === "undefined" || !ComicsCatalog.ready()) {
            if (_openPinnedGcdSeries(d)) return
            if (d && d.title) win.openWestern({ title: d.title })
            return
        }
        var gcdId = Number((d && d.gcdId) || 0)
        var s = ComicsCatalog.series(gcdId)
        if (!s || s.gcdId === undefined) {
            if (_openPinnedGcdSeries(d)) return
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

    // ---- a demuxed multi-volume pack shelf (Slice 5, 2026-08-07). The downloads index
    //      already holds the demuxed volumes (Slice 2's demultiplexPack wrote them with
    //      packRole "main"/"extra" + packOrder); packVolumes() (Slice 4) hands them back as
    //      {mains:[...], extras:[...]} in natural reading order. We inject those rows into the
    //      western series surface as a BAKED release list, with a packSeriesId that pins the
    //      reader's progress identity — mirroring openGcdSeries' baked injection (Main.qml:591)
    //      but sourced from the downloads store instead of the catalogue.
    //
    //      IDENTITY-ORDERING LAW (df003eb, non-negotiable): packSeriesId + bakedReleases are
    //      set BEFORE openChapterId — ComicReaderShell must mount with a STABLE seriesId so its
    //      resume reads the right progress key. Same sequence as openGcdSeries.
    //      d: { seriesId, seriesTitle, resumeChapterId? } ----
    function _packRow(r) {
        // Map a packVolumes() row to the release shape ComicSeries.qml renders. The pack
        // fields (packRole/packOrder/packId/pages) ride along so the section builder can split
        // mains from extras and the reader chain can build a mains-only crossing list.
        return { id: String(r.id || ""), name: String(r.label || ""),
                 url: "", cover: String(r.art || ""), year: 0, sizeMB: 0, synopsis: "",
                 date: "", collection: false,
                 packRole: String(r.packRole || ""), packOrder: Number(r.packOrder || 0),
                 packId: String(r.id || ""), pages: Number(r.pages || 0) }
    }
    function openPackSeries(d) {
        var seriesId = String((d && d.seriesId) || "")
        if (!seriesId.length) { console.warn("pack: openPackSeries — no seriesId"); return }
        var pv = (typeof Comics !== "undefined") ? Comics.packVolumes(seriesId) : { mains: [], extras: [] }
        var rel = []
        var mains = pv.mains || []
        var extras = pv.extras || []
        for (var i = 0; i < mains.length; i++) rel.push(_packRow(mains[i]))
        for (var j = 0; j < extras.length; j++) rel.push(_packRow(extras[j]))
        if (!rel.length) { console.warn("pack: openPackSeries — no volumes for", seriesId); return }
        westernLayer.baked = { packSeriesId: seriesId, releases: rel, cover: "" }
        westernLayer.title = (d && d.seriesTitle) || ""
        westernLayer.tagSlug = ""; westernLayer.tagId = 0
        westernLayer.resumeChapterId = (d && d.resumeChapterId) || ""
        if (westernLayer.active && westernLayer.item) {
            var it = westernLayer.item
            it.bakedReleases = null                 // reset first so re-injection repaints
            it.seriesTitle = westernLayer.title
            it.packSeriesId = westernLayer.baked.packSeriesId   // identity FIRST (df003eb)
            it.bakedReleases = westernLayer.baked.releases      // seriesId now stable
            it.tagId = 0; it.tagSlug = ""                       // resolve() guard: baked, no live lookup
            it.openChapterId = westernLayer.resumeChapterId || ""   // open reader LAST — identity stable
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
        comicSeriesLayer.resumeChapterId = ""
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
    function openComicSeriesAt(title, numericLocgId, chapterId) {
        // Numeric gc: identities are LOCG/DB-backed. Load the catalogue before the
        // reader target is assigned so ComicSeriesPage.attach() can establish gcTag
        // synchronously; otherwise the reader mounts under the transient "gc:" key.
        if (!ComicsDb.ready()) comicsDbLoader.active = true
        comicSeriesLayer.resumeChapterId = chapterId || ""
        comicSeriesLayer.locgSid = "locg:" + String(numericLocgId || "")
        comicSeriesLayer.locgMeta = ({})
        comicSeriesLayer.title = title || ""
        comicSeriesLayer.cover = ""
        if (comicSeriesLayer.active && comicSeriesLayer.item) {
            var it = comicSeriesLayer.item
            it.seriesTitle = comicSeriesLayer.title
            it.cover = ""
            it.locgMeta = ({})
            it.locgId = comicSeriesLayer.locgSid             // DB identity first
            it.openChapterId = comicSeriesLayer.resumeChapterId   // local reader second
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

    // ---- video player: a fullscreen layer over everything; kept alive once opened so mpv
    //      isn't torn down/recreated each play (avoids the use-after-free teardown trap). ----
    property bool playerOpen: false
    // The movie session the player minimized while still loaded. Reopening it from the
    // taskbar finds the stream warm — we resume in place instead of re-streaming.
    property string warmPlayerSessionId: ""

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

    // Every immersive manga/comic/reader/player surface suppresses the OS-shell taskbar.
    // A Tankoban series detail is itself an immersive Reading Room now: the fixed rail and
    // volume pane own the whole window before a reader is opened, so the taskbar must not
    // remain visible underneath that surface.
    readonly property bool immersiveSurfaceOpen: win.playerOpen
        || bookReaderLayer.active
        || vaultComicLayer.active
        || seriesLayer.active
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
            var directUrl = best.url ? String(best.url) : ""
            if (!directUrl.length && String(best.infoHash || "").indexOf("url:") === 0)
                directUrl = String(best.infoHash).substring(4)
            if ((best.streamKind === "Direct" || directUrl.length) && directUrl.length) {
                Download.feedSource(id, directUrl,
                                    (best.headers && typeof best.headers === "object" && !Array.isArray(best.headers))
                                    ? best.headers : ({}))
                return
            }
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
        setGuiStallContext("open", "Downloads")
        extensionsLayer.active = false
        settingsLayer.active = false
        keyboardGuideLayer.active = false
        updateLayer.active = false
        vaultLayer.active = false
        downloadsLayer.active = true
        taskbar.open = false
    }
    function closeDownloadsPage() {
        downloadsLayer.active = false
        setGuiStallContext("navigate", currentSurface || "Home")
    }

    // ---- Vault page: the taskbar folder door's full-page, mutually exclusive with the other taskbar
    // full-pages (Slice 10). It overlays the current surface (z:56); closing just deactivates the
    // Loader and reveals whatever the user stood on. ----
    function openVaultPage() {
        downloadsLayer.active = false
        extensionsLayer.active = false
        settingsLayer.active = false
        keyboardGuideLayer.active = false
        updateLayer.active = false
        vaultLayer.active = true
        taskbar.open = false
    }
    function closeVaultPage() { vaultLayer.active = false }

    // Esc inside the Vault, mirroring VaultPage's own top-left Back law: sheet, then folder
    // detail, then one browse level up (or out of the Hidden shelf), and only leave once
    // there is nowhere left to ascend.
    // The sheet must be handled HERE, not by its own Keys.onPressed: the window-context
    // Escape Shortcut above is matched before the key reaches the focus item, and nothing in
    // the Vault accepts Keys.onShortcutOverride, so an inner handler can never win Escape
    // back. Backspace is not a Shortcut, so inner ascend handlers still see it.
    function vaultBack() {
        var page = vaultLayer.item
        if (!page) { win.closeVaultPage(); return }
        if (page.detailSheetVisible) { page.closeDetailSheet(); return }
        // Vault ux uplift S14: Escape leaves the search pseudo-level first — it is the most
        // transient view state (the field itself can never see the key: the window-context
        // Shortcut is matched before the focus item, the S2 lesson).
        if (page.searchViewActive) { page.leaveSearchView(); return }
        if (page.hiddenViewActive || (page.crumbStack && page.crumbStack.length > 1)) {
            page.ascendBrowse()
            return
        }
        win.closeVaultPage()
    }

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
    function openOnePaceArc(arc) { win.openExtensionsPage("theatre") }
    function openUniverseHall() { universeHallLayer.active = true }
    function closeUniverseHall() { universeHallLayer.active = false }

    // world (optional, R1 2026-08-21): lands the page straight on that world's Sources
    // tab instead of the default "theatre" — the manga picker's empty-state route
    // passes "tankoban" so enabling Nyaa is one click, not a hunt through tabs.
    function openExtensionsPage(world) {
        downloadsLayer.active = false
        settingsLayer.active = false
        keyboardGuideLayer.active = false
        updateLayer.active = false
        vaultLayer.active = false
        extensionsLayer.active = true
        if (world && extensionsLayer.item) extensionsLayer.item.world = world
        taskbar.open = false
    }
    function closeExtensionsPage() { extensionsLayer.active = false }

    // ---- Settings page: the global preferences gear, entered from the taskbar ----
    function openSettingsPage() {
        downloadsLayer.active = false
        extensionsLayer.active = false
        keyboardGuideLayer.active = false
        updateLayer.active = false
        vaultLayer.active = false
        settingsLayer.active = true
        taskbar.open = false
    }
    function closeSettingsPage() { settingsLayer.active = false }

    // ---- Keyboard Guide: essential controls, entered from the taskbar beside Settings. ----
    function openKeyboardGuide() {
        downloadsLayer.active = false
        extensionsLayer.active = false
        settingsLayer.active = false
        updateLayer.active = false
        vaultLayer.active = false
        keyboardGuideLayer.active = true
        taskbar.open = false
        if (keyboardGuideLayer.item && keyboardGuideLayer.item.takeKeyboardFocus)
            Qt.callLater(keyboardGuideLayer.item.takeKeyboardFocus)
    }
    function closeKeyboardGuide() { keyboardGuideLayer.active = false }

    // ---- Update page: the verified release chronicle, mutually exclusive with the other
    // taskbar full-pages. Opening it marks only the current release as seen; availability stays.
    function openUpdatePage() {
        downloadsLayer.active = false
        extensionsLayer.active = false
        settingsLayer.active = false
        keyboardGuideLayer.active = false
        vaultLayer.active = false
        updateLayer.active = true
        // Full-bleed: the chronicle owns the whole page. The taskbar closes like
        // every other full-page destination (Downloads/Vault/Extensions/Settings)
        // and still reveals on hover for session switching. The Update entry point
        // is the home topbar glyph now — no launcher in the taskbar dock.
        taskbar.open = false
        if (typeof Updates !== "undefined" && Updates.markSeen)
            Updates.markSeen()
    }
    function closeUpdatePage() { updateLayer.active = false }
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
            win.openBookSession(item.path, { "id": item.id || item.path, "title": item.title || "", "author": item.author || "" })
        } else if (item.kind === "comic") {
            var destination = ComicDownloadRoute.destination(item)
            if (destination === "pack") {
                win.openPackSeries({ seriesId: item.seriesId,
                                     seriesTitle: item.seriesTitle,
                                     resumeChapterId: item.id })
            } else if (destination === "locg") {
                // DB-backed ComicSeriesPage stores its reader namespace as gc:<LOCG id>.
                // That numeric suffix is not a GetComics tag and must reopen the DB lane.
                win.openComicSeriesAt(item.seriesTitle, String(item.seriesId).slice(3), item.id)
            } else if (destination === "getcomics") {
                win.openWesternAt(item.seriesTitle, String(item.seriesId).slice(3), item.id)
            } else if (destination === "gcd") {
                win.openGcdSeries({ gcdId: Number(String(item.seriesId).slice(4)),
                                    title: item.seriesTitle, resumeChapterId: item.id })
            } else {
                console.log("[route] ignoring unknown comic id:", item.seriesId)
            }
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
                        "title": job.title || "", "headers": job.headers || ({}),
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
        setGuiStallContext("open", "Player")
        if (!playerLayer.active) playerLayer.active = true
        win.playerOpen = true
        // `backdrop` is the poster url; subType/subId (e.g. "movie"/"tt123" or "series"/"tt123:1:2")
        // let the player fetch online subtitles for this exact title/episode.
        playerLayer.item.playTorrent(infoHash, fileIdx, title, backdrop, subType, subId, streamCandidates || [], playbackContext || ({}))
    }

    function openWatchPartyRoomSource() {
        if (typeof WatchPartyUi === "undefined" || !WatchPartyUi.inRoom)
            return false
        var source = WatchPartyUi.roomSource || ({})
        if (String(source.kind || "") !== "torrent")
            return false
        var hash = String(source.infoHash || "").toLowerCase()
        var fileIdx = Number(source.fileIdx || 0)
        if (hash.length !== 40 || !isFinite(fileIdx) || fileIdx < 0 || Math.floor(fileIdx) !== fileIdx)
            return false
        if (playerLayer.active && playerLayer.item
                && playerLayer.item.watchPartySourceEligible
                && playerLayer.item.watchPartySourceMatchesRoom) {
            playerLayer.item.syncWatchPartyPlayerObservation()
            return true
        }
        if (!playerLayer.active) playerLayer.active = true
        if (!playerLayer.item) return false
        win.playerOpen = true
        var candidate = { "infoHash": hash, "fileIdx": fileIdx }
        playerLayer.item.playTorrent(hash, fileIdx, "Watch Party", "", "", "",
                                    [candidate], { "watchPartyJoin": true })
        return true
    }
    function closePlayer() {
        if (playerLayer.item) playerLayer.item.stop()
        win.playerOpen = false
        setGuiStallContext("navigate", currentSurface || "Home")
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
        setGuiStallContext("open", "Reader")
        bookReaderLayer.bookPath = path
        bookReaderLayer.bookMeta = book || ({})
        if (bookReaderLayer.active && bookReaderLayer.item) {
            bookReaderLayer.item.bookMeta = bookReaderLayer.bookMeta   // fresh catalog identity per book
            bookReaderLayer.item.openBook(path)
        } else bookReaderLayer.active = true
    }
    function closeBookReader() {
        bookReaderLayer.active = false
        setGuiStallContext("navigate", currentSurface || "Home")
    }

    // ---- search: a layer over the world. Biblio has its own rich surface; Tankoban + Theatre use the
    //      generic SearchSurface fed by their own source (AniList / Cinemeta). ----
    function openSearch() {
        setGuiStallContext("open", "Search")
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
    function closeSearch() {
        searchLayer.active = false
        setGuiStallContext("navigate", currentSurface || "Home")
    }
    function closeWorldSearch() {
        worldSearchLayer.active = false
        setGuiStallContext("navigate", currentSurface || "Home")
    }
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
            // A pre-teardown hosted-player (VidKing) watch: the hosted surface is gone
            // (House HTTP slice 4), so route to the Theatre detail — the user picks a real
            // source there instead of resuming into a player that no longer exists.
            if (r.hostedPlayerId) {
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
    // The no-hang floor for detailContinue's movie/series race: if neither typed meta ask
    // has answered (deep-tail title, offline, or a stalled transport leg), open as a movie
    // — a sparse detail page beats a click that does nothing.
    Timer {
        id: continueKindFloor
        interval: 4000
        repeat: false
        property var settle: null
        onTriggered: if (settle) settle()
    }

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
            // Resolve movie vs series live from Cinemeta, then open the Theatre detail —
            // no stored type needed, so existing entries work too.
            // Cinemeta answers a wrong-type meta ask with a 307 into the unpinned live host —
            // a leg the pinned transport cannot follow (it stalls, and QML XHR ignores
            // .timeout) — so a lone series-probe hung forever on every movie tile
            // (diagnosed 2026-08-08). Race both typed asks instead: the matching type's
            // direct 200 settles in well under a second, and the 4s floor below opens the
            // tile as a movie rather than letting the click die silently.
            var settled = false
            var settle = function(type, meta) {
                if (settled) return
                settled = true
                continueKindFloor.stop()
                win.openTheatreSeries({ id: id, type: type, title: title, cover: entry.cover || "" })
            }
            TheatreApi.loadMeta("movie", id, function(meta) { if (meta) settle("movie", meta) })
            TheatreApi.loadMeta("series", id, function(meta) { if (meta) settle("series", meta) })
            continueKindFloor.settle = function() { settle("movie", null) }
            continueKindFloor.restart()
        } else if (entry.kind === "tankoban") {
            // detail = the manga series page; Tankoban Mode restores itself from the
            // service's per-series flag once the id resolves.
            win.openSeries(title)
        } else if (entry.kind === "manga" || entry.kind === "comic") {
            var comicDest = entry.kind === "comic"
                ? ComicDownloadRoute.destination({ seriesId: entry.id || "" }) : "unknown"
            if (comicDest === "locg")
                win.openComicSeries({ id: "locg:" + String(entry.id).slice(3), title: title,
                                      cover: entry.cover || "" })
            else if (comicDest === "getcomics")
                win.openWestern({ title: title, tag: String(entry.id).slice(3) })
            else if (comicDest === "gcd")
                win.openGcdSeries({ gcdId: Number(String(entry.id).slice(4)), title: title,
                                    cover: entry.cover || "" })
            else if (entry.kind === "comic")
                // retired-source or unknown comic id — honest no-op (preset-pages source cut 2026-07-12)
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
        setGuiStallContext("open", "Player")
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
        setGuiStallContext("open", "Player")
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
        setGuiStallContext("open", "Reader")
        var b = book || ({})
        Sessions.openOrSwitch({
            "appType": "biblio", "contentKind": "book", "title": b.title || "Book",
            "target": { "path": path, "book": b, "id": path }
        })
    }

    // ── Vault launch pillar (execution plan Slice 8): hand the app a local file ──
    // The picker / an OS drag-drop / Ctrl+O all funnel here. LocalLaunch (C++) routes the
    // FIRST file and decides if it can open at all; QML paints the door — the right reader/
    // player as a normal taskbar session, or a categorized "can't open" with NO tile.
    function dispatchLocalRoute(r) {
        if (!r) return
        if (r.prompt && typeof LocalLaunch !== "undefined") {
            win.pendingIdentityRoute = r
            identityCeremonyDialog.ceremony = r
            identityCeremonyDialog.open()
            return
        }
        localLaunchState.lastRouteKind = r.family || "unknown"
        localLaunchState.lastRejectCategory = r.accepted ? "" : (r.reject || "unsupported")
        if (!r.accepted) { win.showLocalRejection(r); return }
        localLaunchState.openCount = localLaunchState.openCount + 1
        if (r.family === "comic")      win.openVaultComic(r.path, r.vaultId, r.title)
        else if (r.family === "book")  win.openBookSession(r.path, { "id": r.vaultId, "title": r.title })
        else if (r.family === "video") win.openLocalVideoSession(win.videoTargetFor(r.path, r.vaultId, r.title))
    }
    function openLocalMedia(paths) {
        if (!paths || !paths.length) return
        if (typeof LocalLaunch === "undefined") return
        LocalLaunch.openAsync(paths)
    }
    function openNextToOpen(index) {
        if (typeof LocalLaunch === "undefined") return
        LocalLaunch.openNextToOpenAsync(index)
    }
    function removeNextToOpen(index) {
        if (typeof LocalLaunch !== "undefined") LocalLaunch.removeNextToOpen(index)
    }
    function decidePendingIdentity(choice) {
        var pending = win.pendingIdentityRoute
        if (!pending || typeof LocalLaunch === "undefined") return
        if (!LocalLaunch.decideIdentityCeremony(pending.relationship || "", choice)) return
        win.pendingIdentityRoute = null
        identityCeremonyDialog.close()
        LocalLaunch.routeInfoAsync(pending.path || "")
    }
    // Build the player target for a local video, resuming at the saved spot. A finished movie
    // (>=90%) is dropped from Progress, so its lookup is empty → position 0 → restart from the
    // top; an unfinished one carries its resume position (Slice 9 reopen semantics).
    function videoTargetFor(path, vaultId, title) {
        var pos = 0
        if (typeof Progress !== "undefined") {
            var pg = Progress.get("video", vaultId || "")
            if (pg && pg.resume && pg.resume.position !== undefined)
                pos = Number(pg.resume.position) || 0
        }
        return { "path": path, "id": vaultId, "title": title, "kind": "video", "position": pos }
    }
    // One-click reopen from the recent list. A dead file offers nothing. If the file is already
    // open, focus its tile (no duplicate, no re-count); otherwise route it fresh — comics/books
    // resume via their reader, video resumes-or-restarts via videoTargetFor above.
    function reopenRecent(entry) {
        if (!entry || !entry.available) return
        var existing = win.findVaultSession(entry.vaultId)
        if (existing.length) { Sessions.switchTo(existing); return }
        win.openLocalMedia([entry.path])
    }
    function findVaultSession(vaultId) {
        if (!vaultId) return ""
        var list = Sessions.list()
        for (var i = 0; i < list.length; i++) {
            var t = list[i].target || ({})
            if (String(t.id || "") === vaultId) return list[i].id
        }
        return ""
    }
    // A loose local comic has no series page — open it in the standalone Vault reader host
    // (vaultComicLayer) as a taskbar comic session keyed to its content id.
    function openVaultComic(path, vaultId, title) {
        if (!path) return
        Sessions.openOrSwitch({
            "appType": "tankoban", "contentKind": "comic", "title": title || "Comic",
            "target": { "vaultPath": path, "id": vaultId || ("vault-file:" + path), "title": title || "" }
        })
    }
    function showLocalRejection(r) {
        var msg = "Can't open this file."
        var c = r ? r.reject : ""
        if (c === "corrupt") msg = "This comic looks damaged — there's nothing to read."
        else if (c === "no-decoder") msg = "This video can't be played."
        else if (c === "not-found") msg = "That file no longer exists."
        else msg = "That file type isn't supported."
        localLaunchToast.flash(msg, false)
    }
    function showFolderDropExplain() {
        // The folder → Vault shelf gesture is Slice 10; until then, explain + offer the picker.
        localLaunchToast.flash("Folders open in the Vault (coming soon). For now, pick media files:", true)
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
        if (rec && rec.contentKind === "movie" && win.warmPlayerSessionId === id) {
            if (playerLayer.item) playerLayer.item.stop()
            win.warmPlayerSessionId = ""
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
    // Vault comic (standalone reader host) session verbs — mirror the comic reader verbs,
    // but the live surface is vaultComicLayer, not a series page.
    function minimizeVaultComic() { Sessions.switchTo("") }
    function closeVaultComic() {
        var rec = Sessions.get(Sessions.activeId)
        if (rec && rec.contentKind === "comic") win.closeSession(rec.id)
        else vaultComicLayer.active = false
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
        } else if (rec.contentKind === "comic") {
            if (t.vaultPath && String(t.vaultPath).length) {
                // a loose local comic (Vault): no series page — mount the standalone reader
                // host with the injected VaultPageStore (execution plan Slice 8).
                vaultComicLayer.archivePath = t.vaultPath
                vaultComicLayer.vaultId = t.id || ""
                vaultComicLayer.title = t.title || rec.title || "Comic"
                if (vaultComicLayer.active && vaultComicLayer.item) {
                    vaultComicLayer.item.archivePath = t.vaultPath
                    vaultComicLayer.item.vaultId = t.id || ""
                    vaultComicLayer.item.title = vaultComicLayer.title
                } else vaultComicLayer.active = true
                return
            }
            var sessionComicDest = ComicDownloadRoute.destination({ seriesId: t.seriesId || "" })
            if (sessionComicDest === "locg") {
                win.openComicSeriesAt(t.title, String(t.seriesId).slice(3),
                                      (st.chapterId || t.chapterId || ""))
                return
            }
            if (sessionComicDest === "getcomics") {
                win.openWesternAt(t.title, String(t.seriesId).slice(3), (st.chapterId || t.chapterId || ""))
                if (westernLayer.item && westernLayer.item.restoreState) westernLayer.item.restoreState(st)
                return
            }
            if (sessionComicDest === "gcd") {
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
                restoreSeriesEditionProfile(t.seriesId)
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
            restoreSeriesEditionProfile(t.seriesId)
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
        } else if (rec.contentKind === "comic") {
            // one comic surface hosts the reader at a time — drop whichever is live
            if (vaultComicLayer.active) vaultComicLayer.active = false
            else if (comicSeriesLayer.active) comicSeriesLayer.active = false
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
        onAccountClicked: accountFlyout.toggle()
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
        // Update glyph (home only, takes the retired search slot). Toggle mirrors
        // the taskbar launcher: open when closed, close when already front.
        updateAvailable: typeof Updates !== "undefined" ? Updates.updateAvailable : false
        updateUnseen: typeof Updates !== "undefined" ? Updates.unseenUpdate : false
        reducedMotion: win.reducedMotion
        onUpdateClicked: !updateLayer.active ? win.openUpdatePage() : win.closeUpdatePage()
    }

    // Chrome-free desktop interaction for developer-windowed mode. Reuses the existing TopBar
    // as the drag surface (no titlebar added) and self-disables in fullscreen. See WindowBehavior.qml.
    WindowBehavior {
        shell: win
        dragSurface: topbar
        controller: WindowMode
    }

    // Kept as a Component so Progress.recent() and the ContinueTile delegates are not evaluated
    // while Main.qml's Home object tree is being constructed. The Loader below owns the same
    // bindings and signals as the former inline rail once the shell is idle.
    Component {
        id: homeContinueRailComponent
        Column {
            id: contCol
            width: parent ? parent.width : 0
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
            property bool hasResumeItems: contItems.length > 0
            visible: hasResumeItems
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
    }

    // ---- pinned top bar is above; everything below SCROLLS (vertical wheel/drag) ----
    Flickable {
        id: page
        objectName: "homePageFlickable"
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
                    interval: 6500; running: page.visible && !win.immersiveSurfaceOpen; repeat: true
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
            //      Reserve the old rail footprint until the idle Loader resolves its data. If
            //      there is no resume history, the reservation collapses after the same check that
            //      previously hid this entire Column.
            Loader {
                id: homeContinueRailLoader
                width: parent.width
                height: !homeContinueRailLoaded
                         || (item && item.hasResumeItems)
                         ? homeContinueRailPlaceholderHeight : 0
                asynchronous: true
                onLoaded: {
                    homeContinueRailRequested = false
                    homeContinueRailLoaded = true
                }
                onStatusChanged: if (status === Loader.Error) {
                    homeContinueRailRequested = false
                    homeContinueRailLoaded = true
                }
            }

            // ---- 4. MODE-INTRO WIDGETS — the board that introduces each app AND shows what's inside.
            //      First prototype: Tankoban as a BOOKSHELF (manga covers standing on a shelf ledge).
            //      The other modes get their own widget forms next; this is the shape to react to.
            Loader {
                id: homeBookshelfLoader
                width: parent.width
                height: 400
                active: false
                asynchronous: true
                onLoaded: {
                    item.track = Qt.binding(function() { return page.contentY })
                    item.mangaBooks = Qt.binding(function() { return Catalog.topManga })
                    item.comicsBooks = Qt.binding(function() { return Catalog.topComics })
                    item.clicked.connect(function() { win.openWorld("Tankoban") })
                    item.bookClicked.connect(function() { win.openWorld("Tankoban") })
                    win.homeIntroWidgetSettled(homeBookshelfLoader)
                }
                onStatusChanged: if (status === Loader.Error) win.homeIntroWidgetSettled(homeBookshelfLoader)
            }

            // Theatre = the film-strip, Biblio = the reading desk (mock-reviewed 2026-07-04;
            // both self-load their data, so the board wiring stays declarative).
            Loader {
                id: homeTheatreStripLoader
                width: parent.width
                height: 400
                active: false
                asynchronous: true
                onLoaded: {
                    item.track = Qt.binding(function() { return page.contentY })
                    item.clicked.connect(function() { win.openWorld("Theatre") })
                    win.homeIntroWidgetSettled(homeTheatreStripLoader)
                }
                onStatusChanged: if (status === Loader.Error) win.homeIntroWidgetSettled(homeTheatreStripLoader)
            }

            Loader {
                id: homeReadingDeskLoader
                width: parent.width
                height: 400
                active: false
                asynchronous: true
                onLoaded: {
                    item.track = Qt.binding(function() { return page.contentY })
                    item.clicked.connect(function() { win.openWorld("Biblio") })
                    item.genrePicked.connect(function(name) {
                        win.openWorld("Biblio")
                        win.openBiblioGenre(name)
                    })
                    win.homeIntroWidgetSettled(homeReadingDeskLoader)
                }
                onStatusChanged: if (status === Loader.Error) win.homeIntroWidgetSettled(homeReadingDeskLoader)
            }
            Loader {
                id: homeVaultWidgetLoader
                width: parent.width
                height: 520
                active: false
                asynchronous: true
                onLoaded: {
                    item.track = Qt.binding(function() { return page.contentY })
                    item.clicked.connect(function() { win.openVaultPage() })
                    win.homeIntroWidgetSettled(homeVaultWidgetLoader)
                }
                onStatusChanged: if (status === Loader.Error) win.homeIntroWidgetSettled(homeVaultWidgetLoader)
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
        running: win.worldWarmerEnabled
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
                // Pass the initial activation state into the component before Component.onCompleted.
                // This keeps opt-in warmers from running a world's synchronous setup while hidden.
                active: false
                // Stage 2: build off the GUI thread so instantiating a world (~190 tiles)
                // never freezes the app — the page fills in progressively, and warming
                // (below) builds it hidden ahead of the first click.
                asynchronous: true
                Component.onCompleted: {
                    setSource(win.worldSourceFor(mode), {
                        "lifecycleActive": worldStack.current === mode
                    })
                    active = true
                }
                onLoaded: {
                    if (item.lifecycleActive !== undefined)
                        item.lifecycleActive = Qt.binding(function() {
                            return worldStack.current === mode
                        })
                    item.medium = mode
                    item.backdrop = wall
                    item.homeRequested.connect(win.closeWorld)
                    item.mediumSelected.connect(win.openWorld)
                    item.seriesRequested.connect(win.openSeries)
                    if (item.mangaOpenById) item.mangaOpenById.connect(win.openSeries)
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
                    if (item.accountClicked) item.accountClicked.connect(function() { accountFlyout.toggle() })
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
        property string malId: ""             // Slice C: Discover card's MAL id, threaded to the series page
        property string seriesIdOverride: ""
        property string sourceSearchTitle: ""
        property var sourceSearchAliases: []
        property var sourceRequiredMarkers: []
        property string resumeSeriesId: ""    // Continue resume: jump straight to this chapter…
        property string resumeChapterId: ""   //   …in this series (set seriesId BEFORE the chapter)
        property string resumeVolumeId: ""    // Tankoban resume: open this VOLUME (Mode ON) instead
        property string requestedVolumeNumber: "" // temporary arc-volume landing; chapter catalogue follows
        property bool legacyWeebCentral: false
        source: legacyWeebCentral ? "MangaSeriesThumbnailMock.qml" : "MangaSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.malId = seriesLayer.malId
            item.seriesIdOverride = seriesLayer.seriesIdOverride
            item.sourceSearchTitle = seriesLayer.sourceSearchTitle
            item.sourceSearchAliases = seriesLayer.sourceSearchAliases
            item.sourceRequiredMarkers = seriesLayer.sourceRequiredMarkers
            item.requestedVolumeNumber = seriesLayer.requestedVolumeNumber
            item.seriesTitle = seriesLayer.title
            if (seriesLayer.resumeSeriesId) item.seriesId = seriesLayer.resumeSeriesId
            if (seriesLayer.resumeChapterId) item.openChapterId = seriesLayer.resumeChapterId
            if (seriesLayer.resumeVolumeId) item.resumeTankobanVolume(seriesLayer.resumeVolumeId)
            item.backRequested.connect(win.closeSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.openExtensionsRequested.connect(function() { win.openExtensionsPage("tankoban") })
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
        property var    baked: null           // catalogue run {gcdId,releases,cover} OR pack {packSeriesId,releases,cover} OR universe {gcdId:0,releases,cover}
        source: "ComicSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = westernLayer.title
            item.tagId = westernLayer.tagId
            // Resolve the baked IDENTITY (-> seriesId "gcd:<id>" for catalogue, or the explicit
            // packSeriesId for a downloads-backed pack shelf) BEFORE opening the reader, so
            // ComicReaderShell mounts with a STABLE seriesId and its resume reads the RIGHT
            // progress key. Opening the reader first mounted it under the transient
            // "gc:<empty-slug>" identity -> restored page 1, then saved to the OTHER key and
            // wrote page 1 over the real record (runtime-confirmed 2026-08-06). Same fix as
            // openGcdSeries() above. The pack branch (Slice 5) sets packSeriesId — an explicit
            // downloads-backed identity that needs no gcdId resolution.
            if (westernLayer.baked) {
                item.poster = westernLayer.baked.cover || item.poster
                if (westernLayer.baked.packSeriesId !== undefined
                    && String(westernLayer.baked.packSeriesId).length > 0) {
                    item.packSeriesId = westernLayer.baked.packSeriesId   // pack identity (stable, explicit)
                } else {
                    item.gcdId = westernLayer.baked.gcdId                 // catalogue identity
                }
                item.bakedReleases = westernLayer.baked.releases   // seriesId now stable (gcd:<id> or pack id)
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
        property string resumeChapterId: "" // Downloads/Continue can enter the local reader directly
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
            item.locgId = comicSeriesLayer.locgSid       // identity first — triggers attach()
            if (comicSeriesLayer.resumeChapterId)
                item.openChapterId = comicSeriesLayer.resumeChapterId   // local reader second
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

    // ---- Vault standalone comic reader: a single loose CBZ opened from the Vault, with the
    // injected VaultPageStore — no series page behind it (execution plan Slice 8). ----
    Loader {
        id: vaultComicLayer
        anchors.fill: parent
        z: 57
        active: false
        visible: active
        property string archivePath: ""
        property string vaultId: ""
        property string title: "Comic"
        source: "comicreader/VaultComicReader.qml"
        onLoaded: {
            item.archivePath = vaultComicLayer.archivePath
            item.vaultId = vaultComicLayer.vaultId
            item.title = vaultComicLayer.title
            item.minimizeRequested.connect(win.minimizeVaultComic)
            item.closeRequested.connect(win.closeVaultComic)
            item.backRequested.connect(win.closeVaultComic)
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
        objectName: "downloadsLayer"
        anchors.fill: parent
        z: 56     // taskbar full-page: ABOVE every browsing/detail page (universe 52, series/book 53)
                  // so clicking Downloads always lands on top — below only the immersive surfaces
                  // (book reader 58, player 60). Was 52: any detail page silently covered it.
        active: false
        visible: active
        // DownloadsPage is a large, taskbar-only surface. Spread component creation across
        // frames so opening the download ledger does not synchronously block the world underneath.
        // The route handlers only touch the item from onLoaded, after construction completes.
        asynchronous: true
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

    // ---- Vault page: "On this machine", entered from the taskbar folder door (Slice 10) ----
    // A z:56 overlay exactly like downloadsLayer: opening it hides no world (it sits ABOVE the
    // preserved Home/world/detail), so Back merely deactivates this Loader and reveals whatever the
    // user stood on. Paints from the VaultLibrary read-model. Add-folder opens the native folder
    // picker; the folder-scan ingest lands in Slice 11.
    Loader {
        id: vaultLayer
        objectName: "vaultLayer"
        anchors.fill: parent
        z: 56
        active: false
        visible: active
        // VaultPage is a large, taskbar-only surface. Spread component creation across
        // frames so opening the folder door does not synchronously block the world underneath.
        // The open/close paths tolerate item being null until onLoaded (see vaultBack()).
        asynchronous: true
        source: "VaultPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeVaultPage)
            item.addFolderRequested.connect(function() { vaultFolderDialog.open() })
            if (item.openMediaRequested)                      // Slice 14: folder-view row / preview door → the shared LocalLaunch open path
                item.openMediaRequested.connect(function(path) { win.openLocalMedia([path]) })
            item.viewWorldRequested.connect(win.openVaultIdentity)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // Slice 15: the live-shelf watcher defers its upserts while an immersive surface
    // (player/reader) is open — "no watcher activity during playback beyond the debounce
    // accumulating" (behavior to preserve). The gate is C++ state (VaultLibrary.immersive);
    // QML only reports the surface fact. Null-target Binding is a no-op in tests.
    Binding {
        target: typeof VaultLibrary !== "undefined" ? VaultLibrary : null
        property: "immersive"
        value: win.immersiveSurfaceOpen
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

    // ---- Universes: extension-backed bespoke pages + generic fallback ----
    Loader {
        id: universeLayer
        anchors.fill: parent
        z: 52
        active: false
        visible: active
        asynchronous: true
        property string extensionId: ""
        property string universeName: ""
        source: extensionId === "com.colosseum.universe.dcau"
                ? "DCAUUniversePage.qml"
                : (extensionId === "com.colosseum.universe.starwars"
                   ? "GalaxyUniversePage.qml"
                   : (extensionId === "com.colosseum.universe.onepiece"
                      ? "OnePieceUniversePage.qml"
                      : "UniverseExtensionPage.qml"))
        onLoaded: {
            // NO item.backdrop — UniverseExtensionPage has no such property; it paints its
            // own flat #0c0e11 instead of sampling the shared wallpaper.
            item.extensionId = universeLayer.extensionId
            item.universeName = universeLayer.universeName
            if (universeLayer.extensionId === "com.colosseum.universe.dcau" ||
                    universeLayer.extensionId === "com.colosseum.universe.starwars" ||
                    universeLayer.extensionId === "com.colosseum.universe.onepiece")
                item.reducedMotion = Qt.binding(function() { return win.reducedMotion })
            if (universeLayer.extensionId === "com.colosseum.universe.onepiece")
                item.installedExtensions = Qt.binding(function() { return win.installedExtensions })
            item.backRequested.connect(win.closeUniverse)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            // Works open ABOVE this overlay — series/theatre/western are z:53 (> this layer's 52),
            // book is z:53 — so a clicked work paints on top and the overlay stays loaded beneath.
            // Their Esc checks sit before closeUniverse, so back closes the work first, then the
            // universe. (Replaces an earlier close-on-click that broke back-nav to the universe.)
            item.watchRequested.connect(win.openTheatreSeries)
            if (universeLayer.extensionId === "com.colosseum.universe.dcau") {
                item.comicRequested.connect(win.openGcdSeries)
                item.continueResumeRequested.connect(win.resumeContinue)
                item.continueDetailRequested.connect(win.detailContinue)
            } else {
                item.bookRequested.connect(win.openBook)
                item.comicsArchiveRequested.connect(win.openUniverseComic)
                // manga → Tankoban. Edition-aware entries can carry a discovery/storage
                // profile while reusing the same catalogue identity as the base manga.
                item.seriesRequested.connect(function(e) {
                    var requested = (e && e.requestedVolumeNumber) ? String(e.requestedVolumeNumber) : ""
                    if (e && e.provider === "weebcentral") win.openWeebCentralSeries(e.title || "", requested)
                    else if (e && e.provider === "tankoban") win.openSeries(e.title || "", e.malId || "", {
                        malId: e.malId || "", seriesId: e.seriesId || "",
                        sourceSearchTitle: e.sourceSearchTitle || "",
                        sourceSearchAliases: e.sourceSearchAliases || [],
                        sourceRequiredMarkers: e.sourceRequiredMarkers || []
                    }, requested)
                    else win.openSeries((e && e.title) || e || "", "", null, requested)
                })
                if (universeLayer.extensionId === "com.colosseum.universe.onepiece") {
                    item.onePaceRequested.connect(win.openOnePaceArc)
                    item.continueResumeRequested.connect(win.resumeContinue)
                    item.continueDetailRequested.connect(win.detailContinue)
                }
            }
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
        objectName: "extensionsLayer"
        anchors.fill: parent
        z: 56     // taskbar full-page, same rule as downloadsLayer (see its comment)
        active: false
        visible: active
        source: "ExtensionsPage.qml"
        onLoaded: {
            item.backdrop = wall
            // Same global preference Discover/genres/search read — a live binding so
            // flipping the Settings switch re-asks the registry without a relaunch.
            item.showExplicit = Qt.binding(function() { return contentPreferences.showExplicit })
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
        objectName: "settingsLayer"
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

    // ---- Keyboard Guide: essential controls, directly beside Settings on the taskbar. ----
    Loader {
        id: keyboardGuideLayer
        objectName: "keyboardGuideLayer"
        anchors.fill: parent
        z: 56
        active: false
        visible: active
        source: "KeyboardGuidePage.qml"
        onLoaded: {
            item.backRequested.connect(win.closeKeyboardGuide)
            item.takeKeyboardFocus()
        }
    }

    // ---- Update page: the release chronicle, entered from the permanent taskbar item ----
    Loader {
        id: updateLayer
        anchors.fill: parent
        z: 56
        active: false
        visible: active
        source: "UpdatePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.updates = typeof Updates !== "undefined" ? Updates : null
            item.reducedMotion = Qt.binding(function() { return win.reducedMotion })
            item.backRequested.connect(win.closeUpdatePage)
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
        onOpenMediaClicked: openMediaDialog.open()
        onOpenRecentRequested: openRecentPanel.toggle()
        onWatchPartyJoinClicked: watchPartyJoinSheet.openSheet()
        watchPartyJoinOpen: watchPartyJoinSheet.opened
        watchPartyJoinPhase: typeof WatchPartyUi !== "undefined" ? WatchPartyUi.phase : "unavailable"
        watchPartyJoinErrorCategory: typeof WatchPartyUi !== "undefined"
                                     ? WatchPartyUi.errorCategory : "unavailable"
        downloadsBadge: win.totalActiveDownloads
        downloadsActive: downloadsLayer.active
        onDownloadsClicked: !downloadsLayer.active ? win.openDownloadsPage() : win.closeDownloadsPage()
        vaultActive: vaultLayer.active
        onVaultClicked: !vaultLayer.active ? win.openVaultPage() : win.closeVaultPage()
        extensionsActive: extensionsLayer.active
        onExtensionsClicked: !extensionsLayer.active ? win.openExtensionsPage() : win.closeExtensionsPage()
        settingsActive: settingsLayer.active
        onSettingsClicked: !settingsLayer.active ? win.openSettingsPage() : win.closeSettingsPage()
        keyboardGuideActive: keyboardGuideLayer.active
        onKeyboardGuideClicked: !keyboardGuideLayer.active ? win.openKeyboardGuide() : win.closeKeyboardGuide()
    }

    // Slice 6: the account-optional Room ID door lives outside immersive Player 1.
    // It joins room membership only; playback readiness remains false until Player 1
    // positively loads the authoritative room source.

    Connections {
        target: typeof WatchPartyUi !== "undefined" ? WatchPartyUi : null
        function onRoomActivated(roomId) {
            void roomId
            win.openWatchPartyRoomSource()
        }
    }

    WatchPartyJoinSheet {
        id: watchPartyJoinSheet
        controller: typeof WatchPartyUi !== "undefined" ? WatchPartyUi : null
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)
    }

    // ── Vault launch entry points (execution plan Slice 8): Open Media…, drag-drop, Ctrl+O ──
    // The taskbar Open Media control, an app-wide file DropArea, and Ctrl+O all funnel into
    // win.openLocalMedia(). localLaunchState is the invisible automation surface (Lanista reads
    // lastRouteKind / lastRejectCategory / openCount by objectName).
    Item {
        id: localLaunchState
        objectName: "localLaunchState"
        visible: false
        property string lastRouteKind: ""
        property string lastRejectCategory: ""
        property int openCount: 0
    }

    FileDialog {
        id: openMediaDialog
        title: "Open Media"
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            "Media files (*.cbz *.cbr *.epub *.mp4 *.mkv *.avi *.mov *.webm *.m4v)",
            "Comics (*.cbz *.cbr)",
            "Books (*.epub)",
            "Video (*.mp4 *.mkv *.avi *.mov *.webm *.m4v)",
            "All files (*)"
        ]
        onAccepted: {
            var arr = []
            for (var i = 0; i < selectedFiles.length; i++) arr.push("" + selectedFiles[i])
            win.openLocalMedia(arr)   // multi-select opens the FIRST; the Next-to-Open tray is Slice 20
        }
    }

    // Vault Add-folder picker (Slice 10). Opening the native folder picker is live now; what the app
    // DOES with the chosen folder — canonicalize · add as a Vault root · scan · shelve — is Slice 11.
    // vaultState is the invisible automation surface (a Lanista replay reads pageOpen / lastAddedFolder).
    Item {
        id: vaultState
        objectName: "vaultState"
        visible: false
        property bool pageOpen: vaultLayer.active
        property string lastAddedFolder: ""
    }

    // Data-vault Slice 3 (2026-08-22): catalogVaultState is the invisible automation surface for
    // CatalogVaultClient's fetch progress + the four catalogs' live ready state. On a dev machine
    // every catalog resolves locally so CatalogVault never fetches and readyCount sits at 4 from
    // boot; on a fresh install fetching flips true and readyCount climbs from 0 to 4 as each db
    // lands. readyCount is wired through Connections (not a plain binding) because ready() is a
    // Q_INVOKABLE method shadowing the Q_PROPERTY of the same name — calling it in a binding
    // expression does NOT register as a live dependency, so a bare `MalCatalog.ready()` read would
    // freeze at its first value and never notice a later readyChanged (see MangaSeries.qml's
    // resolve() for the same house convention: reads call .ready(), reactivity rides Connections).
    Item {
        id: catalogVaultState
        objectName: "catalogVaultState"
        visible: false
        property bool fetching: (typeof CatalogVault !== "undefined") ? CatalogVault.fetching : false
        property string tag: (typeof CatalogVault !== "undefined") ? CatalogVault.currentTag : ""
        property int _readyRev: 0
        readonly property int readyCount: {
            var _rev = catalogVaultState._readyRev
            var n = 0
            if (typeof MalCatalog !== "undefined" && MalCatalog && MalCatalog.ready()) n++
            if (typeof TankobanCatalog !== "undefined" && TankobanCatalog && TankobanCatalog.ready()) n++
            if (typeof ComicsCatalog !== "undefined" && ComicsCatalog && ComicsCatalog.ready()) n++
            if (typeof ImdbCatalog !== "undefined" && ImdbCatalog && ImdbCatalog.ready()) n++
            return n
        }
        Connections {
            target: (typeof MalCatalog !== "undefined") ? MalCatalog : null
            function onReadyChanged() { catalogVaultState._readyRev++ }
        }
        Connections {
            target: (typeof TankobanCatalog !== "undefined") ? TankobanCatalog : null
            function onReadyChanged() { catalogVaultState._readyRev++ }
        }
        Connections {
            target: (typeof ComicsCatalog !== "undefined") ? ComicsCatalog : null
            function onReadyChanged() { catalogVaultState._readyRev++ }
        }
        Connections {
            target: (typeof ImdbCatalog !== "undefined") ? ImdbCatalog : null
            function onReadyChanged() { catalogVaultState._readyRev++ }
        }
    }

    FolderDialog {
        id: vaultFolderDialog
        title: "Add a folder to your Vault"
        onAccepted: {
            vaultState.lastAddedFolder = "" + selectedFolder
            // C++ owns it: add the folder as an unconfirmed root and census it off-thread; the
            // confirmation card rises on scanFinished (Slice 11).
            if (typeof VaultLibrary !== "undefined")
                VaultLibrary.addFolder("" + selectedFolder)
        }
    }

    // App-wide file drop. Sits LOW (z:5) so an open player/reader — and PlayerPage's own subtitle
    // DropArea — claim drops on their surface first; only the general app surface routes a dropped
    // media file here. A dropped FOLDER explains + offers the picker (the folder gesture is Slice 10).
    DropArea {
        id: appFileDrop
        anchors.fill: parent
        z: 5
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            if (!drop.hasUrls) { drop.accepted = false; return }
            var files = []
            var anyFolder = false
            for (var i = 0; i < drop.urls.length; i++) {
                var u = "" + drop.urls[i]
                if (LocalLaunch.isDir(u)) anyFolder = true
                else files.push(u)
            }
            drop.accepted = true
            if (files.length === 0 && anyFolder) { win.showFolderDropExplain(); return }
            win.openLocalMedia(files)
        }
    }

    Shortcut {
        sequences: ["Ctrl+O"]
        context: Qt.ApplicationShortcut
        onActivated: openMediaDialog.open()
    }

    // Vault ux uplift S15: a global shortcut opens the Vault from anywhere, via the same
    // door function the taskbar folder door uses (the layer-priority chain is respected —
    // openVaultPage already deactivates the other taskbar layers; Escape's law closes it).
    Shortcut {
        sequences: ["Ctrl+Shift+V"]
        context: Qt.ApplicationShortcut
        onActivated: win.openVaultPage()
    }

    // Arc 41 essentials: direct doors for utility pages.
    Shortcut {
        sequences: ["Ctrl+Shift+D"]
        context: Qt.ApplicationShortcut
        onActivated: win.openDownloadsPage()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+E"]
        context: Qt.ApplicationShortcut
        onActivated: win.openExtensionsPage()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+S"]
        context: Qt.ApplicationShortcut
        onActivated: win.openSettingsPage()
    }

    // ── Open Recent panel (Slice 9): the Open Media control remembers ──
    // A same-window popup (so the bridge can see it) that pops up above the taskbar dock near the
    // Open Media control. Lists recently opened local files for one-click reopen; a dead file is
    // shown dimmed and offers nothing; Clear wipes the shortcuts (never reading progress).
    // Click-away MouseArea below (z:905) closes it; the panel (z:906) sits above it.
    MouseArea {
        anchors.fill: parent
        z: 905
        visible: openRecentPanel.open
        onClicked: openRecentPanel.open = false
    }
    OpenRecentPanel {
        id: openRecentPanel
        objectName: "openRecentPanel"
        z: 906
        visible: opacity > 0.01
        opacity: 0
        property bool open: false
        x: (Math.max(18, Math.min(80, parent.width * 0.045))) + 58
        // hug the dock (Windows-taskbar-preview feel): the closed dock top is a deterministic
        // parent.height - (closedSize 64 + bottomGap 16) = parent.height - 80, so sit the panel
        // bottom ~2px above it (Hemanth 2026-08-09; tunable ±6px on eyes-on).
        y: parent.height - height - 82
        Behavior on opacity { NumberAnimation { duration: 140 } }

        function refresh() { model = (typeof LocalLaunch !== "undefined") ? LocalLaunch.recentItems() : [] }
        function toggle() { open = !open }
        onOpenChanged: { if (open) refresh(); opacity = open ? 1 : 0 }
        Component.onCompleted: refresh()
        Connections {
            target: (typeof LocalLaunch !== "undefined") ? LocalLaunch : null
            function onRecentChanged() { openRecentPanel.refresh() }
        }
        onReopenRequested: (entry) => { win.reopenRecent(entry); openRecentPanel.open = false }
        onClearRequested: if (typeof LocalLaunch !== "undefined") LocalLaunch.clearRecent()
    }

    // Slice 20: explicit multi-file queue. The first selected file is dispatched immediately;
    // these rows never auto-advance and are not part of Open Recent until opened here.
    NextToOpenTray {
        id: nextToOpenTray
        objectName: "nextToOpenTray"
        z: 907
        visible: stagedCount > 0
        x: parent.width - width - 42
        y: parent.height - height - 92
        model: (typeof LocalLaunch !== "undefined") ? LocalLaunch.nextToOpenItems() : []
        function refresh() {
            model = (typeof LocalLaunch !== "undefined") ? LocalLaunch.nextToOpenItems() : []
        }
        Connections {
            target: (typeof LocalLaunch !== "undefined") ? LocalLaunch : null
            function onNextToOpenChanged() { nextToOpenTray.refresh() }
        }
        onOpenRequested: (index, entry) => win.openNextToOpen(index)
        onRemoveRequested: (index, entry) => win.removeNextToOpen(index)
    }

    Connections {
        target: (typeof LocalLaunch !== "undefined") ? LocalLaunch : null
        function onOpenReady(result) { win.dispatchLocalRoute(result) }
        function onRouteInfoReady(result) { win.dispatchLocalRoute(result) }
        function onOpenNextToOpenReady(result) { win.dispatchLocalRoute(result) }
    }

    // Slice 21: launch sessions use the same seedable ceremony component as VaultPage.
    VaultIdentityCeremonyDialog {
        id: identityCeremonyDialog
        anchors.centerIn: parent
        z: 960
        onChoiceMade: (relationship, choice) => win.decidePendingIdentity(choice)
        onCancelRequested: win.cancelPendingIdentityCeremony()
    }

    // A quiet, no-color status pill for local-open feedback: a categorized rejection, or the
    // folder-drop explain with a "Select Media Files…" action. Grays/white only (house rule).
    Rectangle {
        id: localLaunchToast
        objectName: "localLaunchToast"
        z: 950
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - 96
        width: Math.min(parent.width - 96, toastRow.implicitWidth + 40)
        height: 52
        radius: 12
        color: Qt.rgba(0.05, 0.05, 0.07, 0.95)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.16)
        opacity: 0
        visible: opacity > 0.01
        property string message: ""
        property bool showPick: false
        Behavior on opacity { NumberAnimation { duration: 180 } }
        function flash(msg, withPick) {
            message = msg; showPick = !!withPick; opacity = 1; toastTimer.restart()
        }
        Timer { id: toastTimer; interval: 4600; onTriggered: localLaunchToast.opacity = 0 }
        Row {
            id: toastRow
            anchors.centerIn: parent
            spacing: 18
            Text {
                text: localLaunchToast.message
                color: "#e9e9ec"; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: localLaunchToast.showPick
                text: "Select Media Files…"
                color: "#ffffff"; font.pixelSize: 14; font.underline: true
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { localLaunchToast.opacity = 0; openMediaDialog.open() }
                }
            }
        }
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
    Connections {
        target: boot
        function onVisibleChanged() {
            if (!boot.visible) win.armStartupIdleWork()
        }
    }

    // Account identity flyout, dropped from the topbar medallion.
    AccountCenter {
        id: accountCenter
        objectName: "accountCenter"
        controller: typeof AccountController !== "undefined" ? AccountController : null
        recoveryPresenter: typeof AccountRecoveryPresenter !== "undefined" ? AccountRecoveryPresenter : null
        initial: {
            const who = (typeof AccountController !== "undefined" && AccountController)
                        ? AccountController.username : "";
            return who.length > 0 ? who.charAt(0).toUpperCase() : "?";
        }
    }

    AccountFlyout {
        id: accountFlyout
        objectName: "accountFlyout"
        controller: typeof AccountController !== "undefined" ? AccountController : null
        initial: {
            const who = (typeof AccountController !== "undefined" && AccountController)
                        ? AccountController.username : "";
            return who.length > 0 ? who.charAt(0).toUpperCase() : "?";
        }
    }

    // Account onboarding + session surfaces (Bundle 8C adoption, 2026-08-16).
    // The host is invisible unless an account flow is actually live (signed-out
    // gate after boot, device approval, recovery, sign-out warning). Controller
    // and recovery presenter are context properties owned by AccountRuntime.
    AccountOnboardingHost {
        id: accountHost
        objectName: "accountHost"
        anchors.fill: parent
        z: 900   // above all chrome; below the boot splash (1000)
        topInset: 0   // full-bleed cover: no app chrome peeks above the flow
        controller: typeof AccountController !== "undefined" ? AccountController : null
        recoveryPresenter: typeof AccountRecoveryPresenter !== "undefined" ? AccountRecoveryPresenter : null
        backdrop: wall
    }

    // One shell-wide cover turns the full-monitor geometry jump into a deliberate beat.
    // Every entry point (F11, global chrome, player, book, comic) reaches this same gate.
    FullscreenTransitionShield {
        id: fullscreenTransition
        anchors.fill: parent
        onApplyRequested: WindowMode.toggleShellMode(win)
    }
}
