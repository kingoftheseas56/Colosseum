# Biblio Catalog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an English-only, work-level Biblio catalog with durable local identity, transparent public charts, genre and facet discovery, author and series pages, explainable recommendations, and Tankorent acquisition only after a work is selected.

**Architecture:** A Python pipeline streams named public datasets into a validated SQLite artifact at `data/biblio_catalog.db`. A read-only native `BiblioCatalog` service owns identity and queries, while QML renders stable result contracts. A fixture-built vertical slice reaches the screen before the full multi-gigabyte ingestion work, and the existing Apple-backed page remains the fallback throughout migration.

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
- The application remains useful when every live metadata provider is unavailable.
- Structural rebuild cadence is monthly. Apple chart snapshots and optional enrichment run once daily, plus manual refresh on demand.
- Generated databases publish atomically and are rejected before swap when validation fails.
- QML renders catalog results but does not merge providers, resolve identity, or compute rankings.
- Do not add a runtime dependency. Pipeline code uses the Python standard library only.
- Preserve the current Windows build requirements and Qt 6.11.1 MSVC layout documented in `README.md`.

## V1 Evidence Contract

The first release is a **named hybrid evidence model**, not a claim of neutral industry consensus.

| Evidence | Exact source | Used for | Explicitly not used for |
|---|---|---|---|
| Bibliographic identity | Open Library monthly `works`, `editions`, and `authors` dumps | works, editions, authors, dates, identifiers, subjects, languages, translators | popularity or critical judgment |
| Series and historical audience evidence | UCSD Book Graph `goodreads_books.json.gz` and `goodreads_book_series.json.gz` | series order, ratings count, average rating, popular shelves, `similar_books` relationships | current trends |
| Recent chart evidence | Apple Books keyless RSS charts | current rank, daily rank velocity, recent genre chart position | canonical identity or all-time consensus |
| Award evidence | checked-in `awards.csv` generated from named official award lists | award relationships and award-heavy chart evidence | unnamed editorial judgment |

V1 has no anonymous `editorial` signal. `Trending` and the recent component of `New & Notable` are intentionally Apple-led and expose their evidence source. `Critically Acclaimed` is award-led. Goodreads evidence is a historical snapshot and is labelled as such. Add another chart provider only in a later formula version with its own adapter, provenance, tests, and migration.

The supported recommendation reasons are `same_author`, `next_in_series`, `shared_themes`, `nearby_genre`, `readers_also_liked`, and `similar_award_profile`. `readers_also_liked` is backed specifically by the UCSD Goodreads `similar_books` field. Do not emit “frequently read alongside” unless a future co-reading dataset is added.

## Production Inputs and Ownership

The implementation track owns both missing prerequisites. They are not manual chores left to an unnamed maintainer.

- The worker assigned Task 0 creates the input-preparation scripts and generates `tools/biblio_series.db` from the two UCSD Goodreads files.
- The same task downloads or verifies the three Open Library dumps under `D:\catalogs\openlibrary` for the first production build.
- Raw dumps, the generated Goodreads SQLite index, and `data/biblio_catalog.db` remain gitignored. Only scripts, manifests, fixtures, provenance snapshots, and tests are committed.
- Tasks 1 to 4 use tiny checked-in fixtures and do not wait for multi-gigabyte downloads. Task 5 is the first production-data gate.

The expected production input layout is:

```text
D:\catalogs\
├── openlibrary\
│   ├── ol_dump_works_latest.txt.gz
│   ├── ol_dump_editions_latest.txt.gz
│   └── ol_dump_authors_latest.txt.gz
└── goodreads\
    ├── goodreads_books.json.gz
    └── goodreads_book_series.json.gz
```

---

## File and Responsibility Map

### Pipeline

- Create `scripts/biblio_brain/input_manifest.py`: exact input names, source URLs, and role declarations.
- Create `scripts/biblio_brain/prepare_inputs.py`: resumable download/check command for Open Library and UCSD Goodreads files.
- Create `scripts/biblio_brain/build_goodreads_series_db.py`: distill the UCSD files into `tools/biblio_series.db`.
- Create `scripts/biblio_brain/schema.sql`: final and staging schema.
- Create `scripts/biblio_brain/taxonomy.py`: immutable 20-root genre registry.
- Create `scripts/biblio_brain/model.py`: normalized candidate dataclasses.
- Create `scripts/biblio_brain/normalize.py`: title, person, ISBN, language, and date normalization.
- Create `scripts/biblio_brain/providers/open_library.py`: streaming Open Library adapters.
- Create `scripts/biblio_brain/providers/goodreads.py`: series, rating, shelf, and `similar_books` evidence.
- Create `scripts/biblio_brain/providers/apple_charts.py`: Apple RSS chart snapshots.
- Create `scripts/biblio_brain/seed/awards.csv`: official-award snapshot with provenance.
- Create `scripts/biblio_brain/overrides.json`: checked-in identity corrections.
- Create `scripts/biblio_brain/resolver.py`: deterministic work, edition, contributor, translation, and series resolution.
- Create `scripts/biblio_brain/ranking.py`: formula-versioned public charts using only named evidence.
- Create `scripts/biblio_brain/validate.py`: integrity and policy validation.
- Create `scripts/biblio_brain/build_catalog.py`: fixture and production orchestration, health report, atomic publication.
- Create `scripts/biblio_brain/README.md`: setup costs, inputs, commands, and evidence limitations.

