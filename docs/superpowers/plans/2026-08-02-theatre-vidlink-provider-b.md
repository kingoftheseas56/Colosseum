# Theatre VidLink Hosted-Player — Provider B Implementation Plan

> **STATUS: GATED — DO NOT LAUNCH YET.** This plan is deltas on the VidKing hosted-player
> architecture (`2026-08-02-theatre-vidking-hosted-player-extension.md`). It may be handed to interns
> **only after** the VidKing plan is executed AND Agent 4 (player/theatre domain leader) has reviewed
> the VidKing report-back and confirmed the architecture holds. Launching this against an unproven or
> drifted VidKing base is the exact failure Agent 4's Phase-1 gate exists to prevent. If any interface
> named below does not match the VidKing code as actually landed, STOP and report to Agent 4 — do not
> improvise the delta.

> **For agentic workers:** REQUIRED SUB-SKILLS: superpowers:executing-plans + superpowers:test-driven-development.
> Work inline in the existing `master` checkout. Do not create a branch, worktree, or subagent workspace.
> Preserve every unrelated modified/untracked file; stage only the files named by the current task.
> Commits are enabled: land each task's scoped commit as its final step. Report back to Agent 4.

**Goal:** Add VidLink as the second trusted hosted-player provider in Theatre's existing Sources sheet,
reusing the VidKing architecture wholesale. VidLink patches VidKing's one known coverage gap — **anime
without a TMDB ID** — via VidLink's MAL-ID anime route. Add two registry-layer features the fleet needs:
a **per-provider dead-end memory** (a negative cache with a lifespan, NOT a permanent hide) and an
**offered-fallback** action on the honest-failure panel (consented, one click, never an automatic cascade).

**Architecture:** No new architecture. VidLink is a second entry in the app-owned provider registry
(`HostedPlayerApi.js`), a second bundled hosted-player extension, and a second trusted frame origin in
the existing wrapper. The wrapper's single hardcoded VidKing origin becomes a **per-provider expected
origin supplied by the app-owned registry** (never by a remote manifest). Everything else — the bridge,
`HostedPlayerPage`, session integration, Progress, Continue — is unchanged and shared.

**Why keyless / no pre-check:** the 2026-08-02 probe
(`docs/superpowers/plans/2026-08-02-vidking-source-resolver-probe.md`, commit `b9c481f`) proved hosted
providers cannot be honestly pre-checked for availability — VidKing's resolver is keyless-reachable but a
deliberately-encrypted, seed-rotating, nine-engine anti-scrape protocol ("STREAMCRYPTO", `enc=2`); cracking
it is a maintenance trap that crosses the "documented interface only, no scraping" line. The dead-end
memory below is the honest substitute: we do not predict availability, we **remember confirmed failures**
for a short window. The probe also confirmed the documented `PLAYER_EVENT` sets carry **no error /
no-source event** (both providers: `play|pause|seeked|ended|timeupdate` only) — a dead-end is knowable
ONLY by the absence of any play/timeupdate event within the startup window, which is why the startup guard
below is the primary failure signal, not an event.

## Global Constraints (delta from VidKing)

- All VidKing constraints remain in force. VidLink is likewise keyless, removable, `core:false`, uses
  only VidLink's documented iframe/postMessage interface, and never routes through mpv/torrent/download.
- VidLink's trusted origin is `https://vidlink.pro` (NO `www`, unlike VidKing's `https://www.vidking.net`).
  The origin check must be exact; do not normalize `www`.
- VidLink URL routes (documented): movie `https://vidlink.pro/movie/{tmdbId}`, tv
  `https://vidlink.pro/tv/{tmdbId}/{season}/{episode}`, anime `https://vidlink.pro/anime/{malId}/{episode}/{subOrDub}`.
- VidLink parameters differ from VidKing — use VidLink's real names: `primaryColor` (hex, NO `#`),
  `autoplay` (not `autoPlay`), `startAt` (whole seconds — NOT `progress`), `nextbutton` (the series
  next-episode toggle — VidLink has no `episodeSelector`; do not invent one). Set `primaryColor=e8b923`.
