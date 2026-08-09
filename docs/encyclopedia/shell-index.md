# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/shell-state.json`

## Summary

- Total files: **12**
- Documented: **11**
- Undocumented: **1**
- Drifted: **0**

<a id="file-native-collectionstore-h"></a>
## `native/CollectionStore.h`

- Status: **CURRENT**
- Accepted blob: `7c44bce37c1fa1ceab951974a8b657f37d769846`
- Current blob: `7c44bce37c1fa1ceab951974a8b657f37d769846`
- Source: [`native/CollectionStore.h`](../../native/CollectionStore.h)

```text
// CollectionStore — the "Your Collection" shelf: what the user CHOSE to save via
// the + Library toggle. NOT Continue: distinct from ProgressStore (auto history) —
// an entry can exist unstarted and survives finishing. One store, three worlds.
//
// QML side (the only contract):
//   Collection.add(world, { id, type, title, cover, payload })   // upsert; stamps addedAt
//   Collection.remove(world, id)
//   Collection.has(world, id)
//   Collection.items(world)        // newest-first by addedAt
//   Collection.revision            // bump on every change — name it in a binding to make
//                                  //   has()/items()-based bindings re-evaluate reactively.
//
// `type` must ride on every entry (universe-tile law: a tile without type opens a
// series as a movie and dies). `payload` is the world-specific reopen snapshot.
// Persistence mirrors ProgressStore: one JSON blob under "collection/entries".
```

<a id="file-native-searchhistorystore-h"></a>
## `native/SearchHistoryStore.h`

- Status: **CURRENT**
- Accepted blob: `727a8bf03aca45e37a6c65fc1d74c64ef2387037`
- Current blob: `727a8bf03aca45e37a6c65fc1d74c64ef2387037`
- Source: [`native/SearchHistoryStore.h`](../../native/SearchHistoryStore.h)

```text
// SearchHistoryStore -- a tiny, durable MRU list for the three world search surfaces.
// It owns the disk boundary so QML Loader recreation and provider callbacks cannot decide
// whether a user search is remembered.
```

<a id="file-native-sessionstore-h"></a>
## `native/SessionStore.h`

- Status: **CURRENT**
- Accepted blob: `96a0c3bdd90be514139bce2fa37e56c28c6f1bc0`
- Current blob: `96a0c3bdd90be514139bce2fa37e56c28c6f1bc0`
- Source: [`native/SessionStore.h`](../../native/SessionStore.h)

```text
// SessionStore - the OS-shell's open-sessions model, exposed to QML as `Sessions`.
// One small thing: the list of "things you currently have open" (a comic, a movie, a
// book), which one is ACTIVE, and the saved-state blob each carries so it can be torn
// down and rebuilt exactly where you left it (Approach 2 - only the active session is
// ever instantiated). The Taskbar reads this to draw app-grouped tiles; Main.qml's
// switch glue listens to activeChanged to capture/teardown/restore.
//
// QML contract:
//   Sessions.openOrSwitch({appType, contentKind, target, title}) -> id  (dedups by target key)
//   Sessions.switchTo(id)
//   Sessions.close(id)
//   Sessions.saveState(id, obj)   // switch glue writes captured state before teardown
//   QVariantMap Sessions.get(id)  // one record (empty map if not found)
//   QVariantList Sessions.list()  // records in open order
//   QVariantList Sessions.groups()// [{appType,title,icon,sessions:[record,...]}] for the taskbar
//   Sessions.activeId             // "" = none (home shell)
//   Sessions.revision             // bump on every change; name it in a binding to stay reactive
// signals: activeChanged(prevId, nextId)
```

