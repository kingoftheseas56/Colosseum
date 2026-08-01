# Biblio Discover and Explore Design

**Date:** 2026-08-01

**Status:** Approved by Hemanth

**Scope:** Biblio landing-page catalogue body, book discovery data, filtering, ranking, extension catalogues, caching, and navigation

## 1. Summary

Biblio becomes a two-tab book catalogue beneath its existing shared landing content:

- **Discover** is a utilitarian catalogue browser matching Theatre and Tankoban: one catalogue picker, one active filter, and one infinite book grid.
- **Explore** owns all editorial shelves: the live Apple Books Top 10, installed extension shelves, four Biblio house shelves, and three compact browsing mosaics.

The page opens on Discover. Featured, Continue Reading, and Your Collection remain shared above the tab switcher and do not repeat inside either tab.

This specification supersedes the Biblio landing-page, chart-surface, award-discovery, and multi-filter portions of `2026-07-21-biblio-catalog-design.md`. The older document's durable catalogue-identity and acquisition-separation rules remain valid where they do not conflict with this specification.

## 2. Locked product rules

1. Discover is not a shelf feed. It is the same utilitarian catalogue interaction used by Theatre and Tankoban.
2. Explore contains every shelf and mosaic. It does not contain a second hero carousel or another Continue Reading/Collection section.
3. Discover opens by default on every Biblio entry; Biblio does not remember the previously selected tab.
4. The built-in experience is permanently keyless. No API key, login, or account setup is allowed.
5. Discovery contains canonical written works only. Audiobooks remain editions reachable through global search and the existing book detail page.
6. A literary work appears once in discovery. Hardcover, paperback, ebook, translation, and audiobook records are reconciled beneath the canonical work.
7. Catalogue inclusion and ranking are independent of acquisition availability, file sources, commercial status, and public-domain status.
8. The global `Explicit Content` setting hides only sexually explicit works. Adult readership, violence, horror, difficult themes, or mature prose are not explicit by themselves.
9. No award shelf, award mosaic, award filter, or other award-based discovery is allowed.
10. Cards use literal metadata only. There are no generated blurbs or decorative AI-written explanations.

## 3. Shared landing anatomy

`BiblioWorld.qml` retains this top-level order:

1. Featured carousel
2. Continue Reading
3. Your Collection
4. `Discover | Explore` switcher
5. active tab body

The shared content does not scroll independently from the selected tab. Switching tabs changes only the catalogue body and preserves the current landing-page scroll position where practical.

## 4. Discover

### 4.1 Page anatomy

Discover contains exactly:

1. current catalogue label and catalogue picker;
2. one grouped filter control;
3. one infinite catalogue grid;
4. loading, stale-cache, empty, error, and incremental-fetch states belonging to that grid.

It contains no editorial rails, Top 10 widget, extension shelf, genre mosaic, hero, or continuation widget.

### 4.2 Catalogue picker

The picker has two groups.

**Biblio catalogues:**

- Popular
- Top Rated
- New Releases
- Trending

**From Your Extensions:**

- one entry for every enabled, input-free book catalogue exposed by an installed catalogue extension;
- hidden when no compatible catalogue extension is installed.

Popular is selected initially. Acquisition/download extensions never enter this picker. Provider attribution appears beneath the active catalogue name and on hover/focus metadata.

### 4.3 One-filter contract

Only one normalized filter value can be active. Selecting another value replaces it; clearing it restores the unfiltered active catalogue. Advanced combinations are explicitly deferred.

The grouped filter menu contains:

- **Genre**
- **Audience:** Children, Middle Grade, Young Adult, Adult
- **Length:** Short under 200 pages; Standard 200-499; Long 500-799; Epic 800+
- **Publication Era:** Before 1900; 1900-1949; 1950-1979; 1980-1999; 2000-2009; 2010-2019; 2020-Present
- **Themes & Subjects**
- **Setting / Place**
- **Historical Period**
- **Original Language:** English; Translated
- **Publisher:** curated publishers with meaningful catalogue coverage

