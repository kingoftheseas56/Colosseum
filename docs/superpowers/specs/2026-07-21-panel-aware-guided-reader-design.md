# Panel-Aware Guided Comic Reader Design

**Date:** 2026-07-21

**Status:** Product design approved; written specification awaiting Hemanth's review

**Owner:** Agent 1 (Tankoban / comic and manga reader)

**Architecture coordination:** [Agent 0 (Codex), architecture]

## Objective

Add a fifth Reader Style named **Guided** to Colosseum's existing manga/comic reader. Guided keeps
the original page image intact while a native-planned viewport moves through detected panels in
reading order. It supports both right-to-left manga and left-to-right western comics.

Guided contains two first-class submodes:

- **Panel Step:** the reader advances the camera one path step at a time.
- **Auto Read:** Colosseum advances automatically with adaptive timing and the approved Guided Glide
  motion language.

The feature is layout-aware, not artist-aware. Machine learning supplies panel and text rectangles;
deterministic native code decides order, grouping, confidence, camera framing, timing, and fallback.
Uncertain pages become safe full-page steps rather than speculative camera paths.

## Settled Product Decisions

- Support RTL manga and LTR western comics in the first release.
- Ship Panel Step and Auto Read together.
- Auto Read timing adapts to panel size, detected text amount, and camera travel, then applies a user
  speed multiplier.
- Any manual wheel, drag, zoom, pinch, page turn, or scrub pauses Auto Read immediately and shows
  **Resume Auto Read**.
- Page analysis starts automatically in a low-priority resumable background job when a downloaded
  chapter or issue opens.
- Ready pages become usable immediately; the entire chapter does not need to finish first.
- The source page remains intact. Guided moves a viewport over it and never replaces it with cropped
  panel images.
- Low-confidence, splash, borderless, failed, or otherwise uncertain pages use a whole-page path in
  both Panel Step and Auto Read.
- Confirmed double-page spreads are analyzed and presented as one wide canvas.
- First-release manual correction is limited to Use whole page, Retry detection, and Reverse panel
  order. A full panel editor is deferred.
- **Guided** is a fifth Reader Style beside Long Strip, Single Page, Double Page, and MangaPlus.
- Guided restores the previously active style and canvas when exited.
- The approved camera personality is **Guided Glide**: smooth eased travel with restrained internal
  traversal for tall and wide panels.
- Every canvas begins with a full-page overview and returns to a full-page overview after its final
  panel before advancing.
- Auto Read continues into the next locally available chapter/issue and stops cleanly at existing
  acquisition boundaries.

## Scope

### In scope

- Bundled offline panel/text detector
- Native ONNX Runtime inference
- Native duplicate suppression, RTL/LTR ordering, grouping, confidence, and camera planning
- Single-page and confirmed-spread canvases
- Automatic background analysis, pause/resume, retries, persistence, and page-level readiness
- Cached normalized panel/text boxes, ordered paths, camera keyframes, confidence, and overrides
- Guided Reader Style with Panel Step and Auto Read
- Full-page overview beats
- Guided Glide framing and restrained tall/wide traversal
- Adaptive dwell timing and 0.5x-2.0x speed
- Manual-input interruption and Resume Auto Read
- Per-panel resume state and cross-chapter continuation
- Safe whole-page fallback
- Reader-local and global background-analysis status surfaces
- Deterministic fixture corpus, path verification, performance tracing, and eyes-on verification

### Out of scope

- Cropping/extracting panels into replacement images
- OCR or semantic reading of dialogue
- Balloon-level reading order
- Character, face, gaze, or focal-point detection
- Dramatic or generative cinematography
- A box drawing/resizing/reordering editor
- Community panel maps
- Cloud inference
- Model training inside the application
- Replacing or redesigning Long Strip, Single Page, Double Page, or MangaPlus
- Analyzing undownloaded content or an entire library proactively

## User Experience

### Entering Guided

Guided appears in the existing Reader Style selector. Entering it records the prior style and current
canvas, then presents the same original page/spread through the Guided viewport. Leaving Guided
restores the prior style on the same canvas.

Guided controls contain:

- Panel Step / Auto Read selector
- Previous and next path step
- Play/pause Auto Read
- Reading speed from 0.5x to 2.0x
- Analysis status and Pause/Resume analysis
- Page actions: Use whole page, Retry detection, Reverse panel order
- Exit Guided

### Canvas sequence

