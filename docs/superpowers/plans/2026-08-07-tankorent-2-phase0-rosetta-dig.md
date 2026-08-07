# Tankorent 2.0 — Phase 0: The Rosetta Dig (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: use `brotherhood-executing-plans` to execute
> this plan slice by slice. Steps use checkbox (`- [ ]`) syntax for tracking.

- **Date:** 2026-08-07 · **Arc:** TANKORENT_2_CHALLENGER · **Phase:** 0 of 4
- **Spec:** `docs/superpowers/specs/2026-08-02-tankorent-2-challenger-engine-design.md`
- **Lane:** Agent 4 (Player / Theatre). All app-side touches declared on `agents/chat.md`
  before landing — this plan orders none.
- **Repos:** this plan + all Phase-0 artifacts commit to the **Colosseum** repo. The lane
  declaration goes to the **Brotherhood** repo (`agents/chat.md`).

---

## Goal (amended by Hemanth, 2026-08-07)

**Prove or disprove that a Tankorent 2.0 could beat the Stremio server — before we invest a
day in building one.**

The spec's Phase 0 produced a behavior spec. Hemanth's correction promotes it: the dig must
**end in a STOP or GO verdict backed by measurement**, not merely a map. The behavior spec
is now a *means* — the thing that tells us where to look for headroom — and the verdict is
the deliverable.

**Second amendment (same conversation): rqbit is a reference, not a racer.** Harbor's engine
is read for one bounded purpose — a checklist of levers a good streaming engine pulls, so we
don't grade Stremio only against our own defaults and miss a trick neither pulls. It is not
built, not run, not raced. Stremio is the sole competitor.

## What the verdict looks like

One page, three possible outcomes, decided by numbers and not by anyone's word:

| Evidence | Verdict |
|---|---|
| Stremio is already near the swarm's physical ceiling; no lever it skips changes the numbers materially | **STOP.** Keep Stremio. Arc closes having spent one time-box. |
| Named levers are unpulled AND our libtorrent chassis measurably reaches further on the same swarm | **GO.** Phase 1 is justified; the behavior spec feeds it. |
| Levers exist but our chassis cannot capture them | **STOP / INVESTIGATE.** Record why; do not build on hope. |

An ambiguous result is a STOP, not a GO. The burden of proof is on the challenger.

## Ground truth already verified (do not re-derive)

Verified on disk 2026-08-07 by read-only inspection:

- **Specimen is present and needs no procurement:**
  `C:/Users/Suprabha/AppData/Local/Programs/StremioService/` — `server.js` (6,631,104 bytes,
  112,133 lines, dated 2026-04-14), `stremio-runtime.exe` (65 MB, the Node runtime),
  `ffmpeg.exe`/`ffprobe.exe` + codec DLLs, `LICENSE.md` (2024-03-11).
- **Bundle shape:** webpack, non-minified-identifier style, `__webpack_require__` ×4616,
  bootstrap `!(function(modules){ var installedModules = {} ...` at byte 0. A module table
  bundle — splittable, not obfuscated. **No sourcemap** (`sourceMappingURL` absent).
- **Engine family fingerprint (text-mode grep, counts):** `torrent-stream` 3,
  `bittorrent-tracker` 7, `bittorrent-dht` 1, `parse-torrent` 1, `ut_metadata` 6,
  `webtorrent` 1, `trackers` 12, `magnet:` 7, `readahead` 1. The `torrent-stream` lineage
  the spec predicted is confirmed present.
- **`ut_pex` count is 0, `peer-wire` count is 0** — the lead hypothesis (no peer-exchange).
  **Treat as a signal, NOT a finding**: absence in a packed bundle is weaker evidence than
  presence, and only Slice 4's live observation can confirm it.
- **Live surface confirmed in-bundle:** `stats.json` 3, `settings` 130, `infoHash` 268,
  `peers` 94, `downloadSpeed` 15, `streamProgress` 1.
- **Colosseum's seam (`native/player/streamserver.cpp`, 340 lines):** adopt-first probe on
  `http://127.0.0.1:11470/settings`; on success adopts, else spawns
  `stremio-runtime.exe server.js` with `NO_HTTPS_SERVER=1` and `APP_PATH=<CacheLocation>/colosseum-stream`;
  scrapes the port from stdout line `EngineFS server started at http://127.0.0.1:<port>`;
  POSTs `/settings` with `btMaxConnections:200`, `btDownloadSpeedSoftLimit:20971520`,
  `btDownloadSpeedHardLimit:41943040`; polls `/:hash/:idx/stats.json` at 1 Hz reading
  `peers`, `unchoked`, `downloaded`, `downloadSpeed`, `streamProgress`, `streamLen`.
- **`COLOSSEUM_STREAM_SERVER` env var exists** and is the FIRST candidate in
  `findRuntimeDir()` — the sanctioned way to point a run at a different runtime dir without
  editing code.
