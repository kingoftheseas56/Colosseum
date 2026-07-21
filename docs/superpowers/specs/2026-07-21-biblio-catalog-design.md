# Biblio Catalog Design

**Date:** 2026-07-21  
**Status:** Approved design  
**Scope:** Biblio catalog identity, discovery, ranking, ingestion, personalization, failure handling, and test strategy

## 1. Purpose

Biblio should feel as varied and accessible as Theatre and Tankoban without turning discovery into a reflection of whichever acquisition source happens to answer first.

The catalog is independent of acquisition. A book remains discoverable because it belongs in the catalog, not because a current source reports an available file. Tankorent remains downstream and begins only after the user selects a work and asks to acquire it.

The target experience is a broad English-language library with:

- multiple public chart families rather than one overloaded Top 10;
- a stable genre tree backed by rich facets;
- canonical work identities with materially different texts split when necessary;
- series-aware, author-aware, and recommendation-aware browsing;
- a local catalog brain that remains useful offline;
- live enrichment that adds freshness without owning identity;
- personalized shelves that never rewrite public rankings.

## 2. Product Rules

### 2.1 English-only catalog

Biblio is permanently English-only.

English translations of works originally written in other languages are valid catalog entries. Translation metadata is first-class and preserves:

- translator;
- original language;
- translation year;
- the canonical text or translation lineage represented by the edition.

Non-English editions do not enter user-facing shelves, search indexes, recommendations, charts, or acquisition matching.

### 2.2 Availability does not affect discovery

Catalog visibility and ranking do not depend on public-domain status, commercial status, source availability, seed counts, or the presence of an immediately usable file.

Public-domain books are ordinary catalog works. They appear in genre pages, rankings, recommendations, and search whenever their metadata and ranking justify it. They receive neither a special boost nor a penalty.

The flow is:

1. catalog identity;
2. classification and ranking;
3. display in search, charts, genres, series, authors, and recommendations;
4. Tankorent acquisition after explicit user intent.

### 2.3 Complete discovery, acquisition after selection

Every valid catalogued work is eligible for search, genres, charts, author pages, series pages, and recommendations. Acquisition is an action on a selected work, never an eligibility gate or ranking input.

### 2.4 Stable public rankings and separate personalization

Public charts and genre rankings are identical for every user. Personalized shelves are visibly separate and derive from the reader's own activity.

Personalization can create shelves such as:

- Because You Read;
- Continue This Author;
- Next in Series;
- Explore Nearby.

It must not alter Trending, Popular in This Genre, All-Time Essentials, New and Notable, or Critically Acclaimed.

## 3. Architecture

### 3.1 Local catalog brain

Biblio receives a pipeline-built SQLite catalog at `data/biblio_catalog.db`.

The local catalog owns durable identity and structure:

- canonical works;
- materially distinct text editions;
- authors and contributor relationships;
- series and reading order;
- English translation lineage;
- stable genres;
- rich facets;
- awards;
- chart history and normalized ranking evidence;
- recommendation relationships;
- provenance for merged metadata.

The application remains useful when all live services are unavailable.

### 3.2 Native service boundary

A native `BiblioCatalog` service exposes normalized, medium-shaped operations to QML. QML does not merge raw provider responses or implement identity policy.

The service exposes operations for:

- searching works, authors, and series;
- fetching a canonical work;
- fetching an author page;
- fetching a series page;
- fetching a genre page;
- applying facet filters;
- fetching public charts;
- fetching related works with recommendation reasons;
- fetching edition and translation relationships;
- reporting catalog version, build date, freshness, and degraded state.

A separate `BiblioRecommendations` service owns user-specific shelves. It consumes catalog relationships plus reading, collection, author, and series activity, but it cannot modify public chart results.

### 3.3 Live enrichment

Live providers supply freshness, not identity authority. They can enrich:

- recent releases;
- changing popularity evidence;
- cover candidates;
- descriptions;
- missing publication metadata;
- metadata corrections.

Live failure must not block first paint or prevent local search and browsing.

### 3.4 Acquisition boundary

Tankorent remains downstream from the catalog. A canonical work page exposes a clear acquisition action, but source discovery and file selection begin only after explicit user intent.

