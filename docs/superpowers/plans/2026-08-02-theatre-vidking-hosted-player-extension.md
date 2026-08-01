# Theatre VidKing Hosted Player Extension Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Work inline in the existing checkout; do not create a branch, worktree, or subagent workspace.

**Goal:** Add VidKing to Theatre as an enabled-but-removable, keyless hosted-player extension that appears in the existing Sources sheet, plays through a restricted Qt WebEngine surface, and participates in Colosseum progress, Continue Watching, session, minimize, close, and back-navigation behavior.

**Architecture:** Extend the extension vocabulary with a `hosted-player` resource, but keep URL generation in an app-owned provider registry keyed by extension ID. The first provider is `net.vidking.player`; it converts Cinemeta's keyless `moviedb_id` into VidKing's documented movie or episode embed URL. A local wrapper page owns the cross-origin iframe, validates VidKing `PLAYER_EVENT` messages, and forwards a small sanitized event through a least-privilege QWebChannel bridge to a dedicated QML player page.

**Tech Stack:** Qt 6 QML, Qt WebEngineQuick, Qt WebChannel, C++/Qt Core, JavaScript `.pragma library` modules, HTML/CSP, Cinemeta metadata, the existing `ExtensionsStore`, `SourcesSheet`, `ProgressStore`, and session shell.

## Global Constraints

- Work only in `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum` on the existing `master` checkout.
- Do not create a branch or worktree. Preserve all unrelated modified and untracked files.
- VidKing is always keyless. Do not add a TMDB token, API-key setting, login, or account dependency.
- Use only VidKing's documented embed interface. Do not scrape, intercept, expose, or extract HLS/MP4 URLs.
- VidKing is a hosted web player, not a torrent, direct HTTP stream, download source, or mpv backend.
- Ship `net.vidking.player` enabled by default with `core:false`; users may disable, reorder, remove, and reinstall it.
- Use a reusable `hosted-player` extension capability, but allow only app-owned provider adapters to construct embed URLs. Remote manifests cannot supply arbitrary iframe URLs or JavaScript.
- Support Theatre movies and series episodes only. Anime works when its resolved Cinemeta metadata supplies a TMDB ID.
- Show the VidKing row only when the installed extension is enabled and a valid positive TMDB ID is known.
- Place hosted-player rows in the existing Sources sheet before ordinary torrent/direct rows. Do not replace the main Play action.
- Source availability is optimistic: a valid TMDB ID means the row can be offered, not that VidKing has a playable source. Show a clear in-player unavailable/error state if the embed cannot play.
- For series embeds, set `nextEpisode=true` and `episodeSelector=true`; for movies, omit series-only parameters.
- Use Colosseum gold `e8b923`, send resume position in whole seconds, and do not invent a second visual language.
- Preserve Colosseum's 10-second anti-clutter floor and 90% watched/completion rule.
- Persist the 5-second playback heartbeat with `Progress.recordSilent(...)`; use `Progress.record(...)` for lifecycle writes that must refresh Continue Watching.
- A hosted player does not expose mpv's quality, audio, subtitle, cast, download, screenshot, GIF, or skip-segment controls. Do not display controls that cannot work.
- Reject top-level navigation, new windows, downloads, permission requests, and non-HTTPS external navigation from the hosted-player surface.
- Pin clipboard access off on the hosted surface (`javascriptCanAccessClipboard: false`, `javascriptCanPaste: false`) — stated, not assumed.
- Closing hosted playback DESTROYS the page and its off-the-record profile (Loader unload), never merely hides them; no hidden hosted page may outlive its session's visible use.
- The live smoke must record the set of third-party hosts the hosted surface actually contacts (observe, do not block) and append that list to this plan's results.
- Do not claim success from static tests alone: final verification includes a real WebEngine smoke with one movie and one series episode.

---

## File Map

**Create**

- `qml/HostedPlayerApi.js` — trusted provider registry, provider discovery, VidKing URL construction, and defensive event normalization.
- `qml/HostedPlayerPage.qml` — dedicated Theatre hosted-player surface and Progress/session lifecycle owner.
- `native/hostedplayer/HostedPlayerBridge.h` — least-privilege WebChannel gate exposing only sanitized player events to QML.
- `native/hostedplayer/HostedPlayerBridge.cpp` — event parsing, bounds validation, and signal emission.
- `resources/hostedplayer/host.html` — local parent page that creates the VidKing iframe and validates `postMessage` origin/source.
- `resources/hostedplayer/host.js` — QWebChannel boot, iframe lifecycle, CSP-safe query parsing, and sanitized event forwarding.
- `tests/hosted_player_api_test.mjs` — deterministic provider, URL, and event-normalization tests.
- `tests/hosted_player_bridge_harness.cpp` — native validation harness for accepted/rejected event payloads.
- `tests/hosted_player_contract_test.mjs` — cross-file contract test for extension, source-sheet, routing, and WebEngine security wiring.
- `tests/hosted_player_webengine_smoke.qml` — opt-in real wrapper/WebChannel smoke surface.

**Modify**

