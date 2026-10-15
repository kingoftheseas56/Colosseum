// Universes.js — the UNIVERSE COLLECTION: the curated set of multi-medium IPs that feeds the home's
// universe carousel AND each universe page.
//
// ════════════════════════════════════════════════════════════════════════════════════════
// THE UNIVERSE PAGE LAW (Hemanth, ratified 2026-07-13 — binding on every future AI/agent):
// a universe page is a curated set of METADATA-PROVIDER SERIES IDS, never a name search.
//   · Screen entries carry Cinemeta id-pins `{ t, id: "tt…" }` — same-name impostors are
//     the NORM (two Avatar shows, four Dune films, 2003-vs-2008 Clone Wars, Ms. Marvel and
//     She-Hulk on ADJACENT ids). A bare name is only legal when live-verified unambiguous.
//   · Manga/anime entries resolve to MAL/AniList/Kitsu identities (id or a live-verified
//     exact query — same-title NOVELS outrank manga on AniList; send format, prefer ids).
//   · Comics doors pin the GetComics archive: `comics: { tag: <slug>, tagId: <id> }`.
//   · Books are exact verified titles (Apple Books resolution; watch US/UK title splits).
//   · METADATA ID = THE GATE, never release dates: no id → the work does not enter; an id
//     with a future date enters wearing the small UPCOMING tag (probeUpcoming in SagaApi).
//   · Providers only DRESS the slots (canon-over-search, slotByCanon): an unmatched slot
//     stays EMPTY — never filled by a fuzzy stand-in.
//   · Middlemen get ladders (Jikan→Kitsu precedent): the ID is what survives outages.
//   · Every pin is verified LIVE against the provider before it lands here — a research
//     report or memory is a lead, never evidence. Enforced by tests/test_universe_expansion_p0.ps1.
// ════════════════════════════════════════════════════════════════════════════════════════
//
// Real banner key-art, disk-cached like every remote image
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
    // ── ONE PIECE — the bespoke GRAND LINE page (category "onepiece" → OnePieceUniversePage,
    //    Agent 5, 2026-07-15, Hemanth free-reign commission). Unlike Dragon Ball's seven
    //    separate anime, One Piece is ONE continuous voyage — so the signature is the Grand
    //    Line itself: the canon sagas charted as island waypoints. Every WORK id-pinned and
    //    LIVE-verified: the anime tt0388629 (the 1999 Toei series), the Netflix live action
    //    (tt11737520) and the announced WIT remake "The One Piece" (tt30476502, upcoming),
    //    12 theatrical films + 5 "Episode of"/special films (Cinemeta), 8 manga (AniList).
    //    Sagas are the story's spine (arcs of the one anime), not separate ids — each opens
    //    the anime; ranges from the Wikipedia media list.
    { name: "One Piece", c1: "#0e2a3f", category: "onepiece",
      // Wikipedia lead, fetched 2026-07-18 (verbatim) — sourced copy, never self-written
      blurb: "One Piece is a Japanese manga series written and illustrated by Eiichiro Oda. It follows the adventures of Monkey D. Luffy and his crew, the Straw Hats, as he searches for the legendary treasure known as the \"One Piece\" to become the next King of the Pirates.",
      blurbSource: "Wikipedia — One Piece",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg",
      // the one grand anime — the "Set sail" hero and every saga waypoint open this.
      anime: { t: "One Piece", id: "tt0388629", year: "1999" },
      firstRead: { t: "One Piece" },
      // THE GRAND LINE — the canon sagas as charted waypoints (arcs of the one anime).
      sagas: [
        { n: 1,  name: "East Blue",         eps: "Ep 1–61",     hook: "Where the dream sets sail" },
        { n: 2,  name: "Alabasta",          eps: "Ep 62–135",   hook: "Into the Grand Line, a kingdom to save" },
        { n: 3,  name: "Sky Island",        eps: "Ep 136–206",  hook: "A sea in the sky, a city of gold" },
        { n: 4,  name: "Water Seven",       eps: "Ep 207–325",  hook: "A betrayal, Enies Lobby, a new ship" },
        { n: 5,  name: "Thriller Bark",     eps: "Ep 326–384",  hook: "A long night among the dead" },
        { n: 6,  name: "Summit War",        eps: "Ep 385–516",  hook: "Sabaody, Impel Down, the war for Ace" },
        { n: 7,  name: "Fish-Man Island",   eps: "Ep 517–574",  hook: "Two years on — ten thousand metres down" },
        { n: 8,  name: "Dressrosa",         eps: "Ep 575–746",  hook: "Toys, tyrants, a gladiator's colosseum" },
        { n: 9,  name: "Whole Cake Island", eps: "Ep 747–877",  hook: "A tea party with an Emperor" },
        { n: 10, name: "Wano Country",      eps: "Ep 878–1085", hook: "The land of samurai, the dawn of liberation" },
        { n: 11, name: "The Final Sea",     eps: "Ep 1086– ",   hook: "Egghead, and the last of the treasure", treasure: true }
      ],
      // the other adaptations — the live action and the announced remake.
      adaptations: [
        { t: "One Piece — Live Action", id: "tt11737520", year: "2023", note: "Netflix sets sail in the flesh" },
        { t: "The One Piece",           id: "tt30476502", year: "2026", note: "The WIT Studio remake", upcoming: true }
      ],
      // THE FILMS — grouped, chronological, all id-pinned to Cinemeta.
      filmEras: [
        { era: "The Films", films: [
            { t: "One Piece: The Movie",                   id: "tt0814243", year: "2000" },
            { t: "Clockwork Island Adventure",             id: "tt0832449", year: "2001" },
            { t: "Chopper's Kingdom on the Island of Strange Animals", id: "tt0997084", year: "2002" },
            { t: "Dead End Adventure",                     id: "tt1006926", year: "2003" },
            { t: "The Cursed Holy Sword",                  id: "tt1010435", year: "2004" },
            { t: "Baron Omatsuri and the Secret Island",   id: "tt1018764", year: "2005" },
            { t: "The Giant Mechanical Soldier of Karakuri Castle", id: "tt1059950", year: "2006" },
            { t: "Strong World",                           id: "tt1485763", year: "2009" },
            { t: "Film Z",                                 id: "tt2375379", year: "2012" },
            { t: "Film: Gold",                             id: "tt5251328", year: "2016" },
            { t: "Stampede",                               id: "tt9430698", year: "2019" },
            { t: "Film: Red",                              id: "tt16183464", year: "2022" } ] },
        { era: "Episode Of & Specials", films: [
            { t: "Episode of Alabasta",                    id: "tt1037116", year: "2007" },
            { t: "Episode of Chopper Plus",                id: "tt1206326", year: "2008" },
            { t: "Episode of Luffy — Hand Island",         id: "tt3354344", year: "2012" },
            { t: "Episode of Merry",                       id: "tt3354352", year: "2013" },
            { t: "3D2Y — Overcome Ace's Death",            id: "tt5098548", year: "2014" } ] }
      ],
      // THE MANGA — the source + spin-offs (AniList covers; opens the manga reader by title).
      manga: [
        { t: "One Piece",                cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30013-BeslEMqiPhlk.jpg" },
        { t: "Ace's Story",              q: "One Piece Ace Story",            cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx117802-CsCjUyuG4lSB.jpg" },
        { t: "One Piece Party",          q: "One Piece Party",                cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx102533-YLT9eI1BH2a1.jpg" },
        { t: "Shokugeki no Sanji",       q: "One Piece Shokugeki no Sanji",   cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx103252-58RbwHibqsJY.jpg" },
        { t: "Koisuru One Piece",        q: "Koisuru One Piece",              cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx110233-7Z79ZksUA043.jpg" },
        { t: "One Piece × Toriko",       q: "One Piece Toriko",               cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/25146.jpg" },
        { t: "Chapter 1000 Special",     q: "One Piece 1000",                 cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx154266-V6HV8ReEygYZ.png" },
        { t: "Wanted! — Oda's Origins",  q: "Wanted Eiichiro Oda",            cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30793-Zca4SIWG5j8e.png" }
      ],
      chips: [ { t: "8 Manga", ic: "manga" }, { t: "3 Anime", ic: "movies" }, { t: "17 Films", ic: "movies" } ] },
    // renamed Marvel → full name (Hemanth 2026-07-13) + THE TELEVISION ACT: every Marvel
    // Studios series + the Special Presentations, release-ordered, ALL id-pinned (bare
    // names are impostor minefields — Loki/Hawkeye/Echo/What If all collide; Ms. Marvel
    // and She-Hulk sit on ADJACENT ids). X-Men '97 carries no phase (Marvel assigns none).
    { name: "Marvel Cinematic Universe", c1: "#1a2436", category: "cinematic",
      // Wikipedia lead, fetched 2026-07-18 (verbatim) — sourced copy, never self-written
      blurb: "The Marvel Cinematic Universe (MCU) is an American media franchise and shared universe centered on a series of superhero films produced by Marvel Studios. The films are based on characters from American comic books published by Marvel Comics.",
      blurbSource: "Wikipedia — Marvel Cinematic Universe",
      banner: "https://image.tmdb.org/t/p/w1280/gHLs7Fy3DzLmLsD4lmfqL55KGcl.jpg",
      continueLabel: "Continue — Loki S2",
      shows: [ { t: "WandaVision", id: "tt9140560" },
               { t: "The Falcon and the Winter Soldier", id: "tt9208876" },
               { t: "Loki", id: "tt9140554" },
               { t: "What If...?", id: "tt10168312" },
               { t: "Hawkeye", id: "tt10160804" },
               { t: "Moon Knight", id: "tt10234724" },
               { t: "Ms. Marvel", id: "tt10857164" },
               { t: "I Am Groot", id: "tt13623148" },
               { t: "She-Hulk: Attorney at Law", id: "tt10857160" },
               { t: "Secret Invasion", id: "tt13157618" },
               { t: "Echo", id: "tt13966962" },
               { t: "X-Men '97", id: "tt16026746" },
               { t: "Agatha All Along", id: "tt15571732" },
               { t: "Your Friendly Neighborhood Spider-Man", id: "tt16027074" },
               { t: "Daredevil: Born Again", id: "tt18923754" },
               { t: "Ironheart", id: "tt13623126" },
               { t: "Eyes of Wakanda", id: "tt13968252" },
               { t: "Marvel Zombies", id: "tt16027014" },
               { t: "Wonder Man", id: "tt21066182" },
               { t: "VisionQuest", id: "tt23112594" } ],
      films: [ { t: "Werewolf by Night", id: "tt15318872" },
               { t: "The Guardians of the Galaxy Holiday Special", id: "tt13623136" },
               { t: "The Punisher: One Last Kill", id: "tt36042156" } ],
      // premiere phase per id — the tile plate (multi-phase runs wear their premiere)
      mcuShowPhases: { "tt9140560": "IV", "tt9208876": "IV", "tt9140554": "IV",
                       "tt10168312": "IV", "tt10160804": "IV", "tt10234724": "IV",
                       "tt10857164": "IV", "tt13623148": "IV", "tt10857160": "IV",
                       "tt13157618": "V", "tt13966962": "V", "tt15571732": "V",
                       "tt16027074": "V", "tt18923754": "V", "tt13623126": "V",
                       "tt13968252": "VI", "tt16027014": "VI", "tt21066182": "VI",
                       "tt23112594": "VI",
                       "tt15318872": "IV", "tt13623136": "IV", "tt36042156": "VI" },
      seriesQueries: [ "wandavision", "the falcon and the winter soldier", "loki", "what if",
                       "hawkeye", "moon knight", "ms marvel", "i am groot",
                       "she hulk attorney at law", "secret invasion", "echo", "x-men 97",
                       "agatha all along", "your friendly neighborhood spider-man",
                       "daredevil born again", "ironheart", "eyes of wakanda",
                       "marvel zombies", "wonder man", "visionquest" ],
      movieQueries: [ "werewolf by night", "guardians of the galaxy holiday special",
                      "the punisher one last kill" ],
      chips: [ { t: "34 Films", ic: "movies" }, { t: "20 Series", ic: "movies" }, { t: "3 Specials", ic: "movies" } ] },
    // THE COGNITIVE ATLAS — a newcomer-first map of Sanderson's connected book universe.
    // Every query below was live-verified against Apple Books US on 2026-07-13. The author
    // is carried in the query because Biblio.lookupBook returns the first provider hit; this
    // makes each portal an exact Brandon Sanderson object, never a title-shaped stand-in.
    { name: "Dragon Ball", c1: "#e8791e", category: "dragonball",
      // Wikipedia lead, fetched 2026-07-18 (verbatim) — sourced copy, never self-written
      blurb: "Dragon Ball is a Japanese media franchise created by Akira Toriyama. The series follows the adventures of protagonist Son Goku from his childhood through adulthood as he trains in martial arts.",
      blurbSource: "Wikipedia — Dragon Ball",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30042-4aSSSOxCNWgE.jpg",
      // THE SEVEN-STAR SAGA — the seven anime, broadcast order, one per Dragon Ball.
      saga: [
        { star: 1, era: "Dragon Ball",              t: "Dragon Ball",              id: "tt0088509",  year: "1986", note: "The boy, the tail, the first search" },
        { star: 2, era: "Dragon Ball Z",            t: "Dragon Ball Z",            id: "tt0121220",  year: "1989", note: "Saiyans arrive, and the sky gets higher" },
        { star: 3, era: "Dragon Ball GT",           t: "Dragon Ball GT",           id: "tt0139774",  year: "1996", note: "Off Earth, chasing the Black Star balls" },
        { star: 4, era: "Dragon Ball Z Kai",        t: "Dragon Ball Z Kai",        id: "tt1409055",  year: "2009", note: "Z re-cut, tighter, closer to the manga" },
        { star: 5, era: "Dragon Ball Super",        t: "Dragon Ball Super",        id: "tt4644488",  year: "2015", note: "Gods of destruction, other universes" },
        { star: 6, era: "Super Dragon Ball Heroes", t: "Super Dragon Ball Heroes", id: "tt8433216",  year: "2018", note: "Every hero, every timeline at once" },
        { star: 7, era: "Dragon Ball Daima",        t: "Dragon Ball Daima",        id: "tt29485149", year: "2024", note: "Toriyama's parting gift — small again" }
      ],
      // THE FILMS — grouped, chronological, all id-pinned to Cinemeta.
      filmEras: [
        { era: "The Dragon Ball Films", films: [
            { t: "Curse of the Blood Rubies",           id: "tt0142251", year: "1986" },
            { t: "Sleeping Princess in Devil's Castle", id: "tt0142249", year: "1987" },
            { t: "Mystical Adventure",                  id: "tt0142248", year: "1988" },
            { t: "The Path to Power",                   id: "tt0142250", year: "1996" } ] },
        { era: "The Z Films & Specials", films: [
            { t: "Dead Zone",                           id: "tt0142235", year: "1989" },
            { t: "The World's Strongest",               id: "tt0142240", year: "1990" },
            { t: "The Tree of Might",                   id: "tt0142233", year: "1990" },
            { t: "Bardock — The Father of Goku",        id: "tt0142245", year: "1990" },
            { t: "Lord Slug",                           id: "tt0142244", year: "1991" },
            { t: "Cooler's Revenge",                    id: "tt1125254", year: "1991" },
            { t: "The Return of Cooler",                id: "tt0142237", year: "1992" },
            { t: "Super Android 13!",                   id: "tt0142241", year: "1992" },
            { t: "Broly — The Legendary Super Saiyan",  id: "tt0142242", year: "1993" },
            { t: "The History of Trunks",               id: "tt0142247", year: "1993" },
            { t: "Bojack Unbound",                      id: "tt0142238", year: "1993" },
            { t: "Plan to Eradicate the Saiyans",       id: "tt1286785", year: "1993" },
            { t: "Broly — Second Coming",               id: "tt0142239", year: "1994" },
            { t: "Bio-Broly",                           id: "tt0142234", year: "1994" },
            { t: "Fusion Reborn",                       id: "tt0142236", year: "1995" },
            { t: "Wrath of the Dragon",                 id: "tt0142243", year: "1995" },
            { t: "GT: A Hero's Legacy",                 id: "tt0142232", year: "1997" } ] },
        { era: "The Modern Films", films: [
            { t: "Battle of Gods",                      id: "tt2263944",  year: "2013" },
            { t: "Resurrection 'F'",                    id: "tt3819668",  year: "2015" },
            { t: "Dragon Ball Super: Broly",            id: "tt7961060",  year: "2018" },
            { t: "Dragon Ball Super: Super Hero",       id: "tt14614892", year: "2022" } ] }
      ],
      // THE MANGA — the source + spin-offs (AniList covers; opens the manga reader by title).
      manga: [
        { t: "Dragon Ball",                cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30042-4SetGiEbGc9x.jpg" },
        { t: "Dragon Ball Super",          cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx86508-QSahE7mTFEXl.png" },
        { t: "Dragon Ball SD",             cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx53446-iJhUffEy8U9u.jpg" },
        { t: "Reincarnated as Yamcha!",    q: "Dragon Ball Yamcha",                     cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx98030-ljTCpp4oILtu.jpg" },
        { t: "Episode of Bardock",         q: "Dragon Ball Episode of Bardock",         cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx56373-VBxH4drN6jJ1.png" },
        { t: "Resurrection 'F'",           q: "Dragon Ball Z Resurrection F",           cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx94109-oCtSkyO2NOUW.jpg" },
        { t: "Dragon Ball Minus",          q: "Dragon Ball Minus Departure Fated Child", cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx97900-EqScEWX0U6Tj.png" },
        { t: "Goku & Friends Return!!",    q: "Dragon Ball Son Goku and His Friends Return", cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx46110-J5o0hRODa79o.jpg" }
      ],
      firstWatch: { t: "Dragon Ball", id: "tt0088509" },
      chips: [ { t: "8 Manga", ic: "manga" }, { t: "7 Anime", ic: "movies" }, { t: "25 Films", ic: "movies" } ] },
    { name: "Cosmere", c1: "#101927", category: "cosmere",
      // Wikipedia lead (Brandon Sanderson — the Cosmere has no standalone article), fetched
      // 2026-07-18, verbatim — sourced copy, never self-written
      blurb: "Brandon Winn Sanderson is an American author of high fantasy, science fiction, and young adult books. His best known novels include the Mistborn series and The Stormlight Archive, which are set in the \"Cosmere\", a fictional universe.",
      blurbSource: "Wikipedia — Brandon Sanderson",
      // CC BY 4.0 star field by Sahisnusaha, Wikimedia Commons; the page draws its own
      // Cognitive Atlas over it. upload.wikimedia.org is already IPv4-pinned by Colosseum.
      banner: "https://upload.wikimedia.org/wikipedia/commons/thumb/9/99/Window_to_the_Cosmos.jpg/1920px-Window_to_the_Cosmos.jpg",
      chips: [ { t: "6 Systems", ic: "books" }, { t: "26 Books & Stories", ic: "books" },
               { t: "One Connected Epic", ic: "books" } ],
      cosmereStarters: [
        { label: "The balanced beginning", short: "MISTBORN",
          note: "A complete fantasy heist with the clearest doorway into Sanderson's magic.",
          query: "Mistborn The Final Empire Brandon Sanderson" },
        { label: "The deep end", short: "STORMLIGHT",
          note: "A vast epic for readers ready to begin with the Cosmere at full scale.",
          query: "The Way of Kings Brandon Sanderson" },
        { label: "The standalone voyage", short: "TRESS",
          note: "A warm, self-contained adventure with the wider universe glinting beneath it.",
          query: "Tress of the Emerald Sea Brandon Sanderson" }
      ],
      cosmereWorlds: [
        { name: "Scadrial", epithet: "METAL · ASH · REBELLION", accent: "#b8734a",
          books: [
            { label: "Mistborn — Era One", query: "Mistborn The Final Empire Brandon Sanderson" },
            { label: "Mistborn — Era Two", query: "The Alloy of Law Brandon Sanderson" }
          ] },
        { name: "Roshar", epithet: "STORMS · OATHS · RADIANCE", accent: "#78cfe3",
          books: [
            { label: "The Stormlight Archive", query: "The Way of Kings Brandon Sanderson" },
            { label: "Edgedancer", query: "Edgedancer Brandon Sanderson" },
            { label: "Dawnshard", query: "Dawnshard Brandon Sanderson" }
          ] },
        { name: "Sel", epithet: "GLYPHS · SOULS · DEVOTION", accent: "#d9e8ff",
          books: [
            { label: "Elantris", query: "Elantris Brandon Sanderson" },
            { label: "The Emperor's Soul", query: "The Emperor's Soul Brandon Sanderson" }
          ] },
        { name: "Nalthis", epithet: "BREATH · COLOR · AWAKENING", accent: "#d688b4",
          books: [ { label: "Warbreaker", query: "Warbreaker Brandon Sanderson" } ] },
        { name: "Taldain", epithet: "SAND · SUN · MASTERY", accent: "#e5c77c",
          books: [ { label: "White Sand", query: "White Sand Brandon Sanderson" } ] },
        { name: "Farther Worlds", epithet: "OCEANS · DREAMS · STARLIGHT", accent: "#8ec7b5",
          books: [
            { label: "Tress of the Emerald Sea", query: "Tress of the Emerald Sea Brandon Sanderson" },
            { label: "Yumi and the Nightmare Painter", query: "Yumi and the Nightmare Painter Brandon Sanderson" },
            { label: "The Sunlit Man", query: "The Sunlit Man Brandon Sanderson" },
            { label: "Isles of the Emberdark", query: "Isles of the Emberdark Brandon Sanderson" },
            { label: "Arcanum Unbounded", query: "Arcanum Unbounded Brandon Sanderson" }
          ] }
      ],
      // The atlas explains where to begin; these shelves contain the actual published
      // roads. Wikipedia + Brandon's official catalog checked 2026-07-13, then every
      // query live-verified against Apple Books US. Short fiction without its own Apple
      // identity remains honestly represented by Arcanum Unbounded instead of a dead tile.
      cosmereSeries: [
        { name: "Mistborn — Era One", epithet: "THE ORIGINAL TRILOGY", accent: "#b8734a",
          books: [
            { label: "01", query: "Mistborn The Final Empire Brandon Sanderson" },
            { label: "02", query: "The Well of Ascension Brandon Sanderson" },
            { label: "03", query: "The Hero of Ages Brandon Sanderson" }
          ] },
        { name: "Mistborn — Era Two", epithet: "WAX & WAYNE", accent: "#ce9567",
          books: [
            { label: "01", query: "The Alloy of Law Brandon Sanderson" },
            { label: "02", query: "Shadows of Self Brandon Sanderson" },
            { label: "03", query: "The Bands of Mourning Brandon Sanderson" },
            { label: "04", query: "The Lost Metal Brandon Sanderson" }
          ] },
        { name: "The Stormlight Archive", epithet: "FIRST ARC + NOVELLAS", accent: "#78cfe3",
          books: [
            { label: "01", query: "The Way of Kings Brandon Sanderson" },
            { label: "02", query: "Words of Radiance Brandon Sanderson" },
            { label: "2.5", query: "Edgedancer Brandon Sanderson" },
            { label: "03", query: "Oathbringer Brandon Sanderson" },
            { label: "3.5", query: "Dawnshard Brandon Sanderson" },
            { label: "04", query: "Rhythm of War Brandon Sanderson" },
            { label: "05", query: "Wind and Truth Brandon Sanderson" }
          ] },
        { name: "Selish Stories", epithet: "ELANTRIS + THE EMPEROR'S SOUL", accent: "#d9e8ff",
          books: [
            { label: "ELANTRIS", query: "Elantris Brandon Sanderson" },
            { label: "SHARDWORLD NOVELLA", query: "The Emperor's Soul Brandon Sanderson" }
          ] },
        { name: "Hoid's Travails", epithet: "STORIES TOLD ACROSS THE STARS", accent: "#8ec7b5",
          books: [
            { label: "01", query: "Tress of the Emerald Sea Brandon Sanderson" },
            { label: "02", query: "Yumi and the Nightmare Painter Brandon Sanderson" },
            { label: "03", query: "The Fires of December Brandon Sanderson" }
          ] },
        { name: "Cosmere Standalones", epithet: "COMPLETE WORLDS IN ONE VOLUME", accent: "#d688b4",
          books: [
            { label: "NALTHIS", query: "Warbreaker Brandon Sanderson" },
            { label: "CANTICLE", query: "The Sunlit Man Brandon Sanderson" },
            { label: "FIRST OF THE SUN", query: "Isles of the Emberdark Brandon Sanderson" },
            { label: "THRENODY", query: "Shadows for Silence in the Forests of Hell Brandon Sanderson" }
          ] },
        { name: "White Sand", epithet: "THE TALDAIN GRAPHIC NOVEL", accent: "#e5c77c",
          books: [
            { label: "COMPLETE EDITION", query: "White Sand Omnibus Brandon Sanderson" }
          ] },
        { name: "Collections & Secret Histories", epithet: "THE CONNECTIONS BENEATH", accent: "#9aa8c6",
          books: [
            { label: "THE COSMERE COLLECTION", query: "Arcanum Unbounded Brandon Sanderson" },
            { label: "MISTBORN NOVELLA", query: "Mistborn Secret History Brandon Sanderson" }
          ] }
      ] },
    { name: "Harry Potter", c1: "#221c30", category: "saga",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "The Wizarding World — Rowling's seven novels, the eight films, and the Fantastic Beasts era beyond.",
      banner: "https://live.metahub.space/background/medium/tt1201607/img",
      // the canon, Wikipedia-checked 2026-07-12: exactly these, in this order — search is
      // assembly, THIS is curation. novels route to Biblio (Read = book one), films/shows
      // to Theatre (Watch = film one).
      // the seven, then the Hogwarts Library companions (expansion 2026-07-13; the Beasts
      // query carries the Scamander byline to dodge the 2016 screenplay on Apple)
      novels: [ "Harry Potter and the Sorcerer's Stone", "Harry Potter and the Chamber of Secrets",
                "Harry Potter and the Prisoner of Azkaban", "Harry Potter and the Goblet of Fire",
                "Harry Potter and the Order of the Phoenix", "Harry Potter and the Half-Blood Prince",
                "Harry Potter and the Deathly Hallows",
                "Fantastic Beasts and Where to Find Them Newt Scamander",
                "Quidditch Through the Ages", "The Tales of Beedle the Bard" ],
      films:  [ "Harry Potter and the Sorcerer's Stone", "Harry Potter and the Chamber of Secrets",
                "Harry Potter and the Prisoner of Azkaban", "Harry Potter and the Goblet of Fire",
                "Harry Potter and the Order of the Phoenix", "Harry Potter and the Half-Blood Prince",
                "Harry Potter and the Deathly Hallows: Part 1", "Harry Potter and the Deathly Hallows: Part 2",
                "Fantastic Beasts and Where to Find Them", "Fantastic Beasts: The Crimes of Grindelwald",
                "Fantastic Beasts: The Secrets of Dumbledore" ],
      // the HBO show is id-pinned (Cinemeta names it bare "Harry Potter" — search also
      // returns Wizards of Baking / Tournament of Houses); premieres 2026-12-25 → UPCOMING tag
      shows:  [ { t: "Harry Potter", id: "tt13918446" } ],
      movieQueries: [ "Harry Potter", "Fantastic Beasts" ],
      chips: [ { t: "10 Books", ic: "books" }, { t: "11 Films", ic: "movies" }, { t: "1 Show", ic: "movies" } ] },
    { name: "Lord of the Rings", c1: "#1c2414", category: "saga",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "Tolkien's Middle-earth — the novels, Jackson's films, and the age of Rings of Power.",
      banner: "https://live.metahub.space/background/medium/tt0167260/img",
      novels: [ "The Hobbit", "The Fellowship of the Ring", "The Two Towers",
                "The Return of the King", "The Silmarillion" ],
      // + Bakshi's 1978 animated film (id-pinned — buried under the Jackson trilogy in
      // search) and The Hunt for Gollum (2027 → UPCOMING tag); expansion 2026-07-13
      films:  [ "The Lord of the Rings: The Fellowship of the Ring", "The Lord of the Rings: The Two Towers",
                "The Lord of the Rings: The Return of the King", "The Hobbit: An Unexpected Journey",
                "The Hobbit: The Desolation of Smaug", "The Hobbit: The Battle of the Five Armies",
                "The Lord of the Rings: The War of the Rohirrim",
                { t: "The Lord of the Rings", id: "tt0077869" },
                { t: "The Hunt for Gollum", id: "tt32328070" } ],
      shows:  [ "The Lord of the Rings: The Rings of Power" ],
      movieQueries: [ "The Lord of the Rings", "The Hobbit" ],
      seriesQueries: [ "The Lord of the Rings" ],
      readQueries: [ "The Hobbit Tolkien" ],
      chips: [ { t: "5 Novels", ic: "books" }, { t: "9 Films", ic: "movies" }, { t: "1 Show", ic: "movies" } ] },
    { name: "A Song of Ice and Fire", c1: "#1f2429", category: "saga",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "Martin's Westeros — the saga still being written, and the shows that carved it into legend.",
      banner: "https://live.metahub.space/background/medium/tt0944947/img",
      // saga 1-5 in reading order, then the companions (expansion 2026-07-13, Wikipedia-checked:
      // ampersands are canonical — "Fire & Blood", never "and"; Knight = the Dunk & Egg book)
      novels: [ "A Game of Thrones", "A Clash of Kings", "A Storm of Swords",
                "A Feast for Crows", "A Dance with Dragons",
                "A Knight of the Seven Kingdoms", "Fire & Blood",
                "The World of Ice & Fire", "The Rise of the Dragon: An Illustrated History" ],
      films:  [],
      shows:  [ "Game of Thrones", "House of the Dragon",
                { t: "A Knight of the Seven Kingdoms", id: "tt27497448" } ],
      seriesQueries: [ "Game of Thrones", "House of the Dragon", "a knight of the seven kingdoms" ],
      chips: [ { t: "9 Books", ic: "books" }, { t: "3 Shows", ic: "movies" }, { t: "Graphic Novels", ic: "comics" } ] },
    // ── DRAGON BALL — the bespoke SAGA page (category "dragonball" → DragonBallUniversePage,
    //    Agent 5, 2026-07-15, Hemanth free-reign commission). Every work id-pinned and
    //    LIVE-verified against Cinemeta (anime/films) + AniList (manga) at curation time.
    //    Same-name impostors resolved by id: DB = tt0088509 (1986 Toei original, NOT the 1995
    //    US listing tt0280249), DBZ = tt0121220 (1989 Toei original, NOT the 1996 US dub
    //    tt0214341). Fan works (Abridged, Absalon) and the unofficial live-action "The Magic
    //    Begins" excluded — canon only. The SEVEN anime map to the seven Dragon Balls.
    { name: "Naruto", c1: "#2a3212", category: "anime",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "The Hidden Leaf's loudest ninja — Kishimoto's manga, the anime and Shippuden, and Boruto's generation.",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30011-pkX1O0EFqvV7.jpg",
      // expansion 2026-07-13 (AniList-verified): Boruto NNG (AL 87178, finished) + Two Blue
      // Vortex (AL 168468, the live continuation) + the Scarlet Spring gaiden (AL 86171)
      readQueries: [ "Naruto", "Boruto", "Boruto: Two Blue Vortex",
                     "Naruto: The Seventh Hokage and the Scarlet Spring" ],
      seriesQueries: [ "naruto", "boruto" ],
      chips: [ { t: "5 Manga", ic: "manga" }, { t: "3 Anime", ic: "movies" }, { t: "11 Films", ic: "movies" } ] },
    // ERAS template — THE TIMELINE: the Timmverse in in-universe chronology. Ids pinned
    // where research flagged impostors (Batman Beyond's 2015 web series; Batwoman ranks 8th).
    { name: "DC Animated Universe", c1: "#101622", category: "eras",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      eraKicker: "THE TIMELINE",
      blurb: "The Timmverse — one continuous animated world, from the noir rooftops of Gotham to the neon future of Neo-Gotham.",
      banner: "https://live.metahub.space/background/medium/tt0275137/img",
      eras: [
        { era: "Gotham", kind: "series",
          titles: [ { t: "Batman: The Animated Series", id: "tt0103359" },
                    { t: "The New Batman Adventures", id: "tt0118266" } ] },
        { era: "Metropolis & the League", kind: "series",
          titles: [ { t: "Superman: The Animated Series", id: "tt0115378" },
                    { t: "Justice League", id: "tt0275137" },
                    { t: "Justice League Unlimited", id: "tt6025022" },
                    { t: "Static Shock", id: "tt0247729" } ] },
        { era: "The Future", kind: "series",
          titles: [ { t: "Batman Beyond", id: "tt0147746" },
                    { t: "The Zeta Project", id: "tt0260662" } ] }
      ],
      rails: [
        { title: "The Films", kind: "movie",
          titles: [ { t: "Batman: Mask of the Phantasm", id: "tt0106364" },
                    { t: "Batman & Mr. Freeze: SubZero", id: "tt0143127" },
                    { t: "Batman Beyond: Return of the Joker", id: "tt0233298" },
                    { t: "Batman: Mystery of the Batwoman", id: "tt0346578" } ] }
      ],
      firstWatch: "Batman: The Animated Series", firstWatchKind: "series",
      firstWatchLabel: "Begin in Gotham",
      seriesQueries: [ "batman the animated series", "the new batman adventures",
                       "superman the animated series", "justice league",
                       "justice league unlimited", "static shock", "batman beyond",
                       "the zeta project" ],
      movieQueries: [ "batman mask of the phantasm", "batman mr freeze subzero",
                      "batman beyond return of the joker", "batman mystery of the batwoman" ],
      chips: [ { t: "8 Shows", ic: "movies" }, { t: "4 Films", ic: "movies" }, { t: "Comics", ic: "comics" } ] },
    // the MAGAZINE template: Jump publishes MANGA — no anime/film queries at all
    // (Hemanth 2026-07-12 ruling). Third form 2026-07-18 (A5, fresh design on Hemanth's order —
    // the mock-lineage archive concept is DEAD): THE LONG RUN — the magazine as velocity, its
    // history drawn as to-scale serialization strokes. MAL magazine 83 stays the registry spine
    // (the ONLY database with a serialization axis); AniList carries the ART — every flagship
    // below is id-pinned and was live-verified against AniList GraphQL on 2026-07-18, with the
    // baked dates taken from AniList's own records, never from memory. Two impostors caught at
    // verification and handled: the bare "Kinnikuman" search lands the 2001 sequel (dropped),
    // and JoJo's pin is Part 1 — so it enters as Phantom Blood with Part 1's real 1986–87 run.
    { name: "Weekly Shonen Jump", c1: "#3a1414", category: "magazine",
      // Wikipedia lead, fetched 2026-07-18 (verbatim sentences) — sourced copy, never self-written
      blurb: "Weekly Shōnen Jump is a weekly shōnen manga anthology published in Japan by Shueisha under the Jump line of magazines. It is one of the longest-running manga magazines, with the first issue being released on July 11, 1968.",
      blurbSource: "Wikipedia — Weekly Shōnen Jump",
      banner: "https://upload.wikimedia.org/wikipedia/en/0/02/Jump-Cover-1.jpg",
      // MAL's serialization registry pin (myanimelist.net/manga/magazine/83) — Jikan rides it
      malMagazineId: 83,
      // verified PRINT history (Wikipedia, checked 2026-07-18) — print facts stay separate
      // from MAL member counts, always
      milestones: [
          { year: "1968", fact: "launches July 11 at 105,000 copies" },
          { year: "1995", fact: "peaks at 6.53 million copies a week" },
          { year: "2026", fact: "still above a million copies weekly" } ],
      // THE FLAGSHIPS — the curated landmark serializations. Feed the run chart and every
      // offline state. AniList id-pinned (al) with baked covers (s4.anilist.co, IPv4-pinned
      // host); from/to/publishing are AniList's own dates as returned at verification.
      flagships: [
          { t: "KochiKame",                          a: "Osamu Akimoto",     al: 30733,  from: 1976, to: 2021, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx30733-QmyPwBhjbgyX.jpg" },
          { t: "Ring ni Kakero",                     a: "Masami Kurumada",   al: 44231,  from: 1977, to: 1981, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/b44231-AOphgOiTzTEL.jpg" },
          { t: "Dr. Slump",                          a: "Akira Toriyama",    al: 30796,  from: 1980, to: 1984, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30796-PFF4n7Y2spz9.png" },
          { t: "Captain Tsubasa",                    a: "Yoichi Takahashi",  al: 31789,  from: 1981, to: 1988, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx31789-qiNlHJDMJncz.png" },
          { t: "Fist of the North Star",             a: "Buronson & Hara",   al: 31149,  from: 1983, to: 1988, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx31149-TOwqxOlY1Zs0.jpg" },
          { t: "Dragon Ball",                        a: "Akira Toriyama",    al: 30042,  from: 1984, to: 1995, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30042-4SetGiEbGc9x.jpg" },
          { t: "Saint Seiya",                        a: "Masami Kurumada",   al: 31045,  from: 1985, to: 1990, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx31045-YUcWBMk7RpeK.png" },
          { t: "JoJo — Phantom Blood",               a: "Hirohiko Araki",    al: 31517,  from: 1986, to: 1987, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx31517-Y3pL6OTH74Iq.png" },
          { t: "Slam Dunk",                          a: "Takehiko Inoue",    al: 30051,  from: 1990, to: 1996, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30051-5KJyPlO7z5F4.png" },
          { t: "Yu Yu Hakusho",                      a: "Yoshihiro Togashi", al: 30053,  from: 1990, to: 1994, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30053-wCR6xyGzeUYo.png" },
          { t: "Rurouni Kenshin",                    a: "Nobuhiro Watsuki",  al: 30022,  from: 1994, to: 1999, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30022-jxTlViun1o10.jpg" },
          { t: "One Piece",                          a: "Eiichiro Oda",      al: 30013,  from: 1997, to: 0, publishing: true, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30013-BeslEMqiPhlk.jpg" },
          { t: "Hunter x Hunter",                    a: "Yoshihiro Togashi", al: 30026,  from: 1998, to: 0, publishing: true, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30026-uCvXMudMzmwI.jpg" },
          { t: "Naruto",                             a: "Masashi Kishimoto", al: 30011,  from: 1999, to: 2014, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx30011-9yUF1dXWgDOx.jpg" },
          { t: "Bleach",                             a: "Tite Kubo",         al: 30012,  from: 2001, to: 2016, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30012-1epmVfTSv2rr.png" },
          { t: "Eyeshield 21",                       a: "Inagaki & Murata",  al: 30043,  from: 2002, to: 2009, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30043-b2SqUjwSzfIH.png" },
          { t: "Death Note",                         a: "Ohba & Obata",      al: 30021,  from: 2003, to: 2006, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30021-FE6kmrfpuKyb.jpg" },
          { t: "Bakuman",                            a: "Ohba & Obata",      al: 39711,  from: 2008, to: 2012, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx39711-tjPWXT1AW321.jpg" },
          { t: "Haikyu!!",                           a: "Haruichi Furudate", al: 65243,  from: 2012, to: 2020, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx65243-mR4MnJFmfaOF.png" },
          { t: "Assassination Classroom",            a: "Yusei Matsui",      al: 69883,  from: 2012, to: 2016, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx69883-zDt4DUXkQS5N.png" },
          { t: "My Hero Academia",                   a: "Kohei Horikoshi",   al: 85486,  from: 2014, to: 2024, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx85486-INqnYx8gL3eX.jpg" },
          { t: "Demon Slayer: Kimetsu no Yaiba",     a: "Koyoharu Gotouge",  al: 87216,  from: 2016, to: 2020, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx87216-c9bSNVD10UuD.png" },
          { t: "The Promised Neverland",             a: "Shirai & Demizu",   al: 87423,  from: 2016, to: 2020, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx87423-gPNtu8QbGped.jpg" },
          { t: "Dr. Stone",                          a: "Inagaki & Boichi",  al: 98416,  from: 2017, to: 2024, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/b98416-L44f4idEGMAX.jpg" },
          { t: "Jujutsu Kaisen",                     a: "Gege Akutami",      al: 101517, from: 2018, to: 2024, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx101517-H3TdM3g5ZUe9.jpg" },
          { t: "Sakamoto Days",                      a: "Yuto Suzuki",       al: 125828, from: 2020, to: 0, publishing: true, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx125828-p78Z8SflkfmO.jpg" },
          { t: "Blue Box",                           a: "Kouji Miura",       al: 132182, from: 2021, to: 2026, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx132182-maXh2QzYPrqR.jpg" },
          { t: "Kagurabachi",                        a: "Takeru Hokazono",   al: 169355, from: 2023, to: 0, publishing: true, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx169355-5pqzh1Wb4NOQ.png" } ],
      // THE CURRENT LINEUP — the manga in serialization RIGHT NOW. Membership comes from
      // Wikipedia's current-series table (checked 2026-07-18) — no network needed, no MAL
      // lag; AniList supplies id + cover, each pin live-verified with the start year matched
      // against Wikipedia's premiere date. Burn the Witch rides the 2020 season entry
      // (116827), not the 2018 pilot one-shot; RuriDragon rides the 2022 serial (150440),
      // not the 2020 one-shot — both impostors caught at verification.
      currentLineup: [
          { t: "One Piece",                 a: "Eiichiro Oda",          since: "July 1997",      y: 1997, al: 30013,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30013-BeslEMqiPhlk.jpg" },
          { t: "Hunter x Hunter",           a: "Yoshihiro Togashi",     since: "March 1998",     y: 1998, al: 30026,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30026-uCvXMudMzmwI.jpg" },
          { t: "Burn the Witch",            a: "Tite Kubo",             since: "August 2020",    y: 2020, al: 116827, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx116827-dborNlGJ9K8G.png" },
          { t: "Me & Roboco",               a: "Shuhei Miyazaki",       since: "July 2020",      y: 2020, al: 119499, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx119499-bO0ef8QxQozs.png" },
          { t: "Sakamoto Days",             a: "Yuto Suzuki",           since: "November 2020",  y: 2020, al: 125828, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx125828-p78Z8SflkfmO.jpg" },
          { t: "Witch Watch",               a: "Kenta Shinohara",       since: "February 2021",  y: 2021, al: 128896, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx128896-VJfCBLFkm4Lb.jpg" },
          { t: "Akane-banashi",             a: "Suenaga & Moue",        since: "February 2022",  y: 2022, al: 144866, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx144866-ummLIg6x419I.jpg" },
          { t: "RuriDragon",                a: "Masaoki Shindo",        since: "June 2022",      y: 2022, al: 150440, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx150440-QdBFoMh4YHsK.jpg" },
          { t: "Nue's Exorcist",            a: "Kota Kawae",            since: "May 2023",       y: 2023, al: 163497, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx163497-HNrC3KDTVxW5.jpg" },
          { t: "Kagurabachi",               a: "Takeru Hokazono",       since: "September 2023", y: 2023, al: 169355, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx169355-5pqzh1Wb4NOQ.png" },
          { t: "Ultimate Exorcist Kiyoshi", a: "Shoichi Usui",          since: "June 2024",      y: 2024, al: 178509, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx178509-Iyc0m68gQv8U.png" },
          { t: "Ichi the Witch",            a: "Nishi & Usazaki",       since: "September 2024", y: 2024, al: 180752, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx180752-13CVO310XiEs.jpg" },
          { t: "Shinobi Undercover",        a: "Takegushi & Mitarashi", since: "September 2024", y: 2024, al: 180881, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx180881-2NqZaTxr69jc.jpg" },
          { t: "Someone Hertz",             a: "Ei Yamano",             since: "September 2025", y: 2025, al: 198817, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx198817-6WrNkObMtjkN.jpg" },
          { t: "Under Doctor",              a: "Kyo Tanimoto",          since: "January 2026",   y: 2026, al: 206835, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx206835-PWOgOewxlpUi.jpg" },
          { t: "Kinato's Magic",            a: "Kento Amemiya",         since: "February 2026",  y: 2026, al: 207142, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx207142-T7DIOPtJLk50.jpg" },
          { t: "Class 2-B Hero Destroyerz", a: "Hideaki Sorachi",       since: "April 2026",     y: 2026, al: 210041, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx210041-KBPe9Ez3iBHI.jpg" },
          { t: "Roku's House of Oddities",  a: "Atsushi Nakamura",      since: "April 2026",     y: 2026, al: 210422, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx210422-k89sP63t4eYv.png" },
          { t: "Drawn to the Fire",         a: "Masayoshi Satosho",     since: "April 2026",     y: 2026, al: 210838, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx210838-q2ytgCW6cYQB.png" },
          { t: "Animal Signal",             a: "Haruhara & Tsutsui",    since: "June 2026",      y: 2026, al: 213019, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx213019-Bw406cjOq5GP.jpg" },
          { t: "Hal Formula",               a: "Kento Terasaka",        since: "June 2026",      y: 2026, al: 213229, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx213229-09asKdNf3Z3e.png" },
          { t: "Cannon Master",             a: "Reiya Machida",         since: "June 2026",      y: 2026, al: 213403, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx213403-fNxRdRYoPtEc.jpg" } ],
      readQueries: [ "One Piece", "Naruto", "Bleach", "Dragon Ball", "Hunter x Hunter",
                     "My Hero Academia", "Jujutsu Kaisen", "Demon Slayer: Kimetsu no Yaiba",
                     "Chainsaw Man", "Death Note" ],
      chips: [ { t: "50+ Manga", ic: "manga" }, { t: "Weekly", ic: "manga" }, { t: "Since 1968", ic: "manga" } ] },
    // ERAS template — THE FLEET: the chronology in five formations. Cinemeta traps
    // (researched): TAS needs its own query; films III/V/VI need numbered queries;
    // 'Into Darkness'/'Beyond' carry no colon while the TNG-era films do.
    { name: "Star Trek", c1: "#10141f", category: "eras",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      eraKicker: "THE FLEET",
      blurb: "The final frontier, charted end to end — every series and film from Kirk's five-year mission to the streaming age.",
      banner: "https://live.metahub.space/background/medium/tt0796366/img",
      eras: [
        { era: "The Classic Era", kind: "series",
          titles: [ "Star Trek", "Star Trek: The Animated Series", "Star Trek: The Next Generation",
                    "Star Trek: Deep Space Nine", "Star Trek: Voyager", "Star Trek: Enterprise" ] },
        { era: "The Streaming Era", kind: "series",
          titles: [ "Star Trek: Discovery", "Star Trek: Picard", "Star Trek: Lower Decks",
                    "Star Trek: Prodigy", "Star Trek: Strange New Worlds",
                    { t: "Star Trek: Short Treks", id: "tt9059594" },
                    { t: "Star Trek: Starfleet Academy", id: "tt8622160" } ] },
        { era: "The Original Crew", kind: "movie",
          titles: [ "Star Trek: The Motion Picture", "Star Trek II: The Wrath of Khan",
                    "Star Trek III: The Search for Spock", "Star Trek IV: The Voyage Home",
                    "Star Trek V: The Final Frontier", "Star Trek VI: The Undiscovered Country" ] },
        { era: "The Next Generation", kind: "movie",
          titles: [ "Star Trek: Generations", "Star Trek: First Contact",
                    "Star Trek: Insurrection", "Star Trek: Nemesis" ] },
        { era: "The Kelvin Timeline", kind: "movie",
          titles: [ "Star Trek", "Star Trek Into Darkness", "Star Trek Beyond" ] }
      ],
      // Section 31 is movie-type in Cinemeta (2025 TV film) — it rides a rail, not an era
      rails: [
        { title: "The Streaming Films", kind: "movie",
          titles: [ { t: "Star Trek: Section 31", id: "tt9603060" } ] }
      ],
      // GC umbrella tag verified 2026-07-13 (329 releases — IDW's whole fleet)
      comics: { tag: "star-trek", tagId: 691,
                line: "The fleet in panels — IDW's voyages, Year Five to Lower Decks." },
      firstWatch: "Star Trek", firstWatchKind: "series",
      firstWatchLabel: "Begin the five-year mission",
      seriesQueries: [ "Star Trek", "Star Trek The Animated Series", "star trek short treks",
                       "star trek starfleet academy" ],
      movieQueries: [ "Star Trek", "Star Trek III", "Star Trek V", "Star Trek VI",
                      "star trek section 31" ],
      chips: [ { t: "13 Shows", ic: "movies" }, { t: "14 Films", ic: "movies" }, { t: "Novels", ic: "books" } ] },
    // the GALAXY template: the Skywalker Saga as three curated trilogies + the standalone
    // stories + the series in live/animated rails. Canon names verified against Cinemeta's
    // own catalog 2026-07-12 (modern shows don't CONTAIN "Star Wars" — Andor, The
    // Mandalorian — which is exactly why the name-relevance generic page starved empty).
    { name: "Star Wars", c1: "#14181c", category: "galaxy",
      archived: true,   // benched 2026-07-18 — returns when custom-made
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
      // animation deepened 2026-07-13: the 2003 Tartakovsky Clone Wars id-pinned (its 2008
      // namesake OUTRANKS it in search), the three Tales anthologies, Young Jedi Adventures
      animatedShows: [ "Star Wars: The Clone Wars", "Star Wars: Rebels",
                       "Star Wars: The Bad Batch", "Star Wars: Resistance", "Star Wars: Visions",
                       { t: "Star Wars: Clone Wars", id: "tt0361243" },
                       { t: "Star Wars: Tales of the Jedi", id: "tt20723374" },
                       { t: "Star Wars: Tales of the Empire", id: "tt32019314" },
                       { t: "Star Wars: Tales of the Underworld", id: "tt36414431" },
                       { t: "Star Wars: Young Jedi Adventures", id: "tt20674124" } ],
      // GC umbrella tag verified 2026-07-13 (1101 releases — the biggest archive on the site)
      comics: { tag: "star-wars", tagId: 203,
                line: "The galaxy in panels — Vader, Aphra, the High Republic and beyond." },
      firstWatch: "Star Wars: Episode IV - A New Hope",
      movieQueries: [ "Star Wars", "Rogue One: A Star Wars Story", "Solo: A Star Wars Story" ],
      seriesQueries: [ "Star Wars", "The Mandalorian", "Andor", "Obi-Wan Kenobi",
                       "The Book of Boba Fett", "Ahsoka", "The Acolyte", "Skeleton Crew",
                       "clone wars", "tales of the jedi", "tales of the empire",
                       "tales of the underworld", "young jedi adventures" ],
      chips: [ { t: "11 Films", ic: "movies" }, { t: "17 Shows", ic: "movies" }, { t: "Novels", ic: "books" } ] },
    { name: "Dune", c1: "#3a2a18", category: "saga",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "Frank Herbert's world, end to end — the novels, the films, the graphic novel.",
      banner: "https://live.metahub.space/background/medium/tt15239678/img",
      novels: [ "Dune", "Dune Messiah", "Children of Dune", "God Emperor of Dune",
                "Heretics of Dune", "Chapterhouse: Dune" ],
      // expansion 2026-07-13: every same-name Dune id-pinned (1984 Lynch vs 2021 vs Part
      // Three 2026-upcoming; the 2000 miniseries vs Prophecy) — name-match can't win these
      films:  [ { t: "Dune", id: "tt1160419" }, "Dune: Part Two",
                { t: "Dune: Part Three", id: "tt31378509" },
                { t: "Dune", id: "tt0087182" } ],
      shows:  [ "Dune: Prophecy", { t: "Dune", id: "tt0142032" },
                { t: "Children of Dune", id: "tt0287839" } ],
      movieQueries: [ "Dune" ],
      seriesQueries: [ "Dune", "children of dune" ],
      // GC tag verified 2026-07-13 (52 releases — BOOM!'s House books live under it too)
      comics: { tag: "dune", tagId: 11202,
                line: "Arrakis in panels — the graphic adaptations and the House books." },
      chips: [ { t: "6 Novels", ic: "books" }, { t: "4 Films", ic: "movies" }, { t: "3 Shows", ic: "movies" } ] },

    // ===== the 2026-07-12 second expansion (10 more, researched via parallel agents:
    // every Cinemeta name query-verified, every banner HEAD-checked 200 on pinned hosts) =====
    { name: "The Witcher", c1: "#26221c", category: "saga",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "Geralt's path through a Continent of monsters — the saga, the Netflix shows, and the animated film, in one place.",
      banner: "https://live.metahub.space/background/medium/tt5180504/img",
      // + Crossroads of Ravens (English Sep 2025, Orbit — Geralt's-youth prequel, shelved
      // last per publication order; expansion 2026-07-13)
      novels: [ "The Last Wish", "Sword of Destiny", "Blood of Elves", "The Time of Contempt",
                "Baptism of Fire", "The Tower of Swallows", "The Lady of the Lake", "Season of Storms",
                "Crossroads of Ravens" ],
      // + the two released Netflix films (2026-07-13). The Rats trap: movie-type, and it
      // only surfaces on the query "the rats witcher" — both pinned anyway
      films:  [ "The Witcher: Nightmare of the Wolf",
                { t: "The Witcher: Sirens of the Deep", id: "tt15495150" },
                { t: "The Rats: A Witcher Tale", id: "tt28283547" } ],
      shows:  [ "The Witcher", "The Witcher: Blood Origin" ],
      movieQueries: [ "witcher", "witcher sirens of the deep", "the rats witcher" ],
      seriesQueries: [ "witcher" ],
      // GC tag verified 2026-07-13 (52 releases — the Dark Horse line)
      comics: { tag: "the-witcher", tagId: 6975,
                line: "Geralt in panels — the Dark Horse hunts." },
      chips: [ { t: "9 Novels", ic: "books" }, { t: "2 Shows", ic: "movies" }, { t: "3 Films", ic: "movies" } ] },
    { name: "Sherlock Holmes", c1: "#1f242c", category: "saga",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "The Baker Street canon — Doyle's nine books and the defining screen deductions, in one place.",
      banner: "https://live.metahub.space/background/medium/tt1475582/img",
      novels: [ "A Study in Scarlet", "The Sign of the Four", "The Adventures of Sherlock Holmes",
                "The Memoirs of Sherlock Holmes", "The Hound of the Baskervilles",
                "The Return of Sherlock Holmes", "The Valley of Fear", "His Last Bow",
                "The Case-Book of Sherlock Holmes" ],
      films:  [ "Sherlock Holmes", "Sherlock Holmes: A Game of Shadows" ],
      shows:  [ "Sherlock", "Elementary", "The Adventures of Sherlock Holmes" ],
      movieQueries: [ "sherlock holmes" ],
      seriesQueries: [ "sherlock", "elementary" ],
      chips: [ { t: "9 Books", ic: "books" }, { t: "2 Films", ic: "movies" }, { t: "3 Shows", ic: "movies" } ] },
    { name: "Jurassic Park", c1: "#1c2a20", category: "saga",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "Crichton's islands of resurrected giants — the novels, all seven films, and the animated escapes, in one place.",
      banner: "https://live.metahub.space/background/medium/tt0107290/img",
      novels: [ "Jurassic Park", "The Lost World Michael Crichton" ],
      films:  [ "Jurassic Park", "The Lost World: Jurassic Park", "Jurassic Park III",
                "Jurassic World", "Jurassic World: Fallen Kingdom", "Jurassic World: Dominion",
                "Jurassic World: Rebirth" ],
      shows:  [ "Jurassic World: Camp Cretaceous", "Jurassic World: Chaos Theory" ],
      movieQueries: [ "jurassic" ],
      seriesQueries: [ "jurassic" ],
      chips: [ { t: "2 Novels", ic: "books" }, { t: "7 Films", ic: "movies" }, { t: "2 Shows", ic: "movies" } ] },
    { name: "Percy Jackson", c1: "#1a2430", category: "saga",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "A demigod's quests from Camp Half-Blood to Olympus — the five books, the films, and the series, in one place.",
      banner: "https://live.metahub.space/background/medium/tt12324366/img",
      // + the Senior Year novels (expansion 2026-07-13; "Wrath..." carries NO leading "The")
      novels: [ "The Lightning Thief", "The Sea of Monsters", "The Titan's Curse",
                "The Battle of the Labyrinth", "The Last Olympian",
                "The Chalice of the Gods", "Wrath of the Triple Goddess" ],
      films:  [ "Percy Jackson & the Olympians: The Lightning Thief", "Percy Jackson: Sea of Monsters" ],
      shows:  [ "Percy Jackson and the Olympians" ],
      movieQueries: [ "percy jackson" ],
      seriesQueries: [ "percy jackson" ],
      chips: [ { t: "7 Novels", ic: "books" }, { t: "2 Films", ic: "movies" }, { t: "1 Show", ic: "movies" } ] },
    // ERAS template — THE DOSSIER: sixty years of 007 as the six Bonds. All 25 Eon titles
    // individually query-verified ("Goldfinger" carries no "James Bond" — every film has
    // its own search query).
    { name: "James Bond", c1: "#1a1a1e", category: "eras",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      eraKicker: "THE DOSSIER",
      blurb: "Sixty years of 007 — every Eon mission from Dr. No to No Time to Die, six Bonds deep, in one place.",
      banner: "https://live.metahub.space/background/medium/tt0058150/img",
      eras: [
        { era: "Sean Connery · 1962–1971", kind: "movie",
          titles: [ "Dr. No", "From Russia with Love", "Goldfinger", "Thunderball",
                    "You Only Live Twice", "Diamonds Are Forever" ] },
        { era: "George Lazenby · 1969", kind: "movie",
          titles: [ "On Her Majesty's Secret Service" ] },
        { era: "Roger Moore · 1973–1985", kind: "movie",
          titles: [ "Live and Let Die", "The Man with the Golden Gun", "The Spy Who Loved Me",
                    "Moonraker", "For Your Eyes Only", "Octopussy", "A View to a Kill" ] },
        { era: "Timothy Dalton · 1987–1989", kind: "movie",
          titles: [ "The Living Daylights", "Licence to Kill" ] },
        { era: "Pierce Brosnan · 1995–2002", kind: "movie",
          titles: [ "GoldenEye", "Tomorrow Never Dies", "The World Is Not Enough", "Die Another Day" ] },
        { era: "Daniel Craig · 2006–2021", kind: "movie",
          titles: [ "Casino Royale", "Quantum of Solace", "Skyfall", "Spectre", "No Time to Die" ] }
      ],
      // THE FLEMING SHELF (expansion 2026-07-13): all 14 Fleming books, publication order,
      // Wikipedia-checked. "Dr. No" keeps its period; the two story collections ride at
      // their publication slots; dead US paperback retitles never used.
      novels: [ "Casino Royale", "Live and Let Die", "Moonraker", "Diamonds Are Forever",
                "From Russia with Love", "Dr. No", "Goldfinger", "For Your Eyes Only",
                "Thunderball", "The Spy Who Loved Me", "On Her Majesty's Secret Service",
                "You Only Live Twice", "The Man with the Golden Gun",
                "Octopussy and The Living Daylights" ],
      novelsTitle: "The Fleming Shelf",
      // GC umbrella tag verified 2026-07-13 (79 releases; "james-bond-007" is a Dynamite
      // sibling volume, NOT the umbrella)
      comics: { tag: "james-bond", tagId: 2111,
                line: "007 in panels — Dynamite's missions and the classic strips." },
      firstWatch: "Dr. No", firstWatchLabel: "Begin with Dr. No",
      movieQueries: [ "Dr. No", "From Russia with Love", "Goldfinger", "Thunderball",
                      "You Only Live Twice", "Diamonds Are Forever", "On Her Majesty's Secret Service",
                      "Live and Let Die", "The Man with the Golden Gun", "The Spy Who Loved Me",
                      "Moonraker", "For Your Eyes Only", "Octopussy", "A View to a Kill",
                      "The Living Daylights", "Licence to Kill", "GoldenEye", "Tomorrow Never Dies",
                      "The World Is Not Enough", "Die Another Day", "Casino Royale",
                      "Quantum of Solace", "Skyfall", "Spectre", "No Time to Die" ],
      chips: [ { t: "25 Films", ic: "movies" }, { t: "6 Eras", ic: "movies" }, { t: "14 Books", ic: "books" } ] },
    // STUDIO template — the filmography wall. Id-pinned where a remake outranks the
    // original in search (Grave of the Fireflies has a 2024 remake shadow; also Castle in
    // the Sky, Only Yesterday, Ocean Waves, Whisper of the Heart).
    { name: "Studio Ghibli", c1: "#2a3328", category: "studio",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "Hand-drawn worlds from Miyazaki, Takahata, and kin — every Ghibli feature, Nausicaä to The Boy and the Heron.",
      banner: "https://live.metahub.space/background/medium/tt0245429/img",
      filmography: [ "Nausicaä of the Valley of the Wind",
                     { t: "Castle in the Sky", id: "tt0092067" },
                     { t: "Grave of the Fireflies", id: "tt0095327" },
                     "My Neighbor Totoro", "Kiki's Delivery Service",
                     { t: "Only Yesterday", id: "tt0102587" },
                     "Porco Rosso",
                     { t: "Ocean Waves", id: "tt0108432" },
                     "Pom Poko",
                     { t: "Whisper of the Heart", id: "tt0113824" },
                     "Princess Mononoke", "My Neighbors the Yamadas", "Spirited Away",
                     "The Cat Returns", "Howl's Moving Castle", "Tales from Earthsea",
                     "Ponyo", "The Secret World of Arrietty", "From Up on Poppy Hill",
                     "The Wind Rises", "The Tale of The Princess Kaguya",
                     "When Marnie Was There", "The Boy and the Heron" ],
      firstWatch: "Spirited Away", firstWatchLabel: "Begin with Spirited Away",
      movieQueries: [ "Nausicaa of the Valley of the Wind", "Castle in the Sky",
                      "Grave of the Fireflies", "My Neighbor Totoro", "Kiki's Delivery Service",
                      "Only Yesterday", "Porco Rosso", "Ocean Waves", "Pom Poko",
                      "Whisper of the Heart", "Princess Mononoke", "My Neighbors the Yamadas",
                      "Spirited Away", "The Cat Returns", "Howl's Moving Castle",
                      "Tales from Earthsea", "Ponyo", "The Secret World of Arrietty",
                      "From Up on Poppy Hill", "The Wind Rises", "The Tale of the Princess Kaguya",
                      "When Marnie Was There", "The Boy and the Heron" ],
      chips: [ { t: "23 Films", ic: "movies" }, { t: "1984–2023", ic: "movies" } ] },
    // ERAS template — THE CANON: both shows are named "Avatar: The Last Airbender" in
    // Cinemeta (2005 animated vs 2024 live action) — the id-pins are load-bearing.
    { name: "Avatar: The Last Airbender", c1: "#16262c", category: "eras",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      eraKicker: "THE CANON",
      blurb: "One boy, four nations, a hundred-year war — the animated canon, Korra's age, and the live-action retelling.",
      banner: "https://live.metahub.space/background/medium/tt0417299/img",
      eras: [
        { era: "The Animated Canon", kind: "series",
          titles: [ { t: "Avatar: The Last Airbender", id: "tt0417299" },
                    { t: "The Legend of Korra", id: "tt1695360" } ] },
        { era: "The Live Action", kind: "series",
          titles: [ { t: "Avatar: The Last Airbender", id: "tt9018736" } ] }
      ],
      rails: [
        { title: "The Films", kind: "movie",
          titles: [ { t: "The Last Airbender", id: "tt0938283" },
                    { t: "The Legend of Aang: The Last Airbender", id: "tt18259538" } ] }
      ],
      // the canon continues on the page: the GetComics archive (Dark Horse library) as its
      // own COMICS column in the era gallery (Hemanth 2026-07-12). tagId pinned like an id-pin.
      comics: { tag: "avatar-the-last-airbender", tagId: 448,
                line: "The story continues past the finale — the Dark Horse library." },
      // CHRONICLES OF THE AVATAR (expansion 2026-07-13): all six released prequel novels,
      // series order — Awakening of Roku landed Dec 30 2025; book 7 exists but is untitled,
      // so it does not (metadata-id law: no id/title, no entry).
      novels: [ "The Rise of Kyoshi", "The Shadow of Kyoshi", "The Dawn of Yangchen",
                "The Legacy of Yangchen", "The Reckoning of Roku", "The Awakening of Roku" ],
      novelsTitle: "Chronicles of the Avatar",
      firstWatch: "Avatar: The Last Airbender", firstWatchKind: "series",
      firstWatchLabel: "Begin Book One — Water",
      seriesQueries: [ "avatar the last airbender", "the legend of korra" ],
      movieQueries: [ "the last airbender" ],
      chips: [ { t: "3 Shows", ic: "movies" }, { t: "2 Films", ic: "movies" }, { t: "6 Novels", ic: "books" }, { t: "Comics", ic: "comics" } ] },
    { name: "Attack on Titan", c1: "#2a2018", category: "anime",
      archived: true,   // benched 2026-07-18 — returns when custom-made
      blurb: "Humanity's last walls and the titans beyond them — Isayama's manga, the landmark anime, and the compilation films.",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/53390-6Uru5rrjh8zv.jpg",
      // expansion 2026-07-13 (AniList/Kitsu-verified): No Regrets (AL 85199) + Before the
      // Fall (AL 85312 — the same-title NOVEL outranks the manga on bare search) + the
      // Final Chapters specials (Kitsu 46038/47132, invisible on the bare query)
      readQueries: [ "Attack on Titan", "Attack on Titan: No Regrets",
                     "Attack on Titan: Before the Fall" ],
      seriesQueries: [ "attack on titan", "attack on titan final chapters" ],
      movieQueries: [ "attack on titan" ],
      chips: [ { t: "3 Manga", ic: "manga" }, { t: "1 Anime", ic: "movies" }, { t: "Films", ic: "movies" } ] }
];

// Hemanth 2026-07-13: the Cognitive Atlas belongs at the front. Preserve One Piece as
// the collection's first door, then promote Cosmere to slot 2 in both the carousel and Hall.
function promoteUniverse(name, slot) {
    for (var i = 0; i < universes.length; i++) {
        if (universes[i].name !== name) continue;
        var promoted = universes.splice(i, 1)[0];
        universes.splice(slot, 0, promoted);
        return;
    }
}
promoteUniverse("Cosmere", 1);

// ════════════════════════════════════════════════════════════════════════════════════════
// THE SHELF RULING (Hemanth, 2026-07-18): only CUSTOM-MADE pages stay on the shelf.
// "I will slowly custom make them and bring them back one by one." Everything riding a
// shared template is benched — flagged `archived: true` above with EVERY pin intact
// (curation is expensive; nothing is deleted). The bench is invisible to the carousel and
// the Hall of Worlds; configFor/categoryFor still resolve benched entries so template
// harnesses and any stored door keep working. To bring a universe back: build its custom
// page, then delete its archived flag.
// On the shelf now (5): One Piece · Cosmere · Marvel Cinematic Universe · Dragon Ball ·
// Weekly Shonen Jump. (Marvel counts as custom — Hemanth's ratification 2026-07-18; its
// phase panels dropped their box-office descriptions the same day.)
// ════════════════════════════════════════════════════════════════════════════════════════
var archive = universes.filter(function(u) { return u.archived === true; });
universes = universes.filter(function(u) { return u.archived !== true; });

// (the placeholder bench is empty — the 2026-07-12 commission promoted everything;
//  future curations land here first if their page needs work before surfacing)
var placeholders = [];

// which page TEMPLATE a universe opens into: "cinematic" → CinematicPage (the MCU
// Fandom-wiki phase template, Marvel-only for now); everything else → the generic
// name/query-driven UniversePage.
function categoryFor(name) {
    var all = universes.concat(archive);
    for (var i = 0; i < all.length; i++)
        if (all[i].name === name) return all[i].category || "anime";
    return "anime";
}

// the curated entry for a universe (the page's banner + search hints live here,
// beside the carousel data — ONE curation point). Empty object when unknown.
function configFor(name) {
    var q = String(name || "").toLowerCase();
    var all = universes.concat(archive);
    for (var i = 0; i < all.length; i++)
        if (all[i].name.toLowerCase() === q) return all[i];
    return {};
}
