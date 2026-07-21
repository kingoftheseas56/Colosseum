# Biblio Catalog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an English-only, work-level Biblio catalog with durable local identity, separate public charts, genre and facet discovery, author and series pages, explainable recommendations, and Tankorent acquisition only after a work is selected.

**Architecture:** A Python pipeline ingests normalized evidence into a validated SQLite artifact at `data/biblio_catalog.db`. A read-only native `BiblioCatalog` service owns catalog queries and exposes stable `QVariantMap` and `QVariantList` contracts to QML. Existing Apple Books code becomes optional enrichment and fallback metadata, while all discovery, identity, chart, author, series, and genre surfaces read the local catalog first.

**Tech Stack:** Python 3 standard library, SQLite, Qt 6.11.1 Core/Sql/Qml/Quick, C++17, QML/JavaScript, CMake/Ninja, existing Colosseum data-vault scripts.

## Global Constraints

- Biblio is permanently English-only.
- English translations are included and retain translator, original-language, and translation-lineage metadata.
- Public-domain status, commercial status, source availability, seed counts, and local ownership never affect discovery ranking.
- A tile represents a canonical work. Material revisions, substantially different translations, omnibuses, author rewrites, heavily annotated editions, and abridged or dramatized audio texts may be distinct work variants.
- The initial taxonomy has exactly 20 stable top-level genre roots.
- Public chart families are `trending`, `genre_popular`, `all_time`, `new_notable`, and `critically_acclaimed`.
- Public charts are identical for every user. Personal shelves are clearly separate.
- Catalog identity and discovery are complete before acquisition. Tankorent and existing download code remain downstream.
- The application must remain useful when every live metadata provider is unavailable.
- Structural rebuild cadence is monthly, enrichment cadence is daily, and chart evidence refresh cadence is every six hours.
- Generated databases publish atomically and are rejected before swap when validation fails.
- QML renders catalog results but does not merge providers, resolve identity, or compute rankings.
- Do not add a new runtime dependency. Python pipeline code uses the standard library only.
- Preserve the current Windows build requirements and Qt 6.11.1 MSVC layout documented in `README.md`.

---

## File and Responsibility Map

### Pipeline

- Create `scripts/biblio_brain/schema.sql`: final catalog and staging schema.
- Create `scripts/biblio_brain/taxonomy.py`: the immutable 20-root genre registry and facet vocabulary checks.
- Create `scripts/biblio_brain/model.py`: normalized candidate dataclasses shared by providers and resolver.
- Create `scripts/biblio_brain/normalize.py`: title, person, identifier, language, and date normalization.
- Create `scripts/biblio_brain/providers/open_library.py`: streaming Open Library dump adapter for broad works, editions, authors, subjects, and English filtering.
- Create `scripts/biblio_brain/providers/goodreads_series.py`: adapter over the existing `tools/biblio_series.db` evidence.
- Create `scripts/biblio_brain/providers/apple_charts.py`: Apple Books chart evidence and optional metadata enrichment.
- Create `scripts/biblio_brain/overrides.json`: checked-in identity and metadata corrections.
- Create `scripts/biblio_brain/resolver.py`: deterministic work, edition, author, translation, and series identity resolution.
- Create `scripts/biblio_brain/ranking.py`: deterministic public chart formulas.
- Create `scripts/biblio_brain/validate.py`: schema, language, identity, taxonomy, series, chart, and foreign-key validation.
- Create `scripts/biblio_brain/build_catalog.py`: orchestration, health report, atomic publication, and fixture mode.
- Create `scripts/biblio_brain/README.md`: exact data inputs and build commands.

### Native service

- Create `native/engine/BiblioCatalog.h` and `native/engine/BiblioCatalog.cpp`: read-only SQLite seam, grouped search, pages, charts, status, and reload.
- Create `native/engine/BiblioRecommendations.h` and `native/engine/BiblioRecommendations.cpp`: user-specific shelf computation from catalog edges and activity supplied by QML.
- Modify `native/CMakeLists.txt`: compile services and harnesses.
- Modify `native/main.cpp`: expose `BiblioCatalog` and `BiblioRecommendations` context properties.

### QML and JavaScript

- Create `qml/BiblioCatalogAdapter.js`: one mapping layer from native rows to established QML card shapes.
- Create `qml/BiblioWorkTile.qml`: canonical work tile with typographic-jacket fallback.
- Create `qml/BiblioSeriesTile.qml`: stacked series tile.
- Create `qml/BiblioChartSwitcher.qml`: separate public chart tabs and refresh metadata.
- Create `qml/BiblioFacetBar.qml`: facet chips and filter state.
- Create `qml/BiblioAuthor.qml`: first-class author page.
- Modify `qml/BiblioWorld.qml`: local catalog landing page and separate personalization.
- Modify `qml/BiblioGenrePage.qml`: catalog-first genre rails and facets.
- Modify `qml/BiblioGenreIndex.qml`: 20 native genre roots from the catalog.
- Modify `qml/BiblioSearch.qml`: grouped local results for works, authors, and series.
- Modify `qml/BiblioBook.qml`: canonical work page, related reasons, catalog editions, and unchanged acquisition boundary.
- Modify `qml/BiblioSeries.qml`: series ID based reading-order page from the local catalog.
- Modify `qml/BiblioApi.js`: retain acquisition and live enrichment helpers, remove its role as discovery authority.
- Modify `qml/Main.qml`: author and catalog-series navigation and loaders.

### Tests and deployment

- Create `tests/fixtures/biblio/golden_catalog.json`: deliberately difficult identity fixture.
- Create `tests/fixtures/biblio/chart_evidence.json`: deterministic chart fixture.
- Create `tests/biblio_brain_test.py`: Python pipeline contracts.
- Create `tests/biblio_catalog_engine_harness.cpp`: native query contracts.
- Create `tests/biblio_recommendations_harness.cpp`: recommendation separation and reasons.
- Create `tests/biblio_catalog_adapter_probe.qml`: QML mapping contract.
- Create `tests/biblio_world_catalog_probe.qml`: landing-page and public/personal separation smoke test.
- Create `tests/biblio_search_catalog_probe.qml`: grouped search and routing smoke test.
- Modify `scripts/data_vault/publish_release.py` and `scripts/data_vault/pull_data.py`: include `biblio_catalog.db`.
- Modify `README.md`: catalog build, pull, test, and diagnostics commands.

---

