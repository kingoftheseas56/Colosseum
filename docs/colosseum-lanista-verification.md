# Colosseum Lanista Verification — the capability ledger

> **What this is.** The single honest inventory of what the Lanista test bridge can and cannot do
> **today**, for any agent planning or executing verification against the running app. The
> `brotherhood-writing-plans` and `brotherhood-executing-plans` workflows consult this file before
> naming any Lanista action in a plan or claiming any runtime evidence in a report.
>
> **The one rule: never use a Planned capability as if it exists.** If a slice needs something in
> the Planned or Unavailable sections, the slice is **Bridge blocked** — say so, and order the
> smallest bridge prerequisite. Inventing a command, probe, or wait that this file doesn't list as
> Available is a protocol violation, not creativity.
>
> Ground truth as of 2026-08-06, mapped from source (`native/devtools/LanistaServer.*`,
> `native/tools/lanista.cpp`, `native/tools/lanista-mcp/server.py`, `native/main.cpp`). If code and
> ledger disagree, the code wins — and fix this file in the same commit.

---

## AVAILABLE NOW

### Transport

- Named pipe (`QLocalServer`), default name `ColosseumLanista`, override via env
  `COLOSSEUM_LANISTA_PIPE`. User-ACL scoped (`UserAccessOption`); no further auth.
- **One JSON line in, one JSON line out, one command per connection.** Request
  `{"cmd":..., "seq":..., "payload":{...}}`; reply `{"type":"reply","seq":...}`; error
  `{"type":"error","seq":...,"code":"UPPER_SNAKE","message":...}`. Schema id `colosseum.dev.v1.0`.
- Max line 1 MiB (`LINE_TOO_LONG`); idle timeout 10 s (`IDLE_TIMEOUT`, override
  `COLOSSEUM_LANISTA_IDLE_MS`).
- The server is constructed **unconditionally** in `main.cpp` — it is always listening in the
  daily app, Read gate open. Only Drive/Write are env-gated.
- **The daily app and any second instance collide on the default pipe name.** The second listen
  fails quietly. Any test session MUST set a unique `COLOSSEUM_LANISTA_PIPE`.

### Gates

| Gate | Enabled by | Notes |
|---|---|---|
| Read | always on | no env var |
| Drive | `COLOSSEUM_LANISTA_DRIVE=1` | refusal code `DRIVE_DISABLED` |
| Write | `COLOSSEUM_LANISTA_WRITE=1` | **no production command uses Write yet** |
| Selftest cmds | `COLOSSEUM_LANISTA_SELFTEST=1` | absent otherwise (`UNKNOWN_CMD`) |

Gates are enforced centrally in dispatch, checked before any grab is taken.

### Commands (the complete list)

| Command | Gate | What it does | Honest limits |
|---|---|---|---|
| `ping` | Read | schema, pid, pipe, gate states, sorted command list | authoritative capability probe — trust this over any doc, including this one |
| `get-state` | Read | root windows (title, geometry, visible, active) + artifact runDir + **resolved `appDataRoot`/`cacheRoot`** (the isolation-proof seam, added 2026-08-06) | **root windows only** |
| `qml-get` | Read | read named QML properties off an item (by objectName or handle) | property equality only, values as QVariant→JSON |
| `ui-query` | Read | one item's scene rect, visible, enabled, opacity, clippedByWindow | clipping measured against the FIRST root window only |
| `dump-ui` | Read | every item with a non-empty objectName (DFS, depth, scene coords) | unnamed items invisible; no visibility filter |
| `ui-snapshot` | Read | actionable elements with opaque handles, centers, sizes | see "UI model truths" below |
| `ui-click` | Drive | synthesized click at a named item's center | client never supplies pixel coords |
| `ui-keypress` | Drive | key to the focus item of the main window | first key of sequence only; printable-ASCII text |
| `ui-text-input` | Drive | forceActiveFocus + per-char KeyPress | no KeyRelease pairs |
| `ui-scroll` | Drive | wheel event, `dy` (default −120) | no scroll phases |
| `ui-wait-for` | Read | poll one property until **equal** to a value | 50 ms poll, default 3 s timeout, strict equality ONLY — no operators, no compound predicates |
| `invoke-read` | Read | allowlisted C++ method calls | **8 methods**: six `TankobanVolumes` reads + `BiblioImageDiag.rowsForUrl(urlFragment)` / `BiblioImageDiag.recentRows(limitText)` (per-URL image-network rows: status, error, cacheHit, bytes, contentType, timing — newest first; added 2026-08-06). QString args (max 3); returns only list/map/bool |
| `events-tail` | Read | last N lines of the JSONL event log | see "Event log truths" |
| `log-mark` | Read | append a correlation mark to the event log | the ONLY event type that exists today |

### Combined state + capture (grabs)

- Any command's payload may add `"grab": {"target": "window" | "<objectName|handle>", "timeoutMs": N}`.
  Reply gains `grabPath` (PNG in the run dir) + `grabbedAt`.
- **Timing truth:** `target:"window"` is synchronous (`grabWindow()`); any item target is
  **async** (`grabToImage()`) — the pixels land on a *later frame* than the state in the same
  reply. Do not claim state and item-pixels are atomic. `grabbedAt` is stamped at request time.
- Grab deadline 4 s (`COLOSSEUM_LANISTA_GRAB_MS`; per-call `timeoutMs` may only shorten).
  Failure codes: `GRAB_TARGET_NOT_FOUND`, `GRAB_NOT_RENDERABLE`, `GRAB_SAVE_FAILED`, `GRAB_TIMEOUT`.
- **Units:** snapshot/query coordinates are logical/scene; grab PNGs are device pixels. Never
  hardcode a device-pixel ratio.

### Field-learned traps (GLM's Biblio Library slice, 2026-08-06 — read before driving)

- **`ui-query` answers GEOMETRY only** (`rect/visible/enabled/opacity/clipped`). Any other
  property — `rowCount`, `activeTab`, a model value — is `qml-get`. Three iterations were
  lost to asking ui-query for data it structurally does not carry.
- **Targets resolve by `objectName` or handle — NEVER by QML `id`.** A QML id is
  file-scoped and invisible to the bridge; probing one reads whatever item DFS finds (or
  nothing) with no error shaped like "that was an id, not a name".
- **Name collisions resolve DFS-FIRST — including into HIDDEN worlds.** The app pre-warms
  other worlds' trees (Tankoban's warmer builds ~2.5 s after boot), so a generic name like
  `worldTab_library` can resolve to an occluded pill in a DIFFERENT world and your click
  "lands" green while the visible page never moves. **Naming convention, binding:**
  automation objectNames on shared components must be WORLD-NAMESPACED (the world's own
  name prefixed, e.g. `biblioTab_<key>`), never a bare shared stem.
