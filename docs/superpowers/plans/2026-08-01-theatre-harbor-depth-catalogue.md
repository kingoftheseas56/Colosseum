# Theatre Deep Catalogue Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Theatre's Top-10-only Movies, Shows, and Anime bodies with deep, keyless, customizable catalogue pages whose every shelf opens a reusable infinite grid.

**Architecture:** Keep `TheatreApi.js` as the keyless orchestration boundary, but move pure shelf definitions/ranking/customization transforms into focused JS libraries. Extend `MalCatalog` for paged offline Anime queries, reuse Discover's card/grid behavior through extracted QML components, and keep extension catalogs on their existing manifest-driven transport. The Theatre landing page and detail routes remain unchanged.

**Tech Stack:** Qt 6.11 QML/QtQuick, QML JavaScript libraries, QtCore `Settings`, C++/QtSql for the bundled MAL catalogue, Cinemeta, Jikan, Kitsu, PowerShell/QML offscreen harnesses.

## Global Constraints

- Work directly on the existing `master` branch. Do not create a branch or git worktree.
- Preserve unrelated dirty-worktree changes; stage and commit only the exact files named by each task.
- Built-in discovery must remain keyless: no TMDB, Trakt, account, login, or API-key dependency.
- Do not add per-tab heroes, featured banners, Continue Watching, Next Up, awards, or row blurbs.
- IMDb ratings are hidden at rest and revealed only on pointer hover with Discover's existing `★ <value>` treatment.
- Use literal shelf titles and factual source attribution only.
- Apply `ExplicitContentPolicy` exactly; never classify TV-MA/R ratings, violence, gore, horror, profanity, `Berserk`, `Game of Thrones`, or `Ecchi` as explicit by themselves.
- Use TDD: add or strengthen the named harness first, observe its required failure, implement the minimum complete behavior, then rerun it.
- Every commit command stages explicit paths; never use `git add .`.

---

## File structure

### Create

- `qml/TheatreCatalogRules.js` — stable inventories, ranking predicates, daily selection, service-slot classification, and pure row transforms.
- `qml/TheatreRowPreferences.qml` — per-tab QSettings persistence and pure update methods.
- `qml/TheatreRowControls.qml` — Harbor-parity move/hide/rename controls.
- `qml/CataloguePosterCard.qml` — shared Discover-style poster hover/focus card.
- `qml/CataloguePosterGrid.qml` — shared infinite poster grid renderer.
- `qml/TheatreSeeAllPage.qml` — simple Theatre shelf grid and back contract.
- `tests/theatre_catalog_rules_harness.qml` — pure inventory/ranking/rotation/extension tests.
- `tests/theatre_row_preferences_harness.qml` — non-vacuous persistence and migration tests.
- `tests/theatre_catalog_page_harness.qml` — offscreen page structure/edit-mode/hover contract.
- `tests/theatre_see_all_harness.qml` — paging, stale-response, empty/error, and item-open contract.
- `tests/test_theatre_deep_catalogue.ps1` — one focused acceptance runner.

### Modify

- `qml/TheatreApi.js` — normalized metadata, candidate pools, progressive specs, Anime live ladder, and row-page API.
- `qml/AddonClient.js` — expose normalized service identity without changing transport behavior.
- `qml/PosterRail.qml` — shared poster card, factual source line, See-all pin, and edit-mode injection.
- `qml/DiscoverPage.qml` — consume the shared poster grid/card without changing its public behavior.
- `qml/TheatreCatalogPage.qml` — deep rows, preferences, customization controls, explicit policy, and See-all forwarding.
- `qml/TheatreWorld.qml` — pass global preferences/MAL catalogue and host See-all navigation.
- `qml/Main.qml` — pass the one `ContentPreferences` instance into Theatre.
- `native/engine/MalCatalog.h` / `native/engine/MalCatalog.cpp` — allowlisted paged Anime catalogue queries.
- `scripts/anime_brain/build_mal_db.py` — add indexes needed by status/type/year/tag paging.
- `tests/test_theatre_top10_genre_boxes.ps1` — replace obsolete Top-10-only assertions with preserved genre/no-hero assertions.
- `tests/discover_page_harness.qml` / `tests/discover_api_harness.qml` — prove shared-card refactor preserves Discover.

