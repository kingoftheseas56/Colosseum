// Universes.js — the UNIVERSE COLLECTION: the curated set of multi-medium IPs that feeds the home's
// universe carousel AND each universe page. Real banner key-art, disk-cached like every remote image
// via the native launcher — every banner rides an IPv4-PINNED host (live.metahub.space backgrounds /
// s4.anilist.co banners / upload.wikimedia.org), because this machine's dead IPv6 stalls unpinned
// hosts ~21s. Every URL verified live (curl -4 HEAD 200) at curation time, 2026-07-12.
//
// `chips` = the mediums present (icon ∈ books|movies|manga|comics|music). A manga count means that
// many DIFFERENT manga, never books-of-one-title (Hemanth 2026-07-12).
//
// Search-hint fields (all optional — the universe page searches by `name` when absent):
//   seriesQueries — Cinemeta series searches assembling the WATCH·series row (universes whose name
//                   isn't a searchable term: DCAU, ASOIAF, Shonen Jump)
//   movieQueries  — Cinemeta movie searches for the WATCH·films row
//   readQueries   — AniList manga searches for the READ row (Shonen Jump = its flagships)
//
// Home / universe domain (Agent 5) — kept OUT of the shared, contended Catalog.js on purpose.
.pragma library

// The 2026-07-12 expansion (Hemanth commission): every entry below is LIVE in the carousel and
// opens a real universe page. All ride the generic name/query-driven template except Marvel
// (cinematic = the MCU Fandom-wiki phase template).
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
      chips: [ { t: "34 Films", ic: "movies" }, { t: "12 Shows", ic: "movies" }, { t: "Comics", ic: "comics" } ] },
    { name: "Harry Potter", c1: "#221c30", category: "anime",
      blurb: "The Wizarding World — Rowling's seven novels, the eight films, and the Fantastic Beasts era beyond.",
      banner: "https://live.metahub.space/background/medium/tt1201607/img",
      chips: [ { t: "7 Novels", ic: "books" }, { t: "11 Films", ic: "movies" }, { t: "1 Show", ic: "movies" } ] },
    { name: "Lord of the Rings", c1: "#1c2414", category: "anime",
      blurb: "Tolkien's Middle-earth — the novels, Jackson's films, and the age of Rings of Power.",
      banner: "https://live.metahub.space/background/medium/tt0167260/img",
      chips: [ { t: "3 Novels", ic: "books" }, { t: "6 Films", ic: "movies" }, { t: "1 Show", ic: "movies" } ] },
    { name: "A Song of Ice and Fire", c1: "#1f2429", category: "anime",
      blurb: "Martin's Westeros — the saga still being written, and the shows that carved it into legend.",
      banner: "https://live.metahub.space/background/medium/tt0944947/img",
      seriesQueries: [ "Game of Thrones", "House of the Dragon" ],
      movieQueries: [ "Game of Thrones" ],
      chips: [ { t: "5 Novels", ic: "books" }, { t: "2 Shows", ic: "movies" }, { t: "Graphic Novels", ic: "comics" } ] },
    { name: "Dragon Ball", c1: "#3a2a10", category: "anime",
      blurb: "Toriyama's world from the Dragon Radar to Ultra Instinct — the manga, four eras of anime, the films.",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30042-4aSSSOxCNWgE.jpg",
      chips: [ { t: "2 Manga", ic: "manga" }, { t: "5 Anime", ic: "movies" }, { t: "21 Films", ic: "movies" } ] },
    { name: "Naruto", c1: "#2a3212", category: "anime",
      blurb: "The Hidden Leaf's loudest ninja — Kishimoto's manga, the anime and Shippuden, and Boruto's generation.",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30011-pkX1O0EFqvV7.jpg",
      chips: [ { t: "2 Manga", ic: "manga" }, { t: "3 Anime", ic: "movies" }, { t: "11 Films", ic: "movies" } ] },
    { name: "DC Animated Universe", c1: "#101622", category: "anime",
      blurb: "The Timmverse — Batman's deco Gotham to Justice League Unlimited, the DC canon animation built right.",
      banner: "https://live.metahub.space/background/medium/tt0275137/img",
      seriesQueries: [ "Batman: The Animated Series", "Superman: The Animated Series",
                       "Batman Beyond", "Justice League Unlimited", "Justice League",
                       "Static Shock" ],
      movieQueries: [ "Batman: Mask of the Phantasm", "Batman Beyond: Return of the Joker",
                      "Batman & Mr. Freeze: SubZero", "Batman: Mystery of the Batwoman" ],
      chips: [ { t: "9 Shows", ic: "movies" }, { t: "6 Films", ic: "movies" }, { t: "Comics", ic: "comics" } ] },
    { name: "Weekly Shonen Jump", c1: "#3a1414", category: "anime",
      blurb: "Shueisha's arena since 1968 — the magazine where One Piece, Naruto, Bleach and Dragon Ball fought for the reader's vote.",
      banner: "https://upload.wikimedia.org/wikipedia/en/0/02/Jump-Cover-1.jpg",
      readQueries: [ "One Piece", "Naruto", "Bleach", "Dragon Ball", "Hunter x Hunter",
                     "My Hero Academia", "Jujutsu Kaisen", "Demon Slayer: Kimetsu no Yaiba",
                     "Chainsaw Man", "Death Note" ],
      seriesQueries: [ "One Piece", "Naruto", "Bleach", "Jujutsu Kaisen", "My Hero Academia", "Demon Slayer" ],
      movieQueries: [ "Demon Slayer", "Jujutsu Kaisen", "One Piece Film" ],
      chips: [ { t: "50+ Manga", ic: "manga" }, { t: "Anime", ic: "movies" }, { t: "Films", ic: "movies" } ] },
    { name: "Star Trek", c1: "#10141f", category: "anime",
      blurb: "Roddenberry's final frontier — six decades of starships, from The Original Series to Strange New Worlds.",
      banner: "https://live.metahub.space/background/medium/tt0796366/img",
      chips: [ { t: "13 Films", ic: "movies" }, { t: "12 Shows", ic: "movies" }, { t: "Novels", ic: "books" } ] },
    { name: "Star Wars", c1: "#14181c", category: "anime",
      blurb: "A galaxy far, far away — the saga films, the live-action and animated shows, the novels.",
      banner: "https://live.metahub.space/background/medium/tt0080684/img",
      chips: [ { t: "12 Films", ic: "movies" }, { t: "10+ Shows", ic: "movies" }, { t: "Novels", ic: "books" } ] },
    { name: "Dune", c1: "#3a2a18", category: "anime",
      blurb: "Frank Herbert's world, end to end — the novels, the films, the graphic novel.",
      banner: "https://live.metahub.space/background/medium/tt15239678/img",
      movieQueries: [ "Dune" ],
      chips: [ { t: "6 Novels", ic: "books" }, { t: "2 Films", ic: "movies" }, { t: "Graphic Novel", ic: "comics" } ] }
];

// (the placeholder bench is empty — the 2026-07-12 commission promoted everything;
//  future curations land here first if their page needs work before surfacing)
var placeholders = [];

// which page TEMPLATE a universe opens into: "cinematic" → CinematicPage (the MCU
// Fandom-wiki phase template, Marvel-only for now); everything else → the generic
// name/query-driven UniversePage.
function categoryFor(name) {
    for (var i = 0; i < universes.length; i++)
        if (universes[i].name === name) return universes[i].category || "anime";
    return "anime";
}

// the curated entry for a universe (the page's banner + search hints live here,
// beside the carousel data — ONE curation point). Empty object when unknown.
function configFor(name) {
    var q = String(name || "").toLowerCase();
    for (var i = 0; i < universes.length; i++)
        if (universes[i].name.toLowerCase() === q) return universes[i];
    return {};
}
