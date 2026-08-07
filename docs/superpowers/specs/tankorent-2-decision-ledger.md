# Tankorent 2.0 — Arc Decision Ledger

- **Arc:** TANKORENT_2_CHALLENGER · **Opened:** 2026-08-02 · **Last updated:** 2026-08-07
- **Lane:** Agent 4 (Player / Theatre)
- **Why this exists:** decisions were scattered across seven files (spec, plan, four evidence
  docs, one idea note) with no single place to check what is settled. Hemanth asked for the
  ledger on 2026-08-07; this is it. **It is the arc's source of truth for what is decided** —
  the other documents hold the reasoning and evidence.

States are the house four — **Locked · Constraints · Deferred · Open** — plus a
**Falsified** register, which the spec (§9, cross-cutting) requires so no negative result is
ever re-tried blind.

Source documents:
`specs/2026-08-02-tankorent-2-challenger-engine-design.md` ·
`plans/2026-08-07-tankorent-2-phase0-rosetta-dig.md` ·
`ideas/2026-08-07-source-race-and-swarm-merge-ladder.md` ·
`research/tankorent2-phase0/{00,01,02,04}-*.md`

---

## LOCKED — Hemanth approved

| # | Decision | When |
|---|---|---|
| L1 | Build a challenger streaming engine, but it must **win on measured evidence** before it touches a real evening | 2026-08-02 |
| L2 | The chassis is **libtorrent** (we own it; it ships his books daily), not rqbit | 2026-08-02 |
| L3 | The challenger speaks **Stremio's exact dialect** (URL shapes, `stats.json` fields, `/settings`) on a **different port** — so champion↔challenger is a port swap and races are automatically fair | 2026-08-02 |
| L4 | Race visibility is **in-app** ("the challenger rides shotgun"), not reports-only | 2026-08-02 |
| L5 | Races pull **real data** — shadow measurement is theatre | 2026-08-02 |
| L6 | Normal nights pull the film **once**; trial nights pull **twice**, always his explicit per-session choice | 2026-08-02 |
| L7 | **Promotion** (actually replacing Stremio) is a **separate future gate**, never part of this arc | 2026-08-02 |
| L8 | **Phase 0 is a STOP/GO feasibility gate**, not merely a behavior map — prove or disprove that a challenger could beat Stremio before investing a day in building one | **2026-08-07** |
| L9 | An **ambiguous result is a STOP.** The burden of proof sits on the challenger | **2026-08-07** |
| L10 | **rqbit is a reference, never a racer.** Read for a lever checklist only; Stremio is the sole competitor | **2026-08-07** |
| L11 | The **source-race ladder** is recorded as a standing idea, **not scheduled** | **2026-08-07** |

## CONSTRAINTS — boundaries every later choice must respect

| # | Constraint | Origin |
|---|---|---|
| C1 | **Production movie night is never at risk.** Nothing this arc does may degrade what he watches on | spec |
| C2 | **April's falsified list is law** — no re-testing `request_queue_time 10→3` or disabling `setSequentialDownload` without new evidence explaining why our conditions differ | spec / `feedback_stream_failed_hypotheses.md` |
| C3 | **IPv6 stays off the video path** until the ISP-IPv6 scar is deliberately re-examined | spec |
| C4 | **No app code ships in Phase 0.** Not one line | plan |
| C5 | **Instrument before claiming.** Every verdict claim traces to a logged observation or measured number; source-reading alone got the wrong answer twice in April | spec / house |
| C6 | **Every race is fair-start** — identical magnet, simultaneous, same machine | spec |
| C7 | **Findings reach Hemanth as documents.** Teardown and instrumentation are the boys' chore | spec |
| C8 | **Measure only on a quiet machine.** No build, no IDE, nothing else transferring | house law |
| C9 | **The lab never binds port 11470.** Colosseum adopt-firsts that port, so a lab engine there would capture real playback. Lab runs a hash-verified frozen copy on 11480 with its own `APP_PATH` | **Slice 0, 2026-08-07** |
| C10 | **Never kill or pause Hemanth's qBittorrent without asking.** It is his client with his downloads; it confounds throughput measurement, and the correct response is to ask, not to work around it | **Slice 4, 2026-08-07** |
| C11 | **Never set `COLOSSEUM_STREAM_SERVER` in his environment** during this arc | plan |

## DEFERRED — intentionally out, not dead

| # | Item | Why deferred |
|---|---|---|
| D1 | **Path-3 / HTTP-prefer streaming** | belongs to the vidking hosted-player arc; Tankorent 2.0 is pure BitTorrent |
| D2 | **IPv6 re-examination** for the video path | scar stands until deliberately revisited — though Slice 4's dead-DHT measurement strengthens the case for revisiting |
| D3 | **Promotion / replacing Stremio** | separate future gate (L7) |
| D4 | **PEX + reachability upgrades on the DOWNLOAD engine** (comics, manga, books) | a cheap win, independent of this arc's verdict — takeable any time |
| D5 | **The source-race ladder** — merge duplicate swarms → PEX → race slow chunks across peers → race torrents at startup → verified-identical multi-source | recorded, unscheduled (L11). **Rungs 1, 4, 5 survive a STOP verdict** — they are source selection, above the engine |
| D6 | **Fix `streamserver.cpp:256-264`'s stale stock-caps comment** (says 35 / 1.6 / 2.5; actual 55 / 2.5 / 3.5) | owed to A4's lane; Phase 0 ships no app code (C4) |
| D7 | **Show "found N · connected M" in the player** | owed to A4's lane. Needs **zero** engine work — `unique` and `sources` are already in a response Colosseum fetches every second |