## Interfaces

```javascript
// TheatreCatalogRules.js
function defaultRows(pageKey)                         // -> [{key,title,placement,ranked,recipe}]
function dailyRows(dateMs, count)                     // -> stable subset of movie definitions
function rankItems(recipe, items, nowMs)              // -> filtered/deduped ordered items
function placeExtensions(pageKey, installed, houses)  // -> {mainRows, extensionRows}
function applyCustomization(rows, custom, editMode)   // -> rows in persisted order

// TheatreApi.js
function loadCatalogPage(pageKey, options, push)
function loadRowPage(pin, offset, limit, options, done)
// options = { malCatalog, showExplicit, generation }
// push({pageKey,generation,rows,loading,error}) may fire progressively.

// MalCatalog
Q_INVOKABLE QVariantList animeCatalog(const QVariantMap& query,
                                      int offset = 0, int limit = 24) const;

// TheatreRowPreferences.qml
function valueFor(pageKey) // -> {order:[],hidden:[],renamed:{}}
function move(pageKey, availableKeys, key, delta)
function toggleHidden(pageKey, key)
function rename(pageKey, key, label)
function reset(pageKey)
```

---

### Task 1: Pin the pure catalogue contract

**Files:**
- Create: `qml/TheatreCatalogRules.js`
- Create: `tests/theatre_catalog_rules_harness.qml`
- Create: `tests/test_theatre_deep_catalogue.ps1`
- Modify: `tests/test_theatre_top10_genre_boxes.ps1`

**Interfaces:**
- Produces `defaultRows`, `dailyRows`, `rankItems`, `placeExtensions`, and `applyCustomization` for all later tasks.

- [ ] **Step 1: Write the failing pure-rules harness**

Create an offscreen QML harness that imports `TheatreCatalogRules.js`, collects failures, and asserts:

```qml
var movies = Rules.defaultRows("movies")
ok(movies[0].key === "top-10" && movies[0].ranked, "Movies Top 10 first")
ok(movies.some(function(r){ return r.key === "recently-released" }), "Movies recent")
ok(movies.some(function(r){ return r.key === "hidden-gems" }), "Movies gems")
ok(!movies.some(function(r){ return /award/i.test(r.key + r.title) }), "No awards")
ok(!movies.some(function(r){ return /in.theaters|coming.soon/i.test(r.key) }), "No unsupported freshness")

var anime = Rules.defaultRows("anime")
ok(anime.some(function(r){ return r.key === "upcoming-season" }), "Anime upcoming")
ok(anime.some(function(r){ return r.key === "top-anime-movies" }), "Anime movies")
ok(anime.some(function(r){ return r.key === "1990s-earlier" }), "Anime eras")

var dayA = Rules.dailyRows(Date.UTC(2026, 7, 1), 6)
var dayA2 = Rules.dailyRows(Date.UTC(2026, 7, 1, 22), 6)
var dayB = Rules.dailyRows(Date.UTC(2026, 7, 2), 6)
ok(JSON.stringify(dayA) === JSON.stringify(dayA2), "Same UTC day stable")
ok(JSON.stringify(dayA) !== JSON.stringify(dayB), "Next day rotates")
ok(dayA.every(function(r){ return r.rotating === true }), "Daily marker")
```

Add ranking fixtures proving weighted rating needs a vote floor, Hidden Gems excludes the most popular band, missing facts are excluded from fact-dependent recipes, canonical IDs dedupe, and explicit items are removable before ranking.

- [ ] **Step 2: Run the harness and verify failure**

Run:

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage Rules
```

Expected: failure because `TheatreCatalogRules.js` does not exist.

- [ ] **Step 3: Implement stable definitions and pure transforms**

Use immutable definition arrays with literal titles and no `sub`/`blurb` field. Implement UTC-day selection with a deterministic integer seed. Use exact service IDs/names supplied by `AddonClient` for contextual slots; unknown catalogues go to `extensionRows` in installed order.

Ranking helpers must implement:

```javascript
function weighted(score, votes, mean, floor) {
    if (!(score > 0) || !(votes >= floor)) return -1
    return (votes / (votes + floor)) * score + (floor / (votes + floor)) * mean
}
```

`applyCustomization` orders known keys from saved state, appends new keys in default order, ignores removed keys, includes hidden rows only in edit mode, and applies renamed labels without mutating source rows.

- [ ] **Step 4: Run the pure-rules harness**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage Rules`.

