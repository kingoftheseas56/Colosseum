# Audiobook-to-EPUB Read-Along Design

**Date:** 2026-07-21

**Status:** Approved by Hemanth

**Owners:** Agent 2 (Biblio / Reader2) with Agent 0 architecture coordination

**Substrate attribution:** [Agent 0 (Codex), architecture]

## Objective

Give a downloaded English audiobook and its paired EPUB the same live reading experience as
Tankoban-Max TTS: the sentence being narrated can be highlighted, the word currently being
pronounced can receive the word treatment (including enlargement), or both can be shown together.
The narration is the real audiobook rather than synthesized speech.

The relationship is bidirectional:

```text
audiobook time <-> EPUB sentence and word
```

Audiobook playback moves the visible text and highlight. Double-clicking aligned EPUB text seeks
the audiobook to that passage. The existing Reader2 audiobook scrub bar remains the primary audio
timeline and participates in the same mapping.

## Settled Product Decisions

- English only, permanently for this feature's defined scope.
- Alignment is local and offline.
- The first release bundles all required speech models in every installer.
- A later change may make models downloadable on demand, but it must not change the runtime or
  database contracts in this design.
- Pairing an EPUB and audiobook automatically starts background alignment.
- Alignment is resumable, pausable, low priority, and chapter-addressable.
- A completed chapter is usable immediately; the complete book does not need to finish first.
- Low-confidence or mismatched chapters show a visible failure state. Colosseum never presents a
  guessed alignment as trustworthy.
- Reader2 retains all three Tankoban-Max presentation modes: Sentence, Word, and Sentence + Word.
- Manual reading navigation disengages visual following without pausing audio and reveals Return to
  narration.
- Double-clicking aligned text seeks the audiobook. A single click retains normal Reader2 behavior.
- The existing audiobook scrub bar remains present and becomes an aligned passage navigator.

## Scope

### In scope

- Automatic EPUB/audiobook alignment creation after pairing
- Native English coarse speech recognition
- EPUB-aware long-form transcript matching
- Native forced alignment for precise word timing
- Persistent chapter, sentence, word, gap, confidence, and job state
- Incremental chapter availability
- Audio-to-text and text-to-audio navigation
- Scrub-bar passage preview and committed seeking
- Tankoban-Max-compatible sentence/word visual modes and word styles
- Comfort-zone page movement and Return to narration
- Reader Audio-panel and global background-activity status/control
- Deterministic fixtures and accuracy measurements

### Out of scope

- Non-English alignment
- Cloud alignment or user audio upload
- Live transcription while the audiobook is playing
- Automatic repair of abridged or fundamentally different editions
- Editing or repackaging the source EPUB as a Media Overlay publication
- Community-shared alignment maps
- On-demand model delivery in the first release
- TTS engine changes

## User Experience

### Automatic start

When Reader2's existing pairing system establishes an EPUB/audiobook pair, the native alignment
service fingerprints both assets and checks the alignment store. If no compatible result exists,
it creates a background book job automatically.

The scheduler prioritizes:

1. The chapter currently being read or played
2. The following chapter
3. The preceding chapter
4. Remaining chapters in audiobook order

Changing the current chapter reprioritizes queued work but does not discard an in-progress safe
stage.

### Chapter state

Every audiobook chapter has an independent visible state:

```text
Waiting -> Preparing -> Transcribing -> Matching -> Aligning -> Ready
                                             \-> Couldn't sync
```

`Paused` is an overlay on any resumable non-terminal state, not a destructive restart. The UI uses
plain-language detail such as:

```text
Syncing chapter 6 of 24 · Aligning words
11 chapters ready
```

### Status surfaces

The Reader2 Audio panel shows a Text Sync row beneath the attached audiobook. It contains overall
progress, the live chapter stage, ready count, Pause/Resume, and expandable Details.

Details contains per-chapter states, Retry for a failed chapter, failure explanations, and a
book-level Restart syncing action protected by confirmation.

Because work continues after Reader2 closes, the same native job also appears in Colosseum's
unified background activity/download surface. It exposes the same Pause/Resume control. Both
surfaces observe one native job and cannot diverge.

### Read-along modes

Reader2 preserves the Tankoban-Max choices and preferences:

- **Sentence:** highlight the sentence whose time interval contains the playhead.
- **Word:** apply the chosen word treatment to the word whose time interval contains the playhead.
- **Sentence + Word:** keep the sentence highlight while applying the stronger treatment to the
  active word.

