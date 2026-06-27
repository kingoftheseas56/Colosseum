# Handoff — Genre Browse Page (Tankoban / manga lane)

**Agent 1 → Agent 1, across substrates.** This is me handing from the **Claude substrate** to **myself
on the Codex substrate** (Trigger D, scoped QML). Same role, same lane, same memory — Codex just fails
differently, so it smokes and hardens what Claude-side me wrote without running it. Not a commission to
another agent; a continuation of my own work. Sign the result `[Agent 1 (Codex), genre page]`.
**Date:** 2026-06-28 · **Lane:** A1 (Comics/Manga). **Repo:** `Colosseum/` (gitignored, local-only).

## Strategic intent
Colosseum's genre tiles (`GenreMosaic`) currently click into **nothing** — there is no genre listing
page. Hemanth approved a design (mock at `mocks/genre.html`) that recreates MyAnimeList's genre page in
the house glass: a hero where the genre is its own art (its top covers wash behind the title), a sibling-
genre hop row, a readers/score sort, a detailed/covers view toggle, and a grid of rich cards (cover ·
title · type/year/status/vol-chp · genre chips with the current genre lit gold · synopsis · ★score ·
readers · author · +Library). Data is **Jikan** (MyAnimeList's keyless API — no login, no key; the
standing sourcing law). **A1 has already written the two new files** (`GenrePage.qml`, `GenreApi.js`).
Your job is to **smoke them, fix any QML errors, and wire the navigation** so a genre tile opens the page.

## Already done (do NOT rewrite — only fix bugs you find when smoking)
- **`qml/GenreApi.js`** — Jikan data layer. `loadGenre(name, sort, push)` → `{count, desc, cards, montage}`.
  Genre name→MAL id map, per-genre editorial descriptions, sibling list, card mapper. Verified endpoint:
  `https://api.jikan.moe/v4/manga?genres=<id>&order_by=popularity&sort=asc&limit=24&sfw=true` (Adventure=2
  returns Berserk/One Piece/Solo Leveling — matches MAL exactly).
- **`qml/GenrePage.qml`** — the page. Layer `Item` mirroring `UniversePage.qml`'s contract: `backdrop`,
  `genreName`, `sortMode`, `compact`, signals `backRequested / minimizeRequested / closeRequested /
  searchClicked / seriesRequested(title)`. Loads via `GenreApi`, renders hero + pills + controls + grid.
  Card click → `seriesRequested(title)`.

## YOUR TASK

### 1. Smoke the page in isolation, fix QML errors
- Create throwaway harness `qml/_genrecheck.qml` (mirror `qml/_universecheck.qml`): a `Window` that loads
  `GenrePage { genreName: "Adventure"; anchors.fill: parent }`.
- Run it through the native launcher (runs live QML, no rebuild):
  `set QT_FORCE_STDERR_LOGGING=1` then `native\build-msvc\colosseum.exe qml\_genrecheck.qml`
  (PATH needs `C:\Qt\6.11.1\msvc2022_64\bin`, as in `dev.bat`).
- Fix every QML warning/error in `GenrePage.qml` / `GenreApi.js` until the page renders clean: hero with
  the Adventure montage + standfirst + "4,642 titles", sibling pills, sort/view controls, and ~24 rich
  cards with real covers/scores. Likely suspects to watch (A1 wrote this unsmoked): `Loader` +
  `sourceComponent` switching between `detailCard`/`coverTile` and reading `item.card`/`item.rank` +
  the `Connections { onOpen }` signal hookup; `RowLayout/ColumnLayout` inside the card; `Flow` chips;
  `toLocaleString()` on the count. Toggle sort (readers⇄score) and view (detailed⇄covers) — both must work.

### 2. Wire the navigation (the actual gap)
Mirror the existing layer pattern (`seriesLayer` / `universeLayer` in `Main.qml`). Exact edits:

**`qml/WorldPage.qml`** — add one signal next to the others (~line 33):
```qml
signal genreRequested(string genreName)   // tapped a genre tile → host opens its GenrePage
```

**`qml/TankobanWorld.qml`** — wire the **manga** GenreMosaic only (comics has no Jikan source yet; leave
it unwired — clicking it stays a no-op, no regression). Add to the first GenreMosaic (the manga one):
```qml
GenreMosaic {
    title: "Explore by Genre — Manga"
    genres: Catalog.genresManga
    onGenreClicked: (i) => tanko.genreRequested(Catalog.genresManga[i].name)
}
```

**`qml/Main.qml`** —
- Add open/close functions (mirror `openSeries`/`closeSeries`):
```qml
function openGenre(name) {
    genreLayer.genreName = name
    if (genreLayer.active && genreLayer.item) genreLayer.item.genreName = name
    else genreLayer.active = true
}
function closeGenre() { genreLayer.active = false }
```
- Add the layer (place it just below the `universeLayer` Loader; **z: 45** — above worlds/universe, below
  `seriesLayer` z:50 so a card can open a series over it):