### Task 1: Freeze the schema, taxonomy, and golden trouble dataset

**Files:**
- Create: `scripts/biblio_brain/schema.sql`
- Create: `scripts/biblio_brain/taxonomy.py`
- Create: `tests/fixtures/biblio/golden_catalog.json`
- Create: `tests/fixtures/biblio/chart_evidence.json`
- Create: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces: `ROOT_GENRES`, `ROOT_GENRE_BY_SLUG`, `validate_genre_slug(slug: str) -> None`.
- Produces: schema version `1` and a SQLite database containing the tables named below.
- Consumes: nothing from later tasks.

- [ ] **Step 1: Write failing taxonomy and schema tests**

Add these first tests to `tests/biblio_brain_test.py`:

```python
from __future__ import annotations

import json
import sqlite3
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


class TaxonomyContractTest(unittest.TestCase):
    def test_exactly_twenty_stable_roots(self):
        from scripts.biblio_brain.taxonomy import ROOT_GENRES
        self.assertEqual(20, len(ROOT_GENRES))
        self.assertEqual(20, len({g["slug"] for g in ROOT_GENRES}))
        self.assertEqual(20, len({g["name"] for g in ROOT_GENRES}))

    def test_schema_creates_required_tables(self):
        schema = (REPO / "scripts/biblio_brain/schema.sql").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as td:
            db = sqlite3.connect(Path(td) / "catalog.db")
            db.executescript(schema)
            names = {r[0] for r in db.execute(
                "select name from sqlite_master where type='table'"
            )}
        required = {
            "meta", "provider_run", "stage_work", "stage_edition", "stage_person",
            "work", "edition", "person", "work_contributor", "identifier",
            "series", "series_member", "genre", "work_genre", "facet",
            "facet_value", "work_facet", "award", "work_award", "chart_entry",
            "chart_evidence", "recommendation_edge", "field_provenance",
            "unresolved_candidate", "work_fts", "person_fts",
        }
        self.assertTrue(required.issubset(names), required - names)

    def test_golden_fixture_is_english_only_at_user_surface(self):
        fixture = json.loads((REPO / "tests/fixtures/biblio/golden_catalog.json")
                             .read_text(encoding="utf-8"))
        self.assertTrue(fixture["works"])
        self.assertTrue(all(w["display_language"] == "eng" for w in fixture["works"]))
```

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```bat
python -m unittest tests.biblio_brain_test.TaxonomyContractTest -v
```

Expected: imports or file reads fail because the taxonomy, schema, and fixtures do not exist.

- [ ] **Step 3: Add the exact 20-root taxonomy and final schema**

`taxonomy.py` must define these roots in this order:

```python
ROOT_GENRES = [
    {"slug": "fiction-literature", "name": "Fiction & Literature"},
    {"slug": "mystery-thriller", "name": "Mystery & Thriller"},
    {"slug": "science-fiction", "name": "Science Fiction"},
    {"slug": "fantasy", "name": "Fantasy"},
    {"slug": "romance", "name": "Romance"},
    {"slug": "horror", "name": "Horror"},
    {"slug": "historical-fiction", "name": "Historical Fiction"},
    {"slug": "young-adult", "name": "Young Adult"},
    {"slug": "childrens", "name": "Children's"},
    {"slug": "biography-memoir", "name": "Biography & Memoir"},
    {"slug": "history", "name": "History"},
    {"slug": "science-nature", "name": "Science & Nature"},
    {"slug": "technology-computing", "name": "Technology & Computing"},
    {"slug": "business-economics", "name": "Business & Economics"},
    {"slug": "politics-society", "name": "Politics & Society"},
    {"slug": "philosophy-religion", "name": "Philosophy & Religion"},
    {"slug": "health-psychology", "name": "Health & Psychology"},
    {"slug": "arts-culture", "name": "Arts & Culture"},
    {"slug": "travel-adventure", "name": "Travel & Adventure"},
    {"slug": "humor-essays", "name": "Humor & Essays"},
]
ROOT_GENRE_BY_SLUG = {row["slug"]: row for row in ROOT_GENRES}


def validate_genre_slug(slug: str) -> None:
    if slug not in ROOT_GENRE_BY_SLUG:
        raise ValueError(f"unsupported root genre: {slug}")
```

`schema.sql` must use `PRAGMA foreign_keys=ON`, store `schema_version=1`, and create the tables listed by the test. Use text IDs generated by the resolver, explicit foreign keys, unique constraints on provider identifiers, and indexes for title, author, series position, genre, chart, and recommendation lookups. Define `work_fts` and `person_fts` as FTS5 virtual tables. Define `work.variant_kind` as one of `canonical`, `revision`, `translation`, `omnibus`, `annotated`, `rewrite`, and `audio_adaptation`.

The golden fixture must include at least these cases with fixed IDs and expected relationships:

```json
{
  "works": [
    {"id":"w-pride","title":"Pride and Prejudice","display_language":"eng","variant_kind":"canonical"},
    {"id":"w-crime-pevear","title":"Crime and Punishment","display_language":"eng","variant_kind":"translation","translator":"Richard Pevear and Larissa Volokhonsky"},
    {"id":"w-crime-garnett","title":"Crime and Punishment","display_language":"eng","variant_kind":"translation","translator":"Constance Garnett"},
    {"id":"w-dune","title":"Dune","display_language":"eng","variant_kind":"canonical"},
    {"id":"w-dune-audio-drama","title":"Dune","display_language":"eng","variant_kind":"audio_adaptation"},
    {"id":"w-wool-omnibus","title":"Wool Omnibus","display_language":"eng","variant_kind":"omnibus"}
  ],
  "duplicate_title_cases": ["The Stand", "Home", "It"],
  "series_positions": ["0.5", "1", "2"],
  "unresolved_series": ["fixture-unresolved-side-story"]
}
```

- [ ] **Step 4: Run the tests and verify they pass**

Run:

```bat
python -m unittest tests.biblio_brain_test.TaxonomyContractTest -v
```