### Native

- Create `native/engine/BiblioCatalog.h` and `.cpp`: read-only SQLite seam.
- Create `native/engine/BiblioRecommendations.h` and `.cpp`: user-specific shelves from catalog edges and supplied activity.
- Modify `native/CMakeLists.txt`: services and harnesses.
- Modify `native/main.cpp`: expose `BiblioCatalog` and `BiblioRecommendations`.

### QML

- Create `qml/BiblioCatalogAdapter.js`: native rows to existing card shapes without sorting.
- Create `qml/BiblioWorkTile.qml`, `BiblioSeriesTile.qml`, `BiblioChartSwitcher.qml`, and `BiblioFacetBar.qml`.
- Create `qml/BiblioAuthor.qml`.
- Modify `qml/BiblioWorld.qml`, `BiblioGenrePage.qml`, `BiblioGenreIndex.qml`, `BiblioSearch.qml`, `BiblioBook.qml`, `BiblioSeries.qml`, `BiblioApi.js`, and `Main.qml`.

### Tests and deployment

- Create fixture files under `tests/fixtures/biblio/`.
- Create Python, C++, and QML probes named in the tasks below.
- Modify `scripts/data_vault/publish_release.py`, `scripts/data_vault/pull_data.py`, and `README.md`.

---

### Task 0: Make production data prerequisites explicit and reproducible

**Files:**
- Create: `scripts/biblio_brain/input_manifest.py`
- Create: `scripts/biblio_brain/prepare_inputs.py`
- Create: `scripts/biblio_brain/build_goodreads_series_db.py`
- Create: `tests/fixtures/biblio/goodreads_books_sample.json.gz`
- Create: `tests/fixtures/biblio/goodreads_series_sample.json.gz`
- Create: `tests/biblio_inputs_test.py`
- Modify: `.gitignore`
- Create: `scripts/biblio_brain/README.md`

**Interfaces:**
- Produces `prepare_inputs.py --check --root <path>` and `prepare_inputs.py --download --root <path>`.
- Produces `build_goodreads_series_db.py --books <gz> --series <gz> --output <db>`.
- Produces SQLite tables `book`, `series`, `series_member`, and `book_similarity`.

- [ ] **Step 1: Write failing manifest and distillation tests**

```python
class InputManifestTest(unittest.TestCase):
    def test_exact_named_inputs(self):
        from scripts.biblio_brain.input_manifest import INPUTS
        self.assertEqual({
            "ol_dump_works_latest.txt.gz",
            "ol_dump_editions_latest.txt.gz",
            "ol_dump_authors_latest.txt.gz",
            "goodreads_books.json.gz",
            "goodreads_book_series.json.gz",
        }, {row.filename for row in INPUTS})

    def test_goodreads_distill_keeps_series_and_similarity(self):
        from scripts.biblio_brain.build_goodreads_series_db import build
        build(BOOKS_SAMPLE, SERIES_SAMPLE, self.output)
        db = sqlite3.connect(self.output)
        self.assertEqual("0.5", db.execute(
            "select position from series_member where book_id='b-side'"
        ).fetchone()[0])
        self.assertEqual("b-neighbor", db.execute(
            "select similar_book_id from book_similarity where book_id='b-main'"
        ).fetchone()[0])
```

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_inputs_test -v
```

Expected: modules and fixtures are missing.

- [ ] **Step 3: Implement the preparation scripts**

`input_manifest.py` defines immutable records with `filename`, `url`, `group`, and `purpose`. Use this Open Library base:

```python
OPEN_LIBRARY_BASE = "https://openlibrary.org/data"
```

and these filenames:

```python
"ol_dump_works_latest.txt.gz"
"ol_dump_editions_latest.txt.gz"
"ol_dump_authors_latest.txt.gz"
```

The Goodreads manifest names the UCSD Book Graph files and records their public-dataset page in the README. `prepare_inputs.py` streams to `<filename>.downloading`, supports HTTP `Range` when the server accepts it, renames only after a complete response, and `--check` opens every file with `gzip.open`, reads the first record, and reports missing/corrupt inputs with exit code `2`.

`build_goodreads_series_db.py` streams both gzip JSON-lines files. Keep only `language_code` values normalized to English. Store title, title-without-series, work ID, authors, ratings count, average rating, shelves, series membership, fractional position, and `similar_books`. Build to `<output>.building`, run `PRAGMA integrity_check`, then `os.replace`.

Add these ignores:

```gitignore
/data/biblio_catalog.db
/data/biblio_catalog.db.*
/data/biblio_catalog.health.json
/tools/biblio_series.db
```

- [ ] **Step 4: Verify fixture and production-check behavior**

```bat
python -m unittest tests.biblio_inputs_test -v
python scripts\biblio_brain\prepare_inputs.py --check --root D:\catalogs
python scripts\biblio_brain\build_goodreads_series_db.py ^
  --books D:\catalogs\goodreads\goodreads_books.json.gz ^
  --series D:\catalogs\goodreads\goodreads_book_series.json.gz ^
  --output tools\biblio_series.db
