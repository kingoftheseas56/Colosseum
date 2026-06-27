// Catalog.js — the Tankoban catalog data in ONE place: featured slides, the blended Continue row,
// the split Top-10s, and the split genre grids (each genre carries its OWN representative cover for
// the cover-mosaic). TankobanWorld.qml renders from these.
//
// Covers/banners are REMOTE URLs (manga covers/banners = AniList · comic covers = iTunes hi-res).
// The native launcher (Colosseum/native/) installs an on-disk HTTP cache, so each URL downloads ONCE
// then serves from local disk = instant, persists across restarts, LRU-evicts. That is the method
// that scales to a real catalog (thousands of covers across pages) — NOT bundling. Run via the
// launcher, not qml.exe, to get the cache. (Live-API swap later re-points these strings.)
.pragma library

// two cover palettes so a BLENDED row reads which-is-which without a badge: manga = plum, comic = brown
var MANGA_C1 = "#532f49", MANGA_C2 = "#1d121b";
var COMIC_C1 = "#6a4a32", COMIC_C2 = "#241813";
var THEATRE_C1 = "#33445d", THEATRE_C2 = "#0c1118";

var featured = [
    { title: "One Piece",   art: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg", blurb: "Luffy's run for the Grand Line — the manga that anchors the whole catalogue.", ghost: "M", c1: "#7a2f49", c2: "#1d121b" },
    { title: "Saga",        art: "https://is1-ssl.mzstatic.com/image/thumb/Publication4/v4/23/03/9c/23039c5b-155e-0ae4-36b3-d3407b07420c/AUG120491.jpg/2000x2000bb.jpg", artKind: "poster", blurb: "Vaughan & Staples' sweeping space-opera comic — the flagship of the indie shelf.", ghost: "C", c1: "#7a4a2f", c2: "#241813" },
    { title: "Berserk",     art: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30002-3TuoSMl20fUX.jpg", blurb: "Miura's dark-fantasy landmark — Top Rated on the manga charts.", ghost: "M", c1: "#5a2f3f", c2: "#180f14" },
    { title: "Invincible",  art: "https://is1-ssl.mzstatic.com/image/thumb/Publication124/v4/7c/73/8d/7c738d92-dea3-7901-7ef8-962db5966470/Invincible_Compendium01.jpg/2000x2000bb.jpg", artKind: "poster", blurb: "Kirkman's superhero saga — Most Popular on the comics shelf.", ghost: "C", c1: "#5a3f2f", c2: "#1a120b" }
];

// (Continue is no longer a static list — it comes live from the Progress store, written
//  by the player and the manga reader. See ProgressStore / the `Progress` context property.)

var topManga = [
    { caption: "One Piece",         cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30013-BeslEMqiPhlk.jpg", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "Berserk",           cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30002-Cul4OeN7bYtn.jpg", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "Vinland Saga",      cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30642-0mjRDkf4THpo.jpg", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "Vagabond",          cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30656-9mW113O7rDnA.png", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "Chainsaw Man",      cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx105778-euxXZEIfDY2u.png", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "Monster",           cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/93285-mnjMdil9LnNT.jpg", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "Slam Dunk",         cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30051-5KJyPlO7z5F4.png", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "Solo Leveling",     cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx105398-b673Vt5ZSuz3.jpg", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "20th Century Boys",  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30003-E84fwIh22LAQ.jpg", c1: MANGA_C1, c2: MANGA_C2 },
    { caption: "Jujutsu Kaisen",    cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx101517-H3TdM3g5ZUe9.jpg", c1: MANGA_C1, c2: MANGA_C2 }
];

var topComics = [
    { caption: "Invincible",      cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication124/v4/7c/73/8d/7c738d92-dea3-7901-7ef8-962db5966470/Invincible_Compendium01.jpg/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "The Sandman",     cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication122/v4/98/e0/6f/98e06ff4-2cdc-f5f2-4fde-b3a7417c2a49/T2187500018301.jpg/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "Saga",            cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication4/v4/23/03/9c/23039c5b-155e-0ae4-36b3-d3407b07420c/AUG120491.jpg/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "Watchmen",        cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication113/v4/5e/70/d9/5e70d95a-55be-e162-7149-31305a31ed87/T2013000018301.jpg/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "Sin City",        cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication125/v4/f6/d3/9f/f6d39f2f-1ccc-605c-93ea-8a9638cbec05/9781506722894.d.jpg/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "Hellboy",         cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication118/v4/e2/87/bd/e287bd85-1ddc-1f35-4fb1-0ba4f4634717/9781506706870.jpg/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "Paper Girls",     cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication49/v4/64/29/1b/64291b95-1060-6502-23f2-8367ed9c4c00/PaperGirls_Vol01-1.png/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "East of West",    cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/ff/c8/0a/ffc80acf-c295-c08d-b338-b2b0ce5f46fa/east-of-west-2013-vol-1-cover.jpg/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "Black Hammer",    cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication123/v4/e8/b2/a8/e8b2a8bd-85a4-cc14-d63c-94fe6b68e312/3002803.jpg/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 },
    { caption: "Descender",       cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication118/v4/89/86/09/898609f6-e118-ef63-c751-decbba39ee3e/Descender_Boook01-1.png/2000x2000bb.jpg", c1: COMIC_C1, c2: COMIC_C2 }
];

// each genre carries its OWN representative cover (distinct per genre) for the cover-mosaic.
var genresManga = [
    { name: "Shounen",   count: 1240, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx30011-9yUF1dXWgDOx.jpg", c1: "#d05a30", c2: "#4a1e10" },
    { name: "Seinen",    count: 680,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30656-9mW113O7rDnA.png", c1: "#4a6478", c2: "#1a2832" },
    { name: "Romance",   count: 920,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30102-MJuQ0e0k2CgU.png", c1: "#c93f8a", c2: "#5a1640" },
    { name: "Action",    count: 1510, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx53390-1RsuABC34P9D.jpg", c1: "#c9683f", c2: "#5a2816" },
    { name: "Comedy",    count: 1100, cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30044-vPmoz3vTPvZs.jpg", c1: "#7a9a3f", c2: "#2e3a16" },
    { name: "Drama",     count: 870,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx85135-11OOnyaqV71k.png", c1: "#9a5a4f", c2: "#36201c" },
    { name: "Shoujo",    count: 540,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30030-zBHKa3yHdtnM.png", c1: "#d05a9a", c2: "#4a1a36" },
    { name: "Josei",     count: 210,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30028-VJqBC1ar6AxE.png", c1: "#a05a8a", c2: "#3a1a30" },
    { name: "Isekai",    count: 430,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx165131-R4pedWdbZiAW.jpg", c1: "#3fa0b0", c2: "#163e46" },
    { name: "School",    count: 360,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx69883-zDt4DUXkQS5N.png", c1: "#8aa03f", c2: "#323a16" },
    { name: "Magic",     count: 290,  cover: "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx118586-CXKgWikBFQgS.jpg", c1: "#9a5ac9", c2: "#36205a" }
];

var genresComics = [
    { name: "Superhero",    count: 2100, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication/v4/fd/31/c6/fd31c66a-0e3e-537e-a9a8-f6408aed23ae/BMY1_cover.jpg/2000x2000bb.jpg", c1: "#c9533f", c2: "#5a1e16" },
    { name: "Sci-Fi",       count: 740,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication4/v4/c9/76/af/c976af8e-8774-167a-aa3e-8e2e5f233ecd/JAN120485.jpg/2000x2000bb.jpg", c1: "#3f6fc9", c2: "#16285a" },
    { name: "Action",       count: 1320, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication113/v4/61/a6/ff/61a6ff61-873e-40bf-bdc0-260c4072cb9a/BoysOmniVol1-ov-NOTFINAL.jpg/2000x2000bb.jpg", c1: "#c9683f", c2: "#5a2816" },
    { name: "Horror",       count: 560,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication1/v4/6a/70/4f/6a704f5d-f21f-82a7-277b-42ba120bfcdb/APR110291.jpeg/2000x2000bb.jpg", c1: "#8a4fc9", c2: "#2c1a5a" },
    { name: "Crime",        count: 320,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication125/v4/f6/d3/9f/f6d39f2f-1ccc-605c-93ea-8a9638cbec05/9781506722894.d.jpg/2000x2000bb.jpg", c1: "#c99e3f", c2: "#5a4316" },
    { name: "Fantasy",      count: 700,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication/v4/ac/50/e6/ac50e6bd-ae8a-ef77-7a56-dfbde104baab/Fables_v1_cover.jpg/2000x2000bb.jpg", c1: "#4f9cc9", c2: "#16384a" },
    { name: "Adventure",    count: 880,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication3/v4/be/ff/97/beff979e-c602-a873-01b4-1af1e391ef5b/PaperGirls_01-1.png/2000x2000bb.jpg", c1: "#3fae8e", c2: "#16453a" },
    { name: "Mystery",      count: 380,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication122/v4/98/e0/6f/98e06ff4-2cdc-f5f2-4fde-b3a7417c2a49/T2187500018301.jpg/2000x2000bb.jpg", c1: "#5a64b0", c2: "#23284e" },
    { name: "Supernatural", count: 520,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication118/v4/e2/87/bd/e287bd85-1ddc-1f35-4fb1-0ba4f4634717/9781506706870.jpg/2000x2000bb.jpg", c1: "#7a4fc9", c2: "#281a5a" },
    { name: "Romance",      count: 240,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication126/v4/ac/e3/dc/ace3dcbe-acfd-c64c-908e-a762a2bf0a6d/9781770467071.jpg/2000x2000bb.jpg", c1: "#c93f8a", c2: "#5a1640" },
    { name: "Thriller",     count: 470,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication118/v4/f4/6e/f7/f46ef7e6-8ddb-fb8c-788f-b3c38998e977/GideonFalls_01-1.png/2000x2000bb.jpg", c1: "#5a6470", c2: "#23282e" },
    { name: "Drama",        count: 600,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication122/v4/f4/10/25/f41025d7-b1fc-698a-684f-65611230c871/9782080249906.jpg/2000x2000bb.jpg", c1: "#9a5a4f", c2: "#36201c" },
    { name: "War",          count: 180,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication125/v4/32/a2/92/32a29229-62dd-905d-d475-0ab30206bd99/cov.jpg/2000x2000bb.jpg", c1: "#8a7a3f", c2: "#3a3216" },
    { name: "Western",      count: 130,  cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication/v4/00/f2/c6/00f2c649-6f04-cad8-cee9-14164f0c0761/JAN130468.jpg/2000x2000bb.jpg", c1: "#a06a3f", c2: "#3a2616" }
];

// Theatre starts from Harbor/TB3's Cinemeta-shaped home: hero backdrops, Continue Watching,
// a rank row, and genre entry points. Live addon rows come after the surface shape is proven.
var theatreFeatured = [
    { title: "Dune: Part Two", art: "https://live.metahub.space/background/large/tt15239678/img", blurb: "Paul Atreides unites with Chani and the Fremen while war spreads across Arrakis.", ghost: "T", c1: "#5d4633", c2: "#18110c" },
    { title: "Shogun", art: "https://live.metahub.space/background/large/tt2788316/img", blurb: "A shipwrecked pilot, a lord's dangerous gambit, and a country on the edge of war.", ghost: "S", c1: "#4c2f2a", c2: "#160d0b" },
    { title: "The Bear", art: "https://live.metahub.space/background/large/tt14452776/img", blurb: "A kitchen turns into family, pressure, grief, and service at full speed.", ghost: "S", c1: "#33445d", c2: "#0c1118" },
    { title: "Fallout", art: "https://live.metahub.space/background/large/tt12637874/img", blurb: "Vaults, wasteland, and old-world power plays collide above ground.", ghost: "S", c1: "#3f5640", c2: "#111b12" }
];

// (Theatre's "Continue Watching" is live from the Progress store now — see TheatreWorld.)

var theatreTopMovies = [
    { caption: "Dune: Part Two", cover: "https://live.metahub.space/poster/medium/tt15239678/img", c1: "#5d4633", c2: "#18110c" },
    { caption: "Oppenheimer", cover: "https://live.metahub.space/poster/medium/tt15398776/img", c1: "#65452f", c2: "#1c120b" },
    { caption: "The Batman", cover: "https://live.metahub.space/poster/medium/tt1877830/img", c1: "#394454", c2: "#0d1118" },
    { caption: "Spider-Man: Across the Spider-Verse", cover: "https://live.metahub.space/poster/medium/tt9362722/img", c1: "#6b334f", c2: "#190c14" },
    { caption: "Godzilla Minus One", cover: "https://live.metahub.space/poster/medium/tt23289160/img", c1: "#465463", c2: "#10151b" },
    { caption: "Mad Max: Fury Road", cover: "https://live.metahub.space/poster/medium/tt1392190/img", c1: "#764a2c", c2: "#1d1108" },
    { caption: "Interstellar", cover: "https://live.metahub.space/poster/medium/tt0816692/img", c1: "#33465d", c2: "#0c1118" },
    { caption: "Everything Everywhere All at Once", cover: "https://live.metahub.space/poster/medium/tt6710474/img", c1: "#5b3a64", c2: "#170d1b" },
    { caption: "John Wick: Chapter 4", cover: "https://live.metahub.space/poster/medium/tt10366206/img", c1: "#3c4a63", c2: "#0e121b" },
    { caption: "The Lord of the Rings: The Return of the King", cover: "https://live.metahub.space/poster/medium/tt0167260/img", c1: "#55452d", c2: "#161209" }
];

var theatreTopSeries = [
    { caption: "Shogun", cover: "https://live.metahub.space/poster/medium/tt2788316/img", c1: "#4c2f2a", c2: "#160d0b" },
    { caption: "The Bear", cover: "https://live.metahub.space/poster/medium/tt14452776/img", c1: "#33445d", c2: "#0c1118" },
    { caption: "Fallout", cover: "https://live.metahub.space/poster/medium/tt12637874/img", c1: "#3f5640", c2: "#111b12" },
    { caption: "The Last of Us", cover: "https://live.metahub.space/poster/medium/tt3581920/img", c1: "#3d4a39", c2: "#11170f" },
    { caption: "Succession", cover: "https://live.metahub.space/poster/medium/tt7660850/img", c1: "#4c4c54", c2: "#141418" },
    { caption: "Andor", cover: "https://live.metahub.space/poster/medium/tt9253284/img", c1: "#3e4d57", c2: "#0f1519" },
    { caption: "Severance", cover: "https://live.metahub.space/poster/medium/tt11280740/img", c1: "#315263", c2: "#0c171d" },
    { caption: "House of the Dragon", cover: "https://live.metahub.space/poster/medium/tt11198330/img", c1: "#60382f", c2: "#180d0b" },
    { caption: "The Boys", cover: "https://live.metahub.space/poster/medium/tt1190634/img", c1: "#583236", c2: "#160c0d" },
    { caption: "True Detective", cover: "https://live.metahub.space/poster/medium/tt2356777/img", c1: "#34414c", c2: "#0d1216" }
];

var theatreGenres = [
    { name: "In Theaters", count: 44, cover: "https://live.metahub.space/poster/medium/tt15239678/img", c1: "#5d4633", c2: "#18110c" },
    { name: "Prestige TV", count: 128, cover: "https://live.metahub.space/poster/medium/tt2788316/img", c1: "#4c2f2a", c2: "#160d0b" },
    { name: "Sci-Fi", count: 310, cover: "https://live.metahub.space/poster/medium/tt0816692/img", c1: "#33465d", c2: "#0c1118" },
    { name: "Action", count: 480, cover: "https://live.metahub.space/poster/medium/tt10366206/img", c1: "#3c4a63", c2: "#0e121b" },
    { name: "Drama", count: 640, cover: "https://live.metahub.space/poster/medium/tt7660850/img", c1: "#4c4c54", c2: "#141418" },
    { name: "Animation", count: 190, cover: "https://live.metahub.space/poster/medium/tt9362722/img", c1: "#6b334f", c2: "#190c14" },
    { name: "Fantasy", count: 220, cover: "https://live.metahub.space/poster/medium/tt0167260/img", c1: "#55452d", c2: "#161209" },
    { name: "Horror", count: 260, cover: "https://live.metahub.space/poster/medium/tt3581920/img", c1: "#3d4a39", c2: "#11170f" },
    { name: "Crime", count: 350, cover: "https://live.metahub.space/poster/medium/tt2356777/img", c1: "#34414c", c2: "#0d1216" },
    { name: "Anime", count: 210, cover: "https://live.metahub.space/poster/medium/tt0388629/img", c1: "#4d4268", c2: "#151122" }
];

// Biblio (books) — discovery = Apple Books charts (live via BiblioApi). These arrays are the
// STATIC FALLBACK (real data pulled 2026-06-27) so the page paints instantly and never sits empty
// if the live call is slow; BiblioApi overrides Featured + Top-10 on load. Genres stay static here
// (mirrors Theatre). Covers = Apple hi-res on the same mzstatic CDN as comics → disk-cached.
var biblioFeatured = [
    { title: "Regime Change", art: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/f6/f5/a5/f6f5a534-5079-7881-13fb-6151f78a806b/9781668067260.jpg/400x600bb.jpg", artKind: "poster", blurb: "By Maggie Haberman — topping Apple Books now.", ghost: "B", c1: "#6a4a2c", c2: "#1d1209" },
    { title: "Aquarius", art: "https://is1-ssl.mzstatic.com/image/thumb/Publication116/v4/71/33/b9/7133b9b6-a782-fd0f-8848-302f2a5cb413/0000715555.jpg/400x600bb.jpg", artKind: "poster", blurb: "By Gemma James — topping Apple Books now.", ghost: "B", c1: "#5a3a3f", c2: "#180d10" },
    { title: "Whistler", art: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/e1/f4/a5/e1f4a502-9136-f220-0c92-facdfaa78e58/9780063511651.jpg/400x600bb.jpg", artKind: "poster", blurb: "By Ann Patchett — topping Apple Books now.", ghost: "B", c1: "#3f5a4a", c2: "#101a14" },
    { title: "The Calamity Club", art: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/39/64/45/3964459f-f5f3-d0b9-8623-d792b4ee584d/9781954118829.jpg/400x600bb.jpg", artKind: "poster", blurb: "By Kathryn Stockett — topping Apple Books now.", ghost: "B", c1: "#4a4063", c2: "#13101f" }
];

var biblioTop = [
    { caption: "Regime Change", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/f6/f5/a5/f6f5a534-5079-7881-13fb-6151f78a806b/9781668067260.jpg/400x600bb.jpg", c1: "#6a4a2c", c2: "#1d1209" },
    { caption: "Aquarius", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication116/v4/71/33/b9/7133b9b6-a782-fd0f-8848-302f2a5cb413/0000715555.jpg/400x600bb.jpg", c1: "#5a3a3f", c2: "#180d10" },
    { caption: "Whistler", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/e1/f4/a5/e1f4a502-9136-f220-0c92-facdfaa78e58/9780063511651.jpg/400x600bb.jpg", c1: "#3f5a4a", c2: "#101a14" },
    { caption: "The Calamity Club", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/39/64/45/3964459f-f5f3-d0b9-8623-d792b4ee584d/9781954118829.jpg/400x600bb.jpg", c1: "#4a4063", c2: "#13101f" },
    { caption: "Choke Point", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/25/2d/e6/252de6b6-a031-1e18-caed-049078aab004/9781668065945.jpg/400x600bb.jpg", c1: "#6a5a2c", c2: "#1d1809" },
    { caption: "Yesteryear: A GMA Book Club Pick", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/83/35/a9/8335a977-61f3-2f2c-22e2-a0551cf8707f/9780593804223.d.jpg/400x600bb.jpg", c1: "#3f4a63", c2: "#0e121b" },
    { caption: "Theo of Golden", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/ca/94/ee/ca94ee35-57c1-9f26-2a89-7530f60797ac/9781668236536.jpg/400x600bb.jpg", c1: "#5a4a3a", c2: "#181210" },
    { caption: "Weddings", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/1f/f9/e2/1ff9e2eb-50a2-aa1c-3816-3c6971f9e712/9780593973127.d.jpg/400x600bb.jpg", c1: "#634050", c2: "#1b0d14" },
    { caption: "It Could Have Been Her", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/3a/59/9a/3a599a84-72a0-a79c-b31e-ef91507596c6/9781668033920.jpg/400x600bb.jpg", c1: "#3a5a5a", c2: "#0e1a1a" },
    { caption: "The Score", cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/a5/e3/9d/a5e39da7-be49-daf3-2991-8c89357cf5d3/0000128008.jpg/400x600bb.jpg", c1: "#5a5a3a", c2: "#181810" }
];

var biblioGenres = [
    { name: "Fiction & Literature", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/e1/f4/a5/e1f4a502-9136-f220-0c92-facdfaa78e58/9780063511651.jpg/400x600bb.jpg", c1: "#6a4a2c", c2: "#1d1209" },
    { name: "Mysteries & Thrillers", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/25/2d/e6/252de6b6-a031-1e18-caed-049078aab004/9781668065945.jpg/400x600bb.jpg", c1: "#5a3a3f", c2: "#180d10" },
    { name: "Sci-Fi & Fantasy", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/13/fb/63/13fb6355-fce2-0e4b-08b7-48452037759a/9780593135211.d.jpg/400x600bb.jpg", c1: "#4a4063", c2: "#13101f" },
    { name: "Romance", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication116/v4/71/33/b9/7133b9b6-a782-fd0f-8848-302f2a5cb413/0000715555.jpg/400x600bb.jpg", c1: "#3f5a4a", c2: "#101a14" },
    { name: "Biographies & Memoirs", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/0f/0a/db/0f0adb9f-8dd7-7c5a-0458-79ced91d5e39/9780593733325.d.jpg/400x600bb.jpg", c1: "#6a5a2c", c2: "#1d1809" },
    { name: "History", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/6f/ae/14/6fae140e-dcf6-4be4-164e-5100459c5fb3/9780593734247.d.jpg/400x600bb.jpg", c1: "#3f4a63", c2: "#0e121b" },
    { name: "Young Adult", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication122/v4/b8/29/9c/b8299cf0-d6ac-5b0f-bbe3-7eddb7eda0c6/9781534467644.jpg/400x600bb.jpg", c1: "#634050", c2: "#1b0d14" },
    { name: "Comics & Graphic Novels", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication124/v4/7c/73/8d/7c738d92-dea3-7901-7ef8-962db5966470/Invincible_Compendium01.jpg/400x600bb.jpg", c1: "#5a4a3a", c2: "#181210" },
    { name: "Humor", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/c2/47/cb/c247cb59-9caf-a2cc-c483-cc787e64afda/9780316265034.jpg/400x600bb.jpg", c1: "#3a5a5a", c2: "#0e1a1a" },
    { name: "Travel & Adventure", count: 24, cover: "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/ed/3a/53/ed3a5357-2c9a-ca04-d60c-ca541640b143/1057979625.jpg/400x600bb.jpg", c1: "#5a5a3a", c2: "#181810" }
];

// every cover/banner the world shows, de-duped — for the boot prefetch (warms the disk cache).
function allImageUrls() {
    var urls = [];
    function push(u) { if (u && urls.indexOf(u) === -1) urls.push(u); }
    for (var i = 0; i < featured.length; i++) push(featured[i].art);
    var rows = [topManga, topComics, genresManga, genresComics];
    for (var r = 0; r < rows.length; r++)
        for (var j = 0; j < rows[r].length; j++) push(rows[r][j].cover);
    for (var f = 0; f < theatreFeatured.length; f++) push(theatreFeatured[f].art);
    var theatreRows = [theatreTopMovies, theatreTopSeries, theatreGenres];
    for (var tr = 0; tr < theatreRows.length; tr++)
        for (var tj = 0; tj < theatreRows[tr].length; tj++) push(theatreRows[tr][tj].cover);
    for (var bf = 0; bf < biblioFeatured.length; bf++) push(biblioFeatured[bf].art);
    var biblioRows = [biblioTop, biblioGenres];
    for (var br = 0; br < biblioRows.length; br++)
        for (var bj = 0; bj < biblioRows[br].length; bj++) push(biblioRows[br][bj].cover);
    return urls;
}
