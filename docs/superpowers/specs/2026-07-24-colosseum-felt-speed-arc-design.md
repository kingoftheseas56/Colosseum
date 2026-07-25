# Colosseum — The Felt-Speed Arc (design specification)

**Date:** 2026-07-24
**Author:** [Agent 0 (Claude), foundation] — brainstormed on Fable, per model-routing doctrine
**Status:** Approved by Hemanth (brainstorm locked 2026-07-24; sections approved incrementally)
**Predecessors:** connection-concierge (`cb0d610`→`62d6a7f`, merged), playback-stutter audit
(2026-07-20), A2's ONNX/whisper/alignment/guided revert (staged, Hemanth-directed)

---

## 1. Experience promise

**Colosseum stops making you watch it work.** Every shelf you've ever seen paints instantly and
completely; every mode opens like a tab switch; the one remaining "loading" look is a designed
color-wash, not popcorn. And the app holds that speed permanently — a regression fails the build
before it ever reaches Hemanth's screen.

Context: the app had begun accreting weight (the ML read-along/guided subsystems — now reverted as
scope creep — plus a still-unproven poster pipeline). This arc is the deliberate counter-move:
lightweight, fast, responsive, **zero functionality lost**.

## 2. Scope — four stages, strictly ordered

### Stage 0 — Prove the pipe (gate for everything else)
The concierge merged TDD-green but has **never been proven on the real binary**; Hemanth's last
eyes-on still showed half-blank shelves. Nothing downstream ships until this gate passes.

- **Poster scoreboard:** the real binary counts every poster request into exactly one bucket —
  *arrived / network-failed / undecodable* — with reason and host, at the central network layer
  plus a decode-failure hook on tiles. Surfaced in the dev readout and the log.
- **Fix what the count convicts** (known suspects, smallest moves only):
  - **Bundle the WebP decoder** into the app's own deploy (today it is a dev-machine hack inside
    the local Qt install — covers silently fail on any other binary). Boot verifies the decoder is
    present; the scoreboard reports it.
  - **Pin the remaining art host(s)** (wsrv.nl et al.) into the concierge/NAM fast path.
- **Gate:** Hemanth opens the app and the shelves are simply full. His eyes close the stage.

### Stage 1 — The remembered shelf
Every wall snapshots itself; entry repaints **instantly from disk** — full art, zero blanks — while
the network refreshes silently underneath. Never reshuffles under the cursor. Side-gift: every
shelf already seen browses **offline**.

### Stage 2 — Warm the next room + color-wash
After launch settles, mode pages quietly pre-build in an idle queue, so the first click into
Tankoban/Biblio/Theatre lands on an already-built page. Never-before-seen posters arrive as a soft
wash of the poster's own dominant color with the art fading in over it — the app's **only** visible
loading state, everywhere.

### Stage 3 — The speed guardrail
Hidden dev-flag readout (boot-to-first-pixel, mode-entry time, scroll frame drops, poster
scoreboard, memory) + automated budget tests set from measured baselines. Any future change that
slows a locked number fails the build.

## 3. Decision ledger (as locked)

**Locked**
0. Prove the pipe before decorating it (Stage 0 gate; added at Hemanth's objection — the concierge
   was unproven on the real binary).
1. Arc = **felt speed only**, aimed at the two pains Hemanth ranked: **browsing/posters** and
   **first entry into a mode**.