- **Stock caps the app overrides** — ~~per the in-code comment: 35 connections, 1.6 / 2.5 MB/s~~
  **CORRECTED by Slice 0 (2026-08-07), read live off the specimen (server 4.20.17):
  `btMaxConnections` 55, soft limit 2,621,440 (2.5 MB/s), hard limit 3,670,016 (3.5 MB/s).**
  The `streamserver.cpp:256-264` comment describes an older server and is stale; a comment fix
  is owed to A4's lane. Slice 0 also surfaced three knobs the comment never mentioned:
  `btMinPeersForStable` **5** (a peers-count threshold — a direct candidate for the
  "200 seeders reads as single digits" thread), `btHandshakeTimeout` 20000 ms,
  `btRequestTimeout` 4000 ms. Evidence: `docs/research/tankorent2-phase0/00-specimen.md`.
- **There is NO port environment knob** (Slice 0, verified by enumerating every `process.env`
  read in the bundle). The listen port is the literal `port = 11470` with an increment-on-error
  fallback to 11474. See the amended isolation contract below.

## Laws this plan operates under

1. **April's falsified list is law** — `feedback_stream_failed_hypotheses.md`. Two scheduler
   fixes were shipped, smoked, regressed, reverted on 2026-04-19:
   `request_queue_time 10→3` (cold-open regressed 11.5 s → >109 s) and disabling
   `setSequentialDownload` (cold-open regressed 11.5 s → 32 s; sequential is innocent AND
   helps). **No slice here re-tests either.** The ledger's three hard-won rules apply:
   telemetry is ground truth; smoke immediately after any change; never claim a tune worked
   without a diagnostic event proving it activated.
2. **Production movie night is never at risk.** See the isolation contract below.
3. **No app code changes in Phase 0.** Not one line of Colosseum ships from this plan. If a
   slice appears to need one, stop and re-plan.
4. **Instrument before claiming.** Every claim in the verdict traces to a logged observation
   or a measured number. Source-reading alone produced the wrong answer twice in April.

## Isolation contract (the safety spine — read before Slice 0)

**The trap, found while planning:** Colosseum adopts whatever already answers on
`127.0.0.1:11470`. The official Stremio Service normally owns that port. If the dig
instruments *that* process, or binds its own server to that port, then a dig session running
while Hemanth watches a film either (a) hijacks his playback onto an instrumented engine, or
(b) collides on the port and silently breaks streaming — the exact 2026-07-05 failure class.

Therefore, absolutely:

- The dig runs a **frozen private copy** of the specimen in a lab directory, **never** the
  installed one, and **never** by attaching to the running service.
- The lab server binds a **non-11470 port** and is launched with its own `APP_PATH` cache
  dir, so it shares no state with the app's engine or the official service.
- The dig **never sets `COLOSSEUM_STREAM_SERVER`** in Hemanth's environment. It is recorded
  here only as the documented alternative if a future phase needs the app pointed at a
  challenger; Phase 0 does not use it.
- Before any live slice, the executor confirms no film is playing and records whether the
  official service is up — the lab must coexist, not displace.
- All artifacts land under a lab dir; nothing is written into the StremioService install.

## Time-box (explicit, per spec §4)

| Stage | Slices | Box |
|---|---|---|
| Static dig | 0–3 | 3 working sessions |
| Live measurement | 4–6 | 3 working sessions |
| Verdict | 7 | 1 working session |

**Renewal checkpoint after Slice 2.** If the bundle has not yielded a readable module map
and a traced route by the end of the static box, the executor **stops and reports** — the
box is then explicitly renewed by Hemanth or the arc is parked. Every finding is kept
either way. No silent overrun.

## Artifact layout

All under `Colosseum/docs/research/tankorent2-phase0/` (committed) except the frozen
specimen and run logs, which are large and stay local:

| Path | Committed? | Contents |
|---|---|---|
| `docs/research/tankorent2-phase0/00-specimen.md` | yes | specimen pin: hashes, sizes, versions |
| `docs/research/tankorent2-phase0/01-module-map.md` | yes | bundle structure, module table, entry points |
| `docs/research/tankorent2-phase0/02-route-map.md` | yes | the four routes traced inward |
| `docs/research/tankorent2-phase0/03-lever-inventory.md` | yes | Harbor-derived lever checklist + Stremio's score |
| `docs/research/tankorent2-phase0/04-instrumentation.md` | yes | what was instrumented, how, and the event vocabulary |
| `docs/research/tankorent2-phase0/05-battery-results.md` | yes | the experiment battery numbers |
| `docs/research/tankorent2-phase0/06-chassis-probe.md` | yes | libtorrent counter-probe results |
| `docs/research/tankorent2-phase0/VERDICT.md` | yes | **the deliverable** — behavior spec + STOP/GO |
| `native/build-msvc/_t2lab/` | no (gitignored) | frozen specimen, cache, run logs, probe exe |

