# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/catalogs-state.json`

## Summary

- Total files: **12**
- Documented: **12**
- Undocumented: **0**
- Drifted: **0**

<a id="file-native-engine-imdbcatalog-cpp"></a>
## `native/engine/ImdbCatalog.cpp`

- Status: **CURRENT**
- Accepted blob: `5767375817262aeed8ca038c060d9c463a1e604f`
- Current blob: `5767375817262aeed8ca038c060d9c463a1e604f`
- Source: [`native/engine/ImdbCatalog.cpp`](../../native/engine/ImdbCatalog.cpp)

```text
// ImdbCatalog.cpp — see header.
```

<a id="file-native-engine-imdbcatalog-h"></a>
## `native/engine/ImdbCatalog.h`

- Status: **CURRENT**
- Accepted blob: `a0cca17ccad56e00c1b1551429fa857d7cdcb3f8`
- Current blob: `a0cca17ccad56e00c1b1551429fa857d7cdcb3f8`
- Source: [`native/engine/ImdbCatalog.h`](../../native/engine/ImdbCatalog.h)

```text
// ImdbCatalog — read-only seam onto the baked IMDb index (data/imdb_catalog.db,
// built by scripts/theatre_brain/build_imdb_db.py). The movies/shows twin of
// MalCatalog: QML paints, C++ decides. Allowlisted keys only, every value bound,
// limit clamped; missing db => ready()==false and every accessor returns empty so
// the Theatre pages honestly omit index shelves.
```

<a id="file-native-engine-malcatalog-cpp"></a>
## `native/engine/MalCatalog.cpp`

- Status: **CURRENT**
- Accepted blob: `cc979b830da492e4575da6e2271c5f0148bb103d`
- Current blob: `cc979b830da492e4575da6e2271c5f0148bb103d`
- Source: [`native/engine/MalCatalog.cpp`](../../native/engine/MalCatalog.cpp)

```text
// MalCatalog.cpp — see header. [Agent 0 (Claude), shell]
```

<a id="file-native-engine-malcatalog-h"></a>
## `native/engine/MalCatalog.h`

- Status: **CURRENT**
- Accepted blob: `ea8c3565ba75211b14d1a9c7b365eaf28dac0e2b`
- Current blob: `ea8c3565ba75211b14d1a9c7b365eaf28dac0e2b`
- Source: [`native/engine/MalCatalog.h`](../../native/engine/MalCatalog.h)

```text
// MalCatalog — read-only seam onto the baked MyAnimeList catalog
// (data/mal_catalog.db, built by scripts/anime_brain/build_mal_db.py from the
// weekly Kaggle dump; genre-page revival, Hemanth 2026-07-18). QML paints, C++
// decides: db, schema, and query shape live here. Rows come back JIKAN-SHAPED
// (nested images.jpg, [{name}] credit/genre lists, Jikan status strings) so the
// genre pages' existing card mappers consume them without a rendering change.
// Missing db => ready()==false and every accessor returns empty — the pages
// fall through to their live Jikan/AniList/Kitsu ladder untouched.
```

<a id="file-qml-genreapi-js"></a>
## `qml/GenreApi.js`

- Status: **CURRENT**
- Accepted blob: `1a01ad47668b815236745e6d643795b02f12a354`
- Current blob: `1a01ad47668b815236745e6d643795b02f12a354`
- Source: [`qml/GenreApi.js`](../../qml/GenreApi.js)

```text
// GenreApi.js — live data for a Colosseum genre BROWSE page (Tankoban / manga lane).
//
// Source: Jikan (api.jikan.moe/v4) — MyAnimeList's own keyless API. The genre page recreates MAL's
// genre listing (the reference Hemanth handed us: myanimelist.net/manga/genre/2/Adventure), so Jikan
// IS the right source — same data, no login, no key (the standing sourcing law). genre name → MAL id
// is fixed below (authoritative, pulled from /genres/manga). Cards route into A1's MangaSeries.qml by
// title. Comics lane is a DIFFERENT source (MAL is manga/anime only) — not handled here yet.
//
// loadGenre(name, sort, push) fetches once and calls push(payload) with { count, desc, cards, montage }.
// One Jikan call per page (well under the 3/sec · 60/min limit).
```

<a id="file-qml-genrepage-qml"></a>
## `qml/GenrePage.qml`

- Status: **CURRENT**
- Accepted blob: `7f73382e13f31ad0238b2c954553b08fc2c41585`
- Current blob: `7f73382e13f31ad0238b2c954553b08fc2c41585`
- Source: [`qml/GenrePage.qml`](../../qml/GenrePage.qml)

```text
// GenrePage — the genre BROWSE page for the Tankoban / manga lane. Recreates MyAnimeList's genre
// listing (reference: myanimelist.net/manga/genre/2/Adventure) in the house glass. Approved mock:
// mocks/genre.html. Data: GenreApi.js (Jikan / MAL, keyless). Cards route into MangaSeries.qml by title.
//
// PROTOTYPE harness:  qml.exe qml/_genrecheck.qml   (loads this page with a live genre)
//
// Signature: the genre is its OWN art — its top covers wash behind the title (GenreMosaic doctrine),
// and the rank ordinal encodes the by-readers popularity sort (real info, not decoration).
```

<a id="file-qml-tankobandiscoverapi-js"></a>
## `qml/TankobanDiscoverApi.js`

- Status: **CURRENT**
- Accepted blob: `7c806eff8b1d6d125dcba032363c84ddfbe612cc`
- Current blob: `7c806eff8b1d6d125dcba032363c84ddfbe612cc`
- Source: [`qml/TankobanDiscoverApi.js`](../../qml/TankobanDiscoverApi.js)

