# Colosseum Open Library Edition Covers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fill missing collected-edition thumbnails from exact Open Library ISBN records without changing GetComics download truth.

**Architecture:** Add a focused, batch-oriented Open Library provider to the comics build pipeline. Extend the existing resumable checkpoint with an independently versioned ISBN-cover namespace, then merge GetComics and Open Library results into the existing `cover` field before emitting the catalog.

**Tech Stack:** Python 3 standard library (`urllib`, `json`, `unittest`), existing atomic JSON/JavaScript emitter, Qt 6/QML catalog consumer, MSVC 2022 native build.

## Global Constraints

- GetComics remains the preferred cover source and the only v1 download source.
- Open Library is consulted only for editions whose GetComics result has no cover.
- No API key, account, login, LibraryThing, Apple Books, Google Books, torrent fallback, or bundled cover cache.
- Open Library changes only edition `cover`; it never changes `available` or `getcomics_post`.
- Hits and genuine misses are checkpointed; transport failures remain retryable.
- Preserve all unrelated shared-worktree changes and stage files surgically in each repository.

---

## File map

- Create `scripts/comics_brain/openlibrary_cover_enrich.py`: ISBN normalization, batched Books API client, response parsing, and missing-cover enrichment loop.
- Create `scripts/comics_brain/tests/test_openlibrary_cover_enrich.py`: pure provider and batch-enrichment tests.
- Modify `scripts/comics_brain/gcd_getcomics_enrich.py`: independent checkpoint namespace, merge precedence, orchestration, counters, and CLI knobs.
- Modify `scripts/comics_brain/tests/test_gcd_getcomics_enrich.py`: checkpoint isolation, precedence, resumability, and download-truth tests.
- Modify generated `scripts/comics_brain/comics_db.json` and `scripts/comics_brain/comics_db.gen.js` after a real rebuild.
- Modify generated `Colosseum/qml/comics_db.gen.js` by copying the verified builder artifact exactly.

### Task 1: Pure Open Library batch provider

**Files:**
- Create: `scripts/comics_brain/openlibrary_cover_enrich.py`
- Create: `scripts/comics_brain/tests/test_openlibrary_cover_enrich.py`

**Interfaces:**
- Produces: `normalize_isbn(value: object) -> str | None`
- Produces: `OpenLibraryClient.cover_results(isbns: list[str]) -> dict[str, dict]`
- Result shape: `{isbn: {"cover": str | None, "provider": "openlibrary"}}`
- Raises: `OpenLibraryRequestError` after bounded retry exhaustion; callers must not checkpoint that batch.

- [ ] **Step 1: Write failing normalization and response-parsing tests**

```python
def test_normalize_isbn_strips_separators_and_rejects_invalid_values(self):
    self.assertEqual(cover.normalize_isbn("978-1-58240-500-1"), "9781582405001")
    self.assertIsNone(cover.normalize_isbn("not-an-isbn"))

def test_multi_isbn_response_returns_large_cover_and_completed_miss(self):
    client = cover.OpenLibraryClient(opener=fake_opener, sleep=lambda _seconds: None)
    self.assertEqual(client.cover_results(["9781582405001"])["9781582405001"]["cover"],
                     "https://covers.openlibrary.org/b/id/12728566-L.jpg")
    self.assertIsNone(client.cover_results(["9780000000002"])["9780000000002"]["cover"])
```

- [ ] **Step 2: Run the focused tests and confirm they fail because the module does not exist**

Run: `python -m unittest scripts.comics_brain.tests.test_openlibrary_cover_enrich -v`

Expected: `ERROR` importing `openlibrary_cover_enrich`.

- [ ] **Step 3: Implement normalization, request construction, parsing, and bounded retry**

```python
OL_BOOKS_API = "https://openlibrary.org/api/books"

def normalize_isbn(value):
    cleaned = re.sub(r"[^0-9Xx]", "", str(value or ""))
    if len(cleaned) not in (10, 13):
        return None
    return cleaned.upper()

class OpenLibraryRequestError(RuntimeError):
    pass

class OpenLibraryClient:
    def cover_results(self, isbns):
        normalized = [isbn for isbn in (normalize_isbn(v) for v in isbns) if isbn]
        payload = self._request_json(normalized)
        return {
            isbn: {
                "cover": ((payload.get("ISBN:" + isbn) or {}).get("cover") or {}).get("large"),
                "provider": "openlibrary",
            }
            for isbn in normalized
        }
```

The request must use comma-separated `bibkeys`, `format=json`, and `jscmd=data`, with the existing browser-like user agent, bounded exponential backoff, and delay injection for deterministic tests.

- [ ] **Step 4: Run the provider tests**

Run: `python -m unittest scripts.comics_brain.tests.test_openlibrary_cover_enrich -v`

