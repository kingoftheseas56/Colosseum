// TankobanWorld — the REAL instantiation of the world-page template for the Tankoban mode
// (comics + manga / sequential art). Owner: A1. Data lives in Catalog.js (one source, also used by
// the boot prefetch). Ported from our shipped Tankoban Electron catalog (manga = AniList/WeebCentral
// · comics = RCO "rcostation"); live sources come LATER (Hemanth: "apis can come later").
//
// The board (Hemanth-locked 2026-06-25) — personal surfaces BLENDED, discovery surfaces SPLIT:
//   1. Featured (blended) · 2. Continue (blended) · 3. Top in Tankoban — Manga
//   4. Top in Tankoban — Comics (curated) · 5. Explore Genre — Manga
//   6. Explore Comics (canonical curated genre taxonomy, inline)
// The catalogue's needs override the doctrine's ~2-row cap: comics and manga are two real
// sub-catalogues, so the split IS the need (not a lazy row-wall).

import QtQuick
import "Catalog.js" as Catalog
import "ComicsDb.js" as ComicsDb
import "NextUp.js" as NextUp

WorldPage {
    id: tanko
    objectName: "tankobanWorld"
    medium: "Tankoban"

    // Bubbles a tap on a Your Collection tile (from either tab) up to Main's openCollectionEntry door.
    signal collectionOpenRequested(var entry)

    // Next Up (spec 2026-07-18, Jellyfin library inheritance): resume INTO the next
    // chapter/volume through the same session door Continue uses (openComicSession).
    signal nextUpReadRequested(string title, string seriesId, string unitId, string entryKind)

    // "You finished the last one — here's the next one." MANGA ONLY (chapters +
    // tankoban volumes) — western comics excluded by ruling: that catalogue isn't
    // linear. Derivations live in NextUp.js; this only gathers what's on disk.
    function nextUpRows() {
        var fin = NextUp.finishedReads(Progress.recent("manga", 24),
                                       Progress.recent("tankoban", 24))
        var out = []
        for (var i = 0; i < fin.length && out.length < 12; i++) {
            var e = fin[i]
            if (e.kind === "comic") continue
            var next = null, downloaded = false
            if (e.kind === "tankoban") {
                if (typeof TankobanVolumes === "undefined") continue
                var vols = (TankobanVolumes.volumesForSeries(String(e.id)) || []).map(function(v) {
                    return { "id": v.id, "label": "Vol. " + v.number, "number": v.number,
                             "ready": v.state === "ready" }
                })
                next = NextUp.nextUnit(e.sub, vols.filter(function(v) { return v.ready }))
                downloaded = next !== null
                if (!next) next = NextUp.nextUnit(e.sub, vols)   // exists, not downloaded → go-get card
            } else {
                var have = (typeof Downloads !== "undefined") ? Downloads.downloadedChapters() : []
                var mine = have.filter(function(c) { return String(c.seriesId) === String(e.id) })
                next = NextUp.nextUnit(e.sub, mine)
                downloaded = next !== null
                // nothing on disk past the finished chapter: the world can't know the next
                // label without a scrape — honest generic card, routed to the series page.
                if (!next && !isNaN(NextUp.unitNumber(e.sub)))
                    next = { "id": "", "label": "Next chapter", "number": NaN }
            }
            if (!next) continue
            out.push(NextUp.mangaCard(e, next, downloaded))
        }
        return out
    }

    // Comics = GetComics for BOTH metadata and content (Hemanth 2026-07-12; the LOCG
    // catalogue-brain is PARKED in-tree). A comic tile opens the GetComics shelf
    // (ComicSeries resolves the tag from the title, slug-first); an explore box opens
    // the archive index (the middle layer, ratified 2026-07-04 — never raw feeds).
    signal westernRequested(string title)
    signal westernExploreRequested(var box)
    // Comics brain (2026-07-13): the Top-Comics row now reads the weekly-built comics_db.json
    // (RCO-ranked, LOCG-resolved) — a tile carries its LOCG id, so it opens the series directly
    // via openComicSeries (Main wires comicSeriesRequested→openComicSeries). Falls back to the
    // curated westernRequested path when the sidecar isn't loaded.
    signal comicSeriesRequested(var d)
    // Catalogue shelf rows (browse-landing, 2026-07-18): Most Stocked / publisher / decade /
    // deep-shelf / fan-made tiles all carry a gcd id and open the run page directly.
    signal gcdSeriesRequested(var d)
    // Discover card carries a MAL id (Slice C): open the series page with the id so it can
    // fetch our MAL-keyed volume record directly instead of falling back to a title search.
    signal mangaOpenById(string title, string malId)

    // GetComics' own taxonomy (top tags by release count, publishers + franchises,
    // noise-filtered) drives the explore mosaic inline — the old Archives-door page
    // one click earlier. Covers land in a second callback (iTunes art trailing in).
    property var comicBoxes: []
    property var comicCovers: []            // real covers → the mosaic's art pool
    property var comicRows: []              // populated when this lazy world is first created
    property var comicShelves: []           // catalogue shelf rows (browse-landing): [{label, rows}]
    property bool _catalogueInitialized: false
    // small palette so coverless genre tiles aren't all one flat color
    readonly property var _genrePalette: [
        ["#3f5a78","#16222e"], ["#78503f","#2e1c16"], ["#5a3f78","#241630"],
        ["#3f785a","#16281e"], ["#78703f","#2e2a16"], ["#783f5a","#301624"],
        ["#3f6478","#16242e"], ["#785a3f","#2e2216"]
    ]
    function initializeComicCatalogue() {
        if (tanko._catalogueInitialized) return
        tanko._catalogueInitialized = true
        // The curated catalogue rides the ComicsCatalog engine now (P4 seam, 2026-07-18) —
        // behind Main's keep-alive world Loader, same as the old gen.js import was. Root
        // startup never touches this; the multi-megabyte gen.js parse is gone.
        var catalogOk = ComicsDb.setEngine(typeof ComicsCatalog !== "undefined" ? ComicsCatalog : null)
        tanko.comicRows = catalogOk ? ComicsDb.rankedSeries() : Catalog.topComics
        if (catalogOk) console.log("ComicsDb: engine live, " + tanko.comicRows.length + " series")
        else console.warn("ComicsDb: catalogue engine unavailable — using curated fallback")
        // Catalogue shelf rows (browse-landing, ratified lineup): each row computes its
        // model once from ComicsCatalog.shelf(kind, arg, 24); rows with no rows just don't
        // render (Repeater below skips empty entries via a length check on the delegate).
        if (typeof ComicsCatalog !== "undefined" && ComicsCatalog.ready()) {
            var shelfSpecs = [
                { label: "Most Stocked", kind: "stocked", arg: "" },
                { label: "Marvel", kind: "publisher", arg: "Marvel" },
                { label: "DC", kind: "publisher", arg: "DC" },
                { label: "Image", kind: "publisher", arg: "Image" },
                { label: "The 2020s", kind: "decade", arg: "2020" },
                { label: "The 2010s", kind: "decade", arg: "2010" },
                { label: "Deep Shelves", kind: "deep", arg: "" },
                { label: "Fan-Made Shelf", kind: "fanmade", arg: "" }
            ]
            tanko.comicShelves = shelfSpecs.map(function(spec) {
                var rows = ComicsCatalog.shelf(spec.kind, spec.arg, 24) || []
                return {
                    label: spec.label,
                    kind: spec.kind,            // carried so the tab can build a Discover pin
                    arg: spec.arg,              //   (publisher arg = stable key; stocked = own catalogue)
                    rows: rows.map(function(r) {
                        return { caption: r.title + (r.year ? " (" + r.year + ")" : ""),
                                 cover: r.cover || "", gcdId: r.gcdId, title: r.title }
                    })
                }
            }).filter(function(s) { return s.rows.length > 0 })
            var arc21 = [
                {label:"Recently Available", catalogId:"recently-available", group:"", key:""},
                {label:"Complete Runs", catalogId:"complete-runs", group:"", key:""},
                {label:"Near Complete", catalogId:"near-complete", group:"", key:""},
                {label:"Omnibuses", catalogId:"popular", group:"format", key:"omnibus"},
                {label:"Deluxe Editions", catalogId:"popular", group:"format", key:"deluxe"},
                {label:"Graphic Novels", catalogId:"popular", group:"format", key:"graphic novel"},
                {label:"Community Collections", catalogId:"community-collections", group:"", key:""}
            ].map(function(spec) {
                var page=ComicsCatalog.discoverPage(spec.catalogId,spec.group,spec.key,false,0,24)||({})
                return {label:spec.label,catalogId:spec.catalogId,filterGroup:spec.group,filterKey:spec.key,
                    rows:(page.items||[]).map(function(r){return {caption:r.title+(r.year?" ("+r.year+")":""),cover:r.cover||"",locgId:"locg:"+r.locgId,title:r.title}})}
            }).filter(function(s){return s.rows.length>0})
            tanko.comicShelves = arc21.concat(tanko.comicShelves)
        } else {
            tanko.comicShelves = []
        }
        if (catalogOk) {
            var genres = ComicsDb.genreShelves(8)
            tanko.comicBoxes = genres.map(function(g, i) {
                var pal = tanko._genrePalette[i % tanko._genrePalette.length]
                return { name: g.name, count: g.count, c1: pal[0], c2: pal[1] }
            })
            var genreCovers = []
            genres.forEach(function(g) { (g.covers || []).forEach(function(c) { if (c) genreCovers.push(c) }) })
            tanko.comicCovers = genreCovers
        } else {
            tanko.comicBoxes = []
            tanko.comicCovers = []
        }
    }
    Component.onCompleted: if (tanko.lifecycleActive) tanko.initializeComicCatalogue()
    onLifecycleActiveChanged: if (tanko.lifecycleActive) tanko.initializeComicCatalogue()

    property string activeTab: "discover"

    FeaturedCarousel {
        kicker: "Featured in Tankoban"
        primaryLabel: "Read"; secondaryLabel: "Details"
        slides: Catalog.featured
    }

    // Next Up sits ABOVE Continue (Jellyfin's order — the freshest intent first).
    // A card exists only when the series' LATEST read is finished; a half-read
    // chapter keeps the series in Continue instead (the shared rule, NextUp.js).
    ContinueRow {
        title: "Next Up"
        // A retained hidden world must not walk Progress/Downloads while its Loader is warming.
        // Re-evaluating this binding on lifecycle activation preserves the existing row and doors.
        items: tanko.lifecycleActive ? (Progress.revision, tanko.nextUpRows()) : []
        onResumeRequested: (item) => {
            if (item.resume.downloaded && item.resume.unitId.length)
                tanko.nextUpReadRequested(item.title, item.id,
                                          item.resume.unitId,
                                          item.kind === "tankoban" ? "tankoban" : "")
            else
                tanko.seriesRequested(item.title)   // not on disk → series page, go download it
        }
        onDetailRequested: (item) => tanko.seriesRequested(item.title)
        onSeeAllRequested: tanko.continueSeeAllRequested()
    }

    ContinueRow {
        title: "Continue Reading"
        // Real resume data — manga + tankoban + comics BLENDED by true recency and capped like
        // every other row. A retained hidden world must not walk Progress while its Loader is
        // warming; the current-master lifecycle gate remains in force. Personal rows blend all
        // three kinds, while the browse tabs split only catalogue presentation. Progress.revision
        // keeps the projection live.
        items: tanko.lifecycleActive ? (Progress.revision, (function() {
            var a = Progress.recent("manga", 12)
                .concat(Progress.recent("tankoban", 12))
                .concat(Progress.recent("comic", 12))
            a.sort(function(x, y) { return (y.updatedAt || 0) - (x.updatedAt || 0) })
            // Tag the comic source so blended tiles read GetComics on the badge; manga untagged.
            return a.slice(0, 12).map(function(e) {
                var src = (e.kind === "comic") ? "GetComics" : ""
                if (src.length) e.source = src
                return e
            })
        })()) : []
        onResumeRequested: (item) => tanko.continueResumeRequested(item)
        onDetailRequested: (item) => tanko.continueDetailRequested(item)
        onSeeAllRequested: tanko.continueSeeAllRequested()
    }

    // Discover | Manga | Comics | Library — four tabs (mirrors Theatre's discover-first
    // order, which also carries a trailing Library tab). Discover is first and default;
    // only the browse rows split into Manga/Comics halves. Featured/Next Up/Continue above
    // stay blended across all tabs. Library consolidates every saved manga/comic series
    // (Brotherhood#1) — TB-001 slice: mixed wall + Details routing only.
    WorldTabBar {
        objectName: "tankobanTabBar"
        tabPrefix: "tankobanTab"
        backdrop: tanko.backdrop
        currentTab: tanko.activeTab
        tabModel: [ { key: "discover", label: "Discover" },
                    { key: "manga", label: "Manga" },
                    { key: "comics", label: "Comics" },
                    { key: "library", label: "Library" } ]
        onTabRequested: (tab) => tanko.activeTab = tab
    }

    // ── Discover: the shared Discover shell, retained (NOT Loader-swapped) so its per-type
    //    in-session state (catalogue, filter, items, scroll) survives tab switches. Built
    //    once; hidden when another tab is active. Theatre mounts its DiscoverPage the same
    //    way. A normalized card routes to the EXISTING series doors by type — manga →
    //    seriesRequested(title), comics → comicSeriesRequested(item). No download action. ──
    TankobanDiscoverPage {
        id: discoverPage
        visible: tanko.activeTab === "discover"
        width: parent.width
        height: visible ? Math.max(620, tanko.height - 200) : 0
        active: tanko.lifecycleActive && visible
        malCatalog: (typeof MalCatalog !== "undefined") ? MalCatalog : null
        comicsCatalog: (typeof ComicsCatalog !== "undefined") ? ComicsCatalog : null
        extensions: (typeof Extensions !== "undefined") ? Extensions.installed() : []
        showExplicitContent: tanko.showExplicitContent
        // Manga card → the existing manga series door (title-only route, already wired in Main).
        onMangaSeriesRequested: function(item) { tanko.mangaOpenById((item && item.title) || "", (item && item.id) || "") }
        // Comics card → the existing LOCG comic-series door (the normalized item carries the
        // locg id under raw.locgId, which openComicSeries reads via d.id).
        onComicSeriesRequested: function(item) {
            var raw = (item && item.raw) || item || ({})
            tanko.comicSeriesRequested({ id: raw.locgId || raw.locg_id || item.id || "",
                                          title: (item && item.title) || "",
                                          cover: (item && item.cover) || "",
                                          locgMeta: raw })
        }
    }

    // ── Library: retained (NOT Loader-swapped) so its scroll position survives tab
    //    switches, same reasoning as Discover above and the same fixed-viewport-height +
    //    self-scrolling GridView pattern Theatre's own LibraryPage already ships. A card
    //    tap always opens Details for now (TB-001; no progress means no row can be
    //    "started" yet) via the existing collectionOpenRequested door. ──
    TankobanLibraryTab {
        id: libraryPage
        visible: tanko.activeTab === "library"
        active: tanko.lifecycleActive && visible
        width: parent.width
        height: visible ? Math.max(620, tanko.height - 200) : 0
        onDetailRequested: function(entry) { tanko.collectionOpenRequested(entry) }
        // TB-002: a started row's tap emits the row's Progress record up through the
        // existing continueResumeRequested door (already wired in Main.qml to route a
        // kind:"manga" record into the reader). No Main.qml edit — just this signal hop.
        onResumeRequested: function(record) { tanko.continueResumeRequested(record) }
        // TB-005: the ⋮ menu's Remove drops the row's entry from the Collection. The
        // String() cast mirrors TheatreWorld's onRemoveRequested and matches the
        // CollectionStore QString id param.
        onRemoveRequested: function(entry) { if (typeof Collection !== "undefined") Collection.remove("tankoban", String(entry.id)) }
    }

    // The active half's browse rows. Loader-swapped so only the shown half is built; the
    // comics DATA is owned above (computed once in Component.onCompleted) and bound in
    // reactively, so switching tabs never re-runs GcApi.explore / the shelf compute.
    Loader {
        id: tabContent
        width: parent ? parent.width : 0
        height: item ? item.implicitHeight : 0
        source: tanko.activeTab === "comics" ? "TankobanComicsTab.qml"
              : tanko.activeTab === "manga" ? "TankobanMangaTab.qml"
              : ""
        active: tanko.lifecycleActive && (tanko.activeTab === "manga" || tanko.activeTab === "comics")
        onLoaded: {
            if (item.collectionOpenRequested) item.collectionOpenRequested.connect(tanko.collectionOpenRequested)
            // Task 8: a See-all pin from either browse tab routes into the in-tab Discover
            // wall — switch to Discover and apply the pin (the adapter validates/drops a
            // stale filter). Both tabs declare discoverPinRequested(var pin).
            if (item.discoverPinRequested) item.discoverPinRequested.connect(tanko.openDiscoverPin)
            if (tanko.activeTab === "comics") {
                item.comicRows   = Qt.binding(function() { return tanko.comicRows })
                item.comicShelves = Qt.binding(function() { return tanko.comicShelves })
                item.comicBoxes  = Qt.binding(function() { return tanko.comicBoxes })
                item.comicCovers = Qt.binding(function() { return tanko.comicCovers })
                item.gcdSeriesRequested.connect(tanko.gcdSeriesRequested)
                item.comicSeriesRequested.connect(tanko.comicSeriesRequested)
                item.westernRequested.connect(tanko.westernRequested)
                item.westernExploreRequested.connect(tanko.westernExploreRequested)
            } else {
                item.seriesRequested.connect(tanko.seriesRequested)
                item.genreRequested.connect(tanko.genreRequested)
                item.genreIndexRequested.connect(tanko.genreIndexRequested)
            }
        }
    }

    // Task 8: route a See-all pin from a Manga/Comics shelf into the in-tab Discover wall.
    // Switches to Discover and applies the pin; the adapter's resolvePin validates every
    // key against the live facets and drops a stale filter while keeping the valid type/
    // catalogue (spec 3.6). The Discover shell resets paging and scrolls to the wall start.
    function openDiscoverPin(pin) {
        activeTab = "discover"
        discoverPage.applyPin(pin)
    }
}
