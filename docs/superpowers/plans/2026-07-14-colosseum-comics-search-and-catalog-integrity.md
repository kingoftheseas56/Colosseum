# Comics Search and Catalog Integrity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Search all 688 GCD comic series globally, make their detail page opaque black, and rebuild ambiguous numbered editions without false GetComics downloads.

**Architecture:** Add a local GCD lane to the existing Tankoban search fan-out without touching `Main.qml`. Extend exact-ISBN enrichment into canonical edition metadata, then make GetComics matching fail closed against that identity and repair only the affected checkpoint cohort.

**Tech Stack:** Qt 6 QML/JavaScript, PowerShell test harnesses, Python 3 `unittest`, Open Library Books API, GetComics WordPress API, CMake/MSVC.

## Global Constraints

- Preserve every GCD bibliographic record; unavailable editions remain visible without a download action.
- Never attach an issue or unrelated franchise collection to a collected edition on number overlap alone.
- Preserve GCD `title`; expose exact-ISBN canonical text as `display_title`.
- Do not touch Claude's active Tankoban Mode files or unrelated A2/A5 changes.
- Keep `comics_db.gen.js` lazy-loaded through Tankoban World.

---

### Task 1: Search the local 688-series catalog

**Files:**
- Modify: `qml/WorldSearch.js`
- Create: `tests/world_search_comics_catalog_probe.qml`
- Create: `tests/test_world_search_comics_catalog.ps1`

**Interfaces:**
- Consumes: `ComicsDb.ready()` and `ComicsDb.rankedSeries()`.
- Produces: `searchCatalog(query)` results using SearchSurface's existing `{cover,title,subtitle,meta,group,data}` shape.

- [ ] Write a probe that injects representative catalog data and asserts normalized local matching, `data.locg`, and local-over-GetComics deduplication.
- [ ] Run the PowerShell/QML probe and confirm it fails because `searchCatalog` and the third fan-out lane do not exist.
- [ ] Import `ComicsDb.js`, implement the pure catalog search mapper, merge three lanes, and deduplicate normalized comics titles in favor of local GCD rows.
- [ ] Run the probe and existing WorldSearch tests to exit 0.

### Task 2: Cover the actual GCD series page with black

**Files:**
- Modify: `qml/ComicSeriesPage.qml`
- Modify: `tests/test_tankoban_series_background.ps1`

**Interfaces:**
- Produces: the exact Theatre black-fill/art/gradient token contract on every live Tankoban series page.

- [ ] Add `qml/ComicSeriesPage.qml` to the background regression file list and run it to observe failure.
- [ ] Replace the translucent GCD backdrop with Theatre's opaque black stack while preserving page content and chrome z-order.
- [ ] Run the regression and QML syntax harness to exit 0.

### Task 3: Capture canonical exact-ISBN metadata

**Files:**
- Modify: `scripts/comics_brain/openlibrary_cover_enrich.py`
- Modify: `scripts/comics_brain/gcd_getcomics_enrich.py`
- Modify: `scripts/comics_brain/tests/test_openlibrary_cover_enrich.py`
- Modify: `scripts/comics_brain/tests/test_gcd_getcomics_enrich.py`

**Interfaces:**
- Produces: Open Library result `{cover, provider, title, subtitle}` and edition field `display_title`.
- Preserves: raw GCD `edition["title"]`.

- [ ] Add failing tests for title/subtitle extraction, canonical display-title composition, and raw-title preservation.
- [ ] Run the focused Python tests and confirm the new assertions fail.
- [ ] Extend Open Library parsing and checkpoint application with canonical metadata.
- [ ] Run the focused tests to pass.

### Task 4: Reject false numbered GetComics attachments

**Files:**
- Modify: `scripts/comics_brain/gcd_getcomics_enrich.py`
- Modify: `scripts/comics_brain/tests/test_gcd_getcomics_enrich.py`

**Interfaces:**
- Consumes: `display_title || title` as the canonical query title.
- Produces: a GetComics result only when series ownership and canonical collection identity both match.

- [ ] Add failing regression cases for `Saga #1` versus Annihilation Saga, `Saga #3` versus Usagi Yojimbo Saga, and positive Saga Book/Volume matches.
- [ ] Run the focused test and confirm the false posts are currently selected.
- [ ] Tighten series-phrase ownership and pass canonical titles into `attach_edition` while retaining packaging-word normalization.
- [ ] Run the complete comics-brain unit suite to pass.

### Task 5: Repair and deploy the catalog

**Files:**
- Modify: `scripts/comics_brain/comics_db.enrich.checkpoint.json`
- Modify: `scripts/comics_brain/comics_db.json`
- Modify: `scripts/comics_brain/comics_db.gen.js`
- Modify: `Colosseum/qml/comics_db.gen.js`
- Modify: `qml/ComicDbLedger.qml`
- Create: `scripts/comics_brain/audit_catalog_integrity.py`

**Interfaces:**
- UI reads `edition.display_title || edition.title`.
- Audit exits nonzero for known false Saga URLs or ambiguous active matches that fail the canonical predicate.

- [ ] Add a failing catalog-integrity audit against the current six Saga false attachments.
- [ ] Invalidate the 322 `Series #N` checkpoint entries, batch-resolve exact ISBN metadata, and re-run GetComics enrichment for that cohort with polite pacing/retry.
- [ ] Render canonical names in the ledger, regenerate both catalog artifacts, and copy the generated JS into `Colosseum/qml/`.
- [ ] Run the audit and verify 688 series, 5,469 editions, and zero known false Saga attachments.

### Task 6: Full verification and surgical integration

**Files:**
- Verify only; stage only files named above.

- [ ] Run all comics-brain unit tests and QML/PowerShell regression tests.
- [ ] Compare source and deployed generated catalog hashes.
- [ ] Kill any running `colosseum.exe` by PID, then run `native/build-msvc.bat` directly and require exit 0 plus `BUILD_OK`.
- [ ] Inspect scoped diffs and confirm Claude/A2/A5 files are absent.
- [ ] Commit the outer pipeline and nested Colosseum changes surgically with `[Agent 1 (Codex), comics]` attribution, then push both repositories if their remotes are configured.

