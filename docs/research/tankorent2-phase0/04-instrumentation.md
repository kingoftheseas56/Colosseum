# Phase 0 · Slice 4 — Watching the engine run

- **Executed:** 2026-08-07 · **Status:** Runtime-validated, with one measurement class explicitly withheld
- **Engine:** lab specimen, server 4.20.17, port 11480 (production port never touched)
- **Harness:** `labscripts/watch_run.py` · logs `_t2lab/logs/slice4/*.jsonl`
- **Torrents (all Blender open movies, legal, well-seeded):** Sintel
  `08ada5a7…`, Big Buck Bunny `dd8255ec…`, Tears of Steel `209c8226…`

## Method — re-scoped by Slice 2, and cheaper for it

The plan expected to patch a second copy of the engine and log from inside it. Slice 2 found
that `getStatistics` **already** returns everything the discovery questions need — `unique`,
`sources`, `queued`, `connectionTries`, `swarmSize`, `wires[]` — and that Colosseum simply
never reads those fields. So this slice patched **nothing**. It drives a real ranged GET (like
mpv would, never through Colosseum) and samples `stats.json` at the app's own 1 Hz cadence, so
every number here is directly comparable to what the player displays.

Playback was driven by direct HTTP throughout. Colosseum was not involved in any run.

## FINDING 1 — DHT contributes exactly nothing on this machine

Across three separate torrents, in three separate engines:

| Torrent | DHT peers found | DHT requests | Tracker peers found |
|---|---|---|---|
| Sintel | **0** | 3 | all of them |
| Big Buck Bunny | **0** | 1 | 189 unique |
| Tears of Steel (DHT-only run) | **0** | 1 | all of them |

**Every peer the engine has ever connected to this session came from a tracker.** The
distributed lookup — the half of BitTorrent that works without any central announce — returned
zero peers every time, and barely even issued requests (1–3 in 60–100 s).

This corroborates the house's existing IPv4-pin scar (dead ISP IPv6 → 0 DHT nodes) from a
completely independent direction. It also means the swarm-visibility problem is worse than the
PEX finding alone suggested: the engine has **no peer-exchange** (Slice 2, structural) *and*
**a non-functioning DHT** (here, measured). Both of the two ways to find peers without a
tracker are off the table. Tracker announces are the sole supply.

## FINDING 2 — discovery is NOT the bottleneck; conversion is

Big Buck Bunny, under Colosseum's real production settings (200 connections, 20/40 MB/s):

| Measure | Value |
|---|---|
| Unique peers discovered across all sources | **189** |
| Peak peers tracked at once (`unique`) | **172** (at t=56.6 s) |
| Peak peers actually connected (`peers`) | **39** |
| Peak unchoked (actually willing to send) | **32** |
| Cumulative connection attempts (`connectionTries`) | **728** |
| Connection ceiling in force | **200** |
| `queued` (peers waiting for a free slot) | **0**, every single sample |

Read that as a funnel: **189 found → 172 held in the roster → 728 attempts → 39 connected → 32
useful.** At the moment of maximum divergence (t=56.6 s) the engine knew about **172** peers
and was connected to **21**.

The ceiling is not the constraint — it never came within 160 of its 200 limit, and never
queued a single peer for want of a slot. Raising `btMaxConnections` from 55 to 200, which
Colosseum already does, moved peak connections only from 22 to 39. **The engine is not being
held back by permission to connect; it is failing to establish and hold connections to peers
it already knows about.** That is a reachability problem, and it is exactly the lever the
original expert review pointed at.

This is also the honest answer to Hemanth's founding question. "200 seeders, single-digit
peers" is real, and the missing peers are not undiscovered — they are **discovered and
unreachable**.

## FINDING 3 — a stall, captured in the act

A mid-stream stall was recorded in full, unprompted, at t≈79–90 s:

| t (s) | connected | unchoked | speed |
|---|---|---|---|
| 78.8 | 35 | 26 | 1.17 MB/s |
| 80.9 | 29 | 18 | 0.53 MB/s |
| 81.9 | 20 | 10 | 0.23 MB/s |
| 82.9 | 19 | 9 | 0.09 MB/s |
| 83.9 | **16** | **7** | 0.11 MB/s |
| 85.9 | 22 | 9 | 0.47 MB/s |
| 89.0 | 33 | 14 | 1.92 MB/s |
| 90.0 | 35 | 16 | 2.24 MB/s |

The speed collapse tracks the **peer population collapse** almost exactly — connected peers
more than halved, and unchoked peers fell by nearly four-fifths, before throughput recovered as
peers returned. Nothing about piece scheduling changed.

This matters for where a challenger would have to be better. April's arc spent itself on the
piece scheduler; this stall is a **peer-supply** event. One observation is not a law, and
Slice 5 must establish how typical this shape is — but it is a concrete, timestamped
counter-example to "stalls are a scheduler problem."

## FINDING 4 — the tracker list is hardcoded, and roughly half of it is dead

