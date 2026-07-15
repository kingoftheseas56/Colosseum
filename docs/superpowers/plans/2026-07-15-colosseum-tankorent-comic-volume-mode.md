# Colosseum Tankorent Comics Collected-Edition-in-Pack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user choose a full-series comic torrent and safely acquire exactly one GCD
collected edition from its archive, issue-file set, or loose-page subtree under the existing
`Comics`/`chId` reader contract.

**Architecture:** Pure `ComicEditionIdentity`, `ComicCoverage`, trust, ranking, and manifest
selection modules decide what belongs to an edition. A restart-safe shared-infohash downloader
fetches only the union of live edition payloads, `ComicEditionAssembler` creates a complete ordered
page staging directory, and `ComicDownloader` atomically publishes it through the existing comics
index and signals. QML remains a manual torrent chooser and renders typed fallback states only.

**Tech Stack:** Qt 6.11 C++/QML, `TorrentEngine`/libtorrent 2.0, `QSaveFile`, existing bsdtar/7-Zip
extraction policy, PowerShell test runners, MSVC 2022/Ninja.

## Global Constraints

- Read `docs/superpowers/specs/2026-07-15-colosseum-tankorent-comic-volume-mode-design.md` first.
- Do not modify manga/Tankoban Mode, Biblio, Theatre, catalog artifacts, or Python catalog builders.
- The user chooses the torrent; only safe file-in-pack selection is automatic.
- GetComics remains the primary one-click source.
- Every terminal signal and page lookup remains on `Comics` under the original catalog `chId`.
- Apply zero priorities before metadata ambiguity is exposed to QML.
- A partial collected-issue set must never publish or begin automatic payload download.
- Combined multi-edition archives require explicit whole-archive confirmation.
- Use `apply_patch` for source edits and stage only named files; preserve unrelated dirty files.
- Kill a running `colosseum.exe` by PID before building; run `native\build-msvc.bat` directly.
- TDD pure logic: demonstrate RED before implementation and GREEN afterward.

---

## File map

### New production units

- `native/torrent/ComicEditionIdentity.{h,cpp}`: catalog identity, format/ordinal/ISBN normalization,
  collected-issue parsing.
- `native/torrent/ComicCoverage.{h,cpp}`: format-scoped coverage grammar.
- `native/torrent/ComicUploaderTrust.{h,cpp}` and `comics_uploader_trust.json`: bounded uploader tags.
- `native/torrent/ComicEditionFileSelector.{h,cpp}`: manifest-to-payload decision and priorities.
- `native/torrent/ComicRequestLedger.{h,cpp}`: versioned atomic edition intent journal.
- `native/engine/ComicEditionAssembler.{h,cpp}`: archive/issue-set/subtree to ordered page staging.

### Existing units to extend

- `ComicTorrentRanker`: target-aware evidence aggregation.
- `ComicTorrentDownloader`: shared-infohash intents and replay.
- `ComicTorrents`: structured target forwarding and typed selection signals.
- `ComicDownloader`: atomic assembled-page ingest while preserving public QML identity.
- `ComicTorrentSourcesPage.qml` / `ComicTorrentArchivePicker.qml`: typed states and confirmations.
- `native/CMakeLists.txt` / `native/app_resources.qrc`: targets and trust resource.

---

### Task 1: Freeze the existing comics torrent contract

**Files:**
- Modify: `tests/test_comic_torrent_sources_v2.ps1`
- Modify: `tests/test_comics_sources_p0.ps1`
- Test: existing comics torrent harnesses

**Interfaces:**
- Consumes: shipped alternate-source facade and reader behavior.
- Produces: one baseline command that later tasks must keep green.

Before the first source edit, record the exact implementation base outside the repository:

```powershell
git rev-parse HEAD | Set-Content (Join-Path $env:TEMP 'colosseum-comic-pack-implementation-base.txt')
```

- [ ] **Step 1: Add a baseline runner section**

Invoke these existing binaries from `native/build-msvc` and fail on any non-zero exit:

```powershell
$build = Join-Path $PSScriptRoot '..\native\build-msvc'
$baseline = @(
    'comic_torrent_query_planner_harness.exe',
    'comic_torrent_ranker_harness.exe',
    'comic_torrent_filepicker_harness.exe',
    'comic_torrents_search_harness.exe',
    'comic_downloader_ingest_harness.exe'
)
foreach ($name in $baseline) {
    & (Join-Path $build $name)
    if ($LASTEXITCODE -ne 0) { throw "$name failed: $LASTEXITCODE" }
}
```

- [ ] **Step 2: Run the baseline**

Run:

```powershell
& .\tests\test_comic_torrent_sources_v2.ps1
& .\tests\test_comics_sources_p0.ps1
```

Expected: both exit 0 before feature work starts.

- [ ] **Step 3: Commit the frozen gate**

```powershell
git add tests/test_comic_torrent_sources_v2.ps1 tests/test_comics_sources_p0.ps1
git commit -m "test(comics): freeze alternate-source baseline"
```

---

### Task 2: Canonical edition identity and collected-issue parser

**Files:**
- Create: `native/torrent/ComicEditionIdentity.h`
- Create: `native/torrent/ComicEditionIdentity.cpp`
- Create: `tests/comic_edition_identity_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `ComicCollectionFormat`, `ComicIssueRef`, `ComicEditionTarget`,
  `ComicEditionIdentity::buildTarget`, `parseFormat`, `parseOrdinal`, and
  `parseCollectedIssues`.

- [ ] **Step 1: Write the failing identity harness**

Cover these exact assertions:

```cpp
using namespace ComicEditionIdentity;
require(parseFormat("Compendiums") == ComicCollectionFormat::Compendium,
        "compendium alias");
require(parseOrdinal("Invincible Compendium #01", ComicCollectionFormat::Compendium) == 1,
        "numbered compendium");
require(parseOrdinal("Saga Book Two", ComicCollectionFormat::Book) == 2,
        "worded book");
require(parseOrdinal("Hellboy Omnibus IV", ComicCollectionFormat::Omnibus) == 4,
        "roman omnibus");
require(parseOrdinal("Batman 2016 Deluxe", ComicCollectionFormat::Deluxe) == -1,
        "unscoped year is not ordinal");
const auto parsed = parseCollectedIssues(
    "Invincible", "Invincible #0, #14-16; The Pact #4");
require(parsed.complete && parsed.issues.size() == 5, "multi-series issue expansion");
require(parsed.issues[4].series == "The Pact" && parsed.issues[4].number == 4,
        "cross-series issue identity");
require(!parseCollectedIssues("Invincible", "#1-3 plus bonus material").complete,
        "unparsed required fragment disables automatic issue set");
```

- [ ] **Step 2: Add the target and run RED**

Add `comic_edition_identity_harness` to `native/CMakeLists.txt` using `Qt6::Core`.

Run:

```powershell
cmake --build native/build-msvc --target comic_edition_identity_harness
```

Expected: compilation fails because the identity API does not exist.

- [ ] **Step 3: Implement the minimal pure module**

Use a closed alias table, bounded ordinal regexes, word/Roman conversion only beside a recognized
format token, order-preserving issue expansion, and a `complete` flag plus diagnostics.

- [ ] **Step 4: Run GREEN**

```powershell
cmake --build native/build-msvc --target comic_edition_identity_harness
& .\native\build-msvc\comic_edition_identity_harness.exe
```

Expected: `COMIC_EDITION_IDENTITY_OK`, exit 0.

- [ ] **Step 5: Commit**

```powershell
git add native/torrent/ComicEditionIdentity.h native/torrent/ComicEditionIdentity.cpp `
        tests/comic_edition_identity_harness.cpp native/CMakeLists.txt
git commit -m "feat(comics): add canonical collected-edition identity"
```

---

### Task 3: Format-scoped coverage and uploader trust

**Files:**
- Create: `native/torrent/ComicCoverage.{h,cpp}`
- Create: `native/torrent/ComicUploaderTrust.{h,cpp}`
- Create: `native/torrent/comics_uploader_trust.json`
- Create: `tests/comic_coverage_trust_harness.cpp`
- Modify: `native/app_resources.qrc`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `ComicCollectionFormat` from Task 2.
- Produces: `detectComicCoverage`, `coverageCovers`, `ComicUploaderTrust::load`, and
  `taggedUploader(title, table)`.

- [ ] **Step 1: Write the failing coverage/trust harness**

```cpp
const auto spans = detectComicCoverage(
    "TPBs v01-v25, Compendiums v01-v03, Omnibus 2");
require(coverageCovers(spans, ComicCollectionFormat::Compendium, 1),
        "compendium range covers target");
require(!coverageCovers(spans, ComicCollectionFormat::TradePaperback, 26),
        "range upper bound");
require(!coverageCovers(detectComicCoverage("Invincible 025"),
                       ComicCollectionFormat::Volume, 25),
        "bare issue number is not volume coverage");
require(taggedUploader("Invincible Collection (- Nem -)", trust).tier == 1,
        "bounded Nem tag trusted");
require(taggedUploader("The Nemesis Collection", trust).tier == 99,
        "substring is not uploader evidence");
```

- [ ] **Step 2: Run RED**, then implement clause-bound coverage parsing and exact normalized trust
tags.

- [ ] **Step 3: Register the trust JSON resource**

Add:

```xml
<file alias="tankorent/comics_uploader_trust.json">torrent/comics_uploader_trust.json</file>
```

- [ ] **Step 4: Run GREEN**

```powershell
cmake --build native/build-msvc --target comic_coverage_trust_harness
& .\native\build-msvc\comic_coverage_trust_harness.exe
```

Expected: `COMIC_COVERAGE_TRUST_OK`, exit 0.

- [ ] **Step 5: Commit** all files from this task with
`feat(comics): parse format coverage and uploader trust`.

---

### Task 4: Preserve target evidence while ranking torrent results

**Files:**
- Modify: `native/torrent/ComicTorrentRanker.{h,cpp}`
- Modify: `native/torrent/ComicTorrents.{h,cpp}`
- Modify: `tests/comic_torrent_ranker_harness.cpp`
- Modify: `tests/comic_torrents_search_harness.cpp`

**Interfaces:**
- Consumes: `ComicEditionTarget`, coverage spans, uploader trust.
- Produces: `rankForEdition(const ComicEditionTarget&, const QList<TorrentResult>&)` and QML rows
  containing `coverage`, `uploader`, `trustTier`, `confidence`, and unioned `evidence`.

- [ ] **Step 1: Add failing ranker cases**

Use duplicate rows with the same hash: a high-seed generic title and a low-seed exact
`Compendiums v01-v03 (- Nem -)` title. Assert one canonical row retains `COVERAGE` and `UPLOADER`,
ranks strong, and still uses the higher seed count. Add a conflicting `TPBs v01-v03 (- Nem -)` row
and assert trust cannot make it strong for a Compendium target.

- [ ] **Step 2: Run the ranker/search harnesses and verify RED.**

- [ ] **Step 3: Implement an aggregate per canonical infohash**

Keep volatile representative fields separately from unioned identity evidence. Confidence must be
computed after aggregation.

- [ ] **Step 4: Pass a structured target through `ComicTorrents::SourceSession`**

Keep the existing QML facade signature temporarily; construct `ComicEditionTarget` once at the
facade boundary and store it in the session.

- [ ] **Step 5: Run GREEN**

```powershell
cmake --build native/build-msvc --target comic_torrent_ranker_harness comic_torrents_search_harness
& .\native\build-msvc\comic_torrent_ranker_harness.exe
& .\native\build-msvc\comic_torrents_search_harness.exe
```

- [ ] **Step 6: Commit** with `feat(comics): rank format coverage and trusted packs`.

---

### Task 5: Select safe payloads from torrent metadata

**Files:**
- Create: `native/torrent/ComicEditionFileSelector.{h,cpp}`
- Create: `tests/comic_edition_file_selector_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `ComicEditionTarget`, `QList<ManifestFile>`.
- Produces: `ComicPayloadKind`, `ComicSelectionFailure`, `ComicSelectedFile`,
  `ComicPayloadDecision`, `select(target, files)`, and `unionPriorities(decisions, fileCount)`.

- [ ] **Step 1: Write a fixture-rich failing harness**

Assert:

- Compendium 1 selects `Compendiums/Invincible Compendium v01.cbz`, not `TPBs/...v01.cbz`.
- Two exact Compendium 1 candidates return `Ambiguous` and no selected indices.
- `Invincible Compendiums v01-v03.cbz` returns `CombinedOnly`.
- A complete issue set selects all required issue archives in target order.
- A missing issue returns `IncompleteIssueSet`, names the missing issue, and selects nothing.
- A cross-series `The Pact #4.cbz` is included only for that parsed issue identity.
- A matched directory of page images yields `LooseImageSubtree` in natural order.
- Priority union is 7 only for selected manifest indices and 0 everywhere else.

- [ ] **Step 2: Run RED.**

- [ ] **Step 3: Implement the five selection tiers exactly as specified**

Never use size or extension preference to break an identity tie. Clean paths and reject any `..`
escape before returning selected files.

- [ ] **Step 4: Run GREEN** and expect `COMIC_EDITION_FILE_SELECTOR_OK`.

- [ ] **Step 5: Commit** with `feat(comics): isolate edition payloads inside packs`.

---

### Task 6: Assemble archive sets and loose pages atomically

**Files:**
- Create: `native/engine/ComicEditionAssembler.{h,cpp}`
- Create: `tests/comic_edition_assembler_harness.cpp`
- Create: `tests/fixtures/comics-pack/edition-one.cbz`
- Create: `tests/fixtures/comics-pack/issue-001.cbz`
- Create: `tests/fixtures/comics-pack/issue-002.cbz`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: edition identity, payload decision, job root.
- Produces: `assemble(request)`, `cancel(editionId)`, `progress`,
  `finished(editionId, stagingDir, orderedFiles, groups)`, and `failed`.

- [ ] **Step 1: Write failing assembly tests**

Test single archive; two issue archives with groups `[0,0,1,1]`; naturally ordered loose images;
corrupt archive; traversal path; cancellation; and absence of a published/final directory after
every failure.

- [ ] **Step 2: Run RED.**

- [ ] **Step 3: Implement sequential extraction and validation**

Reuse the exact bsdtar then 7-Zip executable discovery policy from `ComicDownloader`; extract each
archive into its own child directory, validate supported image magic/type, natural-sort, then move
to `page_NNN.ext` in a sibling `.staging` directory. Do not publish from this component.

- [ ] **Step 4: Run GREEN** and expect `COMIC_EDITION_ASSEMBLER_OK`.

- [ ] **Step 5: Commit** with `feat(comics): assemble multi-file collected editions`.

---

### Task 7: Publish assembled editions through `Comics`

**Files:**
- Modify: `native/engine/ComicDownloader.{h,cpp}`
- Modify: `tests/comic_downloader_ingest_harness.cpp`
- Modify: `tests/test_comics_sources_p0.ps1`

**Interfaces:**
- Consumes: assembler output.
- Produces: C++-only `ingestAssembledEdition(...)`; unchanged QML `Comics` signals and
  `localPages(chId)`.

- [ ] **Step 1: Add a failing ingest case**

Provide a staging directory with four pages and groups `[0,0,1,1]`. Assert atomic move, one index
entry under the original edition ID, positive pages, preserved groups, and source staging removal.
Cancel before publication and assert no index record.

- [ ] **Step 2: Run RED.**

- [ ] **Step 3: Add an assembled-ingest queue item**

Do not bypass the existing single publication lane. Validate every supplied file is inside the
staging directory, move the complete directory into `issueDir(...)`, save the existing index, then
emit the normal `finished(editionId)`.

- [ ] **Step 4: Run GREEN plus existing GetComics/torrent ingest regressions.**

- [ ] **Step 5: Commit** with `feat(comics): publish assembled editions through Comics`.

---

### Task 8: Add the versioned edition-request ledger

**Files:**
- Create: `native/torrent/ComicRequestLedger.{h,cpp}`
- Create: `tests/comic_request_ledger_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `ComicEditionRequestRow`, `load`, `active`, `upsert`, `setSelection`, `setState`,
  `remove`, and schema version 1.

- [ ] **Step 1: Write failing persistence tests**

Round-trip all structured target fields, several selected indices, and payload kind. Verify
atomic replacement, malformed row quarantine, unknown-version rejection, active-state filtering,
and cancellation persistence.

- [ ] **Step 2: Run RED.**

- [ ] **Step 3: Implement with `QSaveFile`**

Persist enum values as stable lowercase strings, not C++ ordinals. Reject duplicate edition IDs and
non-40-hex infohashes when loading active rows.

- [ ] **Step 4: Run GREEN** and expect `COMIC_REQUEST_LEDGER_OK`.

- [ ] **Step 5: Commit** with `feat(comics): persist collected-edition torrent intents`.

---

### Task 9: Refactor downloader to shared-infohash edition intents

**Files:**
- Modify: `native/torrent/ComicTorrentDownloader.{h,cpp}`
- Modify: `native/torrent/ComicTorrents.{h,cpp}`
- Create: `tests/comic_torrent_pack_transport_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: target, selector, ledger, assembler.
- Produces: per-edition resolving/progress/selection/finished/failed signals; priority union;
  replay; `confirmCombined(editionId)`; `chooseFiles(editionId, indices)`.

- [ ] **Step 1: Write a fake-engine RED harness**

Prove:

1. First intent calls `addMagnet(..., paused=true)` once.
2. Metadata produces exact priorities before `startTorrent`.
3. A second edition on the same hash does not re-add the magnet and grows the union.
4. Canceling one narrows priorities and preserves the sibling.
5. One assembly failure does not fail the sibling.
6. Shared files are removed only after all intents are terminal.
7. Replay regroups two rows by hash, re-adds paused, forgets old selection, and reselects.
8. An already-present completed payload can satisfy a later intent without downloading unrelated
   files again.

- [ ] **Step 2: Run RED.**

- [ ] **Step 3: Replace `Job::issueId` with `QList<Intent>`**

Keep maps `infoHash -> Job*` and `editionId -> infoHash`. Journal before starting payload. Resolve
each intent independently, union only live selected indices, and emit terminal signals per edition.

- [ ] **Step 4: Wire assembly to `ComicTorrents::archiveReady` replacement**

Emit assembled staging payload to `ComicDownloader::ingestAssembledEdition`; keep the existing
single-archive API as a compatibility wrapper for old tests and DLTEST.

- [ ] **Step 5: Run GREEN**

```powershell
cmake --build native/build-msvc --target comic_torrent_pack_transport_harness
& .\native\build-msvc\comic_torrent_pack_transport_harness.exe
```

Expected: `COMIC_TORRENT_PACK_TRANSPORT_OK`, exit 0.

- [ ] **Step 6: Commit** with `feat(comics): share restart-safe torrent pack jobs`.

---

### Task 10: Render typed selection and confirmation states

**Files:**
- Modify: `qml/ComicTorrentSourcesPage.qml`
- Modify: `qml/ComicTorrentArchivePicker.qml`
- Modify: `tests/comic_torrent_sources_page_harness.qml`
- Modify: `tests/test_comic_torrent_sources_v2.ps1`

**Interfaces:**
- Consumes: coverage/trust row fields and typed selection signals.
- Produces: inspecting, ambiguous, incomplete-set, combined-confirmation, and cancellation UI.

- [ ] **Step 1: Extend the headless QML harness and verify RED**

Assert coverage/trust badges render in the row model; safe selection enters inspecting; ambiguous
files open the picker; incomplete-set state names missing issues and makes no download call;
combined state requires a second confirm call; Back during every active state calls
`cancelDownload(issueId)` once.

- [ ] **Step 2: Implement minimal state properties**

Use one `selectionState` string (`results|inspecting|ambiguous|incomplete|combined`) and one active
edition context. Do not add a second downloader object.

- [ ] **Step 3: Preserve house styling and accessibility**

Use existing gold evidence copy, button focus, `Accessible.name`, and the current full-screen lazy
page. Do not alter `ComicDbLedger.qml` unless the facade needs one additive signal connection.

- [ ] **Step 4: Run GREEN and qmllint**

```powershell
& .\tests\test_comic_torrent_sources_v2.ps1
& C:\Qt\6.11.1\msvc2022_64\bin\qmllint.exe qml\ComicTorrentSourcesPage.qml qml\ComicTorrentArchivePicker.qml
```

- [ ] **Step 5: Commit** with `feat(comics): show safe pack-selection states`.

---

### Task 11: Deterministic real-engine pack and restart gates

**Files:**
- Create: `tests/comic_torrent_pack_seed_harness.cpp`
- Create: `tests/test_comic_torrent_pack_dltest.ps1`
- Modify: `native/engine/ComicDownloader.{h,cpp}`
- Modify: `native/main.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `COLOSSEUM_COMIC_PACK_DLTEST` with scenario and loopback magnet.
- Produces: process exit 0 only after positive canonical reader pages and expected groups.

- [ ] **Step 1: Build the legal seed fixture**

The seed harness must create one torrent containing two valid Compendium CBZs, three valid issue
CBZs, a decoy TPB, and a text file. It prints a magnet with an explicit loopback peer and remains
alive for five minutes. The PowerShell runner captures the emitted magnet into `$magnet` before
launching each application scenario.

- [ ] **Step 2: Add three self-test scenarios**

```text
single|$magnet|fixture:comp1
issues|$magnet|fixture:issue-set
shared|$magnet|fixture:comp1|fixture:comp2
```

Each deletes prior fixture IDs first, runs the production target/selector/transport/assembly path,
and exits 0 only after `localPages` is positive. `issues` additionally checks distinct ordered
groups; `shared` cancels the first intent after non-zero progress and requires the second to finish.

- [ ] **Step 3: Add a restart scenario to the PowerShell runner**

Launch a dedicated AppData test root, wait for a ledger row in `downloading`, kill only that
`colosseum.exe` PID, restart with the same root, and require one `DONE` record with no duplicate
edition index entry.

- [ ] **Step 4: Run all four scenarios**

```powershell
& .\tests\test_comic_torrent_pack_dltest.ps1
```

Expected output contains:

```text
COMIC_PACK_SINGLE_DONE pages=[1-9][0-9]*
COMIC_PACK_ISSUES_DONE pages=[1-9][0-9]* groups=[1-9][0-9]*
COMIC_PACK_SHARED_DONE pages=[1-9][0-9]*
COMIC_PACK_RESTART_DONE pages=[1-9][0-9]* records=1
```

Exit 0 is mandatory.

- [ ] **Step 5: Commit** with `test(comics): prove real pack selection and restart`.

---

### Task 12: Full regression, surgical review, and handoff

**Files:**
- Modify only execution evidence checkboxes in this plan if desired.

**Interfaces:**
- Produces: verified Definition-of-Done ledger and Hemanth eyes-on handoff.

- [ ] **Step 1: Run focused suites**

```powershell
& .\tests\test_comic_torrent_sources_v2.ps1
& .\tests\test_comics_sources_p0.ps1
& .\tests\test_comic_torrent_pack_dltest.ps1
& .\tests\test_comics_catalog_v1.ps1
```

Expected: every command exits 0.

- [ ] **Step 2: Run affected cross-lane regressions without editing those lanes**

```powershell
& .\tests\test_manga_tankoban_native.ps1
& .\tests\test_manga_tankoban_mode.ps1
```

Expected: both exit 0.

- [ ] **Step 3: Kill the running app by PID and build**

```powershell
$p = Get-Process colosseum -ErrorAction SilentlyContinue
if ($p) { $p | ForEach-Object { Stop-Process -Id $_.Id -Force } }
& 'C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat'
if ($LASTEXITCODE -ne 0) { throw "build failed: $LASTEXITCODE" }
```

Expected: exit 0 and final `BUILD_OK`.

- [ ] **Step 4: Inspect scope**

```powershell
git status --short
$implementationBase = Get-Content (Join-Path $env:TEMP 'colosseum-comic-pack-implementation-base.txt')
git diff --name-only "$implementationBase..HEAD"
```

Confirm no manga, Biblio, Theatre, catalog, Python, or unrelated dirty files were changed. If the
recorded base file is missing, stop and recover the intended base SHA from the task-start transcript;
do not guess it from branch names.

- [ ] **Step 5: Run Brotherhood self-review**

Score all 12 Definition-of-Done items in the design as `MET/PARTIAL/NOT-MET`. Fix every PARTIAL or
NOT-MET before claiming release readiness.

- [ ] **Step 6: Hemanth eyes-on smoke**

Open Invincible Compendium 1, choose the Nem pack, confirm coverage/trust evidence, and verify the
row progresses to reader pages without a file picker. Then exercise one ambiguous pack and one
incomplete issue pack to confirm the honest fallback copy.

- [ ] **Step 7: Final surgical commit and push only when green**

Stage only files named by this plan. Commit with protocol attribution and push together under
Hemanth's standing rule.

---

## Plan self-review

### Spec coverage

- Canonical identity and complete issue parsing: Task 2.
- Format coverage and bounded uploader trust: Task 3.
- Evidence-safe deduplication and advisory ranking: Task 4.
- Archive, issue-set, loose-subtree, ambiguity, and combined decisions: Task 5.
- Lossless atomic assembly and grouped pages: Tasks 6-7.
- Versioned ledger, shared infohash, cancellation narrowing, and replay: Tasks 8-9.
- QML states and same-`chId` cancellation: Task 10.
- Real engine, multi-issue, shared, and restart evidence: Task 11.
- Cross-lane regressions, build, scope, and eyes-on: Task 12.

### Placeholder scan

The plan contains no unresolved implementation placeholders. Runtime-only values are captured by
the named `$magnet` and `$implementationBase` variables at the exact steps where they are used.

### Type consistency

- `editionId` is the existing catalog `chId` everywhere.
- `ComicEditionTarget` is built once and consumed by ranker, selector, ledger, and transport.
- `ComicPayloadDecision.files` holds manifest indices; assembler output holds page filenames/groups.
- `ComicDownloader::ingestAssembledEdition` is C++-only; QML continues to call the existing
  `Comics` facade.
- Shared torrent cleanup is owned by the infohash job, never by one edition intent.
