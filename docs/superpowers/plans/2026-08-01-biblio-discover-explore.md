# Biblio Discover and Explore Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Biblio's single Top-10/genre body with a default utilitarian Discover grid and a separate deep Explore shelf page backed by a canonical, daily cached Apple Books + Open Library catalogue.

**Architecture:** A native `BiblioCatalog` service owns provider parsing, canonical work identity, controlled facets, rankings, writable SQLite snapshots, and offline queries. `BiblioDiscoverPage.qml` adapts that service plus compatible catalogue extensions into the existing world-neutral `DiscoverBrowser`; `BiblioExplorePage.qml` renders the Apple Top 10, extension previews, house shelves, mosaics, and persisted shelf customization.

**Tech Stack:** C++17, Qt 6 Core/Network/Sql/QML/Quick, SQLite through `QSQLITE`, QML/JavaScript, PowerShell test gates

## Global Constraints

- Work directly on `master`; do not create a branch or worktree.
- Preserve every unrelated staged, unstaged, and untracked file. Commit only explicit task-owned paths with `git commit --only`.
- All built-in providers are keyless: no API keys, accounts, tokens, or setup prompts.
- Discover is one catalogue picker, one active filter, and one infinite grid; it contains no rails or mosaics.
- Explore owns Top 10, extension shelves, Popular, Top Rated, New Releases, Trending, and the three fixed mosaics.
- No award-based discovery, generated row blurbs, duplicate hero, or duplicate Continue Reading/Collection widget.
- Discovery shows canonical written works only; audiobooks remain nested editions available through search/detail.
- The global `Explicit Content` setting hides only sexually explicit works.
- Ratings and source attribution are hidden at rest and revealed on pointer hover or keyboard focus.
- Apple Books and Open Library refresh at most daily; failed refreshes never replace the last successful snapshot.
- Acquisition availability, download wells, ownership, and reading activity never affect discovery or ranking.

---

## File map

**Create:**

- `native/engine/BiblioCatalogTypes.h` — normalized work, edition, facet, score, and snapshot value types.
- `native/engine/BiblioTaxonomy.h/.cpp` — checked-in controlled facets and raw-source normalization.
- `native/engine/BiblioRanking.h/.cpp` — deterministic house ranking formulas.
- `native/engine/BiblioCanonicalizer.h/.cpp` — Apple/Open Library work and edition reconciliation.
- `native/engine/BiblioProviders.h/.cpp` — Apple/Open Library JSON parsers and keyless request construction.
- `native/engine/BiblioCatalogStore.h/.cpp` — SQLite schema, atomic snapshot publication, paging, and history retention.
- `native/engine/BiblioCatalog.h/.cpp` — QML-facing service and daily refresh coordinator.
- `qml/BiblioDiscoverApi.js` — pure house/extension descriptor, pin, and card normalization helpers.
- `qml/BiblioDiscoverPage.qml` — thin adapter into `DiscoverBrowser`.
- `qml/BiblioExploreRules.js` — pure shelf inventory, extension-row derivation, and preference application.
- `qml/BiblioExplorePreferences.qml` — `QSettings` persistence for shelf order/visibility.
- `qml/BiblioBookRail.qml` — book-specific rail with author-at-rest and hover/focus metadata.
- `qml/BiblioExplorePage.qml` — Explore shelves, customization, and mosaics.
- `tests/fixtures/biblio/apple-rss.json`, `apple-search.json`, `openlibrary-search.json` — deterministic provider fixtures.
- `tests/biblio_catalog_logic_harness.cpp` — taxonomy, canonicalization, parser, and ranking oracle.
- `tests/biblio_catalog_store_harness.cpp` — SQLite snapshot/paging/offline oracle.
- `tests/biblio_catalog_service_harness.cpp` — daily refresh and failure-state oracle with fake transport.
- `tests/biblio_discover_api_harness.qml`, `biblio_discover_page_harness.qml`, `biblio_explore_harness.qml` — offscreen UI contracts.
- `tests/test_biblio_discover_explore.ps1` — focused feature gate.

