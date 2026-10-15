# Phase 0 · Slice 1 — The bundle, split into readable modules

- **Executed:** 2026-08-07 · **Plan:** `docs/superpowers/plans/2026-08-07-tankorent-2-phase0-rosetta-dig.md`
- **Status:** Runtime-validated (research slice — both mandated negative controls green)
- **Input:** `_t2lab/specimen/server.js` (pristine, sha256 `567a397b…`, server 4.20.17)
- **Output:** `_t2lab/unpacked/` (local) — `modules/<id>.js` ×1310, `index.json`, `_prologue.txt`, `_epilogue.txt`
- **Tool:** `docs/research/tankorent2-phase0/labscripts/split_bundle.py`

## Bundle shape

```
!(function(modules){ …bootstrap… })([ m0, m1, … m1309 ]);
```

A **positional array**, not an id-keyed object. Consequences that shaped the tooling:

- **Module id == array index.** There are **zero** `/* id */` marker comments in this bundle
  (checked: 0 matches), so ids exist only as position. A splitter that dropped or merged one
  element would silently renumber every module after it — and Slice 2's route map, which cites
  modules by id, would be quietly wrong. That is why the round-trip check below is load-bearing
  rather than ceremony.
- Prologue (bootstrap) 2,197 bytes; epilogue 3 bytes (`]);`).
- **Entry module: `564`** — from the bootstrap's `__webpack_require__.s = 564`.

The splitter walks the array with a JS-aware scanner (string, template-literal with `${}`
substitution, line/block comment, and regex-literal handling via the standard
previous-significant-token heuristic) counting `()[]{}` depth, and cuts at depth-0 commas.

## Result

| Fact | Value |
|---|---|
| Modules extracted | **1,310** |
| Empty slots | 0 |
| Round-trip | **byte-exact** |
| Split runtime | ~10 s |

## Negative controls (both required by the plan; both green)

**1 — Round-trip equivalence.** Re-joining the 1,310 elements with commas and re-attaching
prologue + epilogue reproduces the input **byte-for-byte**. The tool exits nonzero and reports
the first divergent byte offset if it does not, so a lossy split cannot pass silently.

**2 — Fingerprint conservation.** Every fingerprint occurrence counted in the original file is
still present, at the same count, across the split tree:

| String | Original | Split | |
|---|---|---|---|
| `torrent-stream` | 3 | 3 | ok |
| `bittorrent-tracker` | 7 | 7 | ok |
| `bittorrent-dht` | 1 | 1 | ok |
| `parse-torrent` | 1 | 1 | ok |
| `ut_metadata` | 6 | 6 | ok |
| `webtorrent` | 1 | 1 | ok |
| `ut_pex` | 0 | 0 | ok |
| `11470` | 6 | 6 | ok |

`ut_pex` remains at zero in both — consistent with the standing signal, still **not** a finding.
Only Slice 4's live observation settles it.

## The ten largest modules

| id | bytes | what it looks like |
|---|---|---|
| 603 | 219,003 | MIME-type table (`application/1d-interleaved-…`) — data, not logic |
| 719 | 210,182 | `start, log_level, log…` — large logic module |
| 1129 | 129,278 | public-suffix list (`"ac","com.ac",…`) — data |
| 1002 | 107,211 | wrapped module |
| 1 | 79,641 | wrapped module |
| 638 | 78,663 | wrapped module |
| 480 | 75,656 | `module.exports = (functio…` |
| 12 | 69,901 | `"use strict"; var byE…` |
| 13 | 69,901 | byte-identical twin of 12 (duplicate dependency copy) |
| 859 | 65,775 | wrapped module |

Two of the top three are pure data tables; raw size is a poor guide to importance here. The
offset-ownership and fingerprint maps below are the useful ones.

## Where the known byte offsets live

Every 11470 site found in Slice 0, resolved to its owning module:

| Site | Offset | Module |
|---|---|---|
| EngineFS listen block | 2,643,954 | **564** |
| CORS origin check | 2,629,375 | **564** |
| hls-converter `serverPort` | 4,084,131 | 807 |
| subtitles fetch | 4,778,643 | 944 |
| local-addon `engineUrl` | 5,250,283 | 1024 |
| usenet port default | 5,454,067 | 1088 |

## Fingerprint and route strings, by module

| String | Modules (count) |
|---|---|
| `EngineFS server started` | **564** (1) |
| `stats.json` | **172** (3) |
| `streamProgress` | **172** (1) |
| `swarm` | **172** (34), 820 (24), 653 (22), 816 (19), 613 (12), 657 (4), 564 (4) |
| `btMaxConnections` | **105** (3), 564 (2) |
| `btMinPeersForStable` | **105** (3), 564 (2) |
| `torrent-stream` | 412 (2), 816 (1) |
| `bittorrent-tracker` | 627, 631, 637, 639, 653, 657, 806 (1 each) |
| `bittorrent-dht` | 289 (1) |
| `parse-torrent` | 500 (1) |
| `ut_metadata` | 845 (6) |
| `webtorrent` | 1057 (1) |

## Reading of the map — the targets Slice 2 should open first

- **564 — the server core and the entry module.** Owns the listen block, CORS, and reads the
  bt settings. Where the four public routes are mounted.
- **172 — the stream/stats engine.** The only module carrying `stats.json` *and*
  `streamProgress`, and the heaviest `swarm` user (34). **This is where `stats.json`'s `peers`
  number is produced**, which is Slice 2's question 5 and the cheapest possible early STOP
  signal: if `peers` counts *known* rather than *connected* peers, "200 seeders → 8 peers" is
  partly a display artifact rather than a discovery failure.
- **105 — the settings/defaults module.** Holds `btMaxConnections` and `btMinPeersForStable`;
  the likely home of `getDefaults` and therefore Slice 2's question 1 (the complete knob list).
- **Peer/swarm cluster — 820, 653, 816, 613, 657**, plus the `torrent-stream` sites (412, 816)
  and the discovery libraries (289 DHT, 806/627/631/637/639/653/657 tracker, 845 metadata).
  This cluster is where the peer-exchange question is settled statically, before Slice 4
  settles it live.

## Layer matrix

```
Qt Test:                not applicable — Phase 0 ships no app code
Qt Quick Test:          not applicable — no QML
Existing harnesses:     not applicable — adds nothing to the build
Lanista:                not applicable — no app UI
Human aesthetic verdict: not applicable — no surface
Overall:                Runtime-validated
```

Nothing was executed and nothing was modified in this slice — the splitter reads the pristine
copy and writes elsewhere, so the specimen's hash is unchanged by construction.