`Adult` is a readership category and has no relationship to explicit-content classification. Length uses the representative English edition. Works without reliable pagination do not enter a Length result. Publication Era uses the earliest reliable publication year of the canonical work, never a reprint or ebook conversion date. `Translated` means the work was originally written in another language but Biblio presents an English-readable edition.

### 4.4 Extension filtering

An extension catalogue participates in filtering only when it declares compatible metadata that can be mapped into Biblio's controlled facet schema. Unsupported or missing classifications are not inferred. An extension that cannot satisfy the active filter shows an honest unsupported/empty state rather than unfiltered results.

### 4.5 Grid and card behavior

The grid reuses the existing simple infinite-catalogue page pattern and existing book-detail route.

At rest, every card shows:

- cover;
- title;
- primary author.

Pointer hover and keyboard focus reveal:

- rating when known;
- source attribution;
- the existing detail affordance.

Unknown ratings remain absent rather than becoming zero. Missing covers use the existing typographic/fallback jacket. Selecting a card opens the canonical work in the existing book detail page.

## 5. Explore

Explore is a vertically scrolling shelf page in this default order:

1. Top 10 in Biblio
2. From Your Extensions
3. Popular
4. Top Rated
5. New Releases
6. Trending
7. Fiction mosaic
8. Nonfiction mosaic
9. Audience mosaic

`From Your Extensions` disappears when empty. Each installed catalogue contributes its own attributed preview row; extension results are not blended into Biblio's house rankings.

### 5.1 Top 10

Top 10 remains the live Apple Books ebook chart. It is deliberately separate from Biblio's cross-source Popular ranking. It refreshes through the daily catalogue refresh and uses the last successful cached chart while offline.

### 5.2 House shelves

The four house shelves preview the same normalized catalogues available through Discover:

- Popular
- Top Rated
- New Releases
- Trending

Every house and extension shelf, including Top 10, has `See All`. `See All` opens the existing infinite catalogue grid with the correct catalogue pin, title, attribution, paging state, and back-navigation state.

### 5.3 Mosaics

Explore ends with three compact mosaics:

- **Fiction:** curated fiction genres;
- **Nonfiction:** curated nonfiction genres and subjects;
- **Audience:** Children, Middle Grade, Young Adult, Adult.

Selecting a mosaic tile switches to Discover and pins the corresponding single filter. Back navigation returns to the same Explore scroll position. Mosaic tiles do not open or preserve the old dedicated genre-page browsing path.

### 5.4 Customize rows

Explore provides a simple `Customize rows` mode for catalogue shelves:

- show or hide Top 10, house shelves, and individual extension shelves;
- reorder visible shelves by drag handle;
- provide keyboard-accessible move-up/move-down actions equivalent to dragging;
- restore the default order and visibility.

The three mosaics remain fixed at the bottom and are not part of row customization. Preferences persist locally by stable shelf key, not display title. A newly installed extension shelf appears visibly after existing extension shelves without disturbing the user's built-in ordering. Removed extensions leave no broken row or navigation pin.

## 6. Hybrid cached catalogue

### 6.1 Source responsibilities

Apple Books supplies:

- the live ebook Top 10;
- current chart position and release activity;
- storefront ratings and rating counts when present;
- genres, artwork, descriptions, and current edition metadata.

Open Library supplies:

- canonical work and edition evidence;
- first-publication year;
- authors and identifiers;
- page counts;
- original language and English-edition evidence;
- subjects, settings, time periods, and publishers;
- cover and description fallback candidates.

Open Library governs work identity and earliest-publication evidence. Apple governs the Apple chart, Apple ratings, Apple artwork, and current storefront activity. All merged fields retain source provenance.

### 6.2 Canonicalization

The canonicalizer uses layered evidence:

- Open Library work and edition identifiers;
- ISBN and other authority identifiers;
- normalized title and author;
- original and edition publication dates;
- language and translator evidence;
- publisher and edition notes.

Title-only equality cannot silently merge ambiguous works. Ordinary format changes remain editions. Materially revised texts and substantially different English translations may retain distinct edition metadata, but the initial discovery surface still routes through the canonical work unless existing detail-page policy requires a distinct work entry.

