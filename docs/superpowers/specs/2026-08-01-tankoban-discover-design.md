# Tankoban Discover Design

**Date:** 2026-08-01
**Decision owner:** Hemanth
**Design:** Scoped Codex helper
**Status:** Approved in brainstorming; written-spec review pending

## 1. Goal

Add a first-class **Discover** tab to Tankoban with the same compact, utilitarian interaction model as Theatre Discover. The default Tankoban tab order becomes:

`Discover · Manga · Comics`

Discover has an internal `Manga · Comics` type switch, one catalogue picker, one grouped single-choice filter, and an infinite series-cover wall. It is a deep catalogue browser, not another editorial homepage.

The implementation must share the reusable browsing shell with Theatre while allowing each world to retain its own catalogue definitions, filters, cards, identities, and detail routes.

## 2. Locked product decisions

1. Discover is first and default, matching Theatre.
2. Tankoban Discover separates Manga and Comics inside one shared entrance.
3. The interaction model is direct Theatre parity: type switch, named catalogue picker, source attribution, one filter, paged cover wall, loading/empty/error states, and cover-to-detail routing.
4. The page is intentionally utilitarian. No preview pane, editorial hero, decorative shelf stack, or direct-download control belongs inside Discover.
5. Current discovery catalogues are first-party and bundled with Colosseum. Download providers remain extensions and are not discovery catalogues.
6. The catalogue contract must accept future Tankoban discovery extensions in the same way Theatre accepts extension catalogues.
7. Built-in catalogue entries appear together. When extension catalogues eventually exist, the picker groups them beneath an `Extensions` heading after the built-ins.
8. Only one filter choice may be active at a time:
   - Manga menu groups `Genres` and `Demographics`.
   - Comics menu groups `Genres` and `Publishers`.
9. Manga, manhwa, and manhua all live under Manga. Their origin/type may appear as card metadata but is not a separate launch filter.
10. The wall contains series only. A recent volume or issue may affect a series' ranking, but individual releases never become Discover tiles.
11. Selecting a tile opens the existing Manga or Comics series page. Acquisition decisions remain on that page.
12. Discovery is offline-first. Bundled catalogues paint immediately; live sources refresh metadata and ordering in the background.
13. The full catalogue remains discoverable. Working acquisition paths receive a modest rank boost, but unavailable titles are not removed.
14. Comics use one Tankoban house ranking. LOCG, GCD, GetComics, and other ingredients are not exposed as separate user-facing catalogue brands.
15. Existing Tankoban browse shelves gain Theatre-style `See all` doors that open Discover pinned to the matching type, catalogue, and optional filter.
16. A persisted global setting named **Explicit Content** governs only sexually explicit material across Colosseum. Violence, horror, mature themes, and standard age ratings do not count as explicit. *Berserk* and *Game of Thrones* remain visible when the setting is off.

## 3. User experience

### 3.1 Placement and default state

`TankobanWorld.qml` retains the same upper structure as Theatre:

1. Featured in Tankoban
2. Next Up
3. Continue Reading
4. `Discover · Manga · Comics` tab bar
5. Active tab content

`activeTab` defaults to `discover`. Manga and Comics retain their current curated browse pages. Discover adds a deep wall; it does not replace those pages.

The default Discover state is:

- Type: `Manga`
- Catalogue: `Popular`
- Filter: none
- Page: first page

Switching between Manga and Comics restores the most recent catalogue, filter, loaded items, and scroll position for each type during the current app session. Returning from a series page restores the same Discover state. This state does not need cross-restart persistence in the first release.

### 3.2 Header and controls

The shared shell preserves Theatre's anatomy:

- Left: compact type switch.
- Right: `NOW BROWSING`, the selected catalogue name as the picker trigger, and an attribution byline.
- Below: a `FILTER` label and one picker.
- Main body: a dense, paged cover wall.

Built-in byline: `Tankoban built-in catalogue`.

Future extension byline: the extension's display name. The catalogue picker groups entries as:

1. `Tankoban`
2. `Extensions` when at least one compatible extension catalogue exists

No empty `Extensions` heading is shown at launch.

### 3.3 Catalogue sets