**Modify:**

- `native/CMakeLists.txt` — compile the service and three native harnesses.
- `native/main.cpp` — expose one `BiblioCatalog` context property using an AppData SQLite path.
- `qml/DiscoverBrowser.qml` — optional author-at-rest and source-on-hover hooks, disabled by default.
- `qml/ExplicitContentPolicy.js` — exact Biblio explicit classification inputs.
- `qml/BiblioWorld.qml` — shared header, default tab, Discover/Explore switching, detail routing, and Explore-return state.
- `qml/BiblioApi.js` — retain search/detail/audiobook responsibilities; remove landing-chart ownership after native cutover.
- `qml/Main.qml` — keep existing book detail/search routes; retire Biblio's old mosaic routing from the world connection path.

---

### Task 1: Controlled facets and ranking semantics

**Files:**
- Create: `native/engine/BiblioCatalogTypes.h`
- Create: `native/engine/BiblioTaxonomy.h`
- Create: `native/engine/BiblioTaxonomy.cpp`
- Create: `native/engine/BiblioRanking.h`
- Create: `native/engine/BiblioRanking.cpp`
- Create: `tests/biblio_catalog_logic_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces `BiblioWork`, `BiblioEdition`, `BiblioFacet`, `BiblioRatingEvidence`, and `BiblioRankSnapshot` structs.
- Produces `BiblioTaxonomy::filterGroups()`, `normalize(axis, raw)`, `lengthKey(pages)`, `eraKey(year)`, and `languageKey(originalLanguage, englishEditionAvailable)`.
- Produces `BiblioRanking::rank(catalogId, works, history, nowUtc)`.

- [ ] **Step 1: Write the failing taxonomy/ranking cases**

Cover exact boundaries (`199`, `200`, `499`, `500`, `799`, `800`), all seven era boundaries, Adult-as-audience, English/Translated, synonym collapse (`Sci-Fi`, `Science fiction`), unknown-tag suppression, Bayesian vote confidence, trailing-12-month releases, and seven-day momentum distinct from Popular.

```cpp
require(BiblioTaxonomy::lengthKey(199) == "short", "199 pages is Short");
require(BiblioTaxonomy::lengthKey(200) == "standard", "200 pages is Standard");
require(BiblioTaxonomy::eraKey(2019) == "2010-2019", "2019 era boundary");
require(BiblioTaxonomy::normalize("genre", "Sci-Fi") == "science-fiction", "genre alias");
require(BiblioTaxonomy::normalize("theme", "Unreviewed random tag").isEmpty(), "unknown tags stay hidden");
require(ids(BiblioRanking::rank("top-rated", works, {}, now)) == QStringList{"broad-4.7", "tiny-5.0"}, "confidence weighting");
```

- [ ] **Step 2: Add the harness target and verify RED**

Run: `cmake --build native/build-msvc --config Release --target biblio_catalog_logic_harness`

Expected: compilation fails because the Biblio model/taxonomy/ranking files do not exist.

- [ ] **Step 3: Implement the value types, curated mapping tables, and pure ranking functions**

Use stable lowercase keys. `rank()` must accept only `popular`, `top-rated`, `new-releases`, and `trending`; an unknown ID returns an empty vector. Use deterministic canonical-ID tie breaks. Top Rated uses a Bayesian prior derived from the candidate population; Trending requires two dated snapshots at least six days apart and returns empty when honest momentum cannot be computed.

The initial curated-publisher filter admits a normalized publisher only after at least 25 canonical works in the active snapshot map to it. Keep that threshold as the named constant `kPublisherCoverageFloor` so a future taxonomy migration can change it deliberately.

- [ ] **Step 4: Build and run the harness**

Run: `native\build-msvc\Release\biblio_catalog_logic_harness.exe`

Expected: `BIBLIO_CATALOG_LOGIC_OK` and exit `0`.

- [ ] **Step 5: Commit only Task 1 paths**

```powershell
git add native/engine/BiblioCatalogTypes.h native/engine/BiblioTaxonomy.h native/engine/BiblioTaxonomy.cpp native/engine/BiblioRanking.h native/engine/BiblioRanking.cpp tests/biblio_catalog_logic_harness.cpp native/CMakeLists.txt
git commit --only -m "feat(biblio): define catalogue facets and rankings" -- native/engine/BiblioCatalogTypes.h native/engine/BiblioTaxonomy.h native/engine/BiblioTaxonomy.cpp native/engine/BiblioRanking.h native/engine/BiblioRanking.cpp tests/biblio_catalog_logic_harness.cpp native/CMakeLists.txt
```

### Task 2: Provider parsing and canonical work identity

**Files:**
- Create: `native/engine/BiblioProviders.h`
- Create: `native/engine/BiblioProviders.cpp`
- Create: `native/engine/BiblioCanonicalizer.h`
- Create: `native/engine/BiblioCanonicalizer.cpp`
- Create: `tests/fixtures/biblio/apple-rss.json`
- Create: `tests/fixtures/biblio/apple-search.json`
- Create: `tests/fixtures/biblio/openlibrary-search.json`
- Modify: `tests/biblio_catalog_logic_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces `BiblioProviders::parseAppleRss(QByteArray)`, `parseAppleSearch(QByteArray)`, `parseOpenLibrarySearch(QByteArray)`, and keyless URL builders.
- Produces `BiblioCanonicalizer::merge(const QList<BiblioSourceRecord>&)` returning canonical `BiblioWork` values with nested editions and field provenance.

