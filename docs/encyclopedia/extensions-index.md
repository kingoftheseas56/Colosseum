# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/extensions-state.json`

## Summary

- Total files: **10**
- Documented: **10**
- Undocumented: **0**
- Drifted: **0**

<a id="file-native-engine-extensionsstore-cpp"></a>
## `native/engine/ExtensionsStore.cpp`

- Status: **CURRENT**
- Accepted blob: `26597fe3d86e8f51eb055f43abd674652716d210`
- Current blob: `26597fe3d86e8f51eb055f43abd674652716d210`
- Source: [`native/engine/ExtensionsStore.cpp`](../../native/engine/ExtensionsStore.cpp)

```text
// ExtensionsStore.cpp — see ExtensionsStore.h for the contract.
```

<a id="file-native-engine-extensionsstore-h"></a>
## `native/engine/ExtensionsStore.h`

- Status: **CURRENT**
- Accepted blob: `53dbbbdf0dc73c4856d17f875766e2aac3606417`
- Current blob: `53dbbbdf0dc73c4856d17f875766e2aac3606417`
- Source: [`native/engine/ExtensionsStore.h`](../../native/engine/ExtensionsStore.h)

```text
// ExtensionsStore.h
//
// The extension registry behind the Extensions page: which Stremio-protocol
// addons the house carries, in what order, on or off. Spec:
// docs/superpowers/specs/2026-07-05-colosseum-extensions-store-design.md
// (Brotherhood repo). Ratified mock: agents/colosseum-extensions-mock.html.
//
// What it is (plain): a list of {id, transportUrl, manifest} entries persisted
// to <appdata>/extensions/installed.json (QSaveFile atomic, the MangaDownloader
// index pattern). Install = fetch the manifest over HTTP, validate id+name,
// slim it (strip data-URIs, cap description), persist. Order = the array order;
// when Theatre asks its stream extensions for play sources it asks top-first.
//
// Law folded in at THIS layer (not a setting):
//   - adult extensions (behaviorHints.adult) are refused at preview AND install;
//   - no Stremio-account sync — the file is the only store;
//   - first run seeds the four house extensions Theatre already runs on
//     (Cinemeta core/locked, Torrentio, Anime Kitsu, OpenSubtitles v3), so the
//     store tells the truth about the present from day one.
//
// Threading: pure QNetworkAccessManager + lambdas on the main thread.
```

<a id="file-qml-addonclient-js"></a>
## `qml/AddonClient.js`

- Status: **CURRENT**
- Accepted blob: `f2c253621c4d7111b8d38d81269e39d52d03b3f7`
- Current blob: `f2c253621c4d7111b8d38d81269e39d52d03b3f7`
- Source: [`qml/AddonClient.js`](../../qml/AddonClient.js)

```text
// AddonClient.js — the generic Stremio-extension caller (spec slice E).
// Where Torrentio.js speaks to ONE addon, this speaks to EVERY installed stream
// extension: Harbor's resource-matching algorithm (src/lib/addons.ts:50-71) +
// parallel fetch with partial results (src/lib/streams/addons.ts), with
// Torrentio.js's proven quality/seeders/language parsing generalized so every
// extension's answers rank the same way. Torrentio.js stays for the
// season-download resolver; the SourcesSheet asks through here.
//
// URL-stream convention: rows with a direct `url` (debrid, HTTP hosts, live tv)
// carry infoHash = "url:<url>" so they flow through the existing play chain
// (playRequested → playTorrent → playStreamAt) with NO signal changes — the
// same routing-prefix trick as the western lane's "gc:" series ids. The player
// branches on the prefix and hands the url straight to mpv.
```

<a id="file-qml-addonlogo-qml"></a>
## `qml/AddonLogo.qml`

- Status: **CURRENT**
- Accepted blob: `6cec1e35a34c9e4c22f6df615c32354b5d88e661`
- Current blob: `6cec1e35a34c9e4c22f6df615c32354b5d88e661`
- Source: [`qml/AddonLogo.qml`](../../qml/AddonLogo.qml)

```text
// AddonLogo — draws an add-on's real icon the way Harbor's extension page does.
// Priority: (1) a bundled official logo if we ship one (assets/addon-logos,
// matched by id/name in AddonLogos.js — instant, offline, no network stall),
// (2) the add-on's own manifest logo URL when given, (3) the honest coloured
// letter square as the last resort. Mirrors Harbor src/components/addon-logo.tsx,
// with bundled-first ordering because arbitrary logo hosts aren't IPv4-pinned here.
//
// The logo floats on a rounded plate (PreserveAspectFit) rather than an effect-
// masked full-bleed crop: no per-icon MultiEffect FBO (the Discover page shows
// 20+ icons at once), and it renders identically on-GPU and in offscreen grabs.
```

<a id="file-qml-addonlogos-js"></a>
## `qml/AddonLogos.js`

- Status: **CURRENT**
- Accepted blob: `25ab0e2dea4f34145e4d743a4c162cfa761ab920`
- Current blob: `25ab0e2dea4f34145e4d743a4c162cfa761ab920`
- Source: [`qml/AddonLogos.js`](../../qml/AddonLogos.js)

```text
// AddonLogos.js — the bundled official-logo match table, ported from Harbor's
// src/components/addon-logo.tsx BUNDLED array and extended with the curated
// add-ons Harbor doesn't ship (logos pulled from each add-on's own manifest by
// scripts/fetch_addon_logos.py). logoFor(id, name) returns a local asset path
// under assets/addon-logos/, or "" when we ship no logo — in which case the
// AddonLogo component draws the honest letter square instead.
//
// Order matters: first match wins, so put specific matchers before generic ones.
// Paths are relative to AddonLogo.qml (in qml/), which is where they're drawn.
```