Expected: three tests pass.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/schema.sql scripts/biblio_brain/taxonomy.py tests/fixtures/biblio tests/biblio_brain_test.py
git commit -m "feat(biblio): define catalog schema and taxonomy"
```

---

### Task 2: Add normalization and provider-neutral candidate models

**Files:**
- Create: `scripts/biblio_brain/model.py`
- Create: `scripts/biblio_brain/normalize.py`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces: `CandidatePerson`, `CandidateWork`, `CandidateEdition`, `CandidateSeriesMembership`, and `ChartEvidence` dataclasses.
- Produces: `normalize_title`, `normalize_person_name`, `normalize_isbn`, `normalize_language`, `parse_year`, and `stable_id`.

- [ ] **Step 1: Write failing normalization tests**

```python
class NormalizationTest(unittest.TestCase):
    def test_title_normalization_keeps_identity_but_drops_marketing_noise(self):
        from scripts.biblio_brain.normalize import normalize_title
        self.assertEqual("dune", normalize_title("Dune: A Novel (Unabridged)"))
        self.assertEqual("crime and punishment", normalize_title("Crime & Punishment"))

    def test_language_accepts_english_aliases_only(self):
        from scripts.biblio_brain.normalize import normalize_language
        self.assertEqual("eng", normalize_language("English"))
        self.assertEqual("eng", normalize_language("en-US"))
        self.assertIsNone(normalize_language("French"))

    def test_stable_id_is_repeatable_and_namespaced(self):
        from scripts.biblio_brain.normalize import stable_id
        self.assertEqual(stable_id("work", "dune|frank herbert"),
                         stable_id("work", "dune|frank herbert"))
        self.assertNotEqual(stable_id("work", "dune"), stable_id("person", "dune"))
```

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.NormalizationTest -v
```

Expected: module import failure.

- [ ] **Step 3: Implement exact models and normalization**

Use frozen dataclasses. Candidate objects must preserve provider, provider ID, source timestamp, field provenance, and confidence. `normalize_title` lowercases, converts `&` to `and`, strips parenthetical audio labels, strips trailing `: A Novel` style marketing tails, Unicode-normalizes, removes punctuation, and collapses whitespace. `stable_id(namespace, key)` returns `namespace + ":" + sha1(key.encode("utf-8")).hexdigest()[:20]`.

The minimum `CandidateWork` interface is:

```python
@dataclass(frozen=True)
class CandidateWork:
    provider: str
    provider_id: str
    title: str
    subtitle: str
    normalized_title: str
    display_language: str
    original_language: str | None
    first_year: int | None
    description: str
    cover: str
    subjects: tuple[str, ...]
    identifiers: tuple[tuple[str, str], ...]
    contributors: tuple[tuple[str, str, str], ...]
    translator_names: tuple[str, ...]
    variant_hint: str
    source_timestamp: str
```

- [ ] **Step 4: Run and verify pass**

```bat
python -m unittest tests.biblio_brain_test.NormalizationTest -v
```

Expected: three tests pass.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/model.py scripts/biblio_brain/normalize.py tests/biblio_brain_test.py
git commit -m "feat(biblio): normalize catalog evidence"
```

---

### Task 3: Build the broad English inventory staging adapters

**Files:**
- Create: `scripts/biblio_brain/providers/__init__.py`
- Create: `scripts/biblio_brain/providers/open_library.py`
- Create: `scripts/biblio_brain/providers/goodreads_series.py`
- Create: `scripts/biblio_brain/providers/apple_charts.py`
- Create: `tests/fixtures/biblio/open_library_sample.txt`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces: `iter_open_library_works(path)`, `iter_open_library_editions(path)`, and `iter_open_library_authors(path)` generators.
- Produces: `iter_goodreads_series(db_path)`.
- Produces: `fetch_apple_chart(country, genre_id, limit, opener) -> list[ChartEvidence]`.
- All adapters emit candidate dataclasses and never write final catalog tables directly.

- [ ] **Step 1: Write failing adapter tests**

Create a tiny Open Library JSON-lines fixture containing one English work, one French-only work, two English editions, one author, subjects, identifiers, and an English translation. Add tests asserting that only English-display candidates are emitted, the original language survives, and the Goodreads adapter preserves fractional positions without inventing order.

```python
class ProviderAdapterTest(unittest.TestCase):
    def test_open_library_filters_non_english_user_editions(self):
        from scripts.biblio_brain.providers.open_library import iter_open_library_works
        rows = list(iter_open_library_works(
            REPO / "tests/fixtures/biblio/open_library_sample.txt"))
        self.assertEqual(["eng"], [r.display_language for r in rows])

    def test_apple_chart_normalizes_single_entry_and_array(self):
        from scripts.biblio_brain.providers.apple_charts import parse_chart
        one = parse_chart({"feed": {"entry": {"id": {"attributes": {"im:id": "1"}},
                    "im:name": {"label": "Dune"}, "im:artist": {"label": "Frank Herbert"}}}},
                    "trending", "2026-07-21T00:00:00Z")
        self.assertEqual(1, len(one))
        self.assertEqual("Dune", one[0].title)
```

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.ProviderAdapterTest -v
```

Expected: adapter modules are missing.

- [ ] **Step 3: Implement streaming adapters**

The Open Library adapter must stream line by line and never load the full dump. Accept both raw JSON lines and tab-separated dump rows whose final field is JSON. Reject candidate editions with no English language code. Preserve non-English original-language metadata on an English translation. Map subjects into normalized source subjects but do not assign the final 20-root taxonomy here.

The Goodreads adapter must read the existing SQLite artifact read-only and emit series evidence with title, author, series name, position string, and source row ID. Empty positions remain empty.

The Apple adapter must use dependency-injected `opener` for tests, a browser user-agent, timeout 30 seconds, and the existing RSS object-or-array normalization. It emits chart evidence only. It does not create canonical work IDs.

- [ ] **Step 4: Run and verify pass**

```bat
python -m unittest tests.biblio_brain_test.ProviderAdapterTest -v
```

Expected: all adapter tests pass without network access.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/providers tests/fixtures/biblio/open_library_sample.txt tests/biblio_brain_test.py
git commit -m "feat(biblio): stage broad catalog evidence"
```

---

### Task 4: Resolve canonical works, material variants, authors, translations, and series

**Files:**
- Create: `scripts/biblio_brain/overrides.json`
- Create: `scripts/biblio_brain/resolver.py`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces: `Resolver.resolve(connection) -> ResolutionReport`.
- Produces: `ResolutionReport(created_works, merged_candidates, unresolved_candidates, applied_overrides)`.
- Consumes: staging tables and taxonomy from Tasks 1 to 3.

- [ ] **Step 1: Write failing identity tests**

Tests must prove:

1. matching ISBN or authority identifier merges candidates;
2. title alone never merges reused-title cases;
3. normalized title plus confidently matched primary author can merge ordinary editions;
4. materially different translator names produce distinct translation work variants linked through `variant_of_work_id`;
5. an omnibus remains a distinct work variant and stores covered series positions;
6. unknown series order remains null;
7. checked-in overrides win and are counted.

Use an in-memory schema and insert candidates directly into staging tables.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.IdentityResolverTest -v
```