```

Expected: unit tests pass. The check command exits `2` with a precise missing-file list until dumps are present. Once inputs exist, the index command exits `0`, prints row counts, and `tools\biblio_series.db` passes `PRAGMA integrity_check`.

- [ ] **Step 5: Commit**

```bash
git add .gitignore scripts/biblio_brain tests/fixtures/biblio tests/biblio_inputs_test.py
git commit -m "build(biblio): make catalog inputs reproducible"
```

---

### Task 1: Freeze the schema, taxonomy, and golden trouble dataset

**Files:**
- Create: `scripts/biblio_brain/schema.sql`
- Create: `scripts/biblio_brain/taxonomy.py`
- Create: `tests/fixtures/biblio/golden_catalog.json`
- Create: `tests/fixtures/biblio/chart_evidence.json`
- Create: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces `ROOT_GENRES`, `ROOT_GENRE_BY_SLUG`, and schema version `1`.
- Produces final tables for works, editions, people, series, taxonomy, provenance, evidence, charts, and recommendation edges.

- [ ] **Step 1: Write failing schema tests**

```python
class SchemaContractTest(unittest.TestCase):
    def test_exactly_twenty_roots(self):
        from scripts.biblio_brain.taxonomy import ROOT_GENRES
        self.assertEqual(20, len(ROOT_GENRES))
        self.assertEqual(20, len({g["slug"] for g in ROOT_GENRES}))

    def test_required_tables_exist(self):
        db = sqlite3.connect(":memory:")
        db.executescript(SCHEMA.read_text(encoding="utf-8"))
        names = {row[0] for row in db.execute(
            "select name from sqlite_master where type in ('table','view')"
        )}
        required = {
            "meta", "provider_run", "stage_work", "stage_edition", "stage_person",
            "work", "edition", "person", "work_contributor", "identifier",
            "series", "series_member", "genre", "work_genre", "facet",
            "facet_value", "work_facet", "award", "work_award",
            "chart_evidence", "chart_entry", "recommendation_edge",
            "field_provenance", "unresolved_candidate", "work_fts", "person_fts",
        }
        self.assertTrue(required.issubset(names), required - names)
```

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.SchemaContractTest -v
```

Expected: missing files.

- [ ] **Step 3: Implement schema and fixtures**

Use the approved 20 roots in this order: Fiction & Literature, Mystery & Thriller, Science Fiction, Fantasy, Romance, Horror, Historical Fiction, Young Adult, Children's, Biography & Memoir, History, Science & Nature, Technology & Computing, Business & Economics, Politics & Society, Philosophy & Religion, Health & Psychology, Arts & Culture, Travel & Adventure, Humor & Essays.

`work.variant_kind` accepts `canonical`, `revision`, `translation`, `omnibus`, `annotated`, `rewrite`, and `audio_adaptation`. The golden fixture includes duplicate titles, pseudonyms, fractional series positions, unresolved order, an omnibus, revised nonfiction, an abridged audiobook, and two distinct *Crime and Punishment* English translations: Constance Garnett and Richard Pevear/Larissa Volokhonsky.

- [ ] **Step 4: Verify**

```bat
python -m unittest tests.biblio_brain_test.SchemaContractTest -v
```

Expected: all schema tests pass.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/schema.sql scripts/biblio_brain/taxonomy.py tests/fixtures/biblio tests/biblio_brain_test.py
git commit -m "feat(biblio): define catalog schema and taxonomy"
```

---

### Task 2: Add provider-neutral models, normalization, and fixture publication

**Files:**
- Create: `scripts/biblio_brain/model.py`
- Create: `scripts/biblio_brain/normalize.py`
- Create: `scripts/biblio_brain/build_catalog.py`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces frozen `CandidatePerson`, `CandidateWork`, `CandidateEdition`, `CandidateSeriesMembership`, and `ChartEvidence`.
- Produces `normalize_title`, `normalize_person_name`, `normalize_isbn`, `normalize_language`, `parse_year`, and `stable_id`.
- Produces fixture CLI: `python scripts/biblio_brain/build_catalog.py --fixture tests/fixtures/biblio/golden_catalog.json --output <db>`.

- [ ] **Step 1: Write failing normalization and fixture-build tests**

```python
class FixtureBuildTest(unittest.TestCase):
    def test_fixture_build_is_english_only_and_atomic(self):
        run_fixture_build(self.output)
        db = sqlite3.connect(self.output)
        self.assertEqual(0, db.execute(
            "select count(*) from work where display_language <> 'eng'"
        ).fetchone()[0])
        self.assertEqual("ok", db.execute("pragma integrity_check").fetchone()[0])

    def test_material_translations_remain_distinct(self):
        run_fixture_build(self.output)
        db = sqlite3.connect(self.output)
        rows = db.execute(
            "select id from work where normalized_title='crime and punishment'"
        ).fetchall()
        self.assertEqual(2, len(rows))
```

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.FixtureBuildTest -v
```

Expected: imports fail.

- [ ] **Step 3: Implement minimal fixture publisher**

`stable_id(namespace, key)` returns `namespace + ":" + sha1(key.encode("utf-8")).hexdigest()[:20]`. Fixture mode applies `schema.sql`, inserts the golden records directly through the same model types later used by providers, populates FTS, runs integrity and English-policy checks, writes `<output>.building`, then publishes with `os.replace`. It does not call the network or require production dumps.

