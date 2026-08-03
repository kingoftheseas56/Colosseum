# Tankorent 2.0 — The Challenger Engine (Design Spec)

- **Date:** 2026-08-02
- **Owner:** Agent 4 (Player / Theatre lane) — brainstormed by a player-lane guest (Claude), to be ratified into A4's lane
- **Status:** Design approved (Hemanth, 2026-08-02) — pending spec self-review, then implementation plan
- **Arc:** TANKORENT_2_CHALLENGER
- **Supersedes nothing yet.** Stremio's official server remains production video for the foreseeable future. This spec describes a *challenger* that must win on measured evidence before it touches a single real evening.

---

## 0. One-sentence promise

> Build Tankorent 2.0 — our own libtorrent streaming engine — slowly and outside the app, and let Hemanth *watch it beat Stremio's server* on cold-open, peer connection, and streaming speed during ordinary evenings, before it ever replaces what he watches on.

If that promise is ever at risk — if the challenger could stall a real film, or if "it's faster" rests on my word instead of a scoreboard — the design has failed its own bar.

---

## 1. Why this exists (the thread, honestly)

This arc was born the night Hemanth asked why a 200-seeder torrent connects to single-digit peers. That led to Popcorn Time's honest buffering line (shipped, Colosseum `3cb3444`) and raised swarm caps (shipped, `3a8bd27`), then to an expert review arguing the real lever is *reachability*, not connection ceilings. The review's advice fit libtorrent — which we own — far better than the Stremio server we currently run for video.

Hemanth's call: **don't reopen the old fight blind.** Tankorent 1.0 died in April not because the engine "didn't work" (the same libtorrent engine ships his comics, manga, and books every day) but because the *streaming scheduler's tail behavior* was never nailed, and we debugged it by source-reading and guessing — losing ~30 minutes per hypothesis→code→smoke→revert cycle. The April ledger (`feedback_stream_failed_hypotheses.md`) records two fixes proven wrong with reasons, working stall telemetry, and one prime unchased suspect: **libtorrent silently drops our "this piece is urgent" signals once past the cold-open head window.**

Tankorent 2.0 starts warm where 1.0 started cold, because we now hold three things we lacked:

1. **A behavior spec of Stremio's engine** (Phase 0, the Rosetta dig — see §4).
2. **A reference architecture** — Harbor's rqbit engine (`~/Desktop/harbor-main`), read-only, shows how a *good* streaming engine is organized.
3. **A scoreboard-first discipline** — evidence before depth, the exact inversion of April.

---

## 2. Current state (ground truth, inspected)

- **Ships:** Stremio's official server carries all Colosseum video (Hemanth's 2026-07-05 call; adopt-first on `:11470`, now with tonight's raised caps + honest buffering line). Our libtorrent engine (`native/torrent/engine/TorrentEngine.cpp`) ships comics/manga/books daily — UPnP + NAT-PMP on, DHT on, 400-connection ceiling, already stream-tuned. It **does not drive video.**
- **Partial / uncertain:** the April streaming brain (scheduler, `StreamPieceWaiter`, `piece_diag` telemetry) was **never imported into Colosseum** — it lives only in Tankoban 2 (Hoth). Its tail behavior is unproven. It died *informed*, not *confused*.
- **New (this arc):** a standalone Tankorent 2.0 process; a race harness that A/Bs it against Stremio on the same torrent; an in-app "challenger rides shotgun" view; and — first — the Rosetta behavior spec that tells us what to build.