Expected: resolver import failure.

- [ ] **Step 3: Implement deterministic resolution rules**

Apply rules in this order:

```text
1. checked-in force-split and force-merge overrides
2. exact unique authority identifier
3. exact normalized ISBN identity
4. exact normalized title + resolved primary author + compatible year window
5. title + author + translator for translation lineage
6. otherwise create a separate work and record an unresolved duplicate suspect
```

Year compatibility is `abs(a - b) <= 2` for ordinary edition evidence and is ignored when either side lacks a year. A candidate with `variant_hint` in `translation`, `revision`, `omnibus`, `annotated`, `rewrite`, or `audio_adaptation` never collapses into the canonical work unless an override explicitly says it is ordinary edition evidence.

Every selected field writes a `field_provenance` row containing provider, provider ID, confidence, and source timestamp. Provider priority is field-specific: authority IDs and publication claims prefer library/publisher evidence; chart position prefers chart evidence; series position prefers series evidence; descriptions and covers choose the highest-confidence non-empty candidate without changing identity.

- [ ] **Step 4: Run and verify pass**

```bat
python -m unittest tests.biblio_brain_test.IdentityResolverTest -v
```

Expected: all identity tests pass.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/overrides.json scripts/biblio_brain/resolver.py tests/biblio_brain_test.py
git commit -m "feat(biblio): resolve canonical work identities"
```

---

### Task 5: Implement taxonomy mapping, facets, awards, recommendation edges, and public ranking

**Files:**
- Create: `scripts/biblio_brain/ranking.py`
- Create: `scripts/biblio_brain/seed/subject_map.json`
- Create: `scripts/biblio_brain/seed/awards.csv`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces: `map_subjects(subjects) -> tuple[root_genres, facet_values]`.
- Produces: `compute_charts(connection, as_of: datetime) -> None`.
- Produces: `build_recommendation_edges(connection) -> None`.

- [ ] **Step 1: Write failing ranking tests**

Load `chart_evidence.json` into an in-memory database. Assert these independent outcomes:

- a fast recent rise wins `trending`;
- a decades-old award winner can win `all_time` without recent chart activity;
- `new_notable` excludes works older than 730 days except new English translations whose translation publication date is within 730 days;
- `critically_acclaimed` favors awards and review evidence over raw popularity;
- `genre_popular` is scoped to one root genre;
- availability and seed fields do not exist in ranking inputs;
- rerunning with the same evidence and `as_of` produces byte-identical rows.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.RankingTest -v
```

Expected: ranking module is missing.

- [ ] **Step 3: Implement explicit formulas**

Normalize each evidence source to `[0, 1]` per source and window. Use these formulas:

```python
TRENDING = 0.55 * recent_position + 0.30 * velocity + 0.15 * rating_confidence
GENRE_POPULAR = 0.45 * long_popularity + 0.30 * recent_position + 0.25 * rating_confidence
ALL_TIME = 0.35 * historical_presence + 0.30 * awards + 0.20 * bayesian_rating + 0.15 * longevity
NEW_NOTABLE = 0.45 * recent_position + 0.25 * editorial + 0.20 * rating_confidence + 0.10 * awards
CRITICALLY_ACCLAIMED = 0.45 * awards + 0.30 * editorial + 0.25 * bayesian_rating
```

Tie-break by score descending, confidence descending, normalized title ascending, and work ID ascending. Store formula version `1` in every `chart_entry` row. Store each contributing source in `chart_evidence` so the score is inspectable.

Create recommendation edges with reason codes `same_author`, `next_in_series`, `shared_themes`, `nearby_genre`, `frequently_alongside`, `similar_critical`, and `similar_audience`. Each edge stores a score and one human-readable reason string.

- [ ] **Step 4: Run and verify pass**

```bat
python -m unittest tests.biblio_brain_test.RankingTest -v
```

Expected: ranking and determinism tests pass.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/ranking.py scripts/biblio_brain/seed tests/biblio_brain_test.py
git commit -m "feat(biblio): compute catalog taxonomy and charts"
```

---

### Task 6: Orchestrate builds, validation, health reports, and atomic publication

**Files:**
- Create: `scripts/biblio_brain/validate.py`
- Create: `scripts/biblio_brain/build_catalog.py`
- Create: `scripts/biblio_brain/README.md`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces CLI:
  `python scripts/biblio_brain/build_catalog.py --fixture tests/fixtures/biblio/golden_catalog.json --output data/biblio_catalog.db`.
- Produces health report beside the DB at `data/biblio_catalog.health.json`.
- Produces exit code `0` on published catalog and `2` on validation rejection.

- [ ] **Step 1: Write failing build and validation tests**

Tests must run fixture mode in a temporary directory and assert:

- output is created only after validation;
- a previous valid output survives a deliberately invalid rebuild;
- `.building` and `.previous` files are cleaned;
- `PRAGMA integrity_check` returns `ok`;
- FTS rows equal visible work/person rows;
- no user-facing non-English work exists;
- exactly 20 genre rows exist;
- every chart entry points to a work;
- circular series membership rejects publication;
- deterministic duplicate collisions reject publication;
- unresolved suspects remain separate and appear in health output.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.BuildPublicationTest -v
```

Expected: build modules are missing.

- [ ] **Step 3: Implement the build command**

The command sequence is exact:

```text
create <output>.building
apply schema
record provider runs
load staging evidence
run resolver
map taxonomy and facets
load awards
build recommendation edges
compute chart entries
populate FTS tables
run validation and query smoke tests
write health JSON
fsync and close
replace output atomically with os.replace
```

Validation errors print one line per failure to stderr and exit `2`; the existing output is untouched. Health JSON includes counts for works, people, editions, series, English translations, root genre coverage, facet coverage, chart coverage, unresolved candidates, duplicate suspects, stale providers, validation failures, applied overrides, schema version, build timestamp, and formula version.

Document three production modes:

```bat
python scripts/biblio_brain/build_catalog.py --open-library-dir D:\catalogs\openlibrary --goodreads-db tools\biblio_series.db --output data\biblio_catalog.db
python scripts/biblio_brain/build_catalog.py --refresh-enrichment --output data\biblio_catalog.db
python scripts/biblio_brain/build_catalog.py --refresh-charts --output data\biblio_catalog.db
```