---

## Slice 0: A frozen specimen in an isolated lab that cannot touch movie night

**Purpose:** establish a private, version-pinned copy of the Stremio server the dig can
poke freely, with a hard guarantee it can never disturb Hemanth's playback.

**Dependencies:** none.

**Implementation guidance:** create `native/build-msvc/_t2lab/specimen/` and copy
`server.js`, `stremio-runtime.exe`, the ffmpeg binaries and codec DLLs from the
StremioService install (copy, never symlink, never move — the install stays pristine).
Record SHA-256 of `server.js` and `stremio-runtime.exe` plus file sizes and timestamps in
`00-specimen.md`. Write a launcher script `_t2lab/run-specimen.sh` that starts the runtime
with `APP_PATH=_t2lab/cache`, `NO_HTTPS_SERVER=1`, `NODE_OPTIONS` removed (mirroring
`streamserver.cpp:111-115`), and forces a **non-11470 port**.

> **AMENDED 2026-08-07 during execution — the original fallback was unsafe.** This slice
> originally said: "if no port knob exists … the launcher instead asserts 11470 is free before
> starting, refusing to run if it is not." **No port knob exists** (verified: no `process.env`
> read in the bundle influences the port), and that fallback has the danger inverted — the
> hazard is 11470 being *free*, because then the lab binds it and Colosseum's adopt-first probe
> captures the lab engine on the next Play. At baseline 11470 *was* free, so it would have
> fired immediately.
>
> **Implemented instead:** a second copy `_t2lab/specimen-lab/` with every literal `11470`
> rewritten to `11480` — six sites, one byte each, no offset shift. Site 1 is the listener;
> sites 2–5 are outbound self-references that, left alone, would make the lab phone the REAL
> service; site 6 is a CORS check moved for consistency. The `port++ < 11474` retry band is
> deliberately left untouched so a busy lab port fails loudly instead of walking back toward
> production. `_t2lab/specimen/` stays byte-identical to the install and is what Slice 1 reads.
> Tooling committed at `docs/research/tankorent2-phase0/labscripts/`.

`native/build*/` in `.gitignore` already covers `_t2lab/` — no new rule needed (verified).

**Behavior to preserve:** the StremioService install is unmodified (hashes match before and
after); the official service, if running, is neither killed nor reconfigured; Colosseum's
own streaming path is untouched.

**Baseline:** record whether the official service is currently answering on 11470, and the
SHA-256 of the two install binaries, before copying.

**Focused tests:**
- Qt Test: not applicable — no C++ contract changes in Phase 0 (no app code ships).
- Qt Quick Test: not applicable — no QML changes.
- Existing harnesses: none apply; this slice adds no code to the build.
- Negative control: **required and specific** — with the lab server running, confirm the
  install-dir hashes are still identical to baseline, and confirm the lab process is a
  distinct PID from any official service. Then deliberately attempt the launcher a second
  time and confirm it refuses (or binds a second distinct port) rather than silently
  colliding.

**Test seam status:** not applicable — research slice, no deterministic seam exists or is
needed; the checkable outputs are the recorded hashes and the port/PID evidence.

**Lanista actions:** not applicable — no app UI involved.

**Completion signal:** the lab server prints its `EngineFS server started at
http://127.0.0.1:<port>` line (the same line `streamserver.cpp:151` scrapes) on a port that
is **not** 11470, and `GET /settings` on that port returns JSON.

**State / events / probes:** `GET http://127.0.0.1:<labport>/settings` returns the defaults
object; record `btMaxConnections`, `btDownloadSpeedSoftLimit`, `btDownloadSpeedHardLimit` as
observed stock values (the plan's ground-truth claims 35 / 1.6 / 2.5 MB/s come from a code
comment — this is where they get confirmed against the actual specimen).

**Visual evidence:** none — no UI. A terminal transcript of the startup line and the
`/settings` readback is the exhibit.

**Regression paths:** after the lab server is stopped, confirm the official service still
answers on 11470 (or is still absent, matching baseline) and that Colosseum can still start
a stream normally — a one-film sanity play, human-witnessed.

**Evidence artifacts:** `00-specimen.md` (hashes, sizes, port, stock settings readback,
PID evidence); startup transcript saved to `_t2lab/logs/slice0-startup.log`.

**Bridge status:** not applicable — purely internal research slice, no app surface.

**Completion criterion:** a private specimen runs on a non-11470 port, serves `/settings`,
the install directory hashes are byte-identical to baseline, and the post-run sanity film
plays normally. Recorded in `00-specimen.md`.

---

## Slice 1: The bundle becomes readable code

**Purpose:** turn one 6.6 MB packed file into a browsable module tree so its engine can be
read instead of guessed at.