### 6.3 Controlled facets

Apple genres and Open Library's community-generated subjects pass through a versioned Biblio taxonomy mapper. It:

- merges synonyms, spelling variants, singular/plural forms, and known aliases;
- maps source values into stable genre, audience, theme, setting, period, language, and publisher identifiers;
- normalizes reliable imprints under parent publishers;
- admits independent publishers when they have meaningful catalogue coverage;
- suppresses ambiguous, noisy, unsupported, or extremely sparse raw tags;
- never creates a visible filter automatically from an unknown provider string.

The exact controlled values live in a checked-in, testable mapping file. Changing a stable identifier requires an explicit taxonomy migration so saved navigation pins remain valid.

## 7. Ranking semantics

- **Popular:** sustained demand computed from Apple chart performance, rating volume, and available normalized Open Library popularity evidence. It must not be another copy of the current Apple Top 10.
- **Top Rated:** a weighted/Bayesian score using average rating and rating volume. Its confidence prior adapts to the active catalogue population so a handful of perfect ratings cannot dominate.
- **New Releases:** canonical works first published during the trailing 12 months. Reprints, new covers, ebook conversions, and audiobook releases do not reset eligibility.
- **Trending:** seven-day momentum computed from daily snapshots of chart movement, rating-volume change, and supported attention signals. It must not alias Popular when history is insufficient.

Rankings exclude acquisition availability, source-health, public-domain status, ownership, and local reading activity. Missing evidence excludes only the affected signal or recipe; it does not remove the work from unrelated catalogues.

## 8. Refresh, persistence, and offline behavior

The built-in catalogue refreshes at most once per local calendar day unless the user invokes an existing explicit refresh mechanism. Concurrent requests share one refresh job.

The refresh pipeline is:

1. load the last successful snapshot for immediate paint;
2. fetch Apple and Open Library candidates;
3. normalize and canonicalize candidates;
4. map controlled facets;
5. compute rankings and seven-day history;
6. validate referential integrity and catalogue invariants;
7. atomically publish the new snapshot;
8. notify visible Discover/Explore models.

A partial or failed refresh never replaces the last valid cache. The snapshot store keeps enough daily history to calculate seven-day Trending and prunes older ranking snapshots according to a bounded retention policy.

Offline behavior:

- the last successful snapshot keeps Discover, Explore, filters, and previously loaded See-All pages browsable;
- cached covers continue through the existing image cache;
- a subtle page-level offline/stale indicator is shown without repeating provider errors on every row;
- a new installation with no successful snapshot shows a first-sync-required state;
- no placeholder ranking or fabricated catalogue is generated.

## 9. Service and component boundaries

Implementation keeps these responsibilities separate:

- **Biblio catalogue service:** orchestrates keyless provider requests and exposes normalized catalogue queries to QML.
- **Apple adapter:** Apple RSS/Search parsing and Apple-specific pacing/errors.
- **Open Library adapter:** Search/Subjects/Work/Edition parsing and pacing/errors.
- **Canonicalizer:** work/edition identity resolution and provenance.
- **Taxonomy mapper:** controlled facet mapping.
- **Ranking engine:** deterministic Popular, Top Rated, New Releases, and Trending calculations.
- **Snapshot store:** atomic daily snapshots, history, cache freshness, and offline reads.
- **Extension adapter:** compatible catalogue discovery, attribution, paging, and optional normalized filtering.
- **Discover controller/view:** catalogue selection, one-filter state, paging, and card navigation.
- **Explore controller/view:** shelves, mosaics, See-All pins, and customization.

QML does not merge raw provider responses, calculate canonical identity, or implement ranking formulas.

## 10. Failure handling