- [ ] **Step 1: Add fixture-driven failing parser and merge tests**

Fixtures must include RSS singleton/array forms, missing art/rating, HTML descriptions, shared ISBNs, title collisions by different authors, ebook/audiobook forms, an English translation, and two ordinary formats of one work.

```cpp
const auto works = BiblioCanonicalizer::merge(records);
require(works.size() == 4, "ordinary formats merge without title-only collisions");
require(find(works, "shared-isbn").editions.size() == 3, "ebook, print, and audio nest");
require(find(works, "translated-work").originalLanguage == "fr", "translation lineage retained");
```

- [ ] **Step 2: Run the logic harness and verify RED**

Expected: missing parser/canonicalizer symbols.

- [ ] **Step 3: Implement defensive JSON parsing and layered identity resolution**

Resolve by Open Library work key, ISBN/authority IDs, then normalized title+author+publication evidence. Never merge on title alone. Preserve per-field `source`, `sourceId`, and `observedAt`. Apple owns Apple rating/chart/art; Open Library owns work identity/first-publish evidence.

- [ ] **Step 4: Run the logic harness**

Expected: `BIBLIO_CATALOG_LOGIC_OK`.

- [ ] **Step 5: Commit Task 2**

```powershell
git commit --only -m "feat(biblio): canonicalize Apple and Open Library records" -- native/engine/BiblioProviders.h native/engine/BiblioProviders.cpp native/engine/BiblioCanonicalizer.h native/engine/BiblioCanonicalizer.cpp tests/fixtures/biblio/apple-rss.json tests/fixtures/biblio/apple-search.json tests/fixtures/biblio/openlibrary-search.json tests/biblio_catalog_logic_harness.cpp native/CMakeLists.txt
```

### Task 3: Atomic SQLite snapshot store