- VidLink `PLAYER_EVENT` postMessage shape matches VidKing's (`{type:"PLAYER_EVENT", data:{event, currentTime,
  duration, tmdbId, mediaType, season, episode}}`, events `play|pause|seeked|ended|timeupdate`), so the
  existing `HostedPlayerBridge` and `normalizeEvent` accept it unchanged. Verify this against the landed
  VidKing bridge; if the allowed-event set differs, report — do not widen the bridge to fit.
- **Ship VidLink enabled by default.** Redundancy-by-default is this arc's whole thesis — two rows so a
  dead-end on one still leaves the other. It is one-click-removable like VidKing. (Product call, Agent 4,
  reversible via the extension toggle.)
- Anime rows: offer VidLink's anime route only when a positive integer **MAL ID** and episode are known.
  Default `subOrDub` to `sub`. Do not fabricate a MAL ID from a TMDB ID or vice-versa.
- **Dead-end memory is per (providerId, mediaId) and EXPIRES** (default 72h). It suppresses a row only
  after a *confirmed* in-player failure for that exact title+provider, and only until the window lapses —
  never a permanent hide, because hosted sources are volatile. It never suppresses a different provider's
  row for the same title.
- **No silent fallback cascade.** On honest failure, if another enabled hosted provider can offer this
  title, show a `Try {providerName} instead` button. It requires a click. Selecting VidKing then failing
  never auto-opens VidLink.

## File Map (delta)

**Modify**
- `qml/HostedPlayerApi.js` — register the `vidlink` provider (URL builder, expected origin, anime/MAL
  support in `rowsFor`); add `expectedOrigin(providerId)`; add the dead-end-memory read used by `rowsFor`.
- `resources/hostedplayer/host.html` — CSP `frame-src` enumerates BOTH trusted origins (fixed app-owned set).
- `resources/hostedplayer/host.js` — validate the load origin against a registry-supplied `expectedOrigin`
  passed in the wrapper query, instead of the hardcoded VidKing constant.
- `native/engine/ExtensionsStore.cpp` / `.h` — seed `net.vidlink.player` bundled default; extend
  `installBundled` to accept it; bump `kHouseDefaultsVersion` by one.
- `qml/ExtensionsCatalog.js` — VidLink curated card (`bundled:true`).
- `qml/TheatreApi.js` / `qml/TheatreSeries.qml` — preserve a normalized `malId` alongside `tmdbId`
  (take it from the final Cinemeta/anime record, resetting per load), and pass it in the hosted context.
- `qml/HostedPlayerPage.qml` — on confirmed failure, WRITE a dead-end record; on the failure panel,
  render the offered-fallback button when a sibling provider qualifies; emit a typed fallback request.
- `qml/Main.qml` — route the offered-fallback request to open the sibling provider as a fresh hosted session.
- `qml/SourcesSheet.qml` — already renders whatever `HostedPlayerApi.rowsFor` returns; verify VidLink and
  anime rows appear with the same honest, claimless layout (no change if Task 4 of VidKing was faithful).

**Create**
- `native/engine/DeadEndStore.h` / `.cpp` — a tiny persisted per-(provider,mediaId) negative cache with
  TTL; `Q_INVOKABLE record(provider, mediaId)`, `Q_INVOKABLE bool isDead(provider, mediaId)` (TTL-checked),
  `Q_INVOKABLE clear(...)`. No network, no other capability.
- `tests/hosted_player_vidlink_test.mjs` — VidLink URL/param/anime + `expectedOrigin` + rows assertions.
- `tests/dead_end_store_harness.cpp` — record → isDead true within TTL → isDead false past TTL → per-provider isolation.

---

### Task 1: Register the VidLink provider (URL, origin, anime) — test first

**Files:** Modify `qml/HostedPlayerApi.js`; Create `tests/hosted_player_vidlink_test.mjs`

- [ ] **Step 1 — failing tests.** Assert (exact strings):
```js
eq(mod.embedUrl('vidlink', { type:'movie', tmdbId:27205 }, 83),
   'https://vidlink.pro/movie/27205?primaryColor=e8b923&autoplay=true&startAt=83')