- Apple Top 10 failure leaves the cached Top 10 visible.
- One provider's failure does not blank data already supported by the other provider or cache.
- Old callbacks are rejected after a catalogue/filter/tab change.
- A failed extension shelf does not block built-in shelves.
- Empty or unsupported extension shelves collapse in Explore and show an honest state when explicitly opened in Discover.
- A removed/disabled extension invalidates its open pin safely and returns an explanatory empty state.
- Source errors stay in diagnostics unless they affect the user's current action.
- Rate limits use bounded retry/backoff and never create an unbounded request loop.

## 11. Accessibility and interaction

- Discover and Explore are fully keyboard navigable.
- Hover-only metadata is also exposed on keyboard focus and to accessibility APIs.
- Drag ordering has equivalent keyboard actions.
- Focus returns to the originating card or mosaic tile after back navigation.
- Tab, catalogue, filter, row, and loading states expose meaningful accessible names.
- Layout remains usable at supported Colosseum window widths without horizontal page clipping.

## 12. Verification strategy

### 12.1 Data tests

Cover:

- canonical deduplication and ambiguous-title non-merging;
- edition and audiobook nesting;
- source precedence and provenance;
- taxonomy synonym normalization and unknown-tag suppression;
- Audience, Length, Publication Era, Original Language, and Publisher boundaries;
- exact explicit-content behavior;
- ranking determinism and distinctness;
- weighted-rating confidence;
- trailing-12-month release eligibility;
- seven-day Trending movement;
- atomic snapshot publication and history pruning.

### 12.2 Service and failure tests

Cover:

- daily refresh coalescing;
- Apple/Open Library partial failure;
- offline startup with and without a prior snapshot;
- rate limiting and bounded retry;
- stale-callback rejection;
- compatible and incompatible extension filtering;
- extension removal while its catalogue/grid is open.

### 12.3 QML tests

Cover:

- Discover opens by default;
- shared Featured/Continue Reading/Collection render only once;
- Discover contains no shelves or mosaics;
- catalogue switching and one-filter replacement;
- Explore shelf order and fixed mosaics;
- See-All paging and back restoration;
- mosaic-to-pinned-Discover navigation;
- card hover/focus metadata;
- row visibility, drag ordering, keyboard ordering, persistence, and reset;
- offline/empty/error/loading states at supported window sizes.

## 13. Out of scope

- audiobook shelves or a separate audiobook Discover tab;
- multiple simultaneous filters;
- user accounts or cloud-synced row preferences;
- personalized recommendations;
- award discovery;
- acquisition/download-source changes;
- changes to the existing book detail page beyond accepting canonical work records and nested editions;
- non-English user-facing editions;
- API-key-dependent providers;
- replacement of global search.

## 14. Definition of Done

1. Biblio retains one shared Featured, Continue Reading, and Your Collection area above the two tabs.
2. Discover opens by default and contains only a catalogue picker, one active filter, and an infinite canonical-book grid.
3. Explore contains Top 10, extension shelves, Popular, Top Rated, New Releases, Trending, and the three approved mosaics in the approved default order.
4. Discover and Explore contain no award discovery, duplicate hero/continuation widgets, generated blurbs, account gates, or API-key setup.
5. Cards show cover, title, and author at rest; rating and attribution appear on pointer hover and keyboard focus.
6. Apple Books and Open Library records resolve into canonical works with nested editions and retained provenance.
7. The approved normalized filters use the exact audience, length, era, language, and curated-taxonomy rules in this specification.
8. Popular, Top Rated, New Releases, and Trending have distinct, test-covered ranking semantics.
9. Daily atomic snapshots provide last-successful-cache behavior and real seven-day Trending history.
10. Compatible catalogue extensions appear in Discover's picker and Explore's attributed shelves; acquisition extensions never do.
11. Every Explore shelf has a source-correct See-All grid, and every card opens the existing book detail page.
12. Explore shelf visibility and ordering persist by stable key; drag and keyboard controls are equivalent; mosaics remain fixed.
13. Mosaic tiles open Discover with exactly one pinned filter and return to the prior Explore position.
14. The global Explicit Content setting hides only source-confirmed sexually explicit books.
15. Data, service, failure, accessibility, and QML tests pass without regressing existing Biblio search, detail, collection, progress, or acquisition behavior.