- [ ] **Step 4: Run and verify pass**

```bat
python -m unittest tests.biblio_brain_test -v
python scripts/biblio_brain/build_catalog.py --fixture tests/fixtures/biblio/golden_catalog.json --output data/biblio_catalog.db
python -c "import sqlite3; d=sqlite3.connect('data/biblio_catalog.db'); print(d.execute('pragma integrity_check').fetchone()[0])"
```

Expected: unit tests pass, build exits `0`, final command prints `ok`.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain tests/biblio_brain_test.py
git commit -m "feat(biblio): publish validated catalog artifacts"
```

---

### Task 7: Add the native read-only BiblioCatalog query seam

**Files:**
- Create: `native/engine/BiblioCatalog.h`
- Create: `native/engine/BiblioCatalog.cpp`
- Create: `tests/biblio_catalog_engine_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces QML context API:

```cpp
Q_INVOKABLE bool ready() const;
Q_INVOKABLE QVariantMap status() const;
Q_INVOKABLE QVariantMap search(const QString& text, int limit = 30) const;
Q_INVOKABLE QVariantMap work(const QString& workId) const;
Q_INVOKABLE QVariantMap author(const QString& personId) const;
Q_INVOKABLE QVariantMap series(const QString& seriesId) const;
Q_INVOKABLE QVariantList genres() const;
Q_INVOKABLE QVariantMap genre(const QString& slug, const QVariantMap& filters,
                              int limit = 60, int offset = 0) const;
Q_INVOKABLE QVariantList chart(const QString& family, const QString& genreSlug = {},
                               int limit = 10) const;
Q_INVOKABLE QVariantList related(const QString& workId, int limit = 12) const;
Q_INVOKABLE QVariantList editions(const QString& workId) const;
Q_INVOKABLE bool reload();
```

- [ ] **Step 1: Write the failing C++ harness**

Create a temporary fixture DB from `schema.sql`, insert two works, one author, one series, two genres, chart rows, recommendation edges, editions, and meta values. Assert:

- missing DB gives `ready()==false` and empty values;
- a valid DB reports schema/build/formula status;
- exact title ranks before prefix and FTS match;
- grouped search returns keys `works`, `authors`, and `series`;
- work page includes contributors, genres, facets, awards, chart appearances, and variant relationships;
- series members preserve numeric and fractional display positions;
- genre filtering uses AND across selected facet groups;
- chart ordering preserves pipeline rank;
- related rows include `reasonCode` and `reason`;
- reload keeps the old connection when the replacement DB is invalid.

- [ ] **Step 2: Add the target and verify compilation fails**

Add the harness target to `native/CMakeLists.txt`, then run:

```bat
cmake --build native/build-msvc --target biblio_catalog_engine_harness
```

Expected: compilation fails because `BiblioCatalog` is missing.

- [ ] **Step 3: Implement the native service**

Follow the established `ComicsCatalog` pattern: unique connection name, `QSQLITE_OPEN_READONLY`, prepared queries, empty-return degradation, and connection removal only after the handle is released. Require `meta.schema_version == 1`. Never expose SQL or provider rows to QML.

`search()` returns:

```cpp
{
  {"works", QVariantList{/* work cards */}},
  {"authors", QVariantList{/* person cards */}},
  {"series", QVariantList{/* series cards */}},
  {"available", true}
}
```

Each work card contains `workId`, `title`, `subtitle`, `author`, `year`, `cover`, `genres`, `variantKind`, and `series`. `work()` returns the same identity plus `description`, `contributors`, `translations`, `variants`, `facets`, `awards`, `chartAppearances`, and `editions`.

- [ ] **Step 4: Build and run the harness**

```bat
cmake --build native/build-msvc --target biblio_catalog_engine_harness
native\build-msvc\biblio_catalog_engine_harness.exe
```

Expected: prints `BIBLIO_CATALOG_ENGINE_OK` and exits `0`.

- [ ] **Step 5: Commit**

```bash
git add native/engine/BiblioCatalog.* tests/biblio_catalog_engine_harness.cpp native/CMakeLists.txt
git commit -m "feat(biblio): expose local catalog service"
```

---

### Task 8: Add separate personalized recommendations without mutating public charts

**Files:**
- Create: `native/engine/BiblioRecommendations.h`
- Create: `native/engine/BiblioRecommendations.cpp`
- Create: `tests/biblio_recommendations_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
Q_INVOKABLE QVariantList shelves(const QVariantList& activity, int perShelf = 12) const;
```

- Consumes activity rows shaped as `{workId, title, authorId, seriesId, progress, saved, updatedAt}`.
- Produces shelf rows shaped as `{id, title, personalized, reason, items}`.

- [ ] **Step 1: Write the failing recommendation harness**

Fixture activity must produce these shelves when evidence exists: `because_you_read`, `continue_author`, `next_in_series`, and `explore_nearby`. Assert every returned shelf has `personalized=true`, every item has a visible reason, consumed work IDs are excluded, duplicates across shelves are removed by first-shelf priority, and querying public charts before and after `shelves()` returns identical data.

- [ ] **Step 2: Build and verify failure**

```bat
cmake --build native/build-msvc --target biblio_recommendations_harness
```

Expected: service class is missing.

- [ ] **Step 3: Implement shelf computation**

Priority is:

```text
next_in_series
continue_author
because_you_read
explore_nearby
```

Use only catalog relationships and supplied activity. Do not persist profiles, alter `chart_entry`, or call live services. Cap each shelf to `perShelf`, deduplicate by work ID, and omit empty shelves.

- [ ] **Step 4: Run the harness**

```bat
cmake --build native/build-msvc --target biblio_recommendations_harness
native\build-msvc\biblio_recommendations_harness.exe
```

Expected: prints `BIBLIO_RECOMMENDATIONS_OK` and exits `0`.

- [ ] **Step 5: Commit**

```bash
git add native/engine/BiblioRecommendations.* tests/biblio_recommendations_harness.cpp native/CMakeLists.txt
git commit -m "feat(biblio): add explainable personal shelves"
```

---

### Task 9: Register services and deploy the catalog through the data vault

**Files:**
- Modify: `native/main.cpp`
- Modify: `scripts/data_vault/publish_release.py`
- Modify: `scripts/data_vault/pull_data.py`
- Modify: `README.md`