**Dependencies:** Slice 0 (frozen specimen — always work the copy, never the install).

**Implementation guidance:** pretty-print `server.js`, then split the webpack module table
into per-module files under `_t2lab/unpacked/`. The bootstrap at byte 0 is the standard
`installedModules` shape, so the module table is an object/array literal keyed by module id
— write a small throwaway Node or Python splitter that walks the table and emits one file
per id, plus an `index.json` mapping id → byte range → first-line preview. Do not attempt
renaming or de-minification beyond what the bundle already carries. Record in `01-module-map.md`:
total module count, the entry module id, the ids of the largest ten modules, and which ids
contain the fingerprint strings already found (`torrent-stream`, `bittorrent-tracker`,
`bittorrent-dht`, `parse-torrent`, `ut_metadata`).

**Behavior to preserve:** the specimen copy itself is never edited in this slice — the
splitter reads and emits elsewhere. Slice 4 is the only slice permitted to modify a copy,
and it works on a second copy.

**Baseline:** the confirmed counts from this plan's ground-truth section — the splitter's
output must account for them (e.g. all 3 `torrent-stream` occurrences land in identified
modules, none lost).

**Focused tests:**
- Qt Test / Qt Quick Test: not applicable — no app code.
- Existing harnesses: none.
- Negative control: **required** — re-concatenate the split modules and verify the result is
  byte-equivalent to the pretty-printed input (or account for every difference). A splitter
  that silently drops modules would poison every later slice; this is the check that catches
  it. Additionally, confirm the fingerprint string counts in the split tree equal the counts
  in the original file.

**Test seam status:** not applicable — research slice; the round-trip equivalence check IS
the correctness gate and is specified above.

**Lanista actions:** not applicable.

**Completion signal:** `index.json` exists, module count > 1, and the round-trip check
passes.

**State / events / probes:** not applicable — static analysis only.

**Visual evidence:** none.

**Regression paths:** not applicable — nothing running was changed.

**Evidence artifacts:** `01-module-map.md`; `_t2lab/unpacked/` (local, gitignored);
round-trip check transcript.

**Bridge status:** not applicable.

**Completion criterion:** the bundle is split into per-module files, the round-trip check is
green, every fingerprint occurrence is accounted for in a named module, and `01-module-map.md`
records the counts and the largest/entry modules.

---

## Slice 2: The four live routes traced inward to the engine

**Purpose:** know exactly what happens inside the server between "the app asks for a stream"
and "bytes come back" — the map that tells us where the headroom could be.

**Dependencies:** Slice 1.

**Implementation guidance:** start from the four surfaces Colosseum already speaks to
(confirmed in `streamserver.cpp`): `POST /:hash/create`, `GET /:hash/:idx` (Range-seekable),
`GET /:hash/:idx/stats.json`, `POST /settings`. For each, find the handler in the unpacked
tree and follow it inward to: the torrent session object, the file abstraction, the piece
scheduler / read-ahead logic, and the cache. Where the code matches the public
`torrent-stream` ancestor, label the organ by its public name rather than re-deriving it —
the lineage is the shortcut the spec predicted. Record in `02-route-map.md`, per route: the
handler module id, the call chain to the engine, and any tunable constants encountered
(window sizes, thresholds, timeouts, tracker lists, connection caps).

Specifically answer, with a module+line citation for each:
1. Where does `getDefaults` live and what are ALL its knobs (not just the three the app
   overrides)?
2. What determines which pieces are requested first on a fresh play (the cold-open window)?
3. What happens to piece priorities on a seek?
4. How are peers discovered — which of DHT / trackers / PEX / LSD are actually wired?
5. Where does `stats.json`'s `peers` number come from — connected peers, known peers, or
   unchoked? (This decides whether "200 seeders → 8 peers" is a discovery problem or a
   display artifact, and it is the cheapest possible early STOP signal.)

**Behavior to preserve:** static reading only; nothing is executed or modified.

**Baseline:** not applicable — this slice establishes the baseline understanding others use.

**Focused tests:**
- Qt Test / Qt Quick Test / Existing harnesses: not applicable — no app code.
- Negative control: **required** — for at least two claims in the route map, predict an
  observable value before running anything (e.g. "stock `btMaxConnections` is 35",
  "`stats.json.peers` counts connected peers"), then check the prediction against Slice 0's
  `/settings` readback and, for the peers semantics, against Slice 4's live observation. A
  prediction that fails is recorded as a corrected finding, not quietly edited away.

**Test seam status:** not applicable — research slice; the prediction-then-check discipline
above is the falsifiability mechanism.

**Lanista actions:** not applicable.

**Completion signal:** all five questions above have a written answer with a module
citation, or an explicit "not determinable statically — deferred to Slice 4" with the reason.

**State / events / probes:** not applicable (static).

**Visual evidence:** none.

