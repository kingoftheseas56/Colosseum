# Player 2 — Lab Parity Ledger

> **FROZEN 2026-07-25** at branch `agent4/player2-task8-seek`, commit `01e4477` (chrome-parity marathon
> `f530597`→`b2953db`→`cda7d13` + Fable review fixes `01e4477`). Eyes-on verdicts are Hemanth's, across
> three sessions on 2026-07-24/25; the final pass ("done I checked everything" + post-review "it looks
> great") closed every built surface. Cross-substrate review: **Fable 5** re-derived the whole diff
> (engine: clean; chrome: 3 drifts + 1 minor, fixed in `01e4477`). Integrated status reopens at Task 17.
>
> **INTEGRATED COLUMN OPENED 2026-07-26** — first eyes-on of Player 2 running *inside the real app*
> (commit `f00edfc`). Four rows carry his verdicts now: loading screen **FAIL** (font blink), seek-bar
> buffer tint **FAIL** (confirms a known gap), pause card **QUESTIONED**, download/pick-stream icons
> **EXCEPTION CONTESTED**. His standing instruction on all four: *"do not fix any of these because the
> review is still coming in."* Nothing below has been acted on. His verdict on the whole: *"this has
> been a wonderful accomplishment… you have fast tracked what took us months in a matter of days. but
> the few remaining rough edges still take away from it because our orginal player was gorgoues and we
> ned to maintain absolute parity."*

Parity is a ledger, not a feeling. A row is complete only when the behavior exists in the standalone
harness, has automated evidence where practical, AND passes an eyes-on comparison against the current
player. Status: `NOT RUN`, `FAIL`, `PASS`, `OPEN GAP` (needs a build or Hemanth's exception word), or
`ACCEPTED EXCEPTION (<Hemanth's written reference>)`.

## Core playback chrome (Task 13, closed through Tasks 14–15)

| Behavior | Production evidence | Harness test | Eyes-on result | Lab status | Integrated | Notes |
|---|---|---|---|---|---|---|
| Play / pause (button + Space + click) | PlayerPage transportRow hero | gate summary green | ✔ 07-24/25 | PASS | NOT RUN | |
| Exact seek (scrub) | seekBar → `mpv.seekExact` | seek-generation test | ✔ 07-25 ("seek freeze" root-caused + fixed candid-current) | PASS | NOT RUN | |
| Relative seek (±10s, ← / →) | `seekStep` | shell contract | ✔ | PASS | NOT RUN | |
| Seek bar (progress, chapter ticks, handle, hover) | seekBar | shell loads | ✔ | PASS | NOT RUN | ticks only — see Chapters row |
| Buffer/cache fill on seek bar | seekBar cache fill | — | ✖ 2026-07-26 IN-APP: *"no buffering tint on the seek bar/timeline bar"* | **QUEUED (post-swap)** | **FAIL (confirms the known gap)** | Not a surprise — his eyes confirm the gap the ledger already carried. SeekBar rect exists; `bufferedFraction` is never fed by the session. The engine seam is the work: HttpMediaSource already knows the buffered byte range, so this is plumbing, not invention. |
| Volume + mute (slider, M, wheel) | VolumeControl | shell contract | ✔ | PASS | NOT RUN | |
| Elapsed / duration / remaining toggle | seekRow labels | browser-logic gate (fmt) | ✔ | PASS | NOT RUN | |
| State line + ENDS wall clock + now clock | stateRow + Kodi clocks | `fmtWallClock`/`endsAtLabel` tested | ✔ 07-25 (`3e048a6`) | PASS | NOT RUN | |
| Auto-hide + cursor hide | `hideTimer` | shell loads | ✔ | PASS | NOT RUN | |
| Fullscreen request + icon toggle | fullscreen button + F | shell contract | ✔ 07-25 | PASS | NOT RUN | icon flips via host-fed `windowed` (Fable F4) |
| Prev / next episode | transportRow prev/next | adjacency via host seam | ✔ 07-25 | PASS | NOT RUN | peeks `requestAdjacentEpisode`; plays via `playEpisodeRequested` |
| Frame step (`,` / `.`) | `mpv.frameStep` | shell contract | ✔ | PASS | NOT RUN | |
| Playback speed | SpeedMenuButton (speed column) | av-sync gate `-Speed`; 1.5× p95 24ms | ✔ 07-25 (ears: pitch-correct) | PASS | NOT RUN | REAL engine: atempo + clock rate. 2× = 45ms p95 on Intel UHD 620 (fps ceiling ~36) — hardware-matrix item, not a sync defect. No `[`/`]` hotkeys (P2's smaller registry, drift-guarded) |
| Sleep timer + skip-step (speed panel columns) | SpeedMenuButton | — | — | ACCEPTED EXCEPTION (approved absent, eyes-on 07-25 "it looks great"; no engine for either) | NOT RUN | |
| Audio track menu + delay | AudioMenu + SYNC | TrackMenu wired | ✔ | PASS | NOT RUN | icon-button presentation (vs P1 chips) approved eyes-on Task 13 |
| Subtitle menu + delay | SubtitleMenu | TrackMenu wired | ✔ 07-25 ("checked all of it") | PASS | NOT RUN | embedded tracks; external-file/online/styling need engine seams (flagged, not faked) |
| Subtitle rendering (text + PGS bitmap, clock-synced) | mpv/libass | subtitle image/schedule/timing tests | ✔ 07-25 ("subtitles work now brother") | PASS | NOT RUN | Known refinements: letterbox positioning; multi-rect PGS (front rect only) |
| Fit / fill / aspect | FillMenuButton | shell contract | ✔ 07-25 ("it looks great" post-Fable) | PASS | NOT RUN | LEFT-cluster placement + "Video" popover (Fable F1) |
| Loudness normalization | overflow loudness | normalizer tests + benchmark | ✔ | PASS | NOT RUN | |
| Chapters (labels, current, menu) | chapter transient/menu | — | — | **QUEUED (post-swap)** | NOT RUN | ticks render; no labels/menu |
| Stats overlay (`D`) | statsOverlay | diagnostics contract | ✔ | PASS | NOT RUN | |
| Loading / buffering / error screen | PlayerLoadingScreen | — | ✖ 2026-07-26 IN-APP: *"the loading/bufferig screen is shaky. it alternates between the slick per-show custom font to genric font in a blinking fashion"* | BUILT (`0441cab`, shipped screen reused verbatim) | **FAIL** | FIRST integrated eyes-on of this surface. Font resolves, then falls back, repeatedly — the lab never showed it. Suspect font registration/timing differing under the Player 2 boot, or the screen being re-created per state change (each rebuild restarts family resolution). NOT diagnosed, NOT fixed — held for the Codex review. |
| Hotkeys + shortcuts sheet (`?`) | PlayerHotkeys.js + ShortcutsSheet | shortcuts drift-guard contract | ✔ 07-25 | PASS | NOT RUN | P2's real (smaller) registry; sheet mirrors P1 presentation; no scrollview (content fits at min height) |
| Context menu (right-click) | overflowPanel | shell loads | ✔ | PASS | NOT RUN | |
| Pause card | pauseCard | pause-card helpers tested | ✔ 07-25 lab (`3d6c240`; no rating — his veto) · ⚠ 2026-07-26 IN-APP: *"not sure if the pause banner's QML is the same as the original player"* | PASS (lab) | **QUESTIONED** | His doubt is the finding; it outranks my lab PASS. Owed: a literal side-by-side of P2's pauseCard against production `PlayerPage`'s, element by element — not a re-judgement by me of my own work. [[parity_until_swap_match_production_element]] |
| Compact folds (tight/snug/tiny) | width-driven folds | — | — | **QUEUED (post-swap)** | NOT RUN | fixed roster; fine ≥ lab min-width (Fable observation) |

## Source / episode / skip / window / capture / live (Tasks 14–15)

| Behavior | Production evidence | Harness test | Eyes-on result | Lab status | Integrated | Notes |
|---|---|---|---|---|---|---|
| Episode/source drawer (E, tabs, season pills, rows) | Feature 8 BrowserDrawer | browser-logic gate + host-seam test | ✔ 07-25 (`8a0d6f7`) | PASS | NOT RUN | pick→actual switch needs prod pipeline (Task 17) |
| Skip Intro/Recap/Credits pill | skip pill | `activeSegment`/`skipLabel` tested | ✔ 07-25 (parity fix `3e048a6`) | PASS | NOT RUN | lab fixture times (no real AniSkip addon) — his own diagnosis |
| Progress reporting cadence | resume/watched persistence | `shouldReportProgress` tested + event ledger | headless-proven | PASS | NOT RUN | |
| Close-confirm | closeConfirmPanel | `shouldConfirmClose` tested | ✔ 07-25 | PASS | NOT RUN | paused-close exits without prompt (Fable F3, P1 parity) |
| PiP + window lifecycle | WindowMode pip | seams wired; lab toggle | ✔ 07-25 | PASS | NOT RUN | overflow trigger is LAB-ONLY; production drives PiP natively |
| Power inhibit (keep-awake) | display sleep inhibit | `shouldInhibitSleep` tested + `[lab] keep-awake` log | headless-proven | PASS | NOT RUN | production wires `SetThreadExecutionState` at Task 17 |
| Screenshot / GIF capture | mpv screenshot + GIF pipeline | — | — | ACCEPTED EXCEPTION ("you can skip screenshot and gif honestly", 2026-07-25) | NOT RUN | |
| Live guide / DVR panels | LiveGuide/DvrPanel | — | — | ACCEPTED EXCEPTION (left unchecked in the 2026-07-25 surface pick: "Shortcuts sheet, Close-confirm, Window: minimize/PiP") | NOT RUN | |
| Retry / pick-stream / download HUD buttons | leftUtilityRow | — | ✖ 2026-07-26 IN-APP: *"the video player does not have te download icon and another icon for changing streams if I'm remember correctly"* | ACCEPTED EXCEPTION ("Keep sources in the drawer for now", 2026-07-25) | **EXCEPTION CONTESTED — needs his word** | ⚠ His 07-26 eyes CONTRADICT his own 07-25 exception. Read literally, "for **now**" was always temporary and this is him calling time on it. But the exception is his written word and I will not overturn it by inference: **does the exception stand, or do the download + pick-stream HUD icons come back?** Recorded, not acted on. |

## Numeric gates (Task 16)

| Gate | Result | Status |
|---|---|---|
| A/V sync p95 ≤ 40ms | **7.6ms** (15s smoke, The Wire); 1.5× speed **24ms**; 1× post-tempo regression **2.86ms** | PASS (smoke; 30-min release run pending) |
| Deterministic tier (9 unit + 4 contracts + smoke) | `PLAYER2 PROMOTION GATES: PASS` | PASS |
| 2h soak · 100 seeks · 50 open/close · memory | harness modes + scripts being built this wake | NOT RUN |
| ABBA efficiency ≥25% vs mpvqt | **GPU 21.0% vs 57.7% = 63.6% LESS GPU** (passes); CPU 17.9% vs 15.6% = **15% MORE CPU** | **PASS** (GPU; 2026-07-25) |
| Hardware matrix (Intel + discrete) | needs 2nd GPU; will also clear the 2×-speed fps ceiling | NOT RUN |

## Freeze note

Four **OPEN GAP** rows (cache-fill fraction, chapter labels/menu, loading/error screen, compact folds)
were surfaced for Hemanth's build-or-except word rather than silently blessed. Everything else is PASS
or carries his written exception.

**RULED 2026-07-25 (Hemanth, verbatim):** *"we still build all of these: seek-bar cache fill · chapter
labels/menu · loading/error screen · compact folds. But I suppose they can wait after the player's been
swapped."*

So: **all four are BUILD, not except — sequenced AFTER the swap (Tasks 17–18).** They are no longer open
questions blocking Phase D; they are queued work with his date on them. Re-status each row to `QUEUED
(post-swap)` as it is built, and do not close the integrated ledger while any of the four is unbuilt.