<a id="file-native-engine-applog-cpp"></a>
## `native/engine/AppLog.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `5cceadb26302543e8cc34b0380c58eecf3d19f45`
- Current blob: `5cceadb26302543e8cc34b0380c58eecf3d19f45`
- Source: [`native/engine/AppLog.cpp`](../../native/engine/AppLog.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-applog-h"></a>
## `native/engine/AppLog.h`

- Status: **CURRENT**
- Accepted blob: `2b44ff47da8eda9db541d1594f154ec457cceab0`
- Current blob: `2b44ff47da8eda9db541d1594f154ec457cceab0`
- Source: [`native/engine/AppLog.h`](../../native/engine/AppLog.h)

```text
// AppLog.h — the always-on rolling log.
//
// WHY THIS EXISTS (2026-08-05): Hemanth hit a Downloads "Cancel" that did
// nothing and said nothing, killed the app, and asked for the log. There was
// none. Colosseum installed no message handler, and Colosseum.bat — the
// launcher he actually double-clicks — runs the exe with stderr unredirected,
// so every qInfo/qWarning in a normal launch was discarded as it was produced.
// A bug he can reproduce but never evidence is a bug we debug blind, so the
// log is now unconditional: no dev launcher, no env var, no opt-in.
//
// Contract:
//   - appends to <AppDataLocation>/logs/colosseum.log
//   - flushes EVERY line — the case this was built for is a hard kill, and a
//     buffered tail is exactly the part that would be missing
//   - rotates at 5 MB, keeping colosseum.1..3.log (oldest dropped)
//   - forwards to the previously-installed handler, so dev.bat's console and
//     QT_FORCE_STDERR_LOGGING behave exactly as before
//   - mutex-guarded: Qt logs from the network/decode threads, not just the GUI
//
// install() must be called AFTER applicationName/organizationName are set (and
// after the COLOSSEUM_APPDATA_TAG override), because AppDataLocation resolves
// from them — a dltest run then writes its own isolated log, never the real one.
```

<a id="file-native-main-cpp"></a>
## `native/main.cpp`

- Status: **CURRENT**
- Accepted blob: `46d01209ae28ca949c0b3a1ae80348be59f2a7a0`
- Current blob: `46d01209ae28ca949c0b3a1ae80348be59f2a7a0`
- Source: [`native/main.cpp`](../../native/main.cpp)

```text
// Colosseum native launcher. Runs the live qml/ tree with an on-disk HTTP cache
// and the same Metahub IPv4 pin Tankoban-3 uses for instant poster loading.
```

<a id="file-qml-bootsplash-qml"></a>
## `qml/BootSplash.qml`

- Status: **CURRENT**
- Accepted blob: `0047c94309ec4092463724fc1ba63dc7fefff171`
- Current blob: `0047c94309ec4092463724fc1ba63dc7fefff171`
- Source: [`qml/BootSplash.qml`](../../qml/BootSplash.qml)

```text
// BootSplash — OS-style loader. Prefetches every catalog cover/banner into Qt's pixmap cache
// (hidden Images with cache:true) behind a progress bar, then emits finished() so the shell reveals
// with art already warm. A hard timeout guarantees boot always completes even if a CDN stalls.
```

<a id="file-qml-browserdrawer-qml"></a>
## `qml/BrowserDrawer.qml`

- Status: **CURRENT**
- Accepted blob: `c99a291ae40b13391248727f0c35a83a35b1fb8b`
- Current blob: `c99a291ae40b13391248727f0c35a83a35b1fb8b`
- Source: [`qml/BrowserDrawer.qml`](../../qml/BrowserDrawer.qml)

```text
// BrowserDrawer — Feature 8: the in-player episode/source browser (spec 2026-07-08,
// mock-ratified: side drawer, season pills, auto-play best source).
// Self-contained like ShortcutsSheet: props in, signals out, its own Theme. The video
// keeps playing beside it. All derivations live in EpisodeBrowser.js (harness-tested);
// this file only renders and forwards taps.
```

<a id="file-qml-main-qml"></a>
## `qml/Main.qml`

- Status: **CURRENT**
- Accepted blob: `ee9c87942ca590881ec8ea5e624808ca77688cd2`
- Current blob: `ee9c87942ca590881ec8ea5e624808ca77688cd2`
- Source: [`qml/Main.qml`](../../qml/Main.qml)

```text
// Colosseum — HOME (v1, on the proven spine)
// Fullscreen-exclusive frameless OS surface: persistent wallpaper + frosted-glass chrome.
//   Top bar -> universal Continue -> per-medium widgets.
// Glass = proven material (see Glass.qml).
// Run:  C:/Qt/6.11.1/mingw_64/bin/qml.exe qml/Main.qml      (Esc / Ctrl+Q to quit)
```

<a id="file-qml-taskbar-qml"></a>
## `qml/Taskbar.qml`

- Status: **CURRENT**
- Accepted blob: `9e4699e296da85fdec28fa38604f1b5cd20fe577`
- Current blob: `9e4699e296da85fdec28fa38604f1b5cd20fe577`
- Source: [`qml/Taskbar.qml`](../../qml/Taskbar.qml)

```text
// Taskbar.qml - the OS-shell's auto-hiding switcher bar.
// The closed Colosseum button and the open taskbar are the same object, so the bar
// grows out of the button instead of swapping between two separate pieces.
```

<a id="file-qml-topbar-qml"></a>
## `qml/TopBar.qml`

- Status: **CURRENT**
- Accepted blob: `7fc81a9848f364a988593dec534aa0c599446a32`
- Current blob: `7fc81a9848f364a988593dec534aa0c599446a32`
- Source: [`qml/TopBar.qml`](../../qml/TopBar.qml)

```text
// TopBar — the shared Colosseum shell chrome: clock/date · library pills · system icons.
// ONE source for the top bar across the home AND every world page.
//   activeMedium == ""   → HOME: no pill is selected (the no-selection rule).
//   activeMedium == "X"  → WORLD: pill X carries the gold selected accent, and a "‹ Home"
//                          affordance appears at the left.
// Emits intent signals; the host (home / world) decides what navigation happens.
```

<a id="file-qml-worldtabbar-qml"></a>
## `qml/WorldTabBar.qml`

- Status: **CURRENT**
- Accepted blob: `9640d5226bb4cfe55f9ee542dbb67106433f07e7`
- Current blob: `9640d5226bb4cfe55f9ee542dbb67106433f07e7`
- Source: [`qml/WorldTabBar.qml`](../../qml/WorldTabBar.qml)

```text
// WorldTabBar — TheatreTabBar's glass pill bar, generalized to any tab set (parameterized
// tabModel). Used by the Tankoban world (Manga|Comics); Theatre keeps TheatreTabBar for now
// and can migrate to this later. Same glass look/feel: gold active pill, ghost inactive, hover tint.
```