**Regression paths:** not applicable.

**Evidence artifacts:** `02-route-map.md`.

**Bridge status:** not applicable.

**Completion criterion:** `02-route-map.md` traces all four routes to the engine with module
citations, answers the five questions (or defers with reasons), and lists every tunable
constant found. **This is the renewal checkpoint** — if this slice is not complete when the
static box expires, stop and report to Hemanth.

---

## Slice 3: The lever inventory (Harbor as reference, hard-bounded)

**Purpose:** produce a checklist of what a genuinely good streaming engine bothers to do, so
Stremio is graded against best practice and not merely against our own defaults — the way we
avoid missing a lever *neither* engine pulls.

**Dependencies:** none (parallelisable with Slices 1–2).

**Implementation guidance:** read-only pass over `~/Desktop/harbor-main/harbor-main/src-tauri/`
— specifically `torrent_engine.rs` and the `netcheck`, `dht_boot`, `trackers`, `stream_route`,
`cache_sweep`, `selftest`/`stream_probe` modules named in the spec. For each, extract *the
lever*, not the Rust: what capability it provides, why it would make a stream start faster or
stall less, and how one would observe whether an engine has it. Produce `03-lever-inventory.md`
as a table: lever · why it matters · how to detect it · Stremio's status (from Slice 2, or
"needs live check"). Include the two libtorrent-specific dials the spec flagged (global vs
per-torrent connection limits; PEX requiring explicit `ut_pex`) as levers on our own side.

Also score two levers that Harbor will not supply, drawn from Hemanth's source-race ladder
(`docs/superpowers/ideas/2026-08-07-source-race-and-swarm-merge-ladder.md`): **swarm merge**
(does the engine pool peers across torrents holding an identical file?) and **startup source
race** (does anything measure candidates before committing to one?). Both are detectable, so
both meet the inventory's entry requirement; adding them is two rows, not new scope.

**Hard bounds — the scope-gravity guard:** no Rust is built, run, or raced. No rqbit binary
is procured. No architecture is copied into a Colosseum design in this slice. The output is
one table. If the executor finds itself designing Tankorent 2.0's organs here, that is the
failure mode this bound exists to catch — stop and return to the table.

**Behavior to preserve:** not applicable — read-only on a third-party source tree we do not
own or build.

**Baseline:** not applicable.

**Focused tests:**
- Qt Test / Qt Quick Test / Existing harnesses: not applicable — no app code.
- Negative control: **required** — every lever in the table must carry a *detectable*
  signature ("how would we know if an engine has this?"). A lever nobody can detect cannot
  be scored and is struck from the table. This prevents the inventory becoming an
  aspirational wish-list.

**Test seam status:** not applicable — research slice; detectability is the quality gate.

**Lanista actions:** not applicable.

**Completion signal:** `03-lever-inventory.md` exists with every row carrying all four
columns and no row lacking a detection method.

**State / events / probes:** not applicable.

**Visual evidence:** none.

**Regression paths:** not applicable.

**Evidence artifacts:** `03-lever-inventory.md`.

**Bridge status:** not applicable.

**Completion criterion:** a lever table exists, every lever is detectable, Stremio's status
per lever is filled from Slice 2 or explicitly marked "needs live check" for Slice 4/5. No
Rust built, no design work performed.

---

## Slice 4: Watch the specimen breathe (live instrumentation)

**Purpose:** replace reading with observation — see what the engine actually does with peers
and pieces during a real stream, and settle the peer-exchange question that reading cannot.

**Dependencies:** Slices 0, 1, 2.

**Implementation guidance:** make a SECOND copy of the specimen (`_t2lab/specimen-instrumented/`)
so the pristine frozen copy stays untouched for re-runs. Insert temporary logging at the
points Slice 2 identified, emitting one JSONL line per event to `_t2lab/logs/`: piece-priority
changes, peer connect / disconnect / choke / unchoke (with the source that introduced the peer
— tracker, DHT, or exchange), HTTP byte-range requests received, cache hit/miss, seek events,
and torrent add/teardown. Correlate with wall-clock and a run id. Keep the vocabulary small
and stable — Slice 5 consumes it.

Drive playback with a direct Range GET (curl/mpv against the lab port), **not** through
Colosseum — the app is not part of this phase and involving it risks the isolation contract.

Use a **legal, well-seeded, fixed test torrent** as the standing specimen torrent (Sintel or
equivalent open movie, the same one the existing `torrent_engine_download_harness` uses per
the Phase-1 plan lineage) so runs are comparable and nothing here depends on Hemanth's
library. Record its hash in `04-instrumentation.md`.

**Behavior to preserve:** the pristine specimen copy and the StremioService install remain
unmodified; the lab port remains non-11470; the official service keeps working throughout.

