# Tankoban Mode Migration (Phase 1) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the volume-first (tankoban) surface the permanent manga experience for every series with a complete, real volume→chapter mapping from Comick (DB-first, live-scrape on miss), and an honest flat WeebCentral chapter list for every series without one — no toggle, no MangaDex, no interpolation, ever.

**Architecture:** A new `ComickCatalogClient` (C++, `native/engine/`) replaces `MangaDexCatalogClient` behind the exact same `catalogReady`/`catalogFailed` signal contract, so `MangaEngine` and QML change minimally. It reads our GitHub volume DB by WeebCentral ULID first, live-scrapes Comick (all languages) on a miss, and applies a **completeness gate**: only a contiguous, quirk-free volume run qualifies. `MangaVolumes.js` loses its interpolation/anchor-repair entirely — the gate guarantees complete ranges, so unknown ranges no longer exist downstream. `MangaSeries.qml` derives tankoban mode from the gate verdict and deletes the toggle.

**Tech Stack:** Qt 6 / C++ (QNetworkAccessManager, no new deps), QML/JS, Python 3.11 + pytest (volume-db repo), MSVC build via `native/build-msvc.bat`.

**Source specs:**
- Locked design: `Brotherhood/docs/superpowers/specs/2026-07-25-colosseum-manga-volume-first-mangafire-db.md`
- Phase-0 plan (executed → `kingoftheseas56/colosseum-volume-db`): `Brotherhood/docs/superpowers/plans/2026-07-25-colosseum-manga-volume-db-phase0.md`

---

## Verified facts (probed live 2026-07-29 — code against these, do not re-derive)

1. **Comick is reachable, token-free**, but every request MUST send a browser-style `User-Agent`
   (bare clients get 403). Endpoints:
   - `GET https://api.comick.dev/v1.0/search?q=<title>&limit=8` → `[{hid, slug, title, md_titles:[{title}], …}]`
   - `GET https://api.comick.dev/comic/<hid>/chapters?limit=100000&chap-order=1` → `{chapters:[{chap, vol, lang, …}], total}`
2. **Drop the `lang=en` filter.** English scanlation rows are often untagged, but other-language
   rows carry complete volume tags. My Hero Academia: `lang=en` → 3 usable volumes; **all
   languages → volumes 1–42 in one unbroken run**, ranges matching Shueisha (vol 1 = ch 1–7).
   Chapter numbers are canonical across translations, so ranges transfer.
3. **Multi-language tags carry minor noise** (MHA: 5 pairs of adjacent ranges overlap by ~1
   chapter from stray tags) → a **majority vote per chapter number** resolves it deterministically.
4. **Census of the top 100 manga** (`Brotherhood/scripts/comick_volume_census.py` + cached
   payloads in `Brotherhood/scripts/comick_cache/`): with en-only data, 72/99 usable, 27/99
   broken (holes/collapse). All-language + gate is expected to raise this; MHA is proven fixed.
5. **The current DB records** (`~/Desktop/colosseum-volume-db/db/*.json`, 10 flagships) were
   built en-only: One Piece has mid-run holes (82 vols spanning 1–109, NOT contiguous),
   Vagabond starts at vol 25. Both must be rebuilt all-language before the app reads them.
6. **WeebCentral's chapter list contains zero images** (probed: 12MB HTML, 0 `<img>`).
   The spec's "volume cover = first chapter's WeebCentral thumbnail" is impossible — `ChapterInfo`
   (`native/engine/MangaResult.h:18`) has no thumbnail field and there is nothing to extract.
   **Cover doctrine for this plan:** undownloaded volume = the existing numbered placeholder
   (`MangaTankobanLibrary.qml` already renders it); downloaded volume = its own first page
   (`MangaVolumeIndex` already publishes this). `cover` is always `""` from the new client.
   No network cover fetch, no MangaDex, no Comick covers.
7. **The DB raw-CDN base**: records live under `db/` in the repo. Read path:
   `https://raw.githubusercontent.com/kingoftheseas56/colosseum-volume-db/main/db/<wc-ulid>.json`
   (Task 1 verifies this URL live and fixes the plan if the path differs).

## Spec deltas (rulings since the spec locked — the spec is amended by these)

| Spec said | This plan does | Why |
|---|---|---|
| Every series gets the volume surface; degrade = "Latest chapters" | **Completeness gate**: only a contiguous quirk-free run qualifies; everything else = flat WeebCentral chapter list | Hemanth's ruling 2026-07-29: "we apply tankoban mode only to titles with the full volume list from comick … that is our compromise." |
| `MangaVolumes.js` keeps interpolation between anchors | Interpolation and anchor-repair are **deleted** | Sparse anchors + interpolation = invented boundaries — the exact thing Hemanth rejected ("we don't compromise like that"). The gate makes them unreachable anyway; dead code lies. |
| Volume cover = first chapter's WC thumbnail | Placeholder → own first page when downloaded | WC has no chapter thumbnails (verified fact 6). |
| Live-scrape `lang=en` | Live-scrape **all languages** + majority dedupe | Verified fact 2/3 — en-only is what broke MHA. |
| Oddball numbering normalized in Phase 1 | Quirk-flagged titles **fail the gate** (chapter list) in v1 | Smallest honest version; normalization is a follow-up, not a blocker. Berserk falls back to chapters until then. |

## File structure

**Repo `colosseum-volume-db`** (`~/Desktop/colosseum-volume-db`, own git remote):
- Modify: `comick_volume_db/comick_client.py` — drop `lang=en` from `fetch_chapters`
- Modify: `comick_volume_db/volume_builder.py` — add `majority_assign()`, `gate()`; group from majority map
- Modify: `comick_volume_db/record.py` — record gains `"qualified": bool` + `"gateReason": str`
- Modify: `comick_volume_db/seeds.json` — add My Hero Academia
- Test: `tests/test_volume_builder.py` (extend), `tests/fixtures/mha_all_lang_pairs.json` (new, trimmed)
- Regenerate: `db/*.json`

**Repo `Colosseum`** (branch `agent1/tankoban-migration` off `master`):
- Create: `native/engine/ComickVolumeGrouper.h/.cpp` — pure logic: majority-assign, group, quirk, gate
- Create: `native/engine/ComickCatalogClient.h/.cpp` — DB fetch → live-scrape fallback → gate → emit
- Create: `tests/comick_volume_grouper_harness.cpp`, `tests/comick_catalog_parse_harness.cpp`
- Create: `tests/test_tankoban_migration_p0.ps1` — grep contracts
- Modify: `native/MangaEngine.h` — swap client, `volumes(seriesId, title)`
- Modify: `native/CMakeLists.txt` — add new files/harnesses, remove MangaDex entries (**SHARED FILE — declare on `Brotherhood/agents/chat.md` first**)
- Modify: `qml/MangaVolumes.js` — delete interpolation; trust ranges
- Modify: `qml/MangaSeries.qml` — derived mode, toggle deletion, latest-chapters bucket
- Delete: `native/engine/MangaDexCatalogClient.h/.cpp`, `tests/mangadex_volume_fold_harness.cpp`

---

### Task 0: Branch + governance declaration

**Files:**
- Modify: `C:\Users\Suprabha\Desktop\Brotherhood\agents\chat.md` (append)