#### Manga

- **Trending** — positive movement between the bundled baseline and refreshed live popularity data. Until two comparable snapshots exist, fall back to Popular and do not invent a momentum claim.
- **Popular** — normalized MAL membership/popularity.
- **Top Rated** — Bayesian-weighted score with a minimum voter-confidence term so small samples do not outrank broadly established titles.
- **New Releases** — series whose publication start date is newest, with invalid/future dates rejected.

The bundled Kaggle/MAL database is the immediate source. Jikan refreshes MAL-backed score, vote count, members/popularity, favourites, status, genres, demographics, type, and dates. A live refresh may reorder or enrich the wall without clearing already visible results.

#### Comics

- **Popular** — the Tankoban house ranking defined in section 5.2.
- **New Releases** — series ordered by their newest known issue/release activity, not by database modification time.
- **Most Stocked** — series ordered by known issue depth, with house rank and acquisition availability as tie-breakers.
- **All Series** — alphabetical, normalized by canonical title and start year.

GCD provides canonical identity and bibliographic structure. The existing LOCG-derived ranking and GCD × GetComics catalogue provide popularity and availability inputs. No new live Comic Vine dependency is introduced. Metron may be evaluated later for identity and freshness enrichment, but it is not a launch dependency.

### 3.4 Filters

The filter is a single grouped menu. Selecting a new value replaces the previous value, even when it belongs to another group. `All` clears the filter.

Manga:

- `Genres`
- `Demographics`

Comics:

- `Genres`
- `Publishers`

Each filter value carries a stable key distinct from its display label. Renames must not break a saved in-session pin. Filters operate on canonical normalized fields, not title keyword guesses.

Multi-filter discovery, origin/country filtering, year ranges, completion status, and compound queries are explicitly deferred. The shared contract must not prevent adding them later to both Theatre and Tankoban.

### 3.5 Cards and opening behavior

Cards reuse Theatre Discover's density, keyboard behavior, focus treatment, skeleton loading, paging, and click/Enter interaction.

Minimum normalized card fields:

```text
id, type, title, cover, year, rating, format, availability, explicit
```

Manga metadata line: year, rating when trustworthy, and format (`Manga`, `Manhwa`, or `Manhua`).
Comics metadata line: start year, publisher, and rating only when the house data carries a meaningful comparable value.

Availability may be represented with quiet metadata and affects ordering, but there is no download button or source badge on the cover. Opening a card emits the normalized item and routes through the existing Manga or Comics series door.

### 3.6 See-all deep links

The existing Manga and Comics tabs may emit a pin:

```js
{
  type: "manga" | "comics",
  catalogId: "popular" | "trending" | "top-rated" | "new-releases" | "most-stocked" | "all",
  filterGroup: "genre" | "demographic" | "publisher" | "",
  filterKey: "stable-key-or-empty"
}
```

Examples:

- `Top in Tankoban — Manga` → Manga / Popular
- a Seinen genre/demographic door → Manga / selected classification
- `Top in Tankoban — Comics` → Comics / Popular
- `Marvel` → Comics / publisher: Marvel
- `Most Stocked` → Comics / Most Stocked

Applying a pin selects Discover, validates every key against the active adapter, resets paging, and scrolls to the wall's start. An invalid/stale filter is dropped while the valid type/catalogue portion is preserved.

## 4. Shared architecture

### 4.1 Reusable shell

Extract Theatre's generic browsing mechanics into `qml/DiscoverBrowser.qml`:

- type tabs
- catalogue menu and grouped headings
- one filter picker
- paged wall and keyboard navigation
- skeleton, empty, missing-catalogue, offline, and incremental-loading states
- stale-response generation fence
- in-session state per type
- `applyPin(pin)` validation
- `itemOpenRequested(item)` signal

The shell receives an adapter object and never imports a world API directly. It does not know what Manga, Comics, Movies, or Shows mean.

Keep `qml/DiscoverPage.qml` as Theatre's compatibility wrapper so existing `TheatreWorld.qml` wiring and tests do not churn unnecessarily. It supplies the Theatre adapter backed by the existing `DiscoverApi.js` behavior.