Expected: `THEATRE_CATALOG_RULES_OK` and exit 0.

- [ ] **Step 5: Update the obsolete Top-10-only contract**

Remove assertions requiring one row and rejecting additional shelves. Retain assertions that each tab has Top 10 first, `GenreMosaic` exists, no per-tab hero exists, genre routes remain, and no TMDB string exists.

- [ ] **Step 6: Commit Task 1**

```powershell
git add qml/TheatreCatalogRules.js tests/theatre_catalog_rules_harness.qml tests/test_theatre_deep_catalogue.ps1 tests/test_theatre_top10_genre_boxes.ps1
git commit -m "feat(theatre): define deep catalogue rules"
```

---

### Task 2: Add paged offline Anime catalogue queries

**Files:**
- Modify: `native/engine/MalCatalog.h`
- Modify: `native/engine/MalCatalog.cpp`
- Modify: `scripts/anime_brain/build_mal_db.py`
- Modify: `tests/anime_order_index_harness.cpp` only if shared fixture helpers are reusable
- Create: `tests/mal_catalog_rows_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces `MalCatalog::animeCatalog(query, offset, limit)` consumed by Task 4.

- [ ] **Step 1: Write the failing C++ harness**

Create a temporary SQLite fixture with rows covering airing/finished/upcoming, TV/Movie, score/votes/members, years, and exact tags. Assert queries:

```cpp
QVariantMap top{{"order", "score"}, {"voteFloor", 5000}};
QCOMPARE(catalog.animeCatalog(top, 0, 2).size(), 2);
QVariantMap airing{{"status", "Currently Airing"}, {"order", "members"}};
QCOMPARE(catalog.animeCatalog(airing, 0, 24).first().toMap().value("status").toString(),
         QStringLiteral("Currently Airing"));
QVariantMap movies{{"type", "Movie"}, {"order", "score"}};
QCOMPARE(catalog.animeCatalog(movies, 0, 24).first().toMap().value("type").toString(),
         QStringLiteral("Movie"));