<a id="file-qml-discoverapi-js"></a>
## `qml/DiscoverApi.js`

- Status: **CURRENT**
- Accepted blob: `41323b104d1a404737c2e6ef16e5adba88f2aa69`
- Current blob: `41323b104d1a404737c2e6ef16e5adba88f2aa69`
- Source: [`qml/DiscoverApi.js`](../../qml/DiscoverApi.js)

```text
// DiscoverApi.js — pure derivations for the Discover page (Stage 1, arc 2026-07-23).
// Computes the three pickers and the fetch URLs from Extensions.installed() manifests.
// A .pragma library can't see context properties: the PAGE passes the installed list
// into every call. Fetch-free except loadPage (which rides AddonClient's XHR lane).
```

<a id="file-qml-extensionscatalog-js"></a>
## `qml/ExtensionsCatalog.js`

- Status: **CURRENT**
- Accepted blob: `f53c4495b4b7227e5e8bd4729048356498945604`
- Current blob: `f53c4495b4b7227e5e8bd4729048356498945604`
- Source: [`qml/ExtensionsCatalog.js`](../../qml/ExtensionsCatalog.js)

```text
// ExtensionsCatalog.js — the store's shelf data: curated rails (Harbor's list, ported
// without its adult entries — baked at port time, nothing to unfilter here) and the
// community registry (stremio-addons.net, with Stremio's official collection as
// the fallback well). The community path is NO LONGER a hard wall: as of 2026-08-15 it
// honours the global `showExplicit` preference (ContentPreferences.qml) like every other
// surface, so with the Settings switch on the user installs whatever the registry lists.
// Ratified mock: agents/colosseum-extensions-mock.html;
// spec: docs/superpowers/specs/2026-07-05-colosseum-extensions-store-design.md.
```

<a id="file-qml-extensionspage-qml"></a>
## `qml/ExtensionsPage.qml`

- Status: **CURRENT**
- Accepted blob: `a92fe176fec38d8107dc58da3c55e929e3bfc5e1`
- Current blob: `a92fe176fec38d8107dc58da3c55e929e3bfc5e1`
- Source: [`qml/ExtensionsPage.qml`](../../qml/ExtensionsPage.qml)

```text
// ExtensionsPage — the extension store: one page where Stremio-protocol addons
// are discovered, browsed, installed, toggled, ordered and removed.
// Ratified design: agents/colosseum-extensions-mock.html (2026-07-05, "we can go
// for it"), spec: docs/superpowers/specs/2026-07-05-colosseum-extensions-store-design.md.
// Serves all three worlds, all live as of stage 1a — Tankoban and Biblio carry real
// catalogues and wells now. Data = `Extensions` (the C++ registry)
// + ExtensionsCatalog.js (curated rails, community registry, adult wall).
```

<a id="file-qml-extensionssources-qml"></a>
## `qml/ExtensionsSources.qml`

- Status: **CURRENT**
- Accepted blob: `b25f8218e549a73fdfb915d36429c6bdce877cfe`
- Current blob: `b25f8218e549a73fdfb915d36429c6bdce877cfe`
- Source: [`qml/ExtensionsSources.qml`](../../qml/ExtensionsSources.qml)

```text
// ExtensionsSources — the world-agnostic Sources pane.
//
// Hemanth's brief (2026-07-26): "a completely new extension page that is world agnostic,
// meaning all the extensions (theatre, biblio, tankoban) are in one page but in different
// rows." Browse and Installed stay beside it, unchanged.
//
// Shape: a chain across the top showing what each world asks and in what order, then one
// section per world. The chain is the page's thesis — the order sources are asked in is the
// single fact that governs what you actually get, and nothing in the app has ever shown it.
//
// SECTIONS ARE DATA, NOT CODE. `sections` below is derived from the installed roster, so a
// world with nothing in it does not render, and UNIVERSES appears the moment a universe
// extension is installed without another line of layout. That matters: Hemanth caught that
// an "ask order" framing breaks for universes, which aggregate an IP and fetch nothing. They
// are their own world here (Catalog.worldsFor returns ["universes"] by role, universes design
// §5.1a), so they get a section with no ranks at all rather than a broken position in a queue.
//
// Vocabulary: CATALOGUE fills the shelves, SOURCES fetch the file, ALSO INSTALLED is neither.
// The group was briefly called "ASK ORDER" — he asked what that meant, which is the label
// failing. The rank numerals carry the order; the words do not have to.
```

<a id="file-qml-universeextapi-js"></a>
## `qml/UniverseExtApi.js`

- Status: **CURRENT**
- Accepted blob: `815b7369113fd30613bfb367dfe9c31e79b78dcd`
- Current blob: `815b7369113fd30613bfb367dfe9c31e79b78dcd`
- Source: [`qml/UniverseExtApi.js`](../../qml/UniverseExtApi.js)

```text
// UniverseExtApi.js — load, validate and cache a universe extension's payload.
//
// The payload contract is the universes-as-extensions design §5.2. Its end state is a
// served universe.json over HTTPS (§5.5); until that server exists the same document is
// bundled at assets/universes/<file>.json. Same shape, same loader, same validation — so
// the move to HTTPS changes the URL below and nothing else.
//
// VALIDATION IS A GATE, NOT A FORMALITY. A video tile that reaches Theatre without a type
// opens a series as a movie and dies (§5.4). An invalid entry is DROPPED and the rest of
// the payload still renders; a section left empty by that is removed entirely, because an
// empty row is a lie about what the universe holds.
```
