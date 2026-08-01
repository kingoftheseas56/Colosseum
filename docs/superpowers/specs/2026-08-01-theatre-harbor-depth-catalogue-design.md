# Theatre Deep Catalogue Design

**Date:** 2026-08-01

**Status:** Approved by Hemanth

**Scope:** Theatre `Movies`, `Shows`, and `Anime` tabs only

## 1. Summary

Replace Theatre's current `Top 10 + Genres` tab bodies with deep, keyless catalogue pages inspired by Harbor's discovery breadth and behavior. Colosseum keeps its own literal taxonomy, existing visual language, data sources, detail routes, and explicit-content policy.

The Theatre landing page remains the sole owner of the hero carousel, Next Up, and Continue Watching. The three catalogue tabs begin directly with shelves and never repeat those landing-page widgets.

## 2. Locked product decisions

1. Match Harbor's catalogue depth, row behavior, customization model, and See-all behavior; do not copy Harbor's exact labels or data dependencies.
2. Every built-in experience is keyless. No account, API key, login, or setup step is allowed.
3. No per-tab hero carousel, featured banner, Continue Watching, or Next Up.
4. No award hub, award winner row, award filter, or other award-based discovery.
5. Row titles are literal and self-explanatory. Do not render descriptive blurbs such as `Great films that slipped past the crowd`.
6. IMDb ratings are hidden at rest. Reveal year and the IMDb-derived rating only on pointer hover, using Discover's existing `★ <value>` presentation and timing.
7. `Top 10` remains the first row on Movies, Shows, and Anime.
8. Movies use Harbor-like asymmetric rotation: several deterministic daily categories. Shows and Anime are predominantly stable.
9. Every house and extension row has `See all`, opening the existing simple infinite-grid pattern with title, source attribution, loading states, back navigation, and normal detail routing.
10. Row customization matches Harbor: move up, move down, hide/show, rename, and reset. It persists separately for each tab. No drag-and-drop is required.
11. Recognized branded-service catalogues appear in their contextual main-catalogue positions only when the matching extension is installed and enabled.
12. All other compatible extension catalogues appear under `From Your Extensions` before the genre mosaic.
13. The existing global `Explicit Content` setting gates only sexually explicit titles. `Game of Thrones`, `Berserk`, TV-MA/R-rated works, violence, horror, gore, profanity, and `Ecchi` are not explicit by themselves.
14. The approved mockups are layout references, not literal data contracts. The two corrections in items 5 and 6 override text or always-visible ratings shown in those images.

## 3. Page anatomy

Each tab is one vertically scrolling catalogue:

1. `Top 10`
2. stable built-in shelves
3. medium-specific shelves
4. Movies-only daily shelves
5. recognized service-extension shelves merged into their declared contextual positions within the main list
6. `From Your Extensions`, hidden when empty
7. genre mosaic
8. a quiet `Customize rows` control available without consuming a content row

There is no secondary masthead. Shelf art and titles carry the page.

## 4. Shelf model

Every normalized shelf exposes:

```text
key             stable per-tab identifier
title           user-visible literal label
pageKey         movies | shows | anime
sourceKind      house | service-extension | extension
sourceLabel     Colosseum | extension display name
ranked          true only for Top 10
items           current preview items
pageSize        preview/deep-page request size
seeAllPin       stable descriptor for the infinite grid
placement       stable ordering slot
rotating        true only for daily-selected movie shelves
```

Shelf keys, not display titles, own persistence. Renaming a shelf never breaks ordering, visibility, daily recurrence, or See-all routing.

The renderer does not accept or display a shelf subtitle/blurb. Source attribution is metadata, not promotional copy.

## 5. Built-in shelf inventory

### 5.1 Movies

Permanent shelves:

- `Top 10`
- `Recently Released`
- `Top Rated`
- `Hidden Gems`
- `All-Time Greats`
- `Under Two Hours`
- `Documentary Movies`
- `Animated Movies`
- `International Cinema`
- `Japanese Cinema`
- `Korean Cinema`
- `French Cinema`
- `2020s Movies`
- `2010s Movies`
- `2000s Movies`
- `1990s Movies`
- `1980s Movies`
- `1970s Movies`

Six daily shelves are selected deterministically from literal genre, runtime, era, and seasonal recipes such as `Crime Thrillers`, `Science Fiction`, `Family Movies`, `90-Minute Movies`, `Classic Horror`, and `Holiday Movies`. The same calendar day yields the same ordered set. A hidden or renamed daily shelf retains that preference when its stable key returns later.