QVariantMap decade{{"yearFrom", 2010}, {"yearTo", 2019}, {"order", "members"}};
QVERIFY(std::all_of(rows.begin(), rows.end(), yearBetween2010And2019));
```

Also assert offset paging has no overlap, limit clamps to 100, unknown keys/order values return an empty list, and tag values are bound rather than interpolated.

- [ ] **Step 2: Configure and run the failing harness**

Run:

```powershell
cmake -S native -B native/build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-msvc --target mal_catalog_rows_harness
& native/build-msvc/mal_catalog_rows_harness.exe
```

Expected: compile failure because `animeCatalog` is absent.

- [ ] **Step 3: Implement the allowlisted query builder**

Accept only `order`, `status`, `type`, `tag`, `yearFrom`, `yearTo`, `voteFloor`, `membersMin`, and `membersMax`. Map `order` to fixed SQL fragments (`members`, `score`, `year`) and bind every value. Return the existing Jikan-shaped maps so `mapJikan` remains reusable.

- [ ] **Step 4: Add database indexes**

Update the bake script to create:

```sql
CREATE INDEX anime_members_idx ON anime(members DESC);
CREATE INDEX anime_score_votes_idx ON anime(score DESC, scored_by DESC);
CREATE INDEX anime_status_type_year_idx ON anime(status, type, year);
CREATE INDEX tag_anime_lookup_idx ON tag(medium, tag, mal_id);
```

Rebuilding the database is a deployment/data-vault action, not part of this source commit. The runtime must still work against the current database without the new indexes.

- [ ] **Step 5: Run native tests**

Run the harness plus the existing Anime and MAL genre tests:

```powershell
& native/build-msvc/mal_catalog_rows_harness.exe
& native/build-msvc/anime_order_index_harness.exe
& tests/test_mal_genre_catalog_p0.ps1
```

Expected: all exit 0.

- [ ] **Step 6: Commit Task 2**

`native/CMakeLists.txt` was already dirty when this plan was written. Inspect its pre-existing diff and stage only the new `mal_catalog_rows_harness` block; do not stage the user's unrelated CMake changes.

```powershell
git add native/engine/MalCatalog.h native/engine/MalCatalog.cpp scripts/anime_brain/build_mal_db.py tests/mal_catalog_rows_harness.cpp
git add -p native/CMakeLists.txt
git diff --cached --check
git commit -m "feat(theatre): add paged offline anime catalogues"
```

---

### Task 3: Build the keyless Movies and Shows row engine

**Files:**
- Modify: `qml/TheatreApi.js`
- Create: `tests/theatre_api_rows_harness.qml`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- Produces progressive `loadCatalogPage` and paged `loadRowPage` for movie/show house pins.
- Consumes Task 1 recipes and existing Cinemeta transport.

- [ ] **Step 1: Write a transport-injected failing harness**

Add a test-only `setRequestAdapter(fn)`/`resetRequestAdapter()` seam. The fake adapter returns deterministic Cinemeta top, genre, and full-meta fixtures. Assert:

- Top 10 publishes first and has ten items;
- `Recently Released`, `Top Rated`, and `Hidden Gems` are distinct;
- runtime/country/status/season shelves contain only qualified fixtures;
- a missing field never qualifies an item;
- one failed genre fetch does not blank successful rows;
- full-meta requests never exceed four concurrent calls;
- duplicate URLs coalesce;
- stale generation callbacks are echoed but ignored by the page contract;
- no result includes `sub` or `blurb` shelf copy;
- preview items preserve `imdbRating`, `releaseInfo`, `genres`, and explicit source flags.

- [ ] **Step 2: Run and observe failure**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage ApiRows`.

Expected: failure because request injection and deep specs are absent.

- [ ] **Step 3: Normalize Cinemeta metadata once**

Extend `mapCinemeta` to retain factual fields:

```javascript
imdbRating: meta.imdbRating || "",
releaseInfo: meta.releaseInfo || "",
runtime: meta.runtime || "",
genres: meta.genres || [],
country: meta.country || "",
status: meta.status || "",
behaviorHints: meta.behaviorHints || ({}),
seasonCount: seasonCount(meta.videos || [])
```

Do not synthesize values.

- [ ] **Step 4: Implement candidate pools and bounded enrichment**

Build one per-type pool from `top` plus only the genre endpoints required by the definitions. Dedupe by IMDb ID before ranking. Add a four-worker full-meta queue with URL-keyed 30-minute memory cache. Publish cheap rows first, then replace/add enriched rows through the same generation.

- [ ] **Step 5: Implement paged house rows**

Define pins as:

```javascript
{ sourceKind:"house", pageKey:"movies", rowKey:"hidden-gems", title:"Hidden Gems" }
```

`loadRowPage` expands the candidate window by offset, applies the same recipe and explicit filter, and returns `{generation,items,hasMore,error}`. It must not silently route a missing row key to Top 10.

- [ ] **Step 6: Run the API harness**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage ApiRows`.

Expected: `THEATRE_API_ROWS_OK` and exit 0.

- [ ] **Step 7: Commit Task 3**

```powershell
git add qml/TheatreApi.js tests/theatre_api_rows_harness.qml tests/test_theatre_deep_catalogue.ps1
git commit -m "feat(theatre): build keyless movie and show shelves"
```

---

### Task 4: Add the deep Anime row ladder

**Files:**
- Modify: `qml/TheatreApi.js`
- Modify: `tests/theatre_api_rows_harness.qml`
- Modify: `tests/theatre_genre_anilist_test.mjs` only to preserve its existing fallback contract if signatures move

**Interfaces:**
- Consumes `MalCatalog.animeCatalog` from Task 2.
- Produces preview and paged Anime rows through the same Task 3 APIs.

- [ ] **Step 1: Add failing Anime fixtures**

Pass a fake `malCatalog` object into `loadCatalogPage("anime", options, push)`. Assert the complete approved Anime keys and order, offset paging, exact tag recipes, decade boundaries, Movie type restriction, and source fallback order.

Test these source outcomes separately:

1. local rows publish immediately, Jikan succeeds and refreshes;
2. local rows publish, Jikan fails, Kitsu refreshes compatible rows;
3. local rows publish, both live sources fail, no row blanks;
4. no local database, Jikan succeeds;
5. no local database, Jikan fails, Kitsu supplies only recipes it can answer;
6. no source can answer a recipe, that shelf is omitted.

- [ ] **Step 2: Run and observe failure**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage ApiRows`.