Every trusted canvas uses:

```text
full-canvas overview
-> first ordered panel
-> remaining ordered panels and any planned internal stops
-> full-canvas overview
-> next canvas
```

A confirmed spread is one canvas in this sequence. A fallback page is one full-canvas overview step.

### Panel Step

Forward input advances one path step using Guided Glide. Backward input returns one step. The reader
waits indefinitely after each transition. RTL/LTR direction affects path construction and existing
forward/back pointer zones; it does not invert keyboard semantics unpredictably.

### Auto Read

Auto Read traverses the same immutable path as Panel Step. It advances after each adaptive hold.
Panel Step and Auto Read cannot disagree about order, framing, or fallback.

At chapter end, Auto Read continues into the next locally available chapter/issue and preserves the
submode and speed. If the next entry is unavailable, downloading, failed, or requires a source
choice, Auto Read stops and exposes the existing acquisition action.

### Manual interruption

Wheel, drag, pinch, zoom, manual page turn, scrub, thumbnail jump, bookmark jump, chapter jump, or
other reader navigation stops the active animation in the same input event and pauses Auto Read.
The viewport remains where the reader placed it. The automatic controls collapse to one quiet
**Resume Auto Read** action.

Resume chooses the nearest sensible panel in reading-path order from the current viewport center,
moves to it with one Guided Glide transition, and continues adaptive timing.

### Resume behavior

Progress records series/entry identity, canvas index, path-step index, Guided submode, speed, and
whether Guided was active. Reopening returns to the same canvas and step, but Auto Read always starts
paused so opening a book never causes unsolicited movement.

### Analysis status

Guided settings show current analysis stage, ready canvas count, overall canvas count, and
Pause/Resume. Expanded Details shows per-canvas status and fallback/failure reasons.

The same job appears in Colosseum's unified background activity/download surface because analysis
continues after the reader closes. Both surfaces control one native job.

## Motion Language

### Guided Glide

Motion uses an eased continuous transition between camera rectangles. The implementation
interpolates viewport center and scale together so the source page never teleports beneath a fixed
zoom.

Initial transition duration is distance-based and clamped to 0.35-0.8 seconds. The default easing is
InOutCubic. The user's speed multiplier scales holds and transitions together.

### Framing rules

- **Ordinary panel:** contain-fit inside the Guided viewport with a restrained safety margin.
- **Tiny adjacent panels:** merge when separate framing would exceed the configured maximum upscale
  or produce a viewport smaller than the minimum readable extent.
- **Tall panel:** fit width and add one reading-direction traversal along the long axis.
- **Wide panel:** fit height and add one traversal in the publication's reading direction.
- **Oversized panel with separated text regions:** permit at most two internal stops.
- **Full-page splash or uncertain canvas:** one full-canvas path.
- **Confirmed spread:** frame and traverse against the combined wide-canvas coordinates.

The first release does not infer dramatic focus. It never generates multiple arbitrary zooms inside
a panel.

### Adaptive timing

Timing uses deterministic features already produced by analysis:

- Panel area relative to canvas
- Number of associated text regions
- Text-region area relative to panel
- Camera travel distance
- Overview versus panel step

The initial profile is:

```text
overview hold = 0.8 seconds
area ratio = clamp(panel area / canvas area, 0.0, 1.0)
text-area ratio = clamp(associated text area / panel area, 0.0, 1.0)
panel-area weight = 1.2 * sqrt(area ratio)
text-region weight = min(4.5, 0.65 * associated text-region count + 2.0 * text-area ratio)
panel hold = clamp(1.5 + panel-area weight + text-region weight, 1.5, 8.0 seconds)
camera distance = min(1.0, normalized center travel + 0.35 * abs(log2(scale ratio)))
transition = clamp(0.35 + 0.45 * camera distance, 0.35, 0.8 seconds)
effective duration = profile duration / user speed multiplier
```

These constants form planner timing profile `guided-v1` and are locked with tests. Changing any
coefficient increments the planner profile version so cached paths/timing are invalidated
intentionally. `normalized center travel` is Euclidean travel after both axes are normalized by
canvas width/height; `scale ratio` is the larger viewport scale divided by the smaller and is always
at least 1.0.

## Architecture

### Production direction

The shipped implementation uses:

- ONNX Runtime for local inference
- An ONNX export of `leoxs22/manga-panel-detector-yolo26n`
- Colosseum-owned C++ planning, persistence, job control, and camera state
- Existing QML image presentation for the original page/spread