Expected: all provider tests pass, including multi-ISBN mapping, malformed record isolation, retry timing, and exhausted-failure exception behavior.

- [ ] **Step 5: Commit the provider slice in the Brotherhood repository**

```powershell
git add -- scripts/comics_brain/openlibrary_cover_enrich.py scripts/comics_brain/tests/test_openlibrary_cover_enrich.py
git commit -m "feat(comics): add Open Library cover provider"
```

### Task 2: Independent ISBN-cover checkpoint and merge precedence

**Files:**
- Modify: `scripts/comics_brain/gcd_getcomics_enrich.py`
- Modify: `scripts/comics_brain/tests/test_gcd_getcomics_enrich.py`

**Interfaces:**
- Produces: `ISBN_COVER_CHECKPOINT_VERSION = 1`
- Produces: `Checkpoint.isbn_cover_done(edition_id)`, `isbn_cover_result(edition_id)`, and `set_isbn_cover(edition_id, result)`
- Consumes: provider result shape from Task 1.

- [ ] **Step 1: Write failing checkpoint-isolation and merge tests**

```python
def test_isbn_cover_version_reset_preserves_getcomics_results(self):
    checkpoint = enrich.Checkpoint({
        "version": enrich.CHECKPOINT_VERSION,
        "hero_version": enrich.HERO_CHECKPOINT_VERSION,
        "isbn_cover_version": 0,
        "heroes": {},
        "editions": {"11": {"done": True, "cover": None, "available": False}},
        "isbn_covers": {"11": {"done": True, "cover": "stale"}},
    })
    self.assertTrue(checkpoint.edition_done("11"))
    self.assertFalse(checkpoint.isbn_cover_done("11"))

def test_apply_checkpoint_prefers_getcomics_then_open_library(self):
    checkpoint.set_edition("11", {"cover": "getcomics", "available": True,
                                  "getcomics_post": "https://getcomics.org/x"})
    checkpoint.set_isbn_cover("11", {"cover": "openlibrary", "provider": "openlibrary"})
    enrich.apply_checkpoint(db, checkpoint)
    self.assertEqual(db["series"][0]["editions"][0]["cover"], "getcomics")
```

- [ ] **Step 2: Run the focused checkpoint tests and confirm missing-method failures**

Run: `python -m unittest scripts.comics_brain.tests.test_gcd_getcomics_enrich.CatalogEnrichmentTests -v`

Expected: failures naming `isbn_cover_done` or `set_isbn_cover`.

- [ ] **Step 3: Add the namespace and merge without touching download fields**

```python
ISBN_COVER_CHECKPOINT_VERSION = 1

def set_isbn_cover(self, edition_id, result):
    result = result or {}
    self.data["isbn_covers"][str(edition_id)] = {
        "done": True,
        "cover": result.get("cover"),
        "provider": "openlibrary",
    }
```

In `apply_checkpoint`, derive `edition["cover"]` from `edition_result.cover` first and `isbn_cover_result.cover` second. Continue deriving `available` and `getcomics_post` exclusively from the GetComics edition result.

- [ ] **Step 4: Run the enrichment suite**

Run: `python -m unittest scripts.comics_brain.tests.test_gcd_getcomics_enrich -v`

Expected: all existing and new tests pass; existing resumability expectations remain unchanged.

- [ ] **Step 5: Commit the checkpoint slice in the Brotherhood repository**

```powershell
git add -- scripts/comics_brain/gcd_getcomics_enrich.py scripts/comics_brain/tests/test_gcd_getcomics_enrich.py
git commit -m "feat(comics): checkpoint ISBN cover fallback"
```

### Task 3: Orchestrate missing-cover batches after GetComics

**Files:**
- Modify: `scripts/comics_brain/openlibrary_cover_enrich.py`
- Modify: `scripts/comics_brain/gcd_getcomics_enrich.py`
- Modify: `scripts/comics_brain/tests/test_openlibrary_cover_enrich.py`
- Modify: `scripts/comics_brain/tests/test_gcd_getcomics_enrich.py`

**Interfaces:**
- Produces: `enrich_missing_covers(db, checkpoint, client, save_checkpoint, emit_catalog, batch_size=50, max_items=None, progress=None) -> tuple[bool, int]`
- The boolean is completion; the integer is newly processed edition count.

- [ ] **Step 1: Write failing orchestration tests**

