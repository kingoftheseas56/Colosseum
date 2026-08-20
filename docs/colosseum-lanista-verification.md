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
| `get-state` | Read | root windows (title, geometry, visible, active, **`state`** — added J1-Tray-Bridge, 2026-08-14) + artifact runDir + **resolved `appDataRoot`/`cacheRoot`** (the isolation-proof seam, added 2026-08-06) | **root windows only** |
| `qml-get` | Read | read named QML properties off an item (by objectName or handle) | property equality only, values as QVariant→JSON |
| `ui-query` | Read | one item's geometry + the full structural vocabulary (handle, parent handle/name, childCount, z, enabled, opacity, localRect, sceneRect, **clipChain**) — L1-Bridge, 2026-08-13 | legacy fields unchanged; `clippedByWindow` still root-window-only, but `clipChain` now answers real ancestor clipping (see Structural dump below) |
| `dump-ui` | Read | **every `QQuickItem`, named or not** (unnamed carry `objectName:""`) with the full structural vocabulary + paging — L1-Bridge, 2026-08-13 | legacy flat fields byte-identical; unnamed items now visible; bounded by root/maxDepth/maxItems + reply-byte budget |
| `ui-snapshot` | Read | actionable elements with opaque handles, centers, sizes | see "UI model truths" below |
| `ui-click` | Drive | synthesized click at a named item's center | client never supplies pixel coords |
| `ui-keypress` | Drive | key to the focus item of the main window | first key of sequence only; printable-ASCII text |
| `ui-text-input` | Drive | forceActiveFocus + per-char KeyPress | no KeyRelease pairs |
| `ui-scroll` | Drive | wheel event, `dy` (default −120) | no scroll phases |
| `window-set-state` | Drive | restore/minimize/hide the first root window via real `QWindow::showNormal()`/`showMinimized()`/`hide()` — J1-Tray-Bridge, 2026-08-14 | `state` required, exactly `normal`/`minimized`/`hidden`; **first root window only**; see "Window state" below |
| `ui-wait-for` | Read | poll one property until **equal** to a value | 50 ms poll, default 3 s timeout, strict equality ONLY — no operators, no compound predicates |
| `invoke-read` | Read | allowlisted C++ method calls | **8 methods**: six `TankobanVolumes` reads + `BiblioImageDiag.rowsForUrl(urlFragment)` / `BiblioImageDiag.recentRows(limitText)` (per-URL image-network rows: status, error, cacheHit, bytes, contentType, timing — newest first; added 2026-08-06). QString args (max 3); returns only list/map/bool |
| `events-tail` | Read | last N lines of the JSONL event log | see "Event log truths" |
| `log-mark` | Read | append a correlation mark to the event log | the ONLY event type that exists today |
| `vault-forensics` | Read | one bounded, typed read projection of the live Vault (F1-Core `VaultForensics`, composing `VaultLibrary` only) — F1-Bridge, 2026-08-13 | `scope` required (`summary`/`root`/`node`/`identity`); see "Vault forensics" below |

### Structural dump — L1-Bridge (2026-08-13)

`dump-ui` and `ui-query` now expose an agent enough structure to explain a layout failure, not
just enumerate named items. Every row keeps its legacy flat fields (`objectName`/`class`/`x`/`y`/
`width`/`height`/`visible`/`depth`) byte-identical — old clients are unaffected — and gains:
`handle` (reuses `ui-snapshot`'s `s<gen>h<n>` epoch machinery — one identity scheme, not two),
`parentHandle`/`parentName`, `childCount`, `z`, `enabled`, `opacity`, `localRect`/`sceneRect`, and
`clipChain` (the ordered `clip:true` ancestors, nearest first, each with its own handle + scene
rect). **`clipChain` is the fix for the demonstrated bug** where `clippedByWindow` reports an item
`visible:true, clippedByWindow:false` while it is scrolled out of its own list's viewport and not
actually rendered (L1-Discovery, `docs/visibility/lanista-structural-gap.md`) — `clippedByWindow`
still measures only the root window, but the clip chain now reveals the real ancestor that hides it.

Top-level reply fields: `generation`, `rootWindow`, `truncated`, `bytesUsed`, `continuation.cursor`.
Request-side `root` / `maxDepth` (ceiling 64) / `maxItems` (ceiling 5000) are clamped, never trusted;
a ~96 KiB reply budget (deliberately below the 1 MiB wire ceiling, favouring several small pageable
replies for an agent consumer) truncates and pages via `cursor` + `generation`. Ephemeral handles die
at the next structural/snapshot generation (stale → `NO_SUCH_ITEM`); `objectName` targeting unchanged;
first-root-window scope; Read gate only. Runtime-validated in an isolated session 2026-08-13 (a real
unnamed production row queried by handle matched its `dump-ui` row exactly). Harness cases: see the
test ledger's "Lanista structural dump" entry.

### Vault forensics — F1-Bridge (2026-08-13)

One Read-gated `vault-forensics` command answers "what does the live Vault actually hold right
now" without hand SQLite archaeology. It invokes F1-Core's `VaultForensics::queryMarshalled()`
on the app's GUI/owner thread (F0's named safe seam — `VaultLibrary`, never `VaultIndex`
directly) and hands the response map back to the wire **UNCHANGED**: the reply body's fields
are exactly F1-Core's `colosseum.vault.forensics.v1` schema, with only the ordinary wire
envelope (`type`, `seq`) added on top like every other command. The bridge does not grow a
generic reflection/write registry — this is one named, typed call onto one named projection.

- **Request payload:** `scope` (required: `summary`/`root`/`node`/`identity`), `key` (string,
  scope-dependent — a root path for `root`, a browse/group key for `node`/`identity`, unused for
  `summary`), `limit` (int, F1-Core clamps to 1..100, default 20), `timeoutMs` (bridge-level
  marshalling deadline for `queryMarshalled()`, clamped server-side to 200..10000, default 2000).
- **Reply:** F1-Core's own envelope — `schema: "colosseum.vault.forensics.v1"`,
  `indexSchemaVersion`, `revision`, `ownerThread{name,id}`, `truncated`, `errors[]`, plus the
  scope's own block (`roots`/`browseCount`/`itemCount`/`recent` for `summary`; `root`/`browse`
  for `root`; `node`/`browse` for `node`; `identity` for `identity`).
- **The `candidateCount:-1` sentinel is inherited honestly, never patched over.** F1-Core found
  the real uncertain-identity candidate count is not reachable through `VaultLibrary`'s public
  surface — it lives only inside `VaultBrowseDetail::detailFor`'s human evidence sentence, and
  reaching raw `VaultIndex` rows to recover it would mean touching `VaultIndex` directly, which
  F0 §10 forbids this seam from doing. Recovering the real count would need a new `VaultLibrary`
  accessor — a Vault-engine change outside F1's fence. Noted for the record; a future decision.
- **An unknown `scope` is F1-Core's own diagnostic, never a bridge-level wire error:** the reply
  stays `type:"reply"` with `errors[]` naming the bad scope (`"unknown scope '<x>' — expected
  summary|root|node|identity"`) — the bridge does not duplicate F1-Core's scope validation.
- **No owner bound (`VAULT_FORENSICS_UNAVAILABLE`):** a process that never wired a
  `VaultForensics*` into `LanistaServer::setVaultForensics()` (e.g. a bare QML-scene harness with
  no live `VaultLibrary`) answers this coded error rather than dereferencing null. Production
  (`main.cpp`) always wires one, parented to `&app` alongside every other Vault object.
- **CLI/facade:** no dedicated CLI verb — the existing generic `lanista vault-forensics
  scope=summary limit=10 [--pipe P] [--timeout T]` plain-command path (`payloadFromArgs`'s k=v
  typing) already handles it; nothing added to `native/tools/lanista.cpp`. The facade adds one
  typed tool, `vault_forensics(scope, key?, limit?, timeoutMs?)` — see "MCP adapter" below.