Add `qml/TankobanDiscoverPage.qml` as the Tankoban wrapper. It supplies the Tankoban adapter and converts normalized card-open events into Manga or Comics series signals.

### 4.2 Adapter contract

The shell expects these adapter operations:

```js
types() -> [{ key, label }]
catalogs(type) -> [{ key, title, sourceKind, attribution }]
filters(type, catalog) -> [{ group, options:[{ key, label }] }]
defaultCatalog(type) -> key
resolvePin(pin) -> validated state
fetchPage(state, cursor, generation, callback)
normalizeItem(raw, type) -> normalized card
```

`fetchPage` returns:

```js
{
  items: [],
  nextCursor: null | string | number,
  exhausted: boolean,
  freshness: "bundled" | "cached" | "live",
  warning: ""
}
```

The callback must echo the request generation. The shell ignores responses from an older generation after type, catalogue, or filter changes.

### 4.3 Tankoban catalogue adapter

Add `qml/TankobanDiscoverApi.js` as the QML-facing adapter. It performs no acquisition and owns no series-page UI. Its responsibilities are:

- enumerate the two types and built-in catalogues;
- expose grouped filter values;
- page the bundled `MalCatalog` and `ComicsCatalog` stores;
- request background Jikan refreshes through the existing network path;
- merge live updates by canonical identity;
- calculate deterministic ranking inputs;
- apply Explicit Content policy;
- normalize cards and availability;
- append future extension catalogues after built-ins.

Native catalogue APIs should expose query/page operations instead of returning every row to QML for repeated full-array sorting. Exact C++ method names belong in the implementation plan after inspecting the current `MalCatalog` and `ComicsCatalog` query surfaces.

### 4.4 Future discovery extensions

Tankoban-compatible extension catalogues use the same normalized catalogue descriptor consumed by the shell. They declare:

- world: `tankoban`
- type: `manga` or `comics`
- stable catalogue id and title
- supported single-choice filter, if any
- pagination capability
- normalized series results
- explicit-content hints

This is a consumption seam, not a requirement to ship a Tankoban discovery extension now. Download-source extensions remain a separate resource class and never appear in the Discover catalogue picker merely because they can acquire files.

## 5. Ranking and data policy

### 5.1 General rules

- Ranking is deterministic for the same input snapshot.
- Missing values are neutral; they never become zero-quality penalties unless the catalogue definition explicitly requires that field.
- Availability is a boost, not an inclusion gate.
- Metadata completeness is a tie-breaker, not a substitute for popularity.
- A source's database update timestamp is not treated as release recency.
- Every score component is normalized before blending.
- The UI never displays the internal composite number as if it were a public rating.

### 5.2 Comics house ranking

The initial Popular composite is:

- 65% normalized existing LOCG-derived popularity/rank
- 20% acquisition availability confidence from the GCD × GetComics catalogue
- 10% recent real release activity
- 5% identity/metadata confidence

If LOCG rank is absent, redistribute its weight proportionally across available non-metadata signals; do not assign an arbitrary worst rank. Metadata confidence alone may contribute at most 10% after redistribution.

Availability confidence distinguishes a canonical series match with downloadable releases from a weak title-only match. Recent activity uses known publication/release dates with bounded decay. Identity confidence rewards stable GCD/LOCG cross-links and penalizes ambiguous title collisions only as a tie-breaker.

The ranking pipeline must emit per-component diagnostics to a test/debug surface so unexpected ordering can be explained without exposing those internals in the production UI.

### 5.3 Manga ranking

Popular and Top Rated use the bundled MAL-derived fields immediately. Live Jikan data may update the same canonical MAL id. A live result with no stable identity does not replace an existing bundled row solely by title similarity.

Top Rated uses a Bayesian weighted score based on mean score, vote count, and the catalogue-wide mean. Trending requires comparable snapshots; it never aliases a raw rating while labeled Trending.

### 5.4 Refresh and caching

- Opening Discover never waits for the network before painting bundled results.
- Refreshes are deduplicated by type/catalogue/filter and obey existing Jikan pacing.
- Successfully normalized live responses are cached with fetch time and source identity.
- Failed refreshes leave bundled/cached rows in place and show at most one quiet warning.
- A refresh can reorder items only when the user has not begun direct keyboard or pointer interaction with the current wall. Otherwise the refreshed ordering applies on the next reload, preventing covers from moving under the user's hand.

