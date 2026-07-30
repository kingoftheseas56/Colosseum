# Spec — the remaining 12% of late video frames (GUI-thread stall residue)

**Commissioned by:** Agent 4 (Claude), player lane
**Date:** 2026-07-29
**Branch/base:** `master` @ `ea8079f` (all four fixes below are already ON master and pushed)
**Nature:** diagnostic-then-fix. The cause CLASS is proven; the remaining instances are not yet named.

## Strategic intent

Hemanth's video stutter is half-fixed and the other half is the same defect in a different phase.
Colosseum's video engine, the hardware, the decode path and the graphics API have all been
measured and exonerated. Frames are ready on time; **Qt Quick cannot present them because the GUI
thread is busy**, and every stall is a visible hitch. One cause was found and fixed (the home-screen
world "warmer" kept building pages behind the film). After that fix 12.3% of frames still arrive
late. Find what else blocks the GUI thread during playback, fix it the same way, and prove it with
the instruments already in the tree — not with a single run.

## What is already ESTABLISHED (measured, do not re-litigate)

Same 60s of the same file (`Tenet`, from 00:06:00), same instrument, before -> after the warmer fix:

| metric | before | after |
|---|---|---|
| frames presented in 59s | 2012 | 2917 |
| median gap between frames | 26ms | 22ms |
| p99 gap | 223ms | 86ms |
| worst gap | 3131ms | 2247ms |
| gaps > 33ms | 27.4% | **12.3%** |
| gaps > 100ms | 4.3% | **0.9%** |
| time lost inside long gaps | 45.2s of 59 | **23.1s of 59** |
| mpv's own output-drop count | 246 | 27 |

GUI stall probe, same runs: 144 stalls / 19678ms blocked -> 124 stalls / 12889ms blocked.
Residual INNER attribution is still `QEvent::Timer` delivered to `Main_QMLTYPE_98` (the Main.qml
root): **111 occurrences, 8461ms total, worst single stall 702ms.**

## What is RULED OUT (each killed by measurement today — do not re-open without new evidence)