2. Remembered shelf — instant disk repaint, silent refresh, no live reshuffle.
3. Warm the next room — background pre-build; a click always beats the warmer.
4. Color-wash fade — dominant-color wash for first-sight posters; the single loading language.
5. Full guardrail — hidden readout + budget tests that fail the build on regression.
6. The ML/whisper/alignment/guided revert stands (A2 executed it at Hemanth's direction).

**Constraints**
- Zero functionality loss anywhere in the arc.
- A2's read-along work is untouched (Hemanth is finishing it himself).
- No reshuffle-under-the-cursor: refresh updates in place; order changes apply on next entry.
- No new user-facing controls; readout and scoreboard live behind the existing dev flag
  (menus-default-OFF house rule).
- QML paints, C++ decides: stores, counters, caching, and network live in C++.
- The snapshot store is capped (LRU eviction) — "remembered" never becomes "bloated."

**Deferred (outside this arc)**
- Footprint diet (installer ~334 MB / ~1 GB staged; WebP bundling is in-scope only as the Stage-0
  decode fix, not as size work).
- Player 1 volume-leveling filter (the 07-20 audit's fix) — **superseded**: Player 2's
  AudioNormalizer already ships the fix as typed modes (Smooth = passthrough / Light = cheap
  dynaudnorm / Full = loudnorm). Verified in `native/player2/audio/AudioNormalizer.{h,cpp}`.
- Player 2's **default** audio mode — A4's swap task; Hemanth's ears are the gate.
- Cold-start boot surgery (Hemanth did not rank cold start; ~20 services still construct eagerly
  in `native/main.cpp` — only touched if a win falls out free of Stage 2).
- captured-motion asset removal + wallpaper-host pinning beyond posters (separate queued thread
  from the humbled-current wake).

**Open** — none.

## 4. Primary user journey

1. **Launch.** Home appears as today. Behind it, unrushed, the other rooms pre-build and the
   scoreboard starts counting.
2. **First click into a mode.** The page is already constructed — lands like a tab switch. The
   wall paints instantly with the exact art last seen there, edge to edge, zero blanks.
3. **Scroll.** Smooth rows; posters ahead of the scroll are decoded and waiting. Nothing pops.
4. **A brand-new show.** Its tile arrives as a wash of the poster's dominant color; art fades in.
5. **Something changed upstream.** The tile updates in place, quietly. Order never shuffles
   mid-visit; reordering waits for the next entry.
6. **A month later, someone adds a heavy feature.** The budget test trips in *their* build, not on
   Hemanth's screen.

## 5. States, interruptions, recovery, edge cases

| Situation | Behavior |
|---|---|
| **Offline** | Every visited shelf paints whole from disk. Unseen tiles hold their color-wash — no error popups, no broken-image icons; art fades in when the network returns. |
| **First-ever launch / fresh install** | No snapshots yet → the whole app speaks color-wash on first sight (a designed reveal, not a broken load). Remembered shelves take over from session two. |
| **Snapshot missing/corrupt** | The wall silently behaves as a first visit (wash → art) and re-snapshots. Self-healing, no visible error. |
| **Disk pressure** | Fixed cap; least-recently-visited shelf evicted first. An evicted wall acts like a first visit. |
| **Click before warming finishes** | The click always wins. Warming yields instantly to any interaction, resumes on idle. Worst case ever = today's behavior. |
| **Show vanishes upstream mid-session** | Tile stays until next entry (the no-reshuffle rule wins); next visit is current. |
| **Poster fails / won't decode** | Wash stays; scoreboard counts it with reason. Systemic failures surface as a number in the readout, never as a wall of blanks Hemanth must report. |
| **App killed mid-snapshot** | Atomic writes (temp-then-rename) — the last good picture of the shelf always survives. |

## 6. Controls, feedback, accessibility, integration

- **New user-facing controls: none.** The arc is the app doing better with the controls it has.
  Readout + scoreboard sit behind the existing dev flag.
- **One loading language.** Color-wash-then-fade on every poster surface across all three modes and
  Theatre's Discover/Library walls. Same wash, same short calm fade. **No shimmer, no pulsing** —
  calmer, and kinder to motion sensitivity.
- **Fits what's built.** Extends the existing keep-alive page pattern (pages already stay warm
  after first visit — warming moves that to *before* it); rides the concierge's fast path; the
  scoreboard feeds the Stage-3 readout; budgets run in the test suite every build. No existing
  feature changes behavior — same shelves, same rows, same actions, minus the waiting.

## 7. Technical shape (contract-level; implementation stays free within it)

- **Scoreboard (Stage 0):** counters at the central `CachingNam` choke point (request → HTTP
  outcome → bytes + content-type, per host) + tile-side decode-error hook routed to one C++
  diagnostic object. Dev-overlay + log dump. Boot check reports whether the WebP image-format
  plugin loaded from the app's own deploy.
- **WebP fix (Stage 0):** the decoder plugin ships in the app deploy (imageformats beside the exe),
  removed from reliance on the dev Qt install. Deploy scripts own it; boot verifies it.
- **Host pinning (Stage 0):** remaining art hosts join the concierge/NAM pinned-host set.
- **Remembered shelf (Stage 1):** per-shelf manifest — ordered item ids, titles, badge state,
  poster cache key + dominant color — persisted atomically, capped with LRU eviction, owned by a
  C++ store. Poster bytes stay in the existing disk cache under a policy that shields remembered
  shelves' art from eviction by other traffic. Entry = one local read hydrates the model (instant
  paint); refresh diffs **in place** (field updates live; insert/remove/reorder staged into the
  manifest for next visit). The merge policy is one pure function — headless-testable.
