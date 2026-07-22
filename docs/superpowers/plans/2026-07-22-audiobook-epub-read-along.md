# Audiobook-to-EPUB Read-Along Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align a downloaded English audiobook to its paired EPUB locally, chapter by chapter, so Reader2 can reproduce Tankoban-Max sentence/word read-along, navigate bidirectionally, and keep its existing audiobook scrub bar as an aligned timeline.

**Architecture:** A native `AudioTextAlignmentService` runs resumable chapter jobs through Colosseum's existing single-worker `BackgroundWorkCoordinator`. It indexes authoritative EPUB text, decodes bounded PCM, uses whisper.cpp for coarse discovery, performs monotonic EPUB matching, and uses an ONNX CTC model for exact word timings stored transactionally in SQLite. A separate `ReadAlongController` turns stored cues into presentation-shaped state for Reader2; QML and the constrained paper bridge only paint, preview, and report interaction.

**Tech Stack:** C++17, Qt 6.11.1 (Core/Gui/Qml/Quick/Sql/Concurrent/WebEngine/WebChannel), SQLite, whisper.cpp `base.en`, ONNX Runtime 1.25.0 CPU x64, `facebook/wav2vec2-base-960h` ONNX CTC, FFmpeg subprocess decode, JavaScript DOM ranges, QML, NSIS.

## Global Constraints