**Interfaces:**
- Produces QML context properties `BiblioCatalog` and `BiblioRecommendations`.
- Adds `biblio_catalog.db` to both GitHub-release and flat-host vault paths.

- [ ] **Step 1: Add a source contract test**

Extend `tests/biblio_brain_test.py` with a source-level assertion that `main.cpp` constructs `BiblioCatalog("data/biblio_catalog.db")`, registers both context properties, and that both vault scripts list `biblio_catalog.db`.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.NativeRegistrationSourceTest -v
```

Expected: assertions fail.

- [ ] **Step 3: Wire services and vault scripts**

In `main.cpp`, construct the catalog beside `ComicsCatalog` and `MalCatalog`, then construct recommendations with a pointer/reference to it:

```cpp
auto *biblioCatalog = new BiblioCatalog(QStringLiteral("data/biblio_catalog.db"), &app);
engine.rootContext()->setContextProperty(QStringLiteral("BiblioCatalog"), biblioCatalog);
auto *biblioRecommendations = new BiblioRecommendations(biblioCatalog, &app);
engine.rootContext()->setContextProperty(QStringLiteral("BiblioRecommendations"), biblioRecommendations);
```

Update `publish_release.py` to:

```python
ARTIFACTS = ["comics_catalog.db", "mal_catalog.db", "biblio_catalog.db"]
```

Update the flat-host loop in `pull_data.py` to the same three names. GitHub-release mode already downloads every release asset, so do not add a second special case there.

Document the build, publish, and pull commands in `README.md`.

- [ ] **Step 4: Verify**

```bat
python -m unittest tests.biblio_brain_test.NativeRegistrationSourceTest -v
cmake --build native/build-msvc --target colosseum
```

Expected: test passes and native build succeeds.

- [ ] **Step 5: Commit**

```bash
git add native/main.cpp scripts/data_vault README.md tests/biblio_brain_test.py
git commit -m "feat(biblio): register and distribute catalog"
```

---

### Task 10: Create the QML catalog adapter and reusable work, series, chart, and facet components

**Files:**
- Create: `qml/BiblioCatalogAdapter.js`
- Create: `qml/BiblioWorkTile.qml`
- Create: `qml/BiblioSeriesTile.qml`
- Create: `qml/BiblioChartSwitcher.qml`
- Create: `qml/BiblioFacetBar.qml`
- Create: `tests/biblio_catalog_adapter_probe.qml`

**Interfaces:**
- Produces JS functions `workCard(row)`, `seriesCard(row)`, `authorCard(row)`, `groupedSearch(payload)`, and `personalActivity(progressRows, collectionRows)`.
- Components emit `workRequested(var work)`, `seriesRequested(string seriesId)`, `chartChanged(string family)`, and `filtersChanged(var filters)`.

- [ ] **Step 1: Write the failing QML adapter probe**

Use a fake native payload and assert:

- work IDs and series IDs are never replaced by title keys;
- ordinary edition data does not create duplicate cards;
- `variantKind` survives;
- missing cover becomes an empty source plus deterministic `c1` and `c2` typographic-jacket colors;
- series cards carry `bookCount`, `cover`, and `covers`;
- grouped search preserves native ordering and groups;
- activity maps current Progress and Collection rows without availability fields.

- [ ] **Step 2: Run and verify failure**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_adapter_probe.qml
```

Expected: import or function failure.

- [ ] **Step 3: Implement the adapter and components**

The adapter is the only QML-side place that translates native row keys into existing card properties such as `title`, `caption`, `cover`, `art`, `author`, `year`, `genres`, and `genreLine`. It must not sort or merge.

`BiblioWorkTile.qml` uses a normal `Image` when `cover` is non-empty and a typographic jacket when empty. `BiblioSeriesTile.qml` uses the approved stacked-book visual. `BiblioChartSwitcher.qml` exposes five exact chart tabs and displays the `builtAt` or `sourceAsOf` date. `BiblioFacetBar.qml` groups selected facet values into a map whose values are string lists.

- [ ] **Step 4: Run and verify pass**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_adapter_probe.qml
```

Expected: prints `BIBLIO_CATALOG_ADAPTER_OK` and exits `0`.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioCatalogAdapter.js qml/BiblioWorkTile.qml qml/BiblioSeriesTile.qml qml/BiblioChartSwitcher.qml qml/BiblioFacetBar.qml tests/biblio_catalog_adapter_probe.qml
git commit -m "feat(biblio): add catalog UI primitives"
```

---

### Task 11: Replace the Biblio landing page with public charts plus separate personal shelves

**Files:**
- Modify: `qml/BiblioWorld.qml`
- Modify: `qml/BiblioApi.js`
- Create: `tests/biblio_world_catalog_probe.qml`

**Interfaces:**
- Consumes: `BiblioCatalog.chart`, `BiblioCatalog.genres`, `BiblioRecommendations.shelves`.
- Produces existing navigation signals plus `authorRequested(string personId)` and `catalogSeriesRequested(string seriesId)` when needed by tiles.

- [ ] **Step 1: Write the failing world probe**

Inject fake catalog and recommendation objects. Assert:

- the default public tab asks for `trending`, not Apple Books;
- switching tabs asks for the exact family;
- public chart items do not change when activity changes;
- personal shelves have visible personalized labels and sit after public discovery and structured browsing;
- missing catalog falls back to the existing static rows without invoking availability ranking;
- Apple enrichment failure does not blank the page.

- [ ] **Step 2: Run and verify failure**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_world_catalog_probe.qml
```

Expected: current `BiblioWorld.qml` calls `BiblioApi.loadBiblio` as discovery authority.

- [ ] **Step 3: Rebuild the world page data flow**

Remove the automatic Apple chart override. Load local public charts synchronously when `BiblioCatalog.ready()`. Keep `Catalog.biblioFeatured` and `Catalog.biblioTop` only as a no-artifact development fallback. Add chart tabs for all five families, a genre preview from `BiblioCatalog.genres()`, then separate personal rails from `BiblioRecommendations.shelves(BiblioCatalogAdapter.personalActivity(...))`.

Retain `BiblioApi.lookupBook` only as an enrichment fallback for a catalog work whose description or cover is empty. The canonical `workId` remains unchanged.

- [ ] **Step 4: Run probe and boot smoke**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_world_catalog_probe.qml
set COLOSSEUM_OPEN_WORLD=Biblio
native\build-msvc\colosseum.exe qml\Main.qml
```

