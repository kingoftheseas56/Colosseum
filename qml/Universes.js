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
    { name: "Harry Potter", c1: "#221c30", category: "saga",
      blurb: "The Wizarding World — Rowling's seven novels, the eight films, and the Fantastic Beasts era beyond.",
      banner: "https://live.metahub.space/background/medium/tt1201607/img",
      // the canon, Wikipedia-checked 2026-07-12: exactly these, in this order — search is
      // assembly, THIS is curation. novels route to Biblio (Read = book one), films/shows
      // to Theatre (Watch = film one).
      novels: [ "Harry Potter and the Sorcerer's Stone", "Harry Potter and the Chamber of Secrets",
                "Harry Potter and the Prisoner of Azkaban", "Harry Potter and the Goblet of Fire",
                "Harry Potter and the Order of the Phoenix", "Harry Potter and the Half-Blood Prince",
                "Harry Potter and the Deathly Hallows" ],
      films:  [ "Harry Potter and the Sorcerer's Stone", "Harry Potter and the Chamber of Secrets",
                "Harry Potter and the Prisoner of Azkaban", "Harry Potter and the Goblet of Fire",
                "Harry Potter and the Order of the Phoenix", "Harry Potter and the Half-Blood Prince",
                "Harry Potter and the Deathly Hallows: Part 1", "Harry Potter and the Deathly Hallows: Part 2",
                "Fantastic Beasts and Where to Find Them", "Fantastic Beasts: The Crimes of Grindelwald",
                "Fantastic Beasts: The Secrets of Dumbledore" ],
      shows:  [ "Harry Potter" ],
      movieQueries: [ "Harry Potter", "Fantastic Beasts" ],
      chips: [ { t: "7 Novels", ic: "books" }, { t: "11 Films", ic: "movies" }, { t: "1 Show", ic: "movies" } ] },
    { name: "Lord of the Rings", c1: "#1c2414", category: "saga",
      blurb: "Tolkien's Middle-earth — the novels, Jackson's films, and the age of Rings of Power.",
      banner: "https://live.metahub.space/background/medium/tt0167260/img",
      novels: [ "The Hobbit", "The Fellowship of the Ring", "The Two Towers",
                "The Return of the King", "The Silmarillion" ],
      films:  [ "The Lord of the Rings: The Fellowship of the Ring", "The Lord of the Rings: The Two Towers",
                "The Lord of the Rings: The Return of the King", "The Hobbit: An Unexpected Journey",
                "The Hobbit: The Desolation of Smaug", "The Hobbit: The Battle of the Five Armies",
                "The Lord of the Rings: The War of the Rohirrim" ],
      shows:  [ "The Lord of the Rings: The Rings of Power" ],
      movieQueries: [ "The Lord of the Rings", "The Hobbit" ],
      seriesQueries: [ "The Lord of the Rings" ],
      readQueries: [ "The Hobbit Tolkien" ],
      chips: [ { t: "5 Novels", ic: "books" }, { t: "7 Films", ic: "movies" }, { t: "1 Show", ic: "movies" } ] },
    { name: "A Song of Ice and Fire", c1: "#1f2429", category: "saga",
      blurb: "Martin's Westeros — the saga still being written, and the shows that carved it into legend.",
      banner: "https://live.metahub.space/background/medium/tt0944947/img",
      novels: [ "A Game of Thrones", "A Clash of Kings", "A Storm of Swords",
                "A Feast for Crows", "A Dance with Dragons" ],
      films:  [],
      shows:  [ "Game of Thrones", "House of the Dragon" ],
      seriesQueries: [ "Game of Thrones", "House of the Dragon" ],
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
      seriesLabel: "Shows", readMode: "none",
      blurb: "The Timmverse — Batman's deco Gotham to Justice League Unlimited, the DC canon animation built right.",
      banner: "https://live.metahub.space/background/medium/tt0275137/img",
      seriesQueries: [ "Batman: The Animated Series", "Superman: The Animated Series",
                       "Batman Beyond", "Justice League Unlimited", "Justice League",
                       "Static Shock" ],
      movieQueries: [ "Batman: Mask of the Phantasm", "Batman Beyond: Return of the Joker",
                      "Batman & Mr. Freeze: SubZero", "Batman: Mystery of the Batwoman" ],
      chips: [ { t: "9 Shows", ic: "movies" }, { t: "6 Films", ic: "movies" }, { t: "Comics", ic: "comics" } ] },
    // the MAGAZINE template: Jump publishes MANGA — no anime/film queries at all
    // (Hemanth 2026-07-12). The readQueries are the ranked lineup, one flagship each.
    { name: "Weekly Shonen Jump", c1: "#3a1414", category: "magazine",
      blurb: "Shueisha's arena since 1968 — the magazine where One Piece, Naruto, Bleach and Dragon Ball fought for the reader's vote.",
      banner: "https://upload.wikimedia.org/wikipedia/en/0/02/Jump-Cover-1.jpg",
      readQueries: [ "One Piece", "Naruto", "Bleach", "Dragon Ball", "Hunter x Hunter",
                     "My Hero Academia", "Jujutsu Kaisen", "Demon Slayer: Kimetsu no Yaiba",
                     "Chainsaw Man", "Death Note" ],
      chips: [ { t: "50+ Manga", ic: "manga" }, { t: "Weekly", ic: "manga" }, { t: "Since 1968", ic: "manga" } ] },
    { name: "Star Trek", c1: "#10141f", category: "anime",
      seriesLabel: "TV Shows", readMode: "none",
      blurb: "Roddenberry's final frontier — six decades of starships, from The Original Series to Strange New Worlds.",
      banner: "https://live.metahub.space/background/medium/tt0796366/img",
      chips: [ { t: "13 Films", ic: "movies" }, { t: "12 Shows", ic: "movies" }, { t: "Novels", ic: "books" } ] },
    // the GALAXY template: the Skywalker Saga as three curated trilogies + the standalone
    // stories + the series in live/animated rails. Canon names verified against Cinemeta's
    // own catalog 2026-07-12 (modern shows don't CONTAIN "Star Wars" — Andor, The
    // Mandalorian — which is exactly why the name-relevance generic page starved empty).
    { name: "Star Wars", c1: "#14181c", category: "galaxy",
      blurb: "A galaxy far, far away — the nine-episode Skywalker Saga, the standalone stories, and the age of The Mandalorian.",
      banner: "https://live.metahub.space/background/medium/tt0080684/img",
      trilogies: [
        { era: "The Prequels",  films: [ "Star Wars: Episode I - The Phantom Menace",
                                         "Star Wars: Episode II - Attack of the Clones",
                                         "Star Wars: Episode III - Revenge of the Sith" ] },
        { era: "The Originals", films: [ "Star Wars: Episode IV - A New Hope",
                                         "Star Wars: Episode V - The Empire Strikes Back",
                                         "Star Wars: Episode VI - Return of the Jedi" ] },
        { era: "The Sequels",   films: [ "Star Wars: Episode VII - The Force Awakens",
                                         "Star Wars: Episode VIII - The Last Jedi",
                                         "Star Wars: Episode IX - The Rise of Skywalker" ] }
      ],
      standalones: [ "Rogue One: A Star Wars Story", "Solo: A Star Wars Story" ],
      liveShows: [ "The Mandalorian", "Andor", "Obi-Wan Kenobi", "The Book of Boba Fett",
                   "Ahsoka", "The Acolyte", "Skeleton Crew" ],
      animatedShows: [ "Star Wars: The Clone Wars", "Star Wars: Rebels",
                       "Star Wars: The Bad Batch", "Star Wars: Resistance", "Star Wars: Visions" ],
      firstWatch: "Star Wars: Episode IV - A New Hope",
      movieQueries: [ "Star Wars", "Rogue One: A Star Wars Story", "Solo: A Star Wars Story" ],
      seriesQueries: [ "Star Wars", "The Mandalorian", "Andor", "Obi-Wan Kenobi",
                       "The Book of Boba Fett", "Ahsoka", "The Acolyte", "Skeleton Crew" ],
      chips: [ { t: "11 Films", ic: "movies" }, { t: "12 Shows", ic: "movies" }, { t: "Novels", ic: "books" } ] },
    { name: "Dune", c1: "#3a2a18", category: "saga",
      blurb: "Frank Herbert's world, end to end — the novels, the films, the graphic novel.",
      banner: "https://live.metahub.space/background/medium/tt15239678/img",
      novels: [ "Dune", "Dune Messiah", "Children of Dune", "God Emperor of Dune",
                "Heretics of Dune", "Chapterhouse: Dune" ],
      films:  [ "Dune", "Dune: Part Two" ],
      shows:  [ "Dune: Prophecy" ],
      movieQueries: [ "Dune" ],
      seriesQueries: [ "Dune" ],
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