Tankoban-Max word styles and configurable enlargement carry forward. The implementation must keep
Reader2 pagination stable: a temporary word treatment must not permanently modify publication
markup, corrupt CFIs/annotations, or cause a page to repaginate on every boundary.

Pausing freezes the current trusted sentence/word state. Resuming continues from the current audio
time rather than from a cached visual index.

### Viewport following

The active sentence stays within a central comfort zone. Reader2 moves the page or scroll position
only when the sentence approaches the zone boundary; it does not recenter on every word.

Manual wheel, drag, page-turn, TOC jump, search jump, bookmark jump, or annotation jump disengages
visual following while the audiobook continues. A quiet **Return to narration** control appears.
Activating it resolves the live audio time, jumps to the trusted range, and restores following.

### Double-click seeking

- Double-click an aligned word: seek to that word's start and play.
- Double-click inside an aligned sentence when no word cue resolves: seek to the sentence start.
- Double-click outside aligned text: preserve existing Reader2 behavior and do not guess.
- Single-click, drag selection, links, footnotes, dictionary, highlights, and annotations keep their
  existing behavior.

### Existing audiobook scrub bar

The current Reader2 audiobook scrub bar remains the primary timeline. It uses the alignment index
without becoming a second or replacement control.

While hovering or dragging, the preview resolves the candidate audio time and shows:

- Timestamp
- Audiobook chapter label
- A short matched EPUB sentence excerpt when that time is aligned
- `This passage isn't synced yet` when no trusted cue exists

Dragging does not repeatedly seek audio or make the EPUB chase the pointer. On release, Reader2
commits one seek, resolves the destination cue, jumps the EPUB to its sentence/word, repaints the
chosen read-along mode, and restores visual following. Keyboard skips and other programmatic audio
seeks use the same committed-seek path.

## Architecture

### Native production stack

The shipped implementation is a native C++ hybrid:

- `whisper.cpp` with the bundled English `base.en` model for coarse transcription
- ONNX Runtime with a bundled ONNX export of `facebook/wav2vec2-base-960h` for English CTC forced
  alignment
- A Colosseum-owned EPUB sequence matcher and confidence engine
- SQLite for durable jobs and alignment cues

WhisperX and Montreal Forced Aligner are development comparison tools only. They are not shipped or
invoked by the production application.

The speech models live in a versioned installer model directory and are described by a manifest
containing identity, checksum, license, language, input requirements, and engine compatibility.
Startup validation detects missing or damaged model files before work begins.

### Components

#### `AudioTextAlignmentService`

The typed native façade exposed to QML. It owns pair discovery, book/chapter status models,
Pause/Resume/Retry/Restart, current-job prioritization, cue lookup, and aligned text-to-time lookup.
QML receives presentation-shaped state and never schedules inference directly.

#### `AlignmentScheduler`

Maintains one active analysis chapter at a time. It runs outside the GUI thread, applies low process
and worker-thread priority, observes pause/cancel tokens at safe checkpoints, and yields when
Colosseum reports latency-sensitive media activity.

#### `EpubTextIndexer`

Parses the EPUB package, spine, and XHTML into canonical English text while preserving stable
locations. Each token maps to:

```text
spine href + canonical character start/end + sentence hash
```

Reader2's paper uses the same canonical text rules when walking live DOM text nodes. Shared test
vectors prove that the C++ indexer and JavaScript resolver produce identical canonical offsets.
At runtime, the paper resolves offsets to a DOM `Range` and may derive a CFI for interoperability;
the stored contract does not depend on fragile generated element IDs.

Canonicalization normalizes Unicode composition, quotation and dash variants, whitespace, and
equivalent number/abbreviation forms for matching. Display text remains byte-for-byte the EPUB's
visible wording.

#### `AudiobookAnalysisDecoder`

Reads the existing audiobook chapter/file model and produces bounded 16 kHz mono PCM windows for
analysis. It never changes the playback files. Decode output is a resumable cache keyed by audio
fingerprint, chapter, and decoder version.

#### `CoarseTranscriber`

Runs native English speech recognition over bounded windows. Its transcript is discovery evidence,
not display text. It emits timestamped coarse segments and confidence.

#### `EpubSequenceMatcher`

Finds monotonic phrase anchors between the coarse transcript and canonical EPUB text. It tolerates
punctuation, abbreviations, narrator credits, skipped headings, omitted footnotes, and local spoken
variation. It cannot move backward to a distant repeated phrase after committing later anchors.

Chapter boundaries are recovery points, not a required one-to-one mapping: one audio chapter may
cover part of an EPUB section or cross several sections.

#### `EnglishForcedAligner`

