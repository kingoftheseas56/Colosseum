// TankobanWorld — the REAL instantiation of the world-page template for the Tankoban mode
// (comics + manga / sequential art). Owner: A1. Data lives in Catalog.js (one source, also used by
// the boot prefetch). Ported from our shipped Tankoban Electron catalog (manga = AniList/WeebCentral
// · comics = RCO "rcostation"); live sources come LATER (Hemanth: "apis can come later").
//
// The board (Hemanth-locked 2026-06-25) — personal surfaces BLENDED, discovery surfaces SPLIT:
//   1. Featured (blended) · 2. Continue (blended) · 3. Top Manga · 4. Top Comics
//   5. Explore Genre — Manga · 6. Explore Genre — Comics
// The catalogue's needs override the doctrine's ~2-row cap: comics and manga are two real
// sub-catalogues, so the split IS the need (not a lazy row-wall).

import QtQuick
import "Catalog.js" as Catalog
import "XoxoApi.js" as Xoxo

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
    signal comicArchiveBoardRequested()

    // Live comics feed from xoxo: Top Comics ← hot-comic; genre boxes ← xoxo's REAL
    // genre axis (superhero/horror/DC/Marvel...) + one "GetComics Archives" door.
    // topComicsXoxo falls back to Catalog.topComics (curated) when xoxo is offline.
    property var topComicsXoxo: []
    property var comicGenresXoxo: []
    // small palette so coverless genre tiles aren't all one flat color
    readonly property var _genrePalette: [
        ["#3f5a78","#16222e"], ["#78503f","#2e1c16"], ["#5a3f78","#241630"],
        ["#3f785a","#16281e"], ["#78703f","#2e2a16"], ["#783f5a","#301624"],
        ["#3f6478","#16242e"], ["#785a3f","#2e2216"]
    ]
    Component.onCompleted: {
        Xoxo.exploreItems("hot-comic", 1, function(r) {
            if (r && r.items.length > 0)
                tanko.topComicsXoxo = r.items.slice(0, 10).map(function(s) {
                    return { caption: s.title, cover: s.cover, c1: "#3f5a78", c2: "#16222e",
                             xoxo: true, id: s.id };
                });
        });
        Xoxo.explore(function(boxes) {
            var genres = boxes.filter(function(b) { return b.kind === "genre"; })
                .map(function(b, i) {
                    var pal = tanko._genrePalette[i % tanko._genrePalette.length];
                    return { name: b.label, boxId: b.id, c1: pal[0], c2: pal[1] };
                });
            genres.push({ name: "GetComics Archives", archives: true, c1: "#4a4a4a", c2: "#151515" });
            tanko.comicGenresXoxo = genres;
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
        title: "Top in Tankoban — Comics"
        // Live from xoxo (its Popular shelf); the curated Catalog list is the offline fallback.
        items: tanko.topComicsXoxo.length > 0 ? tanko.topComicsXoxo : Catalog.topComics
        onItemClicked: (i) => {
            var list = tanko.topComicsXoxo.length > 0 ? tanko.topComicsXoxo : Catalog.topComics
            var it = list[i]
            if (it.xoxo) tanko.xoxoSeriesRequested({ id: it.id, title: it.caption, cover: it.cover })
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
        title: "Explore Comics — Genres"
        // xoxo's REAL genre axis (superhero/horror/DC/Marvel...) + a GetComics Archives
        // door. Replaces the old GetComics publisher/franchise boxes (those live on the
        // Archives board now). GetComics loses nothing (peer-sources spec 2026-07-09).
        genres: tanko.comicGenresXoxo
        navigable: false
        onGenreClicked: (i) => {
            var g = tanko.comicGenresXoxo[i]
            if (g.archives) tanko.comicArchiveBoardRequested()
            else tanko.xoxoGenreRequested({ id: g.boxId, label: g.name })
        }
    }
}
