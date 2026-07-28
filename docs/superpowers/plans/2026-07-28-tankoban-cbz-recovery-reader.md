# Tankoban CBZ Recovery and Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore Tankoban 2's CBZ-only volume lifecycle, recover corrupt legacy
volume rows without data loss, read CBZ pages directly, and make the comic
reader cursor return with its HUD.

**Architecture:** A miniz-backed `CbzArchive` is the single ZIP boundary for
atomic creation, listing, and per-entry reads. `MangaVolumeIndex` stores
archive-backed rows with per-volume sidecars and migrates verified legacy
directories before pruning. `ComicReaderDecode` adds CBZ-entry bytes as a source
while retaining local files for non-Tankoban callers.

**Tech Stack:** C++20, Qt 6 Core/Gui/QML, vendored miniz 3.0.0, CMake/MSVC,
QML harnesses.

## Global Constraints

- Tankoban durable volume storage is CBZ-only.
- Never remove a loose legacy directory until its CBZ, sidecar, and global
  ledger row have all been written and the CBZ has been reopened successfully.
- Do not mutate live AppData while Colosseum is running.
- Preserve unrelated dirty worktree files.
- Every production behavior begins with a failing harness assertion.

---

### Task 1: CBZ archive primitive

**Files:**
- Create: `native/engine/CbzArchive.h`
- Create: `native/engine/CbzArchive.cpp`
- Create: `tests/cbz_archive_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `CbzArchive::imageEntries`, `CbzArchive::readEntry`, and
  `CbzArchive::writeImagesAtomic` exactly as specified in the design.

- [ ] **Step 1: Write the failing archive harness**

Create two images named `page_2.png` and `page_10.jpg`, write them to a CBZ,
assert natural list order, assert exact entry bytes, and assert a failed write
does not replace an existing archive.

- [ ] **Step 2: Run RED**

Run:
`cmake --build native/build-msvc --config Debug --target cbz_archive_harness`

Expected: FAIL because the target/API does not exist.

- [ ] **Step 3: Implement the minimal miniz wrapper**

Use `mz_zip_writer_init_file`, `mz_zip_writer_add_file` with
`MZ_NO_COMPRESSION`, `mz_zip_writer_finalize_archive`,
`mz_zip_reader_init_file`, `mz_zip_reader_file_stat`, and
`mz_zip_reader_extract_to_heap`. Validate and rename `.part` only after reopen.

- [ ] **Step 4: Run GREEN**

Run:
`cmake --build native/build-msvc --config Debug --target cbz_archive_harness; native/build-msvc/cbz_archive_harness.exe`

Expected: build exit 0 and harness exit 0.

### Task 2: Repairable CBZ volume index and legacy migration

**Files:**
- Modify: `native/engine/MangaVolumeIndex.h`
- Modify: `native/engine/MangaVolumeIndex.cpp`
- Modify: `tests/manga_volume_index_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `archivePathFor(const VolumeProvenance&)`.
- Ledger rows use `archive`, `files`, `groups`, and `bytes`; legacy `dir` is
  accepted only for migration.
- `localPages()` emits `{index, archive, entry, group}` for Tankoban.

- [ ] **Step 1: Add failing recovery tests**

Cover: corrupt ledger extensions repaired from legacy `index.json`; successful
legacy-to-CBZ migration; source directory retained when archive creation fails;
sidecar-to-ledger reconciliation; unrecoverable row pruned without payload
deletion.

- [ ] **Step 2: Run RED**

Run:
`cmake --build native/build-msvc --config Debug --target manga_volume_index_harness; native/build-msvc/manga_volume_index_harness.exe`

Expected: the new assertions fail against prune-and-delete `heal()`.

- [ ] **Step 3: Implement sidecar serialization and repair-before-prune**

Parse legacy manifest first, verify all files, write/reopen CBZ, atomically save
sidecar and global ledger, then remove the old directory. Make `entryIntact`
validate CBZ existence and listed entries.

- [ ] **Step 4: Run GREEN**

Run the same build and harness command; expected exit 0.

### Task 3: CBZ-only ingestion for Nyaa and WeebCentral

**Files:**
- Modify: `native/engine/MangaVolumeArchiveIngestor.h`
- Modify: `native/engine/MangaVolumeArchiveIngestor.cpp`
- Modify: `native/engine/MangaVolumePacker.cpp`
- Modify: `tests/manga_volume_packer_harness.cpp`
- Modify: `tests/manga_tankoban_service_harness.cpp`

**Interfaces:**
- `ingestArchive` retains/copies valid CBZ directly into canonical storage.
- `publish` converts prepared WeebCentral staging pages into canonical CBZ.
- Non-ZIP input may use temporary extraction but publishes only CBZ.

- [ ] **Step 1: Add failing lifecycle assertions**

Assert both source paths end at `.cbz`, their archive page entries/groups match,
and no canonical loose-page directory survives successful publication.

- [ ] **Step 2: Run RED**

Run the packer and service harness targets; expected assertions fail because
current publication creates loose `page_NNN` files.

- [ ] **Step 3: Replace durable extraction with CBZ publication**

