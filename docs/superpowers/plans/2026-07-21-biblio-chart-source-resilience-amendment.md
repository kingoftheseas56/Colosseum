# Biblio Chart-Source Resilience Amendment

**Status:** Binding refinement of `2026-07-21-biblio-catalog-implementation.md` and `2026-07-21-biblio-evidence-horizon-amendment.md`.

**Scope:** This amendment governs Apple Books RSS health, stale-chart behavior, source retirement, replacement-provider admission, and chart availability. It does not change the approved catalog identity model, English-only policy, acquisition boundary, or Task 4 visual checkpoint.

## Problem

Apple Books RSS is the only named current chart source in formula version 1. Apple still advertises an RSS Generator for top books, but the endpoint is not a contractual foundation for Biblio. A feed can fail temporarily, change schema, stop updating while still returning HTTP 200, or disappear entirely.

Biblio must not:

- silently convert yesterday's ranking into today's ranking;
- treat a missing observation as rank zero;
- keep calling a frozen feed `Trending` indefinitely;
- replace Apple with an unnamed scrape;
- let one provider failure blank the catalog;
- change chart meaning without a formula-version and UI-label change.

## Binding Decisions

### 1. Apple is a replaceable chart adapter, not a catalog dependency

The catalog, search, genres, authors, series, work pages, All-Time Essentials, and Critically Acclaimed must build and remain usable without Apple RSS.

Apple supplies only these formula-v1 inputs:

```text
apple_current_rank
apple_daily_velocity
apple_current_genre_rank
apple_rolling_90_day_presence
apple_longitudinal_presence
```

No Apple failure may affect canonical identity, classification, editions, translation lineage, or acquisition.

### 2. Every chart provider has an explicit health state

Add a provider-state contract:

```text
provider_name
state                 healthy | delayed | stale | retired | disabled
last_attempt_at
last_success_at
last_valid_observation_on
consecutive_failures
consecutive_schema_failures
content_fingerprint
retirement_reason
```

Apple RSS state is computed from validated observations, not HTTP status alone.

A successful observation requires all of the following:

- HTTP success;
- parseable payload;
- expected feed structure;
- at least one valid ranked entry;
- stable required fields for rank, title, author hint, and source identifier;
- no impossible duplicate ranks;
- observation date newer than the previously accepted capture;
- content fingerprint not frozen beyond the allowed quiet window.

An HTTP 200 response containing an empty feed, error page, unchanged frozen payload, or incompatible schema counts as failure.

### 3. Exact freshness thresholds

Use these formula-v1 thresholds:

```text
healthy: last valid observation <= 48 hours old
delayed: > 48 hours and <= 7 days old
stale:   > 7 days old
retired: explicit 404/410 retirement signal, confirmed incompatible schema,
         or 30 consecutive days without a valid observation
disabled: source policy or operator decision disables collection
```

A single malformed response never retires the source. Retirement requires either an explicit terminal response or sustained failure.

### 4. Fail-closed chart semantics

#### Trending

- `healthy`: render normally with current movement data.
- `delayed`: render the last valid ordering with `Updated <date>`; suppress rise/fall badges because velocity is no longer current.
- `stale`, `retired`, or `disabled`: return `available=false`; do not emit ranked entries under the Trending label.

When Trending is unavailable, Biblio defaults the public chart switcher to the first available family in this order:

```text
new_notable
all_time
critically_acclaimed
genre_popular
```

If `new_notable` is also unavailable because it lacks current evidence, the default moves to `all_time`.

The Trending tab remains visible but disabled with concise copy such as `Current chart unavailable`. This preserves the approved five-family information architecture without publishing stale claims.

#### New and Notable

- publication or translation recency alone is not enough to justify `Notable`;
- when current Apple evidence is stale, `new_notable` returns `available=false`;
- a separate unranked `New Releases` shelf may still be produced from publication dates, but it is not a chart and is not labelled `New and Notable`.

#### Popular in This Genre

- may use Apple rolling observations only while the most recent valid observation is at most 7 days old;
- after that threshold, the family returns `available=false` rather than presenting old popularity as current;
- genre pages still render structural shelves, award winners, series, and recent releases.

#### All-Time Essentials

- remains available when Apple fails by renormalizing over permitted named components such as official awards, longevity, and legally cleared historical evidence;
- confidence decreases and the evidence window remains visible.

#### Critically Acclaimed

- remains independent of Apple and continues from official awards plus other permitted long-horizon evidence.

### 5. Last-known-good observations are immutable history, not synthetic freshness

The rolling Apple observation table remains append-only.

On failure:

- write a provider-attempt record;
- write no chart observation;
- preserve the last-known-good observation unchanged;
- mark the date as unavailable in health reporting;
- never duplicate the previous day's ranks under a new observation date.

