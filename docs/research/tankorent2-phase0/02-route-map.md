# Phase 0 · Slice 2 — The routes traced inward

- **Executed:** 2026-08-07 · **Status:** Runtime-validated (static slice; prediction-check control performed)
- **Source:** `_t2lab/unpacked/modules/` (1,310 modules, round-trip verified in Slice 1)
- **This is the plan's renewal checkpoint.** It completed inside the static box.

Every claim below cites a module id. Module ids are array positions in the bundle and are
trustworthy only because Slice 1's round-trip check passed.

## The four public routes

All four mount in **module 172** (the EngineFS router), reached from the server core
(module 564, the bundle's entry module) which owns the listen block and CORS:

| Route | Handler | Inward path |
|---|---|---|
| `GET /:infoHash/:idx/stats.json` | 172 | → `getStatistics(engines[ih], idx)` → `e.swarm` (820) |
| `GET /:infoHash/stats.json` | 172 | → `getStatistics(engines[ih])` |
| `GET /stats.json` | 172 | all engines; `?sys=1` adds `os.loadavg()`/`os.cpus()` |
| `ALL /:infoHash/create` | 172 | → `createEngine(ih, body, cb)`; body may carry `fileMustInclude` (string or `/regex/`) |
| `POST /settings` | 564 → 105 | the settings object; persists to `server-settings.json` |

Engine stack, bottom to top: **820** `bittorrent-swarm` (wires, connections, choking) →
**816** `torrent-stream` (the engine: selections, critical pieces, files) → **613**
`peerSearch` (discovery) → **172** EngineFS (routes, stats, HTTP) → **564** server core.
Discovery libraries: **614** DHT, **625** Tracker, **845** `ut_metadata`, **500**
`parse-torrent`.

## Q1 — `getDefaults` and the COMPLETE knob list (module 105)

Settings live in `server-settings.json` under `SETTINGS_PATH || appPath`, loaded at boot,
re-saved on load, extended by `POST /settings`. Every BitTorrent knob the engine has:

| Knob | Default | Meaning |
|---|---|---|
| `btMaxConnections` | **55** | peer connection ceiling (app overrides to 200) |
| `btHandshakeTimeout` | **20000 ms** | how long a peer may take to complete handshake |
| `btRequestTimeout` | **4000 ms** | how long a block request may hang (accepts legacy `btConnectionTimeout`) |
| `btDownloadSpeedSoftLimit` | **2,621,440** (2.5 MB/s) | soft throttle (app overrides to 20 MB/s) |
| `btDownloadSpeedHardLimit` | **3,670,016** (3.5 MB/s) | hard throttle (app overrides to 40 MB/s) |
| `btMinPeersForStable` | **5** | peers considered "stable enough" — see below |
| `cacheSize` | 2 GB | `0` if `DISABLE_CACHING` env set, or on android |
| `cacheRoot` / `appPath` | appPath | cache location |

**That is the entire BitTorrent surface — six knobs.** There is no read-ahead size, no
cold-open window, no piece-timeout, no tracker-policy, and no discovery setting. Anything we
would want to tune beyond these six is not exposed at all.

Two env knobs beyond the port finding: `SETTINGS_PATH` and `DISABLE_CACHING`.

## Q4 — how peers are discovered (module 613, `peerSearch`)

The discovery layer accepts exactly **two** source prefixes:

```js
src.match("^dht:")     ? new DHT(src.split(":")[1], options)
: src.match("^tracker:") ? new Tracker(src.slice("tracker:".length), {}, swarm.infoHash)
: void 0                                   // ← anything else is dropped
```

Non-matching sources become `undefined` and are filtered out. **DHT and trackers only. No
peer-exchange. No local peer discovery.**

Each source carries counters — `numFound`, `numFoundUniq`, `numRequests`, `lastStarted` —
deduplicated against a shared `uniq` map before `swarm.add(addr)`.

## THE PEX QUESTION — now a FINDING, no longer a signal

Slice 0 recorded `ut_pex` string-count 0 and explicitly refused to call it a finding, because
absence of a string in a packed bundle is weak evidence. With the bundle split, the evidence
is now structural and positive rather than an absence:

1. **The outgoing extended handshake is enumerable, and there is exactly one.** Module 845 is
   the only extension implementation in the bundle, required by only module 816, and the sole
   handshake it sends is:
   ```js
   wire.peerExtensions.extended && wire.extended(0, metadata
     ? { m: { ut_metadata: 1 }, metadata_size: metadata.length }
     : { m: { ut_metadata: 1 } });
   ```
   `m` is the BitTorrent extension-advertisement map. It declares `ut_metadata` and nothing
   else — so peers are never told this client speaks PEX, and therefore never send it PEX
   messages.
2. **No PEX implementation exists** anywhere in all 1,310 modules (searched `ut_pex`, `pex`,
   `PEX` case-insensitively).
3. **The discovery layer structurally cannot accept it** — `peerSearch` takes `dht:` and
   `tracker:` sources only.

**Conclusion: Stremio's engine cannot learn about a peer from another peer.** Every peer it
ever contacts came from a tracker announce or a DHT lookup. Slice 4 still confirms this live
per the plan (the negative control on peer-source attribution stands), but the static case is
now closed and cited.

## Q5 — what `peers` actually counts (module 172, `getStatistics`) — THE CHEAP STOP DID NOT MATERIALISE

This was the plan's cheapest possible early exit: if `peers` counted *known* rather than
*connected* peers, "200 seeders reads as single digits" would be partly a display artifact and
the verdict would lean STOP. It is not.

```js
peers:            e.swarm.wires.length,
unchoked:         e.swarm.wires.filter(p => !p.peerChoking).length,
queued:           e.swarm.queued,
unique:           Object.keys(e.swarm._peers).length,
connectionTries:  e.swarm.tries,
swarmConnections: e.swarm.connections.length,
swarmSize:        e.swarm.size,
```

`wires` are **live BitTorrent wire-protocol connections** — peers with a completed handshake.
So `peers` is genuinely *connected* peers, and `unchoked` is the subset actually willing to
send data. **The single-digit number Hemanth sees is a real connection deficit, not a
mislabeled counter.** The hypothesis is falsified; the arc continues.

### Bonus that changes Slice 4's cost — the engine already reports the discovery telemetry

Colosseum reads six fields (`streamserver.cpp:330-335`): `peers`, `unchoked`, `downloaded`,
`downloadSpeed`, `streamProgress`, `streamLen`. `getStatistics` already returns, unused:

| Field | What it gives us free |
|---|---|
| `unique` | **known** peers — pair with `peers` and you get discovered-vs-connected directly |
| `swarmSize` / `swarmConnections` / `queued` / `connectionTries` | the funnel from known → attempted → connected |
| `sources` | **per-source discovery stats** (`numFound`, `numFoundUniq`, `numRequests` per DHT/tracker) |
| `peerSearchRunning` | whether discovery is even active |
| `wires[]` | per-peer `address`, `downSpeed`, `upSpeed`, `isSeeder`, `requests` |

**Slice 4's instrumentation is therefore much cheaper than planned.** The known-vs-connected
question and the per-source attribution — including the negative control the plan mandated for
peer-source attribution — can be read straight off `stats.json` without patching the engine at
all. Slice 4 should be re-scoped to instrument only what these fields cannot answer (piece
priorities, cache hits, byte-range reads).

**Specimen defect noted in passing:** `uploadSpeed: e.swarm.downloadSpeed()` — upload speed
reports the download figure. An upstream Stremio bug. Harmless to us (we never read it), but it
is a caution about trusting any single field without checking its source.

## New lever found — uTP is disabled

Module 816 constructs the swarm with transport explicitly off:

```js
swarm = pws(infoHash, opts.id, {
  size: opts.connections || opts.size,
  handshakeTimeout: opts.handshakeTimeout,
  utp: !1                     // ← uTP disabled: TCP only
});
```

uTP is the UDP-based BitTorrent transport. With it off, any peer reachable only over uTP is
unreachable, and NAT traversal loses a path that often succeeds where TCP does not. This joins
PEX on the lever list and goes into Slice 3's inventory. Our own libtorrent supports uTP.

Other engine construction facts: `rechokeSlots` default **5** upload slots, `opts.flood = 0`,
`opts.pulse = Number.MAX_SAFE_INTEGER`.

## Q2 / Q3 — cold-open window and seek behavior: mechanism found, values DEFERRED to Slice 4

The mechanism is identified (module 816):

```js
engine.critical  = function(piece, width) { for (…) critical[piece + i] = !0; };
engine.select    = function(from, to, priority, notify) {
    engine.selection.push({from, to, offset: 0, priority: toNumber(priority), notify});
    engine.selection.sort((a, b) => b.priority - a.priority);   // highest priority first
};
engine.deselect  = …   // file streams deselect on end-of-stream
```

Piece scheduling is **selection ranges sorted by priority, plus a `critical` bitmap** for
must-have-now pieces. `file.createReadStream` creates a selection and deselects it when the
stream ends. Notably, the words `sequential`, `readahead`, and `pick` appear **nowhere** in
this module — there is no named read-ahead window constant to read off.

**Therefore the concrete answers to Q2 (which pieces are prioritised on fresh play, and how
wide the head window is) and Q3 (what happens to priorities on seek) are NOT determinable
statically** — they are emergent from how the HTTP range handler drives `select`/`critical`
per request. This is the plan's sanctioned "deferred to Slice 4 with a reason" outcome, and
Slice 4 must log `select`/`deselect`/`critical` calls with their piece ranges to answer them.

## Negative control — prediction, then check

The plan required predicting observable values before checking, and recording failures as
corrections rather than editing them away. Two predictions were made and **both were wrong**:

| Prediction | Outcome |
|---|---|
| Stock `btMaxConnections` is 35 (carried from Colosseum's code comment) | **FALSIFIED** — 55. Recorded in Slice 0. |
| `stats.json`'s `peers` may count *known* peers, making the low number partly a display artifact (the cheap-STOP hypothesis) | **FALSIFIED** — it counts live wires. The deficit is real. |

Two for two against the standing assumptions. This is the control working: had either gone
unchecked, the verdict would have been built on a false premise.

## Where this leaves the verdict

Nothing here is a GO — no measurement has happened yet. But the cheap STOP is gone, and three
named, detectable levers are now on the board with citations:

1. **No peer-exchange** — cannot learn peers from peers (structural, cited).
2. **uTP disabled** — TCP-only, loses a reachability path (cited).
3. **`btMinPeersForStable = 5`** — a threshold expressed in peers, sitting suspiciously close
   to the observed symptom. Its *consumer* has not yet been traced; that is outstanding work
   for Slice 4, and it must not be cited as a cause until it is.

## Layer matrix

```
Qt Test:                 not applicable — Phase 0 ships no app code
Qt Quick Test:           not applicable — no QML
Existing harnesses:      not applicable — static analysis
Lanista:                 not applicable — no app UI
Human aesthetic verdict: not applicable — no surface
Overall:                 Runtime-validated
```

## Owed to A4's lane

Two app-side notes this plan may not fix (Phase 0 ships no code):
1. `streamserver.cpp:256-264`'s stock-caps comment is stale (35/1.6/2.5 → 55/2.5/3.5).
2. The player's statistics panel could show discovered-vs-connected peers **today**, with no
   engine work, by reading `unique` and `sources` from a response it already fetches.
