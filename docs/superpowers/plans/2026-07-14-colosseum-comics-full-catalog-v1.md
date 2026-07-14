# Colosseum Full Comics Catalog v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the full 688-series, 5,469-edition GCD catalog through Colosseum's lazy Tankoban shelf, with resumable GetComics-only enrichment and honest unavailable editions.

**Architecture:** The haven pipeline builds a lean canonical JSON object from the existing GCD stage, enriches it through an atomic checkpoint, and emits a compact `.pragma library` JavaScript artifact. Colosseum imports that artifact only inside the lazy `TankobanWorld.qml`, while the existing `ComicsDb.js`/`ComicDbLedger.qml` path renders all records and exposes acquisition only for verified GetComics posts.

**Tech Stack:** Python 3.12 (`sqlite3`, `urllib`, `unittest`), JSON, Qt 6/QML JavaScript, PowerShell test runners, CMake/MSVC 2022.

## Global Constraints

- Catalog is exactly the 688 ranked RCO series that resolve to GCD editions; ignore the 294 empty runs.
- Preserve all 5,469 editions, including unavailable editions.
- GetComics is the only v1 download source; torrent live search remains dormant.
- Ship `comics_db.gen.js`, not SQLite, and do not parse it from root `Main.qml` startup.
- Skip long per-edition descriptions.
- Use integer `font.pixelSize` values only.
- Do not stage or modify A2 Biblio/reader work or A5 universe/taskbar work.
- Because this is a shared dirty tree, do not create/switch worktrees or branches; stage exact owned paths and commit/push only after the complete gate is green.

---

### Task 1: Full ranked stage-1 builder

**Files:**
- Modify: `C:/Users/Suprabha/Desktop/Brotherhood/scripts/comics_brain/gcd_app_data.py`
- Delete after migration: `C:/Users/Suprabha/Desktop/Brotherhood/scripts/comics_brain/_build_full_catalog.py`
- Create: `C:/Users/Suprabha/Desktop/Brotherhood/scripts/comics_brain/tests/test_gcd_app_data.py`

**Interfaces:**
- Consumes: `rco_popularity.json`, `comics_seed_map.json`, `gcd_series_ids()`, and `editions_for()`.
- Produces: `select_ranked_series(ranked, edition_loader, identity_loader) -> list[dict]`, `catalog_series_id(ids) -> str`, `emit_catalog(db, json_path, js_path)`, and CLI options `--staging`, `--reuse-staging`, `--output`.

- [ ] **Step 1: Write failing selection tests**

```python
def test_select_ranked_series_drops_only_empty_and_keeps_all_editions():
    ranked = [
        {"rank": 1, "title": "One", "year": 2000, "slug": "One"},
        {"rank": 2, "title": "Empty", "year": 2001, "slug": "Empty"},
        {"rank": 3, "title": "Three", "year": 2002, "slug": "Three"},
    ]
    editions = {"One": [{"locg_comic_id": "11"}, {"locg_comic_id": "12"}],
                "Empty": [], "Three": [{"locg_comic_id": "31"}]}
    result = app.select_ranked_series(
        ranked,
        lambda row: editions[row["title"]],
        lambda row: [1000 + row["rank"]],
    )
    assert [row["rank"] for row in result] == [1, 3]
    assert [len(row["editions"]) for row in result] == [2, 1]
    assert [row["locg_id"] for row in result] == ["gcd-1001", "gcd-1003"]

def test_lean_catalog_removes_descriptions_without_dropping_records():
    result = app.lean_edition({"title": "Book", "description": "long", "available": False})
    assert result["title"] == "Book"
    assert "description" not in result
```

- [ ] **Step 2: Run the tests and verify RED**

Run:

```powershell
python -m unittest discover -s C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\tests -p test_gcd_app_data.py -v
```

Expected: failures naming missing `select_ranked_series`, `catalog_series_id`, or `lean_edition`.