Expected: failure listing missing Anime rows.

- [ ] **Step 3: Implement local-first Anime recipes**

Map definitions to allowlisted queries: status for Airing, future season/year for Upcoming, type Movie, score/member ordering, member bands for Hidden Gems, year ranges for decades, and exact MAL tags for genre/theme shelves.

- [ ] **Step 4: Add live refresh without account state**

Use Jikan `/top/anime`, `/seasons/now`, `/seasons/upcoming`, and filtered `/anime` routes with existing cache/pacing. Keep Kitsu as the second live rung. Never add AniList authentication or user lists.

- [ ] **Step 5: Run Anime and existing fallback tests**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage ApiRows
node tests/theatre_genre_anilist_test.mjs
& tests/test_theatre_anime_parity.ps1
```

Expected: all pass.

- [ ] **Step 6: Commit Task 4**

```powershell
git add qml/TheatreApi.js tests/theatre_api_rows_harness.qml tests/theatre_genre_anilist_test.mjs
git commit -m "feat(theatre): add deep keyless anime shelves"
```

---

### Task 5: Place service and general extension catalogues

**Files:**
- Modify: `qml/AddonClient.js`
- Modify: `qml/TheatreApi.js`
- Modify: `qml/TheatreCatalogRules.js`
- Modify: `tests/theatre_catalog_rules_harness.qml`
- Modify: `tests/discover_api_harness.qml`

**Interfaces:**
- Produces normalized extension descriptor `{serviceKey, extName, transportUrl, type, catalogId, title}`.

- [ ] **Step 1: Add failing placement cases**

Use installed fixtures for Max, Netflix, Apple TV+, Disney+, Prime Video, AMC, FX, an unknown documentary catalogue, a disabled catalogue, and a required-extra catalogue. Assert recognized rows enter main contextual slots, the unknown row enters `extensionRows`, disabled/required rows disappear, and installed order breaks ties.

- [ ] **Step 2: Run the rules and Discover harnesses**

Expected: placement assertions fail; existing Discover assertions remain green.

- [ ] **Step 3: Implement normalized service identity**

Classify from extension ID/origin plus manifest/catalogue names through a fixed table. Do not classify from an arbitrary item title. Preserve the existing `catalogSpecs` and `discoverCatalogSpecs` results for non-service callers.

- [ ] **Step 4: Emit extension See-all pins and live removal behavior**

Use the existing transport/catalog identity in every pin. On registry revision, rebuild only extension-derived rows; house rows remain. A stale pin returns `missing:true` and the provider name.

- [ ] **Step 5: Run tests**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage Rules
& tests/test_theatre_deep_catalogue.ps1 -Stage DiscoverRegression
node tests/extension_world_isolation_test.mjs
node tests/extension_reorder_world_test.mjs
```

Expected: all pass.

- [ ] **Step 6: Commit Task 5**

```powershell
git add qml/AddonClient.js qml/TheatreApi.js qml/TheatreCatalogRules.js tests/theatre_catalog_rules_harness.qml tests/discover_api_harness.qml
git commit -m "feat(theatre): place installed catalogue extensions"
```

---

### Task 6: Extract the Discover poster card and infinite grid