## OPEN — unresolved

| # | Question | Owner / next step |
|---|---|---|
| O1 | **Does pulling the levers actually convert into more connected peers?** | **Slice 6.** This is now the question the entire verdict turns on |
| O2 | **Throughput numbers** — all withheld; qBittorrent was transferring during Slice 4 | **Slice 5, needs a quiet machine.** Blocked on Hemanth pausing qBittorrent |
| O3 | **Cold-open time** — never measured; reported first-byte figures were warm starts | Slice 5, same quiet-machine gate |
| O4 | **Q2/Q3 — cold-open piece window and seek behavior** | not determinable statically; needs `select`/`critical` call logging on the instrumented copy |
| O5 | **What consumes `btMinPeersForStable = 5`?** | definition found, consumer not traced. **Must not be cited as a cause until it is** |
| O6 | **Why is DHT returning zero?** | measured dead across 3 torrents; the cause (IPv6 scar? bootstrap? firewall?) is undiagnosed |
| O7 | **Is the Slice 4 stall shape typical?** | one timestamped observation is not a law; Slice 5 must establish frequency |
| O8 | **Slice 3 lever inventory** | not yet executed — the only Phase-0 slice still untouched |

## FALSIFIED — negative results, so nothing is re-tried blind

Required by spec §9. Each entry is a hypothesis that was **tested and killed**, with what
replaced it.

| # | Hypothesis | Outcome | Where |
|---|---|---|---|
| F1 | `request_queue_time 10→3` fixes stream stalls | **FALSIFIED 2026-04-19** — cold-open regressed 11.5 s → >109 s. The 2 s cap was never firing (`avg_q_ms=163`) | April ledger |
| F2 | `setSequentialDownload` interferes with time-critical selection | **FALSIFIED 2026-04-19** — cold-open regressed 11.5 s → 32 s. Sequential is innocent AND helps | April ledger |
| F3 | Stock `btMaxConnections` is 35 (per our own code comment) | **FALSIFIED** — it is **55**; soft/hard are 2.5 / 3.5 MB/s, not 1.6 / 2.5 | Slice 0 |
| F4 | The lab is safe if it checks that port 11470 is free | **FALSIFIED — dangerously inverted.** A *free* 11470 is the hazard: the lab would bind it and Colosseum would adopt the lab engine on the next Play | Slice 0 |
| F5 | `stats.json`'s `peers` may count *known* peers, making the low number a display artifact (**the cheap STOP**) | **FALSIFIED** — it is `swarm.wires.length`, live connections. The deficit is real | Slice 2 |
| F6 | `ut_pex`'s absence is only a weak signal | **SUPERSEDED by a finding** — the outgoing handshake advertises exactly `{ m: { ut_metadata: 1 } }`, no PEX implementation exists in any of 1,310 modules, and `peerSearch` accepts only `dht:`/`tracker:` | Slice 2 |
| F7 | The connection ceiling is what limits peers | **FALSIFIED** — with a 200 cap the engine peaked at 39 connected and **never queued a single peer**. Raising 55→200 moved peak only 22→39 | Slice 4 |
| F8 | Peer *discovery* is the bottleneck | **FALSIFIED** — 189 discovered, 172 tracked, 728 connection attempts, 39 held. **Conversion** is the bottleneck, i.e. reachability | Slice 4 |
| F9 | Stalls are a piece-scheduler problem (April's working assumption) | **COUNTER-EXAMPLE FOUND** — the captured stall was a peer-population collapse (35→16 connected, 26→7 unchoked) with no scheduling change. Not yet generalised (see O7) | Slice 4 |

## Standing evidence — findings that are settled

Not decisions, but facts later decisions may rely on without re-deriving:

- Stremio server **4.20.17**; bundle is webpack, **1,310 modules**, split round-trip byte-exact.
- Engine stack: 820 swarm → 816 torrent-stream → 613 peerSearch → 172 EngineFS → 564 core.
- **The entire BitTorrent surface is six knobs** (`btMaxConnections`, `btHandshakeTimeout`,
  `btRequestTimeout`, soft/hard speed limits, `btMinPeersForStable`). No read-ahead, no window,
  no tracker policy exists to tune.
- **No PEX** (structural) and **DHT returns zero** (measured, 3 torrents). Trackers are the
  sole peer supply.
- **uTP disabled** (`utp: !1`) — TCP only.
- **Tracker list is hardcoded** (module 806, 20 entries) and used regardless of caller input;
  **9 of 20 returned zero peers**.
- `stats.json` already exposes `unique`, `sources`, `queued`, `connectionTries`, `swarmSize`,
  `swarmConnections`, `peerSearchRunning`, `wires[]` — none read by Colosseum.

## Phase status

| Phase | State |
|---|---|
| 0 — Rosetta dig (STOP/GO gate) | **IN PROGRESS** — Slices 0, 1, 2, 4 Runtime-validated; Slice 3 not started; Slices 5, 6, 7 blocked or pending |
| 1 — Engine skeleton + dialect | **NOT AUTHORIZED** — requires a GO in Slice 7 |
| 2 — The Derby | not started |
| 3 — Shotgun trials | not started |
| 4 — Promotion gate | separate arc (L7) |

**Nothing in this ledger authorizes Phase 1.** Only a GO verdict in Slice 7 does.
