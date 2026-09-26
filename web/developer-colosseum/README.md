# Developer Web Colosseum frontend

This directory is Arc 54's production-intent HTML/CSS/JavaScript frontend for Home plus the three Colosseum worlds that the current native shell warms and retains: Tankoban, Biblio, and Theatre.

It is deliberately not a standalone reimplementation of Colosseum. The existing backend remains authoritative.

## Files

- `index.html` - persistent WebUI shell and search overlay.
- `styles.css` - Colosseum visual tokens, responsive layout, worlds, cards, rails, grids, and Home intro treatments.
- `bridge.js` - frontend/host transport contract.
- `app.js` - DOM rendering, local presentation state, focus/navigation, and action emission.

There is no persisted mock or preview dataset in this directory. Opening `index.html` by itself shows truthful waiting/empty states until a host snapshot arrives.

## Host entry point

After the page has loaded, the host supplies authoritative state with:

```js
window.ColosseumWeb.mount(snapshot)
```

Later changes may be merged without rebuilding the document:

```js
window.ColosseumWeb.patch(partialSnapshot)
```
The same messages can arrive through WebView2 as:

```js
{ type: "mount", data: snapshot }
{ type: "patch", data: partialSnapshot }
{ type: "surface", surface: "Theatre" }
```

The frontend announces `webui-ready` when its transport is ready. The host may then mount Home.

## Snapshot shape

The contract is intentionally tolerant of existing Colosseum row shapes rather than inventing a new canonical database model.

```js
{
  surface: "Home",
  accountInitial: "H",
  wallpaper: "file:///... or https://...",
  universes: [ /* installed Universe projection */ ],

  home: {
    continue: [ /* Progress-derived rows */ ],
    tankoban: { manga: [], comics: [] },
    theatre: { items: [] },
    biblio: { chart: [], genres: [] }
  },

  worlds: {
    Tankoban: { featured: [], nextUp: [], continue: [], tabs: {} },
    Biblio:   { featured: [], nextUp: [], continue: [], tabs: {} },
    Theatre:  { featured: [], nextUp: [], continue: [], tabs: {} }
  }
}
```
World tab payloads may be an array, `{items: []}`, `{rows: []}`, or:

```js
{
  sections: [
    {
      title: "Trending",
      layout: "rail", // or "grid"
      items: [],
      seeAll: true,
      pin: { /* backend-owned route pin */ }
    }
  ]
}
```

The frontend recognizes common existing Colosseum aliases such as `title/caption/name`, `cover/coverUrl/art/image/poster/banner`, and `id/seriesId/malId/gcdId/canonicalId` for presentation.

Crucially, the original item object is retained and returned as `action.item`. The WebUI does not reduce canonical backend identity to a title string.

## Backend-owned search

Search input emits:

```js
{ type: "search-query", query: "..." }
```

The host returns results through:

```js
window.ColosseumWeb.patch({
  search: { loading: false, results: [ /* backend results */ ] }
})
```

The browser does not query Cinemeta, Jikan, Apple, ComicsCatalog, Progress, Collection, or other providers/stores itself.
## Outbound actions

Actions are dispatched as the `colosseum:webui-action` window event and also sent through WebView2 `postMessage` when available.

A host that does not use WebView2 may register:

```js
window.ColosseumWeb.onAction(action => { ... })
```

Current action vocabulary includes:

- `webui-ready`, `request-snapshot`
- `home`, `open-world`, `world-tab`
- `open-item`, `resume`, `continue-details`, `next-up`
- `continue-see-all`, `see-all`
- `open-universe`, `open-universe-hall`
- `open-vault`, `open-genre`
- `search-open`, `search-close`, `search-query`
- `trackers`, `wallpaper`, `account`
- `window-minimize`, `window-toggle-fullscreen`, `window-close`

All user-visible routing remains a request to Colosseum. The frontend does not open native readers, players, provider sessions, databases, or files directly.

## Current integration boundary

Hemanth later explicitly authorized the minimal native hosting seam as long as Colosseum master stayed untouched. That seam now lives only on branch `arc54-developer-webui-seam`.

The branch uses `DeveloperWebUiBridge` + QWebChannel to project the existing Progress, Collection, MAL, Comics, Biblio, IMDb and Extensions owners into this frontend. It does not move ownership into browser JavaScript. Existing QML/native detail, reader, player, Vault, account and window routes remain authoritative.

The frontend is embedded into the branch build as `qrc:///developer-webui/index.html`. `COLOSSEUM_WEBUI=1` enables it.

## Theatre / Portico parity pass - 2026-09-26

Visual authority is Claude's `colosseum-theatre-portico-halfway-mock.html`. Its production-relevant CSS/layout was copied directly where possible while sample/mock data and provider logic were discarded.

Live embedded-DOM geometry on Windows reported:
- TopBar: x 54, y 30, 1172 x 56.
- Featured carousel: 1162 x 330.
- Theatre tab bar: 760 x 54.
- Catalogue poster art: 148 x 222.
- Theatre tabs: Discover, Movies, Shows, Anime, Library.
- Backend status: `Colosseum backend mounted`.
- Real catalogue rows included Shawshank Redemption, The Godfather, The Dark Knight, LOTR: Return of the King, Schindler's List, Inception and others.

Windows WebEngine reported `devicePixelRatio = 1.5`. Non-DPI-aware window capture therefore made the HTML appear shifted/cropped even though live DOM coordinates were correct.

## Verification - 2026-09-25

This frontend slice has been checked at the authored-source boundary:

- `node --check bridge.js` - pass.
- `node --check app.js` - pass.
- Mounted-DOM smoke through the real `ColosseumWeb.mount()` contract - pass.
- Smoke covered Home, Tankoban, Biblio, and Theatre.
- Exact default tab counts observed: Tankoban 4, Biblio 3, Theatre 5.
- A raw backend-only item field survived rendering and was returned unchanged on `open-item`.
- Backend-owned search results patched into the live DOM successfully.
- Chrome headless renders were visually inspected at 1280x720 for all four surfaces.
- Responsive Chrome renders were inspected at 760x900 for Home and Theatre.
- The narrow-width pass exposed and repaired a missing mouse Home affordance on world pages.

The browser fixture and screenshots used for verification were temporary test material outside the arc. No fake catalogue data was added to the production frontend.

This proves the HTML/CSS/JavaScript frontend and its host-facing contract at the browser boundary. It does not prove native WebView hosting, C++ projection code, player/reader handoff, or Colosseum.exe integration.