**Baseline:** an uninstrumented run of the same torrent on the pristine copy first — record
time-to-first-bytes and peak peer count. If instrumentation measurably changes those numbers,
the instrumentation is too heavy and must be thinned; that comparison is what makes later
measurements trustworthy.

**Focused tests:**
- Qt Test / Qt Quick Test: not applicable — no app code.
- Existing harnesses: **explicitly excluded from any deterministic gate.** This slice is
  live-network by nature; per the test ledger, live-network work never enters the `unit`
  gate.
- Negative control: **required, two of them.** (1) *Prove the instrumentation landed* — the
  logs must show events changing when a knob changes (e.g. POST `/settings` lowering
  `btMaxConnections` must visibly reduce logged peer-connect events). A log that looks the
  same regardless of input is measuring nothing — this is the "prove the mutation landed"
  discipline, and April's rule 3 in force. (2) *Prove the peers-source attribution works* —
  with trackers deliberately stripped from the magnet, peers must still appear via DHT and be
  labelled as such; if every peer is labelled identically regardless of origin, the
  attribution is broken and the PEX conclusion would be vacuous.

**Test seam status:** not applicable — no deterministic seam is possible for live swarm
behavior; the negative controls above are the substitute and are mandatory.

**Lanista actions:** not applicable — playback is driven by direct HTTP, not the app.

**Completion signal:** a JSONL log for a completed run exists containing at least one event
of each declared type, and the run reached playable bytes (a Range GET returned video data).

**State / events / probes:** from the run log — peer count over time with introduction
source, first-byte latency, piece-priority timeline, cache hit ratio. Cross-check the live
`peers` figure against `stats.json` on the lab port to settle question 5 from Slice 2.

**Visual evidence:** none required (no UI). A plotted peers-over-time chart is optional and
useful for the verdict page.

**Regression paths:** after the run, confirm the official service still answers on 11470 and
a normal film still plays in Colosseum (human-witnessed, one film, once per live session).

**Evidence artifacts:** `04-instrumentation.md` (event vocabulary, torrent hash, baseline vs
instrumented comparison, both negative-control results); run logs in `_t2lab/logs/`.

**Bridge status:** not applicable.

**Completion criterion:** the engine's live behavior is logged with peer-introduction
attribution working (negative control 2 green), instrumentation proven to respond to input
(negative control 1 green), instrumentation overhead measured as non-distorting, and the
peer-exchange question answered with observed evidence — confirmed present, confirmed
absent, or explicitly undetermined.

---

## Slice 5: The experiment battery (how Stremio performs, measured)

**Purpose:** turn "it feels slow sometimes" into numbers on the axes that decide movie night.

**Dependencies:** Slice 4.

**Implementation guidance:** run the scripted battery from spec §4.5 against the instrumented
lab specimen, same torrent, recording the metric set each time: **cold open** (time to first
playable bytes), **deep seek** (recovery time to bytes after a far-forward Range jump),
**low-peer swarm**, **high bitrate**, **multi-file torrent**, **repeated seeks**, and
**restart with partial cache**. Repeat each trial enough times to see spread — a single run is
never a number (house law: one lucky measurement is not a baseline). Record median and range,
not a single figure. Measure on a quiet machine: no build running, no IDE indexing, no other
download active — note the machine state with each run.

**Behavior to preserve:** isolation contract intact for every run.

**Baseline:** Slice 4's uninstrumented reference run is the anchor for cold-open; each trial
type also establishes its own first-run baseline for later comparison in Phase 2 if the arc
proceeds.

**Focused tests:**
- Qt Test / Qt Quick Test / Existing harnesses: not applicable — no app code; live network
  excluded from deterministic gates per the ledger.
- Negative control: **required** — a deliberately degraded run (e.g. connection cap forced to
  a very low value) must move the numbers in the expected direction. If cold-open time does
  not worsen when the engine is deliberately hobbled, the measurement rig is not measuring
  what we think, and every number in this slice is void.

**Test seam status:** not applicable — live-network measurement; the degradation control is
the validity gate.

**Lanista actions:** not applicable.

**Completion signal:** every trial type has at least three completed runs with recorded
median and range, or an explicit note of why a trial could not be run.

**State / events / probes:** the run logs plus `stats.json` samples at the app's own 1 Hz
cadence, so the numbers are directly comparable to what the player already displays.

**Visual evidence:** the results table; optionally the peers-over-time chart per trial.

**Regression paths:** post-session sanity film, human-witnessed, once per live session.

**Evidence artifacts:** `05-battery-results.md` with the full table, machine-state notes, and
the degradation control's result.

**Completion criterion:** all seven trial types measured with spread (or explicitly skipped
with reason), the degradation negative control confirms the rig detects change, and the
results table is written.

---

## Slice 6: The chassis counter-probe (can OUR engine reach further?)

**Purpose:** answer the actual investment question — not "is Stremio imperfect" but "can the
engine we already own do measurably better on the same swarm at the same moment."