Catalog services do not consume availability, swarm, or source-health signals for ranking.

## 4. Catalog Model

### 4.1 Work

The primary user-facing identity is a canonical `Work`, not an ISBN, storefront record, or delivery result.

A work owns:

- title and subtitle;
- author and contributor relationships;
- publication history;
- original language;
- English translation status;
- description candidates;
- cover candidates;
- genres and facets;
- series placement;
- awards;
- chart appearances;
- recommendation edges;
- provenance and confidence data.

### 4.2 Edition

Ordinary hardcover, paperback, ebook, and audiobook manifestations remain nested beneath the same work when they represent substantially the same text.

An edition becomes independently browsable only when the reading experience or text materially differs, including:

- major revisions;
- expanded editions;
- author-approved rewrites;
- substantially different English translations;
- omnibuses;
- heavily annotated scholarly editions;
- abridged or dramatized audio adaptations.

Audiobooks that faithfully represent the same text remain editions of the canonical work.

### 4.3 Translation lineage

English translations are valid user-facing records. Translation metadata preserves:

- translator identity;
- source language;
- translation publication year;
- relationship to the original work;
- relationship to other English translations;
- evidence that a translation is materially distinct.

### 4.4 Series

Series support:

- numbered positions;
- fractional positions such as `0.5`;
- publication order;
- recommended reading order;
- novellas and side stories;
- subseries and parent universes;
- omnibuses covering several positions;
- unresolved order without invented numbering.

The existing Goodreads-derived series index remains useful as evidence, but title joins do not become the permanent identity foundation.

### 4.5 Authors

Authors are first-class catalog entities, not string filters. The model supports:

- primary and secondary contributors;
- collaborations;
- editors and translators where relevant;
- pseudonyms when confidently linked;
- bibliography ordering;
- author-to-series relationships.

### 4.6 Genres and facets

The initial taxonomy contains exactly 20 stable top-level genre roots. Changes to those roots require an explicit taxonomy migration rather than ad hoc provider-driven additions.

Richer discovery is powered by facets such as:

- subgenre;
- themes;
- mood;
- setting;
- historical period;
- literary movement;
- audience;
- length;
- narrative structure;
- representation;
- awards;
- subject domains.

Facet combinations can create contextual shelves and search filters without turning the main navigation into an unbounded taxonomy.

## 5. Ranking and Charts

### 5.1 Separate chart families

Biblio provides distinct public chart systems:

- **Trending Now:** fast-moving cultural momentum;
- **Popular in This Genre:** contextual ranking within the active genre;
- **All-Time Essentials:** slow-changing enduring prominence;
- **New and Notable:** recent English releases and noteworthy new English translations;
- **Critically Acclaimed:** awards, reviews, and long-term critical standing.

One universal Top 10 is rejected because it mixes incompatible notions of popularity and quality.

### 5.2 Hybrid chart computation

External charts and datasets are evidence, not the final answer.

Biblio ingests multiple bestseller, popularity, awards, ratings, and editorial signals, then normalizes them into work-level rankings. No single vendor's list becomes Biblio's worldview.

Each computed chart entry stores:

- chart family;
- time window;
- source signals;
- source timestamps;
- normalization version;
- final score;
- final rank;
- confidence;
- build timestamp.

This makes rankings reproducible and inspectable.

### 5.3 Ranking exclusions

The following do not influence catalog ranking:

- current source availability;
- torrent seed counts;
- public-domain status;
- commercial status;
- whether the user already owns or downloaded the work.

### 5.4 Recommendation reasons

Personal and related-work recommendations carry explicit reasons such as:

- same author;
- next in series;
- shared themes;
- nearby genre profile;
- frequently read alongside;
- similar critical reception;
- similar audience reception.

The UI can explain why a work appears.

## 6. Ingestion and Refresh

### 6.1 Evidence staging

Every external provider adapter writes normalized candidate records into staging tables. A resolver decides whether each candidate belongs to:

- an existing canonical work;
- a materially distinct edition;
- a translation lineage;
- a genuinely new work;
- an unresolved candidate requiring review.

