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
import "ComicsApi.js" as ComicsApi

WorldPage {
    id: tanko
    medium: "Tankoban"

    // western-comics routes (2026-07-04 lane): a comic tile opens the GetComics
    // shelf (ComicSeries resolves the tag from the title); an explore box opens
    // its tag shelf directly. Declared here, not on the shared WorldPage.
    signal westernRequested(string title)
    signal westernExploreRequested(var box)

    // Explore Comics = GetComics' REAL taxonomy (publishers + franchises, live
    // counts) — the genre facade with mock counts died 2026-07-04 (Hemanth's
    // call: "A for sure"; GetComics has no genre axis, and neither does any
    // keyless source — the genre brain is option B, its own spec).
    property var comicExplore: []
    Component.onCompleted: ComicsApi.explore(function(boxes) { tanko.comicExplore = boxes })

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
            return a.slice(0, 12)
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
        items: Catalog.topComics
        onItemClicked: (i) => tanko.westernRequested(Catalog.topComics[i].caption)
    }

    GenreMosaic {
        title: "Explore by Genre — Manga"
        genres: Catalog.genresManga
        onGenreClicked: (i) => tanko.genreRequested(Catalog.genresManga[i].name)
        onExploreClicked: tanko.genreIndexRequested()
    }

    GenreMosaic {
        title: "Explore Comics — Publishers & Franchises"
        genres: tanko.comicExplore
        navigable: false     // no comics index page yet — "Explore ›" would be a dead door
        onGenreClicked: (i) => tanko.westernExploreRequested(tanko.comicExplore[i])
    }
}
