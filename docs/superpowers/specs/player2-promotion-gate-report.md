# Player 2 — Promotion Gate Report (Task 16)

> Updated 2026-07-25, branch `agent4/player2-task8-seek` @ `dc501c6`+. Hardware: Intel i5-8365U +
> UHD 620 (iGPU), Windows 11 Pro. Clip: The Wire S4E10 (H.264 MKV, AC3, PGS subs, ~58min).
> Every number below was MEASURED on this machine by the named script; nothing is estimated.

## Measured green (this machine, debug/lab build)

| Gate | Script | Result |
|---|---|---|
| Deterministic tier (9 unit tests + 4 contracts + boot smoke) | `player2_gate_summary.ps1` | **PASS** |
| A/V sync 1.0× | `player2_av_sync_gate.ps1` | p95 **2.86–10.73ms** (bar 40) over 15–30s soaks |
| A/V sync 1.5× (atempo path) | `player2_av_sync_gate.ps1 -Speed 1.5` | p95 **20–24ms**, fps 35 (truly faster) |
| Seek soak, FULL | `player2_seek_soak.ps1` | **100/100** deterministic seeks landed, 0 underruns, 0 device errors |
| Memory soak machinery | `player2_memory_soak.ps1` (smoke 45s/2cyc) | **2/2 close→reopen cycles** back to Playing; external RSS sampling + slope fit working |
| Hardware matrix, Intel row | `player2_hardware_matrix.ps1` | sync 1×/1.5× + 25-seek PASS on UHD 620 → `player2_hardware_matrix_results.jsonl` |

## Known measured limits (honest, not hidden)
- **2.0× speed on UHD 620:** p95 45.38ms (bar 40) because the iGPU caps ~36fps (can't render 48).
  Sync logic is proven by the green 1.5× row; this is a throughput ceiling for the discrete-GPU row.
- p95 under a seek storm reads near 0 (generation resets); the seek gate's real assertions are
  all-landed + zero underruns + zero device errors.

## Outstanding before Task 17 (the promotion preconditions)
1. **Release-build long runs:** 30-min sync soak · 2h memory soak + 50 cycles (`player2_memory_soak.ps1`
   defaults) · 100 HTTP seeks (needs the HTTP fixture) — scripts ready, runs need the machine for hours.
2. **ABBA efficiency ≥25% vs mpvqt** — `player2_efficiency_abba.ps1` refuses to run without the real
   production player (`-ProdExe`); it never fabricates a baseline. HARD NO-GO if the advantage misses.
3. **Discrete-GPU matrix row** — run `player2_hardware_matrix.ps1` on the second machine.
4. **Licensing decision** — lab FFmpeg DLLs are the GPL build; recommend the LGPL drop-in at packaging
   (see `player2-runtime-licensing-manifest.md`).
5. Cross-substrate review of raw samples/arithmetic before the Task 17 flag flip (Fable reviewed the
   chrome + engine 2026-07-25; the numeric re-check rides the release runs).

## ABBA efficiency gate — RUN 2026-07-25

**Result: PASS on GPU, regression on CPU.**

| Metric | Player 2 | mpv (production) | Advantage |
|---|---|---|---|
| GPU (3D engine, system-wide counter) | **21.0%** | 57.7% | **63.6% less** |
| CPU (normalized per core) | 17.9% | **15.6%** | **15% more** (a regression) |

Method — and why the first attempt was thrown away. The runner originally launched production as
`colosseum.exe "<clip>"`, but that argument selects a QML entry point, not a media file: production
would have idled on its home screen while Player 2 decoded video. A second attempt drove mpv through
a synthetic probe window, which decoded (position advanced) but never painted — Hemanth caught the
black window on screen. Both would have handed Player 2 a fabricated win.

What was finally measured: **both backends inside the REAL app**, same clip, same window, same session
machinery, ABBA order (P2, prod, prod, P2) with 180s passes and 60s cooldowns so warm-up and thermal
drift cancel. The only difference between passes is which engine draws, so the app's own overhead is
identical on both sides and cancels out. The runner refuses to score a pass that did not start the
clip, ran on the wrong backend, or fell back mid-measurement.

Evidence the Player 2 passes were genuinely rendering (not a fast black screen): 13 and 6 transient
missing-texture lines at startup, against 275 in the known-black failure earlier the same day.

Reading it honestly:
- The GPU result is the headline and it is large. On this Intel UHD 620 the GPU is the binding
  constraint for video, so 57.7% -> 21.0% is the difference that matters for heat, battery and
  sustained smoothness.
- **The CPU regression is real and must not be buried.** Part of it may be the integration rather
  than the engine: `Player2Backend::pump()` calls `update()` on the video item at 60Hz for the whole
  session (mirroring the lab's frame timer). That is a candidate to measure and trim before Task 18.
- Hardware caveat unchanged: one machine, integrated graphics. The discrete-GPU row is still NOT RUN,
  and the 2x-speed fps ceiling measured earlier is a property of this GPU, not of either player.