- **Blind gates:** `run` / `session run` / plain commands accept `--verbose`, printing
  every step's full reply body — use it while iterating instead of forcing failures to
  see values.

### UI model truths

- The whole item model is scoped to the **first root `QQuickWindow`**. Secondary windows,
  popups that own their own window, and anything outside that root are invisible and
  ungrabbable through the bridge.
- `ui-snapshot` "interactive" = superclass-chain match (MouseArea/Flickable/TextInput/TextEdit/
  AbstractButton/Button). **A plain `Item` made interactive by a child `TapHandler`/`MouseArea` is
  NOT detected.** Such surfaces need an `objectName` to be actionable.
- **Handles die at the next snapshot — from ANY client.** Every `ui-snapshot` invalidates all
  prior handles globally. A stale handle is a clean `NO_SUCH_ITEM`, never a silent wrong hit.
  `objectName` targets live as long as the item.

### Event log truths

- One shared JSONL file at `<AppData>/lanista/events.jsonl` — **across all launches**, not
  per-run. 5 MiB rotation keeping one predecessor; rotation is not multi-process-safe; append is
  best-effort (a failed open silently drops the line).
- **Exactly one event type exists: `mark`** (from `log-mark`). There are NO lifecycle, route,
  loader, model, image, network, warning, or player events. `events-tail` reads the whole file
  per call.

### Test sessions — `lanista session run` (added 2026-08-06, pilot-proven 13/13)

- `lanista session run <scenario.json> [--exe] [--qml] [--tag <t>] [--drive] [--seed <dir>]
  [--ready-ms] [--keep-going]` — launches a DISPOSABLE tagged app on a unique pipe
  (`ColosseumLanista-<sessionId>`; refuses the daily default), waits for `ping` readiness with
  a **pid match**, **proves isolation from the app's own `get-state` report** (both
  `appDataRoot` and `cacheRoot` must carry the `Colosseum-dltest-<tag>` marker or the session
  kills itself), runs the scenario, pulls grabs, stops graceful-then-kill, and writes a
  `colosseum.session.v1` manifest + `stdout.log`/`stderr.log` into
  `artifacts/lanista-sessions/<id>/`. Exit codes follow the runner contract (infra 4 on any
  start/isolation failure).
- **Empirical correction (pilot, 2026-08-06): `COLOSSEUM_APPDATA_TAG` re-roots the image
  cache too** — `CacheLocation` derives from `applicationName` on Windows and both cache
  users resolve their path after the tag is applied. The earlier "does NOT move
  CacheLocation" caution below is retired; the session controller still asserts it per run
  rather than trusting the rule.
- `--seed <dir>` copies a fixture tree into the tagged AppData root pre-launch. Registry-backed
  QSettings are NOT seedable this way — only file-backed stores.
- Interactive `session start`/`stop` (a session outliving one command) does NOT exist —
  deferred to the MCP-facade arc. All per-card/dynamic logic must live inside a runner verb.

### Seed zoo (`tests/lanista-seeds/`) and the corrected scenario inventory (Slice J0, 2026-08-12)

- **Scenario inventory correction.** The "Existing scenarios" line below (Scenario runner section)
  names only `self_smoke.json`, `self_visual.json`, `app_home.json` — that list predates the Vault
  and Biblio slices. As of 2026-08-12 there are **18** scenario JSONs under
  `tests/lanista_scenarios/`: `app_home`, `biblio_covers_pilot`, `biblio_library_empty`,
  `seed_zoo_smoke` (this slice), `self_smoke`, `self_visual`, 6 × `update_*` (`available`,
  `downloading`, `idle`, `idle_chapter_nav`, `idle_corrupt_sig`, `up_to_date`), and `vault_door`,
  `vault_identify`, `vault_launch_baseline`, `vault_launch_smoke`, `vault_open_recent`,
  `vault_shelves`. Maintain this count in the same commit as any scenario add/remove — it drifts
  fast.
- **`tests/lanista-seeds/`** is the versioned fixture zoo: one folder per real-bug seed, each
  carrying a `seed.json` manifest (`{name, version, provenance, placement, expectedOnBoot}` — see
  `tests/lanista-seeds/README.md` for the full journey contract). A seed is admitted only when a
  real bug's diagnosis produces one — never invented complexity. Founding seed:
  `vault-stale-index-v1/` (promoted copy of `tests/lanista-slice17-seed/`, which stays in place
  untouched — nothing here re-points its referencing scenarios), provenance the stale-index
  boot-re-derivation bug (dossier `Brotherhood/agents/handoff-vault-boot-rederivation-luna.md`),
  healed by the boot-time republish in `e08424b`. Verified with
  `tests/lanista_scenarios/seed_zoo_smoke.json`: isolated session `20260812-162422-89de070e`, 6/6
  green; negative control (corrupted `expectedOnBoot` value) red in `20260812-162637-47e09b0a`,
  restored green in `20260812-163348-17af5173`.
- **Placement mechanism.** `seed.json`'s `placement` array names, for schema completeness, which of
  a seed's subfolders are Roaming- vs Local-rooted — but as of `4ebec25` (2026-08-11, see the Vault
  Open Recent correction above) `session run --seed <dir>` already copies the ENTIRE seed tree
  directly into the tagged AppDataLocation (Roaming) root the app itself resolves. Every store this
  app persists lives under AppDataLocation (`vault/`, downloads, settings, `logs/` via AppLog) —
  none under GenericDataLocation — so in practice every seed to date needs no manual placement step
  at all; `destination: "local"` in the schema is kept for forward compatibility only, not because
  any current store needs it.

### Warning gate (Slice W0, 2026-08-12) - warnings become verdicts

- **What it is.** `tests/warning_gate.ps1` - a runner-side parser, no app change, no second
  `qInstallMessageHandler` (`native/engine/AppLog.cpp` already chains). Input: one or more
  `-LogPath` values (a session's `<AppDataLocation>/logs/colosseum.log` plus the runner's own
  `stderr.log`). Output: `WARNING_GATE_OK` (exit 0) on a clean session, or `FAIL: <line>` named
  per offending line (exit 1), or `FAIL: <schema message>` (exit 2) when the allowlist itself is
  malformed. House sentinel contract, so it composes with every existing `.ps1` runner.
  ```
  pwsh tests/warning_gate.ps1 -LogPath <sessionRoot>/logs/colosseum.log,<runDir>/stderr.log
  ```
  (a single `-LogPath` value may itself be comma-separated - the script splits defensively
  because a calling shell can collapse a quoted multi-value argument into one string before
  PowerShell ever sees it, verified empirically 2026-08-12).