```python
def test_only_blank_getcomics_covers_are_queried_and_checkpointed(self):
    completed, processed = cover.enrich_missing_covers(
        db, checkpoint, fake_client, save_calls.append, emitted.append, batch_size=50)
    self.assertTrue(completed)
    self.assertEqual(processed, 1)
    self.assertEqual(fake_client.calls, [["9780000000002"]])

def test_transport_failure_does_not_checkpoint_batch(self):
    fake_client.error = cover.OpenLibraryRequestError("offline")
    completed, processed = cover.enrich_missing_covers(
        db, checkpoint, fake_client, save_calls.append, emitted.append, batch_size=50)
    self.assertFalse(completed)
    self.assertEqual(processed, 0)
    self.assertFalse(checkpoint.isbn_cover_done("12"))
```

- [ ] **Step 2: Run the focused tests and confirm the orchestration function is absent**

Run: `python -m unittest scripts.comics_brain.tests.test_openlibrary_cover_enrich -v`

Expected: failure naming `enrich_missing_covers`.

- [ ] **Step 3: Implement batched filtering, atomic checkpoints, progress, and shared max-items accounting**

The candidate set must require a stable edition id, normalized ISBN, completed GetComics edition result, no GetComics cover, and no completed ISBN-cover result. Commit every successful response batch to the checkpoint, apply it to the catalog, and emit incrementally. A request exception returns incomplete without recording misses.

Integrate the pass after the GetComics edition loop. Subtract already processed hero/edition items from `max_items`, so bounded runs remain globally bounded across all three phases.

- [ ] **Step 4: Add CLI controls and reporting**

Add `--openlibrary-delay` (default `0.25`) and `--openlibrary-batch-size` (default `50`). Report phase `openlibrary` with cover/miss counts, and include total covered editions separately from downloadable editions in the final summary.

- [ ] **Step 5: Run all comics-brain unit tests**

Run: `python -m unittest discover -s scripts/comics_brain/tests -p "test_*.py" -v`

Expected: all tests pass.

- [ ] **Step 6: Commit the orchestration slice in the Brotherhood repository**

```powershell
git add -- scripts/comics_brain/openlibrary_cover_enrich.py scripts/comics_brain/gcd_getcomics_enrich.py scripts/comics_brain/tests/test_openlibrary_cover_enrich.py scripts/comics_brain/tests/test_gcd_getcomics_enrich.py
git commit -m "feat(comics): enrich blank covers by ISBN"
```

### Task 4: Real catalog rebuild, deployment, and verification

**Files:**
- Modify: `scripts/comics_brain/comics_db.json`
- Modify: `scripts/comics_brain/comics_db.gen.js`
- Modify: `Colosseum/qml/comics_db.gen.js`

- [ ] **Step 1: Record pre-rebuild invariants**

Run a read-only Python summary of series, editions, covered editions, downloadable editions, and the cover for ISBN `9781582405001`. Save the numeric output in the session log for before/after comparison.

- [ ] **Step 2: Run the resumable foreground rebuild**

Run: `python scripts/comics_brain/build_full_catalog.py --reuse-staging --delay 0.5`

Expected: completion with 688 series and 5,469 editions. If interrupted, rerun the same command; completed Open Library hits and misses must be skipped.

- [ ] **Step 3: Verify generated-data invariants**

Run a Python assertion script that checks:

```python
assert len(db["series"]) == 688
assert sum(len(row["editions"]) for row in db["series"]) == 5469
assert downloadable_after == downloadable_before
assert covered_after > covered_before
assert invincible_cover.startswith("https://covers.openlibrary.org/")
```

- [ ] **Step 4: Deploy the exact JavaScript artifact**

Run: `Copy-Item -LiteralPath scripts\comics_brain\comics_db.gen.js -Destination Colosseum\qml\comics_db.gen.js`

Verify both files have identical SHA-256 hashes.

- [ ] **Step 5: Run final Python and QML catalog logic tests**

Run: `python -m unittest discover -s scripts/comics_brain/tests -p "test_*.py" -v`

Run the existing headless catalog QML harnesses that validate `ComicsDb.js` ingestion and ranked catalog behavior. Expected: exit code 0 for each harness.

- [ ] **Step 6: Kill only the running Colosseum PID and build native**

Run `Get-Process colosseum -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id }`, then invoke `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat` directly from PowerShell.

Expected: `BUILD_OK`, exit code 0.

- [ ] **Step 7: Commit generated artifacts surgically**

In Brotherhood:

```powershell
git add -- scripts/comics_brain/comics_db.json scripts/comics_brain/comics_db.gen.js
git commit -m "data(comics): add Open Library edition covers"
```

In Colosseum:

```powershell
git add -- qml/comics_db.gen.js
git commit -m "data(comics): deploy Open Library edition covers"
```

- [ ] **Step 8: Cross-check and push both repositories**

Confirm each staged/committed diff contains only this plan's files, both repositories are based on current `origin/master`, and all verification evidence is fresh. Push Brotherhood `master` and Colosseum `master` only after both are green.
