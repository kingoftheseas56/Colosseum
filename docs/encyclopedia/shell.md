# Shell — subsystem guide

> **Hand-written. Keep it true.** If you change how the app boots, how modes or sessions route,
> or how the taskbar works, update this file in the same commit. The per-file index beside it
> ([`shell-index.md`](shell-index.md)) is generated — never edit that one.
>
> Verified against `master` (native-boot shell). Drafted via the encyclopedia arc, ground-truthed
> and adopted by Agent 0. Related guides: [`player.md`](player.md) (video playback surface),
> [`comics.md`](comics.md) (comics ingest/reader). Mode content lives there, not here.

## 1. What this subsystem is for

Boot the app, route between its three worlds (Tankoban · Theatre · Biblio) and the surfaces inside
them, and keep the open-sessions model + taskbar honest — without ever persisting a session.

## 2. The flow

**Boot** — process start to shell revealed:

```
native/main.cpp (1,554 lines, the launcher)
  1. RHI pick BEFORE the app object: OpenGL default; D3D11 only on a Player-2 build with
     COLOSSEUM_PLAYER2=1        (main.cpp:436–476; the player guide's Trap 1)
  2. WebEngine init, QT_QUICK_CONTROLS_STYLE=Basic, org identity "Brotherhood" + one-time
     data-dir move, AppLog                 (main.cpp:477–540)
  3. Net layer: IPv4 pins (dead-AAAA stall), loopback concierge for metahub, poster
     scoreboard, zero-byte image-cache sweep (main.cpp:596–742)
  4. ~30 context-property stores registered (Progress, Sessions, WindowMode, Power,
     Collection, SearchHistory, …) + qmlRegisterType<MpvItem>   (main.cpp:543–1124)
     4a. Account runtime (Bundle 8C, 2026-08-17): AccountRuntime is now the SOLE
     owner/binder of Progress/Collection/SearchHistory/AudioPairing (plus
     ProfilePreferences/ProfileHistory/ProfileContext/AccountController/
     AccountRecoveryKey); the four raw store constructions are gone. Boot starts
     sealed behind the onboarding gate; the account choice rebinds stores.
  5. engine.load(qml/Main.qml); LanistaServer ALWAYS on; live-reload only under COLOSSEUM_DEV
                                                          (main.cpp:1125–1142)
qml/Main.qml (3,397 lines, the dispatcher)
  6. Window starts hidden; WindowMode.initializeShell picks the presentation (fullscreen
     default / last stable windowed)          (Main.qml:30–32, 223–228)
  7. BootSplash prefetches catalog covers → finished() → fade → shell revealed
                                                          (Main.qml:2918–2929)
```

**Modes and sessions** — once the shell is up, two separate lifecycles coexist:

```
openWorld("Tankoban"|"Theatre"|"Biblio")            → keep-alive world Loader created on first
   visit, NEVER destroyed; worldStack.current toggles visibility (Main.qml:403–430, 2056–2072).
   A hidden warming timer pre-builds unvisited worlds ahead of first click.

openMovieSession / openComicSession / openBook / hosted → Sessions.openOrSwitch({appType,
   contentKind, target, title})                       (Main.qml:1092–1400)
   → Sessions.activeChanged fires → Main.qml switch glue:
       captureSession(prev) → saveState → teardownSession(prev) → activateSession(next)
                                                          (Main.qml:2685–2704)
   → activateSession builds the surface from target + savedState (movie / hosted-video /
     comic / book branches)                             (Main.qml:1509–1623)
   → Taskbar re-draws from Sessions.groups()            (Taskbar.qml:11)
```

**Approach 2 is the load-bearing idea:** only the **active** session is ever instantiated. A
background session is a descriptor + a saved-state blob in `SessionStore` — not a live page.
Minimize = teardown with a saved blob; the taskbar keeps the tile; reopening rebuilds.

**Vault launch entry points (Slice 8)** — a local file handed to the app enters the SAME session
flow. `Taskbar` Open Media… (`taskbarOpenMedia`) / an app-wide `DropArea` / `Ctrl+O` →
`win.openLocalMedia()` → `LocalLaunch.open()` (C++) classifies + backend-validates + rejects the
unopenable BEFORE any session, returning a decision → the right door: `openVaultComic` (a standalone
`ComicReaderShell` host, `VaultComicReader.qml`, fed the injected `VaultPageStore` — no series page)
/ `openBookSession` / `openLocalVideoSession`. `activateSession`'s comic branch routes a
`target.vaultPath` session to `vaultComicLayer`. A categorized rejection shows a quiet toast and NO
taskbar tile. `localLaunchState` (objectName) exposes `openCount`/`lastRouteKind`/`lastRejectCategory`
for automation. A dropped FOLDER explains + offers the picker (the folder→Vault gesture is Slice 10).