Expected: probe prints `BIBLIO_WORLD_CATALOG_OK`; Biblio opens with separate chart tabs, genres, and personalized shelves. Stop the manual smoke after confirming no QML errors.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioWorld.qml qml/BiblioApi.js tests/biblio_world_catalog_probe.qml
git commit -m "feat(biblio): make local charts the discovery authority"
```

---

### Task 12: Convert genre index and genre pages to the 20-root catalog with facets

**Files:**
- Modify: `qml/BiblioGenreIndex.qml`
- Modify: `qml/BiblioGenrePage.qml`
- Retire discovery use in: `qml/BiblioGenreApi.js`
- Create: `tests/biblio_genre_catalog_probe.qml`

**Interfaces:**
- Consumes: `BiblioCatalog.genres()` and `BiblioCatalog.genre(slug, filters, limit, offset)`.
- Genre page payload is `{genre, total, works, montage, availableFacets, appliedFilters, builtAt}`.

- [ ] **Step 1: Write the failing genre probe**

Assert exactly 20 roots render, counts and cover mosaics come from the native payload, selecting multiple values in the same facet group uses OR, selecting values across different groups uses AND, public-domain metadata is absent, changing filters does not change the page's root identity, and an empty live provider still leaves local cards visible.

- [ ] **Step 2: Run and verify failure**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_genre_catalog_probe.qml
```

Expected: current files read `Catalog.biblioGenres` and Apple RSS.

- [ ] **Step 3: Replace data access while preserving visual language**

`BiblioGenreIndex.qml` reads native genre rows and uses the existing mosaic treatment. `BiblioGenrePage.qml` accepts `genreSlug` plus display name, loads native payloads, renders contextual rails `Popular`, `New and Notable`, `Award Winners`, and the full filtered list, and uses `BiblioFacetBar` for facets. Keep the hero montage and current shell controls.

Leave `BiblioGenreApi.js` only as a development fallback function named `loadLegacyGenre`; no production page calls it when `BiblioCatalog.ready()`.

- [ ] **Step 4: Verify**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_genre_catalog_probe.qml
```

Expected: prints `BIBLIO_GENRE_CATALOG_OK`.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioGenreIndex.qml qml/BiblioGenrePage.qml qml/BiblioGenreApi.js tests/biblio_genre_catalog_probe.qml
git commit -m "feat(biblio): browse local genres and facets"
```

---

### Task 13: Replace Apple-first search with grouped local work, author, and series search

**Files:**
- Modify: `qml/BiblioSearch.qml`
- Create: `tests/biblio_search_catalog_probe.qml`

**Interfaces:**
- Consumes: `BiblioCatalog.search(query, 30)`.
- Produces signals `workRequested(var work)`, `authorRequested(string personId)`, and `catalogSeriesRequested(string seriesId)`.

- [ ] **Step 1: Write the failing search probe**

Inject a fake catalog. Assert:

- two-character minimum remains;
- stale query results are ignored;
- groups render in order Works, Authors, Series;
- exact work title is top match;
- same-title works remain distinct by author/year/variant metadata;
- edition rows do not appear unless they are material variants;
- selecting an author or series emits its stable ID;
- catalog unavailable is distinct from no matches;
- Apple search is optional enrichment only and cannot replace or reorder local results.

- [ ] **Step 2: Run and verify failure**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_search_catalog_probe.qml
```

Expected: current search dispatches Apple Books.

- [ ] **Step 3: Implement grouped local search**

Retain the 200 ms debounce and stale-query guard. Replace `searchDispatcher: BiblioApi.search` with a catalog dispatcher that returns synchronously through a small callback wrapper for testability. Remove the independent audiobook discovery column because audiobooks are nested editions of a work unless materially distinct. Keep recent query history.

When the catalog is unavailable, optionally show an explicitly labelled `Live metadata fallback` group from Apple without presenting it as the canonical catalog. A selected fallback result opens a temporary work detail that may be acquired, but it does not enter public charts or local identity until the next pipeline build.

- [ ] **Step 4: Verify**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_search_catalog_probe.qml
```

Expected: prints `BIBLIO_SEARCH_CATALOG_OK`.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioSearch.qml tests/biblio_search_catalog_probe.qml
git commit -m "feat(biblio): search canonical works authors and series"
```

---

### Task 14: Upgrade work, series, and author pages and wire navigation

**Files:**
- Modify: `qml/BiblioBook.qml`
- Modify: `qml/BiblioSeries.qml`
- Create: `qml/BiblioAuthor.qml`
- Modify: `qml/Main.qml`
- Create: `tests/biblio_detail_navigation_probe.qml`

**Interfaces:**
- `BiblioBook.qml` consumes `workId` and calls `BiblioCatalog.work(workId)`.
- `BiblioSeries.qml` consumes `seriesId` and calls `BiblioCatalog.series(seriesId)`.
- `BiblioAuthor.qml` consumes `personId` and calls `BiblioCatalog.author(personId)`.
- Existing acquisition functions continue consuming title and author after the page has loaded the canonical work.

- [ ] **Step 1: Write the failing navigation probe**

Inject fake work, series, and author payloads. Assert:

- work page shows genres, facets, awards, translation metadata, chart appearances, and visible recommendation reasons;
- ordinary format editions are nested;
- distinct translations and revisions link as work variants;
- selecting an author opens the author page by person ID;
- selecting a series opens by series ID;
- series page shows recommended and publication order when different, fractional positions, novellas, subseries, omnibus coverage, and unresolved order without fabricated numbers;
- author page shows bibliography, series, standalones, collaborations, pseudonyms, popular works, chronological works, and a recommended starting point;
- clicking Acquire invokes the existing Biblio download/Tankorent path only after work selection.

- [ ] **Step 2: Run and verify failure**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_detail_navigation_probe.qml
```

Expected: current detail pages are Apple-object and title-key based.

- [ ] **Step 3: Implement pages and loaders**

Preserve the current dust-jacket design and acquisition code in `BiblioBook.qml`, but make the canonical catalog payload the page model. Move `loadEditions()` acquisition calls behind an explicit acquisition section or first expansion so opening a metadata page does not immediately begin source discovery. Catalog editions render above acquisition results and use different labels: `Editions and text variants` versus `Get this book`.