Runs the CTC alignment model only on bounded audio/text regions between trustworthy anchors. It
returns sentence and word start/end times with confidence. Long audiobook audio is never submitted
as one unbounded dynamic-time-warping problem.

#### `AlignmentStore`

Persists fingerprints, engine/model versions, jobs, chapter stages, checkpoints, sentence cues,
word cues, unmatched regions, confidence, and diagnostics in SQLite. Writes are transactional per
safe stage so pause, crash, or shutdown cannot publish half a chapter as Ready.

#### `ReadAlongController`

Consumes the existing audiobook session position and alignment store. It binary-searches cues and
emits changes only when the active sentence or word changes. It owns follow/detached state and the
single committed-seek path used by the scrub bar, double-click, keyboard skip, and audiobook UI.

#### Reader2 paper bridge

The constrained WebEngine paper receives only presentation operations:

- Resolve a canonical EPUB location to DOM ranges
- Paint/clear active sentence and word treatments
- Keep a range inside the comfort zone
- Report a double-clicked canonical text position
- Preview or commit navigation to a resolved passage

No model, SQLite, matching, job, or playback authority enters JavaScript. QML paints and interacts;
C++ remembers, decides, schedules, and operates.

## Data Model

The schema is normalized around immutable pair and engine identities.

### Book alignment

```text
pair_id
epub_fingerprint
audio_fingerprint
language = "en"
engine_version
coarse_model_id
alignment_model_id
overall_state
created_at / updated_at
```

### Chapter job

```text
pair_id
audio_chapter_index
audio_start_ms / audio_end_ms
stage
checkpoint
coverage
confidence
failure_code / failure_detail
priority
```

### Sentence cue

```text
chapter_job_id
ordinal
start_ms / end_ms
spine_href
canonical_start / canonical_end
sentence_hash
confidence
region_kind
```

### Word cue

```text
sentence_cue_id
ordinal
start_ms / end_ms
canonical_start / canonical_end
confidence
```

`region_kind` is one of `aligned`, `book_only`, `audio_only`, or `uncertain`.

Lookup indexes support `(pair_id, audio time)` and `(pair_id, spine href, canonical offset)` so both
navigation directions are logarithmic and do not scan a chapter at runtime.

## Matching and Confidence Rules

- The EPUB is authoritative for displayed words.
- The coarse transcript is never inserted into the book or shown as replacement prose.
- Multiple separated high-confidence anchors are required before a region is forced-aligned.
- Alignment must be monotonic within the publication spine.
- Sentence and word confidence are stored separately.
- A low-confidence word region clears word emphasis rather than carrying the previous word forward.
- A trustworthy sentence may remain highlighted across a small low-confidence word gap.
- Book-only text receives no audiobook time.
- Audio-only material preserves ordinary playback with no fabricated EPUB highlight.
- A chapter becomes Ready only when trusted sentence cues cover at least 80% of narrative speech
  duration after classified audio-only lead/tail material is excluded, no unresolved internal run
  exceeds 30 seconds, and all unmatched runs are explicitly represented. A chapter below this gate
  produces `edition_mismatch` rather than a partial Ready result.
- Replacing either asset changes its fingerprint and invalidates only that pair's dependent result.
- An engine upgrade does not silently delete a usable result. A future Improve sync operation may
  create a candidate result and atomically replace the old result only after it passes validation.

## Resource Control and Persistence

- One chapter is analyzed at a time.
- Inference and decode never run on the GUI thread.
- Worker CPU priority is low and thread count is capped independently of the GUI/render pools.
- Video playback, high-resolution page decode, app startup, and other declared latency-sensitive
  activity cause the scheduler to yield or reduce its compute budget.
- Pause stops at the next safe checkpoint and retains decoded windows, transcript segments, matched
  anchors, and completed cue batches.
- Shutdown records a resumable state rather than waiting for a full chapter.
- Completed Ready chapters are immutable during normal continuation.
- Cache eviction never removes the published SQLite alignment before it removes reproducible decode
  and transcript intermediates.

## Failures and Recovery

Visible failure codes map to clear text and an expandable technical detail:

- `edition_mismatch` -> Couldn't sync — edition may differ
- `chapter_match_missing` -> Couldn't find matching EPUB chapter
- `audio_decode_failed` -> Audio could not be decoded
- `model_missing` -> English speech model is missing
- `model_checksum_failed` -> English speech model is damaged
- `epub_index_failed` -> Book text could not be indexed
- `alignment_failed` -> Word timing could not be produced reliably