- [ ] **Step 1: Branch off current master (main tree, no worktree)**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum
git fetch origin && git checkout master && git pull --ff-only origin master
git checkout -b agent1/tankoban-migration
```
Expected: branch created at `9141bc7` or later.

- [ ] **Step 2: Declare the shared-file edit on chat.md**

Append to `Brotherhood/agents/chat.md`:

```markdown
[Agent 1 (ZCode), comics] SHARED-FILE DECLARATION — `Colosseum/native/CMakeLists.txt`, tankoban-migration arc (branch `agent1/tankoban-migration`). Scoped edits only: (1) ADD `engine/ComickVolumeGrouper.{h,cpp}` + `engine/ComickCatalogClient.{h,cpp}` to the app target's source list beside the other engine files; (2) ADD two pure-logic harness targets (`comick_volume_grouper_harness`, `comick_catalog_parse_harness`) following the existing harness pattern; (3) REMOVE `engine/MangaDexCatalogClient.{h,cpp}` from the app target and DELETE the `mangadex_volume_fold_harness` target (client retires with this arc). No other targets touched — player2/A4 blocks untouched.
```

- [ ] **Step 3: Commit the declaration (Brotherhood repo, explicit pathspec)**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood
git add agents/chat.md && git commit -m "[Agent 1 (ZCode), comics] chat: declare CMakeLists edits for tankoban-migration" -- agents/chat.md
git push
```

---

### Task 1: Volume DB — all-language scrape, majority dedupe, gate, rebuild

All paths relative to `C:\Users\Suprabha\Desktop\colosseum-volume-db`.

- [ ] **Step 1: Trim the recorded MHA all-language payload into a fixture**

The full payload already exists at `Brotherhood/scripts/mha_all.json` (recorded live 2026-07-29).

```bash
cd /c/Users/Suprabha/Desktop/colosseum-volume-db
python -c "
import json
src = json.load(open(r'C:\Users\Suprabha\Desktop\Brotherhood\scripts\mha_all.json', encoding='utf-8'))
pairs = [{'chap': c.get('chap'), 'vol': c.get('vol')} for c in src['chapters']]
json.dump({'chapters': pairs}, open('tests/fixtures/mha_all_lang_pairs.json', 'w'), indent=0)
print(len(pairs), 'pairs')
"
```
Expected: `2714 pairs`.

- [ ] **Step 2: Write the failing tests for `majority_assign` and `gate`**

Append to `tests/test_volume_builder.py`:

```python
import json
import os

from comick_volume_db.volume_builder import (
    group_volumes, majority_assign, gate, numbering_is_oddball)

FIX = os.path.join(os.path.dirname(__file__), "fixtures")


def _mha_chapters():
    with open(os.path.join(FIX, "mha_all_lang_pairs.json"), encoding="utf-8") as f:
        return json.load(f)["chapters"]


def test_majority_assign_resolves_stray_tags():
    # ch 7: two rows say vol 1, one stray row says vol 2 -> majority wins
    chapters = [
        {"chap": "7", "vol": "1"}, {"chap": "7", "vol": "1"}, {"chap": "7", "vol": "2"},
        {"chap": "8", "vol": "2"},
    ]
    assign = majority_assign(chapters)
    assert assign[7.0] == 1
    assert assign[8.0] == 2


def test_majority_assign_tie_takes_smaller_volume():
    chapters = [{"chap": "7", "vol": "1"}, {"chap": "7", "vol": "2"}]
    assert majority_assign(chapters)[7.0] == 1


def test_group_from_majority_mha_all_language_is_complete():
    chapters = _mha_chapters()
    vols = group_volumes(chapters)
    numbers = [v["number"] for v in vols]
    assert numbers == list(range(1, 43))          # 1..42, unbroken
    assert vols[0]["chapterStart"] == "1"          # vol 1 starts at ch 1


def test_gate_accepts_contiguous_run_from_1():
    vols = [{"number": n, "chapterStart": "1", "chapterEnd": "9"} for n in range(1, 43)]
    ok, reason = gate(vols, numbering_quirk=False)
    assert ok, reason


def test_gate_accepts_volume_zero_start():
    vols = [{"number": n, "chapterStart": "1", "chapterEnd": "9"} for n in range(0, 13)]
    ok, _ = gate(vols, numbering_quirk=False)
    assert ok                                       # Death Note: vol 0..12


def test_gate_rejects_mid_run_gap():
    vols = [{"number": n, "chapterStart": "1", "chapterEnd": "9"} for n in (1, 19, 38)]
    ok, reason = gate(vols, numbering_quirk=False)
    assert not ok and "gap" in reason               # en-only MHA shape


def test_gate_rejects_late_start():
    vols = [{"number": n, "chapterStart": "1", "chapterEnd": "9"} for n in range(25, 39)]
    ok, reason = gate(vols, numbering_quirk=False)
    assert not ok                                   # Vagabond record shape (starts at 25)


def test_gate_rejects_quirk_and_empty():
    assert not gate([{"number": 1, "chapterStart": "1", "chapterEnd": "9"}],
                    numbering_quirk=True)[0]        # Berserk-class
    assert not gate([], numbering_quirk=False)[0]
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
python -m pytest tests/test_volume_builder.py -q
```
Expected: FAIL — `ImportError: cannot import name 'majority_assign'`.

- [ ] **Step 4: Implement `majority_assign` and `gate`; group from the majority map**

In `comick_volume_db/volume_builder.py`, replace `group_volumes` and add the two functions
(keep `_to_num`, `_fmt`, `numbering_is_oddball` unchanged):