- **Classification rule.** `colosseum.log` is level-classified and authoritative: a `[W]`/`[C]`/
  `[F]` line is offending unless an allowlist pattern matches it; `[D]`/`[I]` lines (and
  multi-line continuations / the session-start banner, which carry no level marker at all) are
  never offending. `stderr.log` carries NO level marker at all (Qt's un-leveled console mirror
  plus raw third-party writes AppLog never sees, e.g. `Cannot load nvcuda.dll`), so a stderr.log
  line is offending unless it is either allowlisted or text-identical (after stripping
  colosseum.log's timestamp+level prefix) to a line colosseum.log already proved was `[D]`/`[I]`.
  A run where every given path is missing is refused (exit 2), never silently `WARNING_GATE_OK`.
- **The allowlist.** `tests/lanista-warning-allowlist.json` - `[{pattern, owner, reason, date}]`.
  `pattern` is a regex matched against the full line text. An entry missing `pattern`/`owner`/
  `reason`/`date` is invalid by schema and the WHOLE gate run is refused (exit 2), never silently
  dropping just that one entry - verified live 2026-08-12 (an entry missing `owner` alone, and
  separately one missing `reason` alone, both rejected by field name).
- **Measured baseline (2026-08-12), the handoff's required "current warning baseline."** Four
  fresh isolated sessions via `lanista session run --tag <t> --ready-ms 60000` (scratch scenario
  JSON under `artifacts/warning-baseline/2026-08-12/scenarios/`, not `tests/lanista_scenarios/` -
  ephemeral measurement tooling, not a permanent regression gate): (1) clean boot, no seed, no
  navigation (`20260812-180124-c3bc95c9`); (2) boot + one click into Tankoban
  (`20260812-180159-990382a4`); (3) boot + one click into Biblio, with the named
  `biblioDiscoverPage.loading` real wait (`20260812-180235-5ffd4637`); (4) boot + one click into
  Theatre - no named settle surface exists for Theatre today, so this pass is click + round-trip
  read only, not a claimed completion signal (`20260812-180305-2389417b`). Each world pass was its
  own fresh-boot session (never returning home / clicking a second pill) to avoid the documented
  DFS name-collision trap between a hidden pre-warmed world's pill and the visible one. Classified
  distinct lines, ALL known-noise, ZERO legitimate red found in what these four sessions visited:

  | Pattern (see allowlist for full reason) | Where seen |
  |---|---|
  | `Cannot load nvcuda.dll` | stderr.log, all 4 passes (named known-noise example in the W0 plan) |
  | `QIODevice::read (QNetworkReplyHttpImpl): device not open` | colosseum.log `[W]`, boot-nowhere/Biblio/Theatre, teardown-only |
  | `QRhiGles2: Failed to make context current` | colosseum.log `[W]`, all 4 passes x2, teardown-only |
  | `...items in the process of being created at engine destruction` | colosseum.log `[W]`, Tankoban/Biblio, teardown-only |
  | `qt.sql.qsqldatabase: QSqlDatabase requires a QCoreApplication` | colosseum.log `[W]`, all 4 passes x4-6, teardown-only |
  | `^lanista: listening on ` | harness stderr only (see below) - the bridge's own routine startup line, `[I]` in the real app |

  Every one of these clusters in the final second before graceful session stop (Qt
  RHI/QML-engine/SQL teardown ordering, or a startup announcement) - none is a mid-session,
  user-visible symptom. The classification table above is the durable record (the known-noise
  half is ALSO durable via `tests/lanista-warning-allowlist.json` itself, which carries
  pattern+owner+reason+date per entry); raw per-session logs are ephemeral evidence under
  `artifacts/warning-baseline/2026-08-12/` (gitignored, `.gitignore:34`) and are not the
  deliverable - this table is. **Explicitly NOT claimed:** any statement about the warning
  behavior of surfaces these four sessions did not visit (Vault, Theatre beyond one click, deep
  navigation within any world).
- **Two-sided negative control (mandatory, performed live 2026-08-12).** (a) the Tankoban
  baseline's real `colosseum.log`+`stderr.log` against an EMPTY allowlist -> `FAIL:` naming every
  real line (8 distinct offenders). (b) the same log pair against the full classified allowlist ->
  `WARNING_GATE_OK`. Both outputs preserved under
  `artifacts/warning-baseline/2026-08-12/negative-control/direction-a-red.log` and
  `direction-b-green.log`. Schema guard proven separately: an allowlist entry missing `owner`
  alone, and separately one missing `reason` alone, both rejected (exit 2, entry index + field
  name named) - preserved as `schema-guard-missing-owner.log` / `schema-guard-missing-reason.log`
  in the same directory.
- **Wired caller.** `tests/test_lanista.ps1` is the one caller wired per the plan (opt-in only -
  no other runner touched). Its `--serve` harness process (`lanista_harness.exe`) has NO AppLog
  (it never links `native/engine/AppLog.cpp` - see its `native/CMakeLists.txt` target), so only
  its own `stderr.log` (redirected to `artifacts/test-lanista/harness-stderr.log`) is gated; the
  harness's routine `lanista: listening on ...` startup line needed its own allowlist entry for
  exactly this reason (it is `[I]`-level and silent in the real app's colosseum.log, but has no
  level at all in the harness's raw console capture). Proven both directions live: a clean run is
  green (`WARNING_GATE_OK`), and removing that one allowlist entry turns the SAME wired run red
  (`FAIL: ... lanista: listening on ...`) - then restoring the allowlist turns it green again.

### Named automation surfaces (added 2026-08-06)

- `modePill_<Tankoban|Biblio|Theatre|Vinyl>` (TopBar mode switch — plain Items with child
  MouseArea, invisible to `ui-snapshot`'s interactive walk, clickable BY NAME only)
- `bootSplash` (wait `visible == false` before driving ANYTHING — clicks land "green" on the
  occluded tree while it owns the screen; proven by pixels in pilot run 1)
- `biblioDiscoverPage` with `loading` (bool) and `freshness` (string: `"bundled"` = one-book
  built-in fallback wall; `"fresh"/"aging"/"stale"` = real catalog rows — wait for `"fresh"`
  before asserting on catalog content; proven necessary in pilot run 3)
- `discoverCard_<itemId>` on materialized Discover delegates (world-neutral; skeletons
  unnamed), with `discoverCard_<id>_art` (RoundedPosterImage: `activeSource`, `exhausted`,
  `candidateIndex`, `sources`, `ready`) and `discoverCard_<id>_art_img` (the inner `Image`:
  `source`, `status`, `sourceSize`, `paintedWidth/Height`). **Only delegates near the
  viewport exist** — GridView virtualization; scrolling materializes more.
- **Vault launch entry points (Slice 8, added 2026-08-09):** `taskbarOpenMedia` (the taskbar
  Open Media… control — visible only while the dock is open; `ui-query` for visible/enabled/
  clippedByWindow, `ui-click` opens a NATIVE dialog the bridge cannot see, so clicking it is
  human-witnessed, not driven) and `localLaunchState` (an invisible status Item: `openCount`
  `lastRouteKind` `lastRejectCategory`, exact-value waitable via `qml-get` — the launch
  pillar's machine-checkable seam; idle at boot = `openCount 0`, empty strings). Scenario
  `tests/lanista_scenarios/vault_launch_smoke.json` proves the assembled app exposes the
  control (7/7 in isolated session, 2026-08-09). The native picker / OS drag-drop / Ctrl+O
  are outside the first-root-window model — human-witnessed by design.
- **Vault Open Recent (Slice 9, added 2026-08-09):** `openRecentDisclosure` (the caret on the Open
  Media control that opens the panel), `openRecentPanel` (the same-window popup; property `rowCount`
  is exact-value waitable), `openRecentRow_<n>` (one per recent entry), `openRecentClear` (wipe
  shortcuts). Scenario `tests/lanista_scenarios/vault_open_recent.json` proves the panel renders a
  seeded recent list and a row click reopens the file into the vault comic reader (`comicReaderShell`
  `pageCount == 3`, `seriesId` matches `^vault:`), `localLaunchState.openCount == 1` — 13/13 in an
  isolated session (2026-08-09). **⚠ `--seed` limitation — FIXED 2026-08-11 (Vault Slice 17,
  `4ebec25`):** at the time of that 13/13 run, `session run --seed` copied into
  `GenericDataLocation` (`AppData/Local`), so AppDataLocation stores (Roaming) — including
  VaultRecent's `open-recent.json` — were not reached, and the recent list had to be pre-placed by
  hand at `<Roaming>/Brotherhood/Colosseum-dltest-<tag>/vault/open-recent.json`. Commit `4ebec25`
  repointed the copy target to the same AppDataLocation-derived root the app itself resolves
  (`QStandardPaths::AppDataLocation` + parent dir + `Brotherhood/Colosseum-dltest-<tag>`), so
  **`--seed <dir>` now reaches Roaming stores directly — manual pre-placement is no longer
  required** for content under a seed's `vault/` (or `logs/`) subfolder. Re-verified empirically
  2026-08-12 (Slice J0): `vault_open_recent.json` replayed 13/13 using only `--seed <dir containing
  vault/open-recent.json>`, no manual copy step (session `20260812-164458-d6b4d550`). Also pass
  `--seed` an ABSOLUTE dir (a relative one nests under its own path). Reading progress is
  registry-backed QSettings — not seedable at all — so reopen-resume-at-page and
  completed-video-restart are human-witnessed.
- **Vault Browse detail sheet (Slice 7, added 2026-08-13):** `vaultBrowseSheet` (the same-window
  overlay — never a Window/Popup; property `visible` is exact-value waitable, plus the plain
  scalars `copiesHeld`/`companionsCount`/`extrasCount`/`identityLabel` the Lanista vocabulary
  reads instead of walking nested arrays by dot-path), `vaultBrowseSheetCopy_<n>` (one row per
  copy — `quality`/`where`/`sizeText`/`away`), `vaultBrowseSheetPlay` (routes through the shipped
  `localLaunchState` seam), `vaultBrowseSheetBack` (dismiss; also reachable by Escape/Backspace
  at the Quick Test layer only — keyboard is outside the bridge by design, per the ledger's
  standing law). A Film grid card click opens the sheet instead of routing straight to Play;
  `tests/lanista_scenarios/vault_browse_smoke.json` was extended (still against the real
  `browse-face-smoke` fixture — the Spider-Man shape: 1 copy, 2 companions, 2 extras) to open the
  sheet, `qml-get` its counts and the one copy's filename-parsed quality line against fixture
  truth, item-grab it, dismiss via Back and prove the grid's node count is untouched, then
  reopen and drive Play through to `localLaunchState.openCount == 1` /
  `lastRouteKind == "video"`. Isolated session, 31/31 (2026-08-13). The Spider-Man fixture's
  primary video file was swapped from an 18-byte text stub to real ffmpeg-generated decodable
  bytes (the same file `colosseum.qttest.vault_enricher` already uses) specifically so this
  Play proof is a real admitted launch, not a vacuous reject — `LocalLaunch` sets
  `lastRouteKind`/`openCount` from extension-based classification alone in the reject path too,
  but a genuine end-to-end proof needed a file the admission probe actually decodes. Both
  ordered regressions replayed green in fresh isolated sessions the same day:
  `vault_launch_smoke.json` 7/7, `vault_open_recent.json` 13/13 (seeded by hand — see its own
  entry above — with `tests/fixtures/tankoban/tiny-volume.cbz`, no committed seed directory
  existed for this scenario at the time). Warning gate: `vault_browse_smoke`'s own session is
  `WARNING_GATE_OK`; the two regression sessions each surfaced one pre-existing, unrelated
  signal not on the allowlist (a Continue-rail live-network image 404 to `metahub.space` for an
  unrelated catalogue id, and one `QMetaObject::invokeMethod: No such method
  QObject::writeSnapshot` warning) — named honestly, confirmed absent from every Slice 7 file,
  not triaged further here (out of this slice's fence).
- **Vault Browse series drill (Slice 8, added 2026-08-13):** no new automation surfaces — the
  drill reuses every objectName Slice 5-7 already named (`vaultBrowseCard_<key>`,
  `vaultBrowseCrumb`, `vaultBrowseGrid`, `vaultBrowseRailRoot_<n>`), because a season/show/folder
  card and an episode/clip card share the SAME click→pushCrumb path already wired. What's new is
  the SCALE fixture: `tests/fixtures/vault/browse-face-smoke`'s Gintama folder grew from 3 to 300
  stub episode files (real disk holds 367) for a genuine virtualization proof at runtime, not
  just the Qt Quick Test's seeded-model layer. `tests/lanista_scenarios/vault_browse_smoke.json`
  was extended (same `browse-face-smoke` fixture, same seed) to drill The Wire's show card to
  its seasons band (grid count 1 — season-presence honesty: the folder claims 5 seasons, disk
  holds only Season 4 — the physical fact's exact shipped string is `Season 4 only`, capitalized,
  differing from casual lowercase references elsewhere; asserted as the exact string per the
  plan's own instruction to compose the expected string, not a substring match), then Season 4's
  episode wall (grid count 2, first card's `physicalFact` exact `S4:E1 · 1080p BluRay`), then
  separately the Gintama-scale show — which drills DIRECTLY to its 300-episode wall with NO
  season band, because the real disk shape has no season subfolders (absolute-numbered, flat) —
  proven live, not assumed: `tst_vault_kit.cpp`'s own Slice-1 test already documents "no season
  band — there is no season subfolder to hold one" for this exact fixture shape. Ten realistic
  `ui-scroll` wheel-notch events (`dy: -800`, not one extreme delta) reveal episode 22 (outside
  the initial ~1-18 window the grid renders unscrolled); ascending to root and redrilling WITHOUT
  a fresh scroll finds episode 22 already there, proving grid persistence across drill depth
  (design §4.8). Item-grabs preserved for the seasons band and both episode walls. Isolated
  session (tag `s8final`), 70/70 green 2026-08-13 (the extended scenario, replayed from a clean
  boot — the earlier Slice 5-7 steps replay unchanged as part of the same run). Two live-driving
  lessons, folded into the scenario rather than left as tribal knowledge: (1) `ui-wait-for` never
  replies `{matched:false}` — it only ever replies `matched:true` or fails `WAIT_TIMEOUT` — so an
  attempted "prove X is NOT yet there" step reading `matched == false` is invalid against this
  bridge (the ledger's own documented absence-assertion gap, UNAVAILABLE section); removed, the
  Quick Test's delegate-count assertion covers virtualization's negative half instead. (2) one
  massive single `ui-scroll` (`dy: -16000`) against the 300-row grid occasionally left a residual
  flick-momentum artifact that fought the grid's own scroll-restore when a level-changing click
  followed immediately after (an empty-looking grid, auto-grab preserved as evidence in an
  earlier iteration) — replaced with many small, realistic-magnitude scrolls, which replayed
  clean across repeated runs. Warning gate: `WARNING_GATE_OK` on the clean run's own session
  logs (`colosseum.log` + `stderr.log`); a separate `--drive` attempt under
  `QT_QPA_PLATFORM=offscreen` was tried per a standing-instruction request to stop opening
  windowed sessions on Hemanth's desktop — **result: offscreen is not viable for this app's
  Lanista-driven scenarios today**, and the breakage is structural, not limited to item-grab
  capture (the ledger's existing "Qt/D3D is uncapturable headless" trap): roughly half the
  property waits/reads return no reply at all (`WAIT_TIMEOUT` or an outright empty response)
  starting from the FIRST substantive interaction (opening the detail sheet), both under the
  default RHI and under a forced `QSG_RHI_BACKEND=software` (which additionally ran markedly
  slower, >120s versus ~30-60s windowed for the same 70 steps, without fixing the breakage).
  Windowed sessions remain the default for Vault browse-face scenarios until this is
  investigated further; named here rather than worked around silently. `vault_launch_smoke.json`
  replayed 7/7 green in a fresh isolated session the same day; `vault_open_recent.json` was not
  replayed this slice (its seed needs hand-placing per the Slice 7 note above, and Slice 8's own
  regression list names only `vault_browse_smoke` + the scroll-restore behavior).

- **Vault Browse empty states (Slice 9, added 2026-08-13):** `vaultBrowseGridEmpty` (the
  same-window component inside `vaultBrowseGrid`, visible only when `count == 0` and the Hidden
  shelf is not active; plain scalars `cause`/`headingText`/`bodyText` are the Lanista vocabulary
  — no walking nested text elements by objectName). Three of the design’s four empty causes
  (§4.5) are reachable live; the fourth (“filtered”) is deliberately never produced — no
  filter control has shipped on the Browse face, named honestly rather than invented for this
  slice. Three new isolated scenarios, each a fresh session:
  `tests/lanista_scenarios/vault_browse_no_storage.json` (NO `--seed` at all — the pre-existing
  onboarding screen `vaultDropSurface` owns this cause, unchanged; `vaultBrowseFace` never
  renders) 7/7; `tests/lanista_scenarios/vault_browse_empty_folder.json` (new fixture
  `tests/fixtures/vault/browse-empty-folder-seed`, a confirmed root at a real on-disk directory
  holding only `.gitkeep` — VaultKit’s own kind classifier treats a suffix-less dotfile as
  non-media, so it never becomes a row) 9/9; `tests/lanista_scenarios/vault_browse_allaway_empty.json`
  (new fixture `tests/fixtures/vault/browse-allaway-empty-seed`, a confirmed root at a path that
  has never existed, no `index-v1.sqlite` seeded — zero durable rows) 9/9. The all-away-empty
  fixture found a real bug live: `VaultBrowseAway::ownerRootAway` reads the away flag off an
  EXISTING index row, so a root that was NEVER scanned while present has no row to carry that
  flag and the cause read as `emptyFolder` instead of `allAway` until
  `VaultLibrary::browseEmptyCause()` also checked live `QDir::exists()` (see
  `VaultBrowseEmpty::isLevelAway`, `colosseum.qttest.vault_browse_empty`). A second real bug,
  also found only by driving live: the new empty-cause QML bindings recomputed on EVERY
  navigation regardless of whether the grid had anything to show, doubling the cost of walking
  Gintama’s 300-episode directory and pushing `vault_browse_smoke.json`’s own “redrill” wait past
  its 15s timeout — fixed by gating both bindings on `browseGridRows.length === 0` first.
  Regressions replayed in fresh isolated sessions: `vault_browse_away.json` (Slice 6’s own
  away-tiles-visible contract, the scenario closest to this slice’s own away-detection fix)
  10/10; `vault_launch_smoke.json` 7/7. `vault_browse_smoke.json` (Slice 8’s 68-step
  Gintama-scale scenario) was replayed 8 times across this slice and never failed at the same
  step twice (57, an INFRA boot timeout, 62 ×2, 68) while its own session logs show heavy
  unrelated live network traffic (434 requests to `live.metahub.space` plus several other hosts,
  Continue-rail image loading unrelated to Vault) — confirmed via a direct A/B test against the
  pre-Slice-9 `qml/VaultPage.qml` (checked out from the parent of the Slice 9 QML commit,
  temporarily swapped in, rebuilt, replayed) that the SAME scenario also fails under this
  machine’s current load, proving the flake pre-exists this slice. Every step through the detail
  sheet/rail/breadcrumb/drill assertions — everything this slice’s own gating change touches —
  passed clean on every single run; only Slice 8’s own late virtualization/redrill/reopen
  assertions were affected, at a different point each time — not silently skipped or re-run-
  until-green. `vault_open_recent.json` needs a hand-placed seed this slice does not touch any
  code path of — not replayed, named honestly. Warning gate: `WARNING_GATE_OK` on all three new
  empty-cause session logs plus the away and launch-smoke regression logs;
  `vault_browse_smoke.json`’s own log fails the gate on exactly the one pre-existing,
  already-documented `live.metahub.space` 404 pattern (unrelated `qml/LibraryPage.qml`, not any
  Vault file) — the only offender, not a new one. Keyboard reach (arrow traversal, Enter,
  Backspace, Tab-to-rail, the focus ring) is Test-reported at the Quick Test layer only
  (`tst_vault_browse_page.qml`) — the ledger’s own standing law that `ui-keypress` is
  first-key-only means keyboard proof deliberately never lives here; it folds into Slice 10’s
  eyes-on (Hemanth’s hands on real keys).

### Scenario runner (`native/tools/lanista.cpp`)

- Pure client for every verb except `session run` (above); `run`/`suite` still require the
  app booted externally.
- A step's client deadline honors its own `payload.timeout_ms` (+5 s slack, floor 10 s) —
  long `ui-wait-for`s no longer die at a flat 10 s cap as phantom INFRA (fixed 2026-08-06).
- Verbs: single command (`lanista <cmd> k=v [--grab target]`), `run <scenario.json>
  [--keep-going]`, `expect <cmd> <dot.path> <op> [value]`, `bless <target> <golden>`, `suite
  [--dir] [--out]` (JUnit + Markdown + failure PNGs), `brief <arc>` (eyes-on gallery).
- Exit codes: 0 pass · 1 red · 2 usage · 4 infra (no pipe / timeout) · 5 scenario error. A
  malformed scenario is exit 5, never a silent green.
- Assertions: `exists ==  != contains matches >= <= > <` over dot-paths (array indices ok);
  numeric compares as doubles.
- Visual: 9×8 dHash goldens in `tests/lanista_goldens/`, default max Hamming distance 6;
  null/unreadable PNG on either side is a hard FAIL. **dHash detects broad drift only — it cannot
  prove a cover is sharp, correctly sourced, or large enough.**
- Every failing step auto-grabs (`grab_on_fail`, default whole window) and prints the evidence
  path.
- Existing scenarios (harness-fixture-bound): `self_smoke.json`, `self_visual.json`; real-app,
  boot-first: `app_home.json`. **Full corrected 18-scenario inventory:** see "Seed zoo... and the
  corrected scenario inventory" above.

### MCP adapter v0 (`native/tools/lanista-mcp/server.py`, registered in `.mcp.json`) — Slice F, 2026-08-12

- **The deadline flaw is closed.** The old adapter opened the named pipe directly and did an
  UNBOUNDED blocking read (`pipe_call()`) — a hung app hung the adapter forever. v0 NEVER touches
  the pipe from Python. Every tool call — the 8 new ones AND the 3 legacy ones — shells the
  existing `lanista` CLI (`native/build-msvc/lanista.exe`) with an explicit `--timeout`, plus a
  Python-side `subprocess` timeout as a hard backstop beyond the CLI's own deadline. One
  automation stack, not two: the CLI's hardened QLocalSocket client (connect/write/drain-on-
  disconnect) is reused, never re-implemented. Stdlib only (plus `ctypes` for one Windows API
  call, see session_stop below) — no SDK, no new dependency.
- **Protocol ruling (unchanged, do not revisit without a fresh plan):** still the hand-rolled
  JSON-RPC `2024-11-05` base, no Tasks, no SDK. All 11 tools are plain `tools/call`.
- **Eight new typed tools**, plus the original 3 kept working unchanged in name and target:
  - `session_start(seedName?, tag?, drive?)` — an INTERACTIVE session the adapter itself owns
    (spawns `colosseum.exe` directly via `subprocess.Popen`, not through `session run`, which is
    self-contained/disposable). Generates a UNIQUE `ColosseumLanista-<sessionId>` pipe every
    time — the bare daily default is refused UNCONDITIONALLY, checked BEFORE any process is
    touched (same law as the CLI's `session run`, and equally unreachable by construction under
    normal generation — see "Negative controls" below for how this was actually exercised).
    Resolves J0 seed placement by copying the ENTIRE `tests/lanista-seeds/<seedName>/` tree into
    the tagged Roaming AppData root (`%APPDATA%/Brotherhood/Colosseum-dltest-<tag>` — org
    `Brotherhood`, app `Colosseum-dltest-<tag>` under `COLOSSEUM_APPDATA_TAG`, `main.cpp:520-560`),
    identical in effect to `session run --seed`. Readiness = `ping` until PID match (2s per-call
    CLI deadline, 60s overall — the ledger's own operational note: the 30s CLI default times out
    on a loaded box). Isolation asserted from the app's OWN `get-state` report (`appDataRoot` +
    `cacheRoot` must both carry `Colosseum-dltest-<tag>`), killing the session on mismatch — the
    same law `session run` enforces. **One live session at a time in v0**, gated two ways: an
    in-memory dict for the adapter's own process, and a `session.json`-derived pointer file
    (`artifacts/lanista-sessions/_mcp-active.json`) checked via a live-pid probe (`tasklist`) so a
    SECOND adapter process — or the same one restarted after a crash — refuses to double-book
    rather than trusting a stale file.
  - `session_stop()` — graceful (broadcasts `WM_CLOSE` to the child's top-level windows via
    `ctypes`/`user32.dll`, the same path Qt's `QProcess::terminate()` takes on Windows; Python's
    own `Popen.terminate()` calls `TerminateProcess` directly with no grace, so this is done by
    hand) then kill after an 8s bound. Logs preserved; manifest records which path fired.
  - `act(action, target, key?, text?, dy?, timeoutMs?)` → `ui-click` / `ui-keypress` /
    `ui-text-input` / `ui-scroll`, flat k=v CLI payloads.
  - `get(target, props, timeoutMs?)` → `qml-get`. **`props` is a JSON array — the CLI's plain
    `k=v` mode structurally cannot build one** (`payloadFromArgs` only types flat scalars).
    Resolved by writing a one-step scratch scenario and running `lanista --verbose run
    <scratch>.json`, which prints the step's full reply body to stdout regardless of pass/fail —
    the documented CLI facility for exactly this ("Blind gates" trap note above). No `expect`
    clause is set; this is data retrieval, not an assertion. Scratch files land under the live
    session's own `artifacts/lanista-sessions/<id>/scratch/` (evidence, not cleaned up).
  - `snapshot(timeoutMs?)` → `ui-snapshot`.
  - `wait_for(target, prop, value, timeoutMs?)` → `ui-wait-for`. The CLI's OWN client deadline
    must outlive the server's poll deadline (same floor+slack rule the scenario engine already
    uses: 10s floor, `timeoutMs + 5s`) — a naive `--timeout` equal to `timeoutMs` would make the
    CLI give up before a legitimately-long server-side wait finishes.
  - `grab(target, timeoutMs?)` → `get-state --grab <target>` (the CLI's own dedicated flag —
    the only way to reach `get-state`'s nested `{"grab":{"target":...}}` payload shape through
    plain k=v mode). The CLI forces a flat 10s client deadline for any grab regardless of
    `--timeout` (`main()`: `grabName.isEmpty() ? g_timeout : 10000`); the adapter's own backstop
    accounts for that floor so a short caller-supplied `timeoutMs` can't make Python kill the
    subprocess before the CLI's internal wait finishes.
  - `warnings()` → runs W0's `tests/warning_gate.ps1` (via `powershell.exe -File`, not `pwsh` —
    not installed on this box; the gate script is written to be ANSI-safe for Windows PowerShell
    5.1 too, see its own header) against the active-or-last-stopped session's own
    `<appDataRoot>/logs/colosseum.log` + its `stderr.log`. Does not reimplement W0's parsing —
    calls the gate script exactly as documented above.
  - `lanista_call` / `lanista_grab` / `lanista_snapshot` — **unchanged names, unchanged target**
    (whatever `COLOSSEUM_LANISTA_PIPE` resolves to at call time — daily app or an externally-
    managed session — exactly as before), reimplemented on the same deadline-safe transport.
- **Test-only hooks, deliberately NOT in the public `inputSchema`:** `session_start` accepts
  `_forcePipe` and `_forceIsolationMismatch` (read via `args.get(...)`, never schema-validated —
  matching this file's existing lightweight-dispatch style). They exist ONLY to exercise two
  guards that a normal call can never organically reach — the pipe is always a generated
  `ColosseumLanista-<sessionId>`, never the bare default, and the isolation check always compares
  against the tag actually launched with — mirroring the CLI's OWN equivalently
  unreachable-by-construction default-pipe guard (`lanista.cpp` : `if (s.pipe ==
  "ColosseumLanista")`, dead code under its own id-always-unique generation). Forcing the value is
  the honest way to negative-test a "this should never happen" guard, the same way a unit test
  forces an edge case; both negative controls (a) and (c) below used them, exercising the SAME
  production code path a normal call runs, not a mock.
- **Ground-truthed live bug, found and fixed in this slice:** the initial drive hit a REAL app
  crash (`STATUS_ACCESS_VIOLATION`, exit code `3221225477`) partway through a Vault-page session
  (immediately after three repeated `HouseScrollBar.qml:26: QML Theme: Cannot find member data`
  warnings — outside this slice's fence to diagnose or fix; flagged for the record, not
  Slice-F's to touch). The crash exposed an unguarded `proc.kill(); proc.wait(timeout=5)` in three
  `session_start` failure paths and one in `session_stop`: a slow-to-die child could raise an
  uncaught `TimeoutExpired`, turning a clean coded tool error into an ugly protocol-level one —
  precisely the kind of deadline gap this slice exists to close. Fixed with a `_kill_and_wait()`
  helper that bounds and swallows the wait. Re-verified clean on a fresh healthy session
  afterward (see verification below) — the crash itself is an app-level fact, independent of the
  fix, and is not claimed resolved by it.
- Assumes adapter and app share a filesystem; grab PNGs are never cleaned up (unchanged limit).

**Verification (Slice F, 2026-08-12).** Driven via real JSON-RPC over stdio (`python
native/tools/lanista-mcp/server.py` as a subprocess, not a host MCP client — neither available
chat had the `lanista_*` tools loaded, the plan's documented fallback), transcripts under
`artifacts/lanista-sessions/mcp-drive-20260812/` (gitignored, evidence only):
- **Main drive** (`transcript.jsonl`, session `20260812-191242-471215e6`, seed
  `vault-stale-index-v1`): `session_start` → `snapshot` (91 elements) → `act` click
  `taskbarVaultDoor` → `wait_for` `vaultState.pageOpen == true` (matched) → `get` on `vaultState`
  + `localLaunchState` (real prop values returned) → `grab` window (real PNG) → `warnings()`
  (correctly returned `FAIL` — caught a real unsuppressed `HouseScrollBar.qml` warning, proving
  the gate isn't a rubber stamp) → the app crashed (see above) → `session_stop` correctly recorded
  the crash exit code without hanging.
- **Negative control (a)** — `session_start` with the forced default pipe: refused before any
  process touched, `isError: true`, `DEFAULT_PIPE_REFUSED`.
- **Negative control (b)** — `act()` against no session (0.00s) and, after a clean `session_stop`
  in the follow-up run (`transcript2.jsonl`), `act()` against a stopped session (0.00s both
  times): clean coded `NO_SESSION` error, never a hang.
- **Negative control (c)** — `session_start` with `_forceIsolationMismatch`: real child spawned,
  became ready, isolation check deliberately mismatched against the REAL reported roots, killed
  (7.0s elapsed) — `tasklist` confirmed the named pid was no longer alive afterward.
- **Regression, legacy 3 tools** — first attempt hit the crash mid-session (all 3 timed out
  cleanly at their bounded deadlines — correct behavior against a dead pipe, but not proof the
  tools work against a HEALTHY one); re-run on a fresh session (`transcript2.jsonl`) after the
  `_kill_and_wait` fix: `lanista_snapshot` (91 elements), `lanista_call ping` (schema/pid/gates),
  `lanista_grab` (real PNG) all answered correctly.
- **Regression, CLI path** — `lanista session run tests/lanista_scenarios/vault_launch_smoke.json
  --drive --ready-ms 60000` (a fresh tag): 7/7, matching the ledger's prior documented result —
  the scenario runner and the app are fully undisturbed by this slice (pure-Python, zero native
  changes).

### Isolation mechanisms that exist

- `COLOSSEUM_LANISTA_PIPE` — unique pipe per instance. **Required** for any test session.
- `COLOSSEUM_APPDATA_TAG=<tag>` — re-roots every `AppDataLocation` store (settings, indexes,
  downloads, lanista logs/runs) **and `CacheLocation` (the image cache included)** to
  disposable `Colosseum-dltest-<tag>` siblings — both derive from `applicationName` on
  Windows, verified empirically by the pilot's isolation assert (2026-08-06). Does not change
  the pipe name; `session run` sets the pipe itself.
- `dev.bat` isolates **nothing**: no pipe override, no data tag, no gates — it shares the daily
  app's data, cache, and default pipe. It is a live-reload convenience, not a test session.

---

## Updater runtime coverage (2026-08-08)

The auto-update slice now has two disposable, test-key-only sessions driven by the existing
`session run` bridge. `tests/test_update_lanista.ps1` configures `COLOSSEUM_UPDATE_TESTING=ON`,
passes absolute fixture paths, gives each run a unique tagged root, and restores the shipping
configuration (`COLOSSEUM_UPDATE_TESTING=OFF`) in `finally`.

| Scenario | Seed / assertions | Evidence |
|---|---|---|
| `tests/lanista_scenarios/update_available.json` | signed `Available` chronicle; boot wait, taskbar notification/reveal, click-through to `colosseumUpdatePage`, exact `automationState`/`automationVersion`/primary label, user-facing status copy, actionable hero, whole-window grab | Runtime-validated in isolated session `20260808-223629-945d25ca`; `12 steps, 0 failed`; grab `.../seq112-1.png` |
| `tests/lanista_scenarios/update_up_to_date.json` | signed latest release; `UpToDate` state, no-update primary label/action, retained highlights/status, whole-window grab | Runtime-validated in isolated session `20260808-223658-686af06c`; `13 steps, 0 failed`; grab `.../seq113-1.png` |

The sessions prove only the assembled test-key build and disposable tagged roots. They do not
claim GitHub network reachability, installer installation, or aesthetic approval of the update
chronicle; those remain unit/eyes-on concerns.

The committed smoke seed keeps the release highlight list empty so the route/state contract stays
minimal; `tests/qml/tst_update_page.qml` carries the non-empty feature-card, long-copy, fallback,
and reduced-motion cases. Hemanth's eyes-on pass is still required for the real release artwork and
final card composition.

## PLANNED (designed, NOT built — using any of these is a Bridge blocked violation)

From the preflight capability guide (2026-08-06). Ordering is decided in the Test Session +
Biblio Image Diagnostics decision brief (Brotherhood repo, `agents/`).

1. **Deterministic test session** — client-owned disposable app process: unique pipe, isolated
   data AND cache, explicit gates, stdout/stderr/crash capture, readiness wait, graceful stop,
   machine-readable session manifest.
2. **Semantic UI contract** — declarative `testId`/role/label/actions metadata surviving delegate
   recycling; window/popup/focus/modality model.
3. **Structured event plane** — per-session typed events (route, loader, model, image, work,
   warnings, WebEngine, player) + `events-wait` predicate wait.
4. **Typed domain-probe registry** — versioned read-only probes replacing growth of the
   `invoke-read` allowlist (image diagnostics, route state, downloads, reader, player, cache
   health).
5. **Explainable capture** — request/completion frame identity, visual-idle wait, crop/mask,
   exact + perceptual compare, diff/heatmap, decoded-dimension and sharpness checks.
6. **Act + Observe transaction** — one call correlating action → semantic completion → events →
   after-state → pixels into a single timeline. No retries, no sleeps inside.
7. ~~**MCP facade**~~ — **DELIVERED v0, Slice F, 2026-08-12.** See "MCP adapter v0" under
   AVAILABLE NOW above: 8 typed tools (`session_start`/`session_stop`/`act`/`get`/`snapshot`/
   `wait_for`/`grab`/`warnings`) with real deadlines on every call, refusing the daily-app pipe
   unconditionally. Still v0-scoped: no Tasks, no protocol bump (deferred to the Night Watch),
   session ownership is in-adapter only (no cross-host handoff).
8. **WebEngine + media observation** — read-only URL/DOM-readiness/console and
   source/buffering/seek probes behind the proper gates.
9. **`window-set-state` (Drive-gated)** — minimize/restore/normal via the real QWindow
   path, demanded by the three-layer minimize/restore regression (test ledger, slice 7):
   the bridge cannot reach the Windows taskbar, so restoring a minimized session is
   impossible today and that slice's runtime layer is Bridge blocked on exactly this.
   **Named alternative shape (tooling map, 2026-08-06):** an outside-the-process
   FlaUI/winapp prototype — it acts where in-process code can't (real taskbar, real
   focus) and verifies what Windows shows the user. Decide in-process vs. outside when
   the demand's customer arrives; see `docs/colosseum-verification-tooling-map.md`.

## UNAVAILABLE (nothing designed will change this soon — plan around it)

- **A per-card WALK/JOIN.** The halves exist separately (per-URL rows via
  `BiblioImageDiag`, per-card QML truth via the named `discoverCard_*` chain) but nothing
  enumerates the wall's cards and joins the two into per-card verdicts — scenario JSON is
  static and cannot loop over names it discovers at runtime. Needs a runner verb.
- **Any typed event or event wait.** `ui-wait-for` strict property equality is the only wait.
- **Interactive sessions** (`session start`/`stop` — a session an agent drives command by
  command). Only the self-contained `session run` exists.
- **Secondary windows and own-window popups** — invisible to state, snapshot, and grabs.
- **Semantic sharpness/size verdicts on pixels** — dHash drift only.
- **Absence assertions.** `expect` has `exists` but no `absent` — a scenario cannot assert a
  path is missing (e.g. "no un-normalized fetch rows"). Note it when a plan needs one.

## HUMAN-ONLY (never claim these from the bridge)

- **Aesthetic judgment.** Whether a surface *looks right* — spacing, warmth, taste — is
  Hemanth's eyes, always. Evidence packages feed his judgment; they do not replace it.
- **On-screen truth of the daily app.** The daily app is not a test fixture: no Drive against
  it, no mutation of live collection/progress/settings, and its default pipe is off-limits to
  automated sessions. Read-only diagnosis of the daily app is allowed only when the task is
  explicitly about the daily app's live state.

---

## Status vocabulary (for plans and reports)

`Runtime-validated` is the only status that closes a user-visible slice without qualification.
The others — `Implemented, verification pending` · `Bridge blocked` · `Verification failed` ·
`Plan contradicted` · `Test-reported` — are honest intermediate states, not failures of nerve.
A green unit suite is `Test-reported`, never `Runtime-validated`.
