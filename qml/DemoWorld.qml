// DemoWorld — the REFERENCE instantiation of the world-page template (PLACEHOLDER content only).
// Two jobs:
//   1. The navigation target wired from the home right now, so every pill / row header has somewhere
//      to go and the template can be seen working.
//   2. The copy-paste template each mode owner clones (→ ComicsWorld.qml, BooksWorld.qml, …) and
//      fills with REAL data + their own mix of widgets.
//
// The widget TYPES here are the doctrine's lean board: Featured · Continue · Trending · Genre-picker.
// Content rows are capped at TWO (Continue + Top 10); the genre grid varies the board instead of a
// third row. Mode-specific bodies/data are NOT built here — that's the owning agent's job.

import QtQuick

WorldPage {
    id: demo

    // placeholder cover tint, so the board reads as a coherent set without real art
    property string accent1: "#6a4a32"
    property string accent2: "#241813"

    FeaturedCarousel {
        kicker: "Featured in " + demo.medium
        primaryLabel: "Open"; secondaryLabel: "Details"
        slides: [
            { title: "Spotlight One",   blurb: "Placeholder spotlight — the mode owner drops real " + demo.medium.toLowerCase() + " art and copy in here.", ghost: "01", c1: "#2a1a3d", c2: "#120b1a" },
            { title: "Spotlight Two",   blurb: "A second featured slide; the dots at the right switch between them.", ghost: "02", c1: "#13314a", c2: "#0b1119" },
            { title: "Spotlight Three", blurb: "A third placeholder slide for the spotlight carousel.", ghost: "03", c1: "#3d2a16", c2: "#1a120b" }
        ]
    }

    ContinueRow {
        title: "Continue"
        items: [
            { caption: "Placeholder A", c1: demo.accent1, c2: demo.accent2, progress: 0.80 },
            { caption: "Placeholder B", c1: demo.accent1, c2: demo.accent2, progress: 0.30 },
            { caption: "Placeholder C", c1: demo.accent1, c2: demo.accent2, progress: 0.55 },
            { caption: "Placeholder D", c1: demo.accent1, c2: demo.accent2, progress: 0.12 },
            { caption: "Placeholder E", c1: demo.accent1, c2: demo.accent2, progress: 0.66 }
        ]
    }

    TrendingTop10 {
        title: "Top 10 in " + demo.medium
        items: [
            { caption: "Rank One",   c1: demo.accent1, c2: demo.accent2 },
            { caption: "Rank Two",   c1: demo.accent1, c2: demo.accent2 },
            { caption: "Rank Three", c1: demo.accent1, c2: demo.accent2 },
            { caption: "Rank Four",  c1: demo.accent1, c2: demo.accent2 },
            { caption: "Rank Five",  c1: demo.accent1, c2: demo.accent2 }
        ]
    }

    GenreMosaic {
        title: "Browse by Genre"
        genres: [
            { name: "Superhero",     count: 418, c1: "#c9533f", c2: "#5a1e16" },
            { name: "Sci-Fi",        count: 206, c1: "#3f6fc9", c2: "#16285a" },
            { name: "Slice of Life", count: 97,  c1: "#3fae8e", c2: "#16453a" },
            { name: "Horror",        count: 143, c1: "#8a4fc9", c2: "#2c1a5a" },
            { name: "Crime",         count: 88,  c1: "#c99e3f", c2: "#5a4316" },
            { name: "Romance",       count: 64,  c1: "#c93f8a", c2: "#5a1640" },
            { name: "Noir",          count: 41,  c1: "#5a6470", c2: "#23282e" },
            { name: "Fantasy",       count: 177, c1: "#4f9cc9", c2: "#16384a" },
            { name: "Comedy",        count: 72,  c1: "#7a9a3f", c2: "#2e3a16" },
            { name: "Drama",         count: 134, c1: "#c9683f", c2: "#5a2816" }
        ]
    }
}
