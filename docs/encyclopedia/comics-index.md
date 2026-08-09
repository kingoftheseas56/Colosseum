# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/comics-state.json`

## Summary

- Total files: **91**
- Documented: **70**
- Undocumented: **21**
- Drifted: **0**

<a id="file-native-comicreader-comicreadercore-cpp"></a>
## `native/comicreader/ComicReaderCore.cpp`

- Status: **CURRENT**
- Accepted blob: `b93c4f84914e361f6bd4110c7642eafc6faa6706`
- Current blob: `b93c4f84914e361f6bd4110c7642eafc6faa6706`
- Source: [`native/comicreader/ComicReaderCore.cpp`](../../native/comicreader/ComicReaderCore.cpp)

```text
// native/comicreader/ComicReaderCore.cpp
```

<a id="file-native-comicreader-comicreadercore-h"></a>
## `native/comicreader/ComicReaderCore.h`

- Status: **CURRENT**
- Accepted blob: `b68638b964d4ca9a2e871b556bb1129717fd8cb0`
- Current blob: `b68638b964d4ca9a2e871b556bb1129717fd8cb0`
- Source: [`native/comicreader/ComicReaderCore.h`](../../native/comicreader/ComicReaderCore.h)

```text
// native/comicreader/ComicReaderCore.h
//
// The ONE app-facing backend for the from-scratch Comic Reader (Agent 1, plan
// 2026-07-23, Task 7). This is where the five pure engine modules become a single
// orchestrated whole and are exposed to QML as the `ComicReaderCore` context
// property plus an `image://comicreader/` provider:
//
//   * ComicReaderTypes / ComicReaderPairing — value types + the canonical
//     double-page pairing walk (units for the current coupling phase).
//   * ComicReaderPageCache — pinned, budgeted LRU of decoded QImages, keyed by
//     (generation, page). Owned here.
//   * ComicReaderDecode — the generation-safe decode coordinator. Constructed
//     and destroyed on THIS object's thread (its affinity invariant); its queued
//     results land back on this thread.
//   * ComicReaderCoupling — the pure auto-coupling probe. Once per Auto+unresolved
//     entry, the core decodes candidate pairs under both phases at LOW priority,
//     collects edgeContinuityCost per pair into the FULL per-phase cost vectors
//     (both non-empty), and calls chooseCouplingPhase exactly once. The two phases
//     routinely have DIFFERENT sample counts (each phase's pairing yields its own
//     seams); mean aggregation in chooseCouplingPhase handles that — the vectors
//     are NOT trimmed to equal length (doing so would drop real seams and could
//     flip the verdict).
//   * ComicReaderStripModel — the Long Strip geometry model. Owned here; fed
//     PageMeta as pages decode; drives the decode window and scroll compensation.
//
// Ported from Tankoban 2's ComicReader.cpp as BEHAVIOUR, not code: no QWidget, no
// GUI-thread decode. QML paints; C++ (this) decides.
//
// Compensation surfacing: the strip model's anti-jump compensation is emitted to
// QML as `stripCompensation(double delta)`. The QML strip surface adds the
// delta to its own scroll position after a decode batch shifts pages above the
// fold. (Documented here per the Task 7 brief's "your call" for this seam.)
//
// A handful of extra Q_INVOKABLE getters (persistedState, pinnedPages,
// cacheBudget, couplingProbeDebug) are additive to the brief's minimum surface:
// persistedState is required for the shell (Task 9) to save/restore state (and is
// the round-trip oracle); the other three are cheap, read-only observability the
// harness and QML debugging use. None mutate state.
```

<a id="file-native-comicreader-comicreadercoupling-cpp"></a>
## `native/comicreader/ComicReaderCoupling.cpp`

- Status: **CURRENT**
- Accepted blob: `018f250d559d6d6768b5783ba23dddf3fc1adca7`
- Current blob: `018f250d559d6d6768b5783ba23dddf3fc1adca7`
- Source: [`native/comicreader/ComicReaderCoupling.cpp`](../../native/comicreader/ComicReaderCoupling.cpp)

```text
// native/comicreader/ComicReaderCoupling.cpp
```

<a id="file-native-comicreader-comicreadercoupling-h"></a>
## `native/comicreader/ComicReaderCoupling.h`

- Status: **CURRENT**
- Accepted blob: `f5f29f1e34c9258a019812bbb62fb1bcfde4730b`
- Current blob: `f5f29f1e34c9258a019812bbb62fb1bcfde4730b`
- Source: [`native/comicreader/ComicReaderCoupling.h`](../../native/comicreader/ComicReaderCoupling.h)

```text
// native/comicreader/ComicReaderCoupling.h
//
// Auto-coupling probe for the Comic Reader (Agent 1, plan 2026-07-23, Task 5).
// Pure scoring + decision: given a pair of already-decoded page images, score
// how visually continuous their touching inner edges are; given the per-pair
// costs measured under each CouplingPhase (Task 2's buildUnits), decide which
// phase to adopt by comparing their MEANS. No Qt GUI, no decode, no file I/O,
// no global state — depends only on QImage (Gui) for pixel sampling and
// Task 1's CouplingPhase enum.
//
// Behavioral reference: TankobanQTGroundWork app_qt/ui/readers/comic_reader.py
// `_edge_continuity_cost` (edge sampling + luminance cost) and
// `_choose_auto_coupling_phase` (confidence + floor/tie decision) — ported to
// clean C++, not copied. The reference's `_score_auto_coupling_phase` returns
// the MEAN of each phase's per-pair costs, and `_choose_auto_coupling_phase`
// compares those two scalar means — NOT sums. Mean is the faithful aggregation
// boundary here because the two phases' sample sets routinely have DIFFERENT
// lengths (`_auto_phase_sample_indexes` derives indices from each phase's own
// pairing units), so summing would disagree with the lineage whenever the
// vectors are unequal length. The backend (Task 7) decodes candidate pairs
// under both phases, collects edgeContinuityCost per pair into
// normalCosts/shiftedCosts (NOT pre-aggregated), and calls chooseCouplingPhase
// once per entry-open.
```

<a id="file-native-comicreader-comicreaderdecode-cpp"></a>
## `native/comicreader/ComicReaderDecode.cpp`

- Status: **CURRENT**
- Accepted blob: `7ed28cbb018c4528213c534cf3d53096a4bddfa4`
- Current blob: `7ed28cbb018c4528213c534cf3d53096a4bddfa4`
- Source: [`native/comicreader/ComicReaderDecode.cpp`](../../native/comicreader/ComicReaderDecode.cpp)

```text
// native/comicreader/ComicReaderDecode.cpp
```

<a id="file-native-comicreader-comicreaderdecode-h"></a>
## `native/comicreader/ComicReaderDecode.h`

- Status: **CURRENT**
- Accepted blob: `7c3717ba5eacce5fd158d1fa9d1d73894f53a5dd`
- Current blob: `7c3717ba5eacce5fd158d1fa9d1d73894f53a5dd`
- Source: [`native/comicreader/ComicReaderDecode.h`](../../native/comicreader/ComicReaderDecode.h)

```text
// native/comicreader/ComicReaderDecode.h
//
// Generation-safe decode coordinator for the Comic Reader (Agent 1, plan
// 2026-07-23, Task 4). This is the reader's concurrency crux: it guarantees a
// rapid entry switch (chapter A -> chapter B) can NEVER paint a stale page,
// because a decode result tagged with a superseded generation is dropped before
// it can ever be inserted into the cache or emitted.
//
// Threading model (the whole point):
//   * The coordinator has ONE owning thread (GUI thread in production, the
//     test's main/event thread in the harness). openGeneration(), request() and
//     onWorkerResult() all run on that thread; ALL coordinator state
//     (m_currentGen, m_pageByIndex, m_inflight) is touched ONLY there. No mutex
//     is needed because nothing else touches that state.
//   * request() enqueues a QRunnable on a QThreadPool capped at 2 threads. The
//     worker is PURE over its captured values (gen, page, localPath): it reads
//     the file bytes and decodes with QImageReader, computes an image-or-error,
//     and reports the result BACK to the owning thread via a QUEUED delivery
//     (QMetaObject::invokeMethod(..., Qt::QueuedConnection)). The worker NEVER
//     touches the cache, the inflight set, or any coordinator state directly.
//   * The worker posts TWO reports on that same queued path, in order: first a
//     HEADER-ONLY dimension hint (QImageReader::size() over the bytes it just
//     read — TB2 DecodeTask's dimensionsReady), then the finished decode. The
//     hint costs a few KB of parsing and lets the strip column snap to real
//     geometry, and pairing learn a spread, without waiting for pixels. It is
//     posted BEFORE the decode runs, so it lands even when the decode FAILS.
//   * onWorkerResult() and onWorkerDimensions() (owning thread) are the STALE
//     GUARD: if the report's gen is not the live generation, it is dropped
//     silently.
//
// Affinity invariant: CONSTRUCT AND DESTROY this object on its owning thread.
// The destructor waits for in-flight workers to finish; any report-back still
// queued after teardown is discarded by ~QObject (the receiver is gone), so a
// stale result can never run against a dead object.
```

<a id="file-native-comicreader-comicreaderimageresponse-cpp"></a>
## `native/comicreader/ComicReaderImageResponse.cpp`

- Status: **CURRENT**
- Accepted blob: `470cceef7294919d7acf75aca5f5d5d8ea141777`
- Current blob: `470cceef7294919d7acf75aca5f5d5d8ea141777`
- Source: [`native/comicreader/ComicReaderImageResponse.cpp`](../../native/comicreader/ComicReaderImageResponse.cpp)

```text
// native/comicreader/ComicReaderImageResponse.cpp
```

<a id="file-native-comicreader-comicreaderimageresponse-h"></a>
## `native/comicreader/ComicReaderImageResponse.h`

- Status: **CURRENT**
- Accepted blob: `4ca0a286748f96da5c9e2b1bf955c08397f4aad4`
- Current blob: `4ca0a286748f96da5c9e2b1bf955c08397f4aad4`
- Source: [`native/comicreader/ComicReaderImageResponse.h`](../../native/comicreader/ComicReaderImageResponse.h)

```text
// native/comicreader/ComicReaderImageResponse.h
//
// One in-flight image://comicreader/ request (Agent 1, overhaul plan
// 2026-07-28, Task 1). ComicReaderProvider hands one of these to QML per page
// hit; it does the cache lookup and the SmoothTransformation downscale on a
// worker thread instead of on Qt's image thread, and it can be CANCELLED — the
// case that matters, because a fast scroll walks past pages whose scale is
// still queued and every one of those is pure waste.
//
// Read-only, exactly like the synchronous provider it replaces: it never
// decodes, never mutates core state, never touches the decode coordinator. It
// reads the mutex-guarded page cache and the ONE live-generation atomic
// ComicReaderCore publishes. A request tagged with a superseded generation
// resolves to NOTHING, so a QML Image still bound to a retired volume can never
// repaint the old chapter's pixels.
//
// That stale guard is NOT byte-for-byte the old behaviour, and the difference
// is worth stating: the synchronous provider read the atomic inline, at request
// time; this reads it on the pool thread at some later moment. So a generation
// that retires between the request and the worker now nulls a request the old
// code would have served. It can never go the other way — a retired generation
// can never start serving again — so the delta is strictly MORE conservative,
// which is the safe direction and is inherent to going async.
//
// Threading + lifetime:
//  - Constructed on the caller's thread (Qt's image thread / the GUI thread);
//    that thread is the response's thread for the rest of its life.
//  - run() is the QRunnable body, executed on the provider's pool. It does the
//    cache read and the scale, then QUEUES publication back to the response's
//    own thread.
//  - Because publication is queued, a cancel() arriving from the response's
//    thread is strictly ORDERED AHEAD of it — cancel never races publication,
//    it simply wins. That is what makes the cancel path deterministic rather
//    than best-effort.
//  - autoDelete is off, so the pool never deletes the response — whoever asked
//    for it owns it, and the QML engine deletes it after finished(). The pool
//    captures that flag BEFORE dispatch (qtbase 6.11's QThreadPoolThread::run()
//    reads autoDelete() into a local, then calls run(), then acts on the local,
//    precisely so an autoDelete(false) runnable may be destroyed during or
//    after run()), so it never revisits the object once run() has returned.
//    Qt's image reader then disposes of the response with deleteLater() on the
//    reader thread. Net guarantee: an owner may destroy the response the
//    instant finished() arrives, with NO window — which is exactly what both
//    harnesses do with a bare delete.
//
// ── Task 2: two tiers, and where the work actually goes ──────────────────────
// The worker now asks the SCALED tier first (ComicReaderScaleCache) and only
// falls through to the full-resolution page on a miss. That is the whole point
// of Task 2: scrolling back one page used to recompute an identical
// SmoothTransformation downscale of a 2400px page; now it is a memory read.
//
// Reading and writing the scaled tier does NOT make this a writer of reader
// state. The scaled tier is a delivery-side store: nothing the reader DECIDES
// (decode priority, pairing, coupling, the strip window) ever reads it. The
// same test passes the metrics counters — they are observation, and no decision
// consults them.
//
// The three tiers, and what each is FOR:
//   preview   — a FastTransformation at the requested size. Cheap, and queued
//               ahead of hq work in the provider's pool, so the frame gets
//               pixels first. That is a strong bias, not a guarantee: with two
//               lanes an hq already running keeps running.
//   hq        — the reader's real page: the default, and what every existing
//               caller gets. Task 7's quality dial governs THIS tier and only
//               this tier — fast/balanced/best pick its resampler and decide
//               whether the tonal maths runs before or after the downscale.
//   thumbnail — SmoothTransformation, but clamped to kThumbnailMaxWidth. A
//               filmstrip entry has no business holding a viewport-sized scale;
//               a hundred of those is the memory bug this task exists to avoid
//               causing.
```

<a id="file-native-comicreader-comicreaderpagecache-cpp"></a>
## `native/comicreader/ComicReaderPageCache.cpp`

- Status: **CURRENT**
- Accepted blob: `de4cad6b0c89ae5ef710901dd313511b119a05c3`
- Current blob: `de4cad6b0c89ae5ef710901dd313511b119a05c3`
- Source: [`native/comicreader/ComicReaderPageCache.cpp`](../../native/comicreader/ComicReaderPageCache.cpp)

```text
// native/comicreader/ComicReaderPageCache.cpp
```

<a id="file-native-comicreader-comicreaderpagecache-h"></a>
## `native/comicreader/ComicReaderPageCache.h`

- Status: **CURRENT**
- Accepted blob: `91a465ebf9e6f50d7caf8da147c19ede3982cf69`
- Current blob: `91a465ebf9e6f50d7caf8da147c19ede3982cf69`
- Source: [`native/comicreader/ComicReaderPageCache.h`](../../native/comicreader/ComicReaderPageCache.h)

```text
// native/comicreader/ComicReaderPageCache.h
//
// Pinned, budgeted LRU page cache for the Comic Reader (Agent 1, plan
// 2026-07-23). Later tasks feed decoded pages in here (Task 4's decode
// coordinator) and pin the visible/neighbor pages so the on-screen frame never
// blanks under memory pressure (Task 7's backend). Depends only on Qt Core/Gui
// (QImage), std, and ComicReaderTypes.h — the reader's neutral foundation
// header, for the shared lock-free raiseMax() this publishes its high-water mark
// through. It knows nothing of the scaled tier, the decoder, or the provider.
//
// Keyed by (generation, page). `generation` is a monotonic counter the caller
// bumps each time a new entry (chapter/volume) opens, so a stale generation's
// pages can be dropped wholesale via clearGeneration without touching the live
// one. Thread-safe: one mutex guards the map + LRU list; nothing heavier than
// list/hash bookkeeping ever runs while it's held (QImage is implicitly shared,
// so storing/copying one under lock is cheap — no decode or scaling lives here).
```

<a id="file-native-comicreader-comicreaderpairing-cpp"></a>
## `native/comicreader/ComicReaderPairing.cpp`

- Status: **CURRENT**
- Accepted blob: `4f2262298fc90170ed53eb66116878b3e01553e0`
- Current blob: `4f2262298fc90170ed53eb66116878b3e01553e0`
- Source: [`native/comicreader/ComicReaderPairing.cpp`](../../native/comicreader/ComicReaderPairing.cpp)

```text
// native/comicreader/ComicReaderPairing.cpp
```

<a id="file-native-comicreader-comicreaderpairing-h"></a>
## `native/comicreader/ComicReaderPairing.h`

- Status: **CURRENT**
- Accepted blob: `e4e5319c9b4bd4383349698b0a439b1ec29bb6aa`
- Current blob: `e4e5319c9b4bd4383349698b0a439b1ec29bb6aa`
- Source: [`native/comicreader/ComicReaderPairing.h`](../../native/comicreader/ComicReaderPairing.h)

```text
// native/comicreader/ComicReaderPairing.h
//
// Pure canonical double-page pairing for the Comic Reader (Agent 1, plan
// 2026-07-23). Ported from Tankoban 2's proven reader as behaviour, not code:
// no QWidget, no QPixmap, no I/O — just the deterministic combinatorial walk.
```

<a id="file-native-comicreader-comicreaderprovider-cpp"></a>
## `native/comicreader/ComicReaderProvider.cpp`

- Status: **CURRENT**
- Accepted blob: `002f057c76e6ceec97393fb28f311ee32e43f4ab`
- Current blob: `002f057c76e6ceec97393fb28f311ee32e43f4ab`
- Source: [`native/comicreader/ComicReaderProvider.cpp`](../../native/comicreader/ComicReaderProvider.cpp)

```text
// native/comicreader/ComicReaderProvider.cpp
```

<a id="file-native-comicreader-comicreaderprovider-h"></a>
## `native/comicreader/ComicReaderProvider.h`

- Status: **CURRENT**
- Accepted blob: `6f301dee0dede12b021e0f38e8096f3f8f6c0c77`
- Current blob: `6f301dee0dede12b021e0f38e8096f3f8f6c0c77`
- Source: [`native/comicreader/ComicReaderProvider.h`](../../native/comicreader/ComicReaderProvider.h)

```text
// native/comicreader/ComicReaderProvider.h
//
// Read-only image://comicreader/ provider for the Comic Reader (Agent 1, plan
// 2026-07-23 Task 7; made ASYNC by the overhaul plan 2026-07-28 Task 1). QML
// asks for a decoded page by URL —
//   image://comicreader/<generation>/<page>?rev=<n>
// — and this provider answers from the backend's pinned LRU cache. It NEVER
// decodes, NEVER mutates any core state, and NEVER touches the coordinator: it
// only reads the cache and consults the ONE live-generation value the backend
// publishes. A request tagged with a superseded generation (a stale QML Image
// still bound to the previous entry) resolves to nothing, so a fast entry
// switch can never repaint the old chapter's pixels. That guard is now checked
// on the worker thread rather than inline, which makes it strictly more
// conservative than the synchronous version — see ComicReaderImageResponse.h.
//
// Why async: the synchronous requestImage() ran the cache copy and the
// SmoothTransformation downscale on Qt's image thread with NO way to cancel, so
// a fast scroll paid in full for every page it had already passed. Each request
// is now a ComicReaderImageResponse scheduled on this provider's own pool, and
// QML can cancel one the moment the page leaves the window.
//
// Who parses the URL: the RESPONSE does, in its constructor — not this provider,
// which only dispatches. Two reasons, and Task 2's author (adding `tier`/`dpr`
// to the grammar) should know them before moving it: the parse then happens on
// the requesting thread rather than burning pool time, and the response stays
// self-contained enough to construct and drive directly, which is what lets the
// harness hold a worker without any production test hook.
//
// Threading: Qt calls requestImageResponse() off the GUI thread. That is safe —
// ComicReaderPageCache is mutex-guarded and the live generation is read through
// a std::atomic. Both are owned by ComicReaderCore and outlive the QML engine
// that owns this provider (the engine takes ownership via addImageProvider()).
```

<a id="file-native-comicreader-comicreaderrenderprofile-cpp"></a>
## `native/comicreader/ComicReaderRenderProfile.cpp`

- Status: **CURRENT**
- Accepted blob: `f89ffa02ffc56221282631bb414b12091ca14258`
- Current blob: `f89ffa02ffc56221282631bb414b12091ca14258`
- Source: [`native/comicreader/ComicReaderRenderProfile.cpp`](../../native/comicreader/ComicReaderRenderProfile.cpp)

```text
// native/comicreader/ComicReaderRenderProfile.cpp
```

<a id="file-native-comicreader-comicreaderrenderprofile-h"></a>
## `native/comicreader/ComicReaderRenderProfile.h`

- Status: **CURRENT**
- Accepted blob: `591ee2756fb06da3a8a7e7f360be410556acd75b`
- Current blob: `591ee2756fb06da3a8a7e7f360be410556acd75b`
- Source: [`native/comicreader/ComicReaderRenderProfile.h`](../../native/comicreader/ComicReaderRenderProfile.h)

```text
// native/comicreader/ComicReaderRenderProfile.h
//
// THE IMAGE ADJUSTMENTS, as a pure function (Agent 1, overhaul plan 2026-07-28,
// Task 7). Plainly: this is "what the reader did to the picture" — a small
// validated struct, and one function that turns a decoded page into the page you
// actually see. Nothing here knows about caches, threads, QML or the reader's
// state; ComicReaderImageResponse calls it on the provider's worker pool and
// ComicReaderCore owns the one live copy.
//
// ── The contract, and why every field is clamped ─────────────────────────────
// The profile is PERSISTED (per series, through the shell's series record), so
// the bytes that reach normalizeRenderProfile() may have been written by a
// future version, hand-edited, or corrupted. Every value is therefore clamped or
// snapped on the way IN, exactly once, at this boundary:
//
//   brightness   -100..100      clamped
//   contrast     -100..100      clamped
//   gamma          10..300      clamped   (hundredths; 100 = 1.0 = untouched)
//   rotation    0/90/180/270    snapped to the NEAREST quarter turn, mod 360
//   autoCrop       bool
//   nightFilter    bool         (see the note below — NOT a pixel operation)
//   quality      fast|balanced|best   unknown -> balanced
//
// A missing key keeps the DEFAULT, and a present-but-unparseable value keeps the
// default too — never 0, which for gamma would mean a black page. That
// distinction is the whole reason this is a function and not a QVariantMap the
// rest of the code reads directly.
//
// ── Identity is byte-stable, and that is load bearing ────────────────────────
// applyRenderProfile(image, RenderProfile{}) returns THE SAME IMAGE — not a
// visually identical copy, the same implicitly-shared object. Everything
// downstream assumes a no-op profile costs nothing: the delivery worker's
// "no scale to do, the source IS the answer" path stays byte-for-byte what it
// was before this file existed, and a reader who never opens the Image panel
// pays not one instruction for it.
//
// ── nightFilter is deliberately NOT applied here ─────────────────────────────
// It rides in the profile because that is where the panel's controls are
// persisted and validated, but applyRenderProfile IGNORES it and it is excluded
// from samePixelsAs(). The reader's night filter is a composited veil over the
// surfaces (ComicReaderShell's nightVeil rectangle, opacity from
// ComicReaderState.nightVeilOpacity) — a control you toggle WHILE looking at a
// page, so it has to be free. Baking it into the pixels would bump the render
// revision, drop every scaled entry and re-scale the visible pages just to dim
// them, and it would leave the reader with two night controls that could
// disagree. One veil, one painter. See the shell for the wiring.
//
// ── The quality dial, and what it actually buys ──────────────────────────────
// The plan sketched `best` as "smooth scaling and the target DPR". There is no
// DPR to use: ComicReaderCore::imageUrl() never emits one, so ScaleKey::dpr100
// is 100 for every request in the app today (Task 2 left that seam inert and
// this task does not build a DPR pipeline). Rather than ship a third segment
// that is a byte-for-byte copy of the second, the three qualities differ by
// something real and free — WHEN the tonal maths runs relative to the resample:
//
//   fast      geometry -> FastTransformation downscale -> tone on the SMALL image
//   balanced  geometry -> SmoothTransformation downscale -> tone on the small image
//   best      geometry -> tone on the FULL-RESOLUTION page -> SmoothTransformation
//
// Resampling and a non-linear tone curve do not commute: adjust-then-resample
// and resample-then-adjust genuinely differ wherever gamma or contrast clips,
// and adjusting first is the more correct order. It is also the expensive one —
// a 2400x3600 page is ~8.6M pixels of LUT work against ~1.5M for the scaled
// copy — so the names describe the real cost. With a default profile all three
// collapse to today's behaviour and `fast` is the only one that changes a pixel
// (its resampler), which is exactly what a quality dial should do.
//
// RenderStage is what lets the delivery worker split that pipeline without a
// second entry point: Geometry is the half that changes DIMENSIONS (and so must
// always run before a width-driven scale), Tone is the half that only changes
// values.
```

<a id="file-native-comicreader-comicreaderscalecache-cpp"></a>
## `native/comicreader/ComicReaderScaleCache.cpp`

- Status: **CURRENT**
- Accepted blob: `e15a9675452a71cd7f07c2b80697ce497da9c997`
- Current blob: `e15a9675452a71cd7f07c2b80697ce497da9c997`
- Source: [`native/comicreader/ComicReaderScaleCache.cpp`](../../native/comicreader/ComicReaderScaleCache.cpp)

```text
// native/comicreader/ComicReaderScaleCache.cpp
```

<a id="file-native-comicreader-comicreaderscalecache-h"></a>
## `native/comicreader/ComicReaderScaleCache.h`

- Status: **CURRENT**
- Accepted blob: `19b18d8ec13018901a5512a3d0d98af292b2c702`
- Current blob: `19b18d8ec13018901a5512a3d0d98af292b2c702`
- Source: [`native/comicreader/ComicReaderScaleCache.h`](../../native/comicreader/ComicReaderScaleCache.h)

```text
// native/comicreader/ComicReaderScaleCache.h
//
// The SCALED tier for the Comic Reader (Agent 1, overhaul plan 2026-07-28,
// Task 2), plus the delivery counters and the read-only bundle a provider and
// its responses serve from.
//
// What it is, plainly: the decoded page cache answers "what does this page look
// like"; this answers "what does this page look like AT THIS SIZE". Task 1 moved
// the SmoothTransformation downscale off the GUI thread but every provider hit
// still recomputed it from the full-resolution page — scroll back one page and
// the identical scale ran again. That recomputation is the remaining hot path
// behind the long-strip stutter, and this cache is what removes it.
//
// ── Why an app-side tier at all, when QML already has one ────────────────────
// QQuickPixmapCache is a scaled cache too: with `cache: true` an Image keyed by
// (url, sourceSize) keeps its result, which is why the surfaces bother to hold
// sourceSize still. It is not enough for two reasons, and they are worth stating
// because it is the first question any reader of this file will have:
//   1. Its unreferenced-entry budget is small and it is process-global — the
//      posters, covers and chrome art of the rest of Colosseum evict reader
//      pages out of it. We cannot size it for a comic reader without sizing it
//      for everything.
//   2. It only ever holds what an Image still points at or recently released.
//      A page scrolled two screens away is unreferenced immediately, so the
//      exact motion this task exists to fix — scroll away and come back — is
//      the motion its policy is worst at.
// This tier is reader-owned, reader-sized, and swept by the reader's own
// viewport, which is what makes retainRange meaningful at all.
//
// ── The bound: the WINDOW governs, bytes are the safety net ──────────────────
// This is a SECOND image cache, so it has to be bounded, but the first cut of
// this file got the bounding backwards and it is worth writing down why, because
// the mistake is easy to repeat.
//
// That version made a fixed 64 MiB byte budget the operative ceiling and an
// entry cap the "bound that actually bites", on the premise that a scaled entry
// is viewport-sized and therefore small. THE PREMISE WAS FALSE. Scaled entries
// are not viewport-sized — they are `srcCapW`-sized, and srcCapW is a SCREEN-
// and-zoom constant that deliberately does not follow the window:
//   - ComicReaderStripSurface.qml: max(1100, min(2048, screen rounded to 256)).
//     On a 1920-wide screen that is 2048, and its comment explains that tying it
//     to the window re-decoded the whole visible column on every fullscreen
//     toggle (Hemanth, 2026-07-26: "incredibly rough"). It is fixed by design.
//   - ComicReaderDoubleSurface.qml: 1400 / 2048 / 2800 by zoom.
// A 2048-wide colour page of ordinary manga proportions is ~24 MB. So 64 MiB
// held TWO entries, not eight — and one entry under memory saver, or for a
// zoomed pair. The tier disabled itself precisely where it was needed most: a
// three-page viewport missed on every scroll step, and the two halves of a
// spread evicted each other. Worse, which bound bit flipped with PIXEL FORMAT
// (an 8-bit grayscale scan is ~6 MB, where the entry cap did bite), so the same
// code behaved differently on different books. No fixture caught it because
// every fixture uses 4 MiB or 24 KB images, regimes where the entry cap really
// does bite.
//
// So the bounding is now the other way round, and there is one rule:
//
//   THE RETAINED WINDOW GOVERNS RESIDENCY. The capacity — how many scaled
//   entries the reader wants to keep — is the ONE operating bound, and the
//   reader sets it from the page range it actually asked to retain (setCapacity,
//   driven by ComicReaderCore::requestRange). The byte figure is demoted to a
//   pure safety ceiling that must not bite in normal reading.
//
// Two bounds, then, and it is worth being exact about when each one can fire,
// because "derive the budget from entry size × window" was tried here first and
// is a longer way of writing the capacity cap. Let S be the largest entry the
// tier holds. Residency obeys bytesUsed <= count * S <= capacity * S, so ANY
// byte budget at or above capacity * S is arithmetically incapable of evicting
// anything the capacity cap has not already evicted. A derived budget is
// therefore not a second bound at all — it is decoration. What remains is a
// fixed ceiling BELOW that product, which is exactly what a safety net is:
//
//   count > capacity      -> the normal, governing bound (the window)
//   bytes > hardCeiling   -> the stop, and it only fires when entries are big
//                            enough that the window would not have fit in RAM
//
// The ceiling is sized so the retained PAGE window always fits at one scale per
// page even in the worst realistic case, with the per-page replacement
// allowance (see kDefaultCapacity) on top wherever there is room:
//   - strip, 3 pages visible -> 6-page window, colour at srcCapW 2048
//     (2048x3072 ARGB32 = 24 MiB): 144 MiB held, 240 MiB with the allowance,
//     both under the 256 MiB ceiling — the window is held whole.
//   - double page, zoom >= 180% -> 5-page window at srcCapW 2800
//     (2800x4200 = 44.9 MiB): 224 MiB, under the ceiling. The allowance is what
//     gets trimmed here, never the window.
// Memory saver (128 MiB) DOES make the ceiling the operative bound, and that is
// the point of the setting rather than a regression: it holds 5 of a 6-page
// colour strip window, and exactly 2 entries of a zoomed spread — which is both
// halves of what is on screen, so the failure the first cut had (the two halves
// of a spread evicting each other) does not come back even at the tightest
// setting.
//
// Every eviction is counted (DeliveryMetrics::scaledEvictions) and the live byte
// total is published (scaledBytesUsed), so Task 12 settles the sizing by
// measurement instead of by arithmetic in a header comment — including this one.
//
// There is no pinning here, deliberately. A pin exists so an on-screen page can
// never blank; a scaled entry that goes missing costs a rescale, and the decoded
// tier is where blanking is actually prevented. Note the honest version of that
// claim: the rescale needs the full-resolution page, and the decoded tier's own
// budget may ALREADY have evicted it (its eviction skips pinned entries only, not
// in-window ones — the two windows overlap but overlap is not co-residency). When
// that happens the miss costs a full re-decode from disk, not a rescale. It is
// still not worth pinning here — a pin would hold scaled bytes hostage to fix a
// decoded-tier residency problem — but "by definition still decoded" would be a
// false guarantee, so it is not made.
//
// Thread-safe the same way ComicReaderPageCache is: one mutex over the map and
// the LRU list, nothing heavier than list/hash bookkeeping under it (QImage is
// implicitly shared, so storing or copying one while holding the lock is cheap).
```

<a id="file-native-comicreader-comicreaderstripmodel-cpp"></a>
## `native/comicreader/ComicReaderStripModel.cpp`

- Status: **CURRENT**
- Accepted blob: `b873834511c060e4916c70d9ad784c347f7e366f`
- Current blob: `b873834511c060e4916c70d9ad784c347f7e366f`
- Source: [`native/comicreader/ComicReaderStripModel.cpp`](../../native/comicreader/ComicReaderStripModel.cpp)

```text
// native/comicreader/ComicReaderStripModel.cpp
```

<a id="file-native-comicreader-comicreaderstripmodel-h"></a>
## `native/comicreader/ComicReaderStripModel.h`

- Status: **CURRENT**
- Accepted blob: `97562d824bbf4a181ed16ff202b5cdf9bc17776b`
- Current blob: `97562d824bbf4a181ed16ff202b5cdf9bc17776b`
- Source: [`native/comicreader/ComicReaderStripModel.h`](../../native/comicreader/ComicReaderStripModel.h)

```text
// native/comicreader/ComicReaderStripModel.h
//
// Pure geometry model for the Comic Reader (Agent 1, plan 2026-07-23) Long
// Strip surface (Task 10). A QAbstractListModel exposing one row per page
// with its vertical layout (top offset, display width/height) — no image
// decode, no cache, no I/O, no QML registration here. The backend (Task 7)
// owns one instance, feeds it PageMeta via rebuild()/updatePage() as pages
// decode, and drives its own decode-window/pixel-cache decisions from
// window()/pageAtCenter().
//
// Geometry law — ported from Tankoban 2's ScrollStripCanvas
// (rebuildYOffsets/targetPageWidth/firstVisiblePage) and
// TankobanQTGroundWork's comic_reader.py on_page_loaded (see the .cpp for the
// exact line-by-line mapping):
//   - A page whose real size has never been learned displays at an ESTIMATED
//     1600x2400 (portrait) source size.
//   - The moment a page's real sourceSize is LEARNED, that size is LOCKED IN
//     for this model's lifetime (until the next rebuild()). The trigger is the
//     SIZE alone, never `meta.decoded`: the decode coordinator publishes a
//     header-only dimension hint (TB2's DecodeTask::dimensionsReady) carrying
//     real dimensions with decoded=false, ahead of — and independently of —
//     the full decode, and geometry that true must not wait for pixels. A
//     later updatePage() that reports decoded=false with a ZEROED sourceSize
//     (a page-cache eviction, not a fresh decode) must NOT revert the page
//     back to the estimate — TB2 has no "undecode" event either;
//     ScrollStripCanvas's per-page dimension slots are sticky, and only its
//     SEPARATE scaled-pixmap cache evicts (evictScaledOutsideZone).
//     `ReadyRole` tracks the live decoded flag SEPARATELY from the sticky
//     size, so QML can show a placeholder frame while pixels are absent —
//     never yet decoded, or evicted — even though the page's box in the strip
//     already holds its known height.
//   - Spread pages (effective spread = spreadOverride if set, else
//     detectedSpread) span the full viewport width; portrait pages span
//     portraitWidthPct% of it.
//   - top(i) is the running sum of every earlier page's (displayHeight +
//     gap); contentHeight is that same sum through the last page, WITHOUT a
//     trailing gap after it (matches ScrollStripCanvas::totalHeight()).
```

<a id="file-native-comicreader-comicreadertypes-cpp"></a>
## `native/comicreader/ComicReaderTypes.cpp`

- Status: **CURRENT**
- Accepted blob: `9469059b8fff2334bff76cd3ccb8a2c741f67d41`
- Current blob: `9469059b8fff2334bff76cd3ccb8a2c741f67d41`
- Source: [`native/comicreader/ComicReaderTypes.cpp`](../../native/comicreader/ComicReaderTypes.cpp)

```text
// native/comicreader/ComicReaderTypes.cpp
```

<a id="file-native-comicreader-comicreadertypes-h"></a>
## `native/comicreader/ComicReaderTypes.h`

- Status: **CURRENT**
- Accepted blob: `5c495c6bf970a10218d97c08483e4160eacd0e25`
- Current blob: `5c495c6bf970a10218d97c08483e4160eacd0e25`
- Source: [`native/comicreader/ComicReaderTypes.h`](../../native/comicreader/ComicReaderTypes.h)

```text
// native/comicreader/ComicReaderTypes.h
//
// Typed boundary for the from-scratch Comic Reader (Agent 1, plan 2026-07-23).
// Pure value types + QVariant (de)serialization for the manga/comic/Tankoban
// reader engine. No Qt GUI, no image decode, no cache, no I/O — this is the
// combinatorial foundation every native/comicreader/ unit builds on.
```

<a id="file-native-engine-cbzarchive-cpp"></a>
## `native/engine/CbzArchive.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `7cc8a2a4d2b2b16e31a0fe66b404be0343759514`
- Current blob: `7cc8a2a4d2b2b16e31a0fe66b404be0343759514`
- Source: [`native/engine/CbzArchive.cpp`](../../native/engine/CbzArchive.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-cbzarchive-h"></a>
## `native/engine/CbzArchive.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `83443aa5cbb2f870a34c0d5923aabaf3a1da9d87`
- Current blob: `83443aa5cbb2f870a34c0d5923aabaf3a1da9d87`
- Source: [`native/engine/CbzArchive.h`](../../native/engine/CbzArchive.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-comiccoverid-cpp"></a>
## `native/engine/ComicCoverId.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `6b87c0ba5793f05bf634f04b5f12a482a94a7ed3`
- Current blob: `6b87c0ba5793f05bf634f04b5f12a482a94a7ed3`
- Source: [`native/engine/ComicCoverId.cpp`](../../native/engine/ComicCoverId.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-comiccoverid-h"></a>
## `native/engine/ComicCoverId.h`

- Status: **CURRENT**
- Accepted blob: `f4464eb5f2d811c3caaf4808556f4fe63946d450`
- Current blob: `f4464eb5f2d811c3caaf4808556f4fe63946d450`
- Source: [`native/engine/ComicCoverId.h`](../../native/engine/ComicCoverId.h)

```text
// native/engine/ComicCoverId.h
//
// The id half of image://comiccover/<id> (Task 3, 2026-08-06 CBZ-in-place
// arc) -- pure QString/QByteArray, Qt6::Core only. Split out of
// ComicCoverProvider.{h,cpp} on an Opus-advisor review: ComicDownloader.cpp
// is the only OTHER caller (its downloadedIssues() builds the art URL for an
// archive row), and it compiles into four harness targets that have no other
// reason to link Qt6::Gui/Qt6::Quick or pull in CbzArchive.cpp/miniz.c --
// buildId() being a string function living in the QQuickImageProvider
// translation unit dragged all of that into every one of them.
```

<a id="file-native-engine-comiccoverprovider-cpp"></a>
## `native/engine/ComicCoverProvider.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `250701755084ab41be338190f1e0d2ee16536a68`
- Current blob: `250701755084ab41be338190f1e0d2ee16536a68`
- Source: [`native/engine/ComicCoverProvider.cpp`](../../native/engine/ComicCoverProvider.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-comiccoverprovider-h"></a>
## `native/engine/ComicCoverProvider.h`

- Status: **CURRENT**
- Accepted blob: `1e33ca0f7635304e77791a6a6d17a5f6527ae001`
- Current blob: `1e33ca0f7635304e77791a6a6d17a5f6527ae001`
- Source: [`native/engine/ComicCoverProvider.h`](../../native/engine/ComicCoverProvider.h)

```text
// native/engine/ComicCoverProvider.h
//
// Task 3 (2026-08-06 comics CBZ-in-place arc): a cover thumbnail for an
// archive-shaped comic row has no loose page file to point at (that was the
// whole point of Task 2). This provider decodes one straight from the CBZ
// instead -- image://comiccover/<base64url(archivePath)>/<base64url(entryName)>.
//
// Fully STATELESS by design: it never holds a pointer to ComicDownloader or
// touches its m_index. Quick can call requestImage() off the GUI thread, so a
// stateful provider reaching into ComicDownloader would be a real
// GUI-thread/pool-thread race, not a theoretical one -- the self-contained
// URL sidesteps it entirely (same reasoning as Player2SubtitleImageProvider's
// weak-handle note, applied by removing the handle altogether).
//
// Synchronous (QQuickImageProvider::Image), unlike ComicReaderProvider's own
// QQuickAsyncImageProvider: that one exists for the reader's high-frequency
// page-turn path, where a blocking decode on Qt's image thread would stall
// every other pending request behind it. This provider serves the occasional
// library-grid thumbnail load -- a handful of requests, not a page-turn
// stream -- so a plain synchronous decode is the right-sized tool, not a
// smaller version of the reader's problem.
//
// Legacy `dir`-shaped rows are untouched by this provider -- they keep
// emitting a plain file:// URL from ComicDownloader::downloadedIssues(),
// unchanged. No UX gap mid-migration: only archive rows (Task 4+) ever
// produce an image://comiccover/ URL at all.
//
// The id itself (build/parse) lives in ComicCoverId.h, Qt6::Core only --
// ComicDownloader.cpp is the other caller (its downloadedIssues() builds the
// art URL for an archive row) and compiles into four Core/Network-only
// harness targets that have no other reason to link Qt6::Gui/Qt6::Quick or
// pull in CbzArchive.cpp/miniz.c.
```

<a id="file-native-engine-comicdlsparse-cpp"></a>
## `native/engine/ComicDlsParse.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `54472c0ea593a11728c87813688c8e6c6ef38124`
- Current blob: `54472c0ea593a11728c87813688c8e6c6ef38124`
- Source: [`native/engine/ComicDlsParse.cpp`](../../native/engine/ComicDlsParse.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-comicdlsparse-h"></a>
## `native/engine/ComicDlsParse.h`

- Status: **CURRENT**
- Accepted blob: `6a1956ea5edf20b312d9a46ed4a830d6c4641033`
- Current blob: `6a1956ea5edf20b312d9a46ed4a830d6c4641033`
- Source: [`native/engine/ComicDlsParse.h`](../../native/engine/ComicDlsParse.h)

```text
// ComicDlsParse.h — the GetComics release-post link parser, extracted from
// ComicDownloader as a free function so the contract is harness-testable
// (Core-only, no Network). Returns signed /dls/ hrefs best-first:
// DOWNLOAD NOW > MAIN SERVER > aio-red > rest. The bare /dls/<token>/ ad-gate
// (no ":sig" payload) is excluded (the TB2 2026-06-05 scar). Anchors labeled
// pixeldrain — in attributes OR inner text — are DROPPED: the host is blocked
// from this ISP (http=000, probed 2026-07-10); a dead host is not a fallback.
```

<a id="file-native-engine-comicdownloader-cpp"></a>
## `native/engine/ComicDownloader.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `a2ecdb94d56b66236befadd429bd40d34c7aaa54`
- Current blob: `a2ecdb94d56b66236befadd429bd40d34c7aaa54`
- Source: [`native/engine/ComicDownloader.cpp`](../../native/engine/ComicDownloader.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-comicdownloader-h"></a>
## `native/engine/ComicDownloader.h`

- Status: **CURRENT**
- Accepted blob: `84f935ea068c013d76acb4218b750f2832b60e95`
- Current blob: `84f935ea068c013d76acb4218b750f2832b60e95`
- Source: [`native/engine/ComicDownloader.h`](../../native/engine/ComicDownloader.h)

```text
// ComicDownloader.h
//
// The western-comics half of the download-fed backbone: reading is NEVER a live
// stream. A GetComics release post is one archive file (.cbr/.cbz) — the volume
// unit. This is the BookDownloader lineage (single-file HTTP stream, .part →
// rename, stale-HTML failover), NOT MangaDownloader's page→cbz pipeline, plus
// one ingest stage: land the volume as ONE canonical CBZ in the library. Since
// the CBZ-in-place arc (2026-08-06) a natively-readable CBZ MOVES into place with
// no extraction at all; anything else extracts then repacks into a CBZ.
//
// Pipeline (design: docs/superpowers/specs/2026-07-04-colosseum-western-comics-
// getcomics-design.md, ratified — GetComics for both catalog and download):
//   1. resolve: GET the release post → parse the FULL signed "DOWNLOAD NOW"
//      href (getcomics.org/dls/<payload>:<sig>==). The bare /dls/<token>/ link
//      is the ad-gate (TB2's 2026-06-05 scar); the signed one 302s straight to
//      comicfiles.ru, clean HTTP, no browser. Mirror links are kept as failover.
//   2. stream: GET the signed URL with Chrome UA + getcomics.org Referer →
//      write <dir>/<file>.part in chunks (readyRead, NEVER readAll — TPBs run
//      300MB–1GB), text/html first-chunk detection (ad-gate/interstitial ⇒
//      failover to next link), retry 2/4/8s, atomic rename.
//   3. ingest (TWO PATHS — CBZ-in-place, 2026-08-06): probe the download first.
//      A natively-readable CBZ is MOVED into the library as-is — no extraction,
//      no repack, no loose pages. Anything else (RAR/cbr, cb7, cbt, or a CBZ the
//      reader cannot open) is extracted and repacked into one canonical CBZ.
//      Extraction still uses the OS's bundled bsdtar (C:\Windows\System32\tar.exe,
//      libarchive — reads BOTH; proven on the real Kyoshi Warriors #2 RAR5, 25
//      pages in 1.6s), with an installed 7-Zip as fallback. No vendored
//      libunrar/7z — reduction reflex. On SUCCESS the source archive is consumed
//      (moved, or copied then deleted); on FAILURE it is PRESERVED, not deleted.
//   4. index: {issueId → seriesId, title, archive|dir, files, bytes}. `archive`
//      wins whenever both are set (see Entry's storage-precedence note); a legacy
//      loose-folder row has `archive` empty and is migrated on boot. localPages(id)
//      returns the same [{index, url, group}] shape Downloads.localPages does,
//      so the reader reads western issues through the same machinery.
//
// On-disk layout (AppDataLocation, not the purgeable cache):
//   <appdata>/comics/<series>/<issue>-<hash10>.cbz      ← canonical, archive rows
//   <appdata>/comics/<series>/<issue>-<hash10>/page_000.jpg ...  ← legacy, migrating
//   <appdata>/comics/index.json
```

<a id="file-native-engine-comiceditionassembler-cpp"></a>
## `native/engine/ComicEditionAssembler.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `ae3fb19036931d7750cf457c9146f7af74fe919d`
- Current blob: `ae3fb19036931d7750cf457c9146f7af74fe919d`
- Source: [`native/engine/ComicEditionAssembler.cpp`](../../native/engine/ComicEditionAssembler.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-comiceditionassembler-h"></a>
## `native/engine/ComicEditionAssembler.h`

- Status: **CURRENT**
- Accepted blob: `fdb3afbc25fa408f1a2645a94028d98a150bd83b`
- Current blob: `fdb3afbc25fa408f1a2645a94028d98a150bd83b`
- Source: [`native/engine/ComicEditionAssembler.h`](../../native/engine/ComicEditionAssembler.h)

```text
// Converts a selected comic-edition payload (design: docs/superpowers/specs/
// 2026-07-15-colosseum-tankorent-comic-volume-mode-design.md, "Assembly and
// publication") into ONE complete, validated page staging directory without
// ever publishing partial output. Publication — moving a finished staging
// directory into the comics library under its catalog chId — is a separate
// task; this module only stages.
//
// Extraction mirrors ComicDownloader's bsdtar-then-7-Zip executable-discovery
// and extraction policy (native/engine/ComicDownloader.cpp). Image validation
// is the same magic-byte sniff MangaTankoban::MangaVolumeArchiveIngestor uses
// (native/engine/MangaVolumeArchiveIngestor.cpp) — a cheap "is this a real
// image" gate that stays Qt::Core-only.
```

<a id="file-native-engine-comicpacklabels-h"></a>
## `native/engine/ComicPackLabels.h`

- Status: **CURRENT**
- Accepted blob: `8c6ad008721d73cad414f3bd1defc1a92af5c4da`
- Current blob: `8c6ad008721d73cad414f3bd1defc1a92af5c4da`
- Source: [`native/engine/ComicPackLabels.h`](../../native/engine/ComicPackLabels.h)

```text
// ComicPackLabels.h — volume label parser for multi-volume pack demux
//
// A "pack" is one downloaded archive that turns out to contain N nested comic
// archives (e.g. the live Chew v1–v8 + Extras GetComics post: one ZIP whose top
// folder holds 12 complete .cbr/.cbz files, one per volume). The demux (see
// docs/superpowers/specs/2026-08-06-comics-multivolume-pack-demux-design.md)
// ingests each nested file as its own library entry under a shared seriesId;
// this parser maps each nested filename to the three things the shelf/reader
// need: a display label, a main-vs-extra role, and a deterministic order.
//
// This is a PURE function over a QString (the nested file's path relative to
// the pack's extract tree — exact bytes as extracted). No file IO, no Qt app
// context, no mutable state: it is safe to call from any thread and cheap to
// table-test directly (the harness links this header to exercise the dozen
// real Chew names as a literal table, which is why the parser lives in a
// header rather than ComicDownloader.cpp's anonymous namespace).
//
// Contract (the plan's "Label parser" + "Volume labels and roles"):
//   - v(\d+) anywhere in the name, zero-pad normalized → "Vol. N", role main,
//     order N. ("v1" and "v05" both resolve to their integer.)
//   - A "Bonus" token AND a v(\d+) match → role extra, label "Vol. N — Bonus",
//     order N (the bonus sorts with its volume's neighborhood, after mains).
//   - "Script Book" (case-insensitive token) → role extra, label "Script Book",
//     order after every parsed main (natural-sort sentinel, see kAfterMains).
//   - Any other unmatched named special (no v(\d+), no recognised token) →
//     role extra, label from the cleaned filename stem, order after mains.
//   - Unparseable (empty / all-noise) → role main, order kAfterMains, so it is
//     ALWAYS readable and ordered after the parsed mains by natural sort —
//     never hidden, never lost.
//   - Non-ASCII in source names (e.g. the real Chew `´` in "Taster´s") MUST
//     round-trip safely. The parser operates on QString (UTF-16) end to end;
//     it never re-encodes, so any Unicode the filesystem handed us survives.
//
// Serialization: role/order persist on the index Entry as packRole/packOrder
// (optional fields; absent = ordinary single issue, all legacy rows unchanged).
// They are the ONLY inputs the QML shelf needs to build the mains-only crossing
// chain and the Extras group — so the parser's output shape is a stable contract
// the reader chain depends on. See Slice 4's packVolumes() read API.
```

<a id="file-native-engine-comickcatalogclient-cpp"></a>
## `native/engine/ComickCatalogClient.cpp`

- Status: **CURRENT**
- Accepted blob: `5f4da49ed2597c16d44dd457ed3f34a8479033d4`
- Current blob: `5f4da49ed2597c16d44dd457ed3f34a8479033d4`
- Source: [`native/engine/ComickCatalogClient.cpp`](../../native/engine/ComickCatalogClient.cpp)

```text
// ComickCatalogClient.cpp — see the header for the two-step story.
```

<a id="file-native-engine-comickcatalogclient-h"></a>
## `native/engine/ComickCatalogClient.h`

- Status: **CURRENT**
- Accepted blob: `5ec065c58c02b6e0823fe9b3fbcacb9d201be92c`
- Current blob: `5ec065c58c02b6e0823fe9b3fbcacb9d201be92c`
- Source: [`native/engine/ComickCatalogClient.h`](../../native/engine/ComickCatalogClient.h)

```text
// ComickCatalogClient.h
//
// Volume-structure source for tankoban mode. It is the only one: MangaEngine and QML
// see nothing but the catalogReady/catalogFailed contract below — exactly one of the
// two per call, carrying the title back so a caller can match it. Two steps:
//   1. DB read  — raw.githubusercontent.com/kingoftheseas56/colosseum-volume-db/
//                 main/db/<weebcentral-ulid>.json, unauthenticated, kept warm by our
//                 batch job. The record carries volumes plus its own gate verdict.
//   2. On miss  — live Comick scrape (api.comick.dev, token-free, browser UA REQUIRED
//                 or 403): search by title -> hid -> chapters across ALL languages
//                 (en-only tagging is sparse — the My Hero Academia finding) ->
//                 ComickVolumeGrouper.
// Either path ends at the completeness gate. Gate-fail => catalogFailed => the app
// shows the flat WeebCentral chapter list. There is NO interpolation anywhere.
//
// Emitted volumes: ascending QVariantMap{number:double, cover:"", chapterStart, chapterEnd}.
// cover is ALWAYS empty. Undownloaded tiles use the shelf's numbered placeholder; a
// downloaded volume's cover is its own first page (MangaVolumeIndex). WeebCentral's
// chapter list carries no thumbnails (verified 2026-07-29) and Comick's per-volume
// covers don't exist, so there is nothing to fetch.
//
// Threading: pure QNetworkAccessManager + QObject::connect lambdas, all on the main
// thread; each fetch carries its own PendingFetch via shared_ptr, so concurrent calls
// never share state — there is no per-client mutable request state at all.
//
// ACCEPTED LIMIT, written down rather than left to be discovered: there is no
// destructor and no in-flight abort. Destroying the client with a request outstanding
// severs the connection, so that call emits NEITHER signal and its reply is never
// deleteLater'd. That is an accepted limit, not a regression, and unreachable while
// MangaEngine owns the client for the app's lifetime — but anything that starts
// creating and destroying these per-page has to fix it first, because a dropped call
// hangs the page-reveal gate that waits on the one-signal-per-call promise.
```

<a id="file-native-engine-comickvolumegrouper-cpp"></a>
## `native/engine/ComickVolumeGrouper.cpp`

- Status: **CURRENT**
- Accepted blob: `6c05bce4ec8be309903b73bf31ff9144efdbcfd4`
- Current blob: `6c05bce4ec8be309903b73bf31ff9144efdbcfd4`
- Source: [`native/engine/ComickVolumeGrouper.cpp`](../../native/engine/ComickVolumeGrouper.cpp)

```text
// ComickVolumeGrouper.cpp — see the header. Every function here is a line-for-line
// port of colosseum-volume-db/comick_volume_db/volume_builder.py; the comments carry
// that file's reasoning across, including what it CHECKS versus what it ASSUMES.
```

<a id="file-native-engine-comickvolumegrouper-h"></a>
## `native/engine/ComickVolumeGrouper.h`

- Status: **CURRENT**
- Accepted blob: `d796cc93b04a7aa0dc0c2c631898af891b432136`
- Current blob: `d796cc93b04a7aa0dc0c2c631898af891b432136`
- Source: [`native/engine/ComickVolumeGrouper.h`](../../native/engine/ComickVolumeGrouper.h)

```text
// ComickVolumeGrouper.h — pure logic mirrored 1:1 from the Python reference
// (colosseum-volume-db/comick_volume_db/volume_builder.py). Comick chapter rows
// -> majority-voted chapter->volume assignment -> ordered volume ranges -> the
// completeness gate. This is the app's live-scrape core; the Python is the batch
// core. If the two ever diverge, the same series renders differently depending on
// whether it came from the database or a live scrape — keep them mirrored,
// test-for-test.
//
// The gate is doctrine, not tuning: a series qualifies for tankoban mode ONLY when
// its volumes form one unbroken run with real, non-overlapping, fully-covered
// chapter ranges. Everything else falls back to the flat chapter list. NEVER add
// interpolation or estimation here — permanently rejected.
//
// Rows arrive from ALL languages, so one chapter number turns up many times and
// uploaders occasionally disagree about its volume; majorityAssign settles that per
// chapter before grouping. Rows with no `vol` are ignored by the assignment (they
// become the app's "Latest chapters" shelf, derived live elsewhere) but they are
// still real chapters, so gateVolumes counts them when it checks coverage.
```

<a id="file-native-engine-comicscatalog-cpp"></a>
## `native/engine/ComicsCatalog.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `92413f21e2aedd88bd9a8c842da9e853932e2bf4`
- Current blob: `92413f21e2aedd88bd9a8c842da9e853932e2bf4`
- Source: [`native/engine/ComicsCatalog.cpp`](../../native/engine/ComicsCatalog.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-comicscatalog-h"></a>
## `native/engine/ComicsCatalog.h`

- Status: **CURRENT**
- Accepted blob: `692e0db80b9a0f0ad61320af86191be918cfc75e`
- Current blob: `692e0db80b9a0f0ad61320af86191be918cfc75e`
- Source: [`native/engine/ComicsCatalog.h`](../../native/engine/ComicsCatalog.h)

```text
// ComicsCatalog — read-only seam onto the availability-first SQLite catalogue
// (spec 2026-07-17 phase 2b). QML paints, C++ decides: the db, its schema, and
// query shape live here; QML gets QVariant maps/lists. Point queries are indexed
// and sub-ms at this scale (21.5k series / 53k downloads), so the API is
// synchronous — no async ceremony a 31 MB local file doesn't need.
// Missing/invalid db => ready()==false and every accessor returns empty (the app
// runs on without the catalogue — it is pipeline-deployed, not shipped).
```

<a id="file-native-torrent-comiccoverage-cpp"></a>
## `native/torrent/ComicCoverage.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `0494a29c42afca5ec640d14cdefd3caf205f855b`
- Current blob: `0494a29c42afca5ec640d14cdefd3caf205f855b`
- Source: [`native/torrent/ComicCoverage.cpp`](../../native/torrent/ComicCoverage.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comiccoverage-h"></a>
## `native/torrent/ComicCoverage.h`

- Status: **CURRENT**
- Accepted blob: `ff361333daa34f2d5def59cd55c8d8923d7916b1`
- Current blob: `ff361333daa34f2d5def59cd55c8d8923d7916b1`
- Source: [`native/torrent/ComicCoverage.h`](../../native/torrent/ComicCoverage.h)

```text
// Pure format-aware coverage grammar for the Tankorent Comic volume-mode
// feature. Reads a torrent/file/dir name and reports which collection
// formats + inclusive number-ranges it advertises, so "Compendiums v01-v03"
// is recognized as covering Compendium 1..3 but never TPB 1 or issue 1.
// No network, no Qt GUI, no file I/O.
```

<a id="file-native-torrent-comiceditionfileselector-cpp"></a>
## `native/torrent/ComicEditionFileSelector.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `bf1636f1ee04e843383392d43b4a42434c0a392c`
- Current blob: `bf1636f1ee04e843383392d43b4a42434c0a392c`
- Source: [`native/torrent/ComicEditionFileSelector.cpp`](../../native/torrent/ComicEditionFileSelector.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comiceditionfileselector-h"></a>
## `native/torrent/ComicEditionFileSelector.h`

- Status: **CURRENT**
- Accepted blob: `3b7c2cd3a4a3193a56c49feed4b1d317b705bb00`
- Current blob: `3b7c2cd3a4a3193a56c49feed4b1d317b705bb00`
- Source: [`native/torrent/ComicEditionFileSelector.h`](../../native/torrent/ComicEditionFileSelector.h)

```text
// Pure manifest-selection module for the Tankorent Comic volume-mode feature
// (design: docs/superpowers/specs/2026-07-15-colosseum-tankorent-comic-
// volume-mode-design.md, "Manifest selection"). Given a torrent's file
// manifest and the canonical edition target, decides the SAFE payload — the
// file(s) inside the pack that ARE the requested edition — or a typed
// failure. Never guesses: a format-scoped or issue-range mismatch always
// yields a typed failure rather than a wrong file. No network, no Qt GUI, no
// real file I/O.
```

<a id="file-native-torrent-comiceditionidentity-cpp"></a>
## `native/torrent/ComicEditionIdentity.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `f6da74b158be400a92954f384d955ac2566522ad`
- Current blob: `f6da74b158be400a92954f384d955ac2566522ad`
- Source: [`native/torrent/ComicEditionIdentity.cpp`](../../native/torrent/ComicEditionIdentity.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comiceditionidentity-h"></a>
## `native/torrent/ComicEditionIdentity.h`

- Status: **CURRENT**
- Accepted blob: `7b1e50919c09866a389596b215ed6f170d860583`
- Current blob: `7b1e50919c09866a389596b215ed6f170d860583`
- Source: [`native/torrent/ComicEditionIdentity.h`](../../native/torrent/ComicEditionIdentity.h)

```text
// Pure identity module for the Tankorent Comic volume-mode feature. Turns a
// GCD catalog collected edition (chId, series, title, format, ISBN, collects)
// into a canonical match target consumed by later ranking/selection code.
// No network, no Qt GUI, no file I/O.
```

<a id="file-native-torrent-comicrequestledger-cpp"></a>
## `native/torrent/ComicRequestLedger.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `156ab2411d6e18e9b8a3e6c1860a927c8e8fb917`
- Current blob: `156ab2411d6e18e9b8a3e6c1860a927c8e8fb917`
- Source: [`native/torrent/ComicRequestLedger.cpp`](../../native/torrent/ComicRequestLedger.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comicrequestledger-h"></a>
## `native/torrent/ComicRequestLedger.h`

- Status: **CURRENT**
- Accepted blob: `616f6f6cb8998406a31bc7494b82db65ac4a3161`
- Current blob: `616f6f6cb8998406a31bc7494b82db65ac4a3161`
- Source: [`native/torrent/ComicRequestLedger.h`](../../native/torrent/ComicRequestLedger.h)

```text
// Restart-safe intent ledger for Tankorent Comic collected-edition torrent
// downloads (design: docs/superpowers/specs/2026-07-15-colosseum-tankorent-
// comic-volume-mode-design.md, "Durable shared-infohash transport" ->
// ComicRequestLedger). Ports the proven MangaVolumeRequestLedger discipline
// (native/torrent/MangaVolumeRequestLedger.h) to comics: one ROW per
// requested collected edition, keyed by its stable catalog editionId (chId).
// A row records everything Task 9's shared-infohash downloader needs to
// resume after a process restart: the shared torrent infoHash + magnet, the
// canonical edition identity (series/format/ordinal/ISBN/collected issues),
// the save path, the resolved payload selection, and the current state.
//
// State machine (a row NEVER leaves the ledger; it just advances):
//   awaiting_metadata -> downloading -> assembling -> publishing -> completed
//                                          \-> failed        \-> failed
//   (any live state) -------------------------------------------> cancelled
// active() returns only the non-terminal rows (state not in {completed,
// failed, cancelled}) AND only those with a well-formed 40-hex infoHash, so a
// fresh downloader replays exactly the intents still safely resumable.
//
// Persistence is atomic via QSaveFile (write-temp-then-commit) so a crash
// mid-write can never corrupt the journal. The file is a versioned JSON
// object (schemaVersion()); a version mismatch is ignored with a diagnostic
// rather than partially applied, and a structurally broken row is quarantined
// (dropped) rather than partially loaded.
```

<a id="file-native-torrent-comictorrentdownloader-cpp"></a>
## `native/torrent/ComicTorrentDownloader.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `e9da3168d69c25a5a83ab7603adff0a5a5a4fe8e`
- Current blob: `e9da3168d69c25a5a83ab7603adff0a5a5a4fe8e`
- Source: [`native/torrent/ComicTorrentDownloader.cpp`](../../native/torrent/ComicTorrentDownloader.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comictorrentdownloader-h"></a>
## `native/torrent/ComicTorrentDownloader.h`

- Status: **CURRENT**
- Accepted blob: `40bb6dff7add2cac7bd98fa09d78b1ccc9762ca3`
- Current blob: `40bb6dff7add2cac7bd98fa09d78b1ccc9762ca3`
- Source: [`native/torrent/ComicTorrentDownloader.h`](../../native/torrent/ComicTorrentDownloader.h)

```text
// Comic torrent transport: TWO coexisting subsystems sharing one engine seam.
//
//   1. LEGACY single-archive path (download/chooseFile/cancel/statusOf/
//      activeJobs) — UNCHANGED behavior, the shipped GetComics alternate-
//      source flow. One torrent == one issueId; the whole manifest trickles
//      (addMagnet paused=false); ComicTorrentFilePicker::decide() auto-picks
//      or pauses for a manual choice; finished(issueId, archivePath) hands a
//      RAW (unextracted) archive to ComicTorrents -> ComicDownloader::
//      ingestLocalArchive, exactly as before this task.
//
//   2. NEW shared-infohash EDITION pack transport (design: docs/superpowers/
//      specs/2026-07-15-colosseum-tankorent-comic-volume-mode-design.md,
//      "Durable shared-infohash transport"). Mirrors the proven
//      MangaVolumeTorrentDownloader discipline (native/torrent/
//      MangaVolumeTorrentDownloader.h): one job per canonical infoHash with N
//      edition INTENTS. A candidate is added PAUSED; metadata is inspected
//      BEFORE any payload downloads; ComicEditionFileSelector resolves each
//      live intent against the manifest; the union of every live intent's
//      selected indices becomes the file-priority vector; a second edition on
//      the same hash JOINS the job instead of re-adding the magnet. On engine
//      completion each intent independently runs ComicEditionAssembler::
//      assemble() (synchronous) then hands the staging directory to
//      ComicDownloader::ingestAssembledEdition(). One intent's assembly
//      failure never fails its siblings. Every intent is journaled to a
//      ComicRequestLedger so a restart replays exactly the in-flight rows.
//
// Both subsystems are reached through the SAME IComicTorrentEngine seam (the
// comics mirror of MangaVolumeTorrentDownloader's IMangaTorrentEngine), so
// the whole class is testable without libtorrent — see
// tests/comic_torrent_pack_transport_harness.cpp.
```

<a id="file-native-torrent-comictorrentfilepicker-cpp"></a>
## `native/torrent/ComicTorrentFilePicker.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `c823ae89c3637824eba39897d639c8e0bca5c55a`
- Current blob: `c823ae89c3637824eba39897d639c8e0bca5c55a`
- Source: [`native/torrent/ComicTorrentFilePicker.cpp`](../../native/torrent/ComicTorrentFilePicker.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comictorrentfilepicker-h"></a>
## `native/torrent/ComicTorrentFilePicker.h`

- Status: **CURRENT**
- Accepted blob: `cd56442326fbdfc4c7f304a4d84cd25ec67fbb6e`
- Current blob: `cd56442326fbdfc4c7f304a4d84cd25ec67fbb6e`
- Source: [`native/torrent/ComicTorrentFilePicker.h`](../../native/torrent/ComicTorrentFilePicker.h)

```text
// One eligible comic archive inside a torrent manifest, with the evidence the
// archive picker shows the user (exact-title, token coverage) so a split or
// wrong-volume release can be avoided.
```

<a id="file-native-torrent-comictorrentmagnet-h"></a>
## `native/torrent/ComicTorrentMagnet.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `a97a49b7966bb851ae8044de07b1b783170ecd91`
- Current blob: `a97a49b7966bb851ae8044de07b1b783170ecd91`
- Source: [`native/torrent/ComicTorrentMagnet.h`](../../native/torrent/ComicTorrentMagnet.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comictorrentqueryplanner-cpp"></a>
## `native/torrent/ComicTorrentQueryPlanner.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `bad475dd5bb6dff0825c037afeb857d3a20aaca8`
- Current blob: `bad475dd5bb6dff0825c037afeb857d3a20aaca8`
- Source: [`native/torrent/ComicTorrentQueryPlanner.cpp`](../../native/torrent/ComicTorrentQueryPlanner.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comictorrentqueryplanner-h"></a>
## `native/torrent/ComicTorrentQueryPlanner.h`

- Status: **CURRENT**
- Accepted blob: `830bb18874e8b963db1ec04b077d7ff359a98a36`
- Current blob: `830bb18874e8b963db1ec04b077d7ff359a98a36`
- Source: [`native/torrent/ComicTorrentQueryPlanner.h`](../../native/torrent/ComicTorrentQueryPlanner.h)

```text
// Plans the identity queries fanned out through Tankorent's universal comics
// filter for a single collected edition. Pure logic, no I/O — the canonical
// edition title, ISBN, and collected-range variants, deduplicated by a
// case/punctuation/whitespace-folded key while preserving human-readable form.
```

<a id="file-native-torrent-comictorrentranker-cpp"></a>
## `native/torrent/ComicTorrentRanker.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `ac2526d66682d92c6b72f590225f42a8c0189f97`
- Current blob: `ac2526d66682d92c6b72f590225f42a8c0189f97`
- Source: [`native/torrent/ComicTorrentRanker.cpp`](../../native/torrent/ComicTorrentRanker.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comictorrentranker-h"></a>
## `native/torrent/ComicTorrentRanker.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `ece78f20b96a8f7a6b4b305995931fd24306103e`
- Current blob: `ece78f20b96a8f7a6b4b305995931fd24306103e`
- Source: [`native/torrent/ComicTorrentRanker.h`](../../native/torrent/ComicTorrentRanker.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comictorrents-cpp"></a>
## `native/torrent/ComicTorrents.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `af521cec3f8b4d112f8caf06e3e9e4cf3d7ae466`
- Current blob: `af521cec3f8b4d112f8caf06e3e9e4cf3d7ae466`
- Source: [`native/torrent/ComicTorrents.cpp`](../../native/torrent/ComicTorrents.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comictorrents-h"></a>
## `native/torrent/ComicTorrents.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `33c0aea49a8b68888e39c6bcb6a88d3b0035ecd3`
- Current blob: `33c0aea49a8b68888e39c6bcb6a88d3b0035ecd3`
- Source: [`native/torrent/ComicTorrents.h`](../../native/torrent/ComicTorrents.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comicuploadertrust-cpp"></a>
## `native/torrent/ComicUploaderTrust.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `f0d8958e38b048858ca04fd298fe11014bc45a03`
- Current blob: `f0d8958e38b048858ca04fd298fe11014bc45a03`
- Source: [`native/torrent/ComicUploaderTrust.cpp`](../../native/torrent/ComicUploaderTrust.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-comicuploadertrust-h"></a>
## `native/torrent/ComicUploaderTrust.h`

- Status: **CURRENT**
- Accepted blob: `069a5629d4db38a486b3ec19301d1823ec7c6685`
- Current blob: `069a5629d4db38a486b3ec19301d1823ec7c6685`
- Source: [`native/torrent/ComicUploaderTrust.h`](../../native/torrent/ComicUploaderTrust.h)

```text
// Uploader-trust reader for the Tankorent Comic volume-mode feature. Reads a
// bounded release tag ("[Nem]", "(Nem)", "(- Nem -)", or a trailing "- Nem")
// out of a torrent title and grades it against the bundled trust table.
// Trust influences ranking only; it never bypasses identity safety. No
// network, no Qt GUI — the trust table itself loads from the bundled Qt
// resource, not a disk path.
```

<a id="file-qml-comicarchiveboard-qml"></a>
## `qml/ComicArchiveBoard.qml`

- Status: **CURRENT**
- Accepted blob: `cced93968d92caa25a6931242953edc73531e338`
- Current blob: `cced93968d92caa25a6931242953edc73531e338`
- Source: [`qml/ComicArchiveBoard.qml`](../../qml/ComicArchiveBoard.qml)

```text
// ComicArchiveBoard — the GetComics Archives door: the live publisher/franchise
// taxonomy (ComicsApi.explore) as a full page. Each box opens ComicArchiveIndex,
// exactly as the old inline world-page mosaic did. The GetComics taxonomy lives here —
// one click off the world page's "GetComics Archives" tile — while the LOCG catalogue
// (the brain) carries the per-series browse; GetComics is the content layer.
```

<a id="file-qml-comicarchiveindex-qml"></a>
## `qml/ComicArchiveIndex.qml`

- Status: **CURRENT**
- Accepted blob: `7918328c895348734ddd87d284328541bdb6b131`
- Current blob: `7918328c895348734ddd87d284328541bdb6b131`
- Source: [`qml/ComicArchiveIndex.qml`](../../qml/ComicArchiveIndex.qml)

```text
// ComicArchiveIndex — the SERIES ARCHIVES under an explore box (Tankoban mode).
// Clicking "Marvel Comics" or "Batman" on the world's explore mosaic used to dump
// the raw release feed (Hemanth, 2026-07-04: "just individual cbr/cbz uploads, not
// archive pages"). This page is the missing middle layer: a live poster grid of the
// series archives ACTIVE under that box — aggregated from the co-tags of its newest
// 200 posts (ComicsApi.archiveIndex), each card a real /tag/ archive that opens the
// ComicSeries shelf. "All N releases ›" still reaches the raw feed, one level down.
// (This file began as the parked ComicGenrePage; repurposed when genre died on the
// board — the curated-genre concept lives only in git history / option B now.)
```

<a id="file-qml-comicdbledger-qml"></a>
## `qml/ComicDbLedger.qml`

- Status: **CURRENT**
- Accepted blob: `40f55e41eb8311e290a74d42b6562c8f140ad58a`
- Current blob: `40f55e41eb8311e290a74d42b6562c8f140ad58a`
- Source: [`qml/ComicDbLedger.qml`](../../qml/ComicDbLedger.qml)

```text
// ComicDbLedger — the DB-centered series view (step 3 of the comics-brain wiring).
//
// Renders a series straight from the weekly comics_db.json (via ComicsDb.js): a hero with
// creators + synopsis, then collected editions GROUPED BY FORMAT (Compendium / Omnibus /
// Complete Library / …), each edition a catalogue record — collected-issues, page count, year —
// with a download-state symbol (download → % → read). No live LOCG/GetComics resolution; the app
// just reads the file. Shown by ComicSeriesPage when ComicsDb has the series; otherwise the old
// live flow runs. Design ratified with Hemanth via HTML mock 2026-07-13.
//
// Download reuses the SAME engine as the live rows: the global `Comics` bridge (downloadIssue /
// statusOf / progress signals) + Resolve.failureIsTerminal. Reading is emitted up via readRequested.
```

<a id="file-qml-comicresolve-js"></a>
## `qml/ComicResolve.js`

- Status: **CURRENT**
- Accepted blob: `2041e29fc31b5af09b0ed6d9b9090617bb6c899b`
- Current blob: `2041e29fc31b5af09b0ed6d9b9090617bb6c899b`
- Source: [`qml/ComicResolve.js`](../../qml/ComicResolve.js)

```text
// >>> ATTACH MACHINE PARKED 2026-07-12 (GetComics = brain AND content; no catalogue to
// >>> attach onto). failureIsTerminal() stays LIVE — both shelf delegates consume it.
// >>> Revive resolve()/matchIssues() with LOCG. Do not delete.
// ComicResolve.js — the attach machine: pairs a LOCG catalogue series with the source slug
// that actually serves its pages. A SUCCESSFUL attach is persisted forever; a no-match is
// remembered ONLY for the session (never persisted — the source's catalog grows weekly, so a miss
// today may attach next launch). Year is a DISAMBIGUATOR, not a hard gate: source titles usually
// carry no year, so a clean title match must attach. Conservative on the wrong end — an ambiguous
// (2+ surviving) candidate is still a NO-match, a wrong comic must never open silently.
// store + searchFn are INJECTED per-source (Main.qml wires QSettings + GetComics' search fn;
// tests inject fakes) — the machine itself doesn't know which source it's pairing —
// pure/testable, the injected-clock lesson.
```

<a id="file-qml-comicseries-qml"></a>
## `qml/ComicSeries.qml`

- Status: **CURRENT**
- Accepted blob: `a32ef6524204801d5b3c474fb5ac2f775b344f38`
- Current blob: `a32ef6524204801d5b3c474fb5ac2f775b344f38`
- Source: [`qml/ComicSeries.qml`](../../qml/ComicSeries.qml)

```text
// ComicSeries — the western-comics series page (Tankoban mode). A GetComics tag IS
// the series (ratified 2026-07-04: GetComics for both catalog and download, iTunes
// posters on top, no metadata brain in v1). The shelf is the tag's release posts,
// newest-first, lightly grouped: collections (TPB/omnibus/treasury) lead, single
// issues follow. Each release = ONE volume unit (TB2-ratified: TPB is king) — a
// single archive download, ingested by `Comics` into ONE canonical CBZ the reader
// opens directly (CBZ-in-place, 2026-08-06: a readable CBZ moves in unextracted;
// anything else extracts then repacks). Legacy page dirs migrate on boot.
// Covers: each release's own og_image (exact by construction); iTunes art is the
// series-level hero only. Same glass-over-wallpaper language as MangaSeries.
```

<a id="file-qml-comicseriespage-qml"></a>
## `qml/ComicSeriesPage.qml`

- Status: **CURRENT**
- Accepted blob: `f5e4a66cd7613e30b4379440f716cc1f19aaa6e7`
- Current blob: `f5e4a66cd7613e30b4379440f716cc1f19aaa6e7`
- Source: [`qml/ComicSeriesPage.qml`](../../qml/ComicSeriesPage.qml)

```text
// >>> PARKED 2026-07-12 (Hemanth: GetComics = brain AND content). No door routes here —
// >>> the live comic page is ComicSeries.qml (the GetComics shelf). Revive with LOCG
// >>> when an RCO/Batcave-class source restores the catalogue split. Do not delete.
// ComicSeriesPage — the comic series detail page (Tankoban mode): LOCG catalogue issue rows
// with GetComics content attached. The LOCG catalogue is the brain (issue list, never
// dark); GetComics attaches downloadable archives onto those rows via ComicResolve.
// Download-fed, manga-style: one GetComics archive per issue/collection downloads through
// the Comics pipeline, then the reader reads the extracted pages offline. Manga-grade
// layout (mock-ratified 2026-07-09): hero (cover-wash backdrop, Fraunces title, parsed
// metadata + Continue) + glass issue table + a Collected-editions shelf for TPB/Omnibus
// posts. No volume shelf — comics runs are flat.
//
// ORDER: the DISPLAY table is ascending (#1 first — Hemanth's reading-order redline), but
// the READER's chapter model stays newest-first (LOCG's native date-desc), because its
// crossing advances toward index 0 (so finishing #1 goes to #2).
```

<a id="file-qml-comictorrentarchivepicker-qml"></a>
## `qml/ComicTorrentArchivePicker.qml`

- Status: **CURRENT**
- Accepted blob: `2fd88895744f6af22f46554e305a1d197b5110a6`
- Current blob: `2fd88895744f6af22f46554e305a1d197b5110a6`
- Source: [`qml/ComicTorrentArchivePicker.qml`](../../qml/ComicTorrentArchivePicker.qml)

```text
// The second-stage picker: when a chosen torrent's manifest is ambiguous (a
// multi-volume or otherwise unclear pack), the user picks exactly one eligible
// comic archive here. Only backend-validated CBR/CBZ/CB7/CBT candidates appear;
// each path + size is labelled so a split or wrong-volume release is obvious.
```

<a id="file-qml-comictorrentsourcespage-qml"></a>
## `qml/ComicTorrentSourcesPage.qml`

- Status: **CURRENT**
- Accepted blob: `9e91001bc60a0262e8ba0e1002b17730c8ae67bb`
- Current blob: `9e91001bc60a0262e8ba0e1002b17730c8ae67bb`
- Source: [`qml/ComicTorrentSourcesPage.qml`](../../qml/ComicTorrentSourcesPage.qml)

```text
// ComicTorrentSourcesPage — the full-screen "Find alternate sources" picker for a
// collected edition, in the Colosseum house language (the SourcesSheet visual
// stack: black base, key-art hero washing down, gold eyebrow + Fraunces title,
// a glass result table). Comics-specific: an edition identity rail (canonical
// title / ISBN / collected range) and per-row matched-clue evidence so the user
// sees WHY a result ranks where it does. The user always chooses the torrent;
// weak matches require an explicit confirmation; ambiguous packs open the
// second-stage archive picker. Nothing here auto-picks.
//
// Belongs to ComicSeriesPage (lazy in practice — ComicSeriesPage is lazy-loaded);
// never touches root startup. All acquisition rides the global Comics object
// under the original ledger chId — this page emits no reader signal.
```

<a id="file-qml-comicsapi-js"></a>
## `qml/ComicsApi.js`

- Status: **CURRENT**
- Accepted blob: `73c810f66ebd856a1ebab4be5c47b33e5e41ba1a`
- Current blob: `73c810f66ebd856a1ebab4be5c47b33e5e41ba1a`
- Source: [`qml/ComicsApi.js`](../../qml/ComicsApi.js)

```text
// ComicsApi.js — the western-comics catalog: GetComics IS both catalog and download
// (ratified 2026-07-04, RCO retired). All catalog reads are the WP REST API — clean
// JSON, no HTML scraping:
//   • series search  → /wp-json/wp/v2/tags?search=…      (a tag IS a series)
//   • series shelf   → /wp-json/wp/v2/posts?tags=<id>    (a release post IS the volume)
// Covers: each release's own og_image (exact match by construction). iTunes ebook
// search supplies the SERIES-level poster only — the one place fuzzy matching is
// safe, because a wrong poster can't download a wrong edition.
// The actual download happens in C++ (`Comics` / ComicDownloader): signed
// DOWNLOAD-NOW link → comicfiles.ru archive → extracted page dir.
```

<a id="file-qml-comicsdb-js"></a>
## `qml/ComicsDb.js`

- Status: **CURRENT**
- Accepted blob: `54229e6a604019030a3a94ad4eacd20eeed92e43`
- Current blob: `54229e6a604019030a3a94ad4eacd20eeed92e43`
- Source: [`qml/ComicsDb.js`](../../qml/ComicsDb.js)

```text
// ComicsDb.js — the app's comics brain, OFFLINE.
//
// Reads the weekly-built `comics_db.json` sidecar (produced by scripts/comics_brain/
// build_comics_db.py): RCO rank -> LOCG collected editions -> GetComics availability -> PRH/S&S
// enrichment. The app does NO live LOCG/GetComics resolution — it just reads this file. That
// centralizes the fragile, CF-walled, mirror-rotting scraping into one weekly job off the user's
// machine (Hemanth's call 2026-07-13); the app stays instant + offline.
//
// DEPLOYMENT SEAM: everything loads through load(source, done). `source` is a local file URL today
// (Qt.resolvedUrl("../resources/comics_db.json"), injected by Main.qml) and a hosted URL tomorrow —
// local->hosted is a one-line source swap + a cache, not a rewrite.
//
// Pure/testable: fetchFn is injected (Main.qml wires real XHR; tests inject a fake) — the
// injected-clock lesson from LocgApi.js.
```

<a id="file-qml-comicsdbloader-qml"></a>
## `qml/ComicsDbLoader.qml`

- Status: **CURRENT**
- Accepted blob: `609447c7de6709f37d43c5d5215066b86aec9282`
- Current blob: `609447c7de6709f37d43c5d5215066b86aec9282`
- Source: [`qml/ComicsDbLoader.qml`](../../qml/ComicsDbLoader.qml)

```text
// ComicsDbLoader — on-demand catalog handover for shell-side routing.
//
// DB-first series routing (Continue detail, search rows) can fire BEFORE the lazy
// Tankoban world has handed the catalog engine to ComicsDb.js. This tiny component
// carries that handover instead: Main activates it once, on the first route that
// needs the catalog. (P4 seam 2026-07-18: reads the ComicsCatalog engine/curated_*
// SQLite tables — no more multi-MB gen.js parse.)
```

<a id="file-qml-tankobancomicstab-qml"></a>
## `qml/TankobanComicsTab.qml`

- Status: **CURRENT**
- Accepted blob: `33c0d3dc4f93d16590089b9aa9a725bc25b19ced`
- Current blob: `33c0d3dc4f93d16590089b9aa9a725bc25b19ced`
- Source: [`qml/TankobanComicsTab.qml`](../../qml/TankobanComicsTab.qml)

```text
// TankobanComicsTab — the Comics half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the comics rows. Data is passed IN from TankobanWorld (which owns the
// one-time ComicsCatalog.shelf compute + GcApi.explore fetch) so switching tabs never
// re-fetches — the Loader may rebuild this view, but the data is cached upstream and bound
// in reactively. Emits the comics signals the world forwards to the host. No manga knowledge.
```

<a id="file-qml-comicreader-comicreadercommandbar-qml"></a>
## `qml/comicreader/ComicReaderCommandBar.qml`

- Status: **CURRENT**
- Accepted blob: `5ac967225d79da7eee83e23b42fb3a026fa6c97f`
- Current blob: `5ac967225d79da7eee83e23b42fb3a026fa6c97f`
- Source: [`qml/comicreader/ComicReaderCommandBar.qml`](../../qml/comicreader/ComicReaderCommandBar.qml)

```text
// ComicReaderCommandBar — the ONE flat command layer over the book (Task 5, plan 2026-07-28).
//
// Hemanth approved this shape section by section and corrected an earlier draft of it personally:
//
//   "Cover's simplicity is not 'hide everything inside a modern drawer'. It is one shallow layer:
//    large, plainly named actions across the top; one unmistakable progress bar at the bottom; no
//    pill soup, no nested control architecture."
//
// So: ONE Row of plainly named actions. No pill backgrounds, no glass boxes, no segmented chips, no
// second level. Six commands, fixed order — Bookmark, Pages, Loupe, Image, the current Layout, the
// current Order. The last two are READOUTS as well as commands: they say what the book is doing
// right now, which is why their label and glyph are derived from shell state rather than fixed.
//
// GOLD IS SPARING and structural: only the command whose temporary surface is actually open wears
// it (plus Bookmark on a bookmarked page). Order never does — it is a direct toggle with no surface,
// and gold there would claim a panel is open when none is.
//
// PRESENTATION + INTENTS ONLY. This component owns no state and touches no core: it reads the
// shell's layout/order/activeOverlay/bookmark facts through plain properties and raises
// commandTriggered(name). The HUD turns that into semantic intents; the SHELL decides what opens.
// Everything below `trigger()` is pure and callable from the offscreen harness, so the tested logic
// is the shipped logic (the ComicReaderInput house pattern).
```

<a id="file-qml-comicreader-comicreaderdoublesurface-qml"></a>
## `qml/comicreader/ComicReaderDoubleSurface.qml`

- Status: **CURRENT**
- Accepted blob: `a5a4188dfb333249f6400637551d0b59cb45905c`
- Current blob: `a5a4188dfb333249f6400637551d0b59cb45905c`
- Source: [`qml/comicreader/ComicReaderDoubleSurface.qml`](../../qml/comicreader/ComicReaderDoubleSurface.qml)

```text
// ComicReaderDoubleSurface — the direction-aware Double Page reading surface (Task 10).
//
// Renders the CANONICAL unit for the current page (from the Task-7 backend, never re-derived here):
// core.unitForPage(currentPage-1) -> {rightIndex, leftIndex(-1 absent), spread, coverAlone}.
//
//   * spread / coverAlone / single (leftIndex<0) -> ONE full-viewport-width image
//     (core.imageUrl(rightIndex)); an intact page, NEVER a fabricated crop.
//   * ONE SCALE for the whole displayed unit (both lineage readers), so a pair of unevenly trimmed
//     scans keeps its true relative size, meets FLUSH at the spine, and centres vertically as one
//     block instead of hanging off the top. Natural size comes from the BACKEND's header geometry,
//     not the Image's implicitWidth — see the unitScale block for why that distinction is the fix.
//   * a real pair -> TWO images side by side, and the PHYSICAL x-order flips with direction:
//       RTL (manga)  — the rightIndex page sits on the physical RIGHT, leftIndex on the LEFT.
//       LTR (western)— mirrored (rightIndex page on the physical LEFT).
//     (Mirrors QTGW DoublePageCanvas._draw_pair, which swaps the two images when not rtl.)
//   * GUTTER SHADOW — a soft dark vertical gradient over the spine, strength from `gutterStrength`
//     (presets 0 / 0.22 / 0.35 / 0.55). Only for a real pair, never a spread/single.
//   * ZOOM 100–260% (20% steps) + PAN. `zoomPercent` widens the spread; when zoomed, pan slides it;
//     pan clamps to the zoomed bounds; a unit change RESETS PAN ONLY — zoom SURVIVES a page
//     turn, or a magnified volume would snap back to 100% on every turn (see _onUnitShown).
//     (Matches QTGW DoublePageCanvas: set_zoom clamps 1.0–2.6 and resets pan.)
//
// THE UNIT PAINTS AS ONE THING (Task 4, overhaul plan 2026-07-28). Until now this surface bound each
// half's Image straight to the stage, so whichever half decoded first appeared and the other stayed
// black — a spread arriving as one page plus a hole. Hemanth's approved wording is the rule: "A
// paired spread appears as one complete unit. We never flash the left page first and leave the right
// half black."
//
// THE PAINT RULE IS WHAT THE SCREEN HAS, not what the backend has. A half is RESOLVED when its pixels
// are up, or when it has failed terminally and its typed placard is the honest answer for it; the
// unit paints when every half it has is resolved, and not one beat sooner. While it waits, one
// restrained placeholder per half stands exactly where that page will land.
//
// It is deliberately NOT gated on core.presentationForPage. The first version of this task was, and
// that left the defect open in two shapes — see the unitPaints block below for the measurements. The
// backend verdict is strictly EARLIER than the pixels (each half comes back through its own async
// provider round trip, and the very pageReady that flips the verdict to "ready" is what re-points the
// second half's Image at its bumped url), so it can report a unit ready in the beat the second half
// starts loading. presentationForPage stays as the backend's unit-level verdict + reason — exposed
// here as presentationState, and what Task 11's retry needs in order to say WHY a unit is not
// showing — but it decides no painting.
//
// A core WITHOUT presentationForPage (a fake, never production) therefore changes nothing about the
// paint: the gate reads its own Images either way, and such a core paints exactly when its pages are
// on screen.
//
// maxSeen PAIR-ANCHOR CONTRACT (shell Task 9, onCurrentPageChanged): in double mode `currentPage`
// is the pair ANCHOR, and a pair-terminated chapter would never reach `finished` from the anchor
// alone. So every time a unit is shown, the surface emits unitShown(highestPage) with the
// reading-HIGHEST page of the unit — max(rightIndex,leftIndex)+1 in the shell's 1-based page scale.
// The shell folds it into maxSeen, so completion advances to the unit's highest page. (This is the
// Comic Reader equivalent of MangaReader.qml bumpSeen() folding in the pair partner index.)
//
// INJECTABLE + GUARDED. `core` is injected by the shell; every `core.` use is guarded so the
// surface also survives the shell's Task-9 fake (no imageUrl). Unit computation + all side effects
// (setVisible, unitShown) are gated behind `active`, so an INVISIBLE double surface (long_strip
// mode) never touches the backend or the shell's maxSeen.
```

<a id="file-qml-comicreader-comicreaderhud-qml"></a>
## `qml/comicreader/ComicReaderHud.qml`

- Status: **CURRENT**
- Accepted blob: `b59552f441ecc5fbcb93bba7ecba809c3eac80e9`
- Current blob: `b59552f441ecc5fbcb93bba7ecba809c3eac80e9`
- Source: [`qml/comicreader/ComicReaderHud.qml`](../../qml/comicreader/ComicReaderHud.qml)

```text
// ComicReaderHud — the approved sidebar-free reading chrome (Task 5, plan 2026-07-28), replacing
// the Family Gradient pill HUD it grew out of. Hemanth approved this shape section by section; the
// decision ledger (docs/superpowers/specs/2026-07-28-comic-reader-overhaul-design.md) is verbatim:
//
//   * Thin title strip with Back and book title.
//   * One flat top command bar: Bookmark, Pages, Loupe, Image, current Layout, and current Order.
//   * No reader sidebar.
//   * No permanent settings drawer.
//   * One gold bottom rail with current position, total pages, and scrub affordance.
//   * The comic remains visible behind temporary surfaces and never shifts to make room for them.
//   * Toolbar, title toast, progress rail, and cursor sleep together after 2.5 seconds of inactivity.
//   * Any plain mouse movement restores HUD and cursor together. Escape explicitly toggles chrome
//     when no temporary surface is open.
//
// The shorthand the arc runs on: "Cover's calm, YACReader's flow, Colosseum's brain." Calm here is
// ONE shallow layer, not a drawer: no pill soup, no nested control architecture, no side panel.
//
// PRESENTATION + INTENTS ONLY. The HUD binds the shell's read-only reading state off the `reader`
// seam and EMITS semantic intents (openPages / openLoupe / openImage / openLayout / toggleOrder /
// toggleBookmark / seek / crossing / window verbs). It never writes core state: the shell's ONE
// overlay coordinator decides what actually opens (the one-temporary-surface rule).
//
// Gold is SPARING and structural: the progress rail, the scrub fill/knob, and the single active
// command. Nothing else.
//
// Every glyph is a ComicReaderIcon (white-stroke SVG, tinted) — never a Text arrow/character (the
// semantic-icon-audit law). The counter and the command names are text LABELS, not glyph chips.
```

<a id="file-qml-comicreader-comicreadericon-qml"></a>
## `qml/comicreader/ComicReaderIcon.qml`

- Status: **CURRENT**
- Accepted blob: `27202eb4b373cfba166310c928f0925b90cf4e48`
- Current blob: `27202eb4b373cfba166310c928f0925b90cf4e48`
- Source: [`qml/comicreader/ComicReaderIcon.qml`](../../qml/comicreader/ComicReaderIcon.qml)

```text
// One SVG glyph, tinted to `ink` via MultiEffect colorization — the Comic Reader's icon
// vocabulary. Two provenances, both ORIGINAL to us in the sense the design ledger requires
// (Cover is a black-box UX reference only; none of its assets, icons or brand are ever copied):
//   * vendored Lucide (ISC) strokes, carried with their @license header, and
//   * Colosseum-authored glyphs drawn on the SAME Lucide 24-grid / 2px round-stroke spec so the
//     two sit in one family (the layout + order marks have no Lucide equivalent).
// This MIRRORS qml/PlayerIcon.qml exactly:
// the SVGs (assets/icons/comicreader/, provenance in each file's header) MUST carry stroke="#ffffff",
// because MultiEffect colorization keeps the SVG's alpha coverage and replaces its colour — a
// black-stroke SVG colorizes to black = invisible (reference_multieffect_colorization_needs_white_source).
// The reader never draws a text arrow/character for a glyph: every navigational mark is one of these.
```

<a id="file-qml-comicreader-comicreaderimagepopover-qml"></a>
## `qml/comicreader/ComicReaderImagePopover.qml`

- Status: **CURRENT**
- Accepted blob: `68b15f14cc75a7b33f8833a7f09b33085584ce15`
- Current blob: `68b15f14cc75a7b33f8833a7f09b33085584ce15`
- Source: [`qml/comicreader/ComicReaderImagePopover.qml`](../../qml/comicreader/ComicReaderImagePopover.qml)

```text
// ComicReaderImagePopover — the compact Image panel (Task 7, plan 2026-07-28).
//
// Hemanth took Cover's real reader for a ride while this was being designed. Its
// "Image settings" opened a small drop directly under the command's own label —
// not a side drawer, not a sheet — holding three things and nothing else. His
// ruling: Cover is "aggressively selective; it does not expose every technically
// possible adjustment", and he wanted YACReader's DEPTH without losing Cover's
// calm. The approved shape, in his words:
//
//   "Image opens a compact anchored panel and does not move the comic."
//   "Contrast, gamma, rotation, and auto-crop behind one Advanced image tools
//    row. Panel floats over the comic without shifting it."
//
// So: THREE controls you see instantly — Quality, Brightness, Night filter — and
// everything else one disclosure deeper, in the SAME anchored panel. Not a second
// surface, not a sheet, and never a sidebar (standing law: no reader sidebar).
//
// PRESENTATION + INTENTS ONLY, like the rest of the reader chrome. This component
// owns no reading state and never touches the backend: the live profile is pushed
// in as `profile` and every control raises ONE signal,
// profileChangeRequested(map), carrying a COMPLETE profile map. That completeness
// is deliberate — ComicReaderCore::setRenderProfile REPLACES rather than merges,
// so a partial map would silently reset the fields it omitted. Building the map
// here, from the live one, in a single function, is what makes that impossible to
// get wrong at a call site.
//
// `open` is a RULE-level property, deliberately not `visible`: QQuickItem.visible
// is EFFECTIVE visibility, so a test asserting on a child's `visible` reads its
// ancestors' state too. `open` and `advancedOpen` say what this surface believes,
// whatever its parents are doing.
//
// EVERY control is harness-callable as a plain function (the ComicReaderInput
// house pattern), and the pointer paths call those same functions — so the tested
// logic is the shipped logic rather than a parallel description of it.
```

<a id="file-qml-comicreader-comicreaderinput-qml"></a>
## `qml/comicreader/ComicReaderInput.qml`

- Status: **CURRENT**
- Accepted blob: `b8f35929ecdbafa68bfb5788c6c299fd0794efa1`
- Current blob: `b8f35929ecdbafa68bfb5788c6c299fd0794efa1`
- Source: [`qml/comicreader/ComicReaderInput.qml`](../../qml/comicreader/ComicReaderInput.qml)

```text
// ComicReaderInput — the reader's semantic input map (Task 11). It fills the reading area BELOW the
// HUD and turns raw pointer/keyboard events into SEMANTIC actions only (never raw scroll deltas or
// page indices leaking out). The shell wires these actions to its navigation + the surfaces; the
// HUD sits above and consumes pill clicks before they reach here.
//
// The decision logic is pure functions (keyAction / zoneForX / navByZone / releaseAt / wheelAction)
// that BOTH the live handlers AND the offscreen harness call — so the tested logic IS the shipped
// logic. Ground-truthed against the lineage TankobanQTGroundWork comic_reader.py keyPressEvent +
// eventFilter (~2377-2600): click zones turn pages BY DIRECTION, center toggles chrome / (double
// click) fullscreen with the 220ms single-vs-double disambiguation, and a >4px press-drag while
// magnified pans and cancels the click so panning never turns a page.
```

<a id="file-qml-comicreader-comicreaderlayoutpopover-qml"></a>
## `qml/comicreader/ComicReaderLayoutPopover.qml`

- Status: **CURRENT**
- Accepted blob: `1c258452e41e9577882113ecdcc5db116a394510`
- Current blob: `1c258452e41e9577882113ecdcc5db116a394510`
- Source: [`qml/comicreader/ComicReaderLayoutPopover.qml`](../../qml/comicreader/ComicReaderLayoutPopover.qml)

```text
// ComicReaderLayoutPopover — the compact Layout menu (Task 8, plan 2026-07-28).
//
// The approved shape, verbatim from the design ledger:
//
//   "Long Strip owns its contextual controls in the active Layout menu: portrait width, page
//    spacing, Auto-scroll start/pause and speed."
//   "Layout and motion remain separate. Long Strip creates the vertical page flow; Auto-scroll only
//    supplies motion at the already chosen width. Starting or resuming Auto-scroll must never
//    resize the page."
//   "range: 40-100% of viewport width, default 78%, persisted per series, landscape spreads remain
//    100% width, width changes reflow in place while preserving the visible page/fraction."
//   "Page spacing offers at least Seamless and Breathing Room. Seamless remains the default."
//
// Hemanth called the portrait width out by name while this was being designed — "one of the most
// important features is the potrait width in autoscroll. I hope you're not forgetting about that"
// — and then confirmed 78% as the default. So this panel treats 78 and never-resize as the two
// things it exists to get right, and the ONE law it enforces structurally is that no Auto-scroll
// control has any path to a width change: they raise different signals, and the width verbs are
// the only ones that carry a width at all.
//
// CONTEXTUAL, not conditional-looking. The three layouts are always here — Layout is the command,
// and picking one is what it is for. The Long Strip block appears only when Long Strip is the live
// layout, because portrait width and page spacing mean nothing to a paged surface and Auto-scroll
// has nothing to move.
//
// PRESENTATION + INTENTS ONLY, like the rest of the reader chrome (ComicReaderImagePopover is this
// panel's sibling and they are deliberately one family — same glass, same chip vocabulary, same
// anchored drop). It owns no reading state and never touches the backend. Every control raises one
// signal and the shell decides.
//
// `open` and `longStripControlsVisible` are RULE-level properties, deliberately not `visible`:
// QQuickItem.visible is EFFECTIVE visibility, so a test asserting on a child's `visible` reads its
// ancestors' state too. These say what this surface BELIEVES, whatever its parents are doing.
//
// EVERY control is harness-callable as a plain function (the ComicReaderInput house pattern), and
// the pointer paths call those same functions — so the tested logic is the shipped logic.
```

<a id="file-qml-comicreader-comicreaderloupe-qml"></a>
## `qml/comicreader/ComicReaderLoupe.qml`

- Status: **CURRENT**
- Accepted blob: `701e2f85ca74b25682d3324a160f25986756c081`
- Current blob: `701e2f85ca74b25682d3324a160f25986756c081`
- Source: [`qml/comicreader/ComicReaderLoupe.qml`](../../qml/comicreader/ComicReaderLoupe.qml)

```text
// ComicReaderLoupe — the temporary full-resolution magnifier (Task 9, plan 2026-07-28).
//
// This COMPLETES a scaffold rather than adding a feature. The reader already had the Loupe command,
// the `L` shortcut, the `search` glyph, `loupeRequested()` and tests proving the request fires —
// and nothing at all consumed it. Hemanth's ruling when he noticed: "So we'll complete the existing
// Loupe using YACReader's useful behavior, not add a duplicate feature." (YACReader is a strict
// BEHAVIOURAL reference — GPL-3.0 — never a source of code.)
//
// The approved shape, verbatim from the design ledger:
//
//   "circular lens following the pointer, 2.0x default magnification, adjustable 1.5x-4.0x by wheel
//    or +/-, full-resolution cached page sampling, click to pin; click again to resume following,
//    flips inward near viewport edges, works in Single, Pair, and Long Strip, pauses Auto-scroll
//    while active, never changes page zoom, pan, layout, or reading position, closes through Loupe,
//    L, Escape, or its close action."
//
// THE LINE THAT GOVERNS THIS FILE is the second to last: *never changes page zoom, pan, layout, or
// reading position*. This is a temporary INSPECTION tool, not a fourth way to zoom. If using it
// moves the book, it is wrong. That rule is kept STRUCTURALLY rather than by guard:
//
//   * this component owns no reading state — the pages it draws are pushed in as plain boxes;
//   * it raises exactly ONE intent, dismissRequested(), which carries nothing;
//   * it never touches `core`, a surface, a page number, a zoom, a scroll position or a record —
//     it has no reference to any of them, so there is no code path to audit.
//
// HOW IT SAMPLES (the part that is easy to get wrong, and the reason the design says "full-
// resolution cached page sampling" rather than just "magnifier"). The cheap route is a
// ShaderEffectSource over the live page item — but that samples the already-downscaled ON-SCREEN
// texture, so magnifying it shows you bigger blurry pixels, which defeats the entire point of a
// loupe on a 2400px scan. So the lens re-requests the page through the SAME provider the reading
// surface uses, at its OWN request size:
//
//     sourceSize.width = drawnWidth x magnificationMax          (the TOP magnification, 4.0)
//
// Task 2's provider treats the requested width as a CEILING it may downscale TO, never a size it
// upscales to (ComicReaderImageResponse: `scaling = targetWidth > 0 && targetWidth < source.width`).
// So the lens gets, from the decoded SOURCE page:
//   * the untouched full-resolution page whenever 4x the drawn size is at or beyond the scan's own
//     resolution — the normal case for a real comic scan at any sane window size; or
//   * an exact SmoothTransformation scale of that full-resolution page down to 4x the drawn size,
//     which is still at or above every pixel the lens ever displays.
// Either way the lens is looking at the decoded page, never at the screen. It is pinned at the TOP
// magnification rather than the live one on purpose: a request width that moved with the wheel
// would re-scale the page on every notch, and seat a new scaled-cache entry per notch.
//
// It costs the reader nothing it was not already paying: the page under the lens is on screen, so
// ComicReaderCore::setVisible has already PINNED it in the source page cache (visible pages plus
// their neighbours never evict). The lens asks for a page that is already resident, and it never
// calls setVisible itself — the page behind the lens is never blanked to feed the lens.
//
// WHAT IT INHERITS. The url is the reader's own imageUrl(), so the sample rides the whole render
// path: the Image panel's brightness / contrast / gamma / sharpen / rotation / auto-crop are all
// applied by the provider before the pixels come back, and the ?rev= in the url self-busts when
// they change. The one thing the provider does NOT own is the night veil — that is a composited
// black Rectangle the shell paints over the surfaces, so the lens paints its own at the same
// opacity (`veilOpacity`). Without it the lens would glare bright white out of a dimmed page.
//
// PRESENTATION + INTENTS ONLY, like the rest of the reader chrome — ComicReaderPagesOverlay,
// ComicReaderImagePopover and ComicReaderLayoutPopover are this component's siblings and the four
// are deliberately one family: `open` is a RULE-level property (QQuickItem.visible is EFFECTIVE
// visibility, so a test asserting on a child's `visible` reads its ancestors' state too), every
// control is a plain harness-callable function that the pointer paths then call, and dismissal is
// one signal the shell acts on.
```

<a id="file-qml-comicreader-comicreaderpagesoverlay-qml"></a>
## `qml/comicreader/ComicReaderPagesOverlay.qml`

- Status: **CURRENT**
- Accepted blob: `cdfde8bb81a5ac0cd9e62a0785c41564b207a6e8`
- Current blob: `cdfde8bb81a5ac0cd9e62a0785c41564b207a6e8`
- Source: [`qml/comicreader/ComicReaderPagesOverlay.qml`](../../qml/comicreader/ComicReaderPagesOverlay.qml)

```text
// ComicReaderPagesOverlay — the temporary Pages filmstrip (Task 6, plan 2026-07-28).
//
// The permanent gold rail answers "where am I?". The Pages command answers "what is around me?" —
// and Hemanth picked its shape twice, in his own words:
//
//   "temporary overlay, obviously"   (a docked shelf and a navigator takeover were both rejected)
//   "a clean thumbnail film strip like YacR's appletunes looking strip would be great"
//
// So: an iTunes/Cover-Flow-shaped strip with a DOMINANT CENTRE, raised directly above the gold rail,
// drawn OVER the comic — which never shifts to make room for it. Selecting a thumbnail jumps and
// dismisses; Escape or a click on the comic dismisses without moving a single page.
//
// FOUR RULES, and each one is a real defect this component is built to avoid:
//
//   1. VIRTUALIZED. A 1,452-page volume must instantiate a handful of thumbnails, not 1,452 — and a
//      CLOSED filmstrip must instantiate none at all (the model is gated on `open`, so a shut
//      surface holds nothing). This reader exists because of a stutter; a filmstrip that hitches on
//      open would be a self-inflicted one.
//   2. THE THUMBNAIL TIER, ALWAYS. Every request goes through core.imageUrl(page, "thumbnail"),
//      which Task 2 caps at kThumbnailMaxWidth (240px). Asking for "hq" here would pull a
//      full-resolution scan per visible thumbnail and blow the scaled cache the reading surface
//      depends on. The gate asserts the tier both behaviourally and at the source level.
//   3. RTL MIRRORS THE VISUALS ONLY. The strip's sequence reverses for Manga order; the printed page
//      numbers never do. Page 16 is labelled 16 in both directions — that is locked design.
//   4. DISMISS NEVER MOVES. The canvas catcher and dismiss() emit dismissRequested() and nothing
//      else. There is exactly ONE door that can navigate (activateIndex), and it emits exactly one
//      jumpRequested and one dismissRequested per call, so the classic double-fire has nowhere to
//      come from.
//
// PRESENTATION + INTENTS ONLY, like the rest of the reader chrome. This component owns no reading
// state: pageCount / currentPage / order / bookmarks are pushed in, and it raises jumpRequested /
// dismissRequested for the shell's ONE overlay coordinator to act on. It never writes currentPage
// and never calls a navigation function — which is what makes "dismiss without moving" structural
// rather than incidental.
//
// `open` is a RULE-level property, deliberately not `visible`: QQuickItem.visible is EFFECTIVE
// visibility, so a test asserting on a child's `visible` reads the parent's state too. `open` says
// what this surface believes, whatever its ancestors are doing.
//
// AUTO-HIDE: nothing here touches the 2500ms chrome/cursor timer, and nothing needs to. Task 5's
// HUD already holds the chrome while `activeOverlay` is non-empty (ComicReaderHud._holdChrome), the
// cursor follows chromeVisible, and the shell now folds this surface into `modalOpen`. An overlay
// that vanished from under the reader's hand would be a bug; the machinery that prevents it already
// existed, so adding a second timer here would be a second thing to keep in step.
```

<a id="file-qml-comicreader-comicreaderpreview-qml"></a>
## `qml/comicreader/ComicReaderPreview.qml`

- Status: **CURRENT**
- Accepted blob: `32c81d0f254539236daf49c293dc7e36ccea2c80`
- Current blob: `32c81d0f254539236daf49c293dc7e36ccea2c80`
- Source: [`qml/comicreader/ComicReaderPreview.qml`](../../qml/comicreader/ComicReaderPreview.qml)

```text
// ComicReaderPreview — a standalone first-render harness for the from-scratch Comic Reader.
//
// PURPOSE: let a brother LAUNCH the new Comic Reader and SEE it rendering pages with its HUD,
// WITHOUT cutting over production. This is a preview, not a product surface — it is never mounted
// by any caller; it is only ever loaded as the QML-path argument of the real app:
//
//     colosseum.exe qml/comicreader/ComicReaderPreview.qml [pagesFolder]
//
// The real app (native/main.cpp) registers `ComicReaderCore` + the `image://comicreader/` provider
// UNCONDITIONALLY (they don't depend on the argless user-lane), so loading THIS file as argv[1]
// still gets the full native backend. The shell's `core` seam resolves to the real ComicReaderCore;
// openEntry() decodes our demo files and the provider serves them — a genuine first render.
//
// It mounts ComicReaderShell over a tiny in-QML page store that hands back a generated demo chapter
// (tools/comicreader-preview/pages/, 18 pages incl. two landscape spreads so double-page pairing,
// the spread, and the gutter are all visible). Optional real-folder override: pass a folder as
// argv[2] and it lists that folder's images instead (so it can be pointed at real downloaded manga).
```

<a id="file-qml-comicreader-comicreadersettingssheet-qml"></a>
## `qml/comicreader/ComicReaderSettingsSheet.qml`

- Status: **CURRENT**
- Accepted blob: `d5cdfcf4d3b5cba1fb81a2dbd3c2b802c672e72e`
- Current blob: `d5cdfcf4d3b5cba1fb81a2dbd3c2b802c672e72e`
- Source: [`qml/comicreader/ComicReaderSettingsSheet.qml`](../../qml/comicreader/ComicReaderSettingsSheet.qml)

```text
// ComicReaderSettingsSheet — the reader's glass side sheet (Task 12, mockup surface 02). Slides from
// the right over a dimmed page; sectioned in the player's letter-spaced label voice, gold marking
// ONLY the active choice. "Lineage layout, player soul": glass-deep panel, Segoe UI chrome, sparing
// gold. Opens from the HUD settings pill or right-click (shell wires settingsRequested -> open()).
//
// Mode writes only reader.persistedMode / reader.persistedDirection (never reader.mode / reader.rtl),
// like the HUD, so a crossing's load() still owns the actual toggle.
//
// Sections: DISPLAY (Mode, Night veil) always · DOUBLE PAGE (Coupling, Gutter shadow, Zoom readout)
// in Manga/Comic · LONG STRIP (Page width, Gap) in Strip — those two are mirrors, exactly one is up
// at a time · TOOLS (2x2 launcher grid, Memory saver, danger row) in every mode, because a tool is
// not a display choice.
//
// The tool tiles ask the SHELL to raise each overlay; those overlays land in the following slices,
// exactly as the HUD's own pills already work. The danger actions ARM on the first tap and fire on
// the second — nothing here destroys state on a single touch.
//
// NOT persisted across launches yet: every setting here is live-for-the-session, same as it was
// before this sheet existed. One pass wires the whole sheet to a Settings store; doing it per-row
// would leave the sheet half-remembering, which is worse than not remembering at all.
//
// PRESENTATION + INTENTS ONLY. Reads reading state off the injected `reader` seam; every `reader.`
// use is guarded so a null/partial seam never errors. Dismiss (X / scrim / close()) emits dismissed()
// and clears `opened`; a tap on the sheet BODY is swallowed (floating-panel/click-swallower law).
```

<a id="file-qml-comicreader-comicreadershell-qml"></a>
## `qml/comicreader/ComicReaderShell.qml`

- Status: **CURRENT**
- Accepted blob: `3ee8fdec38da9b9a3181bd1fd046f0b13064cd02`
- Current blob: `3ee8fdec38da9b9a3181bd1fd046f0b13064cd02`
- Source: [`qml/comicreader/ComicReaderShell.qml`](../../qml/comicreader/ComicReaderShell.qml)

```text
// ComicReaderShell — the Comic Reader's orchestration spine (Task 9).
//
// This is the reader's ROOT: it exposes the exact public caller contract that qml/MangaReader.qml
// exposes today (Task 1 handoff, docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md),
// so at the Task 13 cutover MangaReader.qml collapses to `ComicReaderShell {}` and every caller —
// MangaSeries / ComicSeries / ComicSeriesPage / Main — keeps working byte-for-byte.
//
// Orchestration ONLY. It owns the entry lifecycle (load -> open, resume, crossing, close), drives
// the C++ backend + the Progress sink through injectable seams, and mounts the two reading surfaces
// (Task 10: ComicReaderStripSurface / ComicReaderDoubleSurface), toggled by `mode`. The surfaces
// PAINT; the shell still owns every DECISION. The Family Gradient HUD (Task 11) and the overlays
// (Task 12) mount INSIDE this shell later. No chrome, no overlays here yet.
//
// Every pairing/crossing/completion/progress/direction/acquisition DECISION is delegated to the
// pure library ComicReaderState.js (Task 8) — the shell never re-derives that logic inline.
//
// LIFECYCLE PARITY (ground-truthed against how the three callers mount the reader: they toggle
// `visible: page.openChapterId.length > 0` — they HIDE the reader on back and SHOW it again to
// reopen; they do NOT destroy it). So: HIDE flushes progress but keeps the backend entry OPEN
// (tearing it down would blank the reader on reopen-same-entry — chapterId "" -> "ch5" while
// curChapterId is still "ch5" skips the openEntryById guard, so load() never re-runs); the backend
// entry is closed ONLY on destruction. Mirrors MangaReader.qml onVisibleChanged (flush, no
// teardown) + Component.onDestruction.
//
// INJECTABLE SEAMS (the reason this is testable offscreen AND degrades gracefully):
//   * core     — the C++ ComicReaderCore backend (a context property in the real app). The
//                harness injects a mock with the same API. Every use is guarded `if (core) ...`.
//   * progress — the Progress sink (a C++ context property in the real app; simply undefined
//                under the offscreen qml.exe runner). Every use is guarded `if (progress) ...`,
//                mirroring the old reader's `typeof Progress === "undefined"` guards.
//   * store    — resolved from the injected `pageStore` (Tankoban volumes / any lane) else the
//                app's western `Comics` / manga `Downloads` context property, exactly as the old
//                reader (contract §3). Callers of western comics pass NO pageStore and rely on this.
```

<a id="file-qml-comicreader-comicreadersinglesurface-qml"></a>
## `qml/comicreader/ComicReaderSingleSurface.qml`

- Status: **CURRENT**
- Accepted blob: `c9b1ba477e9f1b72fd4bb07fd6d19504d2d19f7c`
- Current blob: `c9b1ba477e9f1b72fd4bb07fd6d19504d2d19f7c`
- Source: [`qml/comicreader/ComicReaderSingleSurface.qml`](../../qml/comicreader/ComicReaderSingleSurface.qml)

```text
// ComicReaderSingleSurface — the Single Page reading surface (Task 4, overhaul plan 2026-07-28).
//
// The third layout beside Long Strip and Pair, approved by Hemanth during design ("Yes, we can add a
// single page mode"). It is a LAYOUT, orthogonal to order: a manga read in Single Page is still
// right-to-left. Nothing in this file knows or cares about direction — which page comes next is the
// shell's question, and the shell already answers it from `order`.
//
// WHAT IT DRAWS: one page, alone, on the black stage.
//
//   * FIT IS CONTAIN, not fit-width. This is the one place Single deliberately parts company with
//     the Pair surface. A pair is two pages wide, so fitting it to the viewport WIDTH fills the
//     frame; doing the same to a single portrait page would draw it far taller than the window and
//     force you to pan down every page at 100% zoom, which is a strip pretending to be a page
//     reader. Contain (min of the width fit and the height fit) shows the WHOLE page, centred, and
//     leaves pan for when you have actually zoomed in.
//   * TWO TIERS, stacked (Task 2's preview/hq split, first consumer). `preview` is a FAST transform
//     at a smaller width and lands first; `hq` is the reader's real page and fades over it in 90ms
//     once it completes. The eye reads that as the page sharpening, not as two loads.
//   * ZOOM 100–260% PRESERVES THE CENTROID. Whatever sat under the middle of the window before the
//     zoom step is still there after it. The Pair surface only clamps its existing pan (already an
//     improvement on the reader it replaced, which zeroed it and teleported you to a corner); a
//     single page is small enough on screen that plain clamping still visibly slides the art out
//     from under you, so this one does the arithmetic properly.
//   * PAN CLAMPS to the zoomed page's own bounds, so you can never drag past the paper into black.
//   * FAILURE is the typed placard, and WAITING is the restrained placeholder — the same two
//     components the Pair surface uses.
//
//     They do NOT behave identically on a page turn, and the difference is worth knowing: this
//     surface keeps the OUTGOING page on screen while the next one decodes (retainWhileLoading holds
//     the old pixmaps, and the placeholder is declared first so it sits behind them), whereas the
//     Pair surface shows the placeholder, because its gate hides the images outright and there is
//     nothing left to retain. So Single's placeholder is in practice only reachable on a FIRST open,
//     or on a page whose geometry is known before any pixels exist. Which of the two feels better on
//     a turn is Hemanth's call, not something this file should quietly decide.
//
// PRESENTED (the Task 4 seam, consumed by Task 11): `presented(anchorPage, withinPageFraction)`
// fires when this surface has actually put the page's pixels on screen — not when it was asked to.
// It is emitted ONCE per page (whichever tier lands first wins), so Task 11 can gate progress-saving
// on a page the reader genuinely saw. Nothing consumes it yet; that is Task 11's job, and the signal
// being unused-but-correct until then is expected.
//
// INJECTABLE + GUARDED, exactly like the other two surfaces: `core` is injected by the shell and
// every `core.` use is guarded, so a partial fake (the shell harness's Task-9 stub has no imageUrl)
// degrades to drawing nothing rather than erroring.
//
// EVERY reach for the backend is gated on `active` — the setVisible pin AND both image `source`
// bindings. The sources matter as much as the pin: `currentPage` is bound to the shell's page
// unconditionally, so while Long Strip is the mounted layout this surface's page number follows the
// column as it scrolls. Ungated, that issued two provider requests per page scrolled past, at request
// sizes the strip does not share and therefore as separate pixmap-cache entries competing with the
// strip's own. (Measured: with active:false the urls were live and tracked currentPage. The Pair
// surface never had this, because its sources flow through `unit`, which is already active-gated.)
```

<a id="file-qml-comicreader-comicreaderstate-js"></a>
## `qml/comicreader/ComicReaderState.js`

- Status: **CURRENT**
- Accepted blob: `009aab4e16393f8bd62deb23f691ee0d80d9ec98`
- Current blob: `009aab4e16393f8bd62deb23f691ee0d80d9ec98`
- Source: [`qml/comicreader/ComicReaderState.js`](../../qml/comicreader/ComicReaderState.js)

```text
// Comic Reader — pure decision logic (Task 8).
//
// Every function here is a pure function of its arguments only: NO reference to any QML
// context property (`Progress`, `Downloads`, `Comics`, `TankobanVolumes`, ...) or component
// object (`ComicReaderCore`, any `id`) by name — a `.pragma library` script cannot see those
// (house law: reference_pragma_library_cant_see_context_properties). The Comic Reader shell
// (Task 9) calls these for every crossing/completion/progress/acquisition decision and supplies
// whatever state each function needs as plain arguments.
//
// Ground truth: qml/MangaReader.qml (the current reader being rebuilt) and the Task 1 contract,
// docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md. Where this file
// intentionally departs from that reader's TODAY behavior (a forward-looking "smart default"
// rather than a byte-for-byte port), the function comment says so explicitly.

// --- progress namespace -----------------------------------------------------------------
// Mirrors qml/MangaReader.qml's `progressKind` (~line 64): western never sets entryKind but
// keeps its "comic" namespace; every other caller's entryKind IS the namespace ("manga"
// chapters, "tankoban" volumes). This guarantees a volume record and a chapter record for the
// same series can never overwrite each other (contract §5).
```

<a id="file-qml-comicreader-comicreaderstripsurface-qml"></a>
## `qml/comicreader/ComicReaderStripSurface.qml`

- Status: **CURRENT**
- Accepted blob: `552441423d4fb21321587ad1302b21329d3a57c3`
- Current blob: `552441423d4fb21321587ad1302b21329d3a57c3`
- Source: [`qml/comicreader/ComicReaderStripSurface.qml`](../../qml/comicreader/ComicReaderStripSurface.qml)

```text
// ComicReaderStripSurface — the Long Strip reading surface (Task 10).
//
// The manga-default vertical reader: one continuously-scrolling column of full-bleed pages on
// black. It is a THIN painter over the Task-7 backend — "QML paints, C++ decides":
//
//   * GEOMETRY IS AUTHORITATIVE. A ListView is bound to `core.stripModel` (the Task-6
//     ComicReaderStripModel, roles pageIndex/top/displayWidth/displayHeight/ready/errorCode). Each
//     delegate takes its height/width from the MODEL roles, never from the loaded Image's implicit
//     size — so a page decoding to a taller-than-estimated size never reflows/jerks the column.
//   * VIRTUALIZED. ListView keeps only near-viewport delegates (modest cacheBuffer); on scroll the
//     surface reports the viewport to core.setStripViewport(top,height) + setStripViewportWidth(w)
//     THROTTLED to at most once per frame (a 16ms coalescing timer, not one call per contentY tick).
//   * ANTI-JUMP. core.stripCompensation(delta) — emitted when a page above the fold decodes to a new
//     height — is added straight to contentY so the read position doesn't shift under the reader.
//   * SMOOTH WHEEL. The family's float accumulator — ported from the reader this one replaced
//     (MangaReader.qml's FrameAnimation drain, itself TB2's), NOT re-derived. ~168px/notch intake
//     into a bounded backlog, 38% of that backlog drained per frame, sub-pixel float contentY.
//     THIS is the reading feel, and every clause of it is load-bearing — see the drain below.
//   * PER-PAGE FAILURE. core.pageFailed(page,code) shows a typed placard on THAT page's delegate
//     only (missing / decode / unsupported); the rest of the column keeps reading.
//
// INJECTABLE + GUARDED. `core` is injected by the shell (its ComicReaderCore seam). Every `core.`
// use is guarded, so the surface also survives the shell's Task-9 fake (which has no imageUrl /
// stripModel / setStripViewport) — it simply renders nothing until a real core is present.
//
// STATE-MUTATING SIGNALS ARE PROVENANCE-BLIND, GATED ONLY ON _programmatic. pageInView/scrolled fire
// on ANY real move of the column — wheel, keyboard (Space/PageUp/PageDown), a scrub-bar drag, Home/
// End — throttled to at most once per ~80ms window (Reader 1's pageTrack), never once per contentY
// tick. They fire NEVER on construction, resume, or compensation (those write contentY with
// _programmatic held true) — so mounting this surface never clobbers the shell's resumed page/
// fraction. The shell consumes pageInView/scrolled out; it puts the column somewhere by CALLING
// seekToPage()/haltScrollAt(), never by binding a fraction in (that would be a scroll -> fraction ->
// apply -> scroll loop).
```

<a id="file-qml-comicreader-comicreaderuniterror-qml"></a>
## `qml/comicreader/ComicReaderUnitError.qml`

- Status: **CURRENT**
- Accepted blob: `e26b99340d495b9dae95d32ab8d0528faae60cf1`
- Current blob: `e26b99340d495b9dae95d32ab8d0528faae60cf1`
- Source: [`qml/comicreader/ComicReaderUnitError.qml`](../../qml/comicreader/ComicReaderUnitError.qml)

```text
// ComicReaderUnitError — the typed placard a paged surface shows for a page that cannot arrive.
//
// WHY A FILE (justified up from zero, Task 4 / overhaul plan 2026-07-28): the Long Strip surface
// already draws this exact placard per delegate ("typed error placard — this page only",
// ComicReaderStripSurface.qml). Task 4 adds two more places that need it — the Single surface and
// each half of a Pair — and inlining it there would have made three hand-copied versions of one
// visual, which is how a second error language gets invented by accident. So: ONE leaf visual, the
// strip's own colours and wording carried over verbatim, two consumers today.
//
// (The strip's copy is deliberately left alone this task — it lives inside a ListView delegate and
// re-plumbing it is a change to a surface Task 4 has no other reason to touch. Its colours, sizes and
// wording are the ones reproduced here, so for the three real codes the two are identical. They DO
// differ on an unrecognised code, by design: the strip draws no card at all, this draws an honest
// fallback — see the note below. Folding the strip's copy in is a later tidy.)
//
// TYPED, not generic: the codes are the backend's snake_case PageError wire codes
// (ComicReaderTypes.h — none / missing_file / decode_failed / unsupported_image), so the reader is
// told which of the three actually happened. An unrecognised code still says something honest
// rather than rendering an empty card.
//
// TWO WAYS OUT (Task 11, overhaul plan 2026-07-28). Until now this card was a dead end: it named
// the problem and offered nothing. The approved design is "the reader shows a restrained error card
// in that page's place, offers Retry and Skip, and keeps surrounding pages usable", so the card now
// carries exactly those two actions and nothing else.
//
// It RAISES them, it never performs them. Retry is a backend re-read (ComicReaderCore::retryPage)
// and Skip is a navigation, and neither is a placard's business — routing them through the surface
// to the shell is what keeps "Retry never mutates the archive" a property of one tested function
// instead of a promise repeated in three visuals. Both signals carry the page, because in Pair mode
// two of these cards can be on screen at once and the good half must not be the one retried.
//
// THE STRIP'S HAND-COPY IS GONE (Task 11). Task 4 left the Long Strip surface drawing its own inline
// version of this card and called folding it in "a later tidy"; adding actions made it load-bearing
// rather than tidiness, because a Long Strip reader would otherwise have been left with the exact
// dead end this task exists to close. One card, one error language, three mounts.
```

<a id="file-qml-comicreader-comicreaderunitplaceholder-qml"></a>
## `qml/comicreader/ComicReaderUnitPlaceholder.qml`

- Status: **CURRENT**
- Accepted blob: `8bc95a3837426d1fdc706de98fc98d4e270a7f18`
- Current blob: `8bc95a3837426d1fdc706de98fc98d4e270a7f18`
- Source: [`qml/comicreader/ComicReaderUnitPlaceholder.qml`](../../qml/comicreader/ComicReaderUnitPlaceholder.qml)

```text
// ComicReaderUnitPlaceholder — the quiet stand-in a paged unit shows before it has pixels.
//
// WHY THIS EXISTS AT ALL (justified up from zero, Task 4 / overhaul plan 2026-07-28):
// A paged surface that will not paint a half-decoded unit has to draw SOMETHING in the meantime, and
// the alternative to this file is inlining the same eight lines into both paged surfaces — where the
// two copies then drift, which is exactly how a reader ends up with two different "loading" looks.
// One leaf visual with two consumers (Single and Pair), no behaviour, no state machine.
//
// RESTRAINED is the requirement, in Hemanth's word: a page-shaped panel a shade above the black
// stage, and nothing else. No spinner, no percentage, no pulsing — a reader waiting a beat for a
// page should feel like paper that has not turned yet, not like software thinking.
//
// The FADE is what keeps it from being a flicker, and the EASING is the load-bearing half of that.
// It has to be ease-IN (slow at the start): a page that decodes in 40ms then only reaches t^2 = 0.08
// of full opacity before it is told to go away again, which is imperceptible. An earlier draft used
// OutQuad, which is fastest at the start — exactly backwards for suppressing a flash: at the same
// 40ms it reached t(2-t) = 0.49, so a fast turn showed a ~50% grey panel and faded it back out. (The
// comment there also claimed "about a third", which was wrong about its own curve.) That is the whole
// reason `shown` is a property and `visible` is derived from the animated opacity — the caller states
// intent, the component decides when it is actually worth drawing.
//
// GEOMETRY IS THE CALLER'S: this fills whatever box it is given. The paged surfaces put it exactly
// where the page (or each half of the pair) will land, so the unit does not jump when it arrives.
```