- `native/engine/ExtensionsStore.h` — bundled extension reinstall API and helper declaration.
- `native/engine/ExtensionsStore.cpp` — VidKing default, defaults-version migration, and bundled reinstall implementation.
- `qml/ExtensionsCatalog.js` — VidKing card in the Theatre essentials/play-sources catalogue and hosted-player well classification.
- `qml/ExtensionsPage.qml` — route bundled cards through `Extensions.installBundled(id)`.
- `qml/AddonClient.js` — recognize enabled hosted-player extensions without treating them as Stremio stream endpoints.
- `qml/TheatreApi.js` — preserve Cinemeta `moviedb_id` as normalized `tmdbId`.
- `qml/TheatreSeries.qml` — retain resolved TMDB identity, pass episode context, and forward hosted-player selection.
- `qml/SourcesSheet.qml` — merge hosted rows ahead of fetched stream rows and emit a typed hosted-player selection.
- `qml/Main.qml` — hosted session entry point, Continue resume routing, surface loader, state capture, minimize, close, and teardown.
- `native/main.cpp` — construct/expose the least-privilege hosted-player bridge.
- `native/CMakeLists.txt` — compile the bridge/harness and bundle the wrapper resources.
- `native/app_resources.qrc` — embed `host.html` and `host.js`.
- `tests/extension_worlds_derivation_test.mjs` — pin VidKing to Theatre and classify it as a well.
- `tests/extension_reorder_world_test.mjs` — include the new seeded default in ordering fixtures.
- `tests/test_theatre_progress_parity.ps1` — pin hosted Continue/resume and progress payload behavior.

---

### Task 1: Define and test the trusted hosted-player provider contract

**Files:**

- Create: `qml/HostedPlayerApi.js`
- Create: `tests/hosted_player_api_test.mjs`

**Interfaces:**

- Consumes: installed extension rows from `Extensions.installed()` and media context `{type, imdbId, tmdbId, season, episode}`.
- Produces: `rowsFor(hostedExtensions, media)`, `embedUrl(providerId, media, resumeSeconds)`, and `normalizeEvent(raw)`.
- Row shape: `{kind:"hostedPlayer", extensionId, providerId, addonName, sourceName, streamKind:"Hosted", streamLabel:"Web player", media}`.

- [ ] **Step 1: Write the failing JavaScript tests**

Create a Node VM harness following `tests/addon_torrentio_honesty_test.mjs`. Export the four functions from the real `.pragma library` source, then assert:

```js
const installed = [{
  id: 'net.vidking.player', enabled: true,
  manifest: { id: 'net.vidking.player', resources: ['hosted-player'], types: ['movie', 'series'], idPrefixes: ['tt'] }
}]

eq(mod.rowsFor(installed, { type: 'movie', imdbId: 'tt1375666', tmdbId: 27205 }).length, 1)
eq(mod.rowsFor([{ ...installed[0], enabled: false }], { type: 'movie', imdbId: 'tt1375666', tmdbId: 27205 }).length, 0)
eq(mod.rowsFor(installed, { type: 'movie', imdbId: 'tt1375666', tmdbId: 0 }).length, 0)
eq(mod.embedUrl('vidking', { type: 'movie', tmdbId: 27205 }, 83),
   'https://www.vidking.net/embed/movie/27205?color=e8b923&autoPlay=true&progress=83')
eq(mod.embedUrl('vidking', { type: 'series', tmdbId: 1396, season: 2, episode: 3 }, 41),
   'https://www.vidking.net/embed/tv/1396/2/3?color=e8b923&autoPlay=true&nextEpisode=true&episodeSelector=true&progress=41')
eq(mod.embedUrl('unknown-provider', { type: 'movie', tmdbId: 27205 }, 0), '')
eq(mod.normalizeEvent({ type: 'PLAYER_EVENT', data: { event: 'timeupdate', currentTime: 12, duration: 100 } }).event, 'timeupdate')
eq(mod.normalizeEvent({ type: 'OTHER', data: {} }), null)
eq(mod.normalizeEvent({ type: 'PLAYER_EVENT', data: { event: 'timeupdate', currentTime: -1, duration: 100 } }), null)
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `node tests/hosted_player_api_test.mjs`

Expected: FAIL because `qml/HostedPlayerApi.js` does not exist.

- [ ] **Step 3: Implement the provider registry**

Use constants, not manifest-controlled templates:

```js
.pragma library

var VIDKING_EXTENSION_ID = "net.vidking.player"