- [ ] **Step 3: Implement the pure builder boundary**

```python
def catalog_series_id(series_ids):
    return "gcd-%s" % min(int(value) for value in series_ids)

def lean_edition(edition):
    keep = ("title", "format", "collects", "isbn", "pages", "published",
            "locg_comic_id", "slug", "cover", "available",
            "getcomics_post", "creators", "source")
    return {key: edition.get(key) for key in keep if key in edition}

def select_ranked_series(ranked, edition_loader, identity_loader):
    selected = []
    for row in ranked:
        editions = [lean_edition(e) for e in edition_loader(row)]
        if not editions:
            continue
        item = dict(row)
        item["locg_id"] = catalog_series_id(identity_loader(row))
        item["publisher"] = ""
        item["cover"] = ""
        item["editions"] = editions
        selected.append(item)
    return selected
```

Merge legacy seed metadata by rank only after the stable GCD identity is created; preserve the seed's real LOCG ID, publisher, and cover when present.

- [ ] **Step 4: Add atomic JSON/JS emission and staging reuse**

```python
def atomic_text(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    os.replace(tmp, path)

def emit_catalog(db, json_path, js_path):
    atomic_text(json_path, json.dumps(db, ensure_ascii=False, indent=2) + "\n")
    payload = json.dumps(db, ensure_ascii=False, separators=(",", ":"))
    atomic_text(js_path, "// GENERATED by gcd_app_data.py — do not edit by hand.\n"
                         ".pragma library\nvar data = " + payload + ";\n")
```

The CLI reads `_full_catalog.staging.json` under `--reuse-staging`; otherwise it queries the dump, writes staging atomically, and emits `comics_db.json`/`comics_db.gen.js`.

- [ ] **Step 5: Run stage-1 tests GREEN and build the real source artifact**

Run:

```powershell
python -m unittest discover -s C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\tests -p test_gcd_app_data.py -v
python C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\gcd_app_data.py --reuse-staging
```

Expected: tests pass; builder reports `688 series | 5469 editions`; source and generated files parse successfully.

### Task 2: Checkpointed GetComics enrichment

**Files:**
- Modify: `C:/Users/Suprabha/Desktop/Brotherhood/scripts/comics_brain/gcd_getcomics_enrich.py`
- Create: `C:/Users/Suprabha/Desktop/Brotherhood/scripts/comics_brain/tests/test_gcd_getcomics_enrich.py`
- Create at runtime: `C:/Users/Suprabha/Desktop/Brotherhood/scripts/comics_brain/comics_db.enrich.checkpoint.json`

**Interfaces:**
- Consumes: the stage-1 `comics_db.json`; injected `fetch_text(url)`, `sleep(seconds)`, and `checkpoint_writer(state)` in tests.
- Produces: `signed_download_present(html) -> bool`, `choose_post(series_title, edition_title, posts) -> dict|None`, `Checkpoint`, `enrich_catalog(db, checkpoint, client, emit, max_items=None)`, and the final JSON/JS pair.

- [ ] **Step 1: Write failing pure match and checkpoint tests**

```python
def test_only_signed_dls_is_available():
    assert enrich.signed_download_present('<a href="https://getcomics.org/dls/a:abc==">x</a>')
    assert not enrich.signed_download_present('<a href="https://getcomics.org/dls/a">x</a>')

def test_checkpoint_skips_completed_hit_and_completed_miss():
    state = enrich.Checkpoint({"version": 1, "heroes": {}, "editions": {
        "11": {"done": True, "cover": "x", "available": True},
        "12": {"done": True, "cover": None, "available": False},
    }})
    assert state.edition_done("11")
    assert state.edition_done("12")

def test_hero_phase_finishes_before_editions():
    calls = []
    client = FakeClient(calls)
    enrich.enrich_catalog(sample_db(), enrich.Checkpoint.empty(), client, lambda db, cp: None)
    first_edition = calls.index(("edition", "11"))
    assert all(kind == "hero" for kind, _ in calls[:first_edition])
```

