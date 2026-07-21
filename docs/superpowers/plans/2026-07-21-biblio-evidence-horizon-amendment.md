# Biblio Evidence-Horizon Amendment

**Status:** Binding refinement of `2026-07-21-biblio-catalog-implementation.md`.

**Scope:** This amendment supersedes the Goodreads assumptions and related requirements in the V1 Evidence Contract and Tasks 0, 5, 7, 8, 10, and 13. It does not change the approved catalog identity model, English-only policy, chart families, acquisition boundary, or Task 4 visual checkpoint.

## Problem

The UCSD Goodreads Book Graph was collected in late 2017. It is rich historical evidence for older books, series, ratings, shelves, and `similar_books`, but it is not current reader behavior and has little or no coverage for books published after the snapshot.

Biblio must not penalize modern books because they were born after a dataset stopped observing the world. It must also never present a 2017 rating count, recommendation edge, or shelf relationship as current.

A separate source-use gate also applies: the UCSD dataset's official terms describe it as academic-use-only and prohibit redistribution or commercial use. No UCSD-derived artifact may be placed in the app's public data-vault distribution unless explicit permission or a documented license review clears that use.

## Binding Decisions

### 1. Goodreads becomes optional historical enrichment

The production catalog must build and pass validation with no Goodreads input.

`--goodreads-db` becomes optional. When absent:

- series identity may still come from Open Library and checked-in overrides;
- public charts still build;
- modern and older books remain searchable and browsable;
- personal shelves use structural catalog relationships;
- no empty placeholder is emitted for a missing Goodreads field.

When present and legally cleared, Goodreads evidence is stamped:

```text
source = ucsd_goodreads_snapshot
snapshot_date = 2017-12-31
evidence_horizon = historical
```

The date is a coverage marker, not a claim that every source record was captured on that exact day.

### 2. Every signal declares its evidence horizon

Add these fields to provider, evidence, recommendation, and health-report contracts:

```text
source_name
evidence_kind       current | rolling | historical | structural
snapshot_date       nullable ISO date
coverage_start      nullable ISO date
coverage_end        nullable ISO date
redistribution_ok   yes | no | unresolved
```

Validation rejects a ranking or recommendation row whose source or evidence horizon is missing.

Presentation rules:

- current and rolling evidence may support recent-popularity language;
- historical evidence may support historical-audience language only;
- structural evidence supports relationship language, never popularity language;
- raw Goodreads rating counts are never labelled current;
- diagnostics expose the source and snapshot date without turning normal pages into provenance dashboards.

### 3. Recent charts cannot depend on Goodreads

Formula version 1 is amended as follows.

```python
TRENDING = (
    0.70 * apple_current_rank
    + 0.30 * apple_daily_velocity
)

GENRE_POPULAR = (
    0.55 * apple_current_genre_rank
    + 0.45 * apple_rolling_90_day_presence
)

NEW_NOTABLE = (
    0.65 * apple_current_rank
    + 0.35 * publication_or_translation_recency
)
```

Goodreads is forbidden in `trending`, `genre_popular`, and `new_notable` because those charts make present-tense claims.

For long-horizon charts:

```python
ALL_TIME = named_components(
    official_awards,
    publication_longevity,
    apple_longitudinal_presence,
    optional_goodreads_historical_audience,
)

CRITICALLY_ACCLAIMED = named_components(
    official_awards,
    optional_goodreads_historical_rating,
)
```

`named_components` renormalizes only over present, permitted inputs and lowers confidence when components are absent. A work is never scored down merely because it lacks Goodreads coverage.

The UI must show the evidence window for `All-Time Essentials`, for example `Evidence through July 2026`, so the title is editorial taxonomy rather than a false claim of omniscience.

### 4. Biblio starts building its own rolling history

Daily Apple refreshes append normalized observations instead of overwriting yesterday:

```text
chart_observation(
    source_name,
    observed_on,
    chart_family,
    genre_slug,
    source_rank,
    work_id,
    confidence
)
```

Retention is at least 365 days. A monthly structural rebuild imports prior valid Apple observations before the atomic database swap. This gives books published after 2017 a growing first-party evidence trail from Biblio's own deployment date onward.