## 3. The files that matter

| File | Role |
|---|---|
| `native/main.cpp` | the launcher: RHI/WebEngine boot, net layer, ~30 store registrations, env self-tests, LanistaServer |
| `native/SessionStore.h` | the sessions model (`Sessions`): openOrSwitch/dedup/one-tab-per-show, activeId, savedState; **in-memory only** |
| `qml/Main.qml` | the dispatcher + door layer: world routing, session switch glue, wallpaper, every surface Loader; the **Living Guide shell route** — `guideLayer` (z:59) floats `GuidePage` over the current surface with the taskbar pinned (`openGuidePage`/`closeGuidePage`), a taskbar click routes via `open*Page` while Guide is up, and the shell Escape + UpdatePage yield their Escape (`enabled: !guideLayer.active` / `!guideActive`) so GuidePage's own Escape is the sole owner while Guide floats (two enabled Escape shortcuts on one window fire neither) |
| `qml/Taskbar.qml` | the auto-hiding switcher bar: app-grouped icon circles from `Sessions.groups()`, switch/close/fan, shell buttons + the Open Media… launch control (`taskbarOpenMedia`) + the **Living Guide door** (`colosseumGuideTaskbarButton` → `guideClicked`, `guideActive` underline) |
| `native/engine/LocalLaunch.{h,cpp}` | the Vault launch router + QML `open()` orchestration: classify → backend-validate → route a handed-in file, rejecting before any session (Slice 7/8) |
| `native/engine/VaultPageStore.{h,cpp}` | `ComicReaderShell` injected-store adapter for a local CBZ — same `[{index,archive,entry,group}]` descriptors as the Tankoban volume lane, zero reader edits |
| `qml/comicreader/VaultComicReader.qml` | standalone comic reader host for a single loose CBZ (no series page): wraps `ComicReaderShell` with the injected Vault store, hosted by `vaultComicLayer` |
| `qml/BootSplash.qml` | OS-style boot loader: cover prefetch + progress bar, hard timeout, then reveal |
| `qml/TopBar.qml` | the one shared top chrome (clock/date, medium pills, system icons) across home + worlds |
| `qml/WorldTabBar.qml` | parameterized glass tab bar (Tankoban uses it; Theatre has the `TheatreTabBar` twin) |
| `native/player/windowmodestore.{h,cpp}`, `windowstatepolicy.{h,cpp}` | the shell window authority: fullscreen / F11-windowed / PiP, geometry policy (overlaps player.md — see there) |
| `native/ProgressStore.h` | `Progress` — Continue backbone; the thing that *recreates* sessions after restart |
| `native/CollectionStore.h`, `SearchHistoryStore.h` | cross-world persisted shelves: "Your Collection" + search MRU (one store, three worlds) |

## 4. Where state lives

- **Sessions — in memory ONLY.** `SessionStore` has no persistence and survives nothing
  (SessionStore.h is pure QObject). A session descriptor is `appType` / `contentKind` / `title` /
  `target`; `savedState` is captured per switch. **What does not persist:** the open sessions, the
  taskbar layout, and any background surface. After a restart, Continue Watching tiles
  (`Progress`) are what re-open movies/comics/books — each tile calls `openMovieSession` etc. and a
  fresh session record is born (Main.qml:1246).
- **Continue / Collection / SearchHistory — QSettings JSON blobs.** Untagged (the daily app):
  registry, org `Brotherhood` app `Colosseum` (Roaming/Brotherhood/Colosseum, unchanged since the
  Trap 7 org-name move): `continue/entries`, `collection/entries`, per-scope search lists. Tagged
  (`COLOSSEUM_APPDATA_TAG` set): diverted to a FILE — `<tag's AppDataLocation>/progress-store.ini`
  / `collection-store.ini` / `search-history-store.ini` — never the registry (2026-08-14 isolation
  fix, see Trap 12; `ProgressStore.h`/`CollectionStore.h`/`SearchHistoryStore.h`). The org name
  still matters for the untagged path — see Trap 6.
- **Wallpapers — `wallpapers.ini`** at repo root: `homePick` / `tankobanPick` / `biblioPick` /
  `theatrePick` (Main.qml:54–62, 101–112).