The model is Apache-2.0 according to its published model card and is trained on Manga109-s. The
installer bundles the exported model, a checksum/version/license manifest, and required notices. No
Ultralytics Python runtime ships with Colosseum.

This feature may share ONNX Runtime and generic background-work infrastructure with audiobook text
alignment. Comic-specific services, tables, models, statuses, and policies remain isolated.

### Components

#### `BackgroundWorkCoordinator`

A shared native scheduling primitive for low-priority resumable jobs. It supplies resource-pressure
signals, pause/cancel tokens, bounded concurrency, persistent job identity, and presentation-shaped
status. It contains no comic or audiobook domain logic.

If audiobook alignment has not established this component when comics begin, the comic slice creates
the minimal generic contract required by both designs. Domain services remain consumers rather than
subclasses with hidden cross-dependencies.

#### `PanelAnalysisService`

The typed façade exposed to QML. It owns job creation, page prioritization, status models,
Pause/Resume, per-canvas Retry, cached-result lookup, manual override writes, and ready/failure
notifications.

#### `PanelDetectorOnnx`

Loads and validates the bundled model once, prepares a letterboxed 640x640 RGB tensor, runs inference
off the GUI thread, transforms normalized results back into source-canvas coordinates, and emits
panel/text boxes with confidence. It performs no ordering or camera decisions.

#### `PanelPlanner`

A pure deterministic native module. It performs non-maximum suppression, geometry validation,
single-page/spread coordinate normalization, RTL/LTR ordering, tiny-panel merging, text-to-panel
association, tall/wide subdivision, confidence evaluation, safe fallback, and camera-keyframe
generation.

The planner accepts plain value objects and has no QML, image decode, ONNX, SQL, or filesystem
dependency. Fixture tests can therefore exercise every rule directly.

#### `PanelMapStore`

SQLite persistence for canvas fingerprints, model/planner versions, raw accepted detections, planned
paths, confidence, fallback reasons, analysis checkpoints, and manual overrides. Writes publish one
canvas atomically; a partial analysis can never appear Ready.

#### `GuidedCameraController`

A native state machine for path position, Panel Step, Auto Read, adaptive deadlines, Guided Glide
targets, interruption, nearest-panel resume, cross-canvas/chapter continuation, and progress
persistence. It consumes immutable planned paths and never performs inference.

#### `GuidedViewport.qml`

A focused QML presentation component that keeps the original source image(s) on one canvas and
applies the controller's viewport rectangle, zoom, transition duration, and easing. A spread uses
two source images in one stable coordinate space. QML owns animation and input reporting, not path
planning.

`MangaReader.qml` integrates Guided as a style and delegates its presentation/state plumbing to this
component rather than absorbing detector/planner logic into the existing large reader file.

## Background Analysis Lifecycle

Opening a downloaded entry automatically creates or resumes its analysis job. Canvas priority is:

1. Current page or spread
2. Next four reading-order canvases
3. Previous canvas
4. Remaining canvases in reading order

Changing canvas reprioritizes queued work. One canvas runs at a time.

Canvas stages are:

```text
Waiting -> Decoding -> Detecting -> Planning -> Ready
                                  \-> Whole-page fallback
                                  \-> Couldn't analyze
```

Whole-page fallback is a valid published path. Couldn't analyze is reserved for operational failure
such as unreadable image bytes, unsupported decoding, model absence/checksum failure, inference
failure, or storage failure.

The worker:

- Runs off the GUI thread at low priority
- Uses bounded 640x640 analysis input, not the display texture
- Yields during video playback, heavy visible-page decoding, startup, and other declared
  latency-sensitive activity
- Persists stage checkpoints and resumes after Pause, shutdown, or crash
- Makes each Ready/fallback canvas usable immediately
- Never analyzes undownloaded pages

If the visible canvas finishes analysis while its whole-page fallback is already being presented,
the camera does not seize control. The new path activates after advance, re-entry, or explicit **Use
detected panels**.

## Data Model

### Analysis job

```text
entry_identity
archive/page-set fingerprint
reading_direction
model_id / model_version
planner_profile_version
state
paused
current_priority_canvas
created_at / updated_at
```

### Canvas

```text
job_id
canvas_index
source page index/indices
single_page | spread
canvas_fingerprint
width / height
stage
confidence
fallback_reason
override_kind
```