```python
def majority_assign(chapters):
    """{chapter_number(float): volume(int)} — each chapter goes to the volume the most
    rows voted for; ties break to the SMALLER volume (earlier book claims the boundary).
    Rows missing chap or vol don't vote."""
    votes = {}
    for ch in chapters:
        vnum = _to_num(ch.get("vol"))
        cnum = _to_num(ch.get("chap"))
        if vnum is None or cnum is None:
            continue
        per = votes.setdefault(cnum, {})
        per[int(vnum)] = per.get(int(vnum), 0) + 1
    return {c: min(v for v, n in per.items() if n == max(per.values()))
            for c, per in votes.items()}


def group_volumes(chapters):
    assign = majority_assign(chapters)
    buckets = {}
    for cnum, vnum in assign.items():
        buckets.setdefault(vnum, set()).add(cnum)
    vols = []
    for vnum in sorted(buckets):
        chaps = sorted(buckets[vnum])
        vols.append({
            "number": vnum,
            "chapterStart": _fmt(chaps[0]),
            "chapterEnd": _fmt(chaps[-1]),
        })
    return vols


def gate(volumes, numbering_quirk):
    """(qualified, reason). Qualified = the mapped volumes are a complete, honest shelf:
    at least one volume, integer numbers in one unbroken run starting at 0 or 1, and no
    numbering quirk. Anything else -> the app shows the flat chapter list instead.
    NEVER soften this into estimation — sparse anchors + interpolation invents book
    boundaries, which Hemanth explicitly rejected."""
    if numbering_quirk:
        return False, "numbering quirk (fractional chapter origin)"
    if not volumes:
        return False, "no mapped volumes"
    nums = [v["number"] for v in volumes]
    if nums[0] not in (0, 1):
        return False, "first mapped volume is %d, not 0/1" % nums[0]
    for a, b in zip(nums, nums[1:]):
        if b != a + 1:
            return False, "gap after volume %d" % a
    return True, ""
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
python -m pytest tests/test_volume_builder.py -q
```
Expected: all PASS (pre-existing tests too — if an old test asserted duplicate-row behavior
that majority voting changes, update that test's fixture expectation and say so in the commit).

- [ ] **Step 6: Drop the language filter in the client; carry the gate into the record**

`comick_volume_db/comick_client.py` — in `fetch_chapters`, remove the `lang=en` query
parameter (keep `limit=100000&chap-order=1` and the browser `User-Agent`).

`comick_volume_db/record.py` — the assembled record gains two fields from the builder:

```python
qualified, gate_reason = gate(volumes, numbering_quirk)
record["qualified"] = qualified
record["gateReason"] = gate_reason
```
(match the existing assembly style; `volumes` stays in the record either way so the data
is inspectable even when it fails the gate).

- [ ] **Step 7: Add My Hero Academia to seeds and rebuild the whole DB**

Append `"My Hero Academia"` to `comick_volume_db/seeds.json` in the same format as the
existing entries. Then:

```bash
python -m comick_volume_db.build_db 2>&1 | tee rebuild.log
```
Expected: one line per seed, no tracebacks. Comick rate-limits — the runner's existing
pacing applies; on a 429, wait and rerun (it should be idempotent per record).

- [ ] **Step 8: Verify the rebuilt records against known truth**

```bash
python -c "
import json, glob
for f in sorted(glob.glob('db/*.json')):
    d = json.load(open(f, encoding='utf-8'))
    nums = [v['number'] for v in d['volumes']]
    contig = nums and nums[0] in (0,1) and all(b==a+1 for a,b in zip(nums,nums[1:]))
    print(f\"{d['seriesTitle'][:24]:<24} vols={len(nums):>3} first={nums[0] if nums else '-'} last={nums[-1] if nums else '-'} contiguous={contig} qualified={d.get('qualified')}\")
"
```
Expected (hard acceptance):
- Bleach: 74 vols, 1..74, qualified=True (unchanged from en-only — regression check)
- **My Hero Academia: 42 vols, 1..42, qualified=True** (the title that started this arc)
- **One Piece: contiguous from 1, qualified=True** (was 82-with-holes en-only). If all-language
  data still leaves holes, One Piece ships `qualified: false` — report the number honestly,
  do NOT hand-patch the record.
- Berserk: qualified=False, gateReason mentions quirk (expected v1 behavior)
- Vagabond: report what all-language gives; if still starting past vol 1 → qualified=False.

- [ ] **Step 9: Commit + push the DB repo, then verify the raw CDN read**

```bash
cd /c/Users/Suprabha/Desktop/colosseum-volume-db
git add -A && git commit -m "feat(volume-db): all-language scrape + majority dedupe + completeness gate; rebuild 10 flagships + MHA"
git push
# find MHA's ULID and prove the app's read path works unauthenticated:
python -c "
import json, glob
for f in glob.glob('db/*.json'):
    d = json.load(open(f, encoding='utf-8'))
    if 'Hero' in d['seriesTitle']: print(f.split('\\\\')[-1].split('/')[-1])
"
curl -s -o /dev/null -w "%{http_code}\n" "https://raw.githubusercontent.com/kingoftheseas56/colosseum-volume-db/main/db/<ULID-from-above>.json"
```
Expected: `200`. **If the repo layout differs (no `db/` prefix), record the real URL — Task 3
codes against whatever this step proves.** CDN propagation can lag a push by a minute or two.

---

### Task 2: C++ pure logic — `ComickVolumeGrouper` + harness

**Files:**
- Create: `native/engine/ComickVolumeGrouper.h`, `native/engine/ComickVolumeGrouper.cpp`
- Create: `tests/comick_volume_grouper_harness.cpp`
- Modify: `native/CMakeLists.txt` (declared in Task 0)

- [ ] **Step 1: Write the header (the contract)**

`native/engine/ComickVolumeGrouper.h`:

```cpp
// ComickVolumeGrouper.h — pure logic mirrored 1:1 from the Python reference
// (colosseum-volume-db/comick_volume_db/volume_builder.py). Comick chapter rows
// -> majority-voted chapter->volume assignment -> ordered volume ranges -> the
// completeness gate. This is the live-scrape core; the Python is the batch core.
// If the algorithms ever diverge, the DB and the live path disagree — keep them
// mirrored, test-for-test.
//
// The gate is doctrine, not tuning: a series qualifies for tankoban mode ONLY
// when its mapped volumes form one unbroken integer run starting at 0 or 1 with
// no numbering quirk. Everything else falls back to the flat chapter list.
// NEVER add interpolation/estimation here — rejected by Hemanth, permanently.

#pragma once

#include <QHash>
#include <QList>
#include <QString>

namespace tankoban::manga::comick {

struct ChapterRow {           // one Comick chapter row, any language
    double chap = -1.0;       // parsed "chap"; < 0 = missing
    double vol = -1.0;        // parsed "vol";  < 0 = missing
};

struct VolumeRange {
    int number = 0;
    QString chapterStart;     // "7" or "110.5" — same formatting law as Python _fmt
    QString chapterEnd;
};

struct GateVerdict {
    bool qualified = false;
    QString reason;
};

// {chapterNumber -> volume} by majority vote; ties -> smaller volume.
// Exposed for the harness.
QHash<double, int> majorityAssign(const QList<ChapterRow>& rows);

// Ascending volume ranges from the majority assignment.
QList<VolumeRange> groupVolumes(const QList<ChapterRow>& rows);

// True when the smallest chapter number is fractional (Berserk 0.01-class).
bool numberingIsOddball(const QList<ChapterRow>& rows);

GateVerdict gateVolumes(const QList<VolumeRange>& vols, bool numberingQuirk);

} // namespace tankoban::manga::comick
```

- [ ] **Step 2: Write the failing harness**

`tests/comick_volume_grouper_harness.cpp` (repo harness style: plain `main`, assert-and-report,
no gtest — mirror `tests/mangadex_volume_fold_harness.cpp`'s structure):

```cpp
// Pure-logic contract for the Comick grouping + completeness gate.
// Mirrors colosseum-volume-db tests/test_volume_builder.py case-for-case.

#include "engine/ComickVolumeGrouper.h"

#include <cstdio>
#include <initializer_list>
#include <utility>

using namespace tankoban::manga::comick;

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS  %s\n", name); } \
    else { std::printf("FAIL  %s\n", name); ++failures; } } while (0)

static QList<ChapterRow> rows(std::initializer_list<std::pair<double, double>> list)
{
    QList<ChapterRow> out;
    for (const auto& p : list) out.append(ChapterRow{p.first, p.second}); // {chap, vol}
    return out;
}

int main()
{
    { // majority vote resolves stray tags; ties take the smaller volume
        const auto a = majorityAssign(rows({{7,1},{7,1},{7,2},{8,2}}));
        CHECK(a.value(7.0) == 1 && a.value(8.0) == 2, "majority: stray tag loses");
        const auto t = majorityAssign(rows({{7,1},{7,2}}));
        CHECK(t.value(7.0) == 1, "majority: tie -> smaller volume");
    }
    { // grouping: ranges from assigned chapters, ascending, _fmt law
        const auto v = groupVolumes(rows({{1,1},{2,1},{7,1},{8,2},{17.5,2}}));
        CHECK(v.size() == 2, "group: two volumes");
        CHECK(v[0].number == 1 && v[0].chapterStart == "1" && v[0].chapterEnd == "7",
              "group: vol1 = 1-7");
        CHECK(v[1].chapterEnd == "17.5", "group: fractional end keeps its form");
    }
    { // gate: contiguous from 1 or 0 passes; gaps, late starts, quirk, empty fail
        QList<VolumeRange> run;
        for (int n = 1; n <= 42; ++n) run.append({n, "1", "9"});
        CHECK(gateVolumes(run, false).qualified, "gate: 1..42 passes");
        QList<VolumeRange> zero;
        for (int n = 0; n <= 12; ++n) zero.append({n, "1", "9"});
        CHECK(gateVolumes(zero, false).qualified, "gate: 0..12 (Death Note) passes");
        QList<VolumeRange> sparse{{1,"1","9"},{19,"1","9"},{38,"1","9"}};
        CHECK(!gateVolumes(sparse, false).qualified, "gate: en-only MHA shape fails");
        QList<VolumeRange> late;
        for (int n = 25; n <= 38; ++n) late.append({n, "1", "9"});
        CHECK(!gateVolumes(late, false).qualified, "gate: Vagabond late start fails");
        CHECK(!gateVolumes(run, true).qualified, "gate: quirk fails");
        CHECK(!gateVolumes({}, false).qualified, "gate: empty fails");
    }
    { // oddball: fractional origin flags, integer origin doesn't, mid-series .5 ok
        CHECK(numberingIsOddball(rows({{0.01,1},{0.02,1}})), "oddball: Berserk flags");
        CHECK(!numberingIsOddball(rows({{1,1},{27.2,3}})), "oddball: mid-series .5 fine");
    }
    std::printf(failures ? "\n%d FAILURES\n" : "\nALL GREEN\n", failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 3: Wire CMake (app target + harness) and confirm the harness FAILS to build**

In `native/CMakeLists.txt`:
1. In the app target's source list (beside `engine/MangaDexCatalogClient.cpp` at ~line 100), add:
```cmake
    engine/ComickVolumeGrouper.cpp
    engine/ComickVolumeGrouper.h
```
2. Beside the other harness targets (pattern at ~line 821), add:
```cmake
# Pure-logic contract for Comick grouping + the tankoban completeness gate:
# mirrors colosseum-volume-db's volume_builder tests case-for-case.
add_executable(comick_volume_grouper_harness
    ../tests/comick_volume_grouper_harness.cpp
    engine/ComickVolumeGrouper.cpp
    engine/ComickVolumeGrouper.h
)
target_include_directories(comick_volume_grouper_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comick_volume_grouper_harness PRIVATE Qt6::Core)
```

Run (absolute path — `cmd //c` on the bare name fails):
```bash
"/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc.bat" comick_volume_grouper_harness 2>&1 | tail -5
```
Expected: FAIL — `ComickVolumeGrouper.cpp` missing. (If the bat takes no target argument,
build all and grep the log — background-build exit codes lie; grep for `error C|ninja: build stopped`.)

- [ ] **Step 4: Implement the grouper**

`native/engine/ComickVolumeGrouper.cpp`:

```cpp
#include "ComickVolumeGrouper.h"

#include <QHash>
#include <QMap>
#include <QSet>

#include <cmath>

namespace tankoban::manga::comick {

static QString fmtNum(double n)   // Python _fmt: "7" for integers, "110.5" for fractions
{
    if (std::floor(n) == n)
        return QString::number(qint64(n));
    return QString::number(n);
}

QHash<double, int> majorityAssign(const QList<ChapterRow>& rows)
{
    // chapter -> (volume -> votes)
    QHash<double, QMap<int, int>> votes;
    for (const ChapterRow& r : rows) {
        if (r.chap < 0 || r.vol < 0)
            continue;
        votes[r.chap][int(r.vol)] += 1;
    }
    QHash<double, int> out;
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it) {
        int best = 0, bestVotes = -1;
        // QMap iterates keys ascending -> first max seen IS the smallest volume (tie law)
        for (auto v = it.value().constBegin(); v != it.value().constEnd(); ++v)
            if (v.value() > bestVotes) { best = v.key(); bestVotes = v.value(); }
        out.insert(it.key(), best);
    }
    return out;
}

QList<VolumeRange> groupVolumes(const QList<ChapterRow>& rows)
{
    const QHash<double, int> assign = majorityAssign(rows);
    QMap<int, QSet<double>> buckets;                 // ascending by volume
    for (auto it = assign.constBegin(); it != assign.constEnd(); ++it)
        buckets[it.value()].insert(it.key());

    QList<VolumeRange> out;
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        double lo = 0, hi = 0;
        bool first = true;
        for (double c : it.value()) {
            if (first) { lo = hi = c; first = false; }
            else { lo = std::min(lo, c); hi = std::max(hi, c); }
        }
        out.append({it.key(), fmtNum(lo), fmtNum(hi)});
    }
    return out;
}

bool numberingIsOddball(const QList<ChapterRow>& rows)
{
    bool any = false;
    double minChap = 0;
    for (const ChapterRow& r : rows) {
        if (r.chap < 0)
            continue;
        if (!any || r.chap < minChap) minChap = r.chap;
        any = true;
    }
    if (!any)
        return true;                                  // no chapters at all = unusable
    return std::floor(minChap) != minChap;            // fractional ORIGIN only
}

GateVerdict gateVolumes(const QList<VolumeRange>& vols, bool numberingQuirk)
{
    if (numberingQuirk)
        return {false, QStringLiteral("numbering quirk (fractional chapter origin)")};
    if (vols.isEmpty())
        return {false, QStringLiteral("no mapped volumes")};
    if (vols.first().number != 0 && vols.first().number != 1)
        return {false, QStringLiteral("first mapped volume is %1, not 0/1")
                           .arg(vols.first().number)};
    for (int i = 1; i < vols.size(); ++i)
        if (vols[i].number != vols[i - 1].number + 1)
            return {false, QStringLiteral("gap after volume %1").arg(vols[i - 1].number)};
    return {true, QString()};
}

} // namespace tankoban::manga::comick
```

- [ ] **Step 5: Build and run the harness**

```bash
"/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc.bat" comick_volume_grouper_harness 2>&1 | tail -3
/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc/comick_volume_grouper_harness.exe
```
Expected: every line `PASS`, final `ALL GREEN`, exit 0.
(Kill any running harness exe first if MSVC reports LNK1104.)

- [ ] **Step 6: Commit (explicit pathspec — CMakeLists is shared)**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum
git add native/engine/ComickVolumeGrouper.h native/engine/ComickVolumeGrouper.cpp tests/comick_volume_grouper_harness.cpp native/CMakeLists.txt
git status --short   # verify ONLY these four staged
git commit -m "feat(manga): ComickVolumeGrouper — majority-voted grouping + tankoban completeness gate (pure logic, mirrored from volume-db Python)" -- native/engine/ComickVolumeGrouper.h native/engine/ComickVolumeGrouper.cpp tests/comick_volume_grouper_harness.cpp native/CMakeLists.txt
git push -u origin agent1/tankoban-migration
```

---

### Task 3: `ComickCatalogClient` — DB read, live-scrape fallback, same signal contract

**Files:**
- Create: `native/engine/ComickCatalogClient.h`, `native/engine/ComickCatalogClient.cpp`
- Create: `tests/comick_catalog_parse_harness.cpp`
- Modify: `native/CMakeLists.txt` (add both to app target; add harness target — covered by the Task 0 declaration)

- [ ] **Step 1: Write the header**

`native/engine/ComickCatalogClient.h`:

```cpp
// ComickCatalogClient.h
//
// Volume-structure source for tankoban mode. Replaces MangaDexCatalogClient
// behind the IDENTICAL catalogReady/catalogFailed contract (MangaEngine and QML
// barely change). Two-step pipeline:
//   1. DB read  — GET raw.githubusercontent.com/kingoftheseas56/colosseum-volume-db/
//                 main/db/<weebcentral-ulid>.json  (unauthenticated CDN; our batch
//                 job keeps it warm). Record carries volumes + the gate verdict.
//   2. On miss  — live Comick scrape (api.comick.dev, token-free, browser UA
//                 REQUIRED or 403): search by title -> hid -> chapters across ALL
//                 languages (en-only tagging is sparse; other languages carry the
//                 tags — the MHA finding, 2026-07-29) -> ComickVolumeGrouper.
// Either path ends at the completeness gate. Gate-fail => catalogFailed => QML
// shows the flat WeebCentral chapter list. There is NO interpolation anywhere.
//
// Emitted volumes: ascending {number:double, cover:"", chapterStart, chapterEnd}.
// cover is ALWAYS empty: undownloaded tiles use the shelf's numbered placeholder;
// a downloaded volume's cover is its own first page (MangaVolumeIndex). WeebCentral
// has no chapter thumbnails (verified 2026-07-29) and Comick covers are rejected.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "ComickVolumeGrouper.h"

class QNetworkAccessManager;

namespace tankoban::manga::comick {

// Pure record parse — exposed for the harness. Input: raw DB-record JSON bytes.
// ok=false on malformed JSON. qualified mirrors the record's own verdict AND a
// local re-gate (belt and braces: a stale un-gated record cannot sneak sparse
// anchors into the app).
struct ParsedRecord {
    bool ok = false;
    bool qualified = false;
    QString gateReason;
    QVariantList volumes;      // ready-to-emit {number, cover:"", chapterStart, chapterEnd}
};
ParsedRecord parseDbRecord(const QByteArray& json);

// Pure chapters parse for the live path — exposed for the harness.
QList<ChapterRow> parseChapterRows(const QByteArray& json);

class ComickCatalogClient : public QObject
{
    Q_OBJECT
public:
    explicit ComickCatalogClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // DB by WeebCentral ULID first, live Comick scrape on miss. Emits exactly one
    // of catalogReady/catalogFailed per call. Concurrent calls allowed.
    void fetchSeries(const QString& weebCentralId, const QString& title);

signals:
    void catalogReady(const QString& title, const QVariantList& volumes);
    void catalogFailed(const QString& title, const QString& reason);

private:
    void stepDbRead(const QString& wcId, const QString& title);
    void stepSearch(const QString& title);
    void stepChapters(const QString& title, const QString& hid);

    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga::comick
```

- [ ] **Step 2: Write the failing parse harness**

`tests/comick_catalog_parse_harness.cpp`:

```cpp
// Pure-parse contract for ComickCatalogClient: DB-record JSON -> emit-ready
// volumes + gate verdict; live chapters JSON -> ChapterRow list. No network.

#include "engine/ComickCatalogClient.h"

#include <QVariantMap>
#include <cstdio>

using namespace tankoban::manga::comick;

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS  %s\n", name); } \
    else { std::printf("FAIL  %s\n", name); ++failures; } } while (0)

int main()
{
    { // qualified record -> volumes in emit shape, cover empty
        const QByteArray db = R"({"seriesTitle":"Bleach","qualified":true,"gateReason":"",
            "numberingQuirk":false,
            "volumes":[{"number":1,"chapterStart":"1","chapterEnd":"7"},
                       {"number":2,"chapterStart":"8","chapterEnd":"17"}]})";
        const ParsedRecord r = parseDbRecord(db);
        CHECK(r.ok && r.qualified, "db: qualified record parses");
        CHECK(r.volumes.size() == 2, "db: two volumes");
        const QVariantMap v0 = r.volumes.first().toMap();
        CHECK(v0.value("number").toDouble() == 1.0
                  && v0.value("cover").toString().isEmpty()
                  && v0.value("chapterStart").toString() == "1"
                  && v0.value("chapterEnd").toString() == "7",
              "db: emit shape {number, cover:\"\", chapterStart, chapterEnd}");
    }
    { // record that SAYS qualified but has a gap is re-gated locally and rejected
        const QByteArray db = R"({"seriesTitle":"X","qualified":true,"gateReason":"",
            "numberingQuirk":false,
            "volumes":[{"number":1,"chapterStart":"1","chapterEnd":"7"},
                       {"number":19,"chapterStart":"169","chapterEnd":"169"}]})";
        const ParsedRecord r = parseDbRecord(db);
        CHECK(r.ok && !r.qualified, "db: local re-gate rejects sparse record");
    }
    { // unqualified record and malformed JSON
        CHECK(!parseDbRecord(R"({"qualified":false,"gateReason":"gap","volumes":[]})").qualified,
              "db: unqualified stays unqualified");
        CHECK(!parseDbRecord("not json").ok, "db: malformed -> ok=false");
    }
    { // live chapters payload -> rows (missing chap/vol become <0)
        const QByteArray live = R"({"chapters":[
            {"chap":"1","vol":"1","lang":"en"},
            {"chap":"2","vol":null,"lang":"en"},
            {"chap":null,"vol":"2","lang":"vi"},
            {"chap":"7.5","vol":"1","lang":"es"}]})";
        const QList<ChapterRow> rows = parseChapterRows(live);
        CHECK(rows.size() == 4, "live: all rows kept");
        CHECK(rows[0].chap == 1.0 && rows[0].vol == 1.0, "live: numeric parse");
        CHECK(rows[1].vol < 0 && rows[2].chap < 0, "live: missing -> sentinel");
        CHECK(rows[3].chap == 7.5, "live: fractional chapter");
    }
    std::printf(failures ? "\n%d FAILURES\n" : "\nALL GREEN\n", failures);
    return failures ? 1 : 0;
}
```

CMake, beside the grouper harness:
```cmake
# Pure-parse contract for the Comick catalog client (DB record + live chapters).
add_executable(comick_catalog_parse_harness
    ../tests/comick_catalog_parse_harness.cpp
    engine/ComickCatalogClient.cpp
    engine/ComickCatalogClient.h
    engine/ComickVolumeGrouper.cpp
    engine/ComickVolumeGrouper.h
)
target_include_directories(comick_catalog_parse_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comick_catalog_parse_harness PRIVATE Qt6::Core Qt6::Network)
```
Also add `engine/ComickCatalogClient.cpp` + `.h` to the app target's source list.

Build → expected: FAIL (no implementation yet).

- [ ] **Step 3: Implement the client**

`native/engine/ComickCatalogClient.cpp`:

```cpp
#include "ComickCatalogClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