function rowsFor(hostedExtensions, media) {
    var out = []
    var ctx = media || ({})
    var tmdbId = Math.floor(Number(ctx.tmdbId || 0))
    var type = ctx.type === "series" ? "series" : "movie"
    var season = Math.floor(Number(ctx.season || 0))
    var episode = Math.floor(Number(ctx.episode || 0))
    if (tmdbId <= 0 || (type === "series" && (season <= 0 || episode <= 0))) return out
    for (var i = 0; i < (hostedExtensions || []).length; ++i) {
        var e = hostedExtensions[i]
        if (!e || e.enabled !== true || e.id !== VIDKING_EXTENSION_ID) continue
        out.push({
            kind: "hostedPlayer", extensionId: e.id, providerId: "vidking",
            addonName: (e.manifest && e.manifest.name) || "VidKing",
            sourceName: "VidKing", streamKind: "Hosted", streamLabel: "Web player",
            media: {
                type: type, imdbId: String(ctx.imdbId || ""), tmdbId: tmdbId,
                season: season, episode: episode
            }
        })
    }
    return out
}
```

`rowsFor` must reject non-positive/non-integer TMDB IDs and series contexts without positive integer season and episode. `embedUrl` must use `encodeURIComponent`, whole-second progress, and return `""` for every unregistered provider. `normalizeEvent` must accept object or JSON string, require `type === "PLAYER_EVENT"`, allow only `play`, `playing`, `pause`, `timeupdate`, `seeked`, `ended`, and `error`, clamp finite numeric values, and return `null` for malformed input.

- [ ] **Step 4: Run the provider tests**

Run: `node tests/hosted_player_api_test.mjs`

Expected: PASS with every URL and rejection assertion green.

- [ ] **Step 5: Commit the task if the user wants commits during execution**

```powershell
git add qml/HostedPlayerApi.js tests/hosted_player_api_test.mjs
git commit -m "feat(theatre): define hosted player provider contract"
```

Do not commit unrelated files from the dirty checkout.

---

### Task 2: Make VidKing a removable and reinstallable Theatre extension

**Files:**

- Modify: `native/engine/ExtensionsStore.h`
- Modify: `native/engine/ExtensionsStore.cpp`
- Modify: `qml/ExtensionsCatalog.js`
- Modify: `qml/ExtensionsPage.qml`
- Modify: `qml/AddonClient.js`
- Modify: `tests/extension_worlds_derivation_test.mjs`
- Modify: `tests/extension_reorder_world_test.mjs`
- Create: `tests/hosted_player_contract_test.mjs`

**Interfaces:**

- Produces: `Q_INVOKABLE void installBundled(const QString &id)` and `AddonClient.hostedPlayerExtensions(installedList, type, id)`.
- VidKing manifest: `id:"net.vidking.player"`, `resources:["hosted-player"]`, `types:["movie","series"]`, `idPrefixes:["tt"]`, `core:false`.

- [ ] **Step 1: Extend the extension fixtures first**

Add this exact fixture to the seeded roster assertions:

```js
E('net.vidking.player', false, ['hosted-player'], ['movie', 'series'])
```

Assert `worldsFor(vidking)` returns `['theatre']`, `isWell(vidking)` is true, and Theatre-relative reordering includes it without moving core Cinemeta.

- [ ] **Step 2: Add a failing bundled-install contract test**

In `tests/hosted_player_contract_test.mjs`, read the real source files and assert that:

- `ExtensionsStore` seeds `net.vidking.player` with `core:false` and `hosted-player`.
- `ExtensionsStore.h` exposes `installBundled`.
- `ExtensionsPage.qml` calls `installBundled(item.id)` for a row carrying `bundled:true`.
- `AddonClient.js` exposes `hostedPlayerExtensions` and does not feed `hosted-player` extensions into `loadStreams`.

- [ ] **Step 3: Run the extension tests and verify failure**

Run:

```powershell
node tests/extension_worlds_derivation_test.mjs
node tests/extension_reorder_world_test.mjs
node tests/hosted_player_contract_test.mjs
```

Expected: the existing tests fail on the changed roster and the new contract fails on missing VidKing wiring.

- [ ] **Step 4: Add VidKing to the house defaults**

In `ExtensionsStore.cpp`, add after Torrentio:

```cpp
add("net.vidking.player", "bundled:vidking", false,
    manifest("net.vidking.player", "VidKing",
             "Keyless hosted playback for movies and series through VidKing's web player.",
             { QStringLiteral("hosted-player") },
             { QStringLiteral("movie"), QStringLiteral("series") },
             { QStringLiteral("tt") }, false));
```

Bump `kHouseDefaultsVersion` by exactly one. Preserve the existing migration law: the new row is added once to older profiles, and removing it afterward must not resurrect it on subsequent boots.

- [ ] **Step 5: Implement bundled reinstall**

Extract the VidKing manifest construction into one private helper used by both `appendHouseDefaults` and `installBundled`. `installBundled` must:

1. accept only `net.vidking.player`;
2. emit `installFailed("bundled:vidking", "Unknown bundled extension.")` for other IDs;
3. return the existing row to enabled state if already present;
4. append the trusted app-owned row if absent;
5. save atomically, bump revision, and emit `installFinished(id, "VidKing")`.

- [ ] **Step 6: Add the curated extension card and install routing**

Add VidKing to the essentials/play-sources rail:

```js
{ id: "net.vidking.player", name: "VidKing",
  desc: "Keyless hosted playback for movies and series.",
  kind: "hosted player · movies, shows", url: "bundled:vidking", bundled: true,
  tone1: "#3a3020", tone2: "#171207" }