## 6. Global Explicit Content setting

### 6.1 Meaning

Add a persisted global preference with this exact presentation:

**Explicit Content**

> Show sexually explicit titles across Theatre, Tankoban, and Biblio. Violence, horror, mature themes, and standard age ratings are not filtered.

Default: `Off`.

This is not named `Mature Content`. An R/TV-MA/18 rating, a `Mature Readers` imprint label, graphic violence, horror, profanity, or dark themes must never trigger the gate by itself.

### 6.2 Storage and surface

Add a small global settings surface reachable from a settings icon in the Colosseum taskbar, using the existing `assets/icons/settings.svg`. The first page may contain only a `Content` section and the Explicit Content toggle; its component boundary must allow future global preferences without redesigning the taskbar again.

Persist the setting through a single QML `Settings`-backed source of truth with key `content/showExplicit`, default `false`, plus a revision/change signal. All world adapters consume that one value; no per-world duplicate settings are created.

### 6.3 Classification policy

Filtering is conservative and source-aware:

- Manga: explicit source flags and explicit classifications such as Hentai may gate; ordinary Ecchi, violence, or demographic labels do not gate by themselves.
- Comics: an explicit/adult source classification may gate; `Mature Readers`, horror, or violent imprints do not gate by themselves.
- Theatre: explicit/pornographic source classification may gate; R, NC-17, TV-MA, violence, and sexual-content advisories alone do not gate.
- Biblio: explicitly erotic/pornographic classification may gate; romance, literary sexuality, horror, and adult readership do not gate by themselves.
- Unknown classification defaults to visible. False positives are worse than incomplete gating.

A small curated allow/deny override table handles known source misclassifications. The policy helper returns both `explicit: bool` and a reason code for tests/debugging.

The setting applies to world discovery/browse results and future compatible catalogue extensions. It does not silently change the existing security policy for installing extensions that declare `behaviorHints.adult`; extension-install policy requires a separate explicit decision.

## 7. Loading, empty, and failure behavior

- **Bundled data ready:** paint immediately.
- **Live refresh running:** keep the wall interactive; no full-page spinner.
- **Pagination running:** append skeleton cards at the end.
- **No matches after a filter:** `No series match this filter.` with a `Clear filter` action.
- **Catalogue empty:** `This catalogue answered with nothing.`
- **Bundled store unavailable:** show an honest unavailable state for that type; do not fall through to a different type or fabricate curated rows.
- **Live refresh fails:** retain local results and show a quiet `Showing offline catalogue` status.
- **Future extension removed while pinned:** fall back to that type's built-in default and explain that the catalogue is no longer installed.
- **Cover failure:** retain the card with the standard art placeholder.
- **Series route cannot resolve:** keep Discover state and display the existing routing error; never initiate a download as fallback.

## 8. Files and boundaries

Expected design boundaries; the implementation plan may refine exact filenames after source inspection:

- New `qml/DiscoverBrowser.qml` — shared generic shell.
- Retained/refactored `qml/DiscoverPage.qml` — Theatre compatibility wrapper.
- Retained/refactored `qml/DiscoverApi.js` — Theatre adapter behavior.
- New `qml/TankobanDiscoverPage.qml` — Tankoban wrapper and routing signals.
- New `qml/TankobanDiscoverApi.js` — Tankoban adapter, ranking, refresh, filters, pins.
- Modified `qml/TankobanWorld.qml` — `Discover · Manga · Comics`, default Discover, loader/wiring.
- Modified `qml/TankobanMangaTab.qml` and `qml/TankobanComicsTab.qml` — See-all pins.
- Extended native `MalCatalog` and `ComicsCatalog` query/page seams where required.
- New global content-preference source and `qml/SettingsPage.qml`.
- Modified `qml/Taskbar.qml` and `qml/Main.qml` — settings entry/layer and shared preference wiring.
- Shared explicit-classification helper used by Theatre, Tankoban, and Biblio browse/discovery adapters.

