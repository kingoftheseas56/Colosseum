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
- Existing scenarios: `self_smoke.json`, `self_visual.json` (harness-fixture-bound),
  `app_home.json` (real app, boot-first, not in the CI gate).

### MCP adapter (`native/tools/lanista-mcp/server.py`, registered in `.mcp.json`)

- Three tools: `lanista_call` (generic cmd+payload passthrough), `lanista_grab` (returns the PNG
  inline as image content), `lanista_snapshot`.
- **No deadlines anywhere** — a hung app hangs the adapter's blocking pipe read. Prefer the CLI
  (`lanista --timeout`) when the app's health is itself in question.
- Assumes adapter and app share a filesystem; grab PNGs are never cleaned up.

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
7. **MCP facade** — typed tools (`lanista_session_start`, `lanista_act`, `lanista_wait`,
   `lanista_probe`, …) with real deadlines; refuse the daily-app pipe unless explicit.
8. **WebEngine + media observation** — read-only URL/DOM-readiness/console and
   source/buffering/seek probes behind the proper gates.
9. **`window-set-state` (Drive-gated)** — minimize/restore/normal via the real QWindow
   path, demanded by the three-layer minimize/restore regression (test ledger, slice 7):
   the bridge cannot reach the Windows taskbar, so restoring a minimized session is
   impossible today and that slice's runtime layer is Bridge blocked on exactly this.

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
