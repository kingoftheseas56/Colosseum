// TankobanWorld — the REAL instantiation of the world-page template for the Tankoban mode
// (comics + manga / sequential art). Owner: A1. Data lives in Catalog.js (one source, also used by
// the boot prefetch). Ported from our shipped Tankoban Electron catalog (manga = AniList/WeebCentral
// · comics = RCO "rcostation"); live sources come LATER (Hemanth: "apis can come later").
//
// The board (Hemanth-locked 2026-06-25) — personal surfaces BLENDED, discovery surfaces SPLIT:
//   1. Featured (blended) · 2. Continue (blended) · 3. Top Manga
//   4. Top 10 This Week (comics) · 5. Most Popular (comics)
//   6. Explore Genre — Manga · 7. Explore Comics — Publishers
// The catalogue's needs override the doctrine's ~2-row cap: comics and manga are two real
// sub-catalogues, so the split IS the need (not a lazy row-wall).

import QtQuick
import "Catalog.js" as Catalog
import "LocgApi.js" as Locg

WorldPage {
    id: tanko
    medium: "Tankoban"

    // western-comics routes (2026-07-04 lane): a comic tile opens the GetComics
    // shelf (ComicSeries resolves the tag from the title); an explore box opens
    // its tag shelf directly. Declared here, not on the shared WorldPage.
    signal westernRequested(string title)
    signal westernExploreRequested(var box)

    // xoxo comics routes (2026-07-09 peer-sources lane): xoxo is now the primary
    // comics feed. A Top-Comics tile / genre-grid tile opens the xoxo issue list;
    // the Archives box opens the GetComics taxonomy (GetComics loses nothing).
    signal xoxoSeriesRequested(var data)        // {id, title, cover}
    signal xoxoGenreRequested(var box)          // {id, label}
    signal locgPublisherRequested(var box)      // {id, label} — opens LocgPublisherPage
    signal comicArchiveBoardRequested()

    // Live comics CATALOGUE from LOCG (AniList model): Top 10 This Week (pull-ranked),
    // Most Popular, publisher boxes. Falls back to curated Catalog.topComics when LOCG offline.
    property var topComicsWeek: []
    property var popularComics: []
    property var comicPublishers: []
    property var comicCovers: []            // real covers → the mosaic's art pool
    // small palette so coverless genre tiles aren't all one flat color
    readonly property var _genrePalette: [
        ["#3f5a78","#16222e"], ["#78503f","#2e1c16"], ["#5a3f78","#241630"],
        ["#3f785a","#16281e"], ["#78703f","#2e2a16"], ["#783f5a","#301624"],
        ["#3f6478","#16242e"], ["#785a3f","#2e2216"]
    ]
    Component.onCompleted: {
        Locg.top10ThisWeek(function(list, meta) {
            if (meta && meta.ok && list.length > 0) {
                tanko.topComicsWeek = list.map(function(s) {
                    return { caption: s.title, cover: s.cover, c1: "#3f5a78", c2: "#16222e",
                             locg: true, id: s.id, locgMeta: { publisher: s.publisher, rating: s.rating } };
                });
            }
        });
        Locg.popular(function(list, meta) {
            if (!meta || !meta.ok || list.length === 0) return;
            tanko.popularComics = list.slice(0, 20).map(function(s) {
                return { caption: s.title, cover: s.cover, c1: "#3f5a78", c2: "#16222e",
                         locg: true, id: s.id, locgMeta: { publisher: s.publisher, startYear: s.startYear } };
            });
            tanko.comicCovers = list.map(function(s) { return s.cover; })
                .filter(function(c) { return c && c.length > 0; });
        });
        Locg.publisherBoxes(function(boxes) {
            var pubs = boxes.map(function(b, i) {
                var pal = tanko._genrePalette[i % tanko._genrePalette.length];
                return { name: b.label, boxId: b.id, c1: pal[0], c2: pal[1] };
            });
            pubs.push({ name: "GetComics Archives", archives: true, c1: "#4a4a4a", c2: "#151515" });
            tanko.comicPublishers = pubs;
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
            // Tag the comic source so blended tiles read XOXO vs GetComics (peer-sources
            // spec 2026-07-09): xoxo ids are "xoxo:…", GetComics are "gc:…". Manga untagged.
            return a.slice(0, 12).map(function(e) {
                var id = String(e.id || e.seriesId || "")
                var src = id.indexOf("xoxo:") === 0 ? "XOXO"
                        : (e.kind === "comic" ? "GetComics" : "")
                if (src.length) e.source = src
                return e
            })
        })())
        onResumeRequested: (item) => tanko.continueResumeRequested(item)
        onDetailRequested: (item) => tanko.continueDetailRequested(item)
    }

    TrendingTop10 {
        title: "Top in Tankoban — Manga"
        items: Catalog.topManga
        onItemClicked: (i) => tanko.seriesRequested(Catalog.topManga[i].caption)
    }

    TrendingTop10 {
        title: "Top 10 This Week"
        // Live from LOCG (releases ranked by real pull-counts); curated Catalog list is the offline fallback.
        items: tanko.topComicsWeek.length > 0 ? tanko.topComicsWeek : Catalog.topComics
        onItemClicked: (i) => {
            var list = tanko.topComicsWeek.length > 0 ? tanko.topComicsWeek : Catalog.topComics
            var it = list[i]
            if (it.locg) tanko.xoxoSeriesRequested({ id: it.id, title: it.caption, cover: it.cover, locgMeta: it.locgMeta })
            else tanko.westernRequested(it.caption)
        }
    }

    TrendingTop10 {
        title: "Most Popular"
        // Live from LOCG (most-popular series). Empty when LOCG offline → row shows nothing.
        items: tanko.popularComics
        onItemClicked: (i) => {
            var it = tanko.popularComics[i]
            if (it.locg) tanko.xoxoSeriesRequested({ id: it.id, title: it.caption, cover: it.cover, locgMeta: it.locgMeta })
        }
    }

    GenreMosaic {
        title: "Explore by Genre — Manga"
        genres: Catalog.genresManga
        onGenreClicked: (i) => tanko.genreRequested(Catalog.genresManga[i].name)
        onExploreClicked: tanko.genreIndexRequested()
    }

    GenreMosaic {
        title: "Explore Comics — Publishers"
        // LOCG's publisher axis (Marvel/DC/Image/Dark Horse...) + a GetComics Archives
        // door. Publisher is the comics-native axis (genre dropped — LOCG carries no
        // keyless genre, ratified 2026-07-09). GetComics loses nothing.
        genres: tanko.comicPublishers
        covers: tanko.comicCovers          // real comic art behind the publisher gradients
        navigable: false
        onGenreClicked: (i) => {
            var g = tanko.comicPublishers[i]
            if (g.archives) tanko.comicArchiveBoardRequested()
            else tanko.locgPublisherRequested({ id: g.boxId, label: g.name })
        }
    }
}
