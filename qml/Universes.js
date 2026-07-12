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
      // expansion 2026-07-13 (AniList/Kitsu-verified): Ace's Story manga (AL 117802 — the
      // source NOVEL outranks it on bare search) + Fan Letter (Kitsu 49259, invisible on
      // bare "one piece")
      readQueries: [ "One Piece", "One Piece: Ace's Story" ],
      seriesQueries: [ "one piece", "one piece fan letter" ],
      chips: [ { t: "8 Manga", ic: "manga" }, { t: "2 Anime", ic: "movies" }, { t: "15 Films", ic: "movies" } ] },
    // renamed Marvel → full name (Hemanth 2026-07-13) + THE TELEVISION ACT: every Marvel
    // Studios series + the Special Presentations, release-ordered, ALL id-pinned (bare
    // names are impostor minefields — Loki/Hawkeye/Echo/What If all collide; Ms. Marvel
    // and She-Hulk sit on ADJACENT ids). X-Men '97 carries no phase (Marvel assigns none).
    { name: "Marvel Cinematic Universe", c1: "#1a2436", category: "cinematic",
      blurb: "The Marvel Cinematic Universe — decades of films and shows, grown from the comics.",
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
    { name: "Harry Potter", c1: "#221c30", category: "saga",
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
    { name: "Dragon Ball", c1: "#3a2a10", category: "anime",
      blurb: "Toriyama's world from the Dragon Radar to Ultra Instinct — the manga, four eras of anime, the films.",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30042-4aSSSOxCNWgE.jpg",
      // expansion 2026-07-13 (AniList/Kitsu-verified): Super manga (AL 86508) + DAIMA
      // (Kitsu 48108 — invisible on the bare "dragon ball" query)
      readQueries: [ "Dragon Ball", "Dragon Ball Super" ],
      seriesQueries: [ "dragon ball", "dragon ball daima" ],
      movieQueries: [ "dragon ball" ],
      chips: [ { t: "2 Manga", ic: "manga" }, { t: "6 Anime", ic: "movies" }, { t: "21 Films", ic: "movies" } ] },
    { name: "Naruto", c1: "#2a3212", category: "anime",
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
    // (Hemanth 2026-07-12). The readQueries are the ranked lineup, one flagship each.
    { name: "Weekly Shonen Jump", c1: "#3a1414", category: "magazine",
      blurb: "Shueisha's arena since 1968 — the magazine where One Piece, Naruto, Bleach and Dragon Ball fought for the reader's vote.",
      banner: "https://upload.wikimedia.org/wikipedia/en/0/02/Jump-Cover-1.jpg",
      // MAL's serialization registry pin (myanimelist.net/manga/magazine/83) — Jikan rides it;
      // the registry drives In This Issue / the all-time vote / the Back Issues era shelves
      malMagazineId: 83,
      readQueries: [ "One Piece", "Naruto", "Bleach", "Dragon Ball", "Hunter x Hunter",
                     "My Hero Academia", "Jujutsu Kaisen", "Demon Slayer: Kimetsu no Yaiba",
                     "Chainsaw Man", "Death Note" ],
      chips: [ { t: "50+ Manga", ic: "manga" }, { t: "Weekly", ic: "manga" }, { t: "Since 1968", ic: "manga" } ] },
    // ERAS template — THE FLEET: the chronology in five formations. Cinemeta traps
    // (researched): TAS needs its own query; films III/V/VI need numbered queries;
    // 'Into Darkness'/'Beyond' carry no colon while the TNG-era films do.
    { name: "Star Trek", c1: "#10141f", category: "eras",
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
    { name: "Pokémon", c1: "#2c2418", category: "anime",
      blurb: "Twenty-six seasons of Ash and Pikachu, the Horizons generation, the theatrical films, and the Adventures manga.",
      banner: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30928-YSyv6mRbR73n.jpg",
      readQueries: [ "Pokémon Adventures" ],
      seriesQueries: [ "pokemon", "pokemon horizons", "pokemon concierge" ],
      movieQueries: [ "pokemon", "detective pikachu" ],
      chips: [ { t: "2 Shows", ic: "movies" }, { t: "23 Films", ic: "movies" }, { t: "Manga", ic: "manga" } ] },
    { name: "Attack on Titan", c1: "#2a2018", category: "anime",
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