- [ ] **Step 4: Verify**

```bat
python -m unittest tests.biblio_brain_test.FixtureBuildTest -v
python scripts\biblio_brain\build_catalog.py --fixture tests\fixtures\biblio\golden_catalog.json --output data\biblio_catalog.db
```

Expected: tests pass and fixture DB is created.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/model.py scripts/biblio_brain/normalize.py scripts/biblio_brain/build_catalog.py tests/biblio_brain_test.py
git commit -m "feat(biblio): publish fixture catalog"
```

---

### Task 3: Add the minimal native catalog seam required for an eyes-on slice

**Files:**
- Create: `native/engine/BiblioCatalog.h`
- Create: `native/engine/BiblioCatalog.cpp`
- Create: `tests/biblio_catalog_slice_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp`

**Interfaces:**
- Produces:

```cpp
Q_INVOKABLE bool ready() const;
Q_INVOKABLE QVariantMap status() const;
Q_INVOKABLE QVariantList chart(const QString& family, const QString& genreSlug = {},
                               int limit = 10) const;
Q_INVOKABLE QVariantList genres() const;
Q_INVOKABLE QVariantMap work(const QString& workId) const;
```

- [ ] **Step 1: Write the failing C++ slice harness**

The harness opens the Task 2 fixture and asserts `ready()`, five chart-family keys, 20 genres, stable work IDs, and empty-return behavior for a missing DB.

- [ ] **Step 2: Build and verify failure**

```bat
cmake --build native/build-msvc --target biblio_catalog_slice_harness
```

Expected: target or class is missing.

- [ ] **Step 3: Implement the read-only seam**

Follow `ComicsCatalog`: unique connection name, `QSQLITE_OPEN_READONLY`, prepared statements, schema-version probe, empty values when unavailable, and safe handle removal. Register:

```cpp
auto *biblioCatalog =
    new BiblioCatalog(QStringLiteral("data/biblio_catalog.db"), &app);
engine.rootContext()->setContextProperty(
    QStringLiteral("BiblioCatalog"), biblioCatalog);
```

Do not implement search, author, series, or recommendations yet.

- [ ] **Step 4: Verify**

```bat
cmake --build native/build-msvc --target biblio_catalog_slice_harness colosseum
native\build-msvc\biblio_catalog_slice_harness.exe
```

Expected: `BIBLIO_CATALOG_SLICE_OK`, exit `0`, and successful app build.

- [ ] **Step 5: Commit**

```bash
git add native/engine/BiblioCatalog.* native/CMakeLists.txt native/main.cpp tests/biblio_catalog_slice_harness.cpp
git commit -m "feat(biblio): expose fixture catalog slice"
```

---

### Task 4: Render the first fixture-fed Biblio shelf

**Files:**
- Create: `qml/BiblioCatalogAdapter.js`
- Create: `qml/BiblioWorkTile.qml`
- Create: `qml/BiblioChartSwitcher.qml`
- Modify: `qml/BiblioWorld.qml`
- Create: `tests/biblio_world_slice_probe.qml`

**Interfaces:**
- Produces `workCard(row)` and a five-tab chart switcher.
- Preserves existing `bookRequested(var book)` navigation during the slice.
- Uses current Apple/static rows only when `BiblioCatalog.ready()` is false.

- [ ] **Step 1: Write the failing QML probe**

Inject a fake `BiblioCatalog` and assert:

- default family is `trending`;
- all five tabs request their exact family names;
- stable `workId` survives mapping;
- missing covers become typographic jackets;
- catalog rows render before the existing Continue and Collection sections;
- absent catalog uses the current Apple/static path;
- no acquisition or availability field participates.

- [ ] **Step 2: Run and verify failure**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_world_slice_probe.qml
```

Expected: missing adapter/components or catalog calls.

- [ ] **Step 3: Implement the vertical slice**

Replace only the existing `TrendingTop10` discovery block with the catalog chart switcher and `BiblioWorkTile` row. Leave Continue, Collection, GenreMosaic, search, and the current detail page untouched. The adapter maps native rows but never sorts. When the catalog is unavailable, retain today’s `BiblioApi.loadBiblio` and static rows byte-for-behavior.

- [ ] **Step 4: Verify by probe and eyes-on run**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_world_slice_probe.qml
set COLOSSEUM_OPEN_WORLD=Biblio
native\build-msvc\colosseum.exe qml\Main.qml
```

Expected: probe prints `BIBLIO_WORLD_SLICE_OK`. The fixture-backed public chart is visible and switchable. Deleting `data\biblio_catalog.db` restores the current Biblio screen.

**Review gate:** stop here for eyes-on approval before production ingestion. Reject or revise tile density, chart switching, fallback behavior, or hierarchy now, while the pipeline remains small.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioCatalogAdapter.js qml/BiblioWorkTile.qml qml/BiblioChartSwitcher.qml qml/BiblioWorld.qml tests/biblio_world_slice_probe.qml
git commit -m "feat(biblio): render fixture catalog shelf"
```

---

### Task 5: Stream the named Open Library and Goodreads production evidence