Do not ship built-in `In Theaters` or `Coming Soon` rows without an authoritative keyless freshness source. `Recently Released` must not be relabeled to imply theatrical availability. An installed extension may provide those catalogues under its own attribution.

### 5.2 Shows

- `Top 10`
- `Currently Airing`
- `Top Rated`
- `Long-Running Series`
- `Recently Premiered`
- `Limited Series`
- `All-Time Great Series`
- `Drama Series`
- `Comedy Series`
- `Crime and Mystery`
- `Science Fiction and Fantasy`
- `Documentary Series`
- `Animated Series`
- `Korean Drama`
- `British Television`

Shows do not rotate merely to imitate Movies. A shelf is omitted when the keyless source cannot produce an honest non-empty result.

### 5.3 Anime

Anime deliberately receives Harbor-level public depth:

- `Top 10`
- `Trending`
- `Airing Now`
- `Top Airing`
- `Upcoming Season`
- `Top Series`
- `Top Anime Movies`
- `Most Popular`
- `Top Rated`
- `Hidden Gems`
- `2020s Anime`
- `2010s Anime`
- `2000s Anime`
- `1990s and Earlier`
- `Action and Adventure`
- `Romance`
- `Slice of Life`
- `Mecha`
- `Fantasy`
- `Science Fiction`
- `Psychological`
- `Horror and Supernatural`

There is no `Your AniList`, account recommendation, award winner, or login-dependent shelf.

## 6. Keyless data architecture

### 6.1 Movies and Shows

Cinemeta remains the identity and catalogue source. The page builds one deduplicated candidate pool from the top catalogue plus a bounded set of existing Cinemeta genre catalogues. Catalogue metadata supplies IMDb identity, poster, release information, genre, and IMDb rating where present.

Only recipes that need fields absent from catalogue previews request full Cinemeta metadata. Those requests are bounded, deduplicated, concurrency-limited, cached for the session, and progressively publish shelves. Examples are runtime, country, current status, and season count.

No row may fabricate missing facts:

- missing rating does not become `0`;
- missing country does not enter a country shelf;
- missing runtime does not enter `Under Two Hours`;
- missing status does not enter `Currently Airing`;
- a row with no honest items is omitted.

### 6.2 Anime

The bundled `data/mal_catalog.db` paints stable shelves immediately and supports offset/limit paging. It is queried by score, members, status, type, year range, and exact MAL tag.

Live refresh uses the existing keyless ladder:

1. Jikan
2. Kitsu when Jikan fails or returns empty
3. bundled MAL catalogue remains visible when both live sources fail

Jikan pacing, caching, request deduplication, and `sfw` behavior remain centralized. AniList account data is not introduced.

## 7. Ranking semantics

- `Top 10`: source popularity order, capped at ten, stable for the current refresh window.
- `Recently Released` / `Recently Premiered`: newest known release year/date first, then popularity; undated entries are excluded.
- `Top Rated`: Bayesian/weighted quality ordering where vote counts are available; enforce a vote floor so tiny samples do not dominate.
- `Hidden Gems`: quality floor plus a lower popularity/member band; it must not be a renamed Top Rated shelf.
- `All-Time Greats`: weighted quality with a strong vote floor across all years.
- `Long-Running Series`: descending known season count, then rating/popularity.
- `Trending`: only a source signal that genuinely represents current trend. If no keyless live trend signal exists, the bundled popular shelf remains visible and `Trending` is omitted rather than falsified.

Deduplicate by canonical identity within each shelf. Cross-shelf repetition is acceptable because shelves answer different questions, but the page should prefer alternate candidates when a shelf would otherwise duplicate most of the immediately preceding shelf.

## 8. Extension placement

An enabled extension catalogue is browsable only when it has no required extra input. Installed order remains the tie-breaker.

Recognized service catalogues are classified from normalized extension identity and catalogue metadata, not loose title keyword matching alone. Supported contextual slots include Netflix, HBO/Max, Apple TV+, Disney+, Prime Video, AMC, and FX. Their shelf retains quiet `via <extension>` attribution and opens the extension's real catalogue in See all.

Unrecognized compatible catalogues are not discarded. They appear as normal shelves under `From Your Extensions`, in installed order, with provider attribution. The section disappears when empty. Removing or disabling an extension removes its shelves immediately and stale See-all pins fall back with an explanatory empty state.