### Reference assets confirmed on disk
- **Harbor engine:** `~/Desktop/harbor-main/harbor-main/src-tauri/src/torrent_engine.rs` (448 lines) + modules `stream_route`, `cache_sweep`, `selftest` (with `stream_probe`), `dht_boot`, `netcheck`, `trackers`. Uses embedded `librqbit`. **Its live stats DTO uses the identical field names Stremio uses** — `peers`, `unchoked`, `downloaded`, `downloadSpeed`, `streamProgress` — i.e. Harbor independently chose to speak Stremio's dialect. This is architecture reference only; we build on libtorrent, not rqbit.
- **Stremio specimen:** the exact `server.js` Colosseum launches, at `C:/Users/Suprabha/AppData/Local/Programs/StremioService`. Plain (bundled) JavaScript, cooperative, local, already answering `/settings` and `/:hash/:idx/stats.json` to us live. Its lineage descends from the public `torrent-stream` family (same ancestor Popcorn Time used) — so its bundle can be diffed against a labeled skeleton, not deciphered from zero.
- **April ledger:** `~/.claude/projects/.../memory/feedback_stream_failed_hypotheses.md`.
- **rqbit revival design (TB2, paper-only, never coded):** `~/Desktop/Tankoban 2/docs/superpowers/specs/2026-06-07-theatre-rqbit-revival-design.md` — historical context; its rqbit-subprocess choice is superseded by "libtorrent, informed by Harbor."

---

## 3. Scope

### In scope
- A standalone Tankorent 2.0 engine (libtorrent), developed outside the Colosseum app as its own long-lived process.
- The Rosetta dig: a clean-room behavior spec of Stremio's streaming engine.
- A deterministic race harness (the "Derby") that measures challenger vs champion on identical torrents.
- An in-app challenger-shotgun view surfacing live per-engine numbers during a gated trial session.
- A Stremio safety-net catch during trial nights.

### Out of scope (non-goals)
- Replacing Stremio in production during this arc. Promotion is a **separate future gate**, not part of this build.
- HTTP-source streaming / Path-3 — that is the **vidking player** arc. Tankorent 2.0 is **pure BitTorrent.**
- IPv6 for the video path — the IPv4-pin scar (dead ISP IPv6 → 0 DHT nodes) stands until deliberately re-examined.
- Reworking the comics/manga/books download engine. Tankorent 2.0 is a *streaming* engine; the download path is untouched.
- Any promise that the challenger will win. If it can't beat Stremio, we keep Stremio and lose only a bounded time-box.

---

## 4. Phase 0 — The Rosetta Dig (behavior spec before engine code)

**Rule:** no Tankorent 2.0 engine code is written until this phase produces a behavior spec. We build knowing how Stremio breathes, or we don't build.

**Specimen:** the exact `server.js` Colosseum currently launches (pin the version; treat as a fixed specimen). Never "whatever is latest."

