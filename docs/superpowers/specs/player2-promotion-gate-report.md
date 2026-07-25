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