**Files:**
- Create: `scripts/biblio_brain/providers/__init__.py`
- Create: `scripts/biblio_brain/providers/open_library.py`
- Create: `scripts/biblio_brain/providers/goodreads.py`
- Create: `scripts/biblio_brain/providers/apple_charts.py`
- Create: `tests/fixtures/biblio/open_library_sample.txt`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces `iter_open_library_works`, `iter_open_library_editions`, and `iter_open_library_authors`.
- Produces `iter_goodreads_books`, `iter_goodreads_series`, and `iter_goodreads_similar`.
- Produces `fetch_apple_chart(country, genre_id, limit, opener)`.

- [ ] **Step 1: Write failing adapter tests**

Assert line-by-line reading, French-only edition rejection, translator preservation, fractional Goodreads series positions, historical rating fields, `similar_books` edges, and Apple RSS single-object/array normalization.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.ProviderAdapterTest -v
```

Expected: provider modules are missing.

- [ ] **Step 3: Implement adapters**

Open Library readers accept gzip or plain sample input and parse the final tab-separated JSON field. Edition language decides user-facing eligibility. Original language and translator data survive on valid English editions.

Goodreads reads `tools/biblio_series.db` rather than rescanning the raw file during every catalog build. It emits series, ratings count, average rating, shelf, and similarity evidence with source name `ucsd_goodreads_snapshot`.

Apple emits evidence with source name `apple_books_rss`, capture timestamp, chart family, country, genre ID, rank, and normalized title/author hints. It never creates work IDs.

- [ ] **Step 4: Verify adapters and real prerequisites**

```bat
python -m unittest tests.biblio_brain_test.ProviderAdapterTest -v
python scripts\biblio_brain\prepare_inputs.py --check --root D:\catalogs
python -c "import sqlite3; d=sqlite3.connect('tools/biblio_series.db'); print(d.execute('pragma integrity_check').fetchone()[0])"
```

Expected: tests pass; production checks print no missing inputs and `ok`.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/providers tests/fixtures/biblio/open_library_sample.txt tests/biblio_brain_test.py
git commit -m "feat(biblio): ingest named catalog evidence"
```

---

### Task 6: Resolve works, material variants, provenance, taxonomy, and series

**Files:**
- Create: `scripts/biblio_brain/overrides.json`
- Create: `scripts/biblio_brain/resolver.py`
- Create: `scripts/biblio_brain/subject_map.json`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces `Resolver.resolve(connection) -> ResolutionReport`.
- Produces `map_subjects(subjects) -> tuple[root_genres, facet_values]`.

- [ ] **Step 1: Write failing resolution tests**

Prove exact authority/ISBN merges, title-only non-merges, title-plus-author ordinary edition merges, distinct translator variants, omnibus coverage, null unresolved series positions, pseudonym overrides, and field-level provenance.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.IdentityResolverTest -v
```

Expected: resolver import failure.

- [ ] **Step 3: Implement deterministic rules**

Apply:

```text
1. checked-in force-split and force-merge overrides
2. exact unique authority identifier
3. exact normalized ISBN
4. normalized title + resolved primary author + compatible year
5. title + author + translator for translation lineage
6. separate work plus unresolved duplicate suspect
```

A material `variant_hint` never collapses into the canonical text without an override. Every selected field writes provider, provider ID, source timestamp, and confidence. Subject mapping is explicit and many-to-many; unknown values become reviewable unresolved facets rather than new root genres.

- [ ] **Step 4: Verify**

```bat
python -m unittest tests.biblio_brain_test.IdentityResolverTest -v
```

Expected: all resolution tests pass.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/overrides.json scripts/biblio_brain/resolver.py scripts/biblio_brain/subject_map.json tests/biblio_brain_test.py
git commit -m "feat(biblio): resolve catalog identities"
```

---

### Task 7: Compute honest V1 charts and supported recommendation edges

**Files:**
- Create: `scripts/biblio_brain/ranking.py`
- Create: `scripts/biblio_brain/seed/awards.csv`
- Create: `scripts/biblio_brain/seed/awards_sources.json`
- Modify: `tests/fixtures/biblio/chart_evidence.json`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Produces `compute_charts(connection, as_of)`.
- Produces `build_recommendation_edges(connection)`.
- Stores formula version `1`.

- [ ] **Step 1: Write failing evidence-honesty tests**

Assert:

- `trending` uses only Apple current rank and daily velocity;
- `new_notable` uses Apple rank plus publication/translation recency;
- `genre_popular` combines Apple genre rank with Goodreads historical audience evidence;
- `all_time` combines Goodreads historical evidence, official award evidence, chart persistence, and longevity;
- `critically_acclaimed` is award-led and contains no unnamed editorial field;
- every input row has a named source and timestamp/snapshot date;
- removing Apple evidence empties `trending` rather than silently substituting another meaning;
- `readers_also_liked` edges come only from Goodreads `similar_books`;
- identical evidence and `as_of` produce identical rows.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.RankingEvidenceTest -v
```

Expected: ranking module is missing.

- [ ] **Step 3: Implement formula version 1**

Normalize each source to `[0,1]` within its own window:

```python
TRENDING = 0.70 * apple_current_rank + 0.30 * apple_daily_velocity
GENRE_POPULAR = (
    0.45 * apple_genre_rank
    + 0.35 * goodreads_log_ratings
    + 0.20 * goodreads_bayesian_rating
)
ALL_TIME = (
    0.35 * goodreads_log_ratings
    + 0.25 * awards
    + 0.20 * historical_chart_presence
    + 0.20 * longevity
)
NEW_NOTABLE = (
    0.65 * apple_current_rank
    + 0.35 * publication_or_translation_recency
)
CRITICALLY_ACCLAIMED = (
    0.70 * awards
    + 0.30 * goodreads_bayesian_rating
)
```

If a component is absent, renormalize only among the named components present and lower confidence. Never synthesize `editorial`. Tie-break by score descending, confidence descending, normalized title ascending, and work ID ascending.

`awards.csv` columns are:

```csv
award_id,award_name,category,year,result,work_title,author,official_source_url,snapshot_date
```

Every row must reference a named official award source recorded in `awards_sources.json`. Recommendation edge codes are limited to the six codes in the V1 evidence contract.

- [ ] **Step 4: Verify**

```bat
python -m unittest tests.biblio_brain_test.RankingEvidenceTest -v
```

Expected: evidence, formula, edge-source, and determinism tests pass.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain/ranking.py scripts/biblio_brain/seed tests/fixtures/biblio/chart_evidence.json tests/biblio_brain_test.py
git commit -m "feat(biblio): rank with named evidence"
```

---

### Task 8: Complete validation, health reporting, and production publication

**Files:**
- Create: `scripts/biblio_brain/validate.py`
- Modify: `scripts/biblio_brain/build_catalog.py`
- Modify: `scripts/biblio_brain/README.md`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Production CLI:

```bat
python scripts\biblio_brain\build_catalog.py ^
  --open-library-dir D:\catalogs\openlibrary ^
  --goodreads-db tools\biblio_series.db ^
  --output data\biblio_catalog.db
```

- Refresh CLI:

```bat
python scripts\biblio_brain\build_catalog.py --refresh-charts --output data\biblio_catalog.db
```

- Produces `data/biblio_catalog.health.json`.
- Exits `0` on publish and `2` on rejection.

- [ ] **Step 1: Write failing publication tests**

Assert previous-output survival, cleanup of `.building`, integrity check, FTS parity, English-only user surfaces, 20 genres, valid foreign keys, no circular series, deterministic duplicates, unresolved-suspect reporting, provider freshness, evidence-source coverage, and formula version.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.BuildPublicationTest -v
```

Expected: validation is missing.

- [ ] **Step 3: Implement exact pipeline order**

```text
create <output>.building
apply schema
record provider runs and input snapshot dates
stream staging evidence
resolve identities and provenance
map taxonomy and facets
load official awards snapshot
build supported recommendation edges
compute formula-v1 charts
populate FTS
run validation and query smoke tests
write health JSON
fsync and close
publish with os.replace
```

Health output includes counts, unresolved records, duplicate suspects, stale inputs, chart coverage, evidence coverage by source, rejected award rows, applied overrides, schema version, formula version, and build timestamp.

- [ ] **Step 4: Verify fixture and production builds**

```bat
python -m unittest tests.biblio_brain_test -v
python scripts\biblio_brain\build_catalog.py --fixture tests\fixtures\biblio\golden_catalog.json --output data\biblio_catalog.db
python scripts\biblio_brain\build_catalog.py --open-library-dir D:\catalogs\openlibrary --goodreads-db tools\biblio_series.db --output data\biblio_catalog.db
```

Expected: all tests pass; both builds exit `0`; the production health report names Open Library, UCSD Goodreads, Apple Books RSS, and official award snapshots.

- [ ] **Step 5: Commit**

```bash
git add scripts/biblio_brain tests/biblio_brain_test.py
git commit -m "feat(biblio): publish validated production catalog"
```

---

### Task 9: Expand the native service to full catalog queries

**Files:**
- Modify: `native/engine/BiblioCatalog.h`
- Modify: `native/engine/BiblioCatalog.cpp`
- Create: `tests/biblio_catalog_engine_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**

```cpp
Q_INVOKABLE QVariantMap search(const QString& text, int limit = 30) const;
Q_INVOKABLE QVariantMap author(const QString& personId) const;
Q_INVOKABLE QVariantMap series(const QString& seriesId) const;
Q_INVOKABLE QVariantMap genre(const QString& slug, const QVariantMap& filters,
                              int limit = 60, int offset = 0) const;
Q_INVOKABLE QVariantList related(const QString& workId, int limit = 12) const;
Q_INVOKABLE QVariantList editions(const QString& workId) const;
Q_INVOKABLE bool reload();
```

- [ ] **Step 1: Write the failing engine harness**

Assert grouped search, exact-before-prefix-before-FTS ranking, author bibliography, series order and unresolved positions, AND-across-facet-groups filtering, pipeline chart order, relation reason/source fields, editions, status metadata, and invalid-reload preservation.

- [ ] **Step 2: Build and verify failure**

```bat
cmake --build native/build-msvc --target biblio_catalog_engine_harness
```

Expected: new methods are missing.

- [ ] **Step 3: Implement prepared read-only queries**

Return stable IDs and medium-shaped maps. `search()` returns `works`, `authors`, `series`, and `available`. `work()` includes contributors, variants, translations, genres, facets, awards, charts, editions, and related reasons. No provider merge or ranking occurs in C++.

