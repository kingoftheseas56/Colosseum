/* Authentic local artwork; fixture metadata only. */
window.MockCatalog = {
  tankoban: [
    ["One Piece", "manga-covers/onepiece.jpg", "manga", "Adventure", 64],
    ["Berserk", "manga-covers/berserk.jpg", "manga", "Fantasy", 42],
    ["Vinland Saga", "manga-covers/vinland.jpg", "manga", "Adventure", 25],
    ["Vagabond", "manga-covers/vagabond.png", "manga", "Drama", 0],
    ["Chainsaw Man", "manga-covers/chainsaw.png", "manga", "Fantasy", 0],
    ["Monster", "manga-covers/monster.jpg", "manga", "Mystery", 0],
    ["Saga", "comic-covers/saga.jpg", "comics", "Fantasy", 31],
    ["Invincible", "comic-covers/invincible.jpg", "comics", "Adventure", 18],
    ["Watchmen", "comic-covers/watchmen.jpg", "comics", "Mystery", 0],
    ["The Sandman", "comic-covers/sandman.jpg", "comics", "Fantasy", 0],
    ["Hellboy", "comic-covers/hellboy.jpg", "comics", "Fantasy", 0],
    ["Sin City", "comic-covers/sincity.jpg", "comics", "Mystery", 0],
  ],
  biblio: [
    ["Dune", "@feria/assets/dune-book.jpg", "book", "Science fiction", 43],
    [
      "Project Hail Mary",
      "@feria/assets/phm-cover.jpg",
      "book",
      "Science fiction",
      28,
    ],
  ],
  theatre: [
    [
      "Dune: Part Two",
      "theatre-posters/dune.jpg",
      "movies",
      "Science fiction",
      43,
    ],
    ["Oppenheimer", "theatre-posters/oppenheimer.jpg", "movies", "Drama", 68],
    ["The Batman", "theatre-posters/batman.jpg", "movies", "Mystery", 0],
    [
      "Spider-Man: Across the Spider-Verse",
      "theatre-posters/spiderverse.jpg",
      "movies",
      "Animation",
      0,
    ],
    [
      "Godzilla Minus One",
      "theatre-posters/godzilla.jpg",
      "movies",
      "Science fiction",
      0,
    ],
    [
      "Mad Max: Fury Road",
      "theatre-posters/madmax.jpg",
      "movies",
      "Adventure",
      0,
    ],
    ["Severance", "@feria/assets/severance-poster.jpg", "shows", "Mystery", 20],
    [
      "Stranger Things",
      "@feria/assets/stranger-poster.jpg",
      "shows",
      "Fantasy",
      15,
    ],
    ["Wednesday", "@feria/assets/wednesday-poster.jpg", "shows", "Mystery", 0],
    ["Frieren", "@feria/assets/frieren-poster.jpg", "anime", "Fantasy", 0],
    ["Arcane", "@feria/assets/arcane-poster.jpg", "anime", "Fantasy", 0],
    [
      "Vinland Saga",
      "@feria/assets/vinland-poster.jpg",
      "anime",
      "Adventure",
      0,
    ],
  ],
};
Object.keys(MockCatalog).forEach(function (w) {
  MockCatalog[w] = MockCatalog[w].map(function (x, i) {
    return {
      id: w + "-" + i,
      title: x[0],
      cover:
        "../assets/" +
        (x[1][0] === "@" ? x[1].slice(1) : "fixtures/captures/" + x[1]),
      kind: x[2],
      genre: x[3],
      progress: x[4],
      world: w,
      year: 2024 - i,
      rating: (8.9 - i * 0.12).toFixed(1),
    };
  });
});
window.catalogRoute = function (c, resume) {
  if (resume)
    return c.kind === "book"
      ? "reader.book"
      : /movies|shows|anime/.test(c.kind)
        ? "player.video"
        : "reader.comic";
  return c.kind === "book"
    ? "book.detail"
    : c.kind === "comics"
      ? "comics.series"
      : c.kind === "manga"
        ? MockUI.get("rebuiltTitlePages", false)
          ? "manga.rebuilt"
          : "manga.series"
        : MockUI.get("rebuiltTitlePages", false)
          ? "theatre.rebuilt"
          : "theatre.detail";
};
window.openCatalog = function (c, resume) {
  MockUI.go(catalogRoute(c, resume), {
    id: c.id,
    title: c.title,
    cover: c.cover,
    kind: c.kind,
    world: c.world,
    mode: c.kind === "comics" ? "comic" : "manga",
  });
};

var releaseYears = {
  "One Piece": 1997,
  Berserk: 1989,
  "Vinland Saga": 2005,
  Vagabond: 1998,
  "Chainsaw Man": 2018,
  Monster: 1994,
  Saga: 2012,
  Invincible: 2003,
  Watchmen: 1986,
  "The Sandman": 1989,
  Hellboy: 1993,
  "Sin City": 1991,
  Dune: 1965,
  "Project Hail Mary": 2021,
  "Dune: Part Two": 2024,
  Oppenheimer: 2023,
  "The Batman": 2022,
  "Spider-Man: Across the Spider-Verse": 2023,
  "Godzilla Minus One": 2023,
  "Mad Max: Fury Road": 2015,
  Severance: 2022,
  "Stranger Things": 2016,
  Wednesday: 2022,
  Frieren: 2023,
  Arcane: 2021,
};
Object.keys(MockCatalog).forEach(function (w) {
  MockCatalog[w].forEach(function (c) {
    c.year =
      c.kind === "anime" && c.title === "Vinland Saga"
        ? 2019
        : releaseYears[c.title];
  });
});
