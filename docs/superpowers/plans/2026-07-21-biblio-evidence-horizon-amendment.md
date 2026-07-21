# Biblio Evidence-Horizon Amendment

**Status:** Binding refinement of `2026-07-21-biblio-catalog-implementation.md`.

**Scope:** This amendment removes UCSD Goodreads and every Goodreads-derived feature from V1. It supersedes all Goodreads references in the implementation plan and the previous version of this amendment. The chart-source resilience amendment remains binding.

## Decision

Goodreads is not a V1 source.

Its research snapshot is old, cannot support present-tense reader claims, and is not cleared for public redistribution. V1 therefore carries no Goodreads adapter, generated Goodreads database, distribution gate, historical-reader recommendation edge, or health-report ceremony.

The deletion is complete rather than conditional:

- remove `goodreads_books.json.gz` and `goodreads_book_series.json.gz` from production inputs;
- remove `build_goodreads_series_db.py` and its fixtures from Task 0;
- remove `tools/biblio_series.db` as a catalog prerequisite;
- remove the Goodreads provider adapter from Task 5;
- remove Goodreads ratings, shelves, series evidence, and `similar_books` from ranking and recommendation inputs;
- remove `readers_also_liked`;
- remove the phrase `From a historical reader graph`;
- remove `source_policy.json` and its redistribution gate;
- remove Goodreads-specific coverage counters, policy checks, and tests.

A pre-existing local `tools/biblio_series.db` may remain as an unrelated research artifact, but no V1 build, test, chart, recommendation, or shipped database may read it.

## V1 Shippable Evidence

V1 uses only evidence that can actually ship:

| Purpose | Source |
|---|---|
| Work, edition, author, language, translator, subject, and partial series identity | Open Library dumps |
| Current chart observations and Biblio's rolling chart history | Apple Books RSS while healthy |
| Awards and critical recognition | Checked-in snapshot from named official award lists |
| Identity corrections and difficult series ordering | Checked-in reviewed overrides |
| User activity | Existing local Progress and Collection stores |

Open Library series evidence is incomplete. The UI must show unresolved order honestly, and reviewed overrides may repair high-value cases. V1 does not introduce a legally dubious source merely to make every series perfect.

## Evidence-Horizon Rules

The principle now fits in three rules:

1. Old evidence is never labelled current.
2. Missing evidence never lowers a work's score.
3. When the source required for a claim is unavailable, that claim becomes unavailable rather than being fabricated.

Provider provenance remains in diagnostics and ranking evidence. It does not appear as recommendation copy.

## Rolling Biblio History

Keep the useful part of the previous amendment: Biblio appends each valid daily Apple observation instead of overwriting yesterday.

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

Retention is at least 365 days. Failed collection days produce gaps, not copied ranks or synthetic zeroes. Monthly catalog rebuilds preserve prior valid observations before atomic publication.

This is chart history, not reader ratings or co-reading evidence.

## V1 Chart Inputs

Formula version 1 contains no Goodreads terms.

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

ALL_TIME = named_components(
    official_awards,
    publication_longevity,
    apple_longitudinal_presence,
)

CRITICALLY_ACCLAIMED = named_components(
    award_result_strength,
    award_breadth,
    award_persistence,
)
```

`named_components` renormalizes over present named inputs and lowers confidence when evidence is thin. It never penalizes a work for lacking a removed dataset.

## V1 Recommendations

Recommendations are structural and shippable:

```text
next_in_series
same_author
shared_themes
nearby_genre
similar_award_profile
```

No V1 reason claims reader affinity or co-reading behavior.

User-facing copy must be warm, specific, and about the books or the reader's activity. Approved shapes include:

- `Continue the Silo series`
- `More from Frank Herbert`
- `Because you read Dune`
- `More political science fiction`
- `Award-winning epics with a similar scale`

`Because you loved ...` is allowed only if Biblio later gains an explicit positive-rating or favorite signal. Reading or saving a book alone does not prove love.

Forbidden recommendation copy includes provider names, dataset dates, graph terminology, confidence values, provenance jargon, and phrases such as `historical reader graph`.

Diagnostics may explain why an edge exists. The shelf should speak human.

## Task Amendments

### Task 0

Prepare and verify only the Open Library production dumps. Delete Goodreads downloads, distillation scripts, fixtures, database generation, and policy files from the task.

### Task 5

Implement only Open Library and Apple adapters. Series evidence comes from Open Library plus checked-in overrides.

### Task 7

Use the chart formulas above. Build only the five structural recommendation edge types. Delete all Goodreads-derived inputs and reason codes.

### Task 8

Health output reports source freshness, rolling Apple coverage, unresolved identities, unresolved series order, awards coverage, and chart availability. Delete Goodreads-specific coverage classes and redistribution checks.

### Task 10

The recommendation harness tests structural shelves only. No test mode enables Goodreads.

### Task 13

Recommendation-copy tests assert that reasons are book-centred and contain no database or provenance language.

## Required Tests

1. A post-2017 work can rank without any historical audience dataset.
2. Missing optional evidence does not lower a work relative to an otherwise identical work.
3. No production file, test, or source contract references `ucsd_goodreads_snapshot`, `readers_also_liked`, `historical reader graph`, or `source_policy.json`.
4. Deleting `tools/biblio_series.db` changes no V1 build or runtime behavior.
5. Structural recommendations remain useful for a recent book.
6. User-facing reasons use approved human copy and contain no provenance jargon.
7. Failed Apple days create observation gaps and do not fabricate chart continuity.
8. Open Library series records with unresolved positions remain unresolved until a reviewed override supplies order.

## Result

V1 ships one smaller, legal-by-construction recommendation system. It relies on series, authors, themes, genre proximity, awards, and the user's own activity. Biblio keeps growing its own rolling chart history, and a co-reading feature returns only when a current, redistributable dataset actually exists.