### 6.2 Identity matching

Matching uses layered evidence:

- normalized title and subtitle;
- author identity;
- publication year;
- series position;
- ISBNs and other identifiers;
- publisher and edition notes;
- translator;
- synopsis similarity.

Title-only matching is a weak signal and must not silently merge ambiguous works.

### 6.3 Field-specific provenance

Every merged field preserves provenance. Biblio knows which source supplied a cover, description, genre, award, publication date, series position, or translation claim.

Conflicts are resolved by field-specific policies rather than one global provider order. An awards source can outrank a storefront for awards, while a publisher feed can outrank a community dataset for release dates.

### 6.4 Refresh cadence

The pipeline uses three explicit cadences:

1. **Monthly structural rebuild:** works, authors, series, taxonomy, historical awards, provenance, and recommendation edges.
2. **Daily enrichment:** recent releases, covers, descriptions, publication corrections, and metadata gaps.
3. **Every six hours:** chart evidence ingestion and deterministic recalculation of public chart families.

The slower chart families remain stable through their weighting and time windows, not through an irregular refresh schedule.

### 6.5 Atomic publication

Generated databases publish atomically.

The application continues reading the previous valid catalog until the replacement has passed schema checks, integrity checks, content validation, and query smoke tests. A failed import cannot expose a partial catalog.

### 6.6 Checked-in override layer

A small, explicit override layer handles cases that automated matching cannot reliably resolve, including:

- reused titles;
- pseudonyms;
- omnibuses;
- revised nonfiction;
- substantially different translations;
- duplicate author names;
- disputed or ambiguous series placement.

Overrides are reviewable, survive rebuilds, and are never applied by patching generated SQLite files directly.

## 7. Catalog Surfaces

### 7.1 Biblio landing page

The landing page has three visually distinct layers.

#### Public discovery

Chart tabs such as Trending, New and Notable, Critically Acclaimed, and All-Time Essentials. Rankings are identical for every user and show their refresh date.

#### Structured browsing

Stable genre pages with contextual rails such as:

- Popular in Fantasy;
- New Fantasy;
- Award Winners;
- Short Reads;
- thematic or facet combinations.

A genre page remains recognizably that genre after filters are applied.

#### Personal discovery

Clearly labelled shelves such as Because You Read, Continue This Author, Next in Series, and Explore Nearby.

### 7.2 Search

Search queries the local catalog first and groups results into:

- works;
- authors;
- series.

Ranking favours exact title and author matches, followed by canonical popularity and semantic relevance. Edition-level results appear only for materially distinct texts. Source availability is not a search-ranking signal.

### 7.3 Work page

A canonical work page includes:

- title, author, description, genres, facets, awards, and publication context;
- English translation metadata where applicable;
- series placement and nearby books;
- chart appearances;
- related works with visible reasons;
- nested editions and formats;
- an acquisition action that invokes Tankorent only after selection.

### 7.4 Series page

Series tiles use a stacked-book treatment. Series pages expose:

- recommended reading order;
- publication order when different;
- novellas and side stories;
- subseries;
- omnibuses and their coverage;
- unresolved positions without fabricated numbering.

### 7.5 Author page

Author pages show:

- bibliography;
- series;
- standalones;
- collaborations;
- confidently linked pseudonyms;
- most popular works;
- chronological order;
- recommended starting points.

They are catalog surfaces, not filtered search-result pages.

### 7.6 Partial and empty states

Missing artwork uses typographic jackets. Missing descriptions do not hide works. Weak live enrichment does not blank genre or chart pages. Unresolved series order is shown honestly rather than guessed.

## 8. Failure Handling and Observability

### 8.1 Lane-level degradation

Biblio fails by lane rather than collapsing as one catalog.

- Live enrichment failure leaves local discovery intact.
- One failed chart source reduces confidence but does not erase rankings.
- Cover failure falls back to typographic jackets.
- A failed catalog rebuild leaves the previous database active.
- A failed personalized shelf does not affect public charts.

### 8.2 Native catalog state

The native service exposes:

- database schema version;
- catalog build date;
- last successful enrichment refresh;
- last successful chart refresh;
- degraded providers;
- unavailable optional features;
- whether a result came from local data, live enrichment, or both.

### 8.3 User-facing error behavior

Errors remain quiet unless they affect the current action.

- Stale charts show their last refresh date.
- Search distinguishes no matches from catalog unavailable.
- Provider details stay in diagnostics rather than dominating the UI.
- No surface fabricates content to hide missing data.

### 8.4 Catalog health report

Each pipeline run generates a compact health report containing at least:

- total works;
- total authors;
- total editions;
- total series;
- total English translations;
- genre and facet coverage;
- duplicate suspects;
- unresolved candidates;
- stale providers;
- chart coverage;
- validation failures;
- override usage.

## 9. Validation Rules

A candidate catalog is rejected when it contains:

- non-English user-facing editions;
- works without a resolvable title or author relationship;
- circular series relationships;
- impossible publication dates;
- two work rows that the resolver or checked-in overrides mark as the same canonical identity;
- collisions on authority identifiers declared unique by the schema;
- chart entries pointing to missing works;
- unsupported genre or facet values;
- materially different texts collapsed without evidence;
- broken foreign keys or schema-version mismatches.

Unresolved duplicate suspects remain separate, are excluded from automatic cross-linking, and appear in the health report for review. They do not silently merge and do not block publication unless a deterministic identity rule marks them as the same work.

## 10. Testing Strategy

### 10.1 Pipeline fixtures

Fixtures cover:

- work merging;
- English filtering;
- translation handling;
- series ordering;
- fractional positions;
- omnibus coverage;
- taxonomy validation;
- override application;
- ambiguous title handling;
- provider conflict resolution.

### 10.2 Database contracts

Contract tests cover:

- schema versioning;
- referential integrity;
- atomic replacement compatibility;
- chart reproducibility;
- recommendation edges;
- provenance retention;
- migration compatibility.

### 10.3 Native service tests

Tests cover:

- exact and fuzzy search ranking;
- grouped work, author, and series results;
- genre and facet queries;
- chart retrieval;
- author pages;
- series pages;
- related works and reasons;
- degraded-mode behavior;
- catalog version reporting;
- atomic database swaps.

### 10.4 QML smoke tests

Smoke tests cover:

- chart switching;
- chart refresh labels;
- genre navigation;
- facet filtering;
- series stack rendering;
- work navigation;
- author navigation;
- personalized shelf labels;
- missing-cover fallbacks;
- empty states;
- catalog unavailable versus no-results states.

### 10.5 Golden trouble dataset

A compact golden dataset includes intentionally difficult records:

- reused titles;
- multiple authors with the same name;
- pseudonyms;
- revised nonfiction;
- omnibuses;
- fractional series positions;
- abridged audiobooks;
- dramatized audiobooks;
- multiple materially different English translations;
- conflicting publication dates;
- unresolved series order.

## 11. Performance Requirements

- Local search and shelf queries must feel immediate.
- Database startup and validation must not block the shell.
- Live enrichment must never delay first paint.
- Genre and chart pages render from the local catalog before optional enrichment completes.
- Expensive ranking and identity resolution belong in the pipeline or native service, not QML delegates.

## 12. Scope Boundaries

This design does not implement:

- non-English catalog browsing;
- source availability ranking;
- Tankorent search or file-selection changes;
- a standalone audiobook catalog identity separate from books;
- user-editable public charts;
- social networking or public user reviews;
- arbitrary user-defined taxonomy.

## 13. Recommended Delivery Sequence

The implementation plan must decompose the work into independently testable stages:

1. catalog schema, 20-root taxonomy, and golden dataset;
2. ingestion staging, identity resolution, provenance, and overrides;
3. local SQLite build and validation pipeline;
4. native `BiblioCatalog` service and query contracts;
5. search, work, author, series, genre, and chart surfaces;
6. chart evidence ingestion and deterministic ranking;
7. live enrichment and atomic refresh;
8. `BiblioRecommendations` and personalized shelves;
9. diagnostics, health reporting, and release packaging.

The implementation plan must preserve the central boundary: catalog identity and discovery first, acquisition only after explicit user intent.