**Files:**
- Create: `qml/CataloguePosterCard.qml`
- Create: `qml/CataloguePosterGrid.qml`
- Modify: `qml/DiscoverPage.qml`
- Modify: `tests/discover_page_harness.qml`
- Create: `tests/catalogue_poster_card_harness.qml`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- Produces a shared card with `item`, `keyboardFocused`, `skeleton`, and `activated(item)`.
- Produces a shared grid with `items`, `loadingMore`, `hasMore`, `requestMore()`, and `itemRequested(item)`.

- [ ] **Step 1: Write failing visual-contract assertions**

The card harness must expose readonly test properties proving:

```qml
ok(card.ratingVisibleAtRest === false, "rating hidden at rest")
card.testHovered = true
ok(card.ratingVisible === true && card.ratingText === "★ 8.7", "rating on hover")
card.testHovered = false; card.keyboardFocused = true
ok(card.ratingVisible === false, "keyboard focus does not invent a hover reveal")
```

Also prove absent ratings render no empty badge, title remains visible, activation carries the original item, and skeletons never activate.

- [ ] **Step 2: Run and observe failure**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage Cards`.

Expected: missing component failure.

- [ ] **Step 3: Extract Discover's existing card verbatim before changing behavior**

Move the frame, hover lift, scrim, `★ <value>` year/rating reveal, keyboard ring, title, skeleton, `HoverHandler`, and activation into `CataloguePosterCard.qml`. Preserve Discover's current hover-only rating presentation exactly; keyboard focus keeps its ring but does not expose the hover scrim.

- [ ] **Step 4: Extract the grid shell**

Move GridView sizing, keyboard movement, skeleton count, incremental-load trigger, and item activation into `CataloguePosterGrid.qml`. Keep error/empty copy supplied by the parent page.

- [ ] **Step 5: Rewire Discover and run regression**

Run:

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage Cards
& tests/test_theatre_deep_catalogue.ps1 -Stage DiscoverRegression
```

Expected: card contract and existing Discover defaults pass.

- [ ] **Step 6: Commit Task 6**

```powershell
git add qml/CataloguePosterCard.qml qml/CataloguePosterGrid.qml qml/DiscoverPage.qml tests/catalogue_poster_card_harness.qml tests/discover_page_harness.qml tests/test_theatre_deep_catalogue.ps1
git commit -m "refactor(catalogue): share discover poster grid"
```

---

### Task 7: Upgrade rails and add See-all navigation

**Files:**
- Modify: `qml/PosterRail.qml`
- Create: `qml/TheatreSeeAllPage.qml`
- Create: `tests/theatre_see_all_harness.qml`
- Modify: `qml/TheatreWorld.qml`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- `PosterRail` consumes `sourceLabel` and `seeAllPin`, and emits `seeAllRequested(pin)`.
- `TheatreSeeAllPage` consumes `pin`, `malCatalog`, and `showExplicit`.

- [ ] **Step 1: Write the failing See-all harness**

Inject a fake page loader and assert initial offset 0, limit 40, next offset equals loaded count, duplicate end-of-grid requests coalesce, an older generation is ignored after pin change, retry repeats the failed offset, extension missing state names the provider, and item activation preserves identity/type.

- [ ] **Step 2: Run and observe failure**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage SeeAll`.

Expected: missing `TheatreSeeAllPage`.

- [ ] **Step 3: Upgrade `PosterRail`**

Replace `PortraitTile` with `CataloguePosterCard` in portrait mode. Preserve Top 10 numerals. Keep header content to title, factual source attribution, and See all. Do not add `sub` or a rating line under posters.

- [ ] **Step 4: Implement the See-all page**

Compose a back/title/source header, `CataloguePosterGrid`, skeletons, incremental loading, retry, and honest empty state. Call `TheatreApi.loadRowPage`; never duplicate ranking in QML.

- [ ] **Step 5: Host See-all within `TheatreWorld`**

Maintain `property var seeAllPin: null`. When non-null, show `TheatreSeeAllPage` above the tab body; back clears the pin and restores the tab's scroll position. Forward item opens through the existing `theatreItemRequested` signal.

- [ ] **Step 6: Run See-all and routing tests**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage SeeAll
& tests/test_theatre_search_p0.ps1
& tests/test_theatre_series_scroll.ps1
```

Expected: all pass.