- [ ] **Step 4: Verify**

```bat
cmake --build native/build-msvc --target biblio_catalog_engine_harness
native\build-msvc\biblio_catalog_engine_harness.exe
```

Expected: `BIBLIO_CATALOG_ENGINE_OK`, exit `0`.

- [ ] **Step 5: Commit**

```bash
git add native/engine/BiblioCatalog.* native/CMakeLists.txt tests/biblio_catalog_engine_harness.cpp
git commit -m "feat(biblio): expose full catalog queries"
```

---

### Task 10: Add separate personalized shelves using only supported edges

**Files:**
- Create: `native/engine/BiblioRecommendations.h`
- Create: `native/engine/BiblioRecommendations.cpp`
- Create: `tests/biblio_recommendations_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp`

**Interfaces:**

```cpp
Q_INVOKABLE QVariantList shelves(const QVariantList& activity,
                                 int perShelf = 12) const;
```

- [ ] **Step 1: Write the failing recommendation harness**

Activity produces `next_in_series`, `continue_author`, `because_you_read`, and `explore_nearby`. Assert personalized labels, visible evidence-backed reasons, consumed-work exclusion, cross-shelf deduplication, and byte-identical public charts before/after the call. Assert no unsupported reason code can surface.

- [ ] **Step 2: Build and verify failure**

```bat
cmake --build native/build-msvc --target biblio_recommendations_harness
```

Expected: class is missing.

- [ ] **Step 3: Implement shelf computation**

Priority is next in series, continue author, because you read, then explore nearby. Use catalog edges and supplied Progress/Collection activity only. `readers_also_liked` may appear only when the edge provenance is `ucsd_goodreads_snapshot`. Do not persist profiles, call live services, or alter public charts.

Register the service beside `BiblioCatalog`.

- [ ] **Step 4: Verify**

```bat
cmake --build native/build-msvc --target biblio_recommendations_harness colosseum
native\build-msvc\biblio_recommendations_harness.exe
```

Expected: `BIBLIO_RECOMMENDATIONS_OK`, exit `0`.

- [ ] **Step 5: Commit**

```bash
git add native/engine/BiblioRecommendations.* native/CMakeLists.txt native/main.cpp tests/biblio_recommendations_harness.cpp
git commit -m "feat(biblio): add evidence-backed personal shelves"
```

---

### Task 11: Deploy the catalog through the existing data vault

**Files:**
- Modify: `scripts/data_vault/publish_release.py`
- Modify: `scripts/data_vault/pull_data.py`
- Modify: `README.md`
- Modify: `tests/biblio_brain_test.py`

**Interfaces:**
- Adds `biblio_catalog.db` to release publishing and flat-host pulling.

- [ ] **Step 1: Add failing source contract**

Assert:

```python
ARTIFACTS == ["comics_catalog.db", "mal_catalog.db", "biblio_catalog.db"]
```

and the flat-host pull loop uses the same list.

- [ ] **Step 2: Run and verify failure**

```bat
python -m unittest tests.biblio_brain_test.VaultContractTest -v
```

Expected: Biblio artifact is absent.

- [ ] **Step 3: Update vault scripts and documentation**

Use one shared artifact list in each script. GitHub release mode continues downloading every release asset. Document input preparation, fixture preview, production bake, publish, pull, and the V1 evidence limitations.

- [ ] **Step 4: Verify**

```bat
python -m unittest tests.biblio_brain_test.VaultContractTest -v
```

Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add scripts/data_vault README.md tests/biblio_brain_test.py
git commit -m "build(biblio): distribute catalog artifact"
```

---

### Task 12: Complete world, genre, search, and personal discovery surfaces

**Files:**
- Create: `qml/BiblioSeriesTile.qml`
- Create: `qml/BiblioFacetBar.qml`
- Modify: `qml/BiblioWorld.qml`
- Modify: `qml/BiblioGenrePage.qml`
- Modify: `qml/BiblioGenreIndex.qml`
- Modify: `qml/BiblioSearch.qml`
- Create: `tests/biblio_catalog_surfaces_probe.qml`

**Interfaces:**
- Consumes full `BiblioCatalog` and `BiblioRecommendations`.
- Emits stable work, series, and author IDs.

- [ ] **Step 1: Write the failing QML surface probe**

Assert public charts remain stable when activity changes, personal shelves are separately labelled, the genre index has exactly 20 roots, facets preserve genre identity, search groups works/authors/series, edition variants do not duplicate canonical tiles, series use stacked cards, and absent DB retains existing Apple/static fallback.

- [ ] **Step 2: Run and verify failure**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_surfaces_probe.qml
```

Expected: full surfaces are not wired.

- [ ] **Step 3: Implement surfaces**

Extend the Task 4 slice rather than replacing it. Public charts appear first, structured genre discovery second, personal shelves third. Search queries local data first and only uses Apple fallback when the catalog is unavailable, not when local search returns zero matches. Genre pages show catalog counts, contextual rails, and selected facets. QML preserves native order.

- [ ] **Step 4: Verify**

```bat
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_surfaces_probe.qml
set COLOSSEUM_OPEN_WORLD=Biblio
native\build-msvc\colosseum.exe qml\Main.qml
```