- The approved design is `docs/superpowers/specs/2026-07-21-audiobook-epub-read-along-design.md`; it is authoritative when this plan and code appear to disagree.
- English only, local and offline. Do not add a language selector, cloud fallback, or displayed Whisper transcript.
- Bundle both speech models in every first-release installer; on-demand delivery is a later packaging change and must not alter runtime/store contracts.
- The EPUB is the only source of displayed text. Recognition output is matching evidence only.
- Use the existing `work::BackgroundWorkCoordinator` and `work::BackgroundActivityRegistry`; do not create an alignment-only worker pool or second global activity model.
- Analyze one chapter at a time. Decode, indexing, SQLite migration, transcription, matching, and ONNX inference never run on the GUI thread.
- A Ready chapter is usable while later chapters are pending. Failure is chapter-local and visible.
- Reader2 retains Sentence, Word, and Sentence + Word modes, word enlargement, annotations, selection, dictionary, links, footnotes, pagination, and the existing audiobook scrub rail.
- Double-click seeks; single-click and drag keep existing Reader2 behavior.
- Scrub hover/drag previews only. Release performs exactly one committed audio seek and one EPUB navigation.
- Existing dirty files belong to other work. Before every commit, stage and commit only the paths named by that task.
- `native/main.cpp` and `native/CMakeLists.txt` are shared multi-agent collision files — Agent 1's guided arc and Agent 4's Player 2 edit them concurrently. For any task touching them: `git pull` first, grep-verify the current contents, ADD ONLY your new lines (never rewrite a neighbor's block), commit those two files by explicit pathspec, and push immediately — never sit on uncommitted edits to them. On any conflict, stop and hand it to Agent 0 to referee rather than force-resolving.

## Existing Foundations — Reuse, Do Not Rebuild

- `native/work/BackgroundWorkCoordinator.*`: single shared worker, pause/cancel tokens, priority queue, and pressure yielding.
- `native/work/BackgroundActivityRegistry.*` + `qml/BackgroundActivitySection.qml`: unified background activity surface.
- `native/models/ModelManifest.*`: generic manifest parsing and SHA-256 validation.
- `native/cmake/OnnxRuntime.cmake` + `scripts/native/fetch_onnxruntime.ps1`: optional ONNX Runtime seam.
- `native/AudioPairingStore.h`: persisted ebook/audiobook pairing.
- `native/engine/AudiobookDownloader.*`: authoritative local audiobook file list.
- `qml/AudiobookSession.qml`: the one persistent audiobook playback engine.
- `qml/reader2/ReaderShell.qml`, `ReaderChrome.qml`, `LeftPanel.qml`, and `Paper.qml`: the live Reader2 surfaces.
- `resources/reader2/paper_glue.js`: the constrained paper command/event seam.

## File Structure

### Native alignment domain

- Create `native/alignment/AlignmentTypes.h`: enums, identities, locations, cue/status records, and stable wire-code helpers.
- Create `native/alignment/AlignmentStore.h/.cpp`: SQLite schema, transactional stages, checkpoints, cues, indexed lookups, invalidation, and retry/restart.
- Create `native/alignment/EpubTextIndexer.h/.cpp`: EPUB spine/XHTML extraction and canonical sentence/token locations.
- Create `native/alignment/AudiobookAnalysisDecoder.h/.cpp`: bounded FFmpeg-to-16-kHz-mono PCM windows and cache identity.
- Create `native/alignment/CoarseTranscriber.h/.cpp`: whisper.cpp adapter returning timestamped discovery segments.
- Create `native/alignment/EpubSequenceMatcher.h/.cpp`: monotonic phrase anchors and explicit gap regions.
- Create `native/alignment/EnglishForcedAligner.h/.cpp`: ONNX CTC emissions, trellis/backtrack, sentence/word cues, and confidence.
- Create `native/alignment/AudioTextAlignmentService.h/.cpp`: pair discovery, scheduler orchestration, status model, pause/resume/retry/restart, and activity publication.
- Create `native/alignment/ReadAlongController.h/.cpp`: logarithmic time/text lookup, active cue state, follow/detach, previews, and committed seeks.

### Reader2 and paper

- Create `resources/reader2/alignment_text.js`: shared canonicalization and DOM text-node offset mapping.
- Modify `resources/reader2/paper_glue.js`: alignment paint/clear, comfort-zone navigation, canonical double-click events, and manual-navigation notification.
- Modify `qml/reader2/Paper.qml`: narrow QML commands for alignment presentation.
- Modify `qml/reader2/ReaderShell.qml`: bind the native service/controller to the live `AudiobookSession` and route all committed seeks.
- Modify `qml/reader2/ReaderChrome.qml`: scrub-preview contract and Return to narration.
- Modify `qml/reader2/LeftPanel.qml`: Text Sync state/details and read-along mode/style controls.
- `qml/BackgroundActivitySection.qml` (Agent 0's spine component — DO NOT edit): alignment rows appear by publishing into the `BackgroundActivity` registry; verify rendering read-only in the harness.

### Pairing, application wiring, build, and packaging

- Modify `native/AudioPairingStore.h`: persist `bookPath`/`audioDir`, enumerate pairs, and emit pair lifecycle signals.
- Modify `native/engine/AudiobookDownloader.h/.cpp`: pass the local EPUB path through auto-attach and expose stable audio files to the service.
- Modify `native/main.cpp`: construct one service/controller against the existing coordinator, registry, pairing store, and downloader; expose typed façades to QML.
- Modify `native/CMakeLists.txt`: alignment sources, harnesses, whisper.cpp, ONNX-gated targets, and Qt modules.
- Create `native/cmake/WhisperCpp.cmake` and `scripts/native/fetch_whispercpp.ps1`: pinned native recognition dependency.
- Create `resources/models/alignment/coarse/manifest.json` and `resources/models/alignment/forced/manifest.json`: production model identities and generated checksums.
- Create `scripts/alignment/export_wav2vec2_onnx.py` and `scripts/alignment/requirements-export.txt`: development-only reproducible export.
- Modify `scripts/installer/package_release.sh`, `THIRD_PARTY_NOTICES.md`, and `.gitattributes`: runtime/model staging, notices, and LFS model tracking.

### Tests and fixtures

- Create `tests/fixtures/alignment/` with redistributable exact, numbering, credits, skipped-text, repeated-phrase, punctuation, cross-spine, mismatch, and low-confidence-gap cases.
- Create focused C++ harnesses for store, indexer, decoder, matcher, aligner, service, and controller.
- Create `tests/reader2_alignment_text_test.mjs` and `tests/reader2_readalong_harness.qml` for paper/QML contracts.
- Create `tests/test_alignment_installer_payload.ps1` and `tests/run_alignment_acceptance.ps1` for packaging and full acceptance.

---

### Task 1: Lock typed contracts and the durable SQLite store

**Files:**
- Create: `native/alignment/AlignmentTypes.h`
- Create: `native/alignment/AlignmentStore.h`
- Create: `native/alignment/AlignmentStore.cpp`
- Create: `tests/alignment_store_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `alignment::PairIdentity`, `CanonicalLocation`, `SentenceCue`, `WordCue`, `ChapterStatus`, `AlignmentStore::cueAtTime()`, `timeAtLocation()`, `publishReadyChapter()`, and retry/restart APIs.
- Consumes: Qt Core and Qt Sql only.

- [ ] **Step 1: Write the failing store harness**

Exercise schema creation, pair upsert, stage checkpoints, atomic Ready publication, time lookup, text lookup, crash-before-commit rollback, fingerprint invalidation, chapter retry, and pair restart. The key assertions are:

```cpp
CHECK(store.upsertPair(pair), "pair upsert");
CHECK(store.saveCheckpoint(pair.pairId, 0, Stage::Matching, "anchors:4"), "checkpoint");
CHECK(store.publishReadyChapter(pair.pairId, 0, sentences, words, regions, 0.91), "publish");
CHECK(store.cueAtTime(pair.pairId, 1820).word.ordinal == 3, "time -> word");
CHECK(store.timeAtLocation(pair.pairId, {"Text/ch1.xhtml", 47}).value() == 1760, "text -> time");
CHECK(store.chapterStatus(pair.pairId, 0).stage == Stage::Ready, "ready is terminal");
```

- [ ] **Step 2: Build to verify the harness fails**

Run: `cmake --build native/build-msvc --target alignment_store_harness --config Release`

Expected: FAIL because `native/alignment/AlignmentStore.h` does not exist.

- [ ] **Step 3: Define stable types and schema**

Use stable string wire codes (`waiting`, `preparing`, `transcribing`, `matching`, `aligning`, `ready`, `couldnt_sync`) and this schema:

```sql
CREATE TABLE pair_alignment(pair_id TEXT PRIMARY KEY, epub_fingerprint TEXT NOT NULL,
 audio_fingerprint TEXT NOT NULL, language TEXT NOT NULL CHECK(language='en'), engine_version TEXT NOT NULL,
 coarse_model_id TEXT NOT NULL, alignment_model_id TEXT NOT NULL, overall_state TEXT NOT NULL,
 created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);
CREATE TABLE chapter_job(id INTEGER PRIMARY KEY, pair_id TEXT NOT NULL, audio_chapter_index INTEGER NOT NULL,
 audio_start_ms INTEGER NOT NULL, audio_end_ms INTEGER NOT NULL, stage TEXT NOT NULL, checkpoint BLOB,
 coverage REAL NOT NULL DEFAULT 0, confidence REAL NOT NULL DEFAULT 0, failure_code TEXT, failure_detail TEXT,
 priority INTEGER NOT NULL, UNIQUE(pair_id,audio_chapter_index));
CREATE TABLE sentence_cue(id INTEGER PRIMARY KEY, chapter_job_id INTEGER NOT NULL, ordinal INTEGER NOT NULL,
 start_ms INTEGER NOT NULL, end_ms INTEGER NOT NULL, spine_href TEXT NOT NULL, canonical_start INTEGER NOT NULL,
 canonical_end INTEGER NOT NULL, sentence_hash TEXT NOT NULL, confidence REAL NOT NULL, region_kind TEXT NOT NULL);
CREATE TABLE word_cue(id INTEGER PRIMARY KEY, sentence_cue_id INTEGER NOT NULL, ordinal INTEGER NOT NULL,
 start_ms INTEGER NOT NULL, end_ms INTEGER NOT NULL, canonical_start INTEGER NOT NULL,
 canonical_end INTEGER NOT NULL, confidence REAL NOT NULL);
CREATE INDEX cue_by_time ON sentence_cue(chapter_job_id,start_ms,end_ms);
CREATE INDEX cue_by_text ON sentence_cue(spine_href,canonical_start,canonical_end);
CREATE INDEX word_by_time ON word_cue(sentence_cue_id,start_ms,end_ms);
```

`publishReadyChapter()` must use one transaction and enforce the 80% coverage/no-unresolved-run-over-30s gate before changing the chapter to Ready.

- [ ] **Step 4: Build and run the harness**

Run: `cmake --build native/build-msvc --target alignment_store_harness --config Release; native\build-msvc\alignment_store_harness.exe`

Expected: `PASS alignment store schema, atomic publication, lookup, invalidation, retry, restart`.

- [ ] **Step 5: Commit the slice**

```powershell
git add native/alignment/AlignmentTypes.h native/alignment/AlignmentStore.h native/alignment/AlignmentStore.cpp tests/alignment_store_harness.cpp native/CMakeLists.txt
git commit -m "feat: add durable audiobook alignment store"
```

### Task 2: Build the authoritative EPUB canonical index

**Files:**
- Create: `native/alignment/EpubTextIndexer.h`
- Create: `native/alignment/EpubTextIndexer.cpp`
- Create: `resources/reader2/alignment_text.js`
- Create: `tests/epub_text_indexer_harness.cpp`
- Create: `tests/reader2_alignment_text_test.mjs`
- Create: `tests/fixtures/alignment/canonical/fixture.epub`
- Create: `tests/fixtures/alignment/canonical/expected.json`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `EpubTextIndexer::index(epubPath) -> EpubIndex` and JavaScript `canonicalWalk(document) -> [{node,start,end,text}]` using identical rules.
- Consumes: `CanonicalLocation` from Task 1.

- [ ] **Step 1: Add cross-language failing vectors**

The fixture must cover NFC/NFD, curly/straight quotes, em/en/minus dashes, non-breaking whitespace, ellipses, `Dr.`/`Mr.`, `3.14`, and split inline elements. Assert C++ and JS both produce the expected canonical stream and offsets while retaining original display text.

- [ ] **Step 2: Verify both tests fail**

Run: `cmake --build native/build-msvc --target epub_text_indexer_harness --config Release; node tests/reader2_alignment_text_test.mjs`

Expected: missing indexer/module failures.

- [ ] **Step 3: Implement canonicalization and EPUB extraction**

`EpubTextIndexer` must read `META-INF/container.xml`, the OPF manifest/spine, parse XHTML with `QXmlStreamReader`, ignore script/style/hidden navigation, segment sentences/tokens, and record `spine href + canonical start/end + sentence SHA-256`. Add a focused ZIP reader inside this component using vendored miniz rather than Qt private APIs. `alignment_text.js` must mirror canonical folding but return live DOM text-node spans so ranges never depend on generated element IDs.

- [ ] **Step 4: Prove byte-for-byte parity**

Run: `native\build-msvc\epub_text_indexer_harness.exe tests\fixtures\alignment\canonical\fixture.epub tests\fixtures\alignment\canonical\expected.json; node tests/reader2_alignment_text_test.mjs`

Expected: both print `PASS canonical text and offsets agree`.

- [ ] **Step 5: Commit the slice**

```powershell
git add native/alignment/EpubTextIndexer.* native/third_party/miniz resources/reader2/alignment_text.js tests/epub_text_indexer_harness.cpp tests/reader2_alignment_text_test.mjs tests/fixtures/alignment/canonical native/CMakeLists.txt
git commit -m "feat: index EPUB text with stable canonical locations"
```

### Task 3: Add transactional chapter scheduling with deterministic fixture cues

**Files:**
- Create: `native/alignment/AudioTextAlignmentService.h`
- Create: `native/alignment/AudioTextAlignmentService.cpp`
- Create: `tests/alignment_service_harness.cpp`
- Modify: `native/AudioPairingStore.h`
- Modify: `native/engine/AudiobookDownloader.h`
- Modify: `native/engine/AudiobookDownloader.cpp`
- Modify: `native/main.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces QML façade: `statusFor(bookId)`, `chaptersFor(bookId)`, `pause(bookId)`, `resume(bookId)`, `retry(bookId,index)`, `restart(bookId)`, `prioritize(bookId,index)`, `ensurePair(bookId,bookPath,pairKey)` and `jobChanged(bookId)`.
- Consumes: `BackgroundWorkCoordinator`, `BackgroundActivityRegistry`, `AlignmentStore`, `AudioPairingStore`, and `AudiobookDownloader::localFiles(pairKey)` by constructor injection.

- [ ] **Step 1: Write a deterministic service harness**

Inject a fake chapter processor that publishes fixture cues after `Preparing`, `Transcribing`, `Matching`, and `Aligning` checkpoints. Assert priority order `current -> next -> previous -> remainder`, chapter-local Ready, pause preservation, restart confirmation seam, visible failures, and identical activity/service paused state.

- [ ] **Step 2: Verify failure**

Run: `cmake --build native/build-msvc --target alignment_service_harness --config Release`

Expected: missing service type.

- [ ] **Step 3: Extend pairing without breaking existing records**

Add optional `bookPath` and `audioDir` fields, `allPairings()`, and signals:

```cpp
void pairingSaved(const QString &bookId, const QVariantMap &pairing);
void pairingDeleted(const QString &bookId);
```

Extend `downloadAudiobook(..., bookId, bookPath = QString())`; auto-attach stamps `bookPath`, `pairKey`, and final `audioDir`. Old pair records remain readable and are upgraded when Reader2 next calls `ensurePair()`.

- [ ] **Step 4: Implement the service over the shared worker**

Submit IDs as `audio-align:<pairId>:<chapterIndex>`, publish required registry keys (`title`, `stage`, `progress`, `paused`, `canPause`, plus `kind:"audio_text_alignment"`), and connect registry pause/resume requests back to the same service methods. Safe checkpoints occur between bounded windows/stages, never halfway through Ready publication.

Map terminal failures exactly: `edition_mismatch`, `chapter_match_missing`, `audio_decode_failed`, `model_missing`, `model_checksum_failed`, `epub_index_failed`, and `alignment_failed`. The service owns the corresponding approved plain-language copy; QML renders it without inventing alternate meanings.

- [ ] **Step 5: Build and run**

Run: `cmake --build native/build-msvc --target alignment_service_harness --config Release; native\build-msvc\alignment_service_harness.exe`

Expected: `PASS incremental alignment scheduling, pause, failure, retry, restart, activity parity`.

- [ ] **Step 6: Commit the slice**

```powershell
git add native/alignment/AudioTextAlignmentService.* native/AudioPairingStore.h native/engine/AudiobookDownloader.* native/main.cpp tests/alignment_service_harness.cpp native/CMakeLists.txt
git commit -m "feat: schedule resumable chapter text alignment"
```

### Task 4: Give the paper stable alignment ranges and non-destructive painting

**Files:**
- Modify: `resources/reader2/paper_glue.js`
- Modify: `qml/reader2/Paper.qml`
- Modify: `tests/reader2_alignment_text_test.mjs`
- Create: `tests/reader2_paper_alignment_test.mjs`

**Interfaces:**
- Produces paper commands: `setReadAlongStyle(style)`, `paintReadAlong(cue)`, `clearReadAlong()`, `ensureReadAlongVisible(location)`, and `navigateReadAlong(location)`.
- Produces paper events: `alignedDoubleClick`, `manualNavigation`, and `readAlongRangeMissing`.
- Consumes canonical locations from Task 2; never receives SQLite/model authority.

- [ ] **Step 1: Write failing DOM tests**

Prove sentence/word ranges cross inline nodes, word enlargement uses an overlay/paint layer that does not alter layout metrics, repaint is idempotent, clear restores the DOM, double-click text emits canonical offsets, and selection/link/footnote paths still win.

- [ ] **Step 2: Verify failure**

Run: `node tests/reader2_paper_alignment_test.mjs`

Expected: missing paper alignment commands.

- [ ] **Step 3: Implement the narrow paper contract**

Add CSS classes for sentence wash and word treatment, but render enlargement through a positioned clone/overlay so line boxes and pagination do not change. Only repaint when cue identity changes. Tag programmatic navigation so `manualNavigation` is emitted only for wheel, drag, page turn, TOC/search/bookmark/annotation navigation initiated by the reader.

- [ ] **Step 4: Run paper regression tests**

Run: `node tests/reader2_alignment_text_test.mjs; node tests/reader2_paper_alignment_test.mjs; node tests/reader2_paper_text_test.mjs`

Expected: all print PASS; no existing paper command/event changes shape.

- [ ] **Step 5: Commit the slice**

```powershell
git add resources/reader2/paper_glue.js qml/reader2/Paper.qml tests/reader2_alignment_text_test.mjs tests/reader2_paper_alignment_test.mjs
git commit -m "feat: paint stable Reader2 read-along ranges"
```

### Task 5: Build the single bidirectional ReadAlongController

**Files:**
- Create: `native/alignment/ReadAlongController.h`
- Create: `native/alignment/ReadAlongController.cpp`
- Create: `tests/read_along_controller_harness.cpp`
- Modify: `native/main.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces properties `activeSentence`, `activeWord`, `followState`, `preview`, `chapterReady`; methods `setPlayhead(pairId,chapter,timeMs)`, `previewTime(...)`, `commitTime(...)`, `commitLocation(...)`, `detachFollow()`, and `returnToNarration()`.
- Emits `paintRequested(cue)`, `navigationRequested(location)`, `audioSeekRequested(chapter,timeMs,play)`, and `followStateChanged()`.
- Consumes indexed lookups from `AlignmentStore`.

- [ ] **Step 1: Write the failing controller harness**

Cover binary-search boundaries, low-confidence word gaps, trusted sentence carry, audio-only/book-only gaps, text-to-audio lookup, preview without seek, exactly-one committed seek, detach/return, pause freeze, and chapter changes.

- [ ] **Step 2: Verify failure**

Run: `cmake --build native/build-msvc --target read_along_controller_harness --config Release`

Expected: missing controller.

- [ ] **Step 3: Implement controller state and one commit path**

All scrub release, keyboard skip, chapter jump, double-click, and programmatic seek calls must converge on `commitTime()` or `commitLocation()`. `setPlayhead()` emits nothing when sentence/word IDs are unchanged. Untrusted word lookup clears only word emphasis; untrusted sentence lookup clears both.

- [ ] **Step 4: Build and run**

Run: `cmake --build native/build-msvc --target read_along_controller_harness --config Release; native\build-msvc\read_along_controller_harness.exe`

Expected: `PASS read-along lookup, confidence gaps, preview, commit, detach, return`.

- [ ] **Step 5: Commit the slice**

```powershell
git add native/alignment/ReadAlongController.* tests/read_along_controller_harness.cpp native/main.cpp native/CMakeLists.txt
git commit -m "feat: add bidirectional read-along controller"
```

### Task 6: Integrate Reader2 modes, double-click, viewport following, and scrub preview

**Files:**
- Modify: `qml/reader2/ReaderShell.qml`
- Modify: `qml/reader2/ReaderChrome.qml`
- Modify: `qml/reader2/LeftPanel.qml`
- Modify: `qml/reader2/Reader2Logic.js`
- Modify: `qml/AudiobookSession.qml`
- Create: `tests/reader2_readalong_harness.qml`
- Create: `tests/test_reader2_readalong.ps1`

**Interfaces:**
- Consumes: `AudioTextAlignment` and `ReadAlong` context properties plus paper commands/events from Tasks 3–5.
- Produces: complete fixture-cue user experience before speech inference exists.

- [ ] **Step 1: Write the failing QML harness**

Use fake Ready cues and assert Sentence, Word, Sentence + Word, enlargement scale, playhead painting, double-click word/sentence seek, selection non-regression, comfort-zone navigation, manual detach, Return to narration, unsynced status, scrub preview, and one commit on release.

- [ ] **Step 2: Verify failure**

Run: `powershell -NoProfile -File tests/test_reader2_readalong.ps1`

Expected: contract failure because Text Sync properties and preview signals are absent.

- [ ] **Step 3: Replace the old chapter-only Follow behavior**

Keep `AudiobookSession` as playback owner, but feed `position/currentIndex/paused` to `ReadAlong`. Replace direct `audioSession.seekTo()` calls in ReaderShell with controller commits. Preserve the visible scrub rail; split its signal into `audioScrubPreviewed(fraction)` and `audioScrubCommitted(fraction)`. Hover/drag updates timestamp/chapter/excerpt only; release commits once.

- [ ] **Step 4: Add live modes and follow behavior**

Persist mode/style/enlargement under `settings.reader2.readAlong`. Default to Sentence + Word. Map controller paint signals to `paper.paintReadAlong()`. Manual navigation calls `ReadAlong.detachFollow()` without pausing audio; Return calls `returnToNarration()`. Paused audio leaves the last trusted paint intact.

- [ ] **Step 5: Run Reader2 regression**

Run: `powershell -NoProfile -File tests/test_reader2_readalong.ps1; native\build-msvc\reader2_bridge_harness.exe; node tests/reader2_paper_text_test.mjs`

Expected: read-along PASS plus existing bridge/paper PASS.

- [ ] **Step 6: Commit the slice**

```powershell
git add qml/reader2/ReaderShell.qml qml/reader2/ReaderChrome.qml qml/reader2/LeftPanel.qml qml/reader2/Reader2Logic.js qml/AudiobookSession.qml tests/reader2_readalong_harness.qml tests/test_reader2_readalong.ps1
git commit -m "feat: integrate aligned audiobook reading in Reader2"
```

### Task 7: Surface honest Text Sync status in Reader2 and global activity

**Files:**
- Modify: `qml/reader2/LeftPanel.qml`
- Modify: `qml/reader2/ReaderChrome.qml`
- Create: `tests/alignment_activity_harness.qml`
- Create: `tests/test_alignment_activity.ps1`

**Interfaces:**
- Consumes one service status model and one registry row; both operate on the same native job.
- Produces Text Sync summary/details, per-chapter states, pause/resume, retry, failure detail, and protected restart.

- [ ] **Step 1: Add a failing activity/UI harness**

Assert `Syncing chapter 6 of 24 · Aligning words`, `11 chapters ready`, all seven chapter states, `Couldn't sync — edition may differ`, retry, confirmation before restart, and pause/resume parity between Reader2 and the global row.

- [ ] **Step 2: Verify failure**

Run: `powershell -NoProfile -File tests/test_alignment_activity.ps1`

Expected: missing Text Sync controls.

- [ ] **Step 3: Implement presentation only**

Reader2 calls service methods; the global row calls registry pause/resume, already connected to the same service. Do not duplicate stage/progress state in QML. A failed chapter remains playable as ordinary audio and never shows aligned controls for its gaps.

Do NOT edit `qml/BackgroundActivitySection.qml` — that shared spine component (Agent 0's) already renders any registry row; publishing your job state into `BackgroundActivity` is the whole contract. `tests/alignment_activity_harness.qml` feeds a fake registry an alignment row and verifies it renders through the existing component read-only, and Step 4 re-runs `tests/test_background_activity.ps1` as a non-regression check.

- [ ] **Step 4: Run the harness**

Run: `powershell -NoProfile -File tests/test_alignment_activity.ps1; powershell -NoProfile -File tests/test_background_activity.ps1`

Expected: both PASS.

- [ ] **Step 5: Commit the slice**

```powershell
git add qml/reader2/LeftPanel.qml qml/reader2/ReaderChrome.qml tests/alignment_activity_harness.qml tests/test_alignment_activity.ps1
git commit -m "feat: surface audiobook text-sync activity"
```

### Task 8: Decode bounded analysis audio with resumable cache identity

**Files:**
- Create: `native/alignment/AudiobookAnalysisDecoder.h`
- Create: `native/alignment/AudiobookAnalysisDecoder.cpp`
- Create: `tests/audiobook_analysis_decoder_harness.cpp`
- Create: `tests/fixtures/alignment/audio/exact.wav`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `decodeWindow(file,startMs,endMs,cacheKey,WorkContext&) -> PcmWindow` at signed float/16 kHz/mono and `probeChapters(files) -> QList<AudioChapter>`.
- Consumes bundled `ffmpeg.exe`, no playback-file mutation.

- [ ] **Step 1: Write the failing decoder harness**

Assert sample rate/channel/count, deterministic SHA-256, bounded cancellation, cache reuse, corrupt-audio error `audio_decode_failed`, and multi-file chapter ordering.

- [ ] **Step 2: Verify failure**

Run: `cmake --build native/build-msvc --target audiobook_analysis_decoder_harness --config Release`

Expected: missing decoder.

- [ ] **Step 3: Implement bounded FFmpeg decode**

Launch `ffmpeg -nostdin -v error -ss <start> -to <end> -i <file> -ac 1 -ar 16000 -f f32le pipe:1`; read stdout in bounded chunks, call `WorkContext::checkpoint()` between reads, and write cache files atomically under `<AppData>/alignment/cache/<audioFingerprint>/<chapter>/`.

- [ ] **Step 4: Build and run**

Run: `native\build-msvc\audiobook_analysis_decoder_harness.exe`

Expected: `PASS bounded 16k mono decode, cache, cancel, corrupt input`.

- [ ] **Step 5: Commit the slice**

```powershell
git add native/alignment/AudiobookAnalysisDecoder.* tests/audiobook_analysis_decoder_harness.cpp tests/fixtures/alignment/audio native/CMakeLists.txt
git commit -m "feat: decode resumable audiobook analysis windows"
```

### Task 9: Add native coarse English transcription

**Files:**
- Create: `native/cmake/WhisperCpp.cmake`
- Create: `scripts/native/fetch_whispercpp.ps1`
- Create: `native/alignment/CoarseTranscriber.h`
- Create: `native/alignment/CoarseTranscriber.cpp`
- Create: `tests/coarse_transcriber_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces `transcribe(PcmWindow, WorkContext&) -> QList<CoarseSegment>{startMs,endMs,text,confidence}`.
- Consumes whisper.cpp and the validated `base.en` manifest.

- [ ] **Step 1: Write a failing exact-audio harness**

Assert non-empty English segments, monotonic timestamps, no transcript exposed through QML, cancellation between windows, and `model_missing`/`model_checksum_failed` mapping.

- [ ] **Step 2: Verify failure**

Run: `cmake --build native/build-msvc --target coarse_transcriber_harness --config Release`

Expected: missing whisper target/transcriber.

- [ ] **Step 3: Pin and integrate whisper.cpp**

The fetch script downloads the pinned source archive, verifies its archive SHA-256 recorded in the script, and stages it under `C:/tools/whisper.cpp`. Build a CPU-only static target with examples/tests/network disabled. Use capped threads and English-only decoding; return segments to C++ only.

- [ ] **Step 4: Build and run with the real model**

Run: `cmake -S native -B native/build-msvc -DCOLOSSEUM_ENABLE_ALIGNMENT=ON -DCOLOSSEUM_ENABLE_ONNX=ON; cmake --build native/build-msvc --target coarse_transcriber_harness --config Release; native\build-msvc\coarse_transcriber_harness.exe`

Expected: `PASS native coarse English transcription and cancellation`.

- [ ] **Step 5: Commit the slice**

```powershell
git add native/cmake/WhisperCpp.cmake scripts/native/fetch_whispercpp.ps1 native/alignment/CoarseTranscriber.* tests/coarse_transcriber_harness.cpp native/CMakeLists.txt
git commit -m "feat: add native English coarse transcription"
```

### Task 10: Match coarse speech monotonically to the EPUB

**Files:**
- Create: `native/alignment/EpubSequenceMatcher.h`
- Create: `native/alignment/EpubSequenceMatcher.cpp`
- Create: `tests/epub_sequence_matcher_harness.cpp`
- Create: `tests/fixtures/alignment/matching/manifest.json`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `match(EpubIndex, QList<CoarseSegment>, ChapterHint) -> MatchPlan{anchors,regions,confidence}`.
- Consumes canonical EPUB index and coarse segments.

- [ ] **Step 1: Write failing matching cases**

Use fixtures for differing chapter numbering, opening credits, skipped paragraphs, repeated phrases, punctuation/abbreviation variants, cross-spine audio, and deliberate edition mismatch. Assert anchors never move backward and all gaps are classified `aligned`, `book_only`, `audio_only`, or `uncertain`.

- [ ] **Step 2: Verify failure**

Run: `cmake --build native/build-msvc --target epub_sequence_matcher_harness --config Release`

Expected: missing matcher.

- [ ] **Step 3: Implement bounded anchor matching**

Generate rare 4–8 token shingles, score transcript/text candidates with token similarity plus proximity, require multiple separated anchors, then run banded dynamic programming only between anchors. Lock committed anchors monotonically; chapter boundaries reset search windows but do not require one-to-one chapter mapping.

- [ ] **Step 4: Run deterministic cases**

Run: `native\build-msvc\epub_sequence_matcher_harness.exe tests\fixtures\alignment\matching\manifest.json`

Expected: `PASS monotonic anchors, explicit gaps, mismatch rejection`.

- [ ] **Step 5: Commit the slice**

```powershell
git add native/alignment/EpubSequenceMatcher.* tests/epub_sequence_matcher_harness.cpp tests/fixtures/alignment/matching native/CMakeLists.txt
git commit -m "feat: match audiobook speech to EPUB passages"
```

### Task 11: Produce precise English word timings with ONNX CTC alignment

**Files:**
- Create: `native/alignment/EnglishForcedAligner.h`
- Create: `native/alignment/EnglishForcedAligner.cpp`
- Create: `scripts/alignment/export_wav2vec2_onnx.py`
- Create: `scripts/alignment/requirements-export.txt`
- Create: `tests/english_forced_aligner_harness.cpp`
- Create: `tests/fixtures/alignment/ground_truth.json`
- Modify: `.gitattributes`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `align(PcmWindow, CanonicalPassage, WorkContext&) -> ForcedAlignmentResult{sentences,words,confidence}`.
- Consumes ONNX Runtime target, forced-model manifest, tokenizer vocabulary, and bounded MatchPlan regions.

- [ ] **Step 1: Write the failing ground-truth harness**

Assert monotonic word boundaries, sentence aggregation, separate word/sentence confidence, low-confidence gap behavior, cancellation, and exact-fixture timing metrics.

- [ ] **Step 2: Verify failure**

Run: `cmake --build native/build-msvc --target english_forced_aligner_harness --config Release`

Expected: missing aligner/model artifact.

- [ ] **Step 3: Export and validate the English CTC model**

The export script pins the Hugging Face model revision in source, exports fixed 16-kHz float input with dynamic sample length, writes tokenizer/vocabulary, runs ONNX-vs-PyTorch logits comparison, and generates the manifest/checksum. Track only generated model artifacts with Git LFS; Python is never installed or invoked by Colosseum.

- [ ] **Step 4: Implement CTC trellis and backtracking**

Normalize only for model tokens while retaining canonical offsets, compute log-softmax emissions, build a banded CTC trellis, backtrack token frames, merge subword tokens to EPUB words, aggregate sentences, and reject paths below confidence rather than fabricating cues.

- [ ] **Step 5: Run the accuracy harness**

Run: `native\build-msvc\english_forced_aligner_harness.exe tests\fixtures\alignment\ground_truth.json`

Expected: median onset error <=250 ms, p95 <=600 ms, >=95% sentence transitions <=500 ms, and no confident cues in declared unmatched regions.

- [ ] **Step 6: Commit the slice**

```powershell
git add native/alignment/EnglishForcedAligner.* scripts/alignment tests/english_forced_aligner_harness.cpp tests/fixtures/alignment/ground_truth.json resources/models/alignment/forced .gitattributes native/CMakeLists.txt
git commit -m "feat: align EPUB words with native ONNX CTC"
```

### Task 12: Connect the real pipeline, pressure yielding, and crash recovery

**Files:**
- Modify: `native/alignment/AudioTextAlignmentService.h`
- Modify: `native/alignment/AudioTextAlignmentService.cpp`
- Modify: `native/main.cpp`
- Create: `tests/alignment_pipeline_harness.cpp`
- Create: `tests/alignment_recovery_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes Tasks 2, 8, 9, 10, and 11 through constructor-injected interfaces.
- Produces the production stage chain `Preparing -> Transcribing -> Matching -> Aligning -> Ready/Couldn't sync`.

- [ ] **Step 1: Add failing end-to-end and recovery harnesses**

Interrupt every stage, reopen the store/service, and prove resume starts at the last safe checkpoint. Assert active/next/previous priority, one chapter in inference, latency-sensitive pressure yielding, immutable Ready chapters, and mismatch failure without blocking a later matching chapter.

- [ ] **Step 2: Verify failure**

Run: `cmake --build native/build-msvc --target alignment_pipeline_harness alignment_recovery_harness --config Release`

Expected: fixture processor still active; real stage chain absent.

- [ ] **Step 3: Replace fixture processing with bounded production stages**

Each stage reads/writes its own checkpoint artifact and calls `WorkContext::checkpoint()` between bounded windows/regions. Only `AlignmentStore::publishReadyChapter()` exposes cues. Map service failures to the approved stable codes and plain-language text.

- [ ] **Step 4: Wire app pressure**

Connect video playback, high-resolution page decode, and startup state to `BackgroundWorkCoordinator::setPressure()`. `LatencySensitive` yields between compute beats; `Suspended` blocks analysis but not lookup/playback/UI.

- [ ] **Step 5: Run recovery and pipeline tests**

Run: `native\build-msvc\alignment_pipeline_harness.exe; native\build-msvc\alignment_recovery_harness.exe`

Expected: both PASS with recorded stage-resume and pressure-yield evidence.

- [ ] **Step 6: Commit the slice**

```powershell
git add native/alignment/AudioTextAlignmentService.* native/main.cpp tests/alignment_pipeline_harness.cpp tests/alignment_recovery_harness.cpp native/CMakeLists.txt
git commit -m "feat: run resilient native audiobook alignment pipeline"
```

### Task 13: Bundle models, runtime, manifests, and licenses in every installer

**Files:**
- Create: `resources/models/alignment/coarse/manifest.json`
- Create: `resources/models/alignment/forced/manifest.json`
- Modify: `scripts/installer/package_release.sh`
- Modify: `THIRD_PARTY_NOTICES.md`
- Create: `tests/test_alignment_installer_payload.ps1`
- Modify: `native/alignment/AudioTextAlignmentService.cpp`

**Interfaces:**
- Produces validated installed payload at `models/alignment/{coarse,forced}` plus ONNX Runtime/whisper runtime dependencies.
- Consumes `models::ModelManifest` for startup validation.

- [ ] **Step 1: Write a failing installer payload test**

Require the coarse model, forced model, tokenizer/vocabulary, both manifests, ONNX Runtime DLL, model/runtime licenses, and notices. Corrupt one copied model and assert service state becomes `model_checksum_failed`; remove it and assert `model_missing`.

- [ ] **Step 2: Verify failure**

Run: `powershell -NoProfile -File tests/test_alignment_installer_payload.ps1`

Expected: missing alignment payload.

- [ ] **Step 3: Stage versioned production assets**

`package_release.sh` copies models by manifest, validates SHA-256 before NSIS runs, stages ONNX Runtime/whisper dependencies, and fails closed on missing licenses. Add third-party notices for whisper.cpp, ONNX Runtime, wav2vec2 model/license, and miniz.

- [ ] **Step 4: Build and inspect the installer**

Run: `bash scripts/installer/package_release.sh; powershell -NoProfile -File tests/test_alignment_installer_payload.ps1`

Expected: `PASS alignment models, runtimes, checksums, and notices`; installer creation succeeds.

- [ ] **Step 5: Commit the slice**

```powershell
git add resources/models/alignment scripts/installer/package_release.sh THIRD_PARTY_NOTICES.md tests/test_alignment_installer_payload.ps1 native/alignment/AudioTextAlignmentService.cpp
git commit -m "build: bundle offline audiobook alignment models"
```

### Task 14: Run the complete accuracy, interaction, responsiveness, and non-regression gate

**Files:**
- Create: `tests/run_alignment_acceptance.ps1`
- Create: `docs/verification/audiobook-alignment-acceptance.md`
- Modify: `tests/fixtures/alignment/ground_truth.json` only if a provenance/checksum correction is required; do not tune expected timings to make failures pass.

**Interfaces:**
- Consumes every preceding harness plus the committed redistributable corpus.
- Produces recorded Definition-of-Done evidence.

- [ ] **Step 1: Compose one fail-fast acceptance runner**

Run store/index/decoder/transcriber/matcher/aligner/service/controller/recovery harnesses, paper/QML contracts, installer checks, Reader2 regression, and the production build. Record timing metrics and GUI-heartbeat latency while background alignment runs.

- [ ] **Step 2: Prove the gate detects regressions**

Run once with the mismatched-edition expected result inverted in a temporary working copy.

Expected: acceptance fails on “mismatched edition published Ready”; restore the temporary edit immediately.

- [ ] **Step 3: Run the real gate**

Run: `powershell -NoProfile -File tests/run_alignment_acceptance.ps1`

Expected:

```text
PASS alignment accuracy median<=250ms p95<=600ms sentence95<=500ms
PASS mismatch/gap honesty
PASS Reader2 sentence word combined modes
PASS double-click and scrub one-commit navigation
PASS detach and return-to-narration
PASS incremental ready, pause, restart, crash recovery
PASS background responsiveness and GUI-thread prohibition
PASS installer offline payload
```

- [ ] **Step 4: Perform eyes-on Reader2 smoke**

Open the exact-match fixture, start its audiobook, inspect all three modes and enlargement, drag the existing scrub rail without page chasing, release to one synchronized jump, manually turn away, and activate Return to narration. Record observed result and installer size in `docs/verification/audiobook-alignment-acceptance.md`.

- [ ] **Step 5: Commit the verification gate**

```powershell
git add tests/run_alignment_acceptance.ps1 docs/verification/audiobook-alignment-acceptance.md
git commit -m "test: lock audiobook alignment acceptance gate"
```

## Final Definition-of-Done Review

- [ ] Pairing a downloaded English EPUB/audiobook automatically schedules alignment without a network request.
- [ ] Active, next, previous, then remaining chapters are prioritized through the one shared worker.
- [ ] Any Ready chapter is immediately usable while later chapters remain pending.
- [ ] Sentence, Word, and Sentence + Word modes match Tankoban-Max behavior, including enlargement without repagination.
- [ ] Playback drives trusted EPUB cues; double-clicking trusted EPUB text seeks and plays the audiobook.
- [ ] Existing scrub rail remains present, previews without seeking during drag, and commits one audio/EPUB jump on release.
- [ ] Manual navigation detaches following without interrupting audio; Return to narration restores the live cue.
- [ ] Reader2 and global activity show the same pause/resume and progress state.
- [ ] Failed/mismatched chapters are visible, retryable, and never publish guessed cues.
- [ ] Asset/model/engine identities invalidate only affected results; usable old engine results survive upgrades.
- [ ] All inference/index/decode/migration work stays off the GUI thread and yields to latency-sensitive media.
- [ ] Installer contains validated English models, runtimes, manifests, and licenses; installed alignment works offline.
- [ ] Accuracy and responsiveness evidence meets every threshold in the approved design.

## Execution Order and Review Checkpoints

1. Tasks 1–3 establish durable state and automatic incremental scheduling.
2. Tasks 4–7 prove the entire user experience with deterministic cues before model complexity.
3. Tasks 8–11 build and independently verify decode, recognition, matching, and exact alignment.
4. Task 12 replaces the fixture processor with the real resumable pipeline.
5. Tasks 13–14 package and prove the complete offline feature.

After Tasks 3, 7, 11, 12, and 14, run a cross-substrate review against the approved design. Task 14 must be approved before the feature is called shippable.
