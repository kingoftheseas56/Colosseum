# Colosseum Long-Strip Reader Bakeoff Design

**Date:** 2026-07-15

**Status:** Approved by Hemanth

**Owner:** Agent 1 (Comic Reader)

**Scope:** Evidence-gathering bakeoff only; no reader replacement in this work

**Compared readers:** Tankoban-Max, Tankoban 2, Colosseum

## 1. Purpose

Colosseum's current QML long-strip reader is borderline unusable for sustained comic and manga reading because wheel and touchpad scrolling do not deliver stable motion. Tankoban-Max feels substantially smoother. Tankoban 2 also contains a mature native Qt scroll-strip implementation and may be a better architectural donor than Max.

The project must not choose a donor by reputation or source inspection alone. This bakeoff runs all three readers against the same page bytes under controlled conditions, records comparable frame/input/decode evidence, and gives Hemanth a randomized A/B/C feel test. Its output is a written ruling selecting Max, Tankoban 2, or a deliberate hybrid as the basis for Colosseum's long-strip rescue.

The eventual rescue target is strict: Colosseum's long-strip scrolling must be indistinguishable from Tankoban-Max on Hemanth's Intel UHD 620 machine with both a mouse wheel and touchpad. "Better than now" is not an acceptable exit criterion.

## 2. Known Starting Point

### 2.1 Colosseum

The production long strip in `qml/MangaReader.qml` uses a QML `Flickable`, a full-model `Repeater`, per-page `Image` items, a moving decode zone, and a private 240 ms `OutCubic` animation restarted for every wheel event. It reads `angleDelta` but not native touchpad `pixelDelta`. Page heights begin as estimates and can change when decoded dimensions arrive.

### 2.2 Tankoban-Max

Max uses a viewport-sized canvas, a frame-synchronized wheel backlog pump, an explicit 512/256 MB bitmap cache, two-way decode limiting, neighbor prefetch, no-upscale sizing, and viewport-only drawing. It is the behavioral and feel reference because Hemanth has explicitly required Max-indistinguishable motion.

### 2.3 Tankoban 2

Tankoban 2 contains a clean native decomposition:

- `SmoothScrollArea`: native `pixelDelta` preference, angle-delta fallback, bounded backlog, 38% drain per 16 ms tick, capped step, and floating-point scroll accumulation.
- `ScrollStripCanvas`: culled painting of exposed pages, scaled-page cache, binary-search page lookup, viewport-zone decode requests, and no-upscale sizing.
- `PageCache`: byte-budgeted 512 MB LRU with pinning.
- `DecodeTask`: two-worker asynchronous archive read/decode with early dimension signaling.

TB2 is a credible donor, but it is QWidget/QScrollArea code rather than a direct Qt Quick component. It also uses a fixed 60 Hz timer, performs smooth pixmap scaling when decoded results reach the UI, and retains whole-strip geometry. The bakeoff must determine whether those differences matter in practice on the target machine.

## 3. Non-Goals

This bakeoff does not:

- change production scroll behavior in any reader;
- choose WebEngine, Qt Quick scene graph, or QWidget embedding before evidence exists;
- redesign the reader HUD, settings, bookmarks, progress, chapter crossing, or paged modes;
- benchmark library browsing, download speed, archive extraction throughput in isolation, or visual design;
- write benchmark progress into a real user library;
- treat a synthetic unit harness as a substitute for Hemanth's eyes-on feel judgment.

## 4. Controlled Test Artifact

### 4.1 Source and legality

Prepare one legally redistributable public-domain comic issue from a reputable public-domain archive. Record the title, source page, download URL, and public-domain basis in the evidence manifest. Do not use an unverified modern commercial comic.

### 4.2 Shape

The canonical test volume must contain 80-120 reading-order pages and include:

- predominantly portrait pages;
- at least four natural double-width spreads;
- realistic high-resolution page dimensions;
- enough decoded image data to cross cache/decode boundaries during a sustained scroll;
- no corrupt or encrypted entries.

If the chosen issue is shorter than 80 pages, construct a deterministic stress volume by repeating its pages in original order until the range is reached. Repetition must not resize, recompress, recolor, or otherwise change the source images.

### 4.3 Byte identity

The CBZ is canonical. Generate a manifest containing:

- CBZ SHA-256;
- ordered entry names;
- each page's SHA-256, encoded size, format, width, and height;
- spread classification using one shared threshold recorded in the manifest.

Max and TB2 open that exact CBZ. Colosseum receives an extracted copy of the exact entries through a temporary benchmark page store. Before each run, the launcher verifies the extracted page hashes against the CBZ manifest. A hash mismatch invalidates the run.

### 4.4 Isolation

All fixture files, extracted pages, profiles, caches, and progress stores live below `artifacts/reader-bakeoff/` or an associated temporary directory. The bakeoff must not add the fixture to a real library or modify real Continue/progress records.

## 5. Controlled Environment

Run the bakeoff on Hemanth's target Windows machine with the Intel UHD 620 GPU.

For all readers:

- use fresh Release binaries and record executable SHA-256 plus modification time;
- use the same physical display, resolution, refresh rate, Windows scale factor, and fullscreen geometry;
- hide reader chrome during motion trials;
- use the same page-width percentage, gap, background, split-wide setting, scaling-quality setting, and reading direction;
- disable auto-scroll, auto-flip, image filters, loupe, and unrelated overlays;
- close competing builds and media playback before capture;
- run only one reader at a time;
- record OS build, Qt/Chromium versions, GPU driver version, power mode, display refresh rate, and whether the device is on battery or AC.

The launcher must preserve the existing dirty worktree. It may not reset, clean, revert, or broadly stage files.

## 6. Run Matrix

Each reader runs the following motions three times in both cold-cache and warm-cache states:

1. **Slow wheel reading** - discrete wheel input at a natural reading cadence for 20 seconds.
2. **Sustained wheel reading** - repeated wheel input for 20 seconds, including overlapping events while motion is still draining.
3. **Fast touchpad swipe** - high-resolution pixel-delta input with momentum, repeated for 20 seconds.
4. **Boundary crossing** - deliberate slow and fast crossings over at least ten page boundaries.

Cold-cache means a fresh isolated profile/process with no decoded page cache. Warm-cache means the same volume has completed one end-to-end traversal in that isolated profile before measurement.

Input must be replayable for objective comparisons. Capture the first valid physical mouse and touchpad traces, normalize them into timestamped input scripts, and replay those same traces into each reader. Hemanth's later blind test remains physical and is not replaced by replay.

## 7. Measurement Architecture

### 7.1 Presented-frame evidence

Use one external Windows presentation tracer for all three processes so frame cadence is measured by the same clock and methodology. PresentMon CLI is the preferred tool. Record its version and full invocation in the run manifest.

For every run, report:

- median, p95, p99, and worst presented-frame interval;
- counts and percentages above 16.7 ms, 25 ms, and 33.3 ms;
- consecutive long-frame streaks;
- CPU frame time, GPU frame time, and display latency when exposed by the tracer;
- process working-set peak.

If the external tracer cannot associate frames with one reader reliably, the run is invalid; do not substitute incomparable per-app frame definitions in the final table.

### 7.2 Internal scroll evidence

Temporary instrumentation may be added behind an explicit bakeoff-only flag. It records into a preallocated in-memory ring buffer and flushes only after a run. No per-frame disk writes, console floods, layout queries, image conversions, or synchronous IPC are allowed.

Each reader records a common event schema:

```text
timestamp_us, reader, run_id, event,
input_kind, raw_dx, raw_dy, normalized_dy,
pending_delta, consumed_delta, scroll_offset,
page_index, decode_inflight, cache_bytes,
geometry_revision, detail
```

Required internal events are:

- input received;
- backlog/target updated;
- scroll step applied;
- page boundary crossed;
- decode queued, started, and completed;
- scale/texture preparation started and completed;
- cache hit, insert, and eviction;
- page geometry changed;
- visible frame/paint requested and completed where the runtime exposes it safely.

The instrumentation overhead calibration runs with tracing enabled and disabled against an already warm volume. If tracing increases p95 frame interval by more than 1 ms or changes long-frame count by more than 5%, fix the instrumentation and discard the affected evidence.

### 7.3 Derived metrics

The report derives:

- input-to-first-motion latency;
- input-to-present latency when clocks can be correlated safely;
- scroll velocity continuity and velocity resets;
- accumulated-input loss or overshoot;
- decode/scale/cache events overlapping long frames;
- geometry changes occurring during active motion;
- cold-to-warm improvement;
- page-boundary stall duration.

Correlation is evidence, not automatic causation. Each claimed hitch cause must cite the relevant time window in both the presentation trace and internal event trace.

## 8. Blind Hemanth Test

The machine evidence does not replace feel.

After all three readers have valid traces:

1. Present fullscreen page-only surfaces labeled A, B, and C.
2. Randomize their order independently for three rounds and save the concealed mapping before the round begins.
3. Keep page, zoom, gap, direction, cache state, and starting offset identical.
4. Hemanth performs the same natural wheel and touchpad reading gestures in each surface.
5. For each round he records:
   - smoothest;
   - second;
   - roughest;
   - any pair that feels indistinguishable;
   - observed hitch type: notchy input, velocity reset, decode pause, reflow/jump, or other.
6. Reveal the mapping only after all three rounds are recorded.

The blind result is invalid if the title bar, HUD, cursor treatment, obvious page mismatch, or launch order reveals the reader identity.

## 9. Calibration and Invalid Runs

Run a calibration pass before collecting evidence. Reject and repeat a run if any of the following occurs:

- wrong page bytes or reading order;
- different page width, gap, scaling, DPI, or window geometry;
- unexpected HUD/overlay visibility;
- cache state does not match the run label;
- another build or media process contends materially for CPU/GPU;
- instrumentation overhead exceeds the allowed threshold;
- presentation tracing drops data or attaches to the wrong process;
- a decode error, corrupt page, or adapter error changes the reading path;
- the user interacts outside the prescribed blind trial.

Every rejection and its reason must appear in the run ledger. Do not silently select favorable passes.