- [ ] **Step 7: Commit Task 7**

```powershell
git add qml/PosterRail.qml qml/TheatreSeeAllPage.qml qml/TheatreWorld.qml tests/theatre_see_all_harness.qml tests/test_theatre_deep_catalogue.ps1
git commit -m "feat(theatre): open every shelf into see all"
```

---

### Task 8: Persist Harbor-parity row customization

**Files:**
- Create: `qml/TheatreRowPreferences.qml`
- Create: `qml/TheatreRowControls.qml`
- Create: `tests/theatre_row_preferences_harness.qml`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- Produces the persistence/update API defined above for Task 9.

- [ ] **Step 1: Write a non-vacuous persistence harness**

Use one temporary INI URL. Write a known empty baseline, destroy/reload, then write this Movies state and destroy/reload again:

```json
{"order":["top-rated","top-10"],"hidden":["hidden-gems"],"renamed":{"top-rated":"My Best Movies"}}
```

Assert Shows and Anime remain empty, moving at boundaries is a no-op, empty rename removes the key, new available keys append, removed keys are ignored, and reset clears only the selected tab.

- [ ] **Step 2: Run and observe failure**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage Preferences`.

Expected: missing component failure.

- [ ] **Step 3: Implement QSettings persistence**

Use category `theatreCatalogRows` with three JSON strings: `movies`, `shows`, `anime`. Parse defensively to `{order:[],hidden:[],renamed:{}}`. Emit `changed(pageKey)` only after a real mutation.

- [ ] **Step 4: Implement row controls**

Provide compact move-up, move-down, eye/eye-off, and rename/reset-name actions. Rename uses an inline `TextInput` committed by Enter/focus loss and canceled by Escape. Disable impossible move actions rather than wrapping.

- [ ] **Step 5: Run persistence harness**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage Preferences`.

Expected: `THEATRE_ROW_PREFERENCES_OK` and no temp INI residue.

- [ ] **Step 6: Commit Task 8**

```powershell
git add qml/TheatreRowPreferences.qml qml/TheatreRowControls.qml tests/theatre_row_preferences_harness.qml tests/test_theatre_deep_catalogue.ps1
git commit -m "feat(theatre): persist catalogue row customization"
```

---

### Task 9: Compose the deep tab pages

**Files:**
- Modify: `qml/TheatreCatalogPage.qml`
- Modify: `qml/TheatreWorld.qml`
- Modify: `qml/Main.qml`
- Create: `tests/theatre_catalog_page_harness.qml`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- Consumes Tasks 3–8 and the existing global `ContentPreferences`/`ExplicitContentPolicy`.

- [ ] **Step 1: Write the failing page harness**

Instantiate the page with fake rows, preferences, and content preference. Assert:

- no hero/Continue/Next Up/award/blurb component exists;
- Top 10 renders first by default;
- extension service rows and `From Your Extensions` precede GenreMosaic;
- the extension section hides when empty;
- edit mode shows hidden rows and controls;
- moving/hiding/renaming updates the rendered model;
- reset restores defaults;
- tab change and explicit-setting change increment generation;
- stale callbacks do not alter rows;
- loading skeleton gives way progressively without clearing earlier rows.

- [ ] **Step 2: Run and observe failure**

Run `& tests/test_theatre_deep_catalogue.ps1 -Stage Page`.

Expected: deep-page assertions fail against the current one-row page.

- [ ] **Step 3: Wire options and progressive loading**

Pass `{malCatalog,showExplicit,generation}` to `TheatreApi.loadCatalogPage`. Keep a per-load generation integer. Merge progressive rows by stable key and apply customization after every update.

- [ ] **Step 4: Compose sections without repeated widgets**

Render main rows, the conditional `From Your Extensions` heading/rows, then `GenreMosaic`. Put `Customize rows` near the row area as a quiet control. Do not add a masthead substitute.

- [ ] **Step 5: Thread the one global preference instance**

Expose `property var contentPreferences` on `TheatreWorld` and `TheatreCatalogPage`; bind it from Main's existing `contentPreferences`. Filter through `ExplicitContentPolicy` in the API before preview and See-all ranking. Reload the active surface on `changed()` while preserving the selected tab/pin.