**Dependencies:** Slices 4, 5.

**Implementation guidance:** write a **disposable** measurement probe — not an engine, not an
organ, not the start of Tankorent 2.0 — against our existing imported
`native/torrent/engine/TorrentEngine`. Model it on the existing
`tests/torrent_engine_download_harness.cpp` (already live-network, watchdog-guarded,
exit-code-verdict, and classified in the test ledger as live-network-never-in-a-gate). The
probe adds the same magnet as the battery, and reports: time to metadata, time to first
piece, peers known vs connected vs unchoked over time, and sustained throughput.

Run it **A/B against itself** on the levers Slice 3 identified on our side — at minimum PEX
off vs PEX on (`ut_pex` added explicitly), and global vs per-torrent connection limits. Then
run it **fair-start against the lab Stremio**: identical magnet, both started together, same
machine, same moment (spec §6's fair-start constraint). Record in `06-chassis-probe.md`.

**Explicitly forbidden here:** any scheduler tuning from April's falsified list; any work
that shapes into Phase 1's organs. This probe is thrown away after the verdict. If it starts
growing a piece scheduler, that is scope gravity and the slice has failed its bound.

**Behavior to preserve:** the probe must not touch the app's torrent state — it uses its own
temp cache dir and its own session, exactly as the existing download harness does
(`QTemporaryDir` / dedicated dir, per the ledger's isolation practice). The daily download
engine's behavior for comics/manga/books is unchanged; nothing in `TorrentEngine` is edited
in this phase.

**Baseline:** the Slice 5 Stremio numbers on the same torrent and the same machine state.

**Focused tests:**
- Qt Test: not applicable — the probe is disposable measurement code, not a shipped contract.
  Adding a permanent unit test for code we intend to delete would be waste.
- Qt Quick Test: not applicable — no QML.
- Existing harnesses: the probe is modelled on `torrent_engine_download_harness` and, like
  it, is **live-network — never registered into the `unit` gate.** The existing harness must
  still build and pass unchanged (it is the pattern being reused, not modified).
- Negative control: **required** — the PEX on/off A/B is itself the control, and it must be
  proven to have *landed*: log the session's active extensions so an "unchanged" result can
  be distinguished from "the flag never applied." A silently-unapplied `ut_pex` would produce
  a vacuous "no difference" and could wrongly STOP the whole arc.

**Test seam status:** not applicable — live-network probe by nature; the landed-mutation
control above is mandatory and replaces a deterministic seam.

**Lanista actions:** not applicable.

**Completion signal:** the probe exits 0 with a full metrics line for both PEX states and for
the fair-start race against the lab Stremio.

**State / events / probes:** probe stdout metrics plus the lab Stremio's `stats.json` sampled
during the fair-start race, so both sides are read in the same dialect.

**Visual evidence:** none required; the comparison table is the exhibit.

**Regression paths:** confirm the app's own downloads still work after probe runs (start and
cancel one small book/comic download, human-witnessed) — the probe shares a library with the
daily engine even though it uses its own session, so this is cheap insurance.

**Evidence artifacts:** `06-chassis-probe.md` with the A/B table, the fair-start race
results, and the extensions-active log proving the PEX flag applied.

**Bridge status:** not applicable.

**Completion criterion:** our chassis is measured on the same swarm against the same
competitor with a fair start; the PEX A/B is proven to have actually applied; the numbers are
recorded with spread, not single runs.

---

## Slice 7: The verdict (behavior spec + STOP/GO)

**Purpose:** give Hemanth one page that says, on evidence, whether we build Tankorent 2.0 or
keep Stremio and walk away.

**Dependencies:** Slices 2–6.

**Implementation guidance:** write `VERDICT.md` in two parts.

*Part A — the behavior spec* the original Phase 0 promised (spec §9): cold-open piece priority
and read-ahead window; seek demote/promote behavior; buffer-start threshold; cache retention
and ownership; starvation recovery; and which defaults Stremio overrides. Every claim carries
a citation — a module reference from Slice 2 or an observation from Slice 4/5. **Any claim
that cannot be cited is deleted, not softened.**

*Part B — the verdict*: the three-outcome table from this plan's header, with the actual
result, the numbers behind it, and the named levers with their measured impact. State
explicitly what would change the verdict (e.g. "STOP unless a future measurement shows X"),
so a later session can reopen it honestly rather than re-litigating from scratch.

Include a **negative-results ledger** — every hypothesis tested and falsified during the dig,
with reasons, appended in the shape of `feedback_stream_failed_hypotheses.md` so it is never
re-tried blind. This is a spec §9 cross-cutting requirement and is not optional even on a GO.

**Behavior to preserve:** not applicable — documentation slice.

**Baseline:** not applicable.

**Focused tests:**
- Qt Test / Qt Quick Test / Existing harnesses: not applicable — documentation.
- Negative control: **required in a documentary form** — an explicit adversarial pass asking
  "what would have to be true for this verdict to be wrong?", answered in writing. On a GO
  this must name the strongest argument for STOP; on a STOP it must name the strongest
  argument for GO. A verdict that cannot state its own counter-case has not been tested.
  Route this pass through a second substrate (the `advisor` skill or Codex) per house review
  reflex — a verdict this consequential should be re-derived by a mind that fails differently.

**Test seam status:** not applicable — documentation slice.

**Lanista actions:** not applicable.

**Completion signal:** `VERDICT.md` exists with both parts, every Part-A claim cited, and the
adversarial pass recorded.

**State / events / probes:** not applicable.

**Visual evidence:** the one-page scoreboard — challenger-chassis vs Stremio per trial,
readable in ten seconds (spec §6).

**Regression paths:** not applicable.

**Evidence artifacts:** `VERDICT.md`; a chat.md report to Hemanth in Hemanth-language
summarising the verdict in user terms (what it means for movie night), not in engine terms.

**Bridge status:** not applicable.

**Completion criterion:** Hemanth has a one-page STOP or GO backed by cited claims and
measured numbers, the negative-results ledger is written, the adversarial counter-case is
recorded, and the second-substrate review has been run. **Phase 1 does not begin without a
GO on this page.**

---

## Plan self-review (performed at write time)

1. **Spec coverage.** Spec §4's six-step method maps to Slices 1 (recover structure), 2
   (fingerprint + map routes inward), 4 (instrument the live engine), 5 (controlled
   experiments), 7 (extract clean-room behavior spec). §4's time-box requirement is Slice
   2's renewal checkpoint plus the explicit box table. §9's Phase-0 acceptance list is
   Slice 7 Part A, item by item. §9's cross-cutting "every negative result written to the
   arc's ledger" is Slice 7's negative-results ledger. Hemanth's 2026-08-07 amendment (dig
   must prove or disprove feasibility) is Slices 3, 5, 6 and Part B — the parts the original
   spec did not have. His second amendment (rqbit reference-only) is Slice 3 with explicit
   hard bounds.