- **Global prefs — `ContentPreferences`** (QML, app QSettings): showExplicit and friends, pushed
  into TheatreApi at boot (Main.qml:64–67, 264–267).
- **Window mode — WindowModeStore** QSettings: fullscreen vs F11-windowed, saved geometry, PiP.
- **Shell-level env gates** (never shipped behaviour): `COLOSSEUM_APPDATA_TAG` (isolated AppData
  for tests), `COLOSSEUM_SESSION_SELFTEST`, `COLOSSEUM_DEV` (live reload + debug logs),
  `COLOSSEUM_OPEN_WORLD`, `COLOSSEUM_OPEN_EXTENSIONS` (main.cpp:530–533, 1115–1116, 1134–1142).

## 5. Traps

1. **Sessions are ephemeral; worlds are immortal — don't confuse the two lifecycles.** Worlds
   (`openWorld` → keep-alive Loader, visibility toggled, never destroyed — Main.qml:403–430,
   2056–2072) exist to keep covers warm. Sessions (Approach 2) are torn down on switch and exist
   only as descriptors + savedState. A "hidden page" you're looking for is usually a world, not a
   session — and if it's a session, it doesn't exist anywhere but in `SessionStore`.
2. **The world warmer was THE 2026-07-29 video stutter.** The 1.8s warming timer built hidden
   ~190-tile world pages **behind the film** because it only checked `worldStack.current`, not
   `immersiveSurfaceOpen` — measured 130 GUI-thread stalls in 76s, worst 1094ms. Any new
   background work in the shell must yield on `immersiveSurfaceOpen` exactly like the warmer does
   now (Main.qml:2036–2054). (Same root cause as player.md Trap 2 — the GUI thread gates the
   picture.)
3. **Minimize keeps the player WARM; close is the only hard stop.** `teardownSession` on a movie
   calls `suspendForMinimize` (pause + hide, never `stop()`), and reopen resumes in place with no
   re-stream (`warmPlayerSessionId`) (Main.qml:1518–1520, 1642–1648). Hosted-video is the
   deliberate opposite: its WebEngine page + off-the-record profile are **destroyed** on minimize;
   restore rebuilds the embed (Main.qml:1552–1562, 1649–1655). Don't "optimise" one to match the
   other.
4. **Closing the active session lands on the world behind — never on a parked neighbor.**
   `SessionStore::close()` sets `activeId` to `""` when the closed session was active; it does
   NOT promote the adjacent record. It used to (browser-tab semantics), and the symptom was
   real: close a comic while a movie sits minimized and the movie force-restores, because the
   switch glue (Main.qml:2552–2567) rebuilds whatever becomes active. Sessions are taskbar
   windows, not tabs — a minimized session comes back only from its tile (Hemanth-ruled
   2026-08-08; selfTest asserts the empty landing).
5. **One-tab-per-show replace clears savedState on purpose.** Same `showKey`, different content →
   same tile, new target, old position deliberately dropped (it described the old content).
   Identical content reuses and KEEPS state (SessionStore.h:51–61). A "position lost when I
   switched episodes" report is this behaviour, not a bug.
6. **Retired "audiobook" sessions.** contentKind `audiobook` was retired with the standalone
   player (2026-07-18); a stale taskbar record of that kind activates to nothing and closes
   normally. Don't resurrect that kind (Main.qml:1621–1622).
7. **App data lives under org "Brotherhood" — and used to not.** `setOrganizationName` moved the
   data dir from Roaming/Colosseum to Roaming/Brotherhood/Colosseum; main.cpp performs a
   **one-time guarded move** of the old tree (main.cpp:500–516). Never move data dirs manually —
   that migration's guard is the only thing protecting a user's downloads/books/videos. Test runs
   must use `COLOSSEUM_APPDATA_TAG`, never touch the real tree.
8. **Automation must wait for `bootSplash.visible == false`.** Lanista scenarios clicked "green"
   on the occluded tree while the splash still owned the screen — a vacuous pass (Main.qml:2920–
   2922). The splash exposes `objectName: "bootSplash"` for exactly this wait.