- **Warming (Stage 2):** an idle queue flips the existing keep-alive mode Loaders on early, one at
  a time, at low priority; any user interaction pauses the queue instantly.
- **Color-wash (Stage 2):** dominant color extracted once at a poster's first decode, stored in the
  manifest; wash costs nothing at paint time; unknown color → neutral glass.
- **Guardrail (Stage 3):** readout overlay behind the dev flag (boot-to-first-frame, mode-entry ms,
  scroll frame-drop counts, scoreboard, RSS). Budgets set from measured baselines after Stages 0–2
  land, enforced as tests. **Honest limit:** scroll *feel* on this GPU is not judgeable headless
  (house invariant: Qt/D3D is uncapturable headless; pixels are Hemanth's eyes) — automated budgets
  guard timings and counters; his eyes remain the gate for feel.

**Testing discipline (house standard):** pure logic (merge policy, LRU eviction, scoreboard
aggregation, warm-queue ordering/yield) → headless harnesses; wiring → contract checks; every
stage ends at Hemanth's eyes.

## 8. Acceptance criteria (observable)

- **Stage 0:** a fresh run of the real binary shows failures ≈ 0 and *zero* undecodable-by-format;
  the WebP decoder verifiably ships beside the exe (not the dev hack); Hemanth opens the app and
  shelves are full.
- **Stage 1:** a revisited wall paints full art instantly (entry-to-art under a measured budget) —
  including with the network pulled; nothing reorders mid-session; a killed app wakes with its last
  good shelves.
- **Stage 2:** the first click into each mode lands like a tab switch (entry time under budget);
  a click during warming is never blocked; every never-seen poster arrives wash→fade — no popcorn
  anywhere.
- **Stage 3:** the readout exists behind the dev flag, and the budget tests demonstrably **fail**
  when a slowdown is deliberately injected (the alarm is proven before it is trusted).

## 9. Non-goals

No shelf/visual redesign · no new user-facing controls · no player changes (Player 2 swap is A4's
lane) · no installer/disk work · no boot-order surgery beyond what warming needs · nothing in
read-along · no re-litigation of the ML revert.

## 10. Discarded alternatives (with reasons)

- **Quiet skeleton shimmer** for loading tiles — generic streaming-app look; the color-wash is
  calmer and grows from the poster itself. (Hemanth chose the wash.)
- **Warm-only / instant-paint-only** first-entry fixes — each solves half the pause; the hybrid
  was chosen.
- **Player 1 loudnorm toggle** (the audit's recommendation) — correct in July, superseded by
  Player 2's typed normalizer modes; building settings UI for a player being replaced is waste.
- **Footprint-led arc** — Hemanth ranked felt speed; size work rides a later pass.
- **No instrumentation ("just optimize")** — blind now, silently regressing later; rejected for
  the full guardrail.

## 11. Transition

Next step (separate workflow, per doctrine): implementation plan from this spec — Stage 0 first,
nothing downstream ships before its gate. Execution belongs on Opus; planning stays on the
planning budget.