Module **806** exports a fixed list of 20 tracker URLs. The engine uses it **regardless of what
the caller supplies**: the DHT-only control passed a single `dht:` source and still ended up
with **21 sources** (20 built-in trackers + the one DHT entry), and got its peers from those
trackers. Every run this session showed exactly 21 sources.

Yield is extremely uneven (Big Buck Bunny, unique peers credited per tracker):

| Tracker | unique peers |
|---|---|
| `explodie.org:6969` | 60 |
| `tracker.opentrackr.org:1337` | 40 |
| `open.stealth.si:80` | 22 |
| `tracker-udp.gbitt.info:80` | 19 |
| `tracker.torrent.eu.org:451` | 16 |
| `open.demonii.com:1337` | 13 |
| `t.overflow.biz:6969` | 12 |
| `tracker.zhuqiy.com:443` | 5 |
| `tracker.theoks.net:6969` | 2 |
| **9 other built-in trackers** | **0 each** |
| `dht:` | **0** |

Nine of twenty trackers returned nothing at all. This is the "50-tracker spray versus a small
curated maintained list" lever the spec drew from Harbor, now with local numbers attached: the
spray is real, and about half of it is inert.

## Negative controls

**Control 1 — prove a knob change actually lands (required; PASS).** `POST /settings` with
`btMaxConnections: 5`, then read back: the effective value reported **5**. Restored to
production values (200 / 20 MB/s / 40 MB/s) and read back confirmed. Every run's log records
the *effective* `bt*` settings in its `_meta` line, so no claim in this document rests on a
setting we intended rather than one the engine confirmed. This forecloses the vacuous-pass
failure the plan warned about.

**Control 2 — peer-source attribution (required; PASS, but NOT by the route the plan wrote).**
The plan said: strip trackers, and peers must still arrive via DHT and be labelled as such.
**That control cannot pass as written, because DHT finds nothing here** — and that is Finding 1,
not a broken rig. Attribution is instead demonstrated by *differential response*: across 21
sources the per-source unique counts are 60 / 40 / 22 / 19 / 16 / 13 / 12 / 5 / 2 and nine
zeros. A broken attributor would credit one source for everything or report a uniform figure;
this one discriminates cleanly, dedupes (`numFound` 250 vs `numFoundUniq` 60 on the same
tracker), and reports genuine zeros for genuinely silent sources. The PEX conclusion therefore
does not rest on a vacuous counter.

**PEX, live.** No run produced a source of any kind other than `tracker:` or `dht:` — consistent
with Slice 2's structural finding that `peerSearch` can construct nothing else. Live behavior
matches the code reading.

## What is DELIBERATELY NOT claimed

**Throughput numbers are withheld.** qBittorrent was running throughout (15 established
connections, ~21 % of a core, actively transferring). It is Hemanth's client with his
downloads, so it was left alone. Speed figures observed (peak ≈ 3.5 MB/s on both torrents) are
therefore a **floor, not a measurement**, and no verdict may rest on them. House law is explicit:
measure only on a quiet machine.

**Cold-open time is not measured.** The reported first-byte times (1.00 s, 1.06 s) followed a
`create` that had already been running 6–10 s, so they are warm-start figures. A true
time-to-first-frame requires a fresh engine and a stopwatch starting at create.

Both belong to Slice 5, and **Slice 5 needs a quiet machine** — qBittorrent paused, nothing
else transferring. That is a request for Hemanth, not something to work around.

**Q2/Q3 still open.** Cold-open piece window and seek behavior were deferred here from Slice 2
and remain open: answering them needs `select`/`critical` call logging, which *does* require
patching the instrumented copy — the one thing this slice got to skip.

## Where the verdict stands

Still no GO — nothing decision-grade has been measured, because the measurements that decide it
are the ones withheld. But the picture sharpened considerably, and it moved **away** from STOP:

- Stremio's peer supply runs on one leg. No PEX (structural), no DHT (measured). Trackers only.
- It discovers plenty and connects to a fraction — 172 known, 21 connected at worst divergence,
  with a 200 ceiling it never approaches and a queue that is always empty.
- Its worst observed failure was peers vanishing, not pieces mis-ordered.
- Its tracker list is a fixed spray, half of it inert.

Every one of those is a lever our own libtorrent can pull: PEX is one line, uTP is a setting,
DHT can be checked and fixed independently, and tracker policy is ours to choose. **Whether
pulling them actually yields more connected peers is Slice 6's job, and it is now the question
the whole verdict turns on.**

## Layer matrix

```
Qt Test:                 not applicable — Phase 0 ships no app code
Qt Quick Test:           not applicable — no QML
Existing harnesses:      not applicable — live-network work, never in a deterministic gate
Lanista:                 not applicable — playback driven by direct HTTP, never the app
Human aesthetic verdict: not applicable — no surface
Overall:                 Runtime-validated (discovery findings); throughput WITHHELD pending a quiet machine
```

## Owed to A4's lane (unchanged, and now stronger)

The player could show **"found 172 · connected 21"** today, with zero engine work, by reading
`unique` and `sources` from a response Colosseum already fetches every second. On tonight's
evidence that line would frequently be the honest explanation for a slow start.
