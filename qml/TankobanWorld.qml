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
import "comics_db.gen.js" as ComicsDbData

WorldPage {
    id: tanko
    medium: "Tankoban"

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

    // GetComics' own taxonomy (top tags by release count, publishers + franchises,
    // noise-filtered) drives the explore mosaic inline — the old Archives-door page
    // one click earlier. Covers land in a second callback (iTunes art trailing in).
    property var comicBoxes: []
    property var comicCovers: []            // real covers → the mosaic's art pool
    property var comicRows: []              // populated when this lazy world is first created
    // small palette so coverless genre tiles aren't all one flat color
    readonly property var _genrePalette: [
        ["#3f5a78","#16222e"], ["#78503f","#2e1c16"], ["#5a3f78","#241630"],
        ["#3f785a","#16281e"], ["#78703f","#2e2a16"], ["#783f5a","#301624"],
        ["#3f6478","#16242e"], ["#785a3f","#2e2216"]
    ]
    Component.onCompleted: {
        // The generated full catalog is deliberately imported here, behind Main's keep-alive
        // world Loader. Root startup never parses or ingests this multi-megabyte object.
        var catalogOk = ComicsDb.setData(ComicsDbData.data)
        tanko.comicRows = catalogOk ? ComicsDb.rankedSeries() : Catalog.topComics
        if (catalogOk) console.log("ComicsDb: loaded " + tanko.comicRows.length + " series")
        else console.warn("ComicsDb: ingest failed — using curated fallback")
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
        items: tanko.comicRows
        onItemClicked: (i) => {
            var it = tanko.comicRows[i]
            if (it && it.locgId) tanko.comicSeriesRequested({ id: it.locgId, title: it.caption, cover: it.cover })
            else tanko.westernRequested(it.caption)
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