```qml
Loader {
    id: genreLayer
    anchors.fill: parent
    z: 45
    active: false
    visible: active
    property string genreName: ""
    source: "GenrePage.qml"
    onLoaded: {
        item.backdrop = wall
        item.genreName = genreLayer.genreName
        item.backRequested.connect(win.closeGenre)
        item.minimizeRequested.connect(win.minimizeShell)
        item.closeRequested.connect(function() { Qt.quit() })
        item.searchClicked.connect(win.openSearch)
        item.seriesRequested.connect(win.openSeries)
    }
}
```
- In the `worldStack` Repeater `onLoaded` (~line 565), connect the new world signal:
```qml
item.genreRequested.connect(win.openGenre)
```
- In the Escape `Shortcut` chain, add genre **right after the `seriesLayer` check, before `universeLayer`**:
```qml
else if (seriesLayer.active) win.closeSeries()
else if (genreLayer.active) win.closeGenre()
else if (universeLayer.active) win.closeUniverse()
```

### 3. Smoke the full app
Run `native\build-msvc\colosseum.exe qml\Main.qml`. Path: boot → tap **Tankoban** pill → scroll to
**Explore by Genre — Manga** → click a tile (e.g. Action / Shounen) → **GenrePage opens** with live cards
→ click a card → **MangaSeries opens** → `‹` back returns to the genre page → `‹`/Esc returns to the world.
Capture stderr to a log; confirm no QML errors.

## §Files (touch ONLY these)
- `qml/GenrePage.qml` (fix bugs found in smoke)
- `qml/GenreApi.js` (fix bugs found in smoke)
- `qml/WorldPage.qml` (add `genreRequested` signal)
- `qml/TankobanWorld.qml` (wire manga GenreMosaic)
- `qml/Main.qml` (openGenre/closeGenre + genreLayer + worldStack connect + Esc)
- `qml/_genrecheck.qml` (NEW throwaway harness)

## CONSTRAINTS
**DO NOT:**
- Rebuild or touch `native/` — QML runs live against the existing launcher; A0 owns the MSVC migration,
  do not collide with it.
- Touch other lanes' files (Biblio*, Theatre*, SourcesSheet, MangaReader, MangaDownloader) or any
  `mocks/`, memory, or `.md` docs beyond posting your result.
- Add a maximize/restore button anywhere — **Colosseum is fullscreen-only**; chrome is minimize + power
  only (the page's back-bar already follows this).
- Use TMDB/fanart or any keyed/login source. Jikan only for genre data.
- Expand scope (no library store, no comics-genre source — those are separate, later).

**DO:**
- Match the house style (`Theme.qml` tokens: gold sparing, Fraunces display, glass; mirror `UniversePage`).
- Keep the approved look from `mocks/genre.html` — fix to MATCH the mock, don't redesign.
- Keep changes additive; preserve all code outside your diff.

## Acceptance
1. `_genrecheck.qml` renders the Adventure page clean (no QML errors in stderr), cards have real
   covers/scores, sort + view toggles work.
2. Full app: a manga genre tile opens GenrePage live; a card opens MangaSeries; back/Esc unwind correctly.
3. Diff limited to the §Files list. `native/` untouched.
4. Report back: what errors you fixed, the smoke result (with the stderr tail), and any open issue.
   Update `agents/handoff-genre-page-codex.md` with a `## Codex result` section, or leave a recap note.

## Codex result

Fixed the unsmoked QML load blockers in `GenrePage.qml`: renamed the page payload property from `data`
to `genreData` so it no longer shadows `Item.data`, changed fractional `font.pixelSize` values to
integer sizes accepted by Qt 6.11, replaced the invalid `RowLayout.topPadding` with
`Layout.topMargin`, and fixed the delegate `on`/`root.on` selection bug in the sibling/chip pills.

Wired the live navigation in the requested files: `WorldPage.genreRequested`, the Tankoban manga
`GenreMosaic.onGenreClicked`, `Main.openGenre/closeGenre`, `genreLayer` at `z: 45`, `worldStack`
signal hookup, and Escape unwind between `seriesLayer` and `universeLayer`.

Smoke:
- `_genrecheck.qml` via `native\build-msvc\colosseum.exe qml\_genrecheck.qml`: clean stderr after the
  existing downloads index line; Adventure page loads live.
- Temporary toggle smoke flipped `sortMode` to `score` and `compact` to covers view: clean stderr
  after the existing downloads index line.
- Temporary full-stack smoke loaded `Main`, opened Tankoban, opened Adventure `GenrePage`, opened
  `MangaSeries` for One Piece, then closed series/genre/world. Genre/Main/TankobanWorld produced no
  QML errors. Stderr tail:
  `MangaReader.qml:25:5: QML Settings: Failed to initialize QSettings instance... organizationName,
  organizationDomain`; `MangaFireCatalogClient: falling back to sitemap discovery... 403`;
  `[mangafire] 'One Piece' -> 117 volumes (114 with covers)`.

Open issue: direct Windows mouse-click smoke was unreliable because the Codex desktop window repeatedly
stole focus during screenshot review. The live QML layer path and series path were exercised through
the native launcher without rebuild; the remaining stderr is from the pre-existing MangaSeries/Reader
path, not the genre page wiring.

[Agent 1 (Codex), genre page]