The shared shell owns interaction only. World adapters own catalogue semantics. Native stores own indexed data access. Existing series pages own detail and acquisition. These boundaries must not be crossed for convenience.

## 9. Verification and acceptance

### 9.1 Automated contracts

Add focused tests for:

1. Tankoban tabs are `Discover · Manga · Comics`; Discover is first/default.
2. Theatre and Tankoban instantiate the shared Discover shell.
3. Theatre's existing catalogue, pin, filter, pagination, and item-open behavior remains green.
4. Manga and Comics keep independent in-session Discover states.
5. Only one grouped filter value can be active.
6. Manga filter groups contain Genres and Demographics; Comics contains Genres and Publishers.
7. Every result is a series card and opens the correct existing series page.
8. See-all pins select the expected type/catalogue/filter and reject stale keys safely.
9. Bundled results render before delayed live refresh completion.
10. Stale refresh responses cannot replace a newer selection.
11. Pagination appends without duplicates by canonical identity.
12. Comics ranking is deterministic and availability boosts rather than gates.
13. Missing LOCG rank does not force a title to the bottom by arbitrary sentinel.
14. Trending falls back honestly until comparable snapshots exist.
15. Explicit Content defaults off and persists across restart.
16. Explicit works are hidden when off and shown when on across all three world policy adapters.
17. *Berserk*, *Game of Thrones*, violent comics, horror, and ordinary adult fiction remain visible when Explicit Content is off.
18. Existing adult-extension installation policy is unchanged.
19. Network failure leaves the bundled wall usable.
20. QML numeric properties remain correctly typed and the standard app smoke reaches a stable frame.

### 9.2 Eyes-on acceptance

Hemanth verifies:

- Tankoban and Theatre Discover feel like the same tool.
- Discover is dense and utilitarian rather than decorative.
- Manga/Comics switching is obvious without visually competing with the world tab bar.
- Catalogue and filter pickers are readable at the supported window size.
- Cards do not jump under the pointer when a live refresh completes.
- Returning from a series restores the exact browse position.
- Explicit Content wording cannot be mistaken for hiding R-rated or violent mainstream work.

## 10. Rollout order

1. Global Explicit Content preference, policy helper, and settings surface.
2. Extract and regression-lock the shared Discover shell from Theatre.
3. Add native paged-query seams and deterministic ranking tests.
4. Add the Tankoban adapter and Discover page.
5. Wire default tab, detail routes, and per-type state.
6. Add Manga/Comics See-all pins.
7. Wire Explicit Content policy across existing Theatre, Tankoban, and Biblio browse/discovery inputs.
8. Run regression, offline, stale-response, and eyes-on verification.

## 11. Out of scope

- Advanced multi-filter discovery.
- Separate Manga/Manhwa/Manhua tabs or origin filter.
- Issue- or volume-level Discover tiles.
- Direct download from Discover.
- Preview/detail side panel.
- Personal recommendations or personalized house ranking.
- Shipping a discovery extension.
- Making Metron or Comic Vine a runtime dependency.
- Changing adult-extension installation policy.
- Cross-restart persistence of Discover scroll and selection.
- Redesigning the existing Manga, Comics, or Theatre browse pages beyond their See-all doors.

## 12. Research notes

- Jikan exposes MAL-backed manga type, score, voters, rank, popularity, members, favourites, status, dates, and genres: <https://docs.jikan.moe/objects/model/manga/manga/>.
- GCD is suitable as the canonical bibliographic backbone, but its site describes API fields/formats as not yet stable: <https://www.comics.org/>.
- Metron provides useful GCD/Comic Vine identity filters and incremental sync, but requires authenticated, rate-limited access: <https://metron-project.github.io/blog/api-best-practices>.
- Comic Vine's official API is non-commercial-only, credentialed, and rate-limited, so it is excluded from the launch dependency graph: <https://comicvine.gamespot.com/api/>.

## 13. Approved visual reference

The approved brainstorming mockup is stored outside the committed spec under `.superpowers/brainstorm/1215-1785594721/content/tankoban-discover-shell.html`. It is a layout reference only; this document is the source of truth for behavior and scope.