## 9. Card and rail behavior

At rest, a card shows poster and title only. Top 10 additionally shows the rank numeral.

Pointer hover:

- lifts/highlights the poster using the existing Discover timing;
- reveals a bottom scrim;
- reveals year when known;
- reveals the IMDb-derived rating as `★ <value>` when known, exactly like Discover;
- exposes the existing play/detail affordance;
- never reserves an always-visible rating line.

Rail headers contain only title, optional factual source attribution, and `See all`. No generated descriptions or marketing copy.

## 10. See-all behavior

Every shelf pin contains `pageKey`, `rowKey`, `sourceKind`, and the extension catalogue identity when applicable. The infinite grid reuses Discover's poster-card hover/focus behavior and loading model.

The page provides:

- shelf title;
- source attribution;
- back navigation;
- initial skeletons;
- incremental loading indicator;
- retryable error state;
- honest empty state;
- canonical item-to-existing-detail routing.

Changing or removing an extension while its grid is open never crashes or silently shows a different catalogue.

## 11. Customization

`Customize rows` enters an explicit edit mode. Each visible/hidden shelf has:

- move up;
- move down;
- hide/show;
- rename/reset name.

A page-level reset restores the current default inventory and ordering. Settings persist in `QSettings` separately for `movies`, `shows`, and `anime` as stable key order, hidden keys, and renamed-key map.

New shelf keys not present in an older saved order append in default order. Removed shelf keys are ignored. `Top 10` is customizable like Harbor; reset always restores it.

## 12. Loading, failure, and performance

- Top 10 and catalogue-preview shelves publish progressively; one slow shelf never blocks the whole tab.
- Old callbacks are rejected after a tab change or explicit-content preference change.
- No more than four full Cinemeta metadata enrichments run concurrently.
- Live caches are deduplicated by request URL and expire after 30 minutes.
- Preview rows cap at 20 items; Top 10 caps at ten.
- Below-fold rows are lazy-instantiated or use delegate reuse.
- Offline Anime falls back to the bundled MAL database.
- Offline Movies/Shows retain already loaded session data and show only a quiet page-level status when nothing can load.
- Empty shelves collapse instead of rendering blank rails.

## 13. Explicit-content behavior

All built-in and extension items pass through `ExplicitContentPolicy.visible("theatre", item, showExplicit)` before ranking and rendering.

The policy remains source-aware and classification-exact. It must not infer explicitness from certifications, titles, descriptions, violence, or broad `adult` audience wording. When `showExplicit` changes, the active tab and See-all grid reload without losing valid navigation state.

## 14. Out of scope

- changes to the Theatre landing hero, Next Up, or Continue Watching;
- advanced multi-filter Discover;
- accounts or synchronization;
- TMDB, Trakt, or any API-key dependency;
- award discovery;
- drag-and-drop customization;
- a new detail page;
- changing Tankoban or Biblio catalogue behavior.

## 15. Acceptance criteria / Definition of Done

1. Movies, Shows, and Anime render the approved deep shelf inventories with Top 10 first and genres last.
2. The tab pages contain no hero, Continue Watching, Next Up, award discovery, account UI, API-key prompt, or row blurb.
3. IMDb ratings are absent at rest and appear only on pointer hover when present, using Discover's existing presentation.
4. Every rendered shelf opens a source-correct infinite See-all grid and every item opens the existing Theatre detail page.
5. Movies use deterministic daily rotation; Shows and Anime remain predominantly stable.
6. Bundled Anime shelves work offline from `mal_catalog.db`, with Jikan then Kitsu as live refresh/fallback sources.
7. Movies and Shows use only keyless Cinemeta and installed extension catalogues; unsupported freshness claims are never fabricated.
8. Recognized installed service catalogues appear contextually; all other compatible catalogues appear under `From Your Extensions`; disabled/removed extensions disappear safely.
9. Customize mode supports move up/down, hide/show, rename/reset name, and page reset, persisted independently for all three tabs.
10. The global Explicit Content setting gates only source-confirmed sexually explicit material across previews and See-all grids.
11. Loading is progressive, stale callbacks are ignored, enrichment concurrency is bounded, empty shelves collapse, and failures do not blank already loaded rows.
12. Existing Theatre landing, Discover, genre pages, detail routing, extension registry, and explicit-content tests remain green.
