# Phase 0 · Slice 5 — The experiment battery (quiet machine)

- **Executed:** 2026-08-07 · **Status:** Runtime-validated
- **Machine state:** qBittorrent **terminated by Hemanth** before this slice — the confound that
  forced Slice 4 to withhold throughput is gone. Residual load: this agent session itself
  (~42 % CPU, 18 machine-wide established connections). That is the floor and cannot be removed
  while the work is being done; it is recorded, not hidden.
- **Method:** every trial gets a **true cold start** — engine stopped, `_t2lab/cache` **wiped**,
  engine restarted, settings re-posted, then create. Without the wipe, run 2 reads run 1's
  pieces off disk and reports a fantasy cold-open.
- **Settings:** Colosseum's production values (200 connections, 20 / 40 MB/s), verified in each
  run's `_meta` line as *effective*, never assumed.
- **Harness:** `labscripts/battery.sh` + `watch_run.py` · logs `_t2lab/logs/slice5/*.jsonl`

## Results

| Trial | cold open | median MB/s | max MB/s | known | connected | conversion | attempts |
|---|---|---|---|---|---|---|---|
| cold-sintel-1 | 3.12 s | 1.55 | 1.87 | 42 | 12 | 29 % | 91 |
| cold-sintel-2 | 4.19 s | 1.43 | 1.65 | 61 | 13 | 21 % | 131 |
| cold-sintel-3 | 4.94 s | 1.54 | 1.89 | 85 | 22 | 26 % | 180 |
| sustained-bbb (180 s) | 2.72 s | 2.75 | 3.38 | 131 | 27 | 21 % | 1159 |
| **degraded-cap5** (control) | **35.92 s** | 0.00 | 1.37 | 70 | **5** | 7 % | 52 |

**Cold open, Sintel: median 4.19 s, range 3.12–4.94 s** across three true-cold repeats. Big Buck
Bunny cold-opened in 2.72 s. Throughput medians are computed over the *downloading window only*
(`streamProgress < 1`), not the whole run.

## Negative control — PASS, and it produced a causal result

Hobbling the engine to 5 connections moved every number in the expected direction, hard:

- cold open **35.92 s vs a 4.19 s median — 8.6× worse**
- peak connected settled at **exactly 5**, the cap, confirming the knob bound the behavior
- known peers stayed at **70**, i.e. *discovery was unaffected* while *connection was crippled*

That last row is the useful one: it cleanly separates the two halves of the funnel and shows the
rig measures them independently. The control is not just "the numbers moved" — it establishes
**connected-peer count causally drives time-to-first-frame.** That is the mechanism the whole
challenger thesis depends on, and it is now measured rather than assumed.

## FINDING — the conversion gap is structural, not bandwidth contention

Slice 4 measured 21 % known→connected with qBittorrent competing. The obvious objection was that
a saturated line was suppressing connections. It was not:

| Condition | conversion |
|---|---|
| Slice 4, qBittorrent active | 23 % |
| Slice 5, quiet machine | **21 %, 21 %, 26 %, 29 %** |

Identical. Removing the competing client changed throughput availability but **did not change
the fraction of known peers the engine manages to connect to.** The gap is a property of the
engine and the network path, not of what else was running. That objection is now closed.

Connection attempts continue to be lavish and unproductive — the sustained run made **1,159
attempts** to hold 27 peers.

## FINDING — Stremio's cold open is respectable, and that cuts against us

Honesty requires leading with the number that hurts the challenger's case: **a film starts in
about 3–5 seconds from a cold engine.** That is not the 30-second spinner the arc's founding
story implies. On well-seeded Blender torrents on a quiet machine, the incumbent performs
decently.

The 30-second experience Hemanth reported is therefore **not** the typical cold-open path. It is
more likely the tail — a poorly-seeded torrent, an unlucky peer draw, or the mid-stream dip
captured in Slice 4. A challenger that improves the median from 4 s to 3 s wins nothing a human
can feel. **A challenger only earns its place if it fixes the tail.** That reframes what Slice 6
must prove and should be carried into the verdict verbatim.