eq(mod.embedUrl('vidlink', { type:'series', tmdbId:1396, season:2, episode:3 }, 41),
   'https://vidlink.pro/tv/1396/2/3?primaryColor=e8b923&autoplay=true&nextbutton=true&startAt=41')
eq(mod.embedUrl('vidlink', { type:'anime', malId:21, episode:1050, subOrDub:'sub' }, 0),
   'https://vidlink.pro/anime/21/1050/sub?primaryColor=e8b923&autoplay=true')
eq(mod.expectedOrigin('vidlink'), 'https://vidlink.pro')
eq(mod.expectedOrigin('vidking'), 'https://www.vidking.net')
// anime row offered on MAL id alone (no TMDB); movie/series still require positive TMDB
eq(mod.rowsFor([{id:'net.vidlink.player',enabled:true,manifest:{resources:['hosted-player']}}],
   { type:'anime', malId:21, episode:1050 }).length, 1)
```
- [ ] **Step 2 — run, verify FAIL** (`node tests/hosted_player_vidlink_test.mjs`; provider `vidlink` unregistered).
- [ ] **Step 3 — implement.** Add a `vidlink` branch to `embedUrl` using VidLink's real params; add
  `expectedOrigin(providerId)` returning the app-owned constant per provider (`vidking`→`https://www.vidking.net`,
  `vidlink`→`https://vidlink.pro`, else `''`); extend `rowsFor` so an anime context with a positive MAL id
  and episode yields a VidLink row even without TMDB. Keep provider data as constants — never manifest-supplied.
- [ ] **Step 4 — run, verify PASS.** VidKing assertions from its own test remain green.
- [ ] **Step 5 — commit** `feat(theatre): register VidLink hosted provider`.

### Task 2: Parameterize the wrapper's trusted origin (security-sensitive)

**Files:** Modify `resources/hostedplayer/host.html`, `resources/hostedplayer/host.js`; extend `tests/hosted_player_contract_test.mjs`

- [ ] **Step 1 — failing contract assertions.** Require: `host.html` CSP `frame-src` lists BOTH
  `https://www.vidking.net` and `https://vidlink.pro` (fixed set, no wildcard); `host.js` reads an
  `expectedOrigin` from the wrapper query and validates the embed URL's origin against it (not a hardcoded
  constant); the `message` handler still requires `event.origin === expectedOrigin` and
  `event.source === iframe.contentWindow`.
- [ ] **Step 2 — run, verify FAIL.**
- [ ] **Step 3 — implement.** Widen the CSP `frame-src` to the two app-owned origins. In `host.js`, accept
  `expectedOrigin` (percent-encoded) in the wrapper query; require `new URL(url).origin === expectedOrigin`,
  and require the pathname to begin with a route this origin actually serves (`/movie/`,`/tv/`,`/embed/movie/`,
  `/embed/tv/`,`/anime/`). Assign the iframe only after validation. The origin is supplied by
  `HostedPlayerPage` from `HostedPlayerApi.expectedOrigin(providerId)` — NEVER from any manifest or event.
- [ ] **Step 4 — run, verify PASS**; VidKing wrapper behavior unchanged (its origin still validates).
- [ ] **Step 5 — commit** `feat(theatre): per-provider trusted origin in hosted wrapper`.

### Task 3: Seed VidLink as a removable bundled extension + carry MAL identity

**Files:** Modify `ExtensionsStore.cpp/.h`, `ExtensionsCatalog.js`, `TheatreApi.js`, `TheatreSeries.qml`; extend the extension + contract tests