**Method (the boys' chore end-to-end; Hemanth receives findings, never hex):**
1. **Recover structure.** Pretty-print `server.js`; identify the bundler; split its module table into per-module files.
2. **Fingerprint dependencies.** Search error strings, constants, tracker defaults, method names → identify the torrent library, storage, HTTP framework, ffmpeg wrapper, cast modules. Diff against the public `torrent-stream` ancestor to relabel minified organs.
3. **Map routes inward.** From observable endpoints (`/create`, `/:hash/:idx` stream, `stats.json`, `/settings`, subtitles, cast) trace each handler back to the torrent session, file abstraction, piece scheduler, and cache.
4. **Instrument the live engine.** Temporary logging around: piece-priority changes, torrent selection, HTTP byte ranges, file reads, cache hit/miss, seek events, peer connection changes, buffer thresholds, torrent cleanup. Watch it breathe, don't trust its claims.
5. **Controlled experiments.** Same specimen, scripted battery: initial playback; forward/backward seek; very low peer count; one fast peer vs many slow; high bitrate; multi-file torrent; repeated seeks; pause/resume; restart with partial cache. Correlate every player action with internal piece-priority + network behavior.
6. **Extract a clean-room behavior spec.** NOT copied code. A document of decisions: *"On initial playback, prioritize pieces A–B; hold this read-ahead window; on seek, demote old window, promote new, preserve these cache regions, resume HTTP output past this threshold; distrust peers matching X; recover from starvation via Y."*

**Phase 0 deliverable:** `docs/superpowers/specs/2026-XX-XX-stremio-engine-behavior-spec.md` — the map that feeds every later phase. **Time-boxed**; if the box expires with the spec half-written, every finding is still kept and the box is explicitly renewed or the arc paused — no silent overrun.

**Explicit discipline (from April):** `piece_diag`-style telemetry is ground truth; run a real smoke immediately after any scheduler change; never ship a scheduler tune without a diagnostic event proving the tune activated. Source-reading + reasoning alone got the wrong answer twice in April.

---

## 5. Engine architecture (Tankorent 2.0 — libtorrent, organs not blob)

The April engine was one blob "playing separate songs." Harbor shows the cure: **coordinated single-purpose organs.** Tankorent 2.0 mirrors that shape on libtorrent:

| Organ | Responsibility | Reference / origin |
|---|---|---|
| **Session/lifecycle** | libtorrent session, settings, clean start/stop | our `TorrentEngine` lineage |
| **DHT bootstrap** | tiered DHT readiness, bootstrap nodes | Harbor `dht_boot` (`dht_tier`) |
| **Reachability (netcheck)** | inbound TCP/UDP status, UPnP/NAT-PMP mapping result, CGNAT estimate — a **first-class organ**, the reviewer's #1 lever | Harbor `netcheck` |
| **Trackers** | tracker set policy (original + preserved + small maintained fallback + DHT/PEX; no 50-tracker spray) | Harbor `trackers` |
| **Piece scheduler** | the head-window + read-ahead + seek demote/promote logic — **the organ that killed April; built from the Rosetta spec, not folklore** | Rosetta spec (§4) |
| **Stream route** | Range-seekable HTTP endpoint mpv consumes | Harbor `stream_route` (axum) → our HTTP server |
| **Cache** | retention-hours + max-GB sweep, keep-list for state files | Harbor `cache_sweep` |
| **Self-test** | the engine probes its own stream + network health and *reports why it is slow* — "instrument before you claim" cast into the engine's bones | Harbor `selftest` + `stream_probe` |

**Two libtorrent-specific dials the review flagged, to verify explicitly (not assume):**
- Global `connections_limit` **and** per-torrent `set_max_connections()` — a low value at either layer silently caps the other.
- **PEX is not enabled by default in libtorrent** — it needs the `ut_pex` extension added explicitly. (This is also the one cheap win worth taking on the *download* engine regardless of this arc.)

**Dialect contract (load-bearing):** Tankorent 2.0 exposes **Stremio's exact interface** — same localhost URL shapes (`/:hash/:idx`), same `stats.json` field names (`peers`, `unchoked`, `downloaded`, `downloadSpeed`, `streamProgress`, `streamLen`), same `/settings` door — on a **different port**. Consequences:
- The app never learns a second language; champion↔challenger is a port swap.
- Everything already shipped — the buffering face (`3cb3444`), the stats line — works on Tankorent 2.0 unchanged from day one.
- Races are automatically fair: same player, same chrome, only the engine differs.

---

## 6. The Derby (deterministic race harness)

"Beat Stremio" becomes a scripted, repeatable race — never a vibe.

- **Fair start (constraint):** the harness hands **both** engines the **identical magnet** and starts them **together**, same machine, same moment. No fair start → the numbers lie.
- **Trials:** cold-open (time-to-first-frame-ready), deep-seek recovery, low-peer swarm, high-bitrate, multi-file, repeated seeks. (Same battery as the Rosetta experiments, so champion and challenger are measured on the axes we studied.)
- **Metrics (from the shared stats dialect):** time-to-playable, sustained MB/s, peers found vs reached vs actively delivering, stall count/duration, seek-recovery time.
- **Scoreboard:** one-page report, challenger vs champion per trial, readable in ten seconds. On demand or nightly.
- **Real data (locked):** the challenger **pulls real torrent data** during a race — shadow-measurement would be theater. A/B honesty is the whole point.

---

## 7. The in-app experience — "the challenger rides shotgun"

The one part Hemanth sees. Locked: race visibility is **in-app**, not reports-only.

**Normal nights (untouched):** he presses Play, Stremio serves the film, exactly as today. His line pulls the film **once**. Nothing about the challenger is visible or active.

**Trial nights (per-session toggle):** he flips the challenger on for a session. Then:
- The film still plays **on Stremio** — what he watches is never at the challenger's mercy.
- A quiet second row appears in the player's existing statistics panel: the **same torrent, same moment, both engines' live numbers side by side** — Stremio getting him the picture, Tankorent 2.0 racing it in the background.
- The challenger **pulls real data** (true A/B). His line pulls the film **twice** this session — the explicit, chosen cost of an honest race. That cost is *why* the true race is gated behind the toggle.
- He watches the challenger **gain over weeks**, during ordinary evenings, until the day its numbers are ahead.

**Safety net (trial nights):** if a challenger *ever* drives playback in a later trial stage and stalls mid-film, the player **silently re-catches the same stream on Stremio at the same second.** Production comfort is never gambled. (In the shadow-race default, Stremio is already the one playing, so the net is implicit.)

**States & recovery:**
- *Toggle off (default):* single engine, single pull, zero challenger footprint.
- *Toggle on, challenger healthy:* shotgun row live, both engines charted.
- *Toggle on, challenger stalls/errors:* its row shows the honest failure (this IS the data); Stremio playback is unaffected; the self-test organ records *why*.
- *Back out / player closed mid-trial:* challenger fetch is torn down with the session (no orphaned second download — mirrors tonight's `unwatchStats` teardown discipline).

---

## 8. Rollout ladder (lab-first, then trial nights)

1. **Phase 0 — Rosetta dig.** Behavior spec of Stremio. (§4)
2. **Phase 1 — Engine skeleton + dialect.** Tankorent 2.0 stands up on libtorrent, speaks Stremio's dialect on its own port, serves a stream to mpv in isolation. Organs from §5 stubbed then filled. No app wiring yet.
3. **Phase 2 — The Derby.** Race harness + scoreboard. Challenger races Stremio headless, on real torrents, fair start. **Lab-only until the challenger consistently ties-or-beats Stremio on the harness.**
4. **Phase 3 — Shotgun trials.** In-app challenger-rides-shotgun view behind the per-session toggle; shadow race during real evenings; Hemanth's eyes on the live gain. Safety net armed.
5. **Phase 4 — Promotion gate (SEPARATE, not this arc).** Only after the scoreboard *and* Hemanth's eyes agree the challenger wins does replacing Stremio even get proposed. Pure BT still; Stremio remains the fallback catch even post-promotion until proven redundant.

Each phase is gated; nothing advances on my word — scoreboard and Hemanth's eyes are the gates.

---

## 9. Acceptance criteria (observable)

- **Phase 0:** a behavior-spec doc exists covering, at minimum: cold-open piece priority + read-ahead window size; seek demote/promote behavior; buffer-start threshold; cache retention/ownership; starvation recovery; and which libtorrent (or equivalent) defaults Stremio overrides. Each claim traceable to an instrumented observation or a controlled experiment, not a guess.
- **Phase 1:** Tankorent 2.0, given a magnet on its own port, returns a Range-seekable stream URL that mpv plays start-to-finish; `stats.json` returns the Stremio-dialect fields; the buffering line (`3cb3444`) renders against it unmodified.
- **Phase 2:** the Derby produces a one-page scoreboard from a fair-start race on ≥6 trial types; results are reproducible run-to-run (same specimen, same battery); a challenger regression shows as a red trial, not a silent pass.
- **Phase 3:** with the toggle on, the player shows both engines' live numbers during a real film; with the toggle off, there is zero challenger network footprint (verified — one pull, not two); a forced challenger stall never disturbs Stremio playback.
- **Cross-cutting:** no scheduler change ships without a diagnostic event proving it activated; every negative result is written to the arc's ledger so it is never re-tried blind.

---

## 10. Constraints (law for every later choice)

- April's falsified-fix list (`feedback_stream_failed_hypotheses.md`) is law — no re-tries without new evidence explaining why *our* conditions differ.
- IPv6 stays off the video path until the ISP-IPv6 scar is deliberately re-examined.
- Production playback is **never** at risk from challenger work.
- Every race is fair-start (identical magnet, simultaneous, same machine).
- Normal nights pull the film **once**; race nights pull **twice** — always Hemanth's explicit per-session choice.
- Tankorent 2.0 speaks Stremio's dialect (URL + stats + settings), different port.
- Engine is coordinated organs with self-diagnosis baked in — never one blob.
- Findings reach Hemanth as documents; teardown/instrumentation is the boys' chore.

---

## 11. Deferred (intentionally out, not dead)

- **Path-3 / HTTP-prefer streaming** — belongs to the **vidking player** arc, not here.
- **IPv6 re-examination** for the video path.
- **Promotion** (replacing Stremio) — a separate future gate after this arc proves the challenger.
- **PEX / reachability upgrades on the comics/manga download engine** — cheap wins the review surfaced; can be taken independently of this arc.

---

## 12. Discarded alternatives (with reasons)

- **Reopen the April engine in-place and keep tuning** — rejected: starts at maximum depth with no scoreboard, the exact trap that drowned us in April.
- **Build another rqbit engine of our own** — rejected: Hemanth's call. We own libtorrent, it ships his books daily; Harbor's rqbit is a *reference to read*, not a chassis to run. (Also retires the earlier "stock-vs-fork rqbit" chassis question entirely.)
- **rqbit as a linked Rust library (FFI)** — rejected (also TB2's 2026-06 finding): no C bindings, async/Tokio bridge across FFI, drags a Rust toolchain into the C++/Qt/CMake build for little gain.
- **Shadow-measure instead of pulling real data** — rejected: not a real A/B; honest numbers require real bytes off the same swarm.
- **Reports-only race visibility** — rejected: Hemanth wants to *feel* the race during ordinary use; in-app shotgun view chosen.
- **Skip Phase 0, build from folklore** — rejected: this is precisely why Tankorent 1.0 failed.

---

## 13. Technical contract (enough to plan, no more)

- **Process shape:** standalone engine process, launched/adopted like Stremio's (our `StreamServer` already models adopt-first + child-launch + port scrape + dead-adopt self-heal; the challenger reuses that lifecycle pattern, pointed at a different binary + port).
- **Interface:** HTTP on `127.0.0.1:<challenger-port>`; routes `POST /:hash/create`, `GET /:hash/:idx` (Range-seekable), `GET /:hash/:idx/stats.json`, `POST /settings` — Stremio-shaped.
- **Engine:** libtorrent, organs per §5; scheduler built from the Phase-0 spec.
- **App wiring:** additive; the player already speaks this dialect (tonight's `watchStats`/`streamStats` + buffering line). The shotgun view extends the existing statistics panel; the toggle is a new per-session player control. Player is **Agent 4's lane** — all app-side edits declared on `agents/chat.md` before landing.
- **Isolation:** engine lives outside the app (its own repo/dir); the app depends on it only through the HTTP dialect + a launch path, exactly as it depends on Stremio today.

---

## 14. Open questions

None blocking. Phase-0 findings will surface engine-internal decisions (window sizes, thresholds), but those are *outputs of the dig*, not unresolved product questions — they get decided with evidence, in the behavior spec, not guessed here.

---

*End of design spec. Next step after Hemanth's review: an implementation plan for Phase 0 (the Rosetta dig) only — later phases are planned once the behavior spec exists.*