**Files:**
- Create: `native/engine/BiblioCatalogStore.h`
- Create: `native/engine/BiblioCatalogStore.cpp`
- Create: `tests/biblio_catalog_store_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces `open(path)`, `publish(snapshot)`, `page(catalogId, facetAxis, facetKey, includeExplicit, offset, limit)`, `filterGroups(includeExplicit)`, `previewRows(limit, includeExplicit)`, `top10(limit, includeExplicit)`, `lastSuccessUtc()`, and `hasSnapshot()`.
- `page()` returns `{items,nextOffset,exhausted,freshness,warning}` in the `DiscoverBrowser` contract.

- [ ] **Step 1: Write the failing temporary-database harness**

Pin schema tables `works`, `editions`, `work_sources`, `work_facets`, `rankings`, `ranking_history`, and `sync_meta`. Test stable paging, exact facet filtering, explicit gating, canonical uniqueness, seven-day retention, cached Top 10, and rollback after an invalid staged snapshot.

- [ ] **Step 2: Add the target and verify RED**

Run the target from `native/build-msvc` so `qsqlite.dll` resolves.

- [ ] **Step 3: Implement schema creation, bound queries, and transactional publication**

Write a complete candidate snapshot into staging tables inside one transaction; validate foreign keys, unique canonical IDs, ranking references, and known facet keys before swapping the active snapshot ID. Keep eight daily ranking snapshots, sufficient for a seven-day delta. Clamp offset to `>=0` and limit to `[1,100]`.

- [ ] **Step 4: Run the store harness**

Expected: `BIBLIO_CATALOG_STORE_OK`.

- [ ] **Step 5: Commit Task 3**

```powershell
git commit --only -m "feat(biblio): persist atomic catalogue snapshots" -- native/engine/BiblioCatalogStore.h native/engine/BiblioCatalogStore.cpp tests/biblio_catalog_store_harness.cpp native/CMakeLists.txt
```

### Task 4: Daily keyless refresh service

**Files:**
- Create: `native/engine/BiblioCatalog.h`
- Create: `native/engine/BiblioCatalog.cpp`
- Create: `tests/biblio_catalog_service_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp`

**Interfaces:**
- Exposes context property `BiblioCatalog`.
- Properties: `ready`, `refreshing`, `stale`, `offline`, `revision`, `lastSuccessfulRefresh`, `lastError`.
- Invokables: `refreshIfDue(bool force = false)`, `discoverPage(...)`, `filterGroups(bool)`, `exploreRows(int,bool)`, and `mosaic(QString,int,bool)`.
- Signals: `revisionChanged()`, `refreshFinished(bool)`, and state-property notifications.

- [ ] **Step 1: Write a fake-transport service harness**

Prove request coalescing, one refresh per local day, forced refresh, bounded concurrency, cached first paint, partial-provider failure, total failure preserving the old snapshot, and first-run failure leaving `ready=false`.

- [ ] **Step 2: Verify the service harness fails to build**

- [ ] **Step 3: Implement the coordinator and injectable HTTP seam**

Use `QNetworkAccessManager` in production and a small injected transport interface in the harness. Fetch Apple global/genre RSS candidate feeds, bounded Apple metadata enrichment, and Open Library work/edition enrichment. Cap concurrent enrichment requests at four, deduplicate URLs, use bounded retry/backoff for `429`/transient failures, and cancel obsolete generations. Publish only after the entire normalized snapshot validates.

- [ ] **Step 4: Wire the writable database path**

Construct the service at `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/catalog/biblio-v1.sqlite"`, expose it before loading `Main.qml`, and call `refreshIfDue()` without blocking startup.

- [ ] **Step 5: Run service, store, and logic harnesses**

Expected markers: `BIBLIO_CATALOG_SERVICE_OK`, `BIBLIO_CATALOG_STORE_OK`, `BIBLIO_CATALOG_LOGIC_OK`.

- [ ] **Step 6: Commit Task 4**

```powershell
git commit --only -m "feat(biblio): add daily cached catalogue service" -- native/engine/BiblioCatalog.h native/engine/BiblioCatalog.cpp tests/biblio_catalog_service_harness.cpp native/CMakeLists.txt native/main.cpp
```

### Task 5: Biblio adapter for the shared Discover shell

**Files:**
- Create: `qml/BiblioDiscoverApi.js`
- Create: `tests/biblio_discover_api_harness.qml`
- Create: `qml/BiblioDiscoverPage.qml`
- Create: `tests/biblio_discover_page_harness.qml`
- Modify: `qml/DiscoverBrowser.qml`

**Interfaces:**
- `BiblioDiscoverApi` produces the existing adapter contract: `types`, `catalogs`, `defaultCatalog`, `filters`, `resolvePin`, and normalized extension cards.
- `BiblioDiscoverPage.applyPin(pin, returnToExplore)` opens a built-in/extension catalogue with an optional Explore-return affordance.