namespace tankoban::manga::comick {

// Browser-style UA — Comick 403s bare clients (verified 2026-07-29).
static const char kUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/126.0 Safari/537.36";

static const char kDbBase[] =
    "https://raw.githubusercontent.com/kingoftheseas56/colosseum-volume-db/main/db/";

static double numOrSentinel(const QJsonValue& v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double n = v.toString().toDouble(&ok);
        if (ok)
            return n;
    }
    return -1.0;
}

ParsedRecord parseDbRecord(const QByteArray& json)
{
    ParsedRecord out;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return out;
    out.ok = true;

    const QJsonObject o = doc.object();
    QList<VolumeRange> ranges;
    for (const QJsonValue& v : o.value(QStringLiteral("volumes")).toArray()) {
        const QJsonObject vo = v.toObject();
        ranges.append({int(vo.value(QStringLiteral("number")).toDouble()),
                       vo.value(QStringLiteral("chapterStart")).toVariant().toString(),
                       vo.value(QStringLiteral("chapterEnd")).toVariant().toString()});
    }
    // Belt and braces: trust the record's verdict only if a local re-gate agrees —
    // a stale/hand-edited record must not smuggle sparse anchors into the shelf.
    const GateVerdict local =
        gateVolumes(ranges, o.value(QStringLiteral("numberingQuirk")).toBool(false));
    out.qualified = o.value(QStringLiteral("qualified")).toBool(false) && local.qualified;
    out.gateReason = out.qualified ? QString()
                                   : (local.qualified
                                          ? o.value(QStringLiteral("gateReason")).toString()
                                          : local.reason);
    if (out.qualified)
        for (const VolumeRange& r : ranges)
            out.volumes.append(QVariantMap{{QStringLiteral("number"), double(r.number)},
                                           {QStringLiteral("cover"), QString()},
                                           {QStringLiteral("chapterStart"), r.chapterStart},
                                           {QStringLiteral("chapterEnd"), r.chapterEnd}});
    return out;
}