9. **WorldTabBar objectName collisions across worlds.** Two worlds sharing a tab key (both
   Tankoban and Biblio have a "library" tab) MUST set a distinct `tabPrefix`, or the DFS
   objectName lookup resolves the wrong hidden pre-warmed pill (observed: Tankoban's
   `worldTab_library` ate Biblio's click, 2026-08-06 — WorldTabBar.qml:14–19, 48–53).
10. **The QML tree is live and self-updating.** The launcher runs `git pull --ff-only` on every
   argless boot — QML changes land instantly, `native/` changes are log-only until a rebuild
   (main.cpp:566–592). Under `COLOSSEUM_DEV` there's also a live-reload watcher that reloads the
   root window on save (main.cpp:382–433). A stale QML tree on disk IS the running app.
11. **`Player2Available` is a boot fact, and the shell must not route around it.** `usePlayer2`
    is `Player2Available === true` (Main.qml:833) and `playerLayer.source` picks the page from it
    (Main.qml:2536). Reporting true on an OpenGL boot is what made Player 2 take playback it could
    never render (2026-07-25, main.cpp:1067–1073). See player.md Trap 1 for the full story.
12. **A hardcoded QSettings(org, app) constructor bypasses `COLOSSEUM_APPDATA_TAG` entirely — it
    is NOT an AppData-derived path.** `ProgressStore`, `CollectionStore`, and `SearchHistoryStore`
    all used to construct their `QSettings` with the literal two-arg constructor
    (`QSettings("Brotherhood", "Colosseum")`), which resolves straight to the Windows registry
    regardless of `QCoreApplication::applicationName()` — so a tagged/isolated test session still
    read AND wrote the real user's Continue map, Collection shelf, and search history (proven:
    a test journey wrote a manga entry into the real registry, 2026-08-14). Fixed by having each
    store resolve `qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG")` at construction and, when
    set, build via its own `IniFormat` constructor with a path under
    `QStandardPaths::writableLocation(AppDataLocation)` (already re-rooted per-tag by Trap 7's
    mechanism) instead of the registry. Untagged behavior (the daily app) is unchanged. Any FUTURE
    QSettings-backed store must follow this pattern (or the already-safe pattern used by
    `AudioPairingStore`/`WindowModeStore`/the torrent stores: a plain default-constructed
    `QSettings()`, which resolves through the CURRENT `applicationName()` and is tag-safe on its
    own) — never hardcode the org/app pair.

## 6. How to test it

- **Unit (ctest):** `ctest -L unit` covers the shell's stores: `colosseum.window_state_policy_harness`,
  `colosseum.search_history_store_harness`, `colosseum.collection_store_harness`, the QtTest
  `colosseum.qttest.window_state_policy`, and the QML suite `colosseum.qml` (tests/qml, incl.
  `tst_search_history_flow.qml`) (tests/CMakeLists.txt:28–103). `colosseum.qttest.store_isolation`
  proves Trap 12: under `COLOSSEUM_APPDATA_TAG`, `ProgressStore`/`CollectionStore` writes land in
  the tagged file and the registry is untouched; untagged, the registry path is unchanged.
- **Session semantics:** `COLOSSEUM_SESSION_SELFTEST=1` runs SessionStore's own selftest at boot
  (dedup, one-tab-per-show replace, group shape — logs `[session-selftest] PASS/FAIL`,
  main.cpp:1115–1116). `tests/test_one_tab_per_show_p0.ps1` is a shape-only contract gate over it.
- **Window/PiP:** `window_shell_gui_harness` (offscreen QPA: PiP round-trips, F11-in-PiP,
  live-reload re-attach — run directly with `-platform offscreen`, not ctest-registered) and
  `window_behavior_harness.qml` (QtTest: drag/double-click/edge-resize).
- **Text-contract gates:** `tests/test_shell_windowed_mode_p0.ps1`,
  `test_chrome_fullscreen_toggle_p0.ps1`, `test_taskbar_*.ps1`, `test_continue_*` — grep-assert
  wiring, never runtime.
- **Runtime bridge:** `tests/lanista_harness.cpp` boots the real engine; scenarios drive the
  shell via `LanistaServer` and wait on `bootSplash` visibility before acting.
- **What it cannot cover:** fullscreen geometry on real screens, boot pacing (splash prefetch
  timing, warm-cache vs cold), and the feel of the taskbar fan — those need eyes on the running
  app.

## Keeping this page honest

```bash
# refresh the index after editing any source comment
python scripts/code_encyclopedia.py --paths docs/encyclopedia/shell.paths \
  --output docs/encyclopedia/shell-index.md --state docs/encyclopedia/shell-state.json

# gate: fails if a file changed since its description was accepted
python scripts/code_encyclopedia.py ... --check

# after reviewing a changed comment, ratify it
python scripts/code_encyclopedia.py ... --accept <path>
```