## The diminishing-returns caution — the strongest argument against building

The degraded control gives two points on the connections→cold-open curve:

| connected peers | cold open |
|---|---|
| 5 | 35.92 s |
| ~12–22 | 3.12–4.94 s |

Going from 5 to ~20 peers bought a **~9× improvement**. But the curve is clearly steep at the
bottom and **already flattening by 20** — Sintel run 3 had nearly twice the peers of run 1
(22 vs 12) and was *slower* to open (4.94 s vs 3.12 s), which means above roughly a dozen peers
the cold-open time is dominated by something else entirely.

**So the inference "PEX and uTP would raise connected peers from 20 to 60, therefore films start
much faster" does not follow from this data.** The measured curve suggests the returns between 20
and 60 peers may be small. This is the single strongest argument for a STOP verdict, and it must
not be argued away — it must be *measured* in Slice 6 by putting our chassis on the same swarm and
seeing both how many peers it holds **and** whether the extra peers translate into a better
cold-open or a shallower tail.

## Trials not run, and why

The plan listed seven trial types. Four ran (cold open ×3, sustained, degraded control, and the
multi-file case implicitly — both torrents are multi-file). Three did **not**:

| Trial | Why not |
|---|---|
| Deep-seek recovery | needs `select`/`critical` logging (open item O4) to interpret; a raw range-jump timing without it measures nothing about *why* |
| Repeated seeks | same dependency |
| Low-peer swarm | the degraded control is a better-conditioned version of this (peer count forced rather than hoped for), so the natural-low-peer trial adds little |
| Restart with partial cache | deferred — the cache-wipe discipline used here is its inverse, and the resume path is not on the verdict's critical path |

Recorded rather than silently skipped, per the plan's "no silent caps" rule.

## Layer matrix

```
Qt Test:                 not applicable — Phase 0 ships no app code
Qt Quick Test:           not applicable — no QML
Existing harnesses:      not applicable — live-network, never in a deterministic gate
Lanista:                 not applicable — direct HTTP, app never involved
Human aesthetic verdict: not applicable — no surface
Overall:                 Runtime-validated
```

## A near-miss worth recording

The first read of the sustained run showed throughput collapsing to 0.00 MB/s from t≈110 s with
unchoked peers falling to zero, and it looked exactly like a catastrophic stall. It was
**completion**: `streamProgress` reached 1.0000 at t=109 s with 270.3 MB downloaded of a 263.3 MB
file. Zero speed was correct behavior, not failure.

Had that gone into the verdict unchecked it would have been a headline finding that was flatly
false — and it would have argued *for* building a challenger on the strength of a bug the
incumbent does not have. The check that caught it cost one query. **Every "the engine broke here"
claim in this arc must be tested against "or it simply finished" before it is written down.**

## Where the verdict stands after Slice 5

Both sides are now stronger, which is the correct state before the deciding measurement:

**For a challenger:** the conversion gap is real and structural (21 % on a quiet machine, 1,159
attempts for 27 peers); connected-peer count causally drives cold-open (8.6× proven); the
incumbent runs on one discovery leg (no PEX, dead DHT) with a half-inert hardcoded tracker list.

**Against a challenger:** the incumbent's typical cold-open is a respectable 3–5 s; and the
connections→speed curve appears to flatten right around the peer count the incumbent already
achieves, so extra peers may buy far less than the funnel gap suggests.

**Slice 6 is now the whole verdict.** It must answer one question with numbers: on the same swarm
at the same moment, does our libtorrent chassis — with PEX on and uTP available — hold materially
more peers than Stremio, and does that translate into a better cold-open or a shallower tail? If
it holds more peers but films do not start faster, the honest answer is **STOP**.