```

Change `installFromCard(item)` so `item.bundled === true` calls `Extensions.installBundled(item.id)`; all remote Stremio cards continue to call `Extensions.install(item.url)` unchanged. Update `isWell` so either `stream` or `hosted-player` is a well.

- [ ] **Step 7: Add the generic enabled-extension predicate**

In `AddonClient.js`, implement `hostedPlayerExtensions(installedList, type, id)` using the existing `accepts(...)` matcher with resource `hosted-player`. Do not alter `parseStream`; an iframe row never becomes `url:<embed-url>` and never enters mpv.

- [ ] **Step 8: Run extension tests**

Run:

```powershell
node tests/extension_worlds_derivation_test.mjs
node tests/extension_reorder_world_test.mjs
node tests/extension_world_isolation_test.mjs
node tests/addon_torrentio_honesty_test.mjs
node tests/hosted_player_contract_test.mjs
```

Expected: PASS; Torrentio behavior remains unchanged and VidKing belongs only to Theatre.

- [ ] **Step 9: Commit the extension slice if commits are enabled**

```powershell
git add native/engine/ExtensionsStore.h native/engine/ExtensionsStore.cpp qml/ExtensionsCatalog.js qml/ExtensionsPage.qml qml/AddonClient.js tests/extension_worlds_derivation_test.mjs tests/extension_reorder_world_test.mjs tests/hosted_player_contract_test.mjs
git commit -m "feat(extensions): add removable VidKing hosted player"
```

---

### Task 3: Carry Cinemeta's TMDB identity into every Theatre source ask

**Files:**

- Modify: `qml/TheatreApi.js`
- Modify: `qml/TheatreSeries.qml`
- Modify: `tests/theatre_api_rows_harness.qml`
- Modify: `tests/test_theatre_progress_parity.ps1`

**Interfaces:**

- Produces: normalized `tmdbId` on mapped Theatre rows and `property int tmdbId: 0` on `TheatreSeries`.
- Passes source context `{tmdbId, imdbId, season, episode, title, backdrop, metaLine}`.

- [ ] **Step 1: Write failing identity assertions**

Extend the Theatre API fixture with `moviedb_id: 27205` and assert `mapCinemeta(meta, 0).tmdbId === 27205`. Add a PowerShell contract assertion requiring `TheatreSeries.qml` to read `meta.moviedb_id` or normalized `meta.tmdbId` and reset `tmdbId` before each load.

- [ ] **Step 2: Run the identity tests and verify failure**

Run:

```powershell
& native/build-msvc/colosseum.exe tests/theatre_api_rows_harness.qml
powershell -ExecutionPolicy Bypass -File tests/test_theatre_progress_parity.ps1
```

Expected: FAIL on the missing normalized/property identity.

- [ ] **Step 3: Preserve the TMDB ID in `TheatreApi.js`**

Add this normalized field to `mapCinemeta`:

```js
tmdbId: Number(meta.moviedb_id || meta.tmdbId || 0),
```

Add the same guarded merge to `mergeMetaFields` so enriched catalogue rows retain the identity.

- [ ] **Step 4: Retain resolved identity in `TheatreSeries.qml`**

Add `property int tmdbId: 0`. At the beginning of a new item load, reset it to zero. After `TheatreApi.loadMeta(...)` succeeds, set:

```qml
page.tmdbId = Math.max(0, Math.floor(Number(meta.moviedb_id || meta.tmdbId || 0)))
```

When anime metadata pivots from MAL/Kitsu to a Cinemeta IMDb record, take the TMDB ID from the final Cinemeta record, not the original anime provider object.

- [ ] **Step 5: Add full hosted media context to every play-mode source ask**

For a movie, pass `tmdbId`, `imdbId: currentId()`, and no season/episode. For an episode, pass `tmdbId`, `imdbId: currentId()`, and the selected episode's positive integer `season` and `episode`. Do not add hosted context to season-download or row-download asks because hosted players cannot download.

- [ ] **Step 6: Run identity tests**

Run the two commands from Step 2.

Expected: PASS and existing Cinemeta rows remain byte-compatible apart from the added `tmdbId` field.

- [ ] **Step 7: Commit the identity slice if commits are enabled**

```powershell
git add qml/TheatreApi.js qml/TheatreSeries.qml tests/theatre_api_rows_harness.qml tests/test_theatre_progress_parity.ps1
git commit -m "feat(theatre): retain keyless TMDB playback identity"
```

---

### Task 4: Put hosted providers in the existing Sources sheet

**Files:**

- Modify: `qml/SourcesSheet.qml`
- Modify: `qml/TheatreSeries.qml`
- Modify: `tests/hosted_player_contract_test.mjs`

**Interfaces:**

- Consumes: `HostedPlayerApi.rowsFor(Extensions.installed(), playbackContext)`.
- Produces: `signal hostedPlayerRequested(var row, var playbackContext)` from `SourcesSheet` and `signal hostedPlayerRequested(var request)` from `TheatreSeries`.

- [ ] **Step 1: Extend the failing contract test**

Assert the real QML contains:

- a separate `hostedRows` property;
- `visibleRows` built from hosted rows followed by filtered stream rows;
- hosted rows only in `mode === "play"`;
- a `hostedPlayerRequested` signal;
- no copy or download action for `kind === "hostedPlayer"`;
- no torrent prefetch for hosted rows.

- [ ] **Step 2: Run the contract and verify failure**

Run: `node tests/hosted_player_contract_test.mjs`

Expected: FAIL on the missing Sources-sheet contract.

- [ ] **Step 3: Load hosted rows independently of Stremio streams**

Import `HostedPlayerApi.js`. In `show(...)`, use `AddonClient.hostedPlayerExtensions(...)` for the generic manifest match, then build trusted provider rows synchronously before starting stream requests:

```qml
var hostedExts = AddonClient.hostedPlayerExtensions(installedList, type, id)
sheet.hostedRows = sheet.mode === "play"
    ? HostedPlayerApi.rowsFor(hostedExts, sheet.playbackContext)
    : []
```

If there are hosted rows but no ordinary stream extensions, stop the loading state and keep the sheet open. `askedNames` includes hosted provider names once, followed by the ordinary stream extension names.

- [ ] **Step 4: Keep the source list type-safe**

Render hosted rows first with literal, utilitarian copy:

- provider: `VidKing`;
- quality line: `HOSTED PLAYER`;
- detail: `Web player · availability checked when opened`;
- action: Play.

For hosted rows:

- hide quality/seed/size/language claims that are unknown;
- hide copy and download controls;
- skip `Magnet.linkFor` and `Stream.prefetch`;
- on click, emit `hostedPlayerRequested(row, sheet.playbackContext)` and close the sheet.

Torrent and direct-stream rows retain their existing layout and signals.

- [ ] **Step 5: Forward the typed selection through Theatre detail**

In `TheatreSeries.qml`, convert the row/context into this request:

```qml
{
  "providerId": row.providerId,
  "extensionId": row.extensionId,
  "type": context.season !== undefined ? "series" : "movie",
  "imdbId": context.imdbId,
  "tmdbId": context.tmdbId,
  "season": context.season || 0,
  "episode": context.episode || 0,
  "mediaId": context.season !== undefined ? sources.subId : page.currentId(),
  "title": context.title || page.title,
  "backdrop": context.backdrop || page.banner,
  "position": 0
}
```

Emit `page.hostedPlayerRequested(request)`. Do not route it through `playRequested`, `infoHash`, `url:`, `Stream`, or `Download`.

- [ ] **Step 6: Run the contract and Theatre structural tests**

Run:

```powershell
node tests/hosted_player_contract_test.mjs
powershell -ExecutionPolicy Bypass -File tests/test_theatre_episode_ledger_p0.ps1
powershell -ExecutionPolicy Bypass -File tests/test_theatre_series_scroll.ps1
```

Expected: PASS; existing episode/source behavior remains intact.

- [ ] **Step 7: Commit the Sources-sheet slice if commits are enabled**

```powershell
git add qml/SourcesSheet.qml qml/TheatreSeries.qml tests/hosted_player_contract_test.mjs
git commit -m "feat(theatre): show hosted players in Sources"
```

---

### Task 5: Build the least-privilege WebEngine wrapper and event bridge

**Files:**

- Create: `native/hostedplayer/HostedPlayerBridge.h`
- Create: `native/hostedplayer/HostedPlayerBridge.cpp`
- Create: `resources/hostedplayer/host.html`
- Create: `resources/hostedplayer/host.js`
- Create: `tests/hosted_player_bridge_harness.cpp`
- Modify: `native/app_resources.qrc`
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp`