- [ ] **Step 1: Write failing pure adapter tests**

Assert one `book` type, four built-ins in order, non-core enabled input-free book catalogues under `From Your Extensions`, exclusion of core Apple Books and acquisition wells, exact normalized filters, extension metadata compatibility, and missing-extension fallback to Popular.

- [ ] **Step 2: Write the failing page construction/behavior harness**

Inject fake catalogue/extension seams; assert default Popular, one-filter replacement, paging, stale callback rejection, author-at-rest, hover/focus fields, offline warning, card activation, and Explore-return signaling.

- [ ] **Step 3: Extend `DiscoverBrowser` without changing other worlds by default**

Add `property bool showAuthorAtRest: false`, `property bool showSourceOnReveal: false`, and `signal backRequested()`. Render `item.author` only when enabled; render `item.source` in the reveal only when enabled; include keyboard focus in the reveal condition. Add an optional back affordance controlled by `property bool showBackAction: false`.

- [ ] **Step 4: Implement the Biblio adapter and wrapper**

Built-in fetches call `BiblioCatalog.discoverPage`; extension fetches reuse `DiscoverApi.catalogByKey`, `selectionsForFilter`, and `loadPage`. Give the wrapper an injected `property bool showExplicit: false` and apply `ExplicitContentPolicy.visible("biblio", item, showExplicit)` before returning extension cards. Do not reach for Main's private `contentPreferences` ID from a separately loaded component.

- [ ] **Step 5: Run the new harnesses plus the existing shared-shell gate**

Run: `powershell -ExecutionPolicy Bypass -File tests/test_discover_shared_shell.ps1`

Expected: existing Theatre/shared-shell behavior remains green.

- [ ] **Step 6: Commit Task 5**

```powershell
git commit --only -m "feat(biblio): add utilitarian Discover adapter" -- qml/BiblioDiscoverApi.js qml/BiblioDiscoverPage.qml qml/DiscoverBrowser.qml tests/biblio_discover_api_harness.qml tests/biblio_discover_page_harness.qml
```

### Task 6: Explore inventory and persisted customization

**Files:**
- Create: `qml/BiblioExploreRules.js`
- Create: `qml/BiblioExplorePreferences.qml`
- Create: `tests/biblio_explore_harness.qml`

**Interfaces:**
- `defaultRows()` returns stable keys in order: `top-10`, extension keys, `popular`, `top-rated`, `new-releases`, `trending`.
- `applyCustomization(rows, {order,hidden}, editMode)` returns copies and appends new extension keys safely.
- Preferences expose `order`, `hidden`, `move(key,toIndex)`, `setVisible(key,bool)`, and `reset()`.

- [ ] **Step 1: Write failing rules and `QSettings` persistence cases**

Prove exact default order, empty extension-section collapse, stable extension keys, hide/show, drag-equivalent move, new-extension append, removed-key ignore, mosaics excluded from preferences, reload persistence, and reset.

- [ ] **Step 2: Run offscreen and verify RED**

- [ ] **Step 3: Implement pure rules and the settings wrapper**

Use category `biblioExplore` with compact JSON strings for order and hidden keys. Never persist display titles. Expose move-up/down functions for keyboard equivalence even though pointer ordering uses drag handles.

- [ ] **Step 4: Run the offscreen harness**

Expected: `BIBLIO_EXPLORE_RULES_OK`.

- [ ] **Step 5: Commit Task 6**

```powershell
git commit --only -m "feat(biblio): persist Explore shelf customization" -- qml/BiblioExploreRules.js qml/BiblioExplorePreferences.qml tests/biblio_explore_harness.qml
```

### Task 7: Book rails, Explore page, and pinned mosaic navigation

**Files:**
- Create: `qml/BiblioBookRail.qml`
- Create: `qml/BiblioExplorePage.qml`
- Modify: `tests/biblio_explore_harness.qml`