Expected: probe prints `BIBLIO_CATALOG_SURFACES_OK`; eyes-on smoke covers chart tabs, a genre, facet changes, personal shelf labels, and grouped search.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioSeriesTile.qml qml/BiblioFacetBar.qml qml/BiblioWorld.qml qml/BiblioGenrePage.qml qml/BiblioGenreIndex.qml qml/BiblioSearch.qml tests/biblio_catalog_surfaces_probe.qml
git commit -m "feat(biblio): complete catalog discovery surfaces"
```

---

### Task 13: Complete work, author, series, enrichment, diagnostics, and final gates

**Files:**
- Modify: `qml/BiblioBook.qml`
- Modify: `qml/BiblioSeries.qml`
- Create: `qml/BiblioAuthor.qml`
- Modify: `qml/BiblioApi.js`
- Modify: `qml/Main.qml`
- Create: `tests/biblio_detail_navigation_probe.qml`
- Create: `tests/biblio_catalog_degraded_probe.qml`
- Create: `tests/biblio_catalog_performance_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `README.md`

**Interfaces:**
- Work pages open by `workId`, authors by `personId`, and series by `seriesId`.
- `BiblioApi.enrichCatalogWork(work, done)` fills only empty cover, description, rating, or recent publication fields.
- Acquisition continues receiving canonical title and primary author only after explicit user action.

- [ ] **Step 1: Write failing navigation, degradation, and performance tests**

Assert translation lineage, nested ordinary editions, variant links, awards, chart appearances, evidence-backed recommendation copy, series orders and omnibuses, author bibliography and pseudonyms, delayed acquisition, missing-cover jackets, separate no-results/unavailable copy, stale snapshot dates, and enrichment that never changes IDs or rank.

Performance fixture contains at least 100,000 works. Gates:

```text
startup/schema probe < 250 ms
exact title search p95 < 25 ms
genre first page p95 < 25 ms
chart retrieval p95 < 10 ms
work detail p95 < 15 ms
```

- [ ] **Step 2: Run and verify failure**

```bat
cmake --build native/build-msvc --target biblio_catalog_performance_harness
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_detail_navigation_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_degraded_probe.qml
```

Expected: targets/probes or required behavior are missing.

- [ ] **Step 3: Implement detail surfaces and acquisition boundary**

Preserve the current dust-jacket design. Metadata loads from `BiblioCatalog.work(workId)`. Catalog editions appear under `Editions and text variants`. Existing LibGen/Tankorent/AudioBookBay discovery moves behind an explicit `Get this book` expansion or acquisition button, so opening a work page performs no source search.

Add author and series loaders in `Main.qml`, stable-ID routing, and Escape ordering. `enrichCatalogWork` accepts an Apple result only when normalized title and primary author match and may fill only empty presentation fields.

- [ ] **Step 4: Run the complete verification matrix**

```bat
python -m unittest tests.biblio_inputs_test tests.biblio_brain_test -v
python scripts\biblio_brain\build_catalog.py --fixture tests\fixtures\biblio\golden_catalog.json --output data\biblio_catalog.db
cmake --build native/build-msvc --target biblio_catalog_slice_harness biblio_catalog_engine_harness biblio_recommendations_harness biblio_catalog_performance_harness colosseum
native\build-msvc\biblio_catalog_slice_harness.exe
native\build-msvc\biblio_catalog_engine_harness.exe
native\build-msvc\biblio_recommendations_harness.exe
native\build-msvc\biblio_catalog_performance_harness.exe data\biblio_catalog.db
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_world_slice_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_surfaces_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_detail_navigation_probe.qml
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\biblio_catalog_degraded_probe.qml
```

Expected: every command exits `0`, every probe prints its `_OK` marker, and `colosseum` builds successfully.

Then run one fixture eyes-on smoke and one production-DB smoke:

```bat
set COLOSSEUM_OPEN_WORLD=Biblio
native\build-msvc\colosseum.exe qml\Main.qml
```

Verify public chart source/date copy, 20 genres, facets, grouped search, work-author-series navigation, missing-cover fallback, and that no acquisition lookup starts before explicit intent.

- [ ] **Step 5: Commit**

```bash
git add qml/BiblioBook.qml qml/BiblioSeries.qml qml/BiblioAuthor.qml qml/BiblioApi.js qml/Main.qml tests/biblio_detail_navigation_probe.qml tests/biblio_catalog_degraded_probe.qml tests/biblio_catalog_performance_harness.cpp native/CMakeLists.txt README.md
git commit -m "feat(biblio): complete catalog experience"
```

---

## Execution Checkpoints

1. **Early visual checkpoint:** Tasks 0 to 4. Fixture DB, minimal native seam, and a real Biblio chart shelf visible with the old page as fallback.
2. **Production catalog checkpoint:** Tasks 5 to 8. Named data inputs, identity resolution, honest formula-v1 rankings, validation, and atomic publication.
3. **Full service checkpoint:** Tasks 9 to 11. Complete native query API, evidence-backed personalization, and data-vault distribution.
4. **Product completion checkpoint:** Tasks 12 to 13. Full discovery and detail surfaces, diagnostics, performance, and acquisition boundary.

The first eyes-on review occurs at Task 4, not Task 11. Production input preparation is owned by Task 0 and is a gate only for Task 5 onward.