- [ ] **Step 2: Run enrichment tests and verify RED**

Run:

```powershell
python -m unittest discover -s C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\tests -p test_gcd_getcomics_enrich.py -v
```

Expected: missing pure APIs fail before network code runs.

- [ ] **Step 3: Implement retrying HTTP and deterministic matching**

```python
class GetComicsClient:
    def __init__(self, opener=urllib.request.urlopen, sleep=time.sleep,
                 delay=0.5, attempts=4, backoff=1.5):
        self.opener = opener
        self.sleep = sleep
        self.delay = delay
        self.attempts = attempts
        self.backoff = backoff

    def get(self, url, timeout=30):
        for attempt in range(self.attempts):
            try:
                request = urllib.request.Request(url, headers=HEADERS)
                with self.opener(request, timeout=timeout) as response:
                    return response.read().decode("utf-8", "replace")
            except urllib.error.HTTPError as error:
                if error.code == 404:
                    return ""
                if error.code != 429 and error.code < 500:
                    raise
            except (OSError, TimeoutError):
                pass
            if attempt + 1 < self.attempts:
                self.sleep(self.backoff * (2 ** attempt))
        return ""
```

`choose_post` must require normalized series-token overlap and then maximize edition/query overlap. `attach_edition` fetches only the chosen post and sets `available` from `signed_download_present`.

- [ ] **Step 4: Implement checkpoint persistence and hero-first orchestration**

```python
class Checkpoint:
    @classmethod
    def empty(cls):
        return cls({"version": 1, "heroes": {}, "editions": {}})

    def edition_done(self, edition_id):
        return bool(self.data["editions"].get(str(edition_id), {}).get("done"))

def hero_cover(series, checkpoint_result):
    if checkpoint_result.get("cover"):
        return checkpoint_result["cover"]
    for edition in series["editions"]:
        if edition.get("cover"):
            return edition["cover"]
    return series.get("cover") or ""
```

Persist the checkpoint after every hero or edition result. Apply it to the in-memory catalog and atomically emit both artifacts after each completed series. Store misses with `done: true`.

- [ ] **Step 5: Prove interruption/resume and retry behavior GREEN**

Add a test that runs with `max_items=2`, reloads the serialized checkpoint, reruns, and asserts the first two IDs are never sent to the fake client again. Add a fake opener sequence `[HTTP 429, OSError, success]` and assert sleeps `[1.5, 3.0]`.

Run:

```powershell
python -m unittest discover -s C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\tests -p test_gcd_getcomics_enrich.py -v
```

Expected: all match, checkpoint, order, resume, and retry tests pass.

- [ ] **Step 6: Run the real foreground enrichment to completion**

Run:

```powershell
python C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\gcd_getcomics_enrich.py
```

Expected: the log completes heroes `688/688` before edition `1/5469`; restarts report skipped checkpointed records; final artifacts retain `688 series | 5469 editions`. Keep the process foreground and use the tool wait mechanism for progress.

### Task 3: Lazy catalog ingestion and honest ledger actions

**Files:**
- Modify: `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/qml/Main.qml`
- Modify: `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/qml/TankobanWorld.qml`
- Modify: `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/qml/ComicDbLedger.qml`
- Modify if identity tests require it: `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/qml/ComicsDb.js`
- Create: `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/tests/comics_catalog_logic_harness.qml`
- Create: `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/tests/test_comics_catalog_v1.ps1`

**Interfaces:**
- Consumes: `ComicsDbData.data`, `ComicsDb.setData`, `ComicsDb.rankedSeries`, and edition `available/getcomics_post`.
- Produces: lazy Tankoban log `ComicsDb: loaded 688 series`; ledger `canAcquire = hasSource || dlState === "done"`.

- [ ] **Step 1: Write the failing static and headless logic gates**