This creates an honest gap in the time series. It does not fabricate continuity.

### 6. The app has a graceful chart-degradation surface

`BiblioCatalog.status()` and chart responses add:

```text
available
providerState
lastSuccessfulObservation
staleDays
confidence
unavailableReason
```

User-facing rules:

- normal pages show only the refresh date and a short unavailable state;
- provider names, fingerprints, response details, and failure counts remain in diagnostics;
- catalog shelves never disappear merely because chart families are unavailable;
- no red global error banner is shown for a chart-provider outage.

### 7. Replacement providers require admission, not a hot swap

A future replacement or second current-chart source must enter through a provider-neutral interface:

```text
ChartProviderAdapter
- provider_name
- legal_status
- evidence_kind
- fetch_snapshot()
- validate_snapshot()
- normalize_entries()
```

Admission requirements:

1. named source and documented access method;
2. redistribution and usage review;
3. deterministic checked-in fixtures;
4. schema and outage tests;
5. at least 30 days of shadow collection where practical;
6. overlap and divergence report against the existing source;
7. explicit formula-version bump;
8. provenance visible in chart evidence;
9. no silent relabelling of one provider's rank as consensus.

Scraping a retail bestseller webpage is not an automatic fallback. It requires the same legal, technical, and provenance review as any other provider.

### 8. Apple retirement does not force an emergency architecture rewrite

If Apple becomes `retired` before another provider is admitted:

- Trending, New and Notable, and Popular in This Genre become unavailable;
- All-Time Essentials and Critically Acclaimed remain available;
- genre pages use structural and recent-release shelves;
- the landing page defaults to All-Time Essentials;
- the catalog remains fully searchable and browsable;
- the data pipeline continues retaining the historical Apple observations already collected;
- no replacement source is invented under deadline pressure.

This is a deliberate reduced-capability mode, not a broken app.

## Task Amendments

### Task 3 or Task 4 vertical slice

The fixture-backed chart switcher must demonstrate all three states:

- healthy Trending;
- delayed Trending with date and no movement badges;
- stale Trending disabled while another chart becomes default.

The eyes-on review includes the unavailable-state hierarchy.

### Task 5 provider adapter

Add content-fingerprint, schema-validation, and frozen-payload detection to the Apple adapter contract. Fixtures include valid, empty, HTML-error, incompatible-schema, and unchanged-feed cases.

### Task 7 ranking

Chart computation consumes only accepted observations. It never copies a previous observation forward. Chart-family responses carry availability and provider state.

### Task 8 validation and health reporting

Add:

```text
apple_provider_state
apple_last_attempt_at
apple_last_success_at
apple_last_valid_observation_on
apple_consecutive_failures
apple_consecutive_schema_failures
apple_stale_days
apple_frozen_payload_days
current_chart_families_available
current_chart_families_unavailable
```

Validation rejects:

- a current chart built from observations older than its freshness threshold;
- duplicated prior ranks stamped with a new observation date;
- a chart marked available without a valid provider state;
- movement badges computed across an unavailable observation gap.

### Task 12 discovery surfaces

The chart switcher supports disabled families, an explicit default-family fallback, and a separate unranked New Releases shelf when current chart evidence is unavailable.

### Task 13 degraded-mode tests

Test complete Apple retirement while the rest of Biblio remains functional.

## Required Tests

1. HTTP 200 plus an empty feed counts as provider failure.
2. HTTP 200 plus unchanged content beyond the quiet window counts as delayed or stale, not healthy.
3. A failed day creates no observation row.
4. Missing days do not become rank zero.
5. Trending remains visible but delayed for at most seven days.
6. Trending becomes unavailable after seven days without valid data.
7. A stale Trending family never emits rise/fall badges.
8. New and Notable becomes unavailable when current evidence is stale.
9. New Releases remains available as a clearly separate unranked shelf.
10. Genre Popular becomes unavailable after the same freshness boundary.
11. All-Time Essentials and Critically Acclaimed continue without Apple.
12. The landing page selects the first available public chart family deterministically.
13. Explicit 404/410 or sustained 30-day failure marks Apple retired.
14. Retired Apple does not affect search, genres, authors, series, work details, or acquisition.
15. A replacement adapter cannot affect production rankings until its formula version is activated.
16. Diagnostics preserve last-success and retirement reason.

## Result

Apple RSS may be the current engine for formula-v1 momentum, but it is no longer a single point of semantic failure. Temporary outages produce dated degradation, sustained outages disable present-tense chart claims, and permanent retirement leaves Biblio as a smaller but still coherent catalog. A future provider can be admitted deliberately through evidence, licensing, shadow comparison, and a formula-version change rather than smuggled in as an emergency scrape.
