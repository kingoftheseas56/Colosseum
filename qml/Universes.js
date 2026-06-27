// Universes.js — the UNIVERSE COLLECTION: the curated set of multi-medium IPs that feeds the home's
// universe carousel AND (next) each universe page. Real banner key-art (TMDB backdrops / AniList
// banner / Wikimedia), disk-cached like every remote image via the native launcher. Curated now;
// the Wikidata + TMDB/AniList hydration engine is the parked upgrade. `chips` = the mediums present
// (icon ∈ books|movies|manga|comics|music).
//
// Home / universe domain (Agent 5) — kept OUT of the shared, contended Catalog.js on purpose.
.pragma library

// The home carousel shows ONLY universes with a BUILT page template (One Piece = anime,
// Marvel = cinematic). The rest are kept below in `placeholders` — curated, but not surfaced
// until their own template exists, so the carousel never advertises a page that isn't real.
var universes = [
    { name: "One Piece", c1: "#1d121b", category: "anime",
      blurb: "Luffy's voyage for the Grand Line — the manga, the anime, and the films, in one place.",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg",
      continueLabel: "Continue — Ch. 1090",
      chips: [ { t: "8 Manga", ic: "manga" }, { t: "2 Anime", ic: "movies" }, { t: "15 Films", ic: "movies" } ] },
    { name: "Marvel", c1: "#1a2436", category: "cinematic",
      blurb: "The Marvel Cinematic Universe — decades of films and shows, grown from the comics.",
      banner: "https://image.tmdb.org/t/p/w1280/gHLs7Fy3DzLmLsD4lmfqL55KGcl.jpg",
      continueLabel: "Continue — Loki S2",
      chips: [ { t: "34 Films", ic: "movies" }, { t: "12 Shows", ic: "movies" }, { t: "Comics", ic: "comics" } ] }
];

// Parked — curated, NOT a built template yet. Move an entry up into `universes` (with a `category`)
// once its page template exists.
var placeholders = [
    { name: "Dune", c1: "#3a2a18",
      blurb: "Frank Herbert's world, end to end — the novels, the films, the graphic novel.",
      banner: "https://image.tmdb.org/t/p/w1280/jYEW5xZkZk2WTrdbMGAPFuBqbDc.jpg",
      continueLabel: "Continue — Part Two",
      chips: [ { t: "6 Novels", ic: "books" }, { t: "2 Films", ic: "movies" }, { t: "Graphic Novel", ic: "comics" } ] },
    { name: "Game of Thrones", c1: "#241a14",
      blurb: "A Song of Ice and Fire — Martin's saga and the shows it became, across Westeros.",
      banner: "https://image.tmdb.org/t/p/w1280/suopoADq0k8YZr4dQXcU6pToj6s.jpg",
      continueLabel: "Continue — S8 E03",
      chips: [ { t: "5 Novels", ic: "books" }, { t: "2 Shows", ic: "movies" } ] },
    { name: "Lord of the Rings", c1: "#1c2414",
      blurb: "Tolkien's Middle-earth — the novels, Jackson's films, and the age of Rings of Power.",
      banner: "https://upload.wikimedia.org/wikipedia/commons/thumb/8/89/Hobbit_holes_reflected_in_water.jpg/1280px-Hobbit_holes_reflected_in_water.jpg",
      continueLabel: "Continue — The Two Towers",
      chips: [ { t: "3 Novels", ic: "books" }, { t: "6 Films", ic: "movies" }, { t: "Show", ic: "movies" } ] },
    { name: "Star Wars", c1: "#14181c",
      blurb: "A galaxy far, far away — the saga films, the live-action and animated shows, the novels.",
      banner: "https://image.tmdb.org/t/p/w1280/9RFS4KceFxAZX7w0m1Jr2euk8ds.jpg",
      continueLabel: "Continue — Andor",
      chips: [ { t: "12 Films", ic: "movies" }, { t: "10+ Shows", ic: "movies" }, { t: "Novels", ic: "books" } ] }
];

// which page TEMPLATE a universe opens into. Only the two built exemplars are tagged
// ("anime" → UniversePage, "cinematic" → CinematicPage); the rest default to the anime
// template until their own per-category template is built.
function categoryFor(name) {
    for (var i = 0; i < universes.length; i++)
        if (universes[i].name === name) return universes[i].category || "anime";
    return "anime";
}