## 10. Adapter Rules

Adapters may only make the same page bytes consumable and make telemetry comparable. They may not alter scrolling behavior.

Allowed adapter work:

- isolated application profiles and temporary library roots;
- a direct-open command or test harness that enters the production long-strip reader;
- Colosseum's temporary `pageStore` implementation returning the canonical extracted page URLs;
- bakeoff-only signals/hooks that append to an in-memory trace;
- a common launcher and report generator.

Forbidden adapter work:

- changing easing, wheel normalization, frame cadence, cache size, prefetch distance, paint strategy, page sizing, or decode concurrency before measurement;
- replacing one reader's production long-strip component with a simplified benchmark renderer;
- warming only one reader;
- hiding a real stall by predecoding pages outside the defined warm-cache pass.

## 11. Donor Decision Rule

Tankoban-Max remains the mandatory feel baseline. The bakeoff chooses the implementation donor as follows:

### 11.1 Choose Max

Choose Max's canvas/runtime path when it wins Hemanth's blind ranking and its frame cadence, input latency, and boundary behavior materially outperform TB2. Reuse the proven hot path rather than translating it unless an integration blocker is demonstrated.

### 11.2 Choose Tankoban 2

Choose TB2's native architecture when Hemanth rates it indistinguishable from or smoother than Max and its p95/p99 cadence, long-frame rate, input latency, and cold-boundary stalls are no worse than Max within measurement noise. The report must still identify what must change when adapting QWidget/QScrollArea concepts to Qt Quick.

### 11.3 Choose a hybrid

Choose a hybrid only when the evidence shows separable strengths—for example, Max wins input/frame delivery while TB2 wins decode/cache stability. A hybrid ruling must name the exact mechanisms inherited from each reader. "Best of both" without trace-backed boundaries is not a valid decision.

### 11.4 No inconclusive escape

If evidence conflicts or noise prevents a ruling, improve the fixture, capture, or isolation and repeat the affected trials. Do not choose the easiest implementation merely because the first bakeoff was inconclusive.

## 12. Rescue Parity Contract Produced by the Bakeoff

The final bakeoff report must translate Max's valid runs into the acceptance thresholds for the later Colosseum rescue. At minimum, the rescue contract contains:

- maximum allowed difference from Max in p95 and p99 frame interval;
- maximum allowed difference in long-frame percentage at 25 and 33.3 ms;
- maximum input-to-motion latency difference;
- maximum page-boundary stall difference;
- zero unexpected geometry jumps during active motion;
- no lost native touchpad pixel deltas;
- Hemanth's three-round blind result: Colosseum must be judged indistinguishable from Max, not merely ranked second.

Numeric tolerances must be based on measured run-to-run variance. Use the larger of the tracer's known precision and Max's observed 95% run-to-run variation; do not invent a generous tolerance after seeing Colosseum's result.

## 13. Evidence and Repository Layout

Commit:

- this design specification;
- small deterministic launcher/harness source;
- instrumentation source guarded by the bakeoff flag;
- fixture manifest and provenance metadata, but not a large copyrighted or externally sourced CBZ unless its license and repository policy explicitly permit it;
- final compact report at `docs/reader-bakeoff/2026-07-15-long-strip-bakeoff-report.md`.

Keep ignored/local:

- `artifacts/reader-bakeoff/{run-id}/raw/` presentation traces;
- internal CSV/JSON event traces;
- extracted page bytes and CBZ fixture;
- isolated application profiles and caches;
- screenshots or recordings too large for the repository.

The final report links each summarized result to its local raw artifact path and records the SHA-256 of every raw trace used.

## 14. Deliverables

The bakeoff is complete only when all of the following exist:

1. Provenance and byte-identity manifest for the canonical test volume.
2. Reproducible isolated launcher for Max, TB2, and Colosseum.
3. Valid cold/warm traces for all four motions, three repetitions per reader.
4. Instrumentation overhead calibration proving the trace is behavior-neutral.
5. Three completed randomized Hemanth A/B/C rounds.
6. Final report with frame/input/decode/reflow evidence and rejected-run ledger.
7. Explicit donor ruling: Max, TB2, or trace-defined hybrid.
8. Numeric Max-parity contract for the subsequent Colosseum long-strip rescue.

No reader rewrite begins inside this bakeoff. Agent 1 uses the ruling and parity contract to author the separate rescue design and implementation plan.

## 15. Definition of Done

- [ ] All three readers consume page-identical content under matched visual settings.
- [ ] Cold and warm cache states are controlled and documented.
- [ ] Presentation cadence is captured with one common external methodology.
- [ ] Internal traces use the common event schema and stay within the overhead limit.
- [ ] Every prescribed motion has three valid runs per reader and cache state.
- [ ] Rejected runs are retained in the ledger with reasons.
- [ ] Hemanth completes three concealed, randomized A/B/C rounds.
- [ ] The final report explains visible hitches using correlated evidence.
- [ ] The donor decision follows the rule in Section 11.
- [ ] The subsequent rescue receives numeric Max-parity thresholds.
- [ ] Real libraries, progress records, downloads, and unrelated dirty files remain untouched.