### Detection

```text
canvas_id
panel | text
normalized x / y / width / height
model_confidence
accepted
```

### Path step

```text
canvas_id
ordinal
overview | panel | internal_stop
source panel identity
normalized camera rectangle
hold_seconds_at_1x
transition_seconds_at_1x
planner_confidence
```

Coordinates are normalized to the combined source canvas. Results remain independent of screen
resolution and Reader window size.

Indexes support entry/canvas lookup and cache validation without scanning prior results.

## Detection, Ordering, and Confidence

### Detector boundary

The detector supplies only rectangular `panel` and `text` observations. Its training-domain metrics
do not authorize a production path by themselves, especially for western comics and unconventional
layouts.

### Planner validation

Before publishing a panel path, the planner verifies:

- At least one plausible panel remains after suppression
- All accepted boxes lie inside the canvas after coordinate conversion
- Duplicate/heavily overlapping boxes are resolved or rejected
- Every accepted panel receives exactly one reading ordinal
- Row/column partitioning is deterministic for the configured direction
- Panel coverage and uncovered area remain plausible for the page class
- Spread geometry and seam transformation are valid
- Generated camera rectangles stay inside the source canvas
- Merge/subdivision never duplicates or loses a source panel

The planner emits `trusted`, `fallback`, or `failed`. Medium confidence is conservative: it emits a
whole-page fallback rather than a reduced speculative path.

### Reading direction

- RTL: top groups before lower groups; right-side panels before left-side peers.
- LTR: top groups before lower groups; left-side panels before right-side peers.
- Direction comes from the existing per-series reader preference. MangaPlus remains LTR under its
  existing rule.
- A direction change invalidates the generated path but not raw cached detections.

### Spreads

Existing persisted pairing memory determines whether adjacent pages form a spread. Confirmed pairs
are decoded and letterboxed into one wide analysis canvas. The planner uses one coordinate system
across the seam and may accept a panel that spans it. Uncertain pairing does not create a speculative
spread.

## Cache and Manual Overrides

Generated results are keyed by:

```text
canvas fingerprint + pairing + direction + model version + planner profile version
```

- Reopening a matching canvas performs no inference.
- Use whole page stores a fingerprint-bound override and publishes one overview step.
- Reverse panel order reverses the accepted panel sequence and regenerates overview/path ordinals
  without changing detections.
- Retry detection clears only the canvas's generated result and transient checkpoints, then reruns
  the current model/planner.
- Changing source bytes or spread pairing invalidates generated results and incompatible overrides.
- Changing direction reuses detections but rebuilds the path.
- Changing model or planner profile invalidates generated results intentionally.

No operation modifies source images, archives, downloads, existing reading progress, or existing
spread-pairing memory.

## Failure Handling

Visible reasons map to stable native codes:

- `no_panels` -> No panels detected — using whole page
- `layout_ambiguous` -> Layout too ambiguous — using whole page
- `spread_uncertain` -> Spread pairing uncertain — using whole page
- `image_decode_failed` -> Image could not be analyzed
- `model_missing` -> Panel model is missing
- `model_checksum_failed` -> Panel model is damaged
- `inference_failed` -> Panel detection failed
- `store_failed` -> Panel map could not be saved

Fallback is local to one canvas and never blocks the chapter. Operational failure also preserves
ordinary reader modes and the original page.

## Verification Strategy

### Fixture corpus

The repository contains or generates legally redistributable RTL and LTR fixtures for:

- Regular rectangular grids
- Tall and wide panels
- Tiny adjacent panels
- Borderless and overlapping artwork
- Dark or absent gutters
- Single-panel splash pages
- Confirmed two-page spreads
- Cross-seam panels
- Repeated/symmetric layouts
- Deliberately ambiguous reading order
- Corrupt and unsupported image data

Each fixture records expected source dimensions, accepted/fallback outcome, panel boxes where
applicable, exact reading order, grouping, spread composition, and camera-step class.

### Planner and detector contracts

- At least 95% of conventional fixture canvases must produce the exact ground-truth panel order.
- Every deliberately ambiguous fixture must publish whole-page fallback rather than an unsafe path.
- RTL and LTR variants of equivalent geometry must traverse in their correct direction.
- Confirmed spreads must remain one canvas and preserve cross-seam panel geometry.
- Raw detections and planner outputs must be deterministic for identical inputs and versions.
- Every generated camera rectangle must remain inside the normalized source canvas.
- Panel Step and Auto Read must consume byte-identical serialized paths.
- Cached reopening must perform zero model inference.

