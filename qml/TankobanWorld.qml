// TankobanWorld — the REAL instantiation of the world-page template for the Tankoban mode
// (comics + manga / sequential art). Owner: A1. Data lives in Catalog.js (one source, also used by
// the boot prefetch). Ported from our shipped Tankoban Electron catalog (manga = AniList/WeebCentral
// · comics = RCO "rcostation"); live sources come LATER (Hemanth: "apis can come later").
//
// The board (Hemanth-locked 2026-06-25) — personal surfaces BLENDED, discovery surfaces SPLIT:
//   1. Featured (blended) · 2. Continue (blended) · 3. Top in Tankoban — Manga
//   4. Top in Tankoban — Comics (curated) · 5. Explore Genre — Manga
//   6. Explore Comics (GetComics' own tag taxonomy, inline)
// The catalogue's needs override the doctrine's ~2-row cap: comics and manga are two real
// sub-catalogues, so the split IS the need (not a lazy row-wall).

import QtQuick
import "Catalog.js" as Catalog
import "ComicsApi.js" as GcApi
import "ComicsDb.js" as ComicsDb
import "NextUp.js" as NextUp

WorldPage {
    id: tanko
    medium: "Tankoban"

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

    // GetComics' own taxonomy (top tags by release count, publishers + franchises,
    // noise-filtered) drives the explore mosaic inline — the old Archives-door page
    // one click earlier. Covers land in a second callback (iTunes art trailing in).
    property var comicBoxes: []
    property var comicCovers: []            // real covers → the mosaic's art pool
    property var comicRows: []              // populated when this lazy world is first created
    property var comicShelves: []           // catalogue shelf rows (browse-landing): [{label, rows}]
    // small palette so coverless genre tiles aren't all one flat color
    readonly property var _genrePalette: [
        ["#3f5a78","#16222e"], ["#78503f","#2e1c16"], ["#5a3f78","#241630"],
        ["#3f785a","#16281e"], ["#78703f","#2e2a16"], ["#783f5a","#301624"],
        ["#3f6478","#16242e"], ["#785a3f","#2e2216"]
    ]
    Component.onCompleted: {
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
                    rows: rows.map(function(r) {
                        return { caption: r.title + (r.year ? " (" + r.year + ")" : ""),
                                 cover: r.cover || "", gcdId: r.gcdId, title: r.title }
                    })
                }
            }).filter(function(s) { return s.rows.length > 0 })
        } else {
            tanko.comicShelves = []
        }
        GcApi.explore(function(boxes) {
            tanko.comicBoxes = (boxes || []).map(function(b, i) {
                var pal = tanko._genrePalette[i % tanko._genrePalette.length];
                return { name: b.name, tag: b.tag, tagId: b.tagId, count: b.count,
                         c1: pal[0], c2: pal[1] };
            });
            tanko.comicCovers = (boxes || []).map(function(b) { return b.cover; })
                .filter(function(c) { return c && c.length > 0; });
        });
    }

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
        items: (Progress.revision, tanko.nextUpRows())
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
        // Real resume data — manga + comics BLENDED by true recency and capped like every other
        // row (audit fix: was all-manga-then-all-comics, unbounded). Progress.revision keeps it live.
        items: (Progress.revision, (function() {
            var a = Progress.recent("manga", 12).concat(Progress.recent("comic", 12))
            a.sort(function(x, y) { return (y.updatedAt || 0) - (x.updatedAt || 0) })
            // Tag the comic source so blended tiles read GetComics on the badge; manga untagged.
            return a.slice(0, 12).map(function(e) {
                var src = (e.kind === "comic") ? "GetComics" : ""
                if (src.length) e.source = src
                return e
            })
        })())
        onResumeRequested: (item) => tanko.continueResumeRequested(item)
        onDetailRequested: (item) => tanko.continueDetailRequested(item)
        onSeeAllRequested: tanko.continueSeeAllRequested()
    }

    TrendingTop10 {
        title: "Top in Tankoban — Manga"
        items: Catalog.topManga
        onItemClicked: (i) => tanko.seriesRequested(Catalog.topManga[i].caption)
    }

    TrendingTop10 {
        title: "Top in Tankoban — Comics"
        // DB-driven (2026-07-13): RCO-ranked series from the weekly comics_db.json sidecar; each
        // tile carries its LOCG id and opens the series directly. Falls back to the curated list
        // if the generated catalog did not ingest when this lazy world was created.
        items: tanko.comicRows.slice(0, 10)
        onItemClicked: (i) => {
            var topComics = tanko.comicRows.slice(0, 10)
            var it = topComics[i]
            if (!it) return
            if (it && it.locgId) tanko.comicSeriesRequested({ id: it.locgId, title: it.caption, cover: it.cover })
            else tanko.westernRequested(it.caption)
        }
    }

    // Catalogue shelf rows (browse-landing, ratified lineup 2026-07-18): Most Stocked,
    // publisher shelves, decade shelves, deep-shelf (10+ downloads), fan-made. Each row
    // reuses the same Top-Comics tile pattern; tiles carry a gcd id and open the run page.
    Repeater {
        model: tanko.comicShelves
        delegate: TrendingTop10 {
            required property var modelData
            title: modelData.label
            items: modelData.rows
            visible: modelData.rows.length > 0
            onItemClicked: (i) => {
                var it = modelData.rows[i]
                if (it) tanko.gcdSeriesRequested({ gcd: true, gcdId: it.gcdId, title: it.title, cover: it.cover })
            }
        }
    }

    GenreMosaic {
        title: "Explore by Genre — Manga"
        genres: Catalog.genresManga
        onGenreClicked: (i) => tanko.genreRequested(Catalog.genresManga[i].name)
        onExploreClicked: tanko.genreIndexRequested()
    }

    GenreMosaic {
        title: "Explore Comics"
        // GetComics' OWN taxonomy inline (publishers + franchises, top tags by release
        // count) — the archive-tag axis IS the driving force (Hemanth 2026-07-12).
        // A box opens the archive index: the series archives alive under that tag.
        genres: tanko.comicBoxes
        covers: tanko.comicCovers          // real comic art behind the box gradients
        navigable: false
        onGenreClicked: (i) => tanko.westernExploreRequested(tanko.comicBoxes[i])
    }
}