```text
// TankobanDiscoverApi.js — the Tankoban adapter for the shared Discover shell.
// (Task 6, arc 2026-08-01.)
//
// The shell (DiscoverBrowser.qml) is WORLD-NEUTRAL: it speaks a small adapter
// contract (types/catalogs/filters/defaultCatalog/resolvePin/fetchPage) and renders
// normalized cards. This file IS the Tankoban adapter — it owns the catalogue
// descriptors, the filter shape, normalization, the See-all pin resolution, the
// local-first page delivery, the non-blocking Jikan refresh, and the extension
// catalogue seam. It performs NO acquisition and owns NO series-page UI (the page
// wrapper routes normalized cards to the existing Manga/Comics series doors).
//
// A .pragma library CANNOT see QML context properties, so dependencies are passed
// explicitly into create(): the MalCatalog and ComicsCatalog objects, the extension
// registry, the global showExplicit flag, and (optionally) an XMLHttpRequest factory
// (the page injects the real one; the harness injects a fake).
```

<a id="file-qml-theatrecatalogpage-qml"></a>
## `qml/TheatreCatalogPage.qml`

- Status: **CURRENT**
- Accepted blob: `dba17f2d4ed6f1aad03567331460fb38a3064c9b`
- Current blob: `dba17f2d4ed6f1aad03567331460fb38a3064c9b`
- Source: [`qml/TheatreCatalogPage.qml`](../../qml/TheatreCatalogPage.qml)

```text
// TheatreCatalogPage — one deep catalogue tab (Movies · Shows · Anime). Begins with Top 10 and
// ends with the genre mosaic; between them sit the house shelves, the recognized service rows
// merged into their contextual slots, and a "From Your Extensions" section. NO hero, Continue,
// Next Up, award, or blurb — those belong to the Theatre landing page. Rows page the keyless
// TheatreApi, filter through ExplicitContentPolicy before rendering, and honour per-tab
// customization (move / hide / rename / reset). Stale-generation callbacks are ignored.
```

<a id="file-qml-theatrecatalogrules-js"></a>
## `qml/TheatreCatalogRules.js`

- Status: **CURRENT**
- Accepted blob: `02391512be686921c44939e0103ae195d280a54c`
- Current blob: `02391512be686921c44939e0103ae195d280a54c`
- Source: [`qml/TheatreCatalogRules.js`](../../qml/TheatreCatalogRules.js)

```text
// TheatreCatalogRules.js — the pure, keyless brain of the deep Theatre catalogue.
// Stable shelf inventories, ranking predicates, deterministic daily rotation, extension
// placement, and row customization. NO transport, NO Date.now(), NO QML context: every
// function is a pure transform so the offscreen rules harness can pin it. Titles are literal
// and self-explanatory — there is intentionally no `sub`/`blurb` field on any row (spec §4).
```

<a id="file-qml-theatregenreapi-js"></a>
## `qml/TheatreGenreApi.js`

- Status: **CURRENT**
- Accepted blob: `5c28fac96cb75b9458f01c9b17b570f09b9a5e51`
- Current blob: `5c28fac96cb75b9458f01c9b17b570f09b9a5e51`
- Source: [`qml/TheatreGenreApi.js`](../../qml/TheatreGenreApi.js)

```text
// TheatreGenreApi.js — genre data for the THEATRE lane (movies / shows / anime). Theatre-owned
// clone of the manga lane's GenreApi/GenreIndexApi pattern (lane discipline: those files are
// A1/A5 territory and stay untouched). Sources, per the standing no-login law:
//   anime          → Jikan (MAL) — live genre ids + counts from /genres/anime, cards from /anime
//   movie / series → Cinemeta top catalogs (catalog/<type>/top/genre=<G>) — lean cards
// loadGenre(kind, name, sort, push) → { count, desc, cards, montage }  (count 0 ⇒ page hides it)
// loadGroups(kind, includeExplicit, done) → GenreIndex-shaped groups [{ group, genres:[tile] }]
```

<a id="file-qml-theatregenrepage-qml"></a>
## `qml/TheatreGenrePage.qml`

- Status: **CURRENT**
- Accepted blob: `a8f86396646fd404d0ce9ad1a210ab24fda1f2a8`
- Current blob: `a8f86396646fd404d0ce9ad1a210ab24fda1f2a8`
- Source: [`qml/TheatreGenrePage.qml`](../../qml/TheatreGenrePage.qml)

```text
// TheatreGenrePage — the genre BROWSE page for the THEATRE lane (movies / shows / anime).
// Theatre-owned clone of the manga lane's GenrePage (MAL genre-listing template, house glass) —
// the Biblio precedent: same template, own lane, zero risk to A1's manga machinery.
// Data: TheatreGenreApi.js — Jikan for anime (rich MAL cards), Cinemeta for movies/shows (lean
// cards: poster · title · rank · rating; fields the source doesn't have simply don't render).
// Cards route into the Theatre detail via itemRequested({ id, type, title, cover }).
```

<a id="file-qml-theatreseeallpage-qml"></a>
## `qml/TheatreSeeAllPage.qml`

- Status: **CURRENT**
- Accepted blob: `73a2d4071aced564c758932d780f1f6296e668ed`
- Current blob: `73a2d4071aced564c758932d780f1f6296e668ed`
- Source: [`qml/TheatreSeeAllPage.qml`](../../qml/TheatreSeeAllPage.qml)

```text
// TheatreSeeAllPage — one shelf's infinite grid. Back + title + factual source header, the
// shared CataloguePosterGrid, skeletons, incremental loading, a retryable error state, and an
// honest "no longer available" state that NAMES the provider when a pinned extension is gone.
// It NEVER ranks in QML — it pages TheatreApi.loadRowPage and renders what comes back. The
// loader is injectable (pageLoader) so the offscreen harness can drive paging deterministically.
```
