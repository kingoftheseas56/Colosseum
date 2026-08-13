# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/vault-state.json`

## Summary

- Total files: **50**
- Documented: **32**
- Undocumented: **18**
- Drifted: **0**

<a id="file-native-engine-vaultbookcoverprovider-cpp"></a>
## `native/engine/VaultBookCoverProvider.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `936cbc90e98cb039ae02b9a5f6af2079815f2665`
- Current blob: `936cbc90e98cb039ae02b9a5f6af2079815f2665`
- Source: [`native/engine/VaultBookCoverProvider.cpp`](../../native/engine/VaultBookCoverProvider.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultbookcoverprovider-h"></a>
## `native/engine/VaultBookCoverProvider.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `796592b0ecdc0ea29dfcdc787afa0b12d84604a0`
- Current blob: `796592b0ecdc0ea29dfcdc787afa0b12d84604a0`
- Source: [`native/engine/VaultBookCoverProvider.h`](../../native/engine/VaultBookCoverProvider.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultbookstatemigrator-cpp"></a>
## `native/engine/VaultBookStateMigrator.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `bdd2a9c9384f9d619d97404b75a4a61f47a2f3e3`
- Current blob: `bdd2a9c9384f9d619d97404b75a4a61f47a2f3e3`
- Source: [`native/engine/VaultBookStateMigrator.cpp`](../../native/engine/VaultBookStateMigrator.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultbookstatemigrator-h"></a>
## `native/engine/VaultBookStateMigrator.h`

- Status: **CURRENT**
- Accepted blob: `abe0bbd80b0d214ab43507de716ab3368211f566`
- Current blob: `abe0bbd80b0d214ab43507de716ab3368211f566`
- Source: [`native/engine/VaultBookStateMigrator.h`](../../native/engine/VaultBookStateMigrator.h)

```text
// Vault-owned bridge for moving existing Reader 2 path-keyed stores after VaultIdentity reports
// an unambiguous file rename. Reader 2 owns the store shape; this consumes only BookStores' public
// API and never edits Reader 2 sources.
```

<a id="file-native-engine-vaultbrowseaway-cpp"></a>
## `native/engine/VaultBrowseAway.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `b6fa7bba1f3bf31abbbf4f2936ba740e7e28ed0a`
- Current blob: `b6fa7bba1f3bf31abbbf4f2936ba740e7e28ed0a`
- Source: [`native/engine/VaultBrowseAway.cpp`](../../native/engine/VaultBrowseAway.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultbrowseaway-h"></a>
## `native/engine/VaultBrowseAway.h`

- Status: **CURRENT**
- Accepted blob: `2bfe435bbea97c773341963341ad9485706e3d22`
- Current blob: `2bfe435bbea97c773341963341ad9485706e3d22`
- Source: [`native/engine/VaultBrowseAway.h`](../../native/engine/VaultBrowseAway.h)

```text
// VaultBrowseAway — Vault Browse projection spine, Slice 6 (living tile states execution plan):
// which confirmed root owns a browse path, whether that root is currently away, and the durable-
// index fallback for a level whose owning root can no longer be walked at all.
//
// Pulled out of VaultLibrary into its own seam so this logic is unit-testable against a real
// VaultIndex + VaultConfig WITHOUT constructing the full VaultLibrary façade — VaultLibrary's
// constructor unconditionally builds a VaultWatcher (which in turn drags in the scanner/
// downloads-root/identifier dependency tree), unnecessary weight for logic that only reads
// VaultIndex rows and VaultConfig's root list. Pure Qt6::Core/Sql, no QObject of its own —
// same "pure logic kit" spirit as VaultKit, one layer up (VaultKit knows nothing of VaultIndex;
// this does, because away is an INDEX fact, not a filesystem fact).
```

<a id="file-native-engine-vaultbrowsedetail-cpp"></a>
## `native/engine/VaultBrowseDetail.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `9680023c8762c9af9266f600453fc951f2af17a0`
- Current blob: `9680023c8762c9af9266f600453fc951f2af17a0`
- Source: [`native/engine/VaultBrowseDetail.cpp`](../../native/engine/VaultBrowseDetail.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultbrowsedetail-h"></a>
## `native/engine/VaultBrowseDetail.h`

- Status: **CURRENT**
- Accepted blob: `562f287fefd87e98230fe1143114f6275c45f790`
- Current blob: `562f287fefd87e98230fe1143114f6275c45f790`
- Source: [`native/engine/VaultBrowseDetail.h`](../../native/engine/VaultBrowseDetail.h)

```text
// VaultBrowseDetail — the browse-face execution plan's Slice 7: the detail sheet's ONE
// projection, `browseDetail(key)`. Physical truth only — the locked design's decision #11 and
// its three explicit refusals of cast/synopsis/related titles (that is Theatre's job). Answers:
// every copy you hold (same canonical identity across roots where identity exists, else the
// single physical group), its companions, its extras, and an honest evidence line for why Vault
// believes the identity it does.
//
// Pulled out of VaultLibrary for the same reason VaultBrowseAway was (Slice 6): a lean Qt Test
// drives this against a real VaultIndex WITHOUT the full VaultLibrary façade (whose constructor
// unconditionally builds a VaultWatcher and drags in the scanner/downloads-root/identifier
// tree). Filesystem-structural work (companions/extras) is VaultKit::describeFilmFolder's job;
// this module composes that with VaultIndex's identity query and the presentation facts (human
// size, a best-quality line parsed from the filename, the evidence sentence) the sheet needs.
```

<a id="file-native-engine-vaultbrowseempty-cpp"></a>
## `native/engine/VaultBrowseEmpty.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `621edc2dc4fb5cc3b9951e9c440ff038c56a7f2d`
- Current blob: `621edc2dc4fb5cc3b9951e9c440ff038c56a7f2d`
- Source: [`native/engine/VaultBrowseEmpty.cpp`](../../native/engine/VaultBrowseEmpty.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultbrowseempty-h"></a>
## `native/engine/VaultBrowseEmpty.h`

- Status: **CURRENT**
- Accepted blob: `6d2431874f1e789a3d3d2fb3f019a8feb26baadf`
- Current blob: `6d2431874f1e789a3d3d2fb3f019a8feb26baadf`
- Source: [`native/engine/VaultBrowseEmpty.h`](../../native/engine/VaultBrowseEmpty.h)

```text
// VaultBrowseEmpty — Vault Browse projection spine, Slice 9 (empty states + keyboard reach
// execution plan): which of the design's four distinct empty causes (locked design §4.5) applies
// to a browse level. Pure classification over facts the rest of the projection already computes —
// whether ANY confirmed/synthetic root exists (VaultLibrary::rootsDetail()), whether the level's
// OWN browseAt() rows are non-empty, and whether the level's owning root is currently away
// (VaultBrowseAway::ownerRootAway) — this file infers nothing new about the filesystem or the
// index; it only NAMES the cause so QML paints instead of guessing (the execution plan's own
// instruction: "the projection already knows which cause applies — key the component off the
// cause, do not infer it in QML"). Same "pure logic kit" spirit as VaultBrowseAway, one layer up
// (this one composes ON TOP of VaultBrowseAway's away verdict, never reimplements it).
//
// The fourth design cause ("a filter has excluded everything") is named here for completeness —
// VaultBrowseEmpty.qml still renders its copy — but classify() below NEVER produces it: no filter
// control has shipped on the Browse face (deferred to the parent design's later arc; the execution
// plan is explicit that inventing a live trigger for it here would be exactly the kind of
// unrequested product surface Slice 9 warns against).
```

<a id="file-native-engine-vaultconfig-cpp"></a>
## `native/engine/VaultConfig.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `6ec777e12fd1013b1582ee7dd1f591ff702f978e`
- Current blob: `6ec777e12fd1013b1582ee7dd1f591ff702f978e`
- Source: [`native/engine/VaultConfig.cpp`](../../native/engine/VaultConfig.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultconfig-h"></a>
## `native/engine/VaultConfig.h`

- Status: **CURRENT**
- Accepted blob: `e59ffa1253d8e5afb075a26b8d5a061da73eb31a`
- Current blob: `e59ffa1253d8e5afb075a26b8d5a061da73eb31a`
- Source: [`native/engine/VaultConfig.h`](../../native/engine/VaultConfig.h)

```text
// VaultConfig — the user-intent store (Slice 2). Holds the decisions the user
// makes about their Vault: which folders are roots (and whether their founding
// card has been confirmed), per-subtree kind overrides (the card's chip
// reassignments), the scanIgnore needles, and hidden items. Never written by
// the scanner — that side owns the rebuildable index (VaultIndex, Slice 3); the
// config/index separation is the Groundworks contract.
//
// File-backed JSON at <vaultDir>/config.json, atomic + last-known-good via
// VaultStoreIo (recoverable if a write is torn). The constructor takes the vault
// directory so tests point it at a QTemporaryDir and production passes
// <appdata>/vault; nothing here touches QStandardPaths directly.
```

<a id="file-native-engine-vaultdownloadsroot-cpp"></a>
## `native/engine/VaultDownloadsRoot.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `5d23cd52cad7f206e3c8b735d9c1252e05c948d1`
- Current blob: `5d23cd52cad7f206e3c8b735d9c1252e05c948d1`
- Source: [`native/engine/VaultDownloadsRoot.cpp`](../../native/engine/VaultDownloadsRoot.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultdownloadsroot-h"></a>
## `native/engine/VaultDownloadsRoot.h`

- Status: **CURRENT**
- Accepted blob: `e865d9f59e6557e4467e88a54deb2a3abc8b5aac`
- Current blob: `e865d9f59e6557e4467e88a54deb2a3abc8b5aac`
- Source: [`native/engine/VaultDownloadsRoot.h`](../../native/engine/VaultDownloadsRoot.h)

```text
// VaultDownloadsRoot — Slice 18. Derives the synthetic downloads root's
// VaultIndex::FileRows from Colosseum's own download backbones, so the Vault
// can shelf downloads as ONE quiet, pre-confirmed root alongside user folders.
//
// This class is a pure JOIN over the backbones' published read APIs. It reads
// the download indices + localPages()/localBook() to recover the on-disk paths
// of CONTAINER downloads (videos, CBZ comics, CBZ tankoban volumes, epub/pdf
// books). It shelves ONLY container files — the Vault scanner + launch router
// classify by file extension and cannot open loose .jpg page dirs, so manga
// chapters stay on the Downloads page exactly as today.
//
// Backbone calls go through QMetaObject::invokeMethod (the same path QML uses),
// which decouples this class from the concrete backbone types and makes it
// unit-testable with trivial QObject fakes that expose matching Q_INVOKABLE
// slots. This class EDITS NOTHING on the Downloads lane — the hard constraint
// from the handoff. It reads; the Downloads page stays the transfer surface.
```

<a id="file-native-engine-vaultenricher-cpp"></a>
## `native/engine/VaultEnricher.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `846336c9ddf88885da08a7611e8e2ebcdf8be549`
- Current blob: `846336c9ddf88885da08a7611e8e2ebcdf8be549`
- Source: [`native/engine/VaultEnricher.cpp`](../../native/engine/VaultEnricher.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultenricher-h"></a>
## `native/engine/VaultEnricher.h`

- Status: **CURRENT**
- Accepted blob: `1c5db2a35ba9a30a0d71ad76fcdef4fd22f6d4d4`
- Current blob: `1c5db2a35ba9a30a0d71ad76fcdef4fd22f6d4d4`
- Source: [`native/engine/VaultEnricher.h`](../../native/engine/VaultEnricher.h)

```text
// VaultEnricher — fills rows with honest facts, progressively, after the census
// (Slice 5). Deliberately THIN: it reuses Colosseum's existing archive + cover
// facilities instead of re-porting Tankoban 2's ArchiveReader —
//   comics: CbzArchive lists the pages; the cover ENTRY name it picks is served
//           on demand by the existing image://comiccover/ provider (no new
//           decoder, no thumbnail cache — Qt's image cache already memoises);
//   video:  ffprobe (kill-on-timeout), memoised in a triple-keyed duration cache;
//   books:  format from the extension, plus bounded EPUB OPF metadata/cover
//           extraction when the file is an EPUB.
// Non-EPUB books remain filename-honest; video thumbnails and page dimensions
// stay deferred. Enrichment is per-file and cancellable; the duration cache
// flushes every 20 files.
```

<a id="file-native-engine-vaultidentifier-cpp"></a>
## `native/engine/VaultIdentifier.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `decb2ee167cf0586e84602f910cf7a6a07e0bd6d`
- Current blob: `decb2ee167cf0586e84602f910cf7a6a07e0bd6d`
- Source: [`native/engine/VaultIdentifier.cpp`](../../native/engine/VaultIdentifier.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultidentifier-h"></a>
## `native/engine/VaultIdentifier.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `2c82c67f00d9f208c8d504e8c24113c03882c482`
- Current blob: `2c82c67f00d9f208c8d504e8c24113c03882c482`
- Source: [`native/engine/VaultIdentifier.h`](../../native/engine/VaultIdentifier.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultidentity-cpp"></a>
## `native/engine/VaultIdentity.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `0ecef460a3c39fab223a46c699da005764e29373`
- Current blob: `0ecef460a3c39fab223a46c699da005764e29373`
- Source: [`native/engine/VaultIdentity.cpp`](../../native/engine/VaultIdentity.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultidentity-h"></a>
## `native/engine/VaultIdentity.h`

- Status: **CURRENT**
- Accepted blob: `5ef9527925152dae76e2836a33b776034c1278b1`
- Current blob: `5ef9527925152dae76e2836a33b776034c1278b1`
- Source: [`native/engine/VaultIdentity.h`](../../native/engine/VaultIdentity.h)

```text
// VaultIdentity — the content-addressed file identity registry (Slice 2). Gives
// every local file a stable id so its progress survives rename/move/restart
// (spec §8). The id is `vault:` + SHA-1 of `normalizedPath::size::mtimeMs`
// (Groundworks contract, decision 2 of the plan), so a plain rename yields a NEW
// computed id — and reconcile() re-attaches it: a known id whose file has
// vanished, matched by an UNIQUE fresh file of the same (size, mtimeMs)
// signature, is aliased to the new file so progress follows. Ambiguous matches
// (two candidates) are left parked, never silently merged — the "same content,
// new location" ceremony is Slice 21.
//
// File-backed JSON at <vaultDir>/identity.json via VaultStoreIo. Progress keys
// to resolve(computeId(...)); the recorded path aliases are the Reader 2 bridge
// hook (Slice 16 pairs them with BookStores::keyFor to migrate book state).
```

<a id="file-native-engine-vaultindex-cpp"></a>
## `native/engine/VaultIndex.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `44b7218caf228d747fc112fd34a5b8baa4a3080c`
- Current blob: `44b7218caf228d747fc112fd34a5b8baa4a3080c`
- Source: [`native/engine/VaultIndex.cpp`](../../native/engine/VaultIndex.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultindex-h"></a>
## `native/engine/VaultIndex.h`

- Status: **CURRENT**
- Accepted blob: `590b2e2b0f9bc8edb69fbccecae2214571af424a`
- Current blob: `590b2e2b0f9bc8edb69fbccecae2214571af424a`
- Source: [`native/engine/VaultIndex.h`](../../native/engine/VaultIndex.h)

```text
// VaultIndex — the rebuildable scan product (Slice 3). The queryable truth the
// Vault UI paints from: every discovered file with its kind, group (series/show
// folder), real name, and a numeric-aware sort key, plus the counts and folder
// listings the shelves and folder view need. It is a PRODUCT of scanning, never
// user intent — that half is VaultConfig (Slice 2); the config/index separation
// is the Groundworks contract.
//
// SQLite at a caller-given path (<appdata>/vault/index-v1.sqlite in production,
// a QTemporaryDir file in tests). A full publish() replaces the whole index in
// ONE transaction, so a crash or a cancelled scan rolls back and leaves the
// previous contents intact (decision 4) — the atomic-publish discipline of
// BiblioCatalogStore, reduced to a single transactional replace since the Vault
// index keeps no history. upsert() lands a single live-shelf arrival (Slice 15)
// without a full republish.
//
// Deploy note (ledger): a Qt SQL harness finds qsqlite.dll only when it runs
// from build-msvc/ where the app deployed it — the test target lands there.
```

<a id="file-native-engine-vaultkit-cpp"></a>
## `native/engine/VaultKit.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `9e6d0a84fa6c5eab4c44469994d9de2446b9bd2b`
- Current blob: `9e6d0a84fa6c5eab4c44469994d9de2446b9bd2b`
- Source: [`native/engine/VaultKit.cpp`](../../native/engine/VaultKit.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultkit-h"></a>
## `native/engine/VaultKit.h`

- Status: **CURRENT**
- Accepted blob: `ece1672123cbb28cf8bfeb7f14f841104b06c76e`
- Current blob: `ece1672123cbb28cf8bfeb7f14f841104b06c76e`
- Source: [`native/engine/VaultKit.h`](../../native/engine/VaultKit.h)

```text
// VaultKit — the Vault's pure-logic kit. Slice 1 of the Vault execution plan
// (Brotherhood/docs/superpowers/plans/2026-08-08-colosseum-vault-execution-plan.md).
//
// Ported from Tankoban 2's src/core/ScannerUtils (the depth-bounded,
// symlink-loop-safe, cooperatively-cancellable walker; the hard-coded
// ignore-dir set; first-level grouping with loose-file capture; and the
// media-folder title cleaner) and EXTENDED with three things the Vault needs
// that TB2 kept scattered:
//   (a) the Groundworks user `scanIgnore` needle layer (a case-insensitive
//       full-path SUBSTRING match — books_library_handlers.py), sanitized and
//       capped, threaded through every walk;
//   (b) a census classifier that infers each first-level subtree's media kind
//       and flags mixed leaves (spec §5, "one folder one kind"); and
//   (c) the season/episode grammar extracted out of TB2's BulkPackVerifier
//       regex and VideosPage::resolveShowPath season-climb guard, so a bare
//       `Season 1` folder never masquerades as a show.
//
// Pure QtCore only (QDir / QFileInfo / QRegularExpression) — no QObject, no app
// dependencies — so a Qt Test links it standalone and the production scanners
// (Slice 4+) #include it the same way. QML paints, C++ decides.
```

<a id="file-native-engine-vaultlibrary-cpp"></a>
## `native/engine/VaultLibrary.cpp`

- Status: **CURRENT**
- Accepted blob: `67fd7b6bd77132ed7573153cd0e5efb0dbdef85e`
- Current blob: `67fd7b6bd77132ed7573153cd0e5efb0dbdef85e`
- Source: [`native/engine/VaultLibrary.cpp`](../../native/engine/VaultLibrary.cpp)

```text
// Mirror VaultConfig::norm so an offered-root key matches the normalized path in roots().
```

<a id="file-native-engine-vaultlibrary-h"></a>
## `native/engine/VaultLibrary.h`

- Status: **CURRENT**
- Accepted blob: `7edf545afe670e43ed26e32f52c11756b02c1c1e`
- Current blob: `7edf545afe670e43ed26e32f52c11756b02c1c1e`
- Source: [`native/engine/VaultLibrary.h`](../../native/engine/VaultLibrary.h)

```text
// VaultLibrary — the Vault's single QML façade: the Slice-10 read-model plus the Slice-11
// scan/confirm commands. QML paints from THIS object and fires gestures AT it; C++ owns the
// scan/publish threading and the multi-step confirm sequence, so QML never sequences
// addRoot→scan or setKind→confirm→publish itself (QML paints, C++ decides). It wraps
// VaultIndex (queryable truth), VaultScanner (the cancellable off-thread census + aggregate
// publish), and VaultConfig (user intent: roots + kind overrides). The read half mirrors
// LocalDownloads (revision + series + items) so shelves only render normalized results.
//
// revision bumps ONLY on committed truth (VaultIndex::changed(), emitted after a successful
// publish/upsert). scanning/scanningRoot drive the scan pill; candidate drives the confirmation
// card. Commands: addFolder (add an unconfirmed root + census it), confirmRoot (persist the
// card's chip reassignments, mark confirmed, publish the UNION of all confirmed roots),
// dismissCard, cancelScan.
```

<a id="file-native-engine-vaultpagestore-cpp"></a>
## `native/engine/VaultPageStore.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `087067b196b5dba951102f95642944bb0b826b09`
- Current blob: `087067b196b5dba951102f95642944bb0b826b09`
- Source: [`native/engine/VaultPageStore.cpp`](../../native/engine/VaultPageStore.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultpagestore-h"></a>
## `native/engine/VaultPageStore.h`

- Status: **CURRENT**
- Accepted blob: `9f8b6ba91e27d02b11523f6f81f8a8e83f3207c4`
- Current blob: `9f8b6ba91e27d02b11523f6f81f8a8e83f3207c4`
- Source: [`native/engine/VaultPageStore.h`](../../native/engine/VaultPageStore.h)

```text
// VaultPageStore — the comic-reader adapter for local archives (Slice 7). It
// satisfies ComicReaderShell's injected-store contract (`store.localPages(id)`)
// so a Vault CBZ opens in ComicReader 2 with ZERO reader edits: it returns the
// SAME "direct archive descriptors" shape the Tankoban volume lane
// (MangaVolumeIndex) returns —
//     [{index, archive, entry, group}]
// — which the reader decodes in place, without extraction. The `id` passed in is
// the archive path.
```

<a id="file-native-engine-vaultrecent-cpp"></a>
## `native/engine/VaultRecent.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `97fe5a3970cfbce5f7804d1e851fd93abc67da17`
- Current blob: `97fe5a3970cfbce5f7804d1e851fd93abc67da17`
- Source: [`native/engine/VaultRecent.cpp`](../../native/engine/VaultRecent.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultrecent-h"></a>
## `native/engine/VaultRecent.h`

- Status: **CURRENT**
- Accepted blob: `4aed66fab64dde96c8fac990894f58f65861aff1`
- Current blob: `4aed66fab64dde96c8fac990894f58f65861aff1`
- Source: [`native/engine/VaultRecent.h`](../../native/engine/VaultRecent.h)

```text
// VaultRecent — the Open Media "recent files" store (execution plan Slice 9). A small
// file-backed list of the local files most recently opened, so the Open Media control can
// offer one-click reopen. Most-recent-first, deduped by normalized path (reopening a file
// moves it to the front), capped at kMax.
//
// It records ONLY shortcuts — path + cleaned title + kind + content id — NEVER reading
// progress, which lives in its own store. Clearing this wipes the shortcuts and leaves
// reading progress untouched (spec §2). File-backed JSON at <vaultDir>/open-recent.json,
// atomic + last-known-good via VaultStoreIo, seedable for tests. Pure Qt Core; the ctor
// takes the vault directory so a test points it at a QTemporaryDir and production passes
// <appdata>/vault — nothing here touches QStandardPaths.
```

<a id="file-native-engine-vaultscanner-cpp"></a>
## `native/engine/VaultScanner.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `44c6b2adad7939ecdd9e0fc5533579589ad8d534`
- Current blob: `44c6b2adad7939ecdd9e0fc5533579589ad8d534`
- Source: [`native/engine/VaultScanner.cpp`](../../native/engine/VaultScanner.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultscanner-h"></a>
## `native/engine/VaultScanner.h`

- Status: **CURRENT**
- Accepted blob: `0fd2c76c11d5fdf816349f71b6bef88bb4271994`
- Current blob: `0fd2c76c11d5fdf816349f71b6bef88bb4271994`
- Source: [`native/engine/VaultScanner.h`](../../native/engine/VaultScanner.h)

```text
// VaultScanner — the cancellable off-thread census (Slice 4). Turns a root path
// into a census: kind-pure slices for the confirmation card (Slice 11) and
// per-file rows for VaultIndex, off the GUI thread so a big drive never freezes
// the app. The filesystem walk + classify runs on a pool thread (the slow part);
// the thread-affine work — VaultIdentity reconcile + VaultIndex publish — runs
// back on the GUI thread. Modelled on ComicDownloader::runPackOrCopyThenPublish:
// by-value captures into the pool, a generation guard so a stale result from a
// superseded scan is dropped, and results marshalled back via QFutureWatcher.
//
// Testability: buildScan() is a pure static census (synchronous, no threads) and
// applyResult() is the generation-guarded commit — both are driven directly by
// the Qt Test. scanRoot()/cancel() are the thin async wrapper.
```

<a id="file-native-engine-vaultstoreio-h"></a>
## `native/engine/VaultStoreIo.h`

- Status: **CURRENT**
- Accepted blob: `b7bdbd637a7f9ad4f58425a7c445a656f65e5fd0`
- Current blob: `b7bdbd637a7f9ad4f58425a7c445a656f65e5fd0`
- Source: [`native/engine/VaultStoreIo.h`](../../native/engine/VaultStoreIo.h)

```text
// VaultStoreIo — the Vault stores' shared atomic JSON persistence (Slice 2).
//
// One place for the "write can't corrupt what's there" discipline both Vault
// stores need (spec §8): every write goes to a temp file, the current good file
// rotates to `<name>.bak` (last-known-good), then the temp is promoted; every
// read falls back to `.bak` and then to empty when the primary is unreadable.
// Header-only inline free functions — no extra translation unit, and the Qt
// Test exercises the recovery paths directly.
```

<a id="file-native-engine-vaultwatcher-cpp"></a>
## `native/engine/VaultWatcher.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `d4347d05f0f49f379b8b2de8cddbe4d7c75eb869`
- Current blob: `d4347d05f0f49f379b8b2de8cddbe4d7c75eb869`
- Source: [`native/engine/VaultWatcher.cpp`](../../native/engine/VaultWatcher.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-vaultwatcher-h"></a>
## `native/engine/VaultWatcher.h`

- Status: **CURRENT**
- Accepted blob: `427c9efa42e5f0a329918795da74608375b6d8fc`
- Current blob: `427c9efa42e5f0a329918795da74608375b6d8fc`
- Source: [`native/engine/VaultWatcher.h`](../../native/engine/VaultWatcher.h)

```text
// VaultWatcher — the live-shelf arrival engine (execution plan Slice 15). Watches every
// CONFIRMED Vault root with a QFileSystemWatcher (the only other in-repo use is the dev QML
// reloader in main.cpp — the reference pattern), debounces arrival storms into one pass, and
// turns what changed into incremental VaultIndex upserts: a file dropped into a watched root
// lands on the right shelf within seconds — no card, no action.
//
// One debounced pass over a dirty root = processRoot(): enumerate the root under the SAME
// VaultKit laws the census uses (groupByFirstLevelSubdir + kindForFile + scanIgnore needles),
// build rows byte-identical to VaultScanner::buildScan's, diff against the ids already in the
// index (exact upsert set = the arrivals only), upsertMany() the delta (one repaint), and
// law-check every arrival: a file whose kind disagrees with the subtree's law (a config chip
// override first, else the index's dominant kind) is a NEW-KIND arrival → a one-slice
// confirmation card (S11 law) so the user can chip-reshelve it. Watcher failure (QFSW limits,
// network drives, vanished roots) sets a per-root degraded flag; the Vault rescan-on-open
// path (VaultLibrary::rescanDegradedRoots) covers those roots silently.
//
// Behavior to preserve: while an immersive surface (player/reader) is open, the debounce may
// keep accumulating but NO upsert/repaint happens — processRoot defers until setImmersive(false)
// flushes. Row construction must stay in lockstep with buildScan so a later full rescan
// reproduces the same rows (the index is a rebuildable product).
//
// Testability: processRoot() is the synchronous seam — a Qt Test drives it directly with a
// QTemporaryDir root (touch files → exact upsert set; new-kind → card slices; a nonexistent
// root → degraded flag via refresh()).
```

<a id="file-qml-vaultapi-js"></a>
## `qml/VaultApi.js`

- Status: **CURRENT**
- Accepted blob: `2aca05797048bb8ce321024e870faaf06a47dea5`
- Current blob: `2aca05797048bb8ce321024e870faaf06a47dea5`
- Source: [`qml/VaultApi.js`](../../qml/VaultApi.js)

```text
// VaultApi — the pure derivation the Vault surfaces share (Slice 14): join the rebuildable index
// rows against the live Progress store, and (VaultApi.continueRail, added with the rail) derive the
// Vault Continue list. It owns no backend; Progress is passed in as an argument so this stays a
// .pragma library with no context capture.
//
// THERE IS NO KIND TRANSLATION HERE, DELIBERATELY. The Vault files each item under its canonical
// kind (comic | book | video), and — after the Slice 14 source-label fix in VaultComicReader — every
// reader/player persists Progress under that SAME kind (comics "comic" not "manga"; books "book";
// video "video"). So the join is a straight Progress.get(row.kind, row.id). If a future reader
// regresses to a non-canonical kind, fix the WRITER, not this file: a translation map here would
// silently leak local items into a catalogue namespace (the hazard Preflight flagged for the
// translate-at-the-boundary approach this fix supersedes).
```

<a id="file-qml-vaultbrowsecrumb-qml"></a>
## `qml/VaultBrowseCrumb.qml`

- Status: **CURRENT**
- Accepted blob: `d3a1ebd5c0b2629b130862368d3d2164f8c74422`
- Current blob: `d3a1ebd5c0b2629b130862368d3d2164f8c74422`
- Source: [`qml/VaultBrowseCrumb.qml`](../../qml/VaultBrowseCrumb.qml)

```text
// VaultBrowseCrumb — the Vault Browse face's breadcrumb (locked design §4.5): current path
// from the selected root down to the current folder/show/season. Middle segments collapse once
// the path runs "far too many" deep; first and last are always visible (design §4.5 occupancy
// table). A non-last, non-collapsed segment is clickable to ascend directly to that level.
```

<a id="file-qml-vaultbrowseempty-qml"></a>
## `qml/VaultBrowseEmpty.qml`

- Status: **CURRENT**
- Accepted blob: `981c88c6ae6dd4d89c08fcf5d97c7c6e498d0084`
- Current blob: `981c88c6ae6dd4d89c08fcf5d97c7c6e498d0084`
- Source: [`qml/VaultBrowseEmpty.qml`](../../qml/VaultBrowseEmpty.qml)

```text
// VaultBrowseEmpty — the Vault Browse face's empty-state family (locked design §4.5, execution
// plan Slice 9). Four distinct causes, each with its OWN copy — the design's own requirement is
// that they never share wording, because only one of the four is actually a problem. The CAUSE is
// a fact the C++ projection computes (VaultLibrary::browseEmptyCause) — this component only
// paints it, never infers it. Copy verbatim from the approved mock
// (Brotherhood/agents/colosseum-vault-browse-face-mock.html, plate 6). No taglines.
//
// The fourth cause ("filtered") renders here for completeness and Quick Test coverage, but no
// production trigger sets `cause` to it — no filter control has shipped on the Browse face yet
// (deferred to the parent design's later arc). See VaultPage.qml's wiring comment for the honest
// account of which causes are actually reachable live.
```

<a id="file-qml-vaultbrowserail-qml"></a>
## `qml/VaultBrowseRail.qml`

- Status: **CURRENT**
- Accepted blob: `d190ad59c9c8567e15fb78d110dfe529f83b1a28`
- Current blob: `d190ad59c9c8567e15fb78d110dfe529f83b1a28`
- Source: [`qml/VaultBrowseRail.qml`](../../qml/VaultBrowseRail.qml)

```text
// VaultBrowseRail — the Vault Browse face's collapsible root rail (locked design decision #10,
// execution plan Slice 5). Collapsed by default: each confirmed/synthetic root renders as a
// glyph with its availability dot; expanding adds names and counts only — it never reveals
// state that was hidden while collapsed (design §4.1: "Expanding never reveals state that was
// hidden — only detail"). Also carries the two capability affordances the old marquee/tab-bar
// owned: Add storage (→ existing addFolder) and the reversible Hidden shelf.
```

<a id="file-qml-vaultbrowsestate-js"></a>
## `qml/VaultBrowseState.js`

- Status: **CURRENT**
- Accepted blob: `ec4d6e94192255fa968f5d869c95c18bed8ea210`
- Current blob: `ec4d6e94192255fa968f5d869c95c18bed8ea210`
- Source: [`qml/VaultBrowseState.js`](../../qml/VaultBrowseState.js)

```text
// VaultBrowseState — Vault Browse face session memory (execution plan Slice 5).
// A `.pragma library` module's top-level state survives across the vaultLayer Loader's
// repeated destroy/recreate cycles (leaving Vault via the taskbar and returning deactivates
// then reactivates the Loader, destroying VaultPage.qml's item each time) for as long as the
// PROCESS lives — exactly the "within a session" half of the locked design's persistence
// contract (§4.8). It deliberately does NOT survive an app restart (module state resets with
// the process); current folder + rail-expanded ARE required to survive a restart, and those
// live in VaultPage.qml's own `Settings { category: "vaultBrowseV1" }` block (registry-backed,
// tag-isolated under COLOSSEUM_APPDATA_TAG the same way every other Colosseum store is).
```

<a id="file-qml-vaultconfirmcard-qml"></a>
## `qml/VaultConfirmCard.qml`

- Status: **CURRENT**
- Accepted blob: `757f0001bec67cdf4bf4f5b61ae6951b647561d1`
- Current blob: `757f0001bec67cdf4bf4f5b61ae6951b647561d1`
- Source: [`qml/VaultConfirmCard.qml`](../../qml/VaultConfirmCard.qml)

```text
// VaultConfirmCard — the founding ceremony's ONE card (Slice 11), built to the locked mock
// agents/colosseum-vault-confirm-card-mock.html. After a new folder's first census it shows
// show-your-work sorting: one row per discovered subtree slice (path · what · sample) with a
// reassignable gold kind chip, the honest leftover line, and consent as one gold button.
//
// A SEEDABLE component (like OpenRecentPanel): it owns no backend. It takes the candidate
// `model` (VaultLibrary.candidate slices) + `rootPath`, and emits shelveRequested(kindOverrides)
// / dismissRequested — VaultPage wires those to VaultLibrary.confirmRoot / dismissCard. So a Qt
// Quick Test drives it with a seeded model, no app.
//
// Slice model row shape (VaultScanner census): { subtreePath, groupTitle, kind, count, mixed,
// loose, leftoverCount, [seriesCount], [sample], [sizeBytes] }. The bracketed fields are the
// scanner-model enrichment that fills the "· N series", sample line, and size count to full mock
// parity; the card renders what is present and stays honest when they are absent.
```

<a id="file-qml-vaultdetailsheet-qml"></a>
## `qml/VaultDetailSheet.qml`

- Status: **CURRENT**
- Accepted blob: `ee61df2d82448ff381777aeb576393f36e6545ec`
- Current blob: `ee61df2d82448ff381777aeb576393f36e6545ec`
- Source: [`qml/VaultDetailSheet.qml`](../../qml/VaultDetailSheet.qml)

```text
// VaultDetailSheet — the Vault Browse face's detail sheet (execution plan Slice 7), built to the
// locked design's decision #11 and the approved mock's plate 4: opening a film answers "what do
// I physically hold" — every copy with its drive, its companions, its extras, and why Vault
// believes the identity it does. Deliberately never cast, synopsis, or related titles; the locked
// design gives that away to Theatre in three separate places.
//
// A SAME-WINDOW surface (a plain Item overlay inside VaultPage, the VaultConfirmCard/
// VaultFolderView convention), never a Window/Popup that would own its own platform window — the
// Lanista bridge structurally cannot see a secondary window (ledger law). Seedable, like its
// siblings: it takes `detail` (VaultLibrary.browseDetail()'s returned map) and emits
// backRequested / playRequested(path) / revealRequested(path) / identifyRequested(key) /
// unidentifyRequested(key) / hideRequested(key) — VaultPage wires those to VaultLibrary + the
// existing openMediaRequested path. So a Qt Quick Test drives it with a seeded map, no app.
```

<a id="file-qml-vaultdoor-qml"></a>
## `qml/VaultDoor.qml`

- Status: **CURRENT**
- Accepted blob: `8da75ae843de0c4b3dbbc3e0cd4f63244db6705a`
- Current blob: `8da75ae843de0c4b3dbbc3e0cd4f63244db6705a`
- Source: [`qml/VaultDoor.qml`](../../qml/VaultDoor.qml)

```text
// VaultDoor.qml — the taskbar's permanent folder door: the always-present "On this machine"
// entry (Slice 10) plus the alive-door state (Slice 15): a quiet gold dot while any scan runs,
// and a time-boxed "arrival" glow the moment a dropped file lands on a live shelf. No counts,
// no badges (spec §3) — the door only pulses and glows.
//
// The state machine lives HERE (QML-side) so the Qt Quick Test can seed scanning/arrivalTick
// and assert the doorState sequence (idle → scanning → arrival-pulse → idle); the FACTS come
// from VaultLibrary (scanning + the monotone arrivalTick landing clock) wired by Taskbar.
// QML paints, C++ decides: C++ only ever bumps arrivalTick / toggles scanning.
```

<a id="file-qml-vaultfolderview-qml"></a>
## `qml/VaultFolderView.qml`

- Status: **CURRENT**
- Accepted blob: `bcd534ee23f3b0a62df8fb3e7ba86c264e77f3a7`
- Current blob: `bcd534ee23f3b0a62df8fb3e7ba86c264e77f3a7`
- Source: [`qml/VaultFolderView.qml`](../../qml/VaultFolderView.qml)

```text
// VaultFolderView — the Vault's file-first detail surface (Slice 13), built to the locked mock
// agents/colosseum-vault-folder-view-mock.html. Click a shelf tile → this surface: a sticky
// preview pane (cover/gradient, kind eyebrow, title, disk facts, doors) on the left, and on the
// right the folder's REAL files exactly as they sit on disk — cleaned titles with the real
// filename faint beneath, real subfolders as descendable folder rows, natural order, and honest progress
// hairline. Identification decorates the pane (later slices); it never restructures the list.
//
// A SEEDABLE component (like VaultConfirmCard / OpenRecentPanel): it owns no backend. It takes a
// flat `model` of file rows (VaultLibrary.items(kind, seriesKey)) + `facts` for the pane, and emits
// backRequested / revealRequested(path) / openRequested(row) / continueRequested — VaultPage wires
// those to navigation, the Reveal invocable, and (Slice 14) LocalLaunch. So a Qt Quick Test drives
// it with a seeded model, no app.
//
// Row shape (VaultIndex::filesInSubtree): { id, path, displayTitle, realName, subfolder, kind,
// size, mtimeMs, pages, durationSec, author, format, progressed, coverRef }. Progress percentage and
// last-read time come from Progress (Slice 14), not the index — so the hairline and the last-read
// completion come from the live QML join, not the immutable index.
```

<a id="file-qml-vaultidentifydialog-qml"></a>
## `qml/VaultIdentifyDialog.qml`

- Status: **CURRENT**
- Accepted blob: `9a92dd7ec540fbb869f54f355875e1232a73d921`
- Current blob: `9a92dd7ec540fbb869f54f355875e1232a73d921`
- Source: [`qml/VaultIdentifyDialog.qml`](../../qml/VaultIdentifyDialog.qml)

```text
// VaultIdentifyDialog — the small explicit identity gesture. The native VaultIdentifier remains
// the certainty gate; this surface is deliberately honest when no single offline candidate exists.
```

<a id="file-qml-vaultidentityceremonydialog-qml"></a>
## `qml/VaultIdentityCeremonyDialog.qml`

- Status: **CURRENT**
- Accepted blob: `9efebf3f3977995607bd7b236f3e31497a236a00`
- Current blob: `9efebf3f3977995607bd7b236f3e31497a236a00`
- Source: [`qml/VaultIdentityCeremonyDialog.qml`](../../qml/VaultIdentityCeremonyDialog.qml)

```text
// Shared identity ceremony surface for launch sessions and Vault rows. It is seedable so
// store decisions stay testable without booting the whole shell; the owning facade supplies
// the ceremony and records the choice in VaultIdentity.
```

<a id="file-qml-vaultpage-qml"></a>
## `qml/VaultPage.qml`

- Status: **CURRENT**
- Accepted blob: `d1b450dd4ed408192306702b7407f62d43f74ada`
- Current blob: `d1b450dd4ed408192306702b7407f62d43f74ada`
- Source: [`qml/VaultPage.qml`](../../qml/VaultPage.qml)

```text
// VaultPage — "On this machine": the local-media Vault as a host-owned full page, entered from
// the taskbar folder door. Slice 10 lands the permanent door + this page's EMPTY state (nothing
// indexed yet): eyebrow, title, and a dashed Add-folder drop surface. It paints from the
// VaultLibrary read-model (itemCount/scanning); the shelves that fill a populated Vault, and the
// folder-scan ingest behind Add folder, land in Slice 11. Same chrome vocabulary as
// Settings/Downloads (back · minimize · fullscreen · power) so it reads as one of the house's pages.
```

<a id="file-qml-vaultpostercard-qml"></a>
## `qml/VaultPosterCard.qml`

- Status: **CURRENT**
- Accepted blob: `d338e14fd267b8ebe3d9133e23abce01f1c3df24`
- Current blob: `d338e14fd267b8ebe3d9133e23abce01f1c3df24`
- Source: [`qml/VaultPosterCard.qml`](../../qml/VaultPosterCard.qml)

```text
// VaultPosterCard — the 2:3 poster face for folder/show/season/film rows (Vault Browse face,
// execution plan Slice 4). Consumes ONE browseAt() row and paints the locked design's card
// language (design §6.3) exactly: artwork edge to edge with nothing printed over it; a centered
// one-line title below with elision; a dimmer physical-fact line beneath, in the slot Jellyfin
// uses for the year; near-square corners (~5px); circular corner indicators, never rectangular
// badges; hover dims the art and reveals a play affordance; gold reserved for the uncertainty
// mark only; away reads as reduced ink + desaturation with no hover and no open signal;
// resolving shows the filename on plain ground and crossfades to the settled face in place.
//
// UNWIRED (Slice 4 scope): no page instantiates this yet. Slice 5 assembles the grid. `state`,
// `displayTitle` and `physicalFact` are exposed as readable properties on purpose — they are the
// Lanista/Quick-Test vocabulary Slices 5-9 read.
```

<a id="file-qml-vaulttile-qml"></a>
## `qml/VaultTile.qml`

- Status: **CURRENT**
- Accepted blob: `ba50416afb53e2dfcbc14cfdde77bfacd384e6ef`
- Current blob: `ba50416afb53e2dfcbc14cfdde77bfacd384e6ef`
- Source: [`qml/VaultTile.qml`](../../qml/VaultTile.qml)

```text
// VaultTile — the shared shelf/folder tile. It keeps unavailable roots in the gallery and paints
// their state in place instead of letting a missing filesystem path erase the user's shelf.
```

<a id="file-qml-vaultwidecard-qml"></a>
## `qml/VaultWideCard.qml`

- Status: **CURRENT**
- Accepted blob: `c802bc0f35004d4cb3fe44b443cfc0332c26f2c6`
- Current blob: `c802bc0f35004d4cb3fe44b443cfc0332c26f2c6`
- Source: [`qml/VaultWideCard.qml`](../../qml/VaultWideCard.qml)

```text
// VaultWideCard — the 16:9 still face for episode/clip rows (Vault Browse face, execution plan
// Slice 4). Same card language as VaultPosterCard (design §6.3) — artwork edge to edge, centered
// one-line title with elision, a dimmer physical-fact line beneath, circular corner indicators,
// near-square corners, hover dims and reveals a play affordance, gold reserved for the
// uncertainty mark, away is reduced ink + desaturation with no hover/open, resolving crossfades
// filename → settled face in place — just a different card, per the locked design's rule that
// drilling into a series is a different card, not the same grid one level deeper (§6.3).
//
// UNWIRED (Slice 4 scope): no page instantiates this yet. Slice 5 assembles the grid. `state`,
// `displayTitle` and `physicalFact` are exposed as readable properties on purpose — they are the
// Lanista/Quick-Test vocabulary Slices 5-9 read.
```