QList<ChapterRow> parseChapterRows(const QByteArray& json)
{
    QList<ChapterRow> rows;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    for (const QJsonValue& v : o.value(QStringLiteral("chapters")).toArray()) {
        const QJsonObject c = v.toObject();
        rows.append({numOrSentinel(c.value(QStringLiteral("chap"))),
                     numOrSentinel(c.value(QStringLiteral("vol")))});
    }
    return rows;
}

ComickCatalogClient::ComickCatalogClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

void ComickCatalogClient::fetchSeries(const QString& weebCentralId, const QString& title)
{
    if (weebCentralId.isEmpty()) {
        stepSearch(title);          // no key for the DB — straight to the live path
        return;
    }
    stepDbRead(weebCentralId, title);
}

void ComickCatalogClient::stepDbRead(const QString& wcId, const QString& title)
{
    QNetworkRequest req(QUrl(QString::fromLatin1(kDbBase) + wcId + QStringLiteral(".json")));
    req.setTransferTimeout(8000);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, title]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            stepSearch(title);      // miss or network trouble -> live scrape decides
            return;
        }
        const ParsedRecord rec = parseDbRecord(reply->readAll());
        if (!rec.ok) {
            stepSearch(title);
            return;
        }
        if (!rec.qualified) {       // the DB already knows the answer — do NOT rescrape
            qInfo("[comick] db record for '%s' not qualified: %s",
                  qUtf8Printable(title), qUtf8Printable(rec.gateReason));
            emit catalogFailed(title, rec.gateReason);
            return;
        }
        qInfo("[comick] db cache hit for '%s' (%lld volumes)",
              qUtf8Printable(title), qint64(rec.volumes.size()));
        emit catalogReady(title, rec.volumes);
    });
}