**Interfaces:**

- Produces C++ signal: `void playerEvent(const QVariantMap &event)`.
- Exposes one WebChannel invokable: `Q_INVOKABLE void postPlayerEvent(const QString &json)`.
- Wrapper query: `?url=<percent-encoded trusted embed URL>&session=<opaque session token>`.

- [ ] **Step 1: Write the failing native bridge harness**

The harness constructs `HostedPlayerBridge`, observes `playerEvent`, then calls `postPlayerEvent` with:

```cpp
R"({"event":"timeupdate","currentTime":12.5,"duration":100,"session":"abc"})"
```

Assert one signal with finite values. Then assert no signal for unknown events, negative values, duration over 24 hours, currentTime over duration plus a five-second tolerance, a payload over 4096 bytes, or malformed JSON.

- [ ] **Step 2: Add the harness target and verify failure**

Add `hosted_player_bridge_harness` linked to `Qt6::Core`, configure, and run:

```powershell
cmake -S native -B native/build-msvc -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build native/build-msvc --target hosted_player_bridge_harness
& native/build-msvc/hosted_player_bridge_harness.exe
```

Expected: compile failure because the bridge files do not exist.

- [ ] **Step 3: Implement the least-privilege bridge**

The bridge must expose no file, network, shell, extension, progress-store, or navigation methods. Parse JSON in C++; copy only `event`, `currentTime`, `duration`, `progress`, `session`, `id`, `mediaType`, `season`, and `episode`; reject all other event names and invalid numeric bounds; then emit the normalized map.

- [ ] **Step 4: Build the local wrapper**

`host.html` contains only one full-viewport iframe and the two scripts. Use a CSP equivalent to:

```html
<meta http-equiv="Content-Security-Policy"
      content="default-src 'none'; frame-src https://www.vidking.net; script-src 'self' qrc:; style-src 'unsafe-inline'; connect-src 'none'; img-src 'none'">
```

`host.js` must:

1. initialize `QWebChannel` and get `channel.objects.hostedPlayerBridge`;
2. parse `url` and `session` from the wrapper query;
3. require `new URL(url).origin === "https://www.vidking.net"` and a pathname beginning `/embed/movie/` or `/embed/tv/`;
4. assign the iframe URL only after validation;
5. listen for `message` events;
6. require `event.origin === "https://www.vidking.net"` and `event.source === iframe.contentWindow`;
7. require `data.type === "PLAYER_EVENT"`;
8. forward a JSON object containing only the allowed event fields plus the wrapper session token.

- [ ] **Step 5: Bundle the wrapper and expose the bridge**

Add both wrapper files to `native/app_resources.qrc`. Add bridge sources to the `colosseum` target. In `main.cpp`, construct one `HostedPlayerBridge` and expose it as context property `HostedPlayerBridge`; the QML WebChannel registers only this object.

- [ ] **Step 6: Build and run the bridge harness**

Run:

```powershell
cmake --build native/build-msvc --target hosted_player_bridge_harness colosseum
& native/build-msvc/hosted_player_bridge_harness.exe
```

Expected: harness PASS and the app links with existing `Qt6::WebEngineQuick` and `Qt6::WebChannel` dependencies.

- [ ] **Step 7: Commit the secure wrapper slice if commits are enabled**

```powershell
git add native/hostedplayer native/main.cpp native/CMakeLists.txt native/app_resources.qrc resources/hostedplayer tests/hosted_player_bridge_harness.cpp
git commit -m "feat(theatre): add secure hosted player bridge"
```

---

### Task 6: Implement the VidKing player surface and progress lifecycle

**Files:**

- Create: `qml/HostedPlayerPage.qml`
- Create: `tests/hosted_player_webengine_smoke.qml`
- Modify: `tests/hosted_player_contract_test.mjs`
- Modify: `tests/test_theatre_progress_parity.ps1`

**Interfaces:**

- Public methods: `open(request)`, `captureState()`, `restoreState(state)`, `suspendForMinimize()`, `resumeFromMinimize()`, and `stop()`.
- Public signals: `backRequested()`, `minimizeRequested()`, `fullscreenRequested()`, and `closeRequested()`.
- Request shape is the exact object produced by Task 4.

- [ ] **Step 1: Add failing QML surface assertions**