- [ ] **Step 6: Run page and explicit-policy tests**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage Page
& tests/test_explicit_content_policy.ps1
& tests/test_content_preferences.ps1
```

Expected: all pass, including `Berserk`/`Game of Thrones` visibility.

- [ ] **Step 7: Commit Task 9**

```powershell
git add qml/TheatreCatalogPage.qml qml/TheatreWorld.qml qml/Main.qml tests/theatre_catalog_page_harness.qml tests/test_theatre_deep_catalogue.ps1
git commit -m "feat(theatre): compose deep catalogue tabs"
```

---

### Task 10: Full regression, performance, and eyes-on verification

**Files:**
- Modify: `tests/test_theatre_deep_catalogue.ps1`
- Modify: `README.md` only if its test-command section lists feature runners

**Interfaces:**
- Produces the final acceptance evidence; no new product API.

- [ ] **Step 1: Add one `-Stage All` acceptance path**

The runner executes Rules, ApiRows, Cards, DiscoverRegression, SeeAll, Preferences, and Page, then prints exactly `THEATRE_DEEP_CATALOGUE_OK` only when all exit cleanly.

- [ ] **Step 2: Run focused and adjacent regressions**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage All
& tests/test_theatre_top10_genre_boxes.ps1
& tests/test_theatre_anime_parity.ps1
& tests/test_theatre_af2_p0.ps1
& tests/test_theatre_search_p0.ps1
& tests/test_explicit_content_policy.ps1
& tests/test_content_preferences.ps1
node tests/extension_world_isolation_test.mjs
node tests/extension_reorder_world_test.mjs
```

Expected: all exit 0.

- [ ] **Step 3: Build the application**

```powershell
cmake --build native/build-msvc --target colosseum
```

Expected: successful build with no new QML load warnings.

- [ ] **Step 4: Run a live eyes-on matrix**

Launch from the existing master checkout and verify at the supported desktop size:

- landing page still owns hero/Next Up/Continue;
- all three tabs begin with Top 10 and contain no repeated hero or progress row;
- row titles have no blurbs;
- IMDb rating is invisible at rest and visible only on pointer hover;
- Movies rotate only when the UTC day changes;
- Anime paints from the local database with network disabled;
- service extension appears contextually, unknown extension appears in its section, disabling each removes it;
- every shelf See-all pages, loads more, returns, and preserves detail routing;
- customization survives restart independently per tab;
- Explicit Content off hides a source-confirmed explicit fixture but not `Game of Thrones` or `Berserk`;
- empty/failing rows collapse without erasing successful shelves.

- [ ] **Step 5: Inspect scope before final commit**

Run:

```powershell
git status --short
git diff --check
git diff --stat
```

Confirm unrelated pre-existing changes remain unstaged and no TMDB/Trakt/account dependency entered the diff.

- [ ] **Step 6: Commit final verification updates**

```powershell
git add tests/test_theatre_deep_catalogue.ps1 README.md
git commit -m "test(theatre): verify deep catalogue experience"
```

Omit `README.md` from `git add` when Step 1 does not require a documentation change.

---

## Final Definition of Done

- [ ] All 12 acceptance criteria in `docs/superpowers/specs/2026-08-01-theatre-harbor-depth-catalogue-design.md` are evidenced by a named task and passing command.
- [ ] No row blurb or always-visible IMDb rating exists in Movies, Shows, or Anime; the rating uses Discover's hover-only presentation.
- [ ] No branch or worktree was created; execution and commits occurred on `master`.
- [ ] Only task-scoped files were staged; pre-existing dirty-worktree files remain untouched.
- [ ] Focused, adjacent, native build, and eyes-on gates pass.

## Recommended execution method

Use **inline execution with `superpowers:executing-plans`** in the existing master checkout. The tasks repeatedly touch `TheatreApi.js`, the shared poster grid, and the same QML harness runner; one continuous implementer minimizes shared-file collisions and keeps the progressive data contract coherent. Use review checkpoints after Tasks 2, 6, and 9. Do not create a worktree or branch.