`apple_rolling_90_day_presence` is computed from distinct observed days and average rank. Missing days caused by feed failure are marked unavailable and do not count as zero popularity.

This rolling history is not described as reader ratings or co-reading behavior. It is chart presence.

### 5. Recommendation reasons become coverage-aware

Supported reasons are split into two classes.

Always available when catalog evidence exists:

```text
same_author
next_in_series
shared_themes
nearby_genre
similar_award_profile
```

Optional historical reason:

```text
readers_also_liked
```

`readers_also_liked` may be emitted only when:

- the edge came directly from Goodreads `similar_books`;
- both endpoint mappings are confident;
- provenance carries the 2017 snapshot date;
- the UCSD source-use gate has been cleared for the distributed build.

The user-facing reason must read `From a historical reader graph`, not `Readers also like`, because the latter implies live behavior.

Modern books with no historical edge fall through the normal hierarchy:

```text
next_in_series
same_author
shared_themes
nearby_genre
similar_award_profile
```

No blank recommendation slot is reserved for Goodreads.

A future rolling chart-neighbor reason may be added only under a distinct code such as `also_charting_in_genre`. It must never be relabelled as reader affinity.

### 6. Production and distribution gates

Task 0 is amended:

- Open Library preparation remains a production prerequisite.
- Goodreads preparation is an optional local research step.
- `tools/biblio_series.db` is not required for Tasks 0 to 4 or for a successful production build.
- the generated Goodreads database remains gitignored;
- the data-vault publisher must reject UCSD-derived rows unless a checked-in clearance record explicitly enables them.

Add a checked-in source policy file:

```text
scripts/biblio_brain/source_policy.json
```

Minimum shape:

```json
{
  "ucsd_goodreads_snapshot": {
    "enabled_for_local_builds": true,
    "enabled_for_distribution": false,
    "clearance_reference": ""
  }
}
```

Changing `enabled_for_distribution` requires a documented human review, not an automatic pipeline decision.

## Task Amendments

### Task 0

Add tests proving Goodreads is optional, distribution is disabled by default, and the source policy is explicit.

### Task 5

The Goodreads adapter remains implementable, but production ingestion skips it cleanly when absent or distribution-disabled. Open Library and Apple behavior is unchanged.

### Task 7

Replace the original formulas with the coverage-aware formulas above. Add rolling Apple observations and forbid historical signals in present-tense charts.

### Task 8

Health reporting adds:

```text
works_with_current_evidence
works_with_rolling_evidence
works_with_historical_evidence
works_with_structural_only_evidence
post_2017_works_without_audience_graph
historical_edges_suppressed_by_policy
```

### Task 10

Personal shelves must produce useful results from structural relationships alone. The harness runs once with Goodreads enabled and once with it absent.

### Task 13

Detail and recommendation copy exposes historical provenance only when relevant. No 2017-derived number receives an unqualified present-tense label.

## Required Tests

The refined plan is not complete until tests prove all of these:

1. A 2024 work with no Goodreads row can rank in Trending, Genre Popular, and New & Notable.
2. Missing Goodreads evidence does not lower the score relative to an otherwise identical work.
3. Goodreads cannot contribute to any present-tense chart.
4. A 2017 `similar_books` edge is labelled historical and carries provenance.
5. A post-2017 work receives structural recommendations without an empty Goodreads shelf.
6. Deleting `tools/biblio_series.db` does not break the fixture build, production build, native service, or QML shelf.
7. A public data-vault build fails closed if UCSD-derived rows exist while distribution clearance is false.
8. Apple feed downtime creates an unavailable observation gap, not a synthetic rank of zero.
9. Monthly rebuilds preserve prior rolling Apple observations.
10. Health output reports modern-book coverage separately from historical coverage.

## Result

The 2017 graph remains useful where it is honest: older-book historical affinity and audience evidence. It stops acting as invisible gravity against newer books. Modern titles use current Apple observations plus durable catalog relationships, and Biblio gradually grows its own longitudinal evidence instead of waiting for another free Goodreads-shaped dataset to appear.