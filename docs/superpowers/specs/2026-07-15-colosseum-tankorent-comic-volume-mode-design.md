# Colosseum Tankorent Comics: Collected-Edition-in-Pack Design

> Enhanced by `[Agent 1 (Codex), comics]`, 2026-07-15. Product decisions originate
> with Hemanth and Agent 0. This design extends the shipped
> [Alternate Torrent Sources v2](2026-07-15-colosseum-comics-alternate-torrent-sources-v2-design.md)
> and borrows the proven transport discipline of
> [Tankoban Nyaa Volume Mode](2026-07-14-colosseum-tankoban-nyaa-volume-mode-design.md)
> without modifying manga code.

## Purpose

Make full-series comic torrent packs useful for bibliographic collected editions. The user still
chooses a torrent manually in the existing alternate-sources page. After that choice, Colosseum
must identify and download only the file or files that constitute the selected GCD edition, publish
them under the edition's existing `chId`, and open them through the existing `Comics` reader
contract.

Examples:

- `Invincible Compendium #1` selects the Compendium 1 archive from a pack containing issues, TPBs,
  hardcovers, and several compendiums.
- An edition collecting issues `#14-16, #18-36, #38, #40-47` can be assembled from those issue
  archives when the pack has no collected-edition archive.
- A loose-page directory representing exactly one collected edition can be published without
  pretending it is an archive.

This is automatic file-in-pack isolation, not automatic torrent selection.

## Locked product decisions

1. Extend the existing comics torrent components; do not rebuild them or modify manga's shipped
   Tankoban Mode implementation.
2. Preserve manga-Tankoban transport parity where it fits: paused metadata inspection,
   exact file priorities, restart-safe intent ledger, shared infohash jobs, and per-edition signals.
3. The user always chooses the torrent. File selection inside that torrent is automatic when the
   evidence is unique and safe. Manual file choice remains the ambiguity fallback.
4. Canonical collection format plus edition number is the primary identity. ISBN is corroborating
   evidence. The exact collected-issue set is the issue-pack fallback.
5. Matching is format-scoped. `Compendium #1` must never silently resolve to TPB 1, issue 1,
   Omnibus 1, or a generic `v01` whose collection format is unknown.
6. Uploader trust influences ranking but never bypasses identity safety. Initial tier-1 uploader:
   `Nem`.
7. All progress, status, cancellation, extraction, completion, local pages, and reader opening
   remain on the public `Comics` object under the original catalog `chId`.
8. A combined archive containing several editions is not an isolated match. It requires an
   explicit whole-archive confirmation and is never labeled as the requested edition automatically.

## Non-goals

- No automatic selection of a torrent search result.
- No cross-media Tankorent consolidation.
- No new torrent indexers or Cloudflare bypasses.
- No manga/Tankoban Mode changes.
- No catalog rebuild or GCD matching changes.
- No bulk whole-series acquisition button.
- No attempt to split a monolithic CBZ/CBR internally by detecting cover pages.

## Architecture

The pipeline is intentionally divided into pure identity/selection logic, durable torrent
transport, and canonical publication:

```text
GCD edition row
  -> ComicEditionIdentity (canonical target)
  -> ComicTorrentRanker (torrent-level advisory evidence)
  -> user chooses torrent
  -> ComicTorrentDownloader (paused metadata + shared infohash intent)
  -> ComicEditionFileSelector (safe payload decision)
  -> ComicEditionAssembler (archive set / loose subtree -> ordered page staging)
  -> ComicDownloader ingest entry (existing index + reader contract)
  -> Comics.finished(chId) / Comics.localPages(chId)
```

QML paints and forwards intent. C++ owns parsing, identity, file priorities, assembly, persistence,
and terminal state.

## Canonical edition identity

### `ComicEditionIdentity`

Create `native/torrent/ComicEditionIdentity.{h,cpp}` as a pure module. It owns catalog-facing
normalization so coverage and file-selection code do not reinterpret raw strings independently.

```cpp
enum class ComicCollectionFormat {
    Unknown,
    Compendium,
    Omnibus,
    TradePaperback,
    Deluxe,
    Absolute,
    Hardcover,
    Collection,
    Volume,
    Book
};

struct ComicIssueRef {
    QString series;
    int number = -1;
};

struct ComicEditionTarget {
    QString editionId;       // existing ledger chId
    QString seriesId;
    QString seriesTitle;
    QString editionTitle;
    ComicCollectionFormat format = ComicCollectionFormat::Unknown;
    int ordinal = -1;        // collection number; -1 for named one-shots
    QString isbnDigits;
    QList<ComicIssueRef> collectedIssues; // canonical order, deduplicated
};
```

`buildTarget(...)` receives the fields already available in `ComicDbLedger.qml`: `chId`, series
identity, display title, catalog format, ISBN, and `collects`. The output is passed through the
facade as structured values; QML never recreates it.

### Format normalization

Normalize these aliases:

| Canonical | Accepted aliases |
|---|---|
| Compendium | `compendium`, `compendiums` |
| Omnibus | `omnibus`, `omnibuses`, `omni` |
| TradePaperback | `tpb`, `trade`, `trade paperback` |
| Deluxe | `deluxe`, `deluxe edition` |
| Absolute | `absolute`, `absolute edition` |
| Hardcover | `hc`, `hardcover`, `hard cover` |
| Collection | `collection`, `collected edition` |
| Volume | `vol`, `volume`, `v` |
| Book | `book` |

The explicit catalog `format` field wins when it maps cleanly. Title-derived format fills only an
unknown catalog value. Conflicting explicit and title formats make the target invalid for automatic
format matching; the eventual decision must be manual.

**AMENDMENT (2026-07-16):** the catalog's generic umbrella label `"Collected Edition"` (→ `Collection`)
is NOT a specific collection line — it is a category carried by ~80% of GCD editions (5,515 of ~6,700).
It must never *conflict with* or *override* the specific format the title itself states ("Book One",
"Vol 3", "Omnibus 1"), because releases are named after the title, never the umbrella ("Saga Collected
Edition 1" does not exist). So when the catalog format is this umbrella, the target *defers to* the
title's specific format (and parses its ordinal there). Treating the umbrella as a conflict flagged
thousands of editions `formatAmbiguous`, zeroed their ordinal, and dropped them into issue-set
assembly ("missing issues") instead of matching the collected volume as a whole. Corollary:
coverage detection (`ComicCoverage`) must read a **single worded ordinal** ("Book One" → 1), not only
worded ranges, so a "Book One" edition can match a "Book One" release.

### Ordinal parsing

Parse `#1`, `No. 1`, `Vol 01`, `Book One`, Roman numerals `I` through `XX`, and a trailing numeric
ordinal adjacent to an accepted collection token. Bare numbers elsewhere in a title are not
collection ordinals. Named one-shots retain `ordinal == -1` and can auto-match only by unique exact
title or ISBN-bearing filename evidence.

### Collected-issue parsing

`parseCollectedIssues(seriesTitle, collects)` recognizes comma-separated single issues and ranges,
including multiple named series within one edition:

```text
Invincible #0, #14-16, #18-36, #38, #40-47
The Pact #4; Image Comics Summer Special #1
```

Each range expands into ordered `ComicIssueRef` values. Repeated entries are deduplicated without
changing first-seen order. Unparseable fragments are returned separately as diagnostics. The
issue-pack path is eligible only when every required numeric fragment parsed successfully.

## Coverage and trust

### `ComicCoverage`

Create `native/torrent/ComicCoverage.{h,cpp}` as pure text grammar:

```cpp
struct ComicCoverageSpan {
    ComicCollectionFormat format = ComicCollectionFormat::Unknown;
    int lo = -1;
    int hi = -1;
    QString evidenceText;
};

QList<ComicCoverageSpan> detectComicCoverage(const QString& text);
bool coverageCovers(const QList<ComicCoverageSpan>& spans,
                    ComicCollectionFormat format, int ordinal);
```

Rules:

- Range forms include `v01-v03`, `Vol 1-3`, `Compendiums #1-3`, `Book One-Three`, and a single
  `Omnibus 02`.
- A range is bound to the closest preceding recognized format token inside the same punctuation
  clause. `TPBs v01-v25, Compendiums v01-v03` yields two independent spans.
- Generic `v01`/`Volume 1` yields `Volume`, never Compendium or TPB.
- Bare issue numbers and issue ranges are not collection coverage.
- Coverage matches only when canonical formats are equal and the target ordinal lies inside the
  inclusive span.

### Uploader trust

Add `native/torrent/comics_uploader_trust.json`, versioned like the manga trust resource:

```json
{
  "version": 1,
  "tier1": ["Nem"],
  "tier2": [],
  "blocked": []
}
```

`ComicUploaderTrust` parses only bounded release-tag positions such as `[Nem]`, `(Nem)`,
`(- Nem -)`, or a final `- Nem`. A mere occurrence of `nem` inside another word is not trust
evidence. Blocked tags remove a row; tier 1/2 adjust ranking only after identity evidence.

## Torrent-level ranking

Extend `ComicTorrentRanker::rankForEdition` to accept a `ComicEditionTarget` and preserve evidence
across duplicate infohashes.

Identity score order:

1. ISBN digits in release title: strongest.
2. Exact normalized edition title.
3. Matching format-scoped coverage.
4. Complete collected-issue range evidence.
5. General series/title token evidence.
6. Comic archive hint.
7. Uploader trust.
8. Seeder count as the final volatile tie-break.

Trust cannot turn a format conflict into `strong`. All universal-filter results remain visible;
weak rows still require the existing explicit confirmation.

Deduplication aggregates evidence per canonical infohash. The highest-seeded representative may
supply volatile fields, but exact title, ISBN, coverage, issue-range, archive, source, and uploader
evidence are unioned from every duplicate row before confidence is assigned.

## Manifest selection

### Payload decision model

Replace the single `PickedFile` assumption with an explicit decision:

```cpp
enum class ComicPayloadKind {
    None,
    SingleArchive,
    IssueArchiveSet,
    LooseImageSubtree,
    CombinedWholeArchive
};

enum class ComicSelectionFailure {
    None,
    TargetMissing,
    Ambiguous,
    CombinedOnly,
    IncompleteIssueSet,
    UnsupportedPayload
};

struct ComicSelectedFile {
    int index = -1;
    QString path;
    qint64 bytes = 0;
    int order = -1;
};

struct ComicPayloadDecision {
    ComicPayloadKind kind = ComicPayloadKind::None;
    ComicSelectionFailure failure = ComicSelectionFailure::TargetMissing;
    QList<ComicSelectedFile> files;
    QVariantList manualCandidates;
};
```

### Selection tiers

`ComicEditionFileSelector` evaluates only supported comic archives and image files:

1. Unique exact normalized edition-title archive.
2. Unique filename coverage match for target format + ordinal.
3. Unique deepest parent-directory coverage match, selecting supported payload files in that
   subtree.
4. Complete collected-issue archive set, ordered by `ComicEditionTarget.collectedIssues`.
5. Otherwise return a typed failure/manual decision.

Filename evidence wins over directory evidence. Two equal candidates are `Ambiguous`; no size or
extension preference silently breaks the tie.

### Issue archive identity

Loose issue archives require both strong series agreement and an explicit issue marker (`#14`,
`Issue 14`, `0014`). Files inside a directory already carrying the exact canonical series may use
bare zero-padded numeric stems. Annuals, specials, and cross-series issues match only when the
parsed `ComicIssueRef.series` agrees. A complete set is required: one missing required issue makes
the automatic decision `IncompleteIssueSet` and downloads no payload.

### Loose image subtree

A directory coverage match can select loose JPEG, PNG, GIF, WebP, or AVIF pages. The subtree is
eligible only when all selected images live under the same matched directory and no sibling
subdirectory advertises another collection ordinal. Images are naturally ordered by relative path.

### Combined archives

An archive whose own name advertises an inclusive multi-edition range is `CombinedOnly`, not an
isolated edition. QML may offer **Download whole archive anyway** with explicit copy explaining
that Colosseum cannot separate its internal books. Acceptance downloads and publishes it as the
release title, not falsely as the target edition. It never occurs without a second confirmation.

## Assembly and publication

### `ComicEditionAssembler`

Create `native/engine/ComicEditionAssembler.{h,cpp}`. It converts the selected payload into one
validated staging page directory without publishing partial output.

- `SingleArchive`: extract through the same bsdtar/7-Zip policy as `ComicDownloader`.
- `IssueArchiveSet`: extract each archive into an isolated child staging directory, validate image
  pages, then append them in canonical collected-issue order. Preserve an integer group per source
  issue for reader navigation.
- `LooseImageSubtree`: validate and copy selected images in natural relative-path order. Never move,
  rename, or delete anything beneath the shared torrent job root because sibling edition intents may
  still depend on the same payload.
- `CombinedWholeArchive`: extract as one explicitly user-confirmed release; group `-1`.

No source image is recompressed. Output names are `page_NNN.<ext>`. Validation requires at least one
real supported image, unique destination names, and no path escaping the staging root.

### `ComicDownloader` ingest boundary

Keep `Comics` as the only public QML object. Add one C++-only entry receiving an assembled payload:

```cpp
void ingestAssembledEdition(const QString& editionId,
                            const QString& seriesId,
                            const QString& seriesTitle,
                            const QString& editionLabel,
                            const QString& stagingDir,
                            const QStringList& orderedFiles,
                            const QList<int>& groups);
```

It queues behind the existing single extraction/publication lane, atomically moves a complete
staging directory into the normal comics directory, saves the existing index, and emits the normal
`progress`, `failed`, or `finished(editionId)` signals. `localPages(editionId)` gains real group
values when an issue set supplied them; existing GetComics and single-archive behavior remains
unchanged.

## Durable shared-infohash transport

### `ComicRequestLedger`

Create a versioned `QSaveFile` ledger under
`<AppDataLocation>/comics-torrent/edition-requests.json`:

```cpp
struct ComicEditionRequestRow {
    QString editionId;
    QString infoHash;
    QString magnetUri;
    QString seriesId;
    QString seriesTitle;
    QString editionTitle;
    ComicCollectionFormat format;
    int ordinal = -1;
    QString isbnDigits;
    QList<ComicIssueRef> collectedIssues;
    QString savePath;
    QList<int> pickedFileIndices;
    ComicPayloadKind payloadKind;
    QString state;
};
```

States:

```text
awaiting_metadata -> downloading -> assembling -> publishing -> completed
                                      |              |
                                      +-> failed     +-> failed
cancelled is terminal from any active state
```

Unknown ledger versions are ignored with a diagnostic. Invalid or incomplete rows are quarantined
as failed rather than partially replayed.

### Shared job model

`ComicTorrentDownloader` becomes one job per canonical infohash with one or more edition intents.

- A new job adds the magnet paused and journals the intent before payload starts.
- Another edition choosing the same infohash joins the job; metadata is reused and priorities
  become the union of every live intent's selected indices.
- Canceling one edition marks only that intent cancelled and re-narrows priorities. The torrent is
  removed with files only when the final live intent is gone.
- On engine completion, each intent assembles and publishes independently. One edition's assembly
  failure does not fail siblings.
- A pack on which NO intent ever succeeded (pure cancel/fail-out) is torn down and its files deleted
  once the final live intent is gone. Reference counting is explicit; no intent owns the whole
  shared directory.
- **RATIFIED AMENDMENT (Hemanth, 2026-07-16):** a pack on which at least one intent SUCCEEDED KEEPS
  SEEDING for the rest of the session (torrent left registered, files preserved). This deliberately
  supersedes the original "delete after every intent reaches a terminal state" rule below. Rationale:
  (1) a later intent joining the already-completed payload assembles immediately with no re-download,
  and (2) seeding completed packs back is the seed toward Tankorent becoming a Torrentio-style source.
  Cross-restart re-seeding is a separate future feature (completed rows are terminal, so replay does
  not re-add them).
- A later intent joining an already completed payload resolves against existing metadata/files and
  can assemble immediately without re-downloading present files.

### Restart replay

At construction, load active ledger rows, regroup them by infohash, and re-add each torrent paused.
Forget persisted file choices for execution purposes, re-run selection against current metadata,
then rewrite `pickedFileIndices` and resume. Replay emits status under the original edition IDs even
before QML reopens the series.

An app kill during assembly leaves only staging directories. Startup cleanup removes stale staging
that has no active ledger owner; no partial edition is ever inserted into the comics index.

## QML behavior

The existing `ComicTorrentSourcesPage.qml` remains the entry surface.

- Torrent cards gain restrained `FORMAT RANGE`, `ISSUES`, and uploader trust evidence.
- Selecting a row transitions to `Inspecting pack...` while metadata is resolved.
- Safe unique decisions close the source page and continue through the normal ledger row progress.
- `Ambiguous` shows the existing archive picker with only eligible candidates.
- `IncompleteIssueSet` explains which required issues were not found and offers another-source or
  manual-file options; it does not download the partial set automatically.
- `CombinedOnly` shows the explicit whole-archive warning and confirmation.
- Back during resolving, choosing, downloading, or assembly calls `Comics.cancelDownload(chId)`.

GetComics remains the primary one-click action. This feature changes only the alternate-source path.

## Error and safety contracts

- Search failure remains separate from acquisition failure and never creates a Downloads job.
- Invalid catalog identity disables automatic format/issue selection but still allows manual source
  inspection.
- Metadata timeout, missing payload, archive extraction failure, invalid image content, disk-space
  failure, cancellation, and restart corruption terminate under the original `chId`.
- Zero file priorities are applied before an ambiguous/manual decision is exposed.
- No automatic decision downloads a subset of the required collected issues.
- Every filesystem path derived from torrent metadata is cleaned and verified to remain inside the
  job root before reading, moving, or deleting it.

## Files and ownership

### New files

- `native/torrent/ComicEditionIdentity.{h,cpp}`
- `native/torrent/ComicCoverage.{h,cpp}`
- `native/torrent/ComicUploaderTrust.{h,cpp}`
- `native/torrent/comics_uploader_trust.json`
- `native/torrent/ComicEditionFileSelector.{h,cpp}`
- `native/torrent/ComicRequestLedger.{h,cpp}`
- `native/engine/ComicEditionAssembler.{h,cpp}`
- Focused C++ harnesses and legal loopback pack fixtures under `tests/fixtures/comics-pack/`

### Modified files

- `native/torrent/ComicTorrentRanker.{h,cpp}`
- `native/torrent/ComicTorrentDownloader.{h,cpp}`
- `native/torrent/ComicTorrents.{h,cpp}`
- `native/engine/ComicDownloader.{h,cpp}`
- `native/CMakeLists.txt`
- `native/app_resources.qrc`
- `qml/ComicTorrentSourcesPage.qml`
- `qml/ComicTorrentArchivePicker.qml`
- Existing comics torrent test runners

Do not modify manga/Tankoban Mode, Biblio, Theatre, catalog artifacts, Python catalog builders, or
unrelated dirty files.

## Testing strategy

### Pure deterministic harnesses

- Identity: format aliases/conflicts, numbered/worded/Roman ordinals, named one-shots, ISBN digits,
  multi-series collected-issue parsing, malformed-range rejection.
- Coverage: multiple formats in one name, format equality, range precedence, generic volume not
  masquerading as Compendium, bare issue numbers rejected.
- Trust: exact Nem tags, blocked tags, false substring rejection.
- Ranking: coverage upgrades the right format, trust cannot rescue a conflict, duplicate hashes
  retain strongest evidence.
- Selection: unique filename, directory subtree, ambiguous tie, combined-only, complete issue set,
  incomplete issue set, cross-series specials, loose images, union priorities.
- Assembly: single archive, ordered multi-archive issue set with groups, loose pages, traversal
  rejection, corrupt archive, cancellation, and atomic publication.
- Ledger/transport: versioning, atomic reload, one infohash/two editions, cancel narrowing, sibling
  failure isolation, completed-payload join, restart regroup/reselect.

### Headless QML

Prove badges, inspecting state, automatic transition, ambiguity fallback, incomplete-set copy,
combined confirmation, and Back-to-cancel under the same `chId`.

### Deterministic real-engine gates

Add a legal loopback seeder that creates a torrent containing:

- `Compendiums/Invincible Compendium v01.cbz`
- `Compendiums/Invincible Compendium v02.cbz`
- issue archives sufficient for a small fixture edition
- decoy TPB and loose-issue files

Required gates:

1. Compendium 1 selects only its archive, downloads, assembles, indexes, and reports positive pages.
2. A fixture collected edition selects several issue archives in canonical order and reports groups.
3. Two editions share one infohash; canceling one preserves and completes the other.
4. Kill only the test `colosseum.exe` after non-zero progress, restart with the same AppData, and
   finish exactly one canonical edition record.

Public-indexer eyes-on smoke is additional evidence, not a substitute for deterministic gates.

## Build and rollout order

1. Identity, coverage, trust, ranking, and pure selection decisions.
2. Assembler plus canonical `ComicDownloader` publication boundary.
3. Shared-infohash downloader and versioned restart ledger.
4. QML ambiguity/combined/incomplete states.
5. Regression, loopback, restart, build, and Hemanth eyes-on smoke.

Each increment is independently testable and committed surgically.

## Definition of Done

1. A chosen pack containing `Compendium v01-v03` ranks as strong coverage for Compendium 1 and
   automatically selects only Compendium 1.
2. Format conflicts are safe: Compendium 1 never auto-selects TPB 1, Omnibus 1, issue 1, or an
   unscoped generic volume.
3. ISBN, title, coverage, collected issues, archive, source, and uploader evidence survive
   infohash deduplication; trust cannot override an identity conflict.
4. A complete issue-only pack assembles every required issue in canonical `collects` order; an
   incomplete set downloads nothing automatically and reports missing issues.
5. Single archives, ordered issue-archive sets, and loose-image subtrees publish through `Comics`
   under the original edition `chId` with positive `localPages`.
6. Combined multi-edition archives require explicit whole-archive confirmation and are never
   mislabeled as an isolated edition.
7. One infohash serves multiple editions with priority union, per-edition progress/terminal state,
   cancellation narrowing, sibling-failure isolation, and reference-safe cleanup of packs that never
   succeeded. (Per the 2026-07-16 ratified amendment above, a pack with >=1 successful intent
   intentionally keeps seeding for the session instead of being torn down.)
8. Active requests survive an app restart by re-deriving selection from metadata; interrupted
   assembly never publishes partial pages or duplicate index records.
9. Ambiguous packs retain the manual picker with zero priorities until the user chooses; Back from
   any acquisition state cancels through `Comics.cancelDownload(chId)`.
10. Existing GetComics downloads, standalone torrent downloads, source browsing, comics reader,
    manga/Tankoban Mode, Biblio, and Theatre regressions remain green.
11. Pure C++ harnesses, async facade tests, headless QML tests, deterministic single-file,
    multi-issue, shared-infohash, and restart DLTESTs all exit 0.
12. `native\build-msvc.bat` exits 0 with `BUILD_OK`, qmllint is clean for changed QML, and Hemanth's
    eyes-on Invincible smoke confirms the intended source-selection experience.