Keep temporary staging/extraction only until `CbzArchive::writeImagesAtomic`
succeeds. Publish the CBZ row and sidecar; consume source material afterward.

- [ ] **Step 4: Run GREEN**

Build and execute both harnesses; expected exit 0.

### Task 4: Direct CBZ reader decode and diagnostics

**Files:**
- Modify: `native/comicreader/ComicReaderTypes.h`
- Modify: `native/comicreader/ComicReaderTypes.cpp`
- Modify: `native/comicreader/ComicReaderCore.cpp`
- Modify: `native/comicreader/ComicReaderDecode.cpp`
- Modify: `tests/comicreader_core_harness.cpp`
- Modify: `tests/comicreader_decode_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Adds `PageSourceKind::{LocalFile,CbzEntry}` and archive fields to `PageMeta`.
- `parsePages` accepts `{archive,entry}`.
- Decode loads CBZ bytes before the existing image header/decode path.

- [ ] **Step 1: Add failing source and decode tests**

Open a real two-page CBZ descriptor list, request both pages, assert both decode,
and assert a missing archive entry reports `missing_file` with no crash.

- [ ] **Step 2: Run RED**

Build and run `comicreader_core_harness` and `comicreader_decode_harness`;
expected failure because archive descriptors are currently rejected.

- [ ] **Step 3: Implement source-aware byte loading**

Factor the current file read into a helper that returns bytes plus `PageError`.
For `CbzEntry`, call `CbzArchive::readEntry`; log archive and entry on failure.
Leave generation, cache, geometry, and pairing logic unchanged.

- [ ] **Step 4: Run GREEN**

Build and execute both harnesses; expected exit 0.

### Task 5: Persistent cursor owner

**Files:**
- Modify: `qml/comicreader/ComicReaderShell.qml`
- Modify: `tests/comicreader_chrome_harness.qml`

**Interfaces:**
- `cursorHideArea` remains enabled while chrome is visible and no modal is open.
- Its `cursorShape` binds directly to `chromeVisible`.
- Pointer movement calls `wakeChrome()`.

- [ ] **Step 1: Add failing QML assertions**

After idle state, assert BlankCursor. Set chrome visible and assert the same item
is still enabled and its cursor is ArrowCursor.

- [ ] **Step 2: Run RED**

Run the existing comic-reader chrome QML harness; expected failure because the
current area disables itself.

- [ ] **Step 3: Apply the Player 2 cursor ownership pattern**

Keep the area persistent, enable hover, bind shape to chrome visibility, and
wake chrome on position change without intercepting clicks.

- [ ] **Step 4: Run GREEN**

Run the harness again; expected exit 0.

### Task 6: Full verification and live migration

**Files:**
- Data migration only after build verification:
  `C:/Users/Suprabha/AppData/Roaming/Brotherhood/Colosseum/manga-volumes/`

- [ ] **Step 1: Run focused harness suite**

Build and run the CBZ, volume-index, packer, service, reader-core,
reader-decode, and reader-chrome harnesses. Require zero failures.

- [ ] **Step 2: Run the native build**

Run the repository's native build check. Require exit 0 and preserve the exact
failure excerpt if it fails.

- [ ] **Step 3: Verify Colosseum is closed**

Confirm no `colosseum.exe` process/window exists before AppData migration.

- [ ] **Step 4: Back up and migrate live Volume 1 and Volume 70**

Copy the ledger and both legacy manifests to a timestamped recovery directory,
run the tested migration path, then verify both CBZs reopen with respectively
216 and 196 expected image entries. Do not delete the recovery copy.

- [ ] **Step 5: Eyes-on smoke**

Launch the captured dev build, open Volume 1 and Volume 70, verify page 1 paints,
idle until cursor hides, move the pointer, and verify both HUD and the Windows
system-arrow cursor return.

- [ ] **Step 6: Review scoped diff and write recap**

Check every design requirement against the diff and evidence, preserve any
unreproduced Volume 70 caveat, and write the Agent 1 recap/next-wake prompt.

## Acceptance Criteria

- Tankoban Nyaa and WeebCentral publications leave one validated canonical CBZ
  plus recovery sidecar and no durable loose-page directory.
- Startup repair migrates both intact and ledger-corrupt legacy volumes, never
  deleting loose payload until archive reopen, sidecar, and ledger save succeed.
- Failed migration preserves a valid legacy row and its payload; unrecoverable
  repair prunes only the lookup row, not unvalidated bytes.
- Sidecar reconciliation restores an archive-backed row whose global ledger
  entry list drifted.
- The comic reader decodes `{archive, entry}` pages directly, reports a missing
  member as `missing_file`, and retains local-file support for non-Tankoban use.
- The cursor owner remains enabled across HUD return, explicitly changes to the
  system arrow, wakes the HUD on movement, and stays click-transparent.
- Focused storage, migration, packer, service, reader, and QML harnesses pass;
  the full `colosseum` target links.
- Live Volume 1 and Volume 70 are backed up and migrated to CRC-valid CBZs with
  216 and 196 image entries respectively; at least one migrated volume paints
  in the real reader.