2. **Ledger honesty.** Both ledgers were read fresh (test ledger at 314 lines, Lanista at
   256). **No slice names a Lanista capability**, because no slice touches the app — every
   Bridge status is `not applicable` with the reason stated, never `available` by assumption
   and never `bridge blocked` for a missing unit test. **No slice claims a unit test**,
   because Phase 0 ships no app code; every Qt Test / Qt Quick Test line says why it does not
   apply rather than being left blank. The one existing harness referenced
   (`torrent_engine_download_harness`) is cited only as a *pattern to copy*, and its ledger
   classification — live-network, never in a deterministic gate — is honored: nothing here
   enters the `unit` gate.
3. **Negative controls.** Every slice carries one, and they are real falsifiers rather than
   ceremony: the round-trip check (Slice 1) catches a lossy splitter; the peer-attribution
   check (Slice 4) prevents a vacuous PEX conclusion; the degradation run (Slice 5) proves the
   rig detects change; the extensions-active log (Slice 6) proves the PEX flag applied — the
   single most dangerous vacuous-pass in the plan, since an unapplied flag would produce a
   false STOP and kill the arc on a measurement error.
4. **Safety.** The isolation contract is a first-class section because planning surfaced a
   genuine hazard: `streamserver.cpp` adopts port 11470, so an instrumented server on that
   port would silently capture Hemanth's real playback. Lab runs are pinned off-11470, on a
   copied specimen, with a human-witnessed sanity film after each live session. The install
   is hash-verified unmodified. No live user data is touched at any point.
5. **No wishes.** No slice says "verify manually", "ensure it works", or ends at a screenshot.
   Where the honest proof is Hemanth's eyes (the post-session sanity film), it is written as
   human-witnessed with exact steps, per the 2026-08-06 ratification — a five-second human
   look is the right witness for "a film still plays," and no bridge capability is invented
   to fake something better.
6. **Scope gravity — the named risk.** The spec warns that once an engine is in reach,
   everything looks like a nail. Two slices carry explicit hard bounds against becoming Phase
   1 early (Slice 3: no Rust built, no design work; Slice 6: probe is disposable, no
   scheduler). April's falsified list is quoted as law with both entries named so no executor
   burns a session re-testing them.
7. **Dependency order.** 0 → 1 → 2 → (4 → 5 → 6) → 7, with Slice 3 parallelisable from the
   start. The renewal checkpoint sits at the static/live boundary, which is exactly where the
   cost curve turns — cheap reading before, expensive live measurement after.

---

*Plan ends. Execute under `brotherhood-executing-plans`. Phase 1 is NOT authorized by this
plan — only a GO verdict in Slice 7 can authorize it.*