**Interfaces:**
- `BiblioExplorePage` signals `itemRequested(work)`, `discoverPinRequested(pin)`, and persists/restores `contentY`.
- Shelf pins use `{type:"book", catalogId, filterGroup, filterKey, sourceKind, extension identity}`.

- [ ] **Step 1: Extend the failing Explore harness**

Assert Top 10 first, extensions second, four house rails, Fiction/Nonfiction/Audience mosaics fixed last, See-All pins, author-at-rest, rating/source focus reveal, no blurbs, drag handles only in edit mode, keyboard move actions, and mosaic pins.

- [ ] **Step 2: Implement `BiblioBookRail`**

Use cover/title/author at rest. Hover or `activeFocus` reveals rating and source. Ranked mode displays only Top-10 numerals. `See All` is present for every rendered shelf.

- [ ] **Step 3: Implement `BiblioExplorePage`**

Read previews/mosaics from `BiblioCatalog`; derive extension previews through the same extension catalogue transport used by Discover; filter explicit records; render loading lanes independently; collapse empty failed extension rows; keep mosaics outside customization.

- [ ] **Step 4: Implement pointer drag plus keyboard-equivalent ordering**

Drag changes only a temporary visible order; persist once the drop completes. Provide accessible Move Up/Move Down actions using the same preference method. Do not write `QSettings` for every pointer move.

- [ ] **Step 5: Run the Explore harness**

Expected: `BIBLIO_EXPLORE_PAGE_OK`.

- [ ] **Step 6: Commit Task 7**

```powershell
git commit --only -m "feat(biblio): build the Explore shelf page" -- qml/BiblioBookRail.qml qml/BiblioExplorePage.qml tests/biblio_explore_harness.qml
```

### Task 8: Integrate BiblioWorld and existing detail/search behavior

**Files:**
- Modify: `qml/BiblioWorld.qml`
- Modify: `qml/BiblioApi.js`
- Modify: `qml/Main.qml`
- Create: `tests/biblio_world_harness.qml`

**Interfaces:**
- `BiblioWorld.activeTab` defaults to `discover` and is never restored from settings.
- Explore See-All/mosaic calls set `activeTab="discover"`, apply the pin, and remember Explore `contentY`; Discover Back restores Explore and its scroll.

- [ ] **Step 1: Write the failing world harness**

Assert one shared Featured/Continue Reading/Collection area, exact two-tab order, Discover default, no Discover rails/mosaics, Explore-only shelves, pinned navigation/back restoration, and canonical card opening through `bookRequested`.

- [ ] **Step 2: Replace the old Top-10/mosaic body**

Keep the three shared widgets. Add `WorldTabBar` with Discover/Explore. Give Discover a viewport-height body and Explore its natural shelf height. Hydrate Featured from cached Top 10 when available and retain the existing static fallback only before the first successful sync.

Add `property bool showExplicit: false` to `BiblioWorld`, pass it into both tab pages, and in Main's world-loader `onLoaded` bind that property to `contentPreferences.showExplicit` when the loaded world exposes it. A preference change must bump the active model generation and reload without retaining an explicit item from the previous page.

- [ ] **Step 3: Preserve detail and audiobook behavior**

Keep `BiblioApi.search`, `lookupBook`, `searchAudiobooks`, and pairing helpers. Change only landing-chart ownership. When a canonical work already carries full detail metadata, open it directly; otherwise use the existing Apple lookup fallback. Do not add audiobooks to Discover/Explore.

- [ ] **Step 4: Retire old genre-route connections from BiblioWorld**

Stop emitting `biblioGenreRequested`/`biblioGenreIndexRequested` from the world. Leave legacy page files intact for compatibility until a later cleanup; all new mosaic navigation goes through Discover pins.

- [ ] **Step 5: Run world, search, detail, and shared-shell gates**

Expected: Biblio search/audiobook tests remain green and `BIBLIO_WORLD_OK` prints.

- [ ] **Step 6: Commit Task 8**

```powershell
git commit --only -m "feat(biblio): split books into Discover and Explore" -- qml/BiblioWorld.qml qml/BiblioApi.js qml/Main.qml tests/biblio_world_harness.qml
```