Retry clears only the affected chapter attempt and its transient intermediates. Restart syncing
clears all generated alignment for the pair after confirmation. Neither operation deletes the EPUB,
audiobook, reading progress, bookmarks, notes, or annotations.

## Verification Strategy

### Deterministic corpus

The repository test corpus contains redistributable English text/audio fixtures for:

- Exact matching narration
- Different EPUB/audio chapter numbering
- Audio credits before chapter text
- EPUB paragraphs skipped by narration
- Repeated phrases
- Curly/straight quotes, punctuation, numbers, and abbreviations
- An audio chapter spanning more than one EPUB section
- A deliberately different edition
- A low-confidence word gap inside a trustworthy sentence

### Alignment accuracy

Ground-truth word and sentence boundaries are stored for the exact-match fixtures. The first-release
acceptance target is:

- Median absolute word-onset error at or below 250 ms on matching professional English narration
- 95th-percentile word-onset error at or below 600 ms on the same corpus
- Sentence transition error at or below 500 ms for at least 95% of ground-truth sentences
- No chapter in the deliberate mismatched-edition fixture may publish as Ready
- No audio-only or book-only fixture region may fabricate confident word cues

WhisperX and Montreal Forced Aligner outputs are retained only as benchmark reports to identify
regressions in the native pipeline.

### Runtime contracts

Automated Reader2 tests cover:

- All three presentation modes
- Every retained Tankoban-Max word treatment, including enlargement
- Audio time to sentence/word lookup
- Double-click word and sentence seeking
- Selection, annotation, dictionary, link, and footnote non-regression
- Existing scrub bar remains visible and functional
- Scrub hover/drag preview does not commit a seek
- Scrub release commits one audio seek and one EPUB navigation
- Unsynced scrub regions show honest status and keep ordinary audio seeking
- Keyboard/programmatic seek uses the same committed-seek path
- Comfort-zone movement without per-word viewport jitter
- Manual navigation detaches follow
- Return to narration restores the live cue
- Pause/resume, app restart, and crash recovery at every pipeline stage
- Incremental availability while later chapters remain pending
- Visible failure and per-chapter retry
- Model absence/checksum failure
- Asset fingerprint invalidation

### Performance contracts

- No inference, audio decode, SQLite migration, or EPUB indexing work runs on the GUI thread.
- Read-along cue changes do not trigger a DOM update unless sentence or word identity changes.
- Scrub dragging performs indexed preview lookup only; it does not decode audio, run inference, seek
  playback, or navigate the EPUB until release.
- Playback, page turning, text selection, and video rendering stay responsive while syncing.
- Pause becomes effective at the next bounded checkpoint and never loses a completed chapter.

## Delivery Slices

1. **Native contracts and persistent job/store skeleton** — fingerprints, statuses, scheduler,
   pause/resume, fake deterministic cues, and both UI status surfaces.
2. **EPUB canonical index and Reader2 DOM resolver** — shared canonicalization fixtures and stable
   sentence/word ranges.
3. **Runtime read-along using fixture cues** — three modes, comfort-zone follow, Return to narration,
   double-click seek, and scrub integration.
4. **Native coarse transcription and chapter matching** — bundled `base.en`, monotonic anchors,
   explicit unmatched regions.
5. **Native ONNX forced alignment** — precise sentence/word cues, confidence, quality corpus and
   comparison reports.
6. **Installer and resilience** — bundled models/manifest/licenses, model validation, background
   activity integration, throttling, recovery, and full regression pass.

Each slice leaves Reader2 usable. The visual experience is proven with deterministic fixture cues
before speech-model complexity enters the runtime.

## Definition of Done

- Pairing an English EPUB and audiobook automatically schedules resumable chapter alignment.
- The active chapter is prioritized and can become usable before the rest of the book.
- Reader2 reproduces Tankoban-Max Sentence, Word, and Sentence + Word presentation with retained
  word treatments and stable pagination.
- Audiobook playback moves the EPUB highlight and viewport through trusted cues.
- Double-clicking aligned EPUB text seeks and plays the audiobook from that passage.
- The existing audiobook scrub bar remains present, previews aligned passages while dragging, and
  performs one synchronized audio/EPUB jump on release.
- Manual navigation reveals Return to narration without interrupting audio.
- Pause/Resume is available in Reader2 and the global background activity surface.
- Failed or mismatched chapters display honest visible status and do not block successful chapters.
- All processing is local, English-only, native, off the GUI thread, and functional without network
  access after installation.
- The installer contains validated model files and required license notices.
- Accuracy, interaction, persistence, failure, and responsiveness contracts pass with recorded
  evidence.