Require the page to import `QtWebEngine` and `QtWebChannel`, use an off-the-record `WebEngineProfile`, register `HostedPlayerBridge`, reject popups/navigation/downloads/permissions, pin clipboard access off (`javascriptCanAccessClipboard: false` and `javascriptCanPaste: false` in the view's settings), expose the six public methods, and call both `Progress.recordSilent` and `Progress.record`.

- [ ] **Step 2: Run the contract and verify failure**

Run: `node tests/hosted_player_contract_test.mjs`

Expected: FAIL because `HostedPlayerPage.qml` is absent.

- [ ] **Step 3: Implement the hosted-player page**

Use a black full-screen surface with Colosseum's existing minimal window controls and a literal status panel. Do not recreate mpv chrome. Configure a dedicated profile:

```qml
WebEngineProfile {
    id: hostedProfile
    offTheRecord: true
    httpCacheType: WebEngineProfile.MemoryHttpCache
    persistentCookiesPolicy: WebEngineProfile.NoPersistentCookies
}
```

The `WebEngineView` loads only `qrc:/hostedplayer/host.html?...`. Allow top-level navigation only to that wrapper URL. Reject `onNewWindowRequested`, `onDownloadRequested`, and permission requests. In the view's `settings`, set `javascriptCanAccessClipboard: false` and `javascriptCanPaste: false` so the hosted page can never read or write the user's clipboard. Accept fullscreen requests only by forwarding them to the existing shell fullscreen action.

- [ ] **Step 4: Make each open generation-safe**

On `open(request)`:

1. validate provider, TMDB ID, media ID, and series coordinates through `HostedPlayerApi.embedUrl`;
2. create a new opaque session token from timestamp plus a monotonic generation;
3. include the requested resume position in the embed URL;
4. reset last position, duration, event timestamp, and error state;
5. load the local wrapper URL containing encoded embed URL and token.

Ignore every bridge event whose `session` does not equal the current token. This prevents a late event from a previous movie or episode from writing into the new title.

- [ ] **Step 5: Mirror Colosseum's progress payload**

After 10 seconds and with a positive duration, write:

```qml
{
  "id": request.mediaId,
  "kind": "video",
  "caption": request.title,
  "title": request.title,
  "sub": episodePrefix + formatTime(duration - currentTime) + " left",
  "cover": request.backdrop,
  "c1": "#33445d", "c2": "#0c1118",
  "progress": Math.max(0, Math.min(1, currentTime / duration)),
  "resume": {
    "hostedPlayerId": request.providerId,
    "extensionId": request.extensionId,
    "imdbId": request.imdbId,
    "tmdbId": request.tmdbId,
    "subType": request.type,
    "subId": request.mediaId,
    "season": request.season || 0,
    "episode": request.episode || 0,
    "position": currentTime
  }
}
```

Use `recordSilent` for time-update heartbeats no more often than once every five seconds. Use notifying `record` on `pause`, `seeked`, `ended`, `stop`, close, and component destruction. Clamp at the page and bridge layers. Let `ProgressStore` enforce the existing 90% watched behavior.

- [ ] **Step 6: Implement honest failure behavior**

On wrapper load failure, VidKing `error`, or no usable playback event within a 20-second startup guard, show:

`VidKing could not find or start a source for this title.`

Offer two actions: `Back to Sources` and `Retry`. Back emits `backRequested`; Main returns to the detail page and reopens the same Sources sheet context. Do not silently fall through to a torrent because that would ignore the user's explicit source choice.

- [ ] **Step 7: Add the opt-in WebEngine smoke**

The smoke accepts environment-provided request data, opens the real wrapper, logs `wrapper-loaded`, and exits success once the bridge receives any valid VidKing event. It exits with a distinct nonzero code on wrapper load failure or timeout. Keep it opt-in because live VidKing availability is external and unsuitable for deterministic CI.

- [ ] **Step 8: Run static and native verification**

Run:

```powershell
node tests/hosted_player_contract_test.mjs
powershell -ExecutionPolicy Bypass -File tests/test_theatre_progress_parity.ps1
cmake --build native/build-msvc --target colosseum
```

Expected: PASS and a clean QML load for `HostedPlayerPage.qml`.

- [ ] **Step 9: Commit the player surface if commits are enabled**

```powershell
git add qml/HostedPlayerPage.qml tests/hosted_player_webengine_smoke.qml tests/hosted_player_contract_test.mjs tests/test_theatre_progress_parity.ps1
git commit -m "feat(theatre): add VidKing hosted playback surface"
```

---

### Task 7: Integrate hosted playback with Main sessions and Continue Watching

**Files:**

- Modify: `qml/Main.qml`
- Modify: `qml/TheatreSeries.qml`
- Modify: `tests/hosted_player_contract_test.mjs`
- Modify: `tests/test_theatre_progress_parity.ps1`

**Interfaces:**

- Produces: `openHostedPlayerSession(request)` and session `contentKind:"hosted-video"`.
- Hosted Loader source: `HostedPlayerPage.qml` at the same immersive z-level as the native video player.

- [ ] **Step 1: Add failing routing assertions**

Pin these behaviors:

- Theatre detail connects `hostedPlayerRequested` to `openHostedPlayerSession`.
- `resumeContinue` checks `resume.hostedPlayerId` before `resume.infoHash`.
- `activateSession`, `captureSession`, `teardownSession`, minimize, and close all handle `hosted-video`.
- `immersiveSurfaceOpen` includes the hosted-player surface.
- the hosted Loader stays separate from `playerLayer` and never changes `usePlayer2`.
- close and minimize UNLOAD the hosted Loader (`active = false` — destroying the WebEngine page and its off-the-record profile) rather than hiding it; no assertion may pass with a hidden-but-alive hosted page.

- [ ] **Step 2: Run routing tests and verify failure**

Run:

```powershell
node tests/hosted_player_contract_test.mjs
powershell -ExecutionPolicy Bypass -File tests/test_theatre_progress_parity.ps1
```

Expected: FAIL on missing hosted session routing.

- [ ] **Step 3: Add the hosted session entry point**

`openHostedPlayerSession(request)` must add the Theatre collection entry using the series root or movie ID, then call:

```qml
Sessions.openOrSwitch({
  "appType": "theatre",
  "contentKind": "hosted-video",
  "title": request.title || "Video",
  "target": request
})
```

The session identity must deduplicate by provider plus `mediaId`; it must not collide with an mpv session for the same episode.

- [ ] **Step 4: Add the hosted surface Loader**

Create a Loader beside `playerLayer`, with the same z-order and full-window bounds, sourcing `HostedPlayerPage.qml`. Wire back/minimize/fullscreen/close to the hosted session functions. On activation, call `open(target)` and then `restoreState(savedState)` when present.

- [ ] **Step 5: Complete lifecycle integration**

- `captureSession`: return hosted page `captureState()`.
- minimize/teardown: call `suspendForMinimize()` (which records final progress and stops playback), capture state, then UNLOAD the hosted layer — `active = false`, destroying the page and profile — and retain the session. A minimized hosted session keeps no live iframe running in the background; restore rebuilds it, which the next bullet already promises.
- restore: reload the embed at the captured position. Do not promise warm iframe preservation; WebEngine sessions may be rebuilt on restore.
- close: call `stop()`, then UNLOAD the hosted layer (`active = false` — the page and its off-the-record profile are destroyed immediately, never kept hidden), then close the session.
- immersive state: hide the OS taskbar while hosted playback is visible.
- back from the player: close/minimize according to the same house semantics as the video player, revealing Theatre detail underneath.

- [ ] **Step 6: Route Continue Watching directly back to VidKing**

Before the `localPath` and `infoHash` branches, detect `r.hostedPlayerId`. Rebuild the exact request from the saved resume object and entry title/cover, but only open it if `net.vidking.player` is still installed and enabled. If the extension was disabled or removed, open the Theatre detail page instead; never bypass the extension switch.

- [ ] **Step 7: Connect Theatre detail and preserve Sources return context**

Connect the detail signal to `openHostedPlayerSession`. When the hosted page reports `Back to Sources`, close the hosted session, reveal the same Theatre detail instance, and call a small `reopenSources(request)` method on it so the original movie or episode source sheet returns.

- [ ] **Step 8: Run routing and session regressions**

Run:

```powershell
node tests/hosted_player_contract_test.mjs
powershell -ExecutionPolicy Bypass -File tests/test_theatre_progress_parity.ps1
powershell -ExecutionPolicy Bypass -File tests/test_theatre_continue_anime_routing_p0.ps1
cmake --build native/build-msvc --target colosseum
```

Expected: PASS; ordinary mpv sessions, anime detail routing, and Continue behavior remain green.

- [ ] **Step 9: Commit the shell integration if commits are enabled**

```powershell
git add qml/Main.qml qml/TheatreSeries.qml tests/hosted_player_contract_test.mjs tests/test_theatre_progress_parity.ps1
git commit -m "feat(theatre): integrate hosted playback sessions"
```

---

### Task 8: Perform live verification and document operational truth

**Files:**

- Modify: `README.md`
- Modify: `THIRD_PARTY_NOTICES.md`
- Modify: `docs/superpowers/plans/2026-08-02-theatre-vidking-hosted-player-extension.md` only to check completed boxes and append measured results.

**Interfaces:**

- Produces user-facing truth about what VidKing can and cannot do.

- [ ] **Step 1: Run the deterministic suite**

Run:

```powershell
node tests/hosted_player_api_test.mjs
node tests/hosted_player_contract_test.mjs
node tests/extension_worlds_derivation_test.mjs
node tests/extension_reorder_world_test.mjs
node tests/extension_world_isolation_test.mjs
node tests/addon_torrentio_honesty_test.mjs
powershell -ExecutionPolicy Bypass -File tests/test_theatre_progress_parity.ps1
powershell -ExecutionPolicy Bypass -File tests/test_theatre_episode_ledger_p0.ps1
powershell -ExecutionPolicy Bypass -File tests/test_theatre_series_scroll.ps1
powershell -ExecutionPolicy Bypass -File tests/test_theatre_continue_anime_routing_p0.ps1
cmake --build native/build-msvc --target hosted_player_bridge_harness colosseum
& native/build-msvc/hosted_player_bridge_harness.exe
```

Expected: every command exits zero.

- [ ] **Step 2: Run the movie smoke — with third-party host observation**

Launch the app for this smoke with request logging enabled so we OBSERVE (never block) which hosts the hosted surface actually contacts:

```powershell
$env:QTWEBENGINE_CHROMIUM_FLAGS = "--log-net-log=artifacts/vidking_netlog.json"
& native/build-msvc/colosseum.exe
```

If `--log-net-log` produces no file on this Qt build, fall back to `QTWEBENGINE_REMOTE_DEBUGGING=9223` and read the host list from the DevTools Network tab at `http://localhost:9223`. After the smoke, extract the unique third-party hostnames contacted during playback and append them to this plan's results section — this is the empirical answer to "what would a network allowlist have to permit," gathered before anyone proposes one.

Open Inception through Theatre (`tt1375666`, Cinemeta `moviedb_id` 27205), choose `VidKing · Hosted Player`, and verify:

1. the Sources row is first and has no quality/seed/download claims;
2. the local wrapper remains the top-level WebEngine page;
3. playback begins when VidKing has a source, or the honest unavailable panel appears;
4. no popup or external navigation escapes the player;
5. after 15 seconds, close the player and verify one `Progress.get("video", "tt1375666")` entry with hosted resume metadata;
6. the Continue center action reopens VidKing at the saved position while the extension is enabled.

- [ ] **Step 3: Run the series smoke**

Open Breaking Bad (`tt0903747`, Cinemeta `moviedb_id` 1396), choose a known episode, and verify the embed URL contains `/embed/tv/1396/<season>/<episode>`, `nextEpisode=true`, `episodeSelector=true`, and the saved Progress ID remains the existing episode stream ID.

- [ ] **Step 4: Verify extension honesty**

Disable VidKing and confirm its row disappears immediately. Re-enable it and confirm it returns. Remove it and confirm it stays absent after restart. Reinstall it from Extensions and confirm it returns enabled without a network manifest fetch. Disable it again, press Continue on a prior VidKing entry, and confirm Theatre opens detail instead of bypassing the switch.

- [ ] **Step 5: Verify the security boundary**

Attempt a popup, target-blank navigation, permission request, and file download from the hosted surface. Confirm each is rejected. Inspect the bridge object and confirm it exposes only `postPlayerEvent`; VidKing iframe content cannot call `Progress`, `Extensions`, filesystem APIs, or shell APIs.

- [ ] **Step 6: Update documentation**

Document:

- VidKing is an optional hosted-player extension using VidKing's documented iframe;
- metadata identity is keyless through Cinemeta's `moviedb_id`;
- playback availability belongs to VidKing and is checked only when opened;
- native mpv controls and downloads do not apply inside the hosted player;
- no raw media URLs are extracted or exposed;
- third-party service availability and terms remain VidKing's responsibility.

- [ ] **Step 7: Inspect only the intended diff**

Run:

```powershell
git status --short
git diff -- native/engine/ExtensionsStore.h native/engine/ExtensionsStore.cpp native/hostedplayer native/main.cpp native/CMakeLists.txt native/app_resources.qrc qml/HostedPlayerApi.js qml/HostedPlayerPage.qml qml/AddonClient.js qml/ExtensionsCatalog.js qml/ExtensionsPage.qml qml/TheatreApi.js qml/TheatreSeries.qml qml/SourcesSheet.qml qml/Main.qml resources/hostedplayer tests/hosted_player_api_test.mjs tests/hosted_player_bridge_harness.cpp tests/hosted_player_contract_test.mjs tests/hosted_player_webengine_smoke.qml tests/extension_worlds_derivation_test.mjs tests/extension_reorder_world_test.mjs tests/test_theatre_progress_parity.ps1 README.md THIRD_PARTY_NOTICES.md
```

Expected: no unrelated user file is staged or modified by this work.

- [ ] **Step 8: Commit the verified documentation if commits are enabled**

```powershell
git add README.md THIRD_PARTY_NOTICES.md docs/superpowers/plans/2026-08-02-theatre-vidking-hosted-player-extension.md
git commit -m "docs(theatre): document VidKing hosted playback"
```

---

## Definition of Done

- VidKing is visible in Extensions as a Theatre hosted-player well.
- It ships enabled, is removable, and can be reinstalled locally without an API key or fake remote manifest.
- Disabling/removing it immediately removes its Sources row and prevents Continue from bypassing that choice.
- A valid Cinemeta `moviedb_id` produces a VidKing Sources row for movies and individual episodes.
- Selecting the row opens a dedicated restricted WebEngine surface, never mpv and never the torrent pipeline.
- The embed URL exactly follows VidKing's documented movie/TV routes and parameters.
- Only validated VidKing-origin `PLAYER_EVENT` messages reach the least-privilege bridge.
- Progress saves every five seconds silently and on lifecycle boundaries visibly, using existing Colosseum video IDs and thresholds.
- Continue Watching resumes the hosted provider when installed and enabled.
- Back, Sources return, minimize, restore, close, fullscreen, taskbar suppression, and session deduplication work.
- Hosted playback makes no false claims about quality, subtitles, downloads, source availability, or native-player controls.
- Clipboard access is pinned off; closing or minimizing hosted playback destroys the page and profile rather than hiding them.
- The smoke results include the observed list of third-party hosts the hosted surface contacted during live playback.
- Deterministic tests, native harness, app build, movie smoke, series smoke, extension switch test, and security smoke all pass.

## Claude Implementation Prompt

```text
Work directly in C:\Users\Suprabha\Desktop\Brotherhood\Colosseum on the existing master checkout.

Read and execute docs/superpowers/plans/2026-08-02-theatre-vidking-hosted-player-extension.md task by task, using the superpowers:executing-plans and superpowers:test-driven-development skills. Do not create a branch, git worktree, or subagent workspace. Preserve every unrelated modified and untracked file already in the checkout; stage only the files named by the current task.

Implement the approved reusable hosted-player extension contract with VidKing as the first trusted provider. It must remain keyless, use only VidKing's documented iframe/postMessage interface, appear in Theatre's existing Sources sheet, and never extract raw media URLs or route VidKing through mpv/torrent/download code. Treat the WebEngine/QWebChannel boundary as security-sensitive and keep the bridge least-privilege.

Run each failing test before implementation, each focused test after implementation, and the complete verification matrix in Task 8. Stop and report with exact evidence if a requirement conflicts with the live source; do not silently redesign the contract. Work sequentially because ExtensionsStore, SourcesSheet, TheatreSeries, Main.qml, sessions, and Progress are coupled. After every task, inspect the scoped diff before proceeding. Do not claim completion until the live movie, series, extension-honesty, Continue-resume, and security smokes pass.

Commits are enabled: land each task's scoped commit as written in its final step (house rule — completed work is committed immediately, never left dangling). Before editing native/main.cpp or native/CMakeLists.txt, check agents/chat.md in the Brotherhood repo for Agent 1's in-flight lanista edits to the same files and declare the touch there first. When done (or blocked), report to Agent 4 — the player/theatre domain leader — with: commits landed (hashes), tests run with results, the observed third-party host list, and any deviation from this plan with its evidence.
```