void ComickCatalogClient::stepSearch(const QString& title)
{
    QUrl url(QStringLiteral("https://api.comick.dev/v1.0/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), title);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("8"));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(8000);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, title]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit catalogFailed(title, QStringLiteral("comick search: ") + reply->errorString());
            return;
        }
        const QJsonArray hits = QJsonDocument::fromJson(reply->readAll()).array();
        if (hits.isEmpty()) {
            emit catalogFailed(title, QStringLiteral("comick search: no match"));
            return;
        }
        // best match: normalized-equal title (incl. md_titles), else first hit —
        // the same law MangaDexCatalogClient used.
        auto norm = [](const QString& s) {
            QString out;
            for (const QChar& c : s.toLower())
                if (c.isLetterOrNumber()) out.append(c);
            return out;
        };
        const QString want = norm(title);
        QString hid = hits.first().toObject().value(QStringLiteral("hid")).toString();
        for (const QJsonValue& h : hits) {
            const QJsonObject o = h.toObject();
            bool match = norm(o.value(QStringLiteral("title")).toString()) == want;
            for (const QJsonValue& alt : o.value(QStringLiteral("md_titles")).toArray())
                match = match || norm(alt.toObject().value(QStringLiteral("title")).toString()) == want;
            if (match) { hid = o.value(QStringLiteral("hid")).toString(); break; }
        }
        stepChapters(title, hid);
    });
}

void ComickCatalogClient::stepChapters(const QString& title, const QString& hid)
{
    // ALL languages on purpose: en-only tagging is sparse (the MHA finding).
    QNetworkRequest req(QUrl(QStringLiteral(
        "https://api.comick.dev/comic/%1/chapters?limit=100000&chap-order=1").arg(hid)));
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(20000);   // multi-MB for long series; 8s would false-fail
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, title]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit catalogFailed(title, QStringLiteral("comick chapters: ") + reply->errorString());
            return;
        }
        const QList<ChapterRow> rows = parseChapterRows(reply->readAll());
        const QList<VolumeRange> vols = groupVolumes(rows);
        const GateVerdict verdict = gateVolumes(vols, numberingIsOddball(rows));
        if (!verdict.qualified) {
            qInfo("[comick] live scrape for '%s' not qualified: %s",
                  qUtf8Printable(title), qUtf8Printable(verdict.reason));
            emit catalogFailed(title, verdict.reason);
            return;
        }
        QVariantList out;
        for (const VolumeRange& r : vols)
            out.append(QVariantMap{{QStringLiteral("number"), double(r.number)},
                                   {QStringLiteral("cover"), QString()},
                                   {QStringLiteral("chapterStart"), r.chapterStart},
                                   {QStringLiteral("chapterEnd"), r.chapterEnd}});
        qInfo("[comick] live scrape for '%s': %lld volumes qualified",
              qUtf8Printable(title), qint64(out.size()));
        emit catalogReady(title, out);
    });
}

} // namespace tankoban::manga::comick
```

- [ ] **Step 4: Build; run BOTH harnesses**

```bash
"/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc.bat" 2>&1 | grep -E "error C|ninja: build stopped" ; echo "grep-exit=$?"
/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc/comick_catalog_parse_harness.exe
/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc/comick_volume_grouper_harness.exe
```
Expected: no compile errors (`grep-exit=1` means no match = good), both harnesses `ALL GREEN`.

- [ ] **Step 5: Commit (explicit pathspec)**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum
git add native/engine/ComickCatalogClient.h native/engine/ComickCatalogClient.cpp tests/comick_catalog_parse_harness.cpp native/CMakeLists.txt
git commit -m "feat(manga): ComickCatalogClient — volume-db read + all-language live scrape behind the catalogReady contract, gate enforced both paths" -- native/engine/ComickCatalogClient.h native/engine/ComickCatalogClient.cpp tests/comick_catalog_parse_harness.cpp native/CMakeLists.txt
git push
```

---

### Task 4: `MangaEngine` swap + MangaDex retirement

**Files:**
- Modify: `native/MangaEngine.h`
- Delete: `native/engine/MangaDexCatalogClient.h`, `native/engine/MangaDexCatalogClient.cpp`, `tests/mangadex_volume_fold_harness.cpp`
- Modify: `native/CMakeLists.txt` (remove MangaDex entries + harness target)

- [ ] **Step 1: Swap the client inside `MangaEngine`**

In `native/MangaEngine.h`:

1. Replace the include `#include "MangaDexCatalogClient.h"` with `#include "engine/ComickCatalogClient.h"`
   (match the file's existing include style — the other engine includes are bare names, so
   `#include "ComickCatalogClient.h"` if that's what the include dirs resolve).
2. Replace the member declaration (find `m_mf` near the bottom member section) —
   `tankoban::manga::mangadex::MangaDexCatalogClient* m_mf` → `tankoban::manga::comick::ComickCatalogClient* m_comick`.
3. In the constructor, replace the MangaDex construction + connects:

```cpp
        m_comick = new tankoban::manga::comick::ComickCatalogClient(m_nam, this);

        // Volume structure now comes from our Comick-sourced volume DB (live-scrape
        // on miss), gated for completeness. A gate-fail or any failure emits an
        // empty list -> QML shows the flat WeebCentral chapter list. Same
        // volumesResult contract as ever, so the reveal gate never hangs.
        connect(m_comick, &tankoban::manga::comick::ComickCatalogClient::catalogReady,
                this, [this](const QString&, const QVariantList& volumes) {
                    emit volumesResult(QVariantMap{{"volumes", volumes}});
                });
        connect(m_comick, &tankoban::manga::comick::ComickCatalogClient::catalogFailed,
                this, [this](const QString& title, const QString& reason) {
                    qInfo("[comick] volumes unavailable for '%s': %s",
                          qUtf8Printable(title), qUtf8Printable(reason));
                    emit volumesResult(QVariantMap{{"volumes", QVariantList{}}});
                });
```

4. Replace the invokable (keep the comment honest):

```cpp
    // VOLUME structure — volume DB by WeebCentral id, live Comick scrape on miss,
    // completeness-gated. {volumes:[]} = not qualified -> flat chapter list.
    Q_INVOKABLE void volumes(const QString& seriesId, const QString& title)
    { m_comick->fetchSeries(seriesId, title); }
```

- [ ] **Step 2: Delete the MangaDex client + harness, purge CMake**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum
git rm native/engine/MangaDexCatalogClient.h native/engine/MangaDexCatalogClient.cpp tests/mangadex_volume_fold_harness.cpp
```
In `native/CMakeLists.txt`: remove the two `engine/MangaDexCatalogClient.*` lines from the app
target (~lines 100–101) and the whole `mangadex_volume_fold_harness` block (~lines 821–829,
including its comment).

- [ ] **Step 3: Build the app; expect ONE QML-side break, not zero**

```bash
"/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc.bat" 2>&1 | grep -E "error C|ninja: build stopped"; echo "grep-exit=$?"
```
Expected: compiles clean (`grep-exit=1`). QML still calls `Manga.volumes(title)` with one
argument — that breaks at RUNTIME, not compile time, and Task 6 fixes it. Do not launch yet.

- [ ] **Step 4: Commit (explicit pathspec, includes deletions)**

```bash
git add native/MangaEngine.h native/CMakeLists.txt
git commit -m "feat(manga): MangaEngine rides ComickCatalogClient; MangaDexCatalogClient retired (files + harness deleted)" -- native/MangaEngine.h native/CMakeLists.txt native/engine/MangaDexCatalogClient.h native/engine/MangaDexCatalogClient.cpp tests/mangadex_volume_fold_harness.cpp
git push
```

---

### Task 5: `MangaVolumes.js` — delete interpolation, trust the gate

**Files:**
- Modify: `qml/MangaVolumes.js`

- [ ] **Step 1: Replace the header comment and `fromEngine` wholesale**

The current `fromEngine` (lines 22–91) interpolates gaps between anchors and repairs corrupt
starts — machinery that exists only because MangaDex ranges were partial. The gate makes
partial ranges impossible (`volumes` is either a complete run or empty), so all of it goes.
Replace the file header (lines 1–9) and `fromEngine` with:

```js
// MangaVolumes.js — volume adapter + chapter grouping for the tankoban surface.
// The native ComickCatalogClient hands us COMPLETE, gate-qualified volume ranges
// (our volume DB first, live Comick scrape on miss) — or nothing at all. There is
// deliberately NO interpolation and NO anchor-repair here any more: a series either
// has a real, complete volume->chapter mapping or it shows the flat WeebCentral
// chapter list. Estimated boundaries are rejected doctrine (Hemanth, 2026-07-29).
// group() (ported from Tankoban Electron's MangaSeries.jsx volumeGroups) buckets the
// flat WeebCentral chapter list into the ranged volumes; chapters past the last
// range land in the 'X' ("Latest chapters") bucket.
.pragma library

function chapterNum(raw) {
    var m = /-?\d+(?:\.\d+)?/.exec(String(raw || ''))
    return m ? Number(m[0]) : null
}

// engine volumes: [{ number, cover, chapterStart, chapterEnd }] (ascending, ranges
// complete — the gate guarantees it) →
//   [{ number, cover, startNum, endNum, chapterStart, chapterEnd }] for group().
function fromEngine(volumes) {
    if (!volumes || !volumes.length) return []
    var out = []
    for (var i = 0; i < volumes.length; i++) {
        var v = volumes[i]
        var number = Number(v.number)
        var s = chapterNum(v.chapterStart)
        var e = chapterNum(v.chapterEnd)
        if (isNaN(number) || s === null || e === null) continue   // malformed row: drop, never guess
        out.push({ number: number, cover: v.cover || "",
                   startNum: s, endNum: Math.max(s, e),
                   chapterStart: String(v.chapterStart), chapterEnd: String(v.chapterEnd) })
    }
    out.sort(function (a, b) { return a.number - b.number })
    // contiguous handoff: each volume owns up to the next volume's start, so untagged
    // sub-chapters between reported ends (a 27.5 omake) land in the right book, not in 'X'.
    for (var j = 0; j + 1 < out.length; j++)
        if (out[j + 1].startNum > out[j].endNum)
            out[j].endNum = out[j + 1].startNum - 0.001
    return out
}
```

`group()` (lines 93–135) stays byte-identical.

- [ ] **Step 2: Grep sanity — the repair MACHINERY is gone (comments may still say
"no interpolation"; grep for code tokens only)**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum
grep -c "runMax\|rawEnd\|start = null" qml/MangaVolumes.js
```
Expected: `0`.

- [ ] **Step 3: Commit**

```bash
git add qml/MangaVolumes.js
git commit -m "refactor(manga): MangaVolumes.js trusts gate-complete ranges — interpolation and anchor-repair deleted" -- qml/MangaVolumes.js
git push
```

---

### Task 6: `MangaSeries.qml` — derived mode, toggle deletion, Latest-chapters bucket

**Files:**
- Modify: `qml/MangaSeries.qml`

Line numbers below are pre-edit (current file); apply top-to-bottom so later numbers shift
predictably. Read each region before editing.

- [ ] **Step 1: Derive `tankobanMode` from the gate verdict**

Replace (line 47):
```qml
    property bool tankobanMode: false
```
with:
```qml
    // Tankoban mode is PERMANENT for qualified series (2026-07-29 ruling): the gate in
    // ComickCatalogClient emits a complete volume list or nothing. volumes.length IS
    // the verdict — no toggle, no per-series persistence.
    property bool tankobanMode: volumes.length > 0
```

In `onSeriesIdChanged` (lines 52–58): delete the two `page.tankobanMode = …` statement lines
(the `TankobanVolumes.modeEnabled` read included); keep the `_tankobanPrepared`/
`tankobanReaderEntries` resets.

- [ ] **Step 2: Retire the toggle plumbing**

- Delete the whole `_setTankobanMode` function (lines 88–93).
- In `_handleVolumeSource` (lines 135–142): delete the line
  `if (!page.tankobanMode) page._setTankobanMode(true)`.
- In `resumeTankobanVolume` (lines 143–149): delete the `TankobanVolumes.setModeEnabled(...)`
  call and the `page.tankobanMode = true` line (mode is derived; a resumable tankoban record
  implies a qualified series — and if the series has since lost qualification the reader
  still opens the downloaded volume by id).
- In `_prepareTankoban` (lines 77–87): after the `_tankobanPrepared` guard, add
  `if (!page.volumes.length) return` — don't seed the native service for chapter-list series.

- [ ] **Step 3: Move the volume fetch behind the WeebCentral id**

In `resolve()` (line ~205): delete the line
```qml
            Manga.volumes(seriesTitle)   // → MangaDex volume structure (covers + partial ranges)
```
In `onSearchResults` (after `page.seriesId = r.id; page.seriesUrl = r.url` and before
`Manga.chapters(r.id)`), add:
```qml
            // volume structure needs the WC id (volume-DB key) — fire it as soon as we have it
            Manga.volumes(r.id, r.title)
```
Note the reveal gate still works: `volumesReady` is set by `onVolumesResult`, which now fires
after search resolves; the 12s `revealGuard` still caps a dead source. Also handle the
search-failed path: in the `results.length === 0` branch nothing fires `volumesResult`, but
that branch already sets `loading = false` directly, so no hang.

- [ ] **Step 4: Chapter table = flat list for chapter-series, Latest-chapters for tankoban**

First update the stale section comment (line ~157):
```qml
    // --- volumes (MangaDex via MangaVolumes.js; ranges partial — covers-first) ---
```
becomes:
```qml
    // --- volumes (Comick volume DB via MangaVolumes.js; complete ranges or none — gated) ---
```

Then replace the `visibleChapters` binding (lines ~173–175):
```qml
    property var visibleChapters: loading ? []
        : (volGroups.options.length ? (volGroups.byKey[shownVol] || []) : chaptersModel)
```
with:
```qml
    // qualified series: the chapter section shows ONLY the loose tail past the last
    // volume (group()'s X bucket) — the "Latest chapters" of an ongoing series.
    // unqualified series: the full flat WeebCentral list, exactly as before.
    property var visibleChapters: loading ? []
        : (page.tankobanMode ? ((volGroups.byKey && volGroups.byKey.X) || []) : chaptersModel)
```
The `activeVol`/`shownVol` properties (lines ~168–172) and `volRange()` (lines ~181–187) lose
their last consumers in Step 5 — delete them once Step 5's section deletions are in (grep
first: `grep -n "shownVol\|activeVol\|volRange" qml/MangaSeries.qml` must show only their
definitions before deleting).

- [ ] **Step 5: Delete the toggle UI + classic volume shelf; gate the sections**

- Delete the "TANKOBAN MODE" pill block: the `Row` containing the `Text { text: "TANKOBAN MODE" … }`
  label and the `modeRow` segmented control (lines ~437–474, ending before `LibraryButton`).