The PowerShell test must assert:

```powershell
Assert-NotContains $main 'import "comics_db.gen.js" as ComicsDbData'
Assert-NotContains $main 'ComicsDb.setData(ComicsDbData.data)'
Assert-Contains $world 'import "comics_db.gen.js" as ComicsDbData'
Assert-Contains $world 'ComicsDb.setData(ComicsDbData.data)'
Assert-NotContains $ledger 'downloadIssueTorrent'
Assert-Contains $ledger 'property bool   hasSource: !!ed.modelData.available && postUrl.length > 0'
```

The QML harness imports `ComicsDb.js` and `comics_db.gen.js`, calls `setData`, and exits nonzero unless `rankedSeries().length === 688`, every row has a route ID, and a known series has editions.

- [ ] **Step 2: Run the QML gate and verify RED**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\tests\test_comics_catalog_v1.ps1
```

Expected: failure because `Main.qml` owns the generated import and ledger still calls `downloadIssueTorrent`.

- [ ] **Step 3: Move ingestion into the lazy world**

Remove both comics DB imports and root `ComicsDb.setData` from `Main.qml`. In `TankobanWorld.qml` add:

```qml
import "comics_db.gen.js" as ComicsDbData

property var comicRows: []

Component.onCompleted: {
    var ok = ComicsDb.setData(ComicsDbData.data)
    comicRows = ok ? ComicsDb.rankedSeries() : Catalog.topComics
    if (ok) console.log("ComicsDb: loaded " + comicRows.length + " series")
    else console.warn("ComicsDb: ingest failed — using curated fallback")
    GcApi.explore(function(boxes) { /* preserve the existing callback body exactly */ })
}
```

Bind `TrendingTop10.items` and its click handler to `tanko.comicRows`.

- [ ] **Step 4: Remove unavailable acquisition behavior**

In `ComicDbLedger.qml`, define:

```qml
readonly property bool canAcquire: hasSource || dlState === "done"