1. **mpv / the video engine.** Decoder drops are 0. mpv delivers frames on time.
2. **Copy-back and OpenGL as a structural wall.** Plain mpv forced into the app's exact handicap
   (`--gpu-api=opengl --hwdec=d3d11va-copy`) performed the same as its native D3D11 path (13 vs 15
   drops/60s). The `d3d11va-copy` fallback is forced (native `d3d11va` needs `--gpu-context=d3d11`,
   impossible while mpv renders through Qt's OpenGL scene) but it is NOT the bottleneck.
3. **libVLC.** Same wall, worse Qt Quick support, and its zero-copy output-callback API is 4.0-only.
4. **Hardware throttling.** CPU ran at 98-142% of nominal (turbo engaged) throughout playback; GPU
   peaked at 46%. `tests/thermal_probe.sh`.
5. **`interpolation` / `video-sync=display-resample`.** Indistinguishable across 5 interleaved runs.
6. **Player 2 / a direct-present rewrite.** Irrelevant to this defect: swapping the video engine does
   not unblock a GUI thread. Player 2 would inherit the same stall.

## The instrument problem (READ THIS BEFORE MEASURING ANYTHING)

Single runs on this laptop are worthless. **Two runs of an IDENTICAL mpv configuration produced 13
and 400 dropped frames.** Five wrong root causes were declared today from single runs. Also:

- **mpv's dropped-frame counter cannot see this defect at all.** mpv delivers the frame; the app sits
  on it. mpv counts nothing. Do not use drop counts as your primary signal.
- The primary signal is **time between frames reaching the screen** — `tests/frame_pacing.sh`.
- For any A/B claim use **`tests/drop_ruler.sh`** (interleaved, order-reversed, refuses to run while a
  build is in progress, reports median + range). **A difference only counts if the ranges do not
  overlap.**

## Instruments in the tree (all on master @ ea8079f)

- `tests/frame_pacing.sh` — frame-to-screen intervals + the tail counts. **This is the one that found
  the bug.** Parses Qt's `qt.scenegraph.time.renderloop` `syncAndRender: start, elapsed since last
  call: N ms` lines.
- `native/GuiStallProbe.h` — env-gated (`COLOSSEUM_GUI_STALL_PROBE=<ms>`) GUI-thread stall attribution.
  Prints `GUI_STALL_PROBE HIT atMs=... blockedMs=... <type>|<ReceiverClass>` per stall with nested
  inner culprits, and a ranked `INNER` summary at exit. Inert when the variable is unset.
- `tests/drop_ruler.sh` — interleaved multi-run A/B with spread.
- `tests/thermal_probe.sh` — CPU clock vs drops.

Driving playback headlessly: set `COLOSSEUM_MPV=1`, `COLOSSEUM_ABBA_CLIP=<file>`,
`COLOSSEUM_MPV_DROP_PROBE=<warmupSec>,<measureSec>`; the app auto-plays and quits after measuring.

## The lead hypothesis (NOT proven — prove or kill it first)

The fix in `qml/Main.qml` stops the `warmer` timer from **appending new** world pages while the
player is open. It does **not** stop world pages the warmer **already queued before playback began**
from continuing to build. Those Loaders are `asynchronous: true`, and QML asynchronous instantiation
is incremental and **driven on the GUI thread** — which matches the residual attribution (a timer
event on the Main.qml root). Each world page is ~190 tiles plus cover pre-cache.

Candidate directions (evaluate, do not assume):
- Suspend/slow QML incubation while `win.immersiveSurfaceOpen` (see `QQmlIncubationController`).
- Deactivate or defer the hidden `worldStack` Loaders while the player is open, without destroying
  the keep-alive behaviour users rely on (returning to a world must not re-fetch covers).
- Cover/image decode and network replies landing on the GUI thread during playback.

**Do not stop at the hypothesis if the probe says otherwise. The probe is the authority.**

## Definition of done

1. `GUI_STALL_PROBE` INNER attribution names the remaining culprit(s) concretely, with evidence
   quoted in the RTC (not a guess).
2. A fix in the listed files that reduces **gaps > 33ms** measurably below the current 12.3%.
3. **Proven with `tests/drop_ruler.sh` methodology** — interleaved, >= 5 rounds, before/after ranges
   reported. If the ranges overlap, say so and do not claim the win.
4. `native/build-msvc.bat` = `BUILD_OK` (grep the log for `error C` / `ninja: build stopped`; the exit
   code lies in this project).
5. `progress_store_harness` and `progress_watched_override_harness` both 20/20 (run with Qt bin on
   PATH and `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`, else they exit 127 on missing DLLs).
6. No user-visible regression: returning to a world page after playback must not re-fetch its covers.

## Files

- `qml/Main.qml` — the `warmer` timer, `worldStack`, the world Loaders (primary suspect area)
- `qml/Warming.js` — warming order logic, if the fix belongs there
- `native/main.cpp` — ONLY if an incubation controller must be installed (keep additive + env-gated)
- `native/GuiStallProbe.h` — may be extended for finer attribution
- `tests/frame_pacing.sh`, `tests/drop_ruler.sh` — may be extended; do not weaken their guards

Do not touch: `qml/PlayerPage.qml`, `native/ProgressStore.h`, anything under `qml/player2*` or
`native/player2/` (Agent 4's in-flight chrome-port lane).

## Known traps

- `qml/` is loaded from disk at runtime: QML-only changes need **no rebuild**.
- The build script must be invoked by ABSOLUTE path; `cmd /c build-msvc.bat` from a shell silently
  opens a prompt and does nothing while reporting success.
- Qt stores `QStringLiteral` as UTF-16 — an ASCII grep of the .exe false-negatives.
- Bare `MpvItem` windows do not paint; the isolation probe at
  `tests/mpv_qtquick_tenet_probe.qml` produces a black window and no counters.
- The film's opening credits are low-motion and flatter every measurement. Measure from 00:06:00.