Update `BiblioSeries.qml` to query the local series record and emit work requests with stable work IDs. Add `BiblioAuthor.qml` in the same solid-page visual family. In `Main.qml`, add loaders and functions:

```qml
function openBiblioAuthor(personId) { /* set loader property and activate */ }
function openBiblioSeries(seriesId) { /* set loader property and activate */ }
function openCatalogWork(work) { /* set bookLayer.workId and activate */ }
```

Connect the new signals from Biblio world, genre, search, work, author, and series surfaces. Add Escape ordering so work closes before author/series, and author/series close before genre/search layers.

- [ ] **Step 4: Verify navigation and acquisition boundary**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_detail_navigation_probe.qml
set COLOSSEUM_OPEN_WORLD=Biblio
native\build-msvc\colosseum.exe qml\Main.qml
```

Expected: probe prints `BIBLIO_DETAIL_NAVIGATION_OK`; manual smoke can move work to author to series and back without QML errors; no torrent or LibGen search starts until the acquisition section is opened.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioBook.qml qml/BiblioSeries.qml qml/BiblioAuthor.qml qml/Main.qml tests/biblio_detail_navigation_probe.qml
git commit -m "feat(biblio): add canonical work author and series pages"
```

---

### Task 15: Add live enrichment, diagnostics, performance gates, and full verification

**Files:**
- Modify: `qml/BiblioApi.js`
- Modify: `qml/BiblioBook.qml`
- Modify: `qml/BiblioWorld.qml`
- Create: `tests/biblio_catalog_performance_harness.cpp`
- Create: `tests/biblio_catalog_degraded_probe.qml`
- Modify: `native/CMakeLists.txt`
- Modify: `README.md`

**Interfaces:**
- Produces `BiblioApi.enrichCatalogWork(work, done)` that only fills empty cover, description, rating, and recent publication metadata.
- Uses `BiblioCatalog.status()` for visible refresh dates and diagnostic state.

- [ ] **Step 1: Write failing degraded-mode and performance tests**

C++ performance harness builds or opens a fixture with at least 100,000 works and asserts on the development machine:

- constructor plus schema probe is under 250 ms;
- exact title search p95 over 100 runs is under 25 ms;
- genre first page p95 is under 25 ms;
- chart retrieval p95 is under 10 ms;
- work detail p95 is under 15 ms.

The QML degraded probe asserts:

- missing DB shows static development fallback and a diagnostic unavailable state;
- stale charts show their last refresh date;
- one degraded provider does not blank charts;
- missing covers render typographic jackets;
- missing descriptions do not hide works;
- no matches and catalog unavailable use different copy;
- enrichment arriving later fills empty fields without changing `workId`, rank, or order.

- [ ] **Step 2: Run and verify failure**

```bat
cmake --build native/build-msvc --target biblio_catalog_performance_harness
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_degraded_probe.qml
```

Expected: target and probe are missing.

- [ ] **Step 3: Implement enrichment and diagnostics**

`enrichCatalogWork` searches Apple by exact title plus author and accepts a result only when normalized title and normalized primary author match. It returns a new object containing only fields that were empty in the catalog object. It never changes IDs, genres, series, variants, awards, chart membership, or recommendation edges.

Surface catalog build date and chart source date in chart UI. Provider names and failure details remain available in a diagnostics object but do not become prominent user-facing banners. Add indexes or query changes until performance gates pass; do not move ranking or provider merging into QML to achieve them.

- [ ] **Step 4: Run the complete verification matrix**

```bat
python -m unittest tests.biblio_brain_test -v
python scripts/biblio_brain/build_catalog.py --fixture tests/fixtures/biblio/golden_catalog.json --output data/biblio_catalog.db
cmake --build native/build-msvc --target biblio_catalog_engine_harness biblio_recommendations_harness biblio_catalog_performance_harness colosseum
native\build-msvc\biblio_catalog_engine_harness.exe
native\build-msvc\biblio_recommendations_harness.exe
native\build-msvc\biblio_catalog_performance_harness.exe data\biblio_catalog.db
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_adapter_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_world_catalog_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_genre_catalog_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_search_catalog_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_detail_navigation_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_degraded_probe.qml
```

Expected: every command exits `0`, all probes print their `_OK` marker, and `colosseum` builds successfully.

Then perform one eyes-on smoke with the generated fixture DB:

```bat
set COLOSSEUM_OPEN_WORLD=Biblio
native\build-msvc\colosseum.exe qml\Main.qml
```

Verify chart switching, genre filters, grouped search, work to author to series navigation, typographic cover fallback, personalized labels, and acquisition only after explicit action.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioApi.js qml/BiblioBook.qml qml/BiblioWorld.qml tests/biblio_catalog_performance_harness.cpp tests/biblio_catalog_degraded_probe.qml native/CMakeLists.txt README.md
git commit -m "test(biblio): verify catalog resilience and performance"
```

---

## Final Self-Review Checklist

Before declaring implementation complete, verify each item against `docs/superpowers/specs/2026-07-21-biblio-catalog-design.md`:

- [ ] English-only user-facing catalog is enforced in staging, final validation, native queries, and QML.
- [ ] English translations retain translator and original-language lineage.
- [ ] Canonical works collapse ordinary formats but split materially different texts.
- [ ] Authors and series are stable entities with stable IDs.
- [ ] Exactly 20 root genres exist and facets remain separate from root navigation.
- [ ] Five independent public chart families exist and are reproducible.
- [ ] Availability, public-domain status, commercial status, ownership, and seed counts are absent from ranking.
- [ ] Public charts remain stable when personalization inputs change.
- [ ] Recommendation reasons are visible.
- [ ] Search groups works, authors, and series and distinguishes unavailable catalog from no results.
- [ ] Work, author, and series pages expose the relationships required by the design.
- [ ] Acquisition begins only after explicit user action on a selected work.
- [ ] Local catalog remains useful with every live provider disabled.
- [ ] Atomic publication preserves the previous valid DB on every rejection path.
- [ ] Health report, freshness, degraded-provider state, and query provenance are inspectable.
- [ ] Full Python, C++, QML, build, performance, and eyes-on verification matrix passes.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-21-biblio-catalog-implementation.md`.

1. **Subagent-Driven, recommended:** use `superpowers:subagent-driven-development`, dispatch a fresh worker per task, and review specification compliance and code quality after each task.
2. **Inline Execution:** use `superpowers:executing-plans`, execute in batches with checkpoints after Tasks 1-6, 7-9, 10-13, and 14-15.