### Reader contracts

- Guided appears as a fifth style and does not alter the four existing styles.
- Enter/exit preserves canvas and restores prior style.
- Overview -> panels -> overview order is exact.
- Guided Glide uses the planned target, duration, and easing without crop substitution.
- Tall/wide panels never exceed the allowed internal-stop count.
- Speed scales holds/transitions deterministically.
- Manual input stops animation and Auto Read in the same event without a camera jump.
- Resume Auto Read selects the nearest path panel and continues cleanly.
- Reopening restores the panel but keeps Auto Read paused.
- Locally available next chapters continue; acquisition boundaries stop honestly.
- All three manual overrides persist and invalidate only under defined fingerprint/version changes.

### Background and resilience contracts

- Decode, inference, planning, and SQLite writes never run on the GUI thread.
- Only one canvas analyzes at once.
- Current/next-page prioritization is deterministic.
- Pause becomes effective at the next bounded stage checkpoint and loses no published canvas.
- Shutdown/crash recovery resumes without publishing partial paths.
- A visible canvas does not change camera automatically when its background result arrives.
- Model absence/checksum failure is visible and ordinary reading remains available.

### Performance and eyes-on contracts

- Guided animation is traced on Hemanth's Intel UHD 620 target using the same presentation-tracing
  discipline established by the long-strip bakeoff design.
- At 60 Hz, p95 presented-frame interval during warm Guided Glide stays at or below 20 ms, and no
  transition contains two consecutive intervals above 33.3 ms.
- Manual interruption becomes visually stationary by the next presented frame after the input event.
- Warm cached canvas entry performs no inference and begins its first overview transition within 100
  ms of the page texture becoming ready.
- Background analysis introduces no measurable regression beyond run-to-run variance in existing
  Long Strip wheel/touchpad tests or Theatre playback frame cadence.
- Hemanth completes eyes-on trials for a conventional RTL page, conventional LTR page, tall panel,
  wide panel, spread, deliberate fallback, interruption, resume, and chapter continuation.

## Delivery Slices

1. **Shared native foundations:** generic background-work contract, ONNX Runtime packaging seam,
   model manifest/checksum loader, panel-map schema, and fake deterministic jobs.
2. **Detector:** reproducible ONNX export, native preprocessing/inference/postprocessing, coordinate
   round-trip tests, and detector fixture harness.
3. **Planner:** NMS, RTL/LTR order, confidence, fallbacks, spreads, merge/subdivide, timing profile,
   and serialized path tests.
4. **Guided Panel Step:** Guided style integration, `GuidedViewport`, overview sequence, prior-style
   restoration, controls, progress, and fixture-driven navigation.
5. **Guided Glide and Auto Read:** native camera state machine, adaptive holds, interruption,
   nearest-panel resume, speed, persistence, and chapter continuation.
6. **Background completion:** automatic jobs, priority/look-ahead, pause/resume, status surfaces,
   cache invalidation, overrides, failure UX, installer model/notices, performance traces, regression,
   and eyes-on verification.

Each slice is independently testable. Guided motion is proven first against fixture paths before
real model output is allowed to control the camera.

## Definition of Done

- Guided is a fifth Reader Style supporting RTL manga and LTR western comics.
- It keeps original page/spread images intact and never replaces them with panel crops.
- Panel Step and Auto Read traverse one shared deterministic path.
- Every trusted canvas performs full overview, ordered panels, and final overview.
- Guided Glide, restrained tall/wide traversal, and adaptive speed match the approved behavior.
- Manual input immediately pauses motion and exposes Resume Auto Read.
- Safe whole-page fallback handles all uncertain canvases without blocking the chapter.
- Confirmed spreads operate as one wide canvas.
- Background analysis is automatic, low-priority, resumable, pausable, cached, page-incremental, and
  reachable from Reader2 and global activity.
- Use whole page, Retry detection, and Reverse panel order work and persist against exact canvas
  identity.
- Auto Read continues across locally available chapter/issue boundaries and stops at acquisition
  boundaries.
- The bundled model and ONNX runtime operate fully offline with validated manifests and notices.
- Planner correctness, reader interaction, cache, resilience, performance, regression, and eyes-on
  contracts pass with recorded evidence.