function primary() {
    if (typeof Comics === "undefined" || !chId.length || !canAcquire) return
    if (dlState === "done") {
        ledger.readRequested(chId, String(ed.modelData.title || ""))
        return
    }
    if (inFlight) return
    ed.dlState = "queued"
    Comics.downloadIssue(chId, postUrl, ledger.gcTag, ledger.seriesTitle,
                         String(ed.modelData.title || ""), 0)
}
```

The download icon is visible only for `hasSource`, progress/read symbols remain state-driven, title hover and cursor use `canAcquire`, and unavailable rows remain fully opaque bibliographic records.

- [ ] **Step 5: Run the catalog logic gate GREEN**

Run the PowerShell gate again. Expected: static assertions pass and the offscreen QML harness prints `COMICS_CATALOG_OK 688` with exit 0.

### Task 4: Deploy the full generated artifact and exercise the lazy page

**Files:**
- Modify: `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/qml/comics_db.gen.js`

**Interfaces:**
- Consumes: the completed haven `comics_db.gen.js`.
- Produces: byte-identical app deployment.

- [ ] **Step 1: Deploy by a normal file copy**

Run:

```powershell
Copy-Item -LiteralPath C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\comics_db.gen.js -Destination C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\qml\comics_db.gen.js -Force
```

This is a bulk generated artifact, so it does not require hand editing.

- [ ] **Step 2: Verify source/deployed hashes and payload invariants**

Run:

```powershell
Get-FileHash C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\comics_db.gen.js
Get-FileHash C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\qml\comics_db.gen.js
python -m unittest discover -s C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\tests -v
```

Expected: hashes match and all Python tests pass. The tests parse both source artifacts and assert 688/5,469 plus truthful availability fields.

- [ ] **Step 3: Kill only the running Colosseum PID**

```powershell
$process = Get-Process colosseum -ErrorAction SilentlyContinue
if ($process) { $process | ForEach-Object { Stop-Process -Id $_.Id -Force } }
```

- [ ] **Step 4: Run the direct MSVC build**

Run:

```powershell
& C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat
```

Expected: exit 0 and terminal `BUILD_OK`.

- [ ] **Step 5: Run boot and lazy Tankoban smokes separately**

First launch without `COLOSSEUM_OPEN_WORLD` for a bounded smoke and assert the log does **not** contain `ComicsDb: loaded`. Then relaunch with:

```powershell
$env:QML_DISABLE_DISK_CACHE='1'
$env:QT_FORCE_STDERR_LOGGING='1'
$env:COLOSSEUM_OPEN_WORLD='Tankoban'
& C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc\Release\colosseum.exe C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\qml\Main.qml
```

Expected after Tankoban activation: `ComicsDb: loaded 688 series`, Loader creates successfully, no invalid property assignment, and the process remains alive until the bounded smoke terminates it by PID.

### Task 5: Review, eyes-on handoff, surgical commits, and continuity

**Files:**
- Review: both repositories' exact scoped diffs
- Create: a new Agent 1 recap under `C:/Users/Suprabha/.claude/recaps/agent-1/`
- Create: a durable memory under `C:/Users/Suprabha/Desktop/Brotherhood/memory/`
- Modify: the Brotherhood global `MEMORY.md` index selected by the memory-write skill

**Interfaces:**
- Consumes: the design Definition of Done and all verification logs.
- Produces: signed review, two surgical commits/pushes if both repos changed, recap, memory index, and Hemanth's eyes-on test instructions.

- [ ] **Step 1: Run the verification-before-completion gate**

Re-run, without relying on earlier output:

```powershell
python -m unittest discover -s C:\Users\Suprabha\Desktop\Brotherhood\scripts\comics_brain\tests -v
powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\tests\test_comics_catalog_v1.ps1
& C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat
```

Expected: all tests pass and build ends `BUILD_OK`.

- [ ] **Step 2: Review every Definition-of-Done item as MET/PARTIAL/NOT-MET**

Inspect only:

```powershell
git -C C:\Users\Suprabha\Desktop\Brotherhood diff -- scripts/comics_brain memory
git -C C:\Users\Suprabha\Desktop\Brotherhood\Colosseum diff -- qml/Main.qml qml/TankobanWorld.qml qml/ComicDbLedger.qml qml/ComicsDb.js qml/comics_db.gen.js tests/comics_catalog_logic_harness.qml tests/test_comics_catalog_v1.ps1 docs/superpowers/specs/2026-07-14-colosseum-comics-full-catalog-v1-design.md docs/superpowers/plans/2026-07-14-colosseum-comics-full-catalog-v1.md
```

Request changes for any missing count, startup import, torrent ledger action, checkpoint gap, or unrelated file.

- [ ] **Step 3: Hand Hemanth the eyes-on surface**

Launch Tankoban with disk cache disabled and tell Hemanth exactly what to inspect: scroll the ranked comics shelf beyond the original six, open an early and late-ranked series, confirm full edition continuity, confirm available rows download, and confirm unavailable rows show metadata without a download control.

- [ ] **Step 4: Stage exact owned files and commit/push each repository**

Use explicit `git add -- <file...>` lists. Never use `git add .`, `git add -A`, or directory staging. Verify `git diff --cached --name-only` before each commit. Commit the haven pipeline/memory separately from the Colosseum app/spec/plan/generated artifact, then push `master` only after both commits succeed locally.

- [ ] **Step 5: Write the session recap and memory index**

Use `brotherhood-session-recap` to export/trim the Codex transcript and create the Agent 1 handoff. Use `brotherhood-memory-write` for the durable full-catalog/lazy-load/checkpoint lesson and its one-line global index entry. The recap must name both commit hashes, both push results, exact tests/build evidence, remaining eyes-on request, and the next-wake prompt.
