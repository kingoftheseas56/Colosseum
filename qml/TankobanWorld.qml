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

WorldPage {
    id: tanko
    medium: "Tankoban"

    // western-comics routes (2026-07-04 lane): a comic tile opens the GetComics
    // shelf (ComicSeries resolves the tag from the title), a genre tile opens the
    // curated western genre page. Declared here, not on the shared WorldPage.
    signal westernRequested(string title)
    signal comicGenreRequested(string genreName)

    FeaturedCarousel {
        kicker: "Featured in Tankoban"
        primaryLabel: "Read"; secondaryLabel: "Details"
        slides: Catalog.featured
    }

    ContinueRow {
        title: "Continue"
        // Real resume data — manga + comics, newest first. (Progress.revision keeps it live.)
        items: (Progress.revision, Progress.recent("manga").concat(Progress.recent("comic")))
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
        title: "Explore by Genre — Comics"
        genres: Catalog.genresComics
        navigable: false     // no comics genre INDEX yet — tiles are live, "Explore ›" would be a dead door
        onGenreClicked: (i) => tanko.comicGenreRequested(Catalog.genresComics[i].name)
    }
}