- Delete the classic volume-shelf section: the `Item` whose visibility is
  `visible: !page.tankobanMode && page.volumes.length > 0` (starts line ~497) — the whole
  block including its `volumesSec` Column. The tankoban surface replaces it permanently.
- Chapter-table section (line ~636, `visible: !page.tankobanMode && page.visibleChapters.length > 0`):
  change visibility to `visible: page.visibleChapters.length > 0` and, if the section has a
  header `Text`, make it conditional:
  `text: page.tankobanMode ? "Latest chapters" : "Chapters"` (match the header's existing
  styling; if the section has no header, add none).
- Tankoban surface (line ~866, `visible: page.tankobanMode`): unchanged — the derived
  property drives it now.

- [ ] **Step 6: Grep-audit the file for dead references**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum
grep -n "_setTankobanMode\|setModeEnabled\|modeEnabled\|TANKOBAN MODE" qml/MangaSeries.qml
```
Expected: no hits. (`TankobanVolumes.prepareSeries/volumesForSeries/statusOf` etc. remain — only
the mode toggle dies.)

- [ ] **Step 7: Full build + boot smoke**

```bash
"/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc.bat" 2>&1 | grep -E "error C|ninja: build stopped"; echo "grep-exit=$?"
```
Expected `grep-exit=1`. Then launch the built exe (Qt bin on PATH first, detached, CWD = repo)
and open a manga series; watch the log for `[comick] db cache hit` / `live scrape` /
`not qualified` lines and zero QML errors mentioning `MangaSeries.qml` or `MangaVolumes.js`.

- [ ] **Step 8: Commit**

```bash
git add qml/MangaSeries.qml
git commit -m "feat(manga): tankoban mode is permanent for gate-qualified series — toggle deleted, volume fetch keyed by WC id, Latest-chapters bucket for ongoing tails" -- qml/MangaSeries.qml
git push
```

---

### Task 7: Contract test + full suite

**Files:**
- Create: `tests/test_tankoban_migration_p0.ps1`

- [ ] **Step 1: Write the grep-contract test** (follow the style of the existing
`tests/test_*_p0.ps1` scripts — ASCII only, PS 5.1-safe):

```powershell
# test_tankoban_migration_p0.ps1 - P0 contracts for the permanent tankoban migration.
# ASCII only (PS 5.1 chokes on non-ASCII in BOM-less files).
$repo = Split-Path $PSScriptRoot -Parent
$fail = 0

function Check($name, $ok) {
    if ($ok) { Write-Host "PASS  $name" }
    else { Write-Host "FAIL  $name"; $script:fail++ }
}

# 1. MangaDex is fully retired from the runtime (docs/comments elsewhere may mention it)
$dex = Select-String -Path "$repo\native\*.h","$repo\native\*.cpp","$repo\native\engine\*.h","$repo\native\engine\*.cpp","$repo\qml\*.qml","$repo\qml\*.js" -Pattern "MangaDexCatalogClient|api\.mangadex\.org" -SimpleMatch:$false
Check "no MangaDex client or endpoint in runtime code" ($null -eq $dex)

# 2. The engine exposes the two-argument volume fetch
$sig = Select-String -Path "$repo\native\MangaEngine.h" -Pattern "void volumes\(const QString& seriesId, const QString& title\)"
Check "MangaEngine.volumes(seriesId, title)" ($null -ne $sig)

# 3. QML calls it with the WC id
$call = Select-String -Path "$repo\qml\MangaSeries.qml" -Pattern "Manga\.volumes\(r\.id, r\.title\)"
Check "MangaSeries fires volumes with the WC id" ($null -ne $call)

# 4. No interpolation machinery survives in MangaVolumes.js (code tokens only --
#    the file's comments legitimately SAY "no interpolation")
$interp = Select-String -Path "$repo\qml\MangaVolumes.js" -Pattern "runMax|rawEnd|start = null"
Check "MangaVolumes.js has no interpolation/repair" ($null -eq $interp)

# 5. The toggle is gone
$tog = Select-String -Path "$repo\qml\MangaSeries.qml" -Pattern "_setTankobanMode|setModeEnabled|TANKOBAN MODE"
Check "tankoban toggle deleted" ($null -eq $tog)

# 6. The gate lives in both native paths (grouper + client re-gate)
$gate1 = Select-String -Path "$repo\native\engine\ComickVolumeGrouper.cpp" -Pattern "gap after volume"
$gate2 = Select-String -Path "$repo\native\engine\ComickCatalogClient.cpp" -Pattern "gateVolumes"
Check "completeness gate present in grouper and client" (($null -ne $gate1) -and ($null -ne $gate2))

if ($fail -gt 0) { Write-Host "$fail CONTRACT FAILURES"; exit 1 }
Write-Host "ALL CONTRACTS GREEN"; exit 0
```

- [ ] **Step 2: Run it + both harnesses + the pre-existing manga harnesses**

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_tankoban_migration_p0.ps1
```
```bash
/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc/comick_volume_grouper_harness.exe
/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc/comick_catalog_parse_harness.exe
/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc/manga_tankoban_logic_harness.exe
/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc/manga_volume_index_harness.exe
```
Expected: all green. Known unrelated red: `tests/test_back_action_p0.ps1` fails on
`PlayerPage.qml` (A4's lane, pre-existing — do not touch it).

- [ ] **Step 3: Commit**

```bash
git add tests/test_tankoban_migration_p0.ps1
git commit -m "test(manga): P0 contracts for the tankoban migration (MangaDex gone, gate present, toggle deleted)" -- tests/test_tankoban_migration_p0.ps1
git push
```

---

### Task 8: Acceptance — evidence before eyes-on

- [ ] **Step 1: Live in-app checks (log-verified, then screenshots for the handoff)**

Launch the built exe. For each, note the `[comick]` log line and screenshot the series page:

1. **Bleach** → `db cache hit … 74 volumes` → shelf of 74 tiles (numbered placeholders /
   local covers), NO toggle anywhere, chapter section shows only any loose tail.
2. **My Hero Academia** → `db cache hit … 42 volumes` → **the title that started this arc
   shows a full 42-volume shelf.**
3. **A qualified series NOT in the DB** (e.g. Frieren) → `live scrape … qualified` after a
   skeleton beat → shelf appears.
4. **A gate-fail series** (One-Punch Man — zero volume tags) → `not qualified` → clean flat
   WeebCentral chapter list, indistinguishable from today's default page, no volume UI.
5. **Berserk** → `not qualified: numbering quirk` → flat chapter list (documented v1 behavior).

- [ ] **Step 2: Ledger the outcome on chat.md**

Append to `Brotherhood/agents/chat.md` a READY-FOR-EYES-ON entry: branch, commit ids, the
five acceptance results with log lines, the two spec deltas Hemanth has not yet seen in
running form (Berserk fallback, cover-placeholder doctrine), and the standing offer of a
Codex cross-review. **Merge to master and Hemanth's eyes-on are HIS gates, not steps here.**

---

## Self-review notes (run before handoff)

- Spec §6.2 contract preserved: `catalogReady(title, volumes)` shape byte-compatible; QML's
  `onVolumesResult` untouched. ✔ (Task 3/4)
- Spec §4 "graceful degrade" now = gate-fail → flat chapter list. ✔ (Task 6 Step 4)
- Spec §7.4 "MangaDex gone" grep = contract test #1. ✔ (Task 7)
- Spec §7.3 cache-hit vs live-scrape log line = client qInfo lines. ✔ (Task 3)
- Deltas from the locked spec are all recorded in the table up top with their rulings. ✔
- NOT in this plan (deliberate): oddball normalization (Berserk stays chapter-list v1),
  Latest-chapters *tile* on the tankoban shelf itself (the chapter section below carries the
  tail), seed-list expansion beyond MHA, `TankobanVolumes.modeEnabled/setModeEnabled` C++
  removal (QML no longer calls them; C++ cleanup is a follow-up sweep).