- Harness cases (`tests/lanista_harness.cpp`, a real `VaultLibrary` fixture — 105 rows under one
  browse node, 5 over F1-Core's `kMaxLimit`): `vault_forensics_is_read_gated`,
  `vault_forensics_passes_response_unchanged` (bridge reply == `VaultForensics::query()` direct
  call, byte-for-byte, envelope aside), `vault_forensics_rejects_bad_scope`,
  `vault_forensics_clamps_limit` (never more than 100 rows through the bridge — negative control:
  temporarily asserting exactly 101 rows reds with "got 100", restored), and
  `vault_forensics_deadline_is_bounded` (same-thread degrade path returns in well under a
  second) — all green 2026-08-13, `lanista_harness.exe` self-test, `LANISTA_OK`. **Isolated-
  session runtime replay against the assembled app (`session_start` → `vault_forensics(scope=
  "summary")` → `vault_forensics(scope="node", key=<seeded key>)` → `warnings()`) is Bridge
  blocked as of this slice's landing:** a `colosseum.exe` was live-running from `native/
  build-msvc` (PID/start-time/cmdline indistinguishable from Hemanth's daily app; per the plan's
  own standing rule it is never killed to clear a lock) when this slice tried to relink the app
  target, so the shipped `colosseum.exe` does not yet carry this command. `main.cpp`/
  `LanistaServer.cpp` compile clean (object-file build proven) and every command/CLI/facade path
  is exercised against the real `lanista_harness.exe` build above; the assembled-app replay is
  the one piece still owed once the app target can relink.

### Window state (tray/minimize) — J1-Tray-Bridge (2026-08-14)

One Drive-gated `window-set-state` command closes the gap the test ledger's slice-7
three-layer regression named: the bridge could not reach the Windows taskbar, so nothing
could restore a minimized/hidden session. `window-set-state` does not reach the taskbar
either — it calls the first root `QWindow`'s own state-transition API directly
(`showNormal()`/`showMinimized()`/`hide()`), the exact same calls a titlebar minimize or a
taskbar/tray restore trigger in production. No FlaUI/pywinauto, no tray-icon clicker, no
secondary-window enumeration, no OS-picker framework — in-process only, same law as every
other Task 2/3/5 command (`native/devtools/LanistaServer.cpp:cmdWindowSetState`).

- **Request payload:** `state` (required, exactly `"normal"` / `"minimized"` / `"hidden"`;
  anything else fails `BAD_STATE` **before touching the window at all** — no partial
  mutation on a refused request, proven by a live case that reads the window's
  `visibility()` back byte-identical after the refusal).
- **Reply:** `objectName`, `state` (the observed post-call name — see below), `visible`,
  `active` — the same vocabulary `get-state` now also carries per window.
- **`get-state`'s new `state` field** is derived from the identical `QWindow::visibility()`
  switch both call sites share (`windowStateName()`), so a `window-set-state` reply and a
  later `get-state` read can never disagree about what to call the same observed state:
  `"normal"` (Windowed), `"minimized"`, `"hidden"`, `"maximized"`, `"fullscreen"`, or
  `"unknown"` (`AutomaticVisibility`, before a window has ever been shown).
- **First root window only.** A second root window (if one exists) is never addressed —
  proven live by a fixture with two root `Window`s: only the first's visibility changes.
- **Ground-truthed live nuance (isolated runtime session, 2026-08-14): a `state=normal`
  request does not always observe back as `"normal"`.** After minimize → normal, the
  assembled app's own root window reported `get-state`'s `state` as `"fullscreen"`, not
  `"normal"` — Colosseum's default window chrome (`WindowModeStore`/`WindowStatePolicy`,
  `tests/window_shell_gui_harness.cpp`'s own "clean settings → fullscreen base" contract)
  re-asserts its own borderless-fullscreen presentation once the window becomes visible
  again; `showNormal()` still ran (the window is genuinely un-minimized, on-screen, and
  responsive), but the app's OWN chrome logic then decides the actual resting `Visibility`.
  A second normal call later in the same session (after hidden, not minimized) DID observe
  `"normal"` — the two paths are not symmetric. **A consumer must not assert `state==
  "normal"` after requesting it; assert `state != "minimized" && state != "hidden"`
  instead**, exactly the shape J1-Tray's own journey needs to use. This is an app-chrome
  fact, not a bridge defect — `window-set-state` itself is not touched by it.
- **CLI/facade:** no dedicated CLI verb needed — the existing generic
  `lanista window-set-state state=minimized [--pipe P] [--timeout T]` plain-command path
  (`payloadFromArgs`'s k=v typing, already string-typed for a non-numeric/non-bool value)
  handles it unchanged; nothing added to `native/tools/lanista.cpp`.
- Qt Test cases (`tests/auto/lanista/tst_window_set_state.cpp`, registered
  `colosseum.qttest.window_set_state`, own inline `Window{}` QML fixtures — no bridge
  scene file needed): `read_gate_refuses_window_set_state`, `drive_gate_accepts_normal`,
  `drive_gate_accepts_minimized`, `drive_gate_accepts_hidden`, `bad_state_is_rejected`,
  `only_first_root_is_addressed` — all green 2026-08-14. Negative control performed both
  directions live (reclassify as Read-gated → exactly the gate-refusal case reds, all 5
  others stay green → restore → reconfirmed green).
- **Isolated-session runtime replay against the assembled app** (unique pipe, tagged
  AppData, windowed — offscreen cannot prove real taskbar-adjacent window-manager
  transitions): minimized → `get-state` confirms `state:"minimized"` → normal → `get-state`
  confirms visible/non-minimized (`"fullscreen"`, see nuance above) → hidden → `get-state`
  confirms `visible:false, state:"hidden"` → normal → `get-state` confirms
  `state:"normal", visible:true`. PID of the daily app (`22956`) verified running,
  untouched, before and after. Evidence under
  `artifacts/visibility-phase2/j1-tray-bridge/` (gitignored).

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

### Runner-owned layout verdicts — Slice L2 (2026-08-13)

`layout_verdict` is a **runner-local scenario step** (`native/tools/lanista.cpp`) — there is no
new server command; it composes the existing Read-gated `dump-ui` (see "Structural dump —
L1-Bridge" above) into a deterministic red/green geometry verdict, turning "this control is cut
off / sitting on top of its peer" into a machine-checkable fact instead of a screenshot judgment
call. The pure evaluator lives in header-only `native/tools/LanistaLayoutVerdict.h` (namespace
`lanista`) — no `native/CMakeLists.txt` edit, no bridge/server change; `LanistaServer.cpp`,
`LanistaServer.h`, and `tests/lanista_harness_scene.qml` are all untouched by this slice.

- **Step shape:** `{"label": "...", "layout_verdict": {"checkpoint": "<path.json>", "out":
  "<optional evidence path>"}}`. The checkpoint file names an optional `root` (scopes the
  dump-ui walk; default the whole window) and a `rules` array, each `{"kind":
  "actionableNonzero"|"contained"|"noPeerOverlap", "name": "...", ...kind-specific fields}`.
- **Three rule kinds, nothing else:** `actionableNonzero` (target nonzero width/height AND
  visible AND enabled); `contained` (target rect within a named viewport rect, tolerance in
  **logical** px — inclusive at the boundary); `noPeerOverlap` (an **explicit** `peers` array,
  2+ names — pairwise positive-area intersection only; a shared edge, zero-area, is never
  overlap). `noPeerOverlap` is provably never a global sweep: its only input is the checkpoint's
  own `peers` array — an item genuinely overlapping an unnamed peer is invisible to the rule by
  construction (proven live and at the Qt Test layer, see below).
- **The single-generation guarantee.** The runner pages through `dump-ui`'s own `continuation`
  contract (as many bounded replies as the checkpoint's named items need) into one
  `LayoutSnapshot`, but every page after the first must report the **same** `generation` the
  first page pinned — a page that doesn't is rejected outright (its rows never enter the
  snapshot) and the whole checkpoint fails a named `oneGenerationIsRequired` rule rather than
  evaluating on a partial, mismatched-moment merge. A moving delegate between two independent
  `dump-ui` calls can never manufacture a verdict.
- **The synthetic `$rootWindow` viewport.** `dump-ui`'s reply already carries a `rootWindow`
  field (the window's own width/height) with no objectName to address it by — `LayoutSnapshot`
  exposes it under the reserved name `$rootWindow` (a QML objectName can never contain `$`, so
  it can never collide with a real item) so a checkpoint's `contained` rule can use the whole
  window as a viewport without a fabricated production object.
- **Real bug found and fixed live driving the assembled app (2026-08-13):** Colosseum
  pre-warms other worlds' pages in the background, and `TopBar.qml` ("ONE source for the top
  bar across the home AND every world page") is reused verbatim by each — so the SAME
  `modePill_*` objectName exists more than once in the full tree: once on the real, visible home
  page (depth 5), and again inside each hidden pre-warmed world's own TopBar instance (depth 8,
  `visible:false`) — the same name-collision shape this ledger already documents for
  `ui-click`/`ui-query` ("Name collisions resolve DFS-FIRST — including into HIDDEN worlds").
  `LayoutSnapshot`'s first cut used last-write-wins name indexing, so a `dump-ui` walk that paged
  far enough to reach a pre-warmed world's hidden duplicate silently overwrote the real row's
  geometry — reproduced twice against a fresh isolated `app_home` session (all four
  `modePill_*` rows read `not visible`), root-caused by paging the full tree by hand and finding
  the duplicate rows at cursor ~1765, fixed by making `LayoutSnapshot` keep the FIRST occurrence
  per name across merged pages (matching the bridge's own DFS-first law), then reproduced green
  twice more. Locked in as the Qt Test's 11th case, `duplicate_names_resolve_dfs_first`, beyond
  the plan's 10 named cases.
- **Checkpoint definitions:** `tests/lanista_layout/harness.json` (against the real, unedited
  `tests/lanista_harness_scene.qml`) and `tests/lanista_layout/app_home.json` (against the real
  assembled app's home screen, the four `modePill_*` pills) — both **Runtime-validated** in
  isolated sessions 2026-08-13 (harness: served `lanista_harness.exe`, `ColosseumLanistaTest`
  pipe; app_home: `lanista.exe session run`, tagged `Colosseum-dltest-<tag>` root, never the
  daily pipe). Full transcripts and verdict JSON under `artifacts/visibility-phase2/l2/`.
- **Negative controls (mandatory, performed live 2026-08-13), both against REAL, unedited
  harness geometry — no QML edit, since `tests/lanista_harness_scene.qml` sits outside this
  slice's fence:** (a) **containment** — `clippedBox`'s real right edge is exactly 80 logical px
  past the real window's right edge; a checkpoint identical to `harness.json` except
  `toleranceLogicalPx: 79` (one logical px short of the true overflow — the boundary-exact
  equivalent of moving the real box one logical pixel further out) turns **exactly** the
  `contained` rule red, the two `actionableNonzero` rules and `noPeerOverlap` unchanged green;
  restored green by reverting to `toleranceLogicalPx: 80`. (b) **overlap** — `row6` (a
  `longList` delegate) and `clipHost` (a clip-chain fixture) were *discovered*, not
  constructed, to genuinely overlap in the current committed scene (authored for unrelated
  purposes, real intersection 60×40 at (500,400)); a checkpoint identical to `harness.json`
  except `noPeerOverlap`'s peers are `["row6","clipHost"]` turns **exactly** that rule red,
  the other three rules unchanged green; the ordinary `harness.json` checkpoint (non-overlapping
  peers) replayed green again immediately after. Evidence:
  `artifacts/visibility-phase2/l2/{negative-containment,negative-overlap}.json` plus their own
  checkpoint files.
- **Baseline (2026-08-13):** `artifacts/visibility-phase2/l2/baseline-rects.json` — real
  captured rects proving the PRE-L2 runner (plain `cmd`+`expect` steps only) cannot reject the
  gap: `clipHostChild` reads `clippedByWindow:false` while its real `clipChain` shows it is
  entirely outside its own `clip:true` ancestor's bounds (a false-green an `expect` on
  `clippedByWindow` alone would miss); `zeroSizeItem` reads `visible:true, enabled:true,
  clippedByWindow:false` while its rect is 0×0 (no existing field names this failure at all);
  and `expect`'s one-dot-path-vs-one-constant shape structurally cannot express a two-item
  overlap assertion regardless of geometry.
- **Qt Test:** `colosseum.qttest.layout_verdict` (`tests/auto/lanista/tst_layout_verdict.cpp`,
  header-only under test — compiles just the test TU, same deploy pattern as
  `tst_http_header_fields.cpp`). 11/11 green (the plan's 10 named cases +
  `duplicate_names_resolve_dfs_first`); two negative controls performed live against the
  production evaluator (restored, reverified green) — see the test ledger's own entry for the
  exact mutations and which named cases flipped.

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
  JSON-RPC `2024-11-05` base, no Tasks, no SDK. All 12 tools are plain `tools/call`.
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
  - `vault_forensics(scope, key?, limit?, timeoutMs?)` — **F1-Bridge, 2026-08-13, the 12th tool,
    strictly appended after the 11 above** (never inserted between them — `TOOLS`/`TOOL_IMPLS`
    order proven unchanged). Shells the same `vault-forensics` bridge command on the ACTIVE
    session's pipe (session ownership preserved, like `act`/`get`/`snapshot`), preserving v0's
    deadline/backstop pattern: the bridge-level `timeoutMs` clamps to 200..30000 ms, and the
    CLI's own client deadline is `max(10000, timeoutMs + 5000)` — the same floor+slack rule
    `wait_for()` already uses, so a hung owner-thread wait can never make Python give up before
    the bridge's own bounded wait would have returned a coded error. The reply is passed through
    unchanged at this layer too (proven by `tests/test_lanista_mcp_forensics.py`'s
    `summary_round_trip`/`node_round_trip`, which monkeypatch `run_lanista` so no subprocess or
    live app is needed to prove the Python plumbing).
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
- **CRITICAL GAP CLOSED (2026-08-14).** Until this fix, the tag above did NOT cover the three
  registry-backed stores — `ProgressStore` (Continue), `CollectionStore` (Your Collection), and
  `SearchHistoryStore` (search MRU) all hardcoded `QSettings("Brotherhood", "Colosseum")`, which
  resolves straight to the Windows registry regardless of `applicationName` — so a tagged
  session still read AND WROTE the real user's Continue map, Collection shelf, and search
  history. Proven live: a tagged test journey wrote `manga␟journey-manga-series-v1` into the
  real registry Continue map. Fixed by having each store divert to a private ini file under the
  tag's own `AppDataLocation` when `COLOSSEUM_APPDATA_TAG` is set (`ProgressStore.h`,
  `CollectionStore.h`, `SearchHistoryStore.h`); untagged behavior (the daily app) is unchanged.
  `colosseum.qttest.store_isolation` (ctest) proves the routing; a live tagged session's empty
  Continue map is the runtime proof. **Any new QSettings-backed store must never hardcode the
  org/app pair** — either follow this file's tag-gate pattern or use a plain
  default-constructed `QSettings()` (resolves through the current `applicationName()`, already
  tag-safe — the pattern `AudioPairingStore`/`WindowModeStore`/the torrent stores already use).
- `dev.bat` isolates **nothing**: no pipe override, no data tag, no gates — it shares the daily
  app's data, cache, and default pipe. It is a live-reload convenience, not a test session.

---

## Vault Browse face — Slice 10 closing replay (2026-08-13)

The plan's closing slice ordered one full scenario-suite replay in fresh isolated sessions:
`vault_browse_smoke`, the states family, the empty family, `vault_launch_smoke`,
`vault_open_recent`. All ten scenarios below were run today, each its own isolated tagged
session, `--drive`, seeded per each scenario's own comment where one is needed.

| Scenario | Result | Notes |
|---|---|---|
| `vault_browse_smoke.json` | **65/70** (`--keep-going`) | See "the redrill finding" below — one root cause, five cascaded failures. Everything through the detail sheet, rail collapse/expand, the Wire's honest season-presence fact, the episode wall (both real-file and Gintama-scale), and the first 10-burst scroll/virtualization proof passed clean. |
| `vault_browse_resolve.json` | 8/9 | One stale assertion, not a defect — see "the stale fixture count" below. Akira settling `resolving → identified` via the real offline catalogue, the canonical title read-through, and the visual-evidence grab all passed. |
| `vault_browse_uncertain.json` | 13/13 (2nd try; 1st hit one `INFRA TIMEOUT` opening the vault door before any assertion ran, consistent with the load-sensitivity noted below) | Masterpiece settling `uncertain`, the gold mark, identify-in-place through the real dialog, settling `identified` via the same re-project path — all clean. |
| `vault_browse_away.json` | 10/10 | Both away tiles (single-file and two-file groups) stay present, marked away, per §4.7 "nothing disappears." |
| `vault_browse_churn.json` | 39/39 | The 303-item fixture, six full scroll-down bursts to true max extent and six back up; Akira/Masterpiece read correctly before AND after the round trip. |
| `vault_browse_no_storage.json` | 7/7 | |
| `vault_browse_empty_folder.json` | 9/9 | |
| `vault_browse_allaway_empty.json` | 9/9 | |
| `vault_launch_smoke.json` | 7/7 | |
| `vault_open_recent.json` | 13/13 | No committed seed directory exists for this scenario yet (same situation Slice 7's own ledger entry already recorded) — hand-built a `vault/open-recent.json` seed (two entries: one real 3-page fixture CBZ, one dead path) under `artifacts/slice10-sweep/open-recent-seed/`, gitignored, not a committed fixture. |

**The redrill finding (`vault_browse_smoke.json`, steps 60–61 and its four downstream casualties).**
Re-drilling into the SAME show a second time — leave it via the rail's root entry, come back,
click it again — reproduced 4/4 times across independent isolated sessions: `ui-click` dispatches
correctly (server-resolved coordinates land on the card's own `_hitArea`, confirmed
`enabled: !card.away` was `false`/not blocking), but `vaultBrowseCrumb.currentPath` and
`vaultBrowseGrid.count` never leave the root level — confirmed by direct `qml-get` (not just the
scenario's own `ui-wait-for`) after an 8s settle and again after extending the wait to 45s (3× the
scenario's own 15s budget); the value never changes. This is the SAME area this ledger already
named as a pre-existing, load-sensitive flake in the Slice 9 entry below ("`vault_browse_smoke.json`
... replayed 8 times ... never failed at the same step twice ... heavy unrelated live network
traffic ... confirmed pre-existing via a direct A/B test") — today it failed at the exact identical
step all four times rather than varying, which reads as the SAME class of issue under heavier or
more consistent load today (a concurrent build was running in `native/build-msvc` for part of this
session; Slice 9's own log showed 434 live requests to `live.metahub.space` alone). What's new here:
the 45s-timeout diagnostic shows the click had NO effect at all rather than merely a delayed
repaint — worth a future slice's attention rather than dismissed as "just slow." Not investigated
further and no code was touched — Slice 10 is verification and documentation only. Evidence:
`artifacts/lanista-sessions/20260813-154417-62cbc69f/` (the fullest `--keep-going` run) and
`artifacts/lanista-sessions/20260813-153223-d1719fbc/` (the 45s-timeout diagnostic).

**The stale fixture count (`vault_browse_resolve.json`, step 5).** Asserts
`vaultBrowseGrid.count == 2` ("Akira, Masterpiece") against
`tests/fixtures/vault/browse-states-smoke`, which `vault_browse_churn.json`'s own comment already
documents as extended to 303 items (Akira + Masterpiece + 301 filler groups) for that scenario's
virtualization proof — a shared-fixture drift between sibling scenarios, not an app defect.
`vault_browse_uncertain.json` shares the same fixture root but asserts no hardcoded count, so it is
unaffected. Not fixed here (editing scenario JSON is execution work, out of this slice's fence);
named for whichever slice next touches these scenarios.

**Warning gate.** Ran `tests/warning_gate.ps1` against all ten sessions' own
`<appDataRoot>/logs/colosseum.log`. Four came back clean (`WARNING_GATE_OK`:
`vault_browse_uncertain` 2nd run, `vault_browse_churn`, `vault_browse_empty_folder`,
`vault_browse_allaway_empty`, `vault_launch_smoke`). The rest fail the gate, but every offending
line is one of the two categories this ledger's own Slice W0 baseline already catalogued as
known-noise (the `live.metahub.space` 404 pattern from Continue-rail prewarming, and Qt
teardown-time `device not open`/SQL-without-QCoreApplication lines) plus the one already-named
`QMetaObject::invokeMethod: No such method QObject::writeSnapshot` warning from the Slice 7 ledger
entry below — no new signal class. Named honestly rather than silently allowlisted: the Slice W0
baseline's own four sessions never visited Vault ("NOT claimed: any statement about the warning
behavior of surfaces these four sessions did not visit (Vault...)"), so `tests/lanista-warning-
allowlist.json` has no Vault-scoped entries yet — these sessions are failing an allowlist that was
never extended to this surface, not failing a check that has seen Vault before and found it clean.
Extending the allowlist is a Slice W0-owned change, not this slice's to make unasked.

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
9. ~~**`window-set-state` (Drive-gated)**~~ — **DELIVERED, Slice J1-Tray-Bridge, 2026-08-14.**
   See "Window state (tray/minimize)" under AVAILABLE NOW above: minimize/restore/hide via
   the real `QWindow` state-transition API, first root window only. Closes the demand from
   the three-layer minimize/restore regression (test ledger, slice 7) at the bridge layer —
   in-process restore is now possible. **Still true, and deliberately unchanged:** this does
   NOT reach the real Windows taskbar/tray icon or verify what Windows itself shows the
   user — that remains the named alternative shape (an outside-the-process FlaUI/winapp
   prototype, `docs/colosseum-verification-tooling-map.md`) or human-witnessed observation,
   exactly as the next slice (J1-Tray, the assembled-app minimize-to-tray-and-back journey)
   is scoped to use.

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

## Tankoban catalogue-independence Slice 3 (2026-08-20) — onboarding solved, a new tab-bar gap found

- **Account onboarding objectName correction (closes a gap Slice 2 left open).** Slice 2's
  ledger entry named the gate correctly (`accountWelcomeContinueLocal` must be clicked
  before ANY world content is reachable on a fresh tag) but never resolved the HOST item's
  addressable name. `main.cpp`'s `AccountOnboardingHost` is instantiated in `qml/Main.qml`
  with an explicit `objectName: "accountHost"` override — this SHADOWS the component's own
  internal default (`objectName: "accountOnboardingHost"`, set inside
  `qml/account/AccountOnboardingHost.qml`'s root `Item`). The internal default is
  UNREACHABLE from outside; `qml-get`/`ui-wait-for` against `"accountOnboardingHost"` is
  `NO_SUCH_ITEM` — the correct target is `"accountHost"`. Proven live (isolated session,
  tag `tk3c`): `qml-get accountHost.visible` → `true` right after `bootSplash.visible ==
  false`; `ui-click accountWelcomeContinueLocal` → `ui-wait-for accountHost.visible ==
  false` matches (mode leaves `signedOut`). The full onboarding→Tankoban entry sequence
  (`bootSplash` wait → `accountHost` probe → `accountWelcomeContinueLocal` click →
  `accountHost` dismiss wait → `modePill_Tankoban` click → `tankobanTabBar` settle) is
  `tests/lanista_scenarios/tankoban_catalogue_smoke.json`, 8/8 green, session
  `20260820-161046-c9c2af67`, `WARNING_GATE_OK` on its own logs.
- **New named surfaces added this slice** (all world-namespaced per the naming law):
  `tankobanShelfState` (invisible Item on `MangaTankobanLibrary`'s root: `rowCount` int,
  `coveredCount` int — rows whose resolved cover is non-empty); `tankobanVolumeCard_<number>`
  (the volume shelf's GridView delegate objectName, replacing the old bare shared stem
  `"volumeTile"`); `tankobanReadingRoomBack` (the reading room's `BackAction`, previously
  unnamed — needed to leave a series page in-session); `tankobanTopMangaTile_<index>` (the
  "Top in Tankoban — Manga" rail's tiles, via a new opt-in `namePrefix` property on the
  shared `TrendingTop10.qml` component — Theatre/Biblio/Demo are unaffected, `namePrefix`
  defaults to `""`/no name).
- **A newly-discovered, NOT-resolved bridge/runtime gap: the Tankoban `WorldTabBar` renders
  only a subset of its modeled tab pills.** `qml/TankobanWorld.qml`'s `tabModel` is a static
  4-entry array (`discover`/`manga`/`comics`/`library`), confirmed byte-identical on disk via
  three independent reads (`Read` tool, `grep`, `sed`) across the investigation — this is not
  a misread. Driving live in FOUR separate isolated fresh-tag sessions (`tk3d`, `tk3e`
  with `--qml qml/Main.qml` explicitly forcing live-disk load, `tk3f`, `tk3g`), after a
  confirmed-correct onboarding dismiss and `modePill_Tankoban` click:
  `qml-get tankobanTab_manga` and a 10s `ui-wait-for tankobanTab_manga.visible==true` both
  fail `NO_SUCH_ITEM`/`WAIT_TIMEOUT`; a `dump-ui` scoped to the real, visible, correctly-
  geometried `tankobanTabBar` root (confirmed `visible:true`, on-screen rect, not clipped)
  shows its inner `Row` (the `Repeater`'s parent, per QML's own delegate-placement rule)
  with exactly TWO children: one materialized delegate — `tankobanTab_library` — and the
  `QQuickRepeater` placeholder itself reporting `childCount:0`. Only 1 of the Repeater's 4
  modeled items had materialized as a sibling; the other three (discover/manga/comics) never
  appeared, across every session tried, not a transient race (confirmed at both an
  immediate dump and after a 10s settling wait). `qml/WorldTabBar.qml` itself reads correct
  on disk (a plain `Repeater { model: tabs.tabModel; delegate: Rectangle {...} }`) and
  carries no scoped-fence issue found. Root cause NOT identified within this slice's fence —
  named honestly rather than worked around by guessing. **Practical consequence:** the
  "Top in Tankoban — Manga" rail (this slice's own new `tankobanTopMangaTile_*` names) is
  UNREACHABLE without first switching to the Manga tab, which is itself unreachable — so
  the masthead/shelf portion of `tankoban_catalogue_smoke.json` (the actual point of this
  slice's scenario: opening One Piece/Vagabond and asserting `tankobanSeriesMasthead`/
  `tankobanShelfState`) could NOT be driven live this slice. The Discover tab's own default
  content DOES render correctly (screenshot-confirmed match to `TankobanDiscoverPage.qml`'s
  "Tankoban built-in catalogue" wall — Slice 2's own `mangaDiscoverCard_<malId>` naming is
  presumably still live there), but `TankobanDiscoverPage.qml`'s root carries no objectName
  at all (only a `loading` property ALIAS, unreachable without one) — a second, smaller gap
  a future slice should close before that route can be scripted either. The committed
  `tankoban_catalogue_smoke.json` therefore stops at the Tankoban-entry step, honestly —
  see the test ledger's Slice 3 entry for what it does and does not prove.
- **Ground-truthed series/counts for a future attempt (real db lookups, 2026-08-20, not
  from the plan's own baseline text — the catalogue has moved on since it was written):**
  One Piece (malId 13, catalog `volumeCount=113`, `count_basis=bookwalker`, 112/113 baked
  covers — the COVERED branch, tile index 0, no scroll needed once the Manga tab is
  reachable); Vagabond (malId 656, catalog `volumeCount=37`, `count_basis=mal`, 0/0 baked
  volume rows — the honest NO-COVER branch, tile index 3); Berserk (malId 2, catalog
  `volumeCount=0` — an ONGOING series with no MAL count yet, so `hasShelf==false` and it
  renders the shelf-less page, NOT a 0-covered shelf — not a usable fixture for the
  coveredCount==0 branch); Monster (malId 1) is now `(8, bookwalker)` in the live db, NOT
  the plan's stated "18 vols mal-basis NO covers" — the harvest has evidently landed
  partially for Monster since the plan was authored; a future attempt should re-query
  rather than trust either this note or the plan's own baseline.

## Tankoban catalogue-independence Slice 4 (2026-08-20) — the tab-bar gap did NOT recur;
## masthead/shelf/picker all driven live, series-mode search proven end-to-end

- **Slices 2-3's own runtime debt is CLOSED — the committed `tankoban_catalogue_smoke.json`
  now runs the whole masthead→shelf→picker journey live, not just the Tankoban-entry stub
  Slice 3 shipped honestly.** Session `20260820-173802-4baf3a24` (tag `tk4`, isolated
  pipe, `Colosseum-dltest-tk4` appData root), driven step-by-step via the MCP session
  tools (a shared single-slot resource — session start was refused twice with `a Lanista
  session is already active: id=20260820-172321-6a31cca9 ... tag=wp-8a`, a genuinely
  concurrent brother's Watch Party acceptance session (PID 22108, live and growing in
  memory across two retries); waited it out rather than force it, slot freed after ~6
  minutes). Sequence run, every step green, no workaround needed at any point:
  `bootSplash.visible==false` → `accountHost.visible==true` → click
  `accountWelcomeContinueLocal` → `accountHost.visible==false` → click
  `modePill_Tankoban` → click `tankobanTab_manga` → click `tankobanTopMangaTile_0` →
  `tankobanSeriesMasthead.ready==true` with `displayTitle=="One Piece"`,
  `resolvedMalId=="13"`, `hasShelf==true`, `primaryAction=="get"` (byte-identical to the
  diagnosis agent's earlier read) → `tankobanShelfState.rowCount==113`,
  `coveredCount==112` → click `tankobanVolumeCard_1` → `tankobanSourcesSheet.visible==true`
  → `hasCompileFallback==false` → whole-window grab (real live Nyaa results: "10 sources",
  header reads "Nyaa" only, no WeebCentral anywhere) → dismiss via `tankobanSourcesBack` →
  regression: reopen on `tankobanVolumeCard_2`, dismiss again → world-tab away
  (`modePill_Biblio`) → world-tab back (`modePill_Tankoban`) → **the series page state
  SURVIVED the round-trip untouched** (`tankobanSeriesMasthead.ready`/`displayTitle`
  still "One Piece" with no re-navigation at all — a stronger regression proof than the
  plan's own "reopen series" wording anticipated, recorded as a runtime fact: Tankoban's
  world root does not tear down an open series page on a sibling-world visit) → back to
  the Manga tab (via `tankobanReadingRoomBack`) → click `tankobanTopMangaTile_1` (Berserk,
  index 1 of the STATIC `qml/Catalog.js#topManga` list backing this rail — deterministic,
  not live-ranked, so this index is stable across runs) → `tankobanSeriesMasthead.ready`
  with `displayTitle=="Berserk"`, `resolvedMalId=="2"`, `hasShelf==false`,
  `primaryAction=="search"` (matches the diagnosis's ground-truthed db read exactly) →
  click the NEW `tankobanSeriesPrimaryAction` button → `tankobanSourcesSheet.visible==true`
  in SERIES MODE (no volumeId) → `hasCompileFallback==false` → whole-window grab: **67
  real live Nyaa results for Berserk** (individual per-volume releases — v41, v42, etc. —
  proving `filterAndRank(seriesMode=true)` genuinely skips the volume-target match end to
  end, not just in the pure-logic harness), header reads "Nyaa" only → dismiss. Shelf
  exhibit grab on One Piece: `On this device 0 OF 113`, real BookWalker cover art on
  volumes 1-7, primary button reads **"Get volume 1"** (not the old always-"Open volume 1"
  — Slice 2's incomplete button-text promise, closed this slice, confirmed live not just
  in source).
- **The Slice-3 tab-bar gap (`tankobanTab_manga`/`tankobanTab_discover` `NO_SUCH_ITEM` /
  `WAIT_TIMEOUT` across four isolated sessions) did NOT recur in this session.**
  `ui-click tankobanTab_manga` resolved and clicked cleanly on the first try, both times
  it was pressed (initial entry and after the world-tab-away/back regression — the second
  press even read back an off-screen `atY: -24`, still registering as a successful click,
  since the geometry query and the click dispatch race slightly differently once the item
  has settled off the visible viewport top — worth a future slice's attention if it ever
  causes a MISS rather than a harmless click-through). No `dump-ui` root-scoping
  workaround was needed to reach it this time. The task brief for this slice (citing a
  separate diagnosis pass, sessions `20260820-162647-95a9507f` and
  `20260820-163420-9f431989`) reported the gap as SOLVED via a specific mechanism: a
  hidden pre-warmed duplicate of `tankobanTabBar` exists in the tree, and `dump-ui`'s DFS-
  first resolution can match the wrong (occluded) copy when scoped ambiguously — the
  fix/workaround being to capture a FULL-WINDOW paged `dump-ui` at any failure point
  rather than a scoped one. This executor did not reproduce the failure to verify that
  mechanism directly (this session's own clicks never needed it), so it is recorded here
  as REPORTED, attributed to those two session ids, not independently re-derived — a
  future session that DOES hit `NO_SUCH_ITEM` on a Tankoban tab pill should reach for the
  full-window paged `dump-ui` first, per that diagnosis, before assuming a new gap.
- **New named surfaces added this slice** (world-namespaced): `tankobanSourcesBack` (the
  picker's `BackAction`, previously unnamed — the dismiss target); `hasCompileFallback`
  (scalar on `tankobanSourcesSheet`, always `false`); `tankobanSeriesPrimaryAction`
  (the reading room's honest open/get/search button, previously unnamed).
- **Warning gate: FAILED, evidence captured, one class of noise named — not fixed, not
  silently allowlisted.** `warnings()` against this session's `colosseum.log` + `stderr.log`
  surfaced ~20 `TypeError: Cannot read property 'revision' of null` / `Cannot call method
  'recent'/'items' of null` lines, all timestamped within the same ~10ms window
  (17:39:00.374-383) across `TankobanWorld.qml`, `TankobanLibraryTab.qml`,
  `TheatreWorld.qml`, `BiblioWorld.qml`, `LibraryPage.qml`, `BiblioLibraryPage.qml`, `Main.qml:2061`
  — none of them a file this slice touched. The single-moment, multi-world clustering
  (Tankoban AND Theatre AND Biblio all erroring at once) points at a shared `Collection`/
  `Progress` context-property construction race at first world-mode entry, not anything
  specific to the nyaa/picker work — but this executor did NOT trace the root cause or
  confirm it pre-dates this slice by bisection; it is named as a newly-OBSERVED foreign
  warning class for a future slice to adjudicate (search the ledger before this entry —
  no prior record of this exact class was found). A trailing `QIODevice::read (QSslSocket):
  device not open` line is almost certainly incidental network/SSL library noise from the
  live Nyaa RSS fetches this session deliberately exercised (not asserted on, per the
  plan's "live nyaa results are NOT a deterministic gate" — but the fetches DID run for
  real, producing the 10-source and 67-source grabs above). Full lines preserved in
  `artifacts/lanista-sessions/20260820-173802-4baf3a24/`.
- **Cover-branch note owed to Slice 7's sweep (per this slice's own instructions):** One
  Piece covers the COVERED branch (112/113). The mal-basis/uncovered branch (Vagabond,
  malId 656, `count_basis=mal`, 0/0 covers per the Slice-3 ground-truth note above) is
  still UNASSERTED by any committed scenario — this slice did not add it (out of fence;
  Slice 4's own shelf-less fixture, Berserk, has NO shelf at all, which is a different
  branch than "has a shelf with zero covers").
- **Runtime status of Slices 2-3's own claims, revised in light of this session:** both are
  now RUNTIME-VALIDATED by this slice's replay, not merely Test-reported as their own
  ledger entries left them (Slice 2 stalled at onboarding; Slice 3 stalled at Tankoban
  entry, both honestly, both now superseded by a full live run reaching every scalar both
  slices only proved by source reading or partial replay).

## Tankoban catalogue-independence Slice 6 (AMENDED, 2026-08-20) — the Discover wall's real
## scroll technique, a script-only click gap, and a window-minimize flake fully characterized

- **The Slice-2-documented "ZERO measurable movement" is SOLVED — but not the way it looks
  at first.** Ground-truthed live across three isolated sessions (tags `tk6`, `tk6b`,
  `tk6c`) that `ui-scroll` targeting a Discover delegate card DIRECTLY (e.g.
  `mangaDiscoverCard_2`) at REST produces no click-usable result, but for a subtler reason
  than "the WheelHandler never receives the event": at 1280x720, `TankobanDiscoverPage`'s
  own wall sits almost entirely BELOW the fold at rest — the "Featured in Tankoban" banner
  + tab bar + filter row already consume nearly the whole 720px viewport, so even RANK-1's
  card (materialized, `visible:true`) reports a scene-space click center around y=879,
  outside the window's own 0-720 range. `visible:true` on a GridView delegate means "not
  culled by virtualization," NOT "on-screen" — the two are easy to conflate and this slice
  did, at first. **The working technique, found live:** `ui-scroll` targeting a STABLE,
  ALWAYS-VISIBLE item (`tankobanTabBar`, not a delegate) with a moderate `dy` first moves
  the PAGE-level container down, clearing the banner dead zone; ONE such scroll is enough
  (repeat calls on the same stable target stop producing movement once it scrolls outside
  the wall's own bounds — expected, not a bug). Once the wall itself is on-screen, handing
  off further `ui-scroll` calls to a currently-MATERIALIZED delegate's own objectName
  (`mangaDiscoverCard_<malId>`) DOES move real content, monotonically, confirmed via
  `qml-get` on the GridView's own ephemeral snapshot handle (`contentY`/`contentHeight`
  tracked frame-to-frame) as well as visually (whole-window grabs matching the live
  `data/mal_catalog.db` popular-order malIds exactly: rank1 Berserk id2, rank11 Naruto
  id11, rank18 Spy x Family id119161). Reach achieved and reproduced 3× scripted
  (`tests/lanista_scenarios/tankoban_discover_depth.json`, sessions `20260820-203921-
  1bba75ed`/`20260820-204140-d87b4944`/`20260820-204605-ebe6608d`, the last one committed
  as the clean evidence run): rank 1-18 (3 materialized rows), after which further scrolls
  by the identical technique stop producing additional movement or additional
  `requestPage()` growth in the same session — a genuine, now-precisely-named automation-
  reach ceiling (not root-caused this slice: candidates are the GridView's plain-integer
  `model:` binding resetting scroll on every page-load-driven count change, or the
  ScrollGlide `FrameAnimation` simply running out of queued backlog with no further trigger
  — a future slice should instrument `browser.loading`/`exhausted` through a bridge-
  addressable scalar before guessing further).
- **A genuinely reproduced window-minimize flake, now more precisely characterized than the
  Slice-2 diagnosis's "once left the window itself minimized mid-sequence."** Two earlier
  isolated attempts this slice (tag `tk6`, session `20260820-201428-0bcae05f`: `ui-keypress
  End`/`PageDown` sent to the window after a tab round-trip; tag `tk6b`, session
  `20260820-202230-e5ac8b98`: the FIRST `ui-scroll` call of the session, against a delegate
  card, before any page-level scroll had cleared the banner) both left `get-state`/`grab`
  reporting the session's own root window `state:"minimized", active:false` — confirmed via
  a bare `grab target=window` returning a blank white capture. Both times this happened
  BEFORE any successful content movement was observed, and a third session (`tk6c`) that
  used the two-stage scroll technique from a healthy baseline (confirmed `state:"normal"`
  before driving anything) never reproduced it across ~20 subsequent scroll/click actions —
  suggesting the minimize correlates with driving `ui-scroll`/`ui-keypress` against a
  delegate whose on-screen position is invalid (below the fold) rather than being a random
  flake, though this is a correlation observed across 3 sessions, not a proven mechanism.
  No `window-set-state` capability is exposed through the `mcp__lanista__*` session-adapter
  tools available to this executor (only `session_start`/`act`/`get`/`wait_for`/`grab`/
  `snapshot`/`warnings`/`session_stop`/`vault_forensics`/`lanista_call`, the last scoped to
  the DAILY pipe only per its own tool description) — once minimized, this executor's only
  recovery was `session_stop` + a fresh `session_start`, not an in-session restore. A future
  slice should either expose `window-set-state` through the session adapter or root-cause
  why an off-screen-target scroll/keypress correlates with a real OS-level minimize.
- **A NEW script-only automation gap: the deep-rank card CLICK does not reproduce in a
  scripted `session run`, despite being proven live by hand.** Hand-driven (tag `tk6c`,
  session `20260820-202607-0ade0c74`): after reaching rank 11 (Naruto, malId 11) by the
  two-stage scroll technique above, `ui-click target=mangaDiscoverCard_11` navigated
  cleanly and `tankobanSeriesMasthead` resolved correctly (`ready:true`,
  `displayTitle:"Naruto"`, `resolvedMalId:"11"`, `hasShelf:true`, `primaryAction:"get"`),
  whole-window grab confirms a fully-rendered series page with real BookWalker covers on
  volumes 1-7. The IDENTICAL scroll-then-click sequence, scripted via `lanista session run`
  (sessions `20260820-203921-1bba75ed` and `20260820-204140-d87b4944`), failed 2/2 times —
  the masthead never became ready and the post-failure grab shows the app STILL on the
  Discover wall, meaning the click did not navigate at all. Adding settle reads (3× extra
  `qml-get` round trips before the click, to rule out an animation-still-draining race) and
  a belt-and-braces re-click (`ui-click` a second time on the same target — safe, since
  `ui-click` carries no `expect` and can never manufacture a false PASS) did NOT fix it
  (session `20260820-204302-0552eaed`, still failed identically). Root cause not
  identified this slice — candidate hypotheses (not verified): a DFS-first objectName
  resolution race against a delegate that JUST finished recycling into that name (the
  binding for `objectName` on `CataloguePosterCard` recomputes from `card.item.id`, which
  could theoretically lag one frame behind a fast-scrolled recycle), or a scenario-runner
  timing difference from interactive driving that this executor did not instrument
  further. Because it could not be made to reproduce reliably, the committed
  `tests/lanista_scenarios/tankoban_discover_depth.json` deliberately STOPS at the
  materialization proof (rank 1-18 reached, whole-window exhibit grabbed) and does NOT
  include the click/masthead steps — shipping an unreliable step as a "gate" would
  misrepresent it, per the same honesty standard Slice 2/3's own partial scenarios set.
  The click-to-masthead path itself remains Runtime-validated (by the hand-driven session
  above, plus Slice 4's own independent proof via the "Top in Tankoban" rail) — only the
  SCRIPTED reproduction of reaching it via deep Discover scroll is the open gap.
- **Ground-truthed pinned series for a future attempt (real db lookups, 2026-08-20):** Hal
  (malId 49611) sits at live popular-order rank 3000 (offset 2999), `tankoban_catalog.db`
  carries `volume_count=1, count_basis=mal` — the plan's originally-requested "inside the
  10k band, rank 2500-5000" fixture, NOT reached through the bridge this slice (scroll
  depth capped at rank 18, four orders of magnitude short). Baby Princess (malId 8676) sits
  at live popular-order rank 15000, ABSENT from `tankoban_catalog.db`'s `series` table
  entirely (0 rows for that malId, confirmed by direct query) — the plan's "absent from the
  10k band" fixture; also not reached. No search-to-series bridge route exists to reach
  either without scroll depth (`qml/TankobanWorld.qml`/`qml/Main.qml` grep found no global
  search-to-series signal path this slice), so both remain candidates for Slice 7's
  eyes-on list or a future slice's bridge-addressable jump seam, not silently dropped.
- Warning gate: `WARNING_GATE_OK` on the committed scenario's own clean session
  (`20260820-204605-ebe6608d`). The two earlier isolated diagnostic sessions this slice
  (tags `tk6`/`tk6b`, not committed as scenarios) surfaced the SAME pre-documented
  `TypeError: Cannot read property 'revision' of null` / `Cannot call method 'recent'/
  'items' of null` cluster Slice 4's own ledger entry already named (foreign, not
  triaged again here per the task's own instruction) plus the routine `QIODevice::read
  (QSslSocket): device not open` teardown line.

## Tankoban catalogue-independence Slice 5 (2026-08-20) — Bridge blocked by an exe lock,
## not a code or bridge defect; QML/C++ layers proven, runtime layer honestly deferred

Slice 5 (the surgical unplug: chapters removed, WC disconnected, one-time chapter-store
migration) is destructive by design — Hemanth's explicit lock, first daily boot on the
migrated build deletes his real WC-era chapter downloads. Full account in
`docs/colosseum-test-verification.md`'s Slice 5 gate entry; this entry covers the runtime
layer only.

- **No Lanista session was run this slice.** Hemanth's daily `colosseum.exe` (PID 9296)
  was running for the whole session and was never killed, per the plan's own standing
  constraint. `cmake --build --target colosseum` compiled all 35/35 objects clean
  (including the new migration class and the main.cpp hook) but failed to LINK
  (`LNK1104: cannot open file 'colosseum.exe'`) because that PID holds the file. A Lanista
  session needs a colosseum.exe that actually contains this slice's C++ to prove anything
  about the migration; none could be produced this pass.
- **Not written as unverified/speculative gates:** the plan's NEW seeded-fixture
  `tests/test_tankoban_chapter_migration.ps1` disk-gate runner, and the regression replay
  of both committed scenarios (`tankoban_catalogue_smoke.json`,
  `tankoban_discover_depth.json`) against a rebuilt exe. Writing a `.ps1` against
  `--seed`/`COLOSSEUM_APPDATA_TAG` mechanics without being able to execute and watch it
  fail-then-pass would ship a guess dressed as a gate — deferred honestly instead, to be
  written AND run together in the next pass once the daily app is closed.
- **Bridge status:** available (no new capability gap found or claimed) — the block is
  purely the file lock, not a Lanista/ledger limitation. Once Hemanth closes the daily
  app: rebuild, then replay both committed scenarios fresh (masthead/shelf/picker/wall
  paths never touch chapters, so they should stay green unchanged), then write and run the
  seeded migration `.ps1`, then the five human-witnessed items from the executor's report.
- **Safety note (recorded, not alarmed):** this slice's QML edits (chapter UI removed from
  MangaSeries/MangaReadingRoom/MangaTankobanLibrary) sit in the SAME source tree
  colosseum.exe reads QML from directly — if Hemanth's running instance loads QML live
  (not from a compiled resource) and he opened a manga series page during this session, he
  may have already seen the chapter UI disappear. Cosmetic and reversible only: no C++,
  no migration, no data deletion reaches a running process without the blocked rebuild +
  relaunch.

---

## Tankoban catalogue-independence closing sweep (2026-08-21) — the exe lock cleared; runtime layer proven

Full account (build/CTest/QML/ps1 detail) in `docs/colosseum-test-verification.md`'s matching
entry; this entry covers the Lanista-specific runtime findings only.

- **Bridge status: available, no new capability gap.** All sessions used the standard
  `lanista session run --drive --seed --tag` shape plus the interactive `mcp__lanista__*`
  adapter for two exploratory checks (the Vagabond uncovered-shelf branch, and diagnosing
  the migration scenario's rail-click race). No new command was needed or invented.
- **Build-slot collisions (3 total this window) — none resolved by killing a foreign
  process, per standing rule.** Two occurred before this sweep's own work began (the
  22:46 `arc12_theatre_anime` session that triggered the original STOP-and-report; already
  exited by the time it was traced). A THIRD occurred mid-sweep (00:00-00:02): Agent 4's
  `lanista.exe session run tests\lanista_capture\arc12_theatre_discover_movies_direct.json
  --tag arc12-default-final-theatre-discover` cycled live `colosseum.exe`/`lanista.exe`
  processes despite the posted chat.md hold. Per the coordinator's own bounded-wait
  adjustment, waited it out (~3 minutes, polling) rather than stopping — cleared on its
  own. No process was killed at any point this sweep.
- **A NEW script-only automation gap, same class Slice 6 already named for Discover,
  now also hit on the Slice-5 migration scenario's rail click.** Clicking
  `tankobanTopMangaTile_0/1` immediately (or even after 2-3 extra `qml-get` settle round-
  trips) after a `tankobanTab_manga` click reliably hit `NO_SUCH_ITEM` when driven through
  batched `lanista session run` — but resolved on the FIRST try, no settle needed, when
  driven interactively via `mcp__lanista__act()` (session `20260820-234936-963268d6`:
  ping-through-masthead in 4 single act() calls, zero retries). This is strong evidence for
  Slice 6's own "scenario-runner timing difference from interactive driving" hypothesis
  over its other candidate (a DFS-first objectName recycling race) — the interactive
  adapter's natural per-call round-trip latency appears to be exactly what the batched
  runner never gives the QML Loader/Repeater. Not root-caused further (would need
  instrumenting the actual Loader.onLoaded / Repeater itemAdded timing, out of this sweep's
  scope). Resolved pragmatically the same way Slice 6 did: the shipped
  `tankoban_chapter_migration.json` scenario stops at what batched driving proves reliably
  (clean boot through Tankoban's tab bar rendering); the deeper masthead check for that
  scenario is hand-driven evidence only.
- **A SEPARATE, newly-reproduced defect class on the EXISTING committed
  `tankoban_catalogue_smoke.json`: a stale-property read on late re-navigation, not a
  click-target-resolution race.** 4 fresh isolated replays this sweep (tags
  `closing-smoke`, `closing-smoke-r2`, `-r3`, `-r4`): run 2 hit the click-target class
  above (early failure); runs 1, 3, 4 all reached the scenario's FINAL Berserk-shelf-less
  assertion cleanly (the click landed, `tankobanSeriesMasthead.ready` flipped true, the
  `ui-wait-for` matched) but the immediately-following `qml-get` read
  `displayTitle=="One Piece"` — the PREVIOUS series still-shown, in 3 of those 3 attempts.
  Every other assertion in all 4 runs passed clean, including two EARLIER masthead
  re-navigations in the same scenario (the world-tab-away/back regression leg) — this
  specific race only manifested on the scenario's THIRD-plus series-page open, late in a
  31-step sequence. Read as `ready` and `displayTitle` (or the underlying `mangaById`
  row bind) updating on different frames/ticks under load, exposed only after several
  prior navigations have already exercised the page's re-bind path. Named here as new,
  not swept into the already-known click-race class above; not fixed (out of this sweep's
  "verification only" fence) — a candidate for whoever next touches `MangaSeries.qml`'s
  `resolve()`.
- **Warning gate: clean across every session this sweep drove.** `WARNING_GATE_OK` on
  `tankoban_discover_depth.json`'s replay and on the smoke scenario's run 4 (the deepest
  clean run). No new warning-noise class surfaced; the pre-documented
  `TypeError: Cannot read property 'revision'/'items' of null` cluster and the routine
  `QIODevice::read (QSslSocket): device not open` teardown line were not observed this
  sweep (may be load-dependent — not chased).
- **Step E, interactive adapter used for a genuine exploratory reach (not a scripted
  gate):** Vagabond (malId 656, static rail index 3) opened in one clean single-session
  pass — `hasShelf true`, `primaryAction "get"`, shelf `rowCount 37`/`coveredCount 0`,
  confirming the mal-basis/uncovered-shelf branch (the plan's requested fixture) renders
  honestly. Hal/Baby Princess remain unreached (Slice 6's own finding stands — no
  search-to-series bridge seam exists yet).
- **RQ for a future slice, not filed as a formal ticket here:** two real gaps now sit in
  the ledger unfixed (the progress-purge account-rebind gap; the masthead stale-read
  race) plus one confirmed, reproducible script-only automation gap. None of the three
  block Hemanth's own eyes-on checklist.

---

## Catalogue-independence closing-sweep FOLLOW-UP fixes (2026-08-21) — bridge-layer status

Full mechanism, code diffs, and Qt Test/static evidence in `docs/colosseum-test-verification.md`'s
matching entry; this entry covers the Lanista/bridge-specific status only.

- **Bridge status for both fixes: bridge blocked, not a defect in either fix.**
  `native/build-msvc/colosseum.exe` (PID 18392, started 00:31:52) was live for this entire
  pass — the closing sweep's own written human-witnessed checklist (`artifacts/tankoban-
  independence/closing/human-witnessed-checklist.md`, item 2) names THIS exact build's next
  boot as Hemanth's real first daily boot of the migrated app (the actual one-time
  destructive chapter purge on his real AppData root), so this was very plausibly not a
  spare/forgotten process. Never killed, per standing rule; polled bounded (~17 min direct
  wait at the claim, ~37 min total elapsed since it was first observed running) rather than
  block indefinitely, per this task's own "poll-wait bounded ~10min" instruction. Claim +
  status posted to `agents/chat.md` before this session block; no Lanista session was driven
  against the live process at any point.
- **No scenario changes needed for either fix's own gate.** The already-committed
  `tests/lanista_scenarios/tankoban_chapter_migration.json` already clicks
  `accountWelcomeContinueLocal` (the exact onboarding rebind Defect 1's fix depends on) —
  Defect 1's fix should turn its existing progress-purge assertion truthful once rebuilt and
  rerun, no scenario edit required. `tests/lanista_scenarios/tankoban_catalogue_smoke.json`
  already carries the late-re-navigation Berserk-shelf-less leg the closing sweep caught the
  race on — Defect 2's completion criterion is a plain 4x fresh-session replay of the SAME
  committed scenario, no edit required there either.
- **Next actor, once `colosseum.exe` is free:** (1) rebuild
  (`native/build-msvc.bat` or the targeted `native/_slice5_build_app.bat` — full relink,
  since `native/main.cpp` changed); (2) rerun `tests/test_tankoban_chapter_migration.ps1`
  for the `TANKOBAN_CHAPTER_MIGRATION_OK` sentinel plus its new durable-ini records check;
  (3) replay `tankoban_catalogue_smoke.json` in 4 fresh isolated tagged sessions and confirm
  the final Berserk `displayTitle` assertion passes 4/4 (previously 3/4); (4) flip this
  entry's and the matching test-verification.md entry's status lines to Runtime-validated
  once both gates are green, and flip Slice 5's own progress-purge line in the closing-sweep
  entry above from "confirmed broken" to fixed, citing this entry.

---

## Held runtime gates closed (R1 sweep, 2026-08-21) + Slice R1 landed — bridge-layer status

Full mechanism, code diffs, and Qt Test/harness evidence in `docs/colosseum-test-
verification.md`'s matching entry; this entry covers the Lanista/bridge-specific status.

- **Held gate 1 (chapter-migration disk gate): GREEN.** `tests/
  test_tankoban_chapter_migration.ps1` in a fresh isolated seeded session (tag
  `tankoban-chmig-*`): `TANKOBAN_CHAPTER_MIGRATION_OK`, all disk + durable-ini checks pass.
  A genuine defect was found and fixed in the SAME pass (a log-line gap in the `.ps1`'s own
  assertions, not a production bug — see the test-verification.md entry) via a real
  red-then-green cycle, not a synthetic negative control.
- **Held gate 2 (masthead race): the underlying defect is closed; the exact "4/4 CLI
  replay" form of the gate is not what closed it.** An interactive lanista session (tag
  `r1sweep-diag`, session `20260821-013744-532e91b6`) ground-truthed that Defect 2's fix
  works correctly whenever `resolve()` actually runs, and separately isolated a REAL,
  previously-uncharacterized scenario defect (`tankobanTab_manga`/`tankobanTopMangaTile_*`
  clicks are absorbed by `MangaSeries.qml`'s own full-window MouseArea while a series page
  is showing — not a masthead race). Fixed in `tests/lanista_scenarios/
  tankoban_catalogue_smoke.json` (an explicit `tankobanReadingRoomBack` close-and-settle
  before every such navigation). A SEPARATE, unrelated `modePill_Biblio`/`modePill_Tankoban`
  back-to-back click race was also found (3 of 4 replays this pass) and is NOT fixed —
  named as a next-actor handoff, candidate root cause (duplicate objectNames across
  keep-alive per-world Loaders) recorded but not confirmed.
- **Slice R1 ("nyaa ships dark"): Runtime-validated.** New committed scenario `tests/
  lanista_scenarios/tankoban_nyaa_dark_gate.json` — fresh tag, extensions default dark ->
  picker shows `sourcesEnabled==false` + the honest empty state + `tankobanSourcesEnableRoute`
  -> route opens Extensions on the Tankoban world -> Installed pane -> `extensionToggle_
  colosseum.well.nyaa` enabled -> picker reopened fresh -> `sourcesEnabled==true`, live
  rows land. Passed clean on repeated fresh replays this pass. A bridge coordinate-cache
  nuance was ground-truthed and worked around with real-property settle waits (never a
  sleep): a click fired immediately after a scroll, or immediately after a pane/tab switch,
  can resolve a stale screen position — reproducible, not flaky-by-luck; noted here for any
  future scenario touching a scrolled Flickable or the Extensions page's pane tabs.
- **Full `-L unit`: 71/71 green** after all fixes (`colosseum.extensions_first_run`,
  `colosseum.qttest.tankoban_chapter_migration`, `colosseum.manga_series_catalogue` all
  included). Warning gate: the pre-existing `revision of null` boot-race class (11 hits,
  reproduced BEFORE any R1 edit existed, confirmed foreign) is unrelated to this pass;
  zero warnings reference any R1-touched file across the clean scenario runs.
- Claim + release posted to `agents/chat.md` around this session block per standing
  discipline; commit is a surgical blob for `qml/Main.qml` (carries an unrelated
  brother's foreign in-flight hunks — `git diff qml/Main.qml` post-commit still shows
  them, untouched, in the working tree).

---

## Status vocabulary (for plans and reports)

`Runtime-validated` is the only status that closes a user-visible slice without qualification.
The others — `Implemented, verification pending` · `Bridge blocked` · `Verification failed` ·
`Plan contradicted` · `Test-reported` — are honest intermediate states, not failures of nerve.
A green unit suite is `Test-reported`, never `Runtime-validated`.