- [ ] **Step 1 — failing fixtures/tests.** Seeded roster includes `net.vidlink.player` (`hosted-player`,
  `['movie','series']`, `core:false`); `worldsFor` → `['theatre']`, `isWell` true; contract test requires
  `installBundled` accepts `net.vidlink.player`; `TheatreApi.mapCinemeta` preserves a normalized `malId`;
  `TheatreSeries` has `property int malId: 0`, reset per load, set from the resolved record.
- [ ] **Step 2 — run, verify FAIL.**
- [ ] **Step 3 — implement.** Seed VidLink via the shared bundled-manifest helper; bump
  `kHouseDefaultsVersion` by one (preserve the migration law — added once, removal not resurrected). Add the
  curated card (`bundled:true`). Preserve `malId` in `mapCinemeta`/`mergeMetaFields` and in `TheatreSeries`
  (take it from the final anime/Cinemeta record, not the original provider object), and include `malId` +
  `subOrDub` in the hosted context passed to Sources.
- [ ] **Step 4 — run tests, PASS**; Torrentio + VidKing unchanged, both hosted providers Theatre-only.
- [ ] **Step 5 — commit** `feat(extensions): add removable VidLink + carry MAL identity`.

### Task 4: Dead-end memory (per-provider negative cache with TTL)

**Files:** Create `native/engine/DeadEndStore.h/.cpp`, `tests/dead_end_store_harness.cpp`; modify `CMakeLists.txt`,
`main.cpp`, `qml/HostedPlayerApi.js`, `qml/HostedPlayerPage.qml`, `qml/SourcesSheet.qml`

- [ ] **Step 1 — failing harness.** `record("vidking","tt1")` → `isDead("vidking","tt1")` true now, false
  after simulated TTL (72h); `isDead("vidlink","tt1")` stays false (per-provider isolation); persists across
  reconstruction; `clear` removes it.
- [ ] **Step 2 — build harness, verify FAIL** (store absent).
- [ ] **Step 3 — implement store.** Tiny JSON/settings-backed map keyed `provider|mediaId` → epoch seconds;
  `isDead` returns true only within TTL. No network/shell/other capability. Expose as context property
  `DeadEnds` in `main.cpp`.
- [ ] **Step 4 — wire write.** In `HostedPlayerPage`, on the SAME confirmed-failure path that shows the honest
  panel, call `DeadEnds.record(providerId, mediaId)`. Note (probe `b9c481f`): neither provider emits an
  error/no-source event, so the **20s startup guard — no `play`/`timeupdate` arrived — is the primary and usually
  the ONLY failure signal**; wrapper load failure is the secondary one. Do NOT wait for a provider `error` event
  that will never come. Never record on user-initiated Back or on successful playback (any `play`/`timeupdate`
  clears the guard).
- [ ] **Step 5 — wire read.** `HostedPlayerApi.rowsFor` (or `SourcesSheet` where it composes hosted rows) drops
  a provider's row when `DeadEnds.isDead(providerId, mediaId)`. A row hidden this way reappears once the TTL lapses.
  A brand-new title always shows the row at least once. Add a contract assertion.
- [ ] **Step 6 — run all hosted + Sources tests, PASS.**
- [ ] **Step 7 — commit** `feat(theatre): per-provider dead-end memory with expiry`.

### Task 5: Offered fallback (consented, no cascade) + live verification

**Files:** Modify `qml/HostedPlayerPage.qml`, `qml/Main.qml`, `tests/hosted_player_contract_test.mjs`,
docs; extend the bake-off note

- [ ] **Step 1 — failing assertions.** The honest-failure panel shows a `Try {name} instead` action ONLY when
  a sibling hosted provider is enabled AND can offer this exact media (TMDB, or MAL for anime) AND is not itself
  dead-ended; the panel emits a typed `fallbackRequested(request)`; Main opens the sibling as a fresh hosted
  session; there is NO code path that opens a sibling without that explicit action.