### Task 9: Explicit-content, extension, and failure hardening

**Files:**
- Modify: `qml/ExplicitContentPolicy.js`
- Modify: `tests/explicit_content_policy_harness.qml`
- Modify: `tests/biblio_discover_api_harness.qml`
- Modify: `tests/biblio_explore_harness.qml`
- Create: `tests/test_biblio_discover_explore.ps1`

**Interfaces:**
- `ExplicitContentPolicy.visible("biblio", item, showExplicit)` is the sole Biblio UI gate.
- Extension removal invalidates open pins and safely falls back to Popular with an explanation.

- [ ] **Step 1: Add failing classification and lifecycle cases**

Test explicit provider flags/controlled explicit subjects, `Adult` readership remaining visible, mature horror/violence remaining visible, preference changes reloading active grids, extension removal during paging, unsupported extension filters, offline stale banner, and no-cache first-sync state.

- [ ] **Step 2: Implement exact Biblio classification**

Accept only source-confirmed explicit flags or controlled sexually explicit facet IDs. Do not inspect broad maturity words, ratings, descriptions, or `Adult` audience to infer explicitness.

- [ ] **Step 3: Add the focused PowerShell gate**

The gate builds/runs the three native harnesses, runs the four Biblio QML harnesses offscreen, checks unique OK markers and exit codes, and statically rejects `api_key`, award row labels, a second hero/Continue widget inside tab pages, and row blurb fields.

- [ ] **Step 4: Run the focused gate**

Run: `powershell -ExecutionPolicy Bypass -File tests/test_biblio_discover_explore.ps1`

Expected: `BIBLIO_DISCOVER_EXPLORE_OK`.

- [ ] **Step 5: Commit Task 9**

```powershell
git commit --only -m "test(biblio): harden discovery policy and failures" -- qml/ExplicitContentPolicy.js tests/explicit_content_policy_harness.qml tests/biblio_discover_api_harness.qml tests/biblio_explore_harness.qml tests/test_biblio_discover_explore.ps1
```

### Task 10: Full regression, eyes-on verification, and handoff

**Files:**
- Modify only if a verified failure requires an in-scope correction.

**Interfaces:**
- Produces a clean verification record against the Definition of Done.

- [ ] **Step 1: Run focused and adjacent automated gates**

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_biblio_discover_explore.ps1
powershell -ExecutionPolicy Bypass -File tests/test_discover_shared_shell.ps1
powershell -ExecutionPolicy Bypass -File tests/test_content_preferences.ps1
powershell -ExecutionPolicy Bypass -File tests/test_explicit_content_policy.ps1
powershell -ExecutionPolicy Bypass -File tests/test_theatre_deep_catalogue.ps1
```

Expected: all exit `0` with their success markers.

- [ ] **Step 2: Build the application**

Run: `cmake --build native/build-msvc --config Release --target colosseum`

Expected: successful Release build with the SQLite driver deployed beside the executable.

- [ ] **Step 3: Perform eyes-on acceptance**

Verify at normal and narrow supported window sizes: shared rows appear once; Discover is utilitarian; every catalogue/filter pages; author is always visible; rating/source appear only on hover/focus; Explore order is correct; drag and keyboard ordering persist; every See-All/mosaic pin returns to the same Explore position; offline cache remains browsable; no award content appears.

- [ ] **Step 4: Review every Definition-of-Done item as MET/PARTIAL/NOT-MET**

Do not declare completion with any PARTIAL or NOT-MET item. Fix only verified in-scope failures and rerun the affected gates.

- [ ] **Step 5: Commit any final in-scope corrections explicitly**

If verification required a correction, inspect `git diff --name-only`, stage only the verified Biblio files, and pass those same literal paths to `git commit --only -m "fix(biblio): close Discover and Explore acceptance gaps"`. If no correction was required, do not create an empty commit.

- [ ] **Step 6: Report outcome without touching unrelated worktree changes**

Include commits, test commands/results, eyes-on observations, remaining external limitations, and confirmation that no branch/worktree was created.