- [ ] **Step 2 — run, verify FAIL.**
- [ ] **Step 3 — implement.** Compute the sibling offer from `HostedPlayerApi.rowsFor(enabled hosted exts, media)`
  minus the failed provider and dead-ended rows; render one button; on click emit `fallbackRequested` with the
  sibling row's request; Main routes it through `openHostedPlayerSession`. Keep `Back to Sources` and `Retry`.
- [ ] **Step 4 — deterministic + native suite** (VidKing suite + VidLink test + dead-end harness + build) PASS.
- [ ] **Step 5 — live smoke (VidLink), with host observation** (same net-log method as VidKing Task 8): a movie,
  a TV episode, and **one anime by MAL id** — verify VidLink URLs/params, progress + Continue parity, the cage
  holds (popups/nav/downloads/clipboard blocked, page+profile destroyed on close), a forced failure shows the
  panel with a working `Try VidKing instead`, and a dead-ended row disappears then returns after the TTL.
  Append the observed third-party host list.
- [ ] **Step 6 — bake-off seed.** Record VidKing-vs-VidLink outcomes for the smoke titles into a bake-off note
  (`docs/.../2026-08-02-hosted-provider-bakeoff.md`), scoring the failure-correlation table (independent redundancy
  vs shared plumbing). This is the Phase-3 deliverable's start, not fleet telemetry.
- [ ] **Step 7 — docs + commit** `feat(theatre): VidLink provider B with offered fallback`.

## Definition of Done
- VidLink is a removable, keyless, enabled-by-default Theatre hosted-player extension using only its
  documented iframe/postMessage interface; disabling/removing it drops its row and Continue path.
- Movie/TV rows key off Cinemeta TMDB; **anime rows key off MAL id**, offered even without a TMDB id.
- The wrapper trusts exactly two app-owned origins; the per-load origin is validated against the registry,
  never a manifest.
- A confirmed VidLink (or VidKing) dead-end suppresses only that provider's row for only that title, and only
  until the TTL lapses; it never hides a sibling or hides permanently.
- The failure panel offers a one-click `Try {sibling}` that never fires automatically.
- Progress/Continue/session/minimize/close/back/security all match VidKing behavior.
- Deterministic tests, native harnesses, build, and the live movie/TV/anime smoke (with observed host list and
  bake-off seed) pass.

## Claude Implementation Prompt (DO NOT DISPATCH until Agent 4 lifts the gate)

```text
GATED: run only after Agent 4 confirms the VidKing hosted-player plan is executed and reviewed.

Work directly in C:\Users\Suprabha\Desktop\Brotherhood\Colosseum on the existing master checkout. Pull
latest master first. Read and execute docs/superpowers/plans/2026-08-02-theatre-vidlink-provider-b.md task
by task, using superpowers:executing-plans and superpowers:test-driven-development. Do not create a branch,
worktree, or subagent workspace. Preserve every unrelated modified/untracked file; stage only the files
named by the current task. Commits are enabled: land each task's scoped commit as written.

This is Provider B on the already-built VidKing hosted-player architecture. Reuse the existing registry,
wrapper, bridge, HostedPlayerPage, sessions, and Progress — do not rebuild them. VidLink stays keyless and
uses only its documented interface (movie /movie/{tmdb}, tv /tv/{tmdb}/{s}/{e}, anime /anime/{mal}/{ep}/{sub},
params primaryColor/autoplay/startAt/nextbutton, origin https://vidlink.pro with NO www). Never scrape or
pre-check availability; the dead-end memory (per-provider, expiring) is the only availability mechanism, and
fallback between providers is always an offered one-click action, never an automatic cascade. Treat the
wrapper origin change as security-sensitive: two fixed app-owned origins in CSP, per-load origin validated
against the app registry, never from a manifest or event.

If any VidKing interface named in this plan does not match the code as actually landed, STOP and report to
Agent 4 — do not improvise the delta. Run each failing test before implementation and each focused test
after. Report back to Agent 4 (player/theatre domain leader) with: commits landed (hashes), tests run with
results, the observed third-party host list, the bake-off seed data, and any deviation with its evidence.
```
