# Reader2 — Paper (Anx foliate-js fork) API notes

> Task 1 reference. Every later task's glue code is checked against THIS file. Where a name here
> differs from the plan, **these notes win** — they are read/grep-verified from the vendored `src/`,
> not guessed. Browser-proven on 2026-07-16 (see "Browser proof" at the bottom).
>
> `[Agent 2 (Claude), biblio]`

## Pinned donor

- **Donor repo:** `anxcye/anx-reader` (Flutter reader; MIT-forked foliate-js lives in its `assets/foliate-js`).
- **Pinned donor SHA:** `107f4fa74db0e7247c846c49d6211df3edf9887c` (shallow-clone HEAD, 2026-07-16).
- **Vendored to:** `resources/reader2/vendor/foliate-anx/` (copied from donor `assets/foliate-js`, verbatim).
- **License chain (from vendored `README.md`):** forked from [johnfactotum/foliate-js](https://github.com/johnfactotum/foliate-js) (MIT), with some changes from [readest/foliate-js](https://github.com/readest/foliate-js) (MIT). KEEP `README.md` for the attribution chain. (Note: the fork's `package.json` mislabels `"license": "ISC"`; the authoritative statement is the README's MIT chain — the code is MIT-derived.)

## What landed in the vendor tree

`resources/reader2/vendor/foliate-anx/` — 117 files. Notables:

- `src/` — dependency-free **ES modules** (the "paper"). This is what we load. Key files:
  `book.js` (entry/orchestrator), `view.js` (the `<foliate-view>` custom element), `epub.js`,
  `mobi.js`, `fb2.js`, `pdf.js`, `comic-book.js`, `paginator.js` (renderer), `overlayer.js`
  (annotations), `search.js`, `epubcfi.js`, `tts.js`, `footnotes.js`, `progress.js`, `dict.js`.
- `src/vendor/` — bundled 3rd-party deps kept in-tree (no npm install needed): `zip.js`, `fflate.js`,
  `pdfjs/` (PDF.js), `prism/` (code highlighting).
- `dist/` — webpack **legacy bundle** (`bundle.js`, `pdf-legacy.js`, `pdf-legacy.worker.js`) for old
  engines. `index.html` picks `src/` (modern, Chrome ≥100 / Apple) vs `dist/` (legacy) at runtime.
  **We are on a modern Chromium (Qt WebEngine / Electron), so we use `src/` directly.** `dist/` kept
  as a fallback only.
- `debug.html` — their standalone test harness (mocks the Flutter channel). Used for the browser proof.
- `index.html` — their production entry (engine-sniffs modern vs legacy bundle).
- `README.md` / `package.json` — attribution + webpack packaging config (webpack is only their build
  convenience; **not required** — `src/` runs as-is over http://).
- Deleted nothing (no `node_modules` came over). Vendored `.gitignore` only ignores `node_modules/`.

## How the paper boots (entry + view creation)

**Entry module: `src/book.js`** (loaded as `<script type="module">`). For PDF it ALSO needs
`src/vendor/pdfjs/pdf.js` loaded as a **classic (non-module) script BEFORE `book.js`** — it defines
the global `pdfjsLib` that `src/pdf.js` consumes. (EPUB/MOBI/FB2/CBZ do NOT need it.)

`book.js` self-boots from **URL query params** (all JSON-encoded), at its very bottom:

```js
var urlParams    = new URLSearchParams(window.location.search)
var importing    = JSON.parse(urlParams.get('importing'))    // bool; true = metadata-only import pass
var url          = JSON.parse(urlParams.get('url'))          // string: URL the paper will fetch()
var initialCfi   = JSON.parse(urlParams.get('initialCfi'))   // string CFI | null  (resume point)
var style        = JSON.parse(urlParams.get('style'))        // big style object (see below)
var readingRules = JSON.parse(urlParams.get('readingRules')) // {convertChineseMode, bionicReadingMode}
fetch(url).then(r => r.blob())
          .then(blob => open(new File([blob], new URL(url,origin).pathname), initialCfi))
```

So the paper **fetches its own book file over HTTP** from the `url` param — it does NOT accept the
bytes pushed in. **Bridge implication for Qt:** the host must expose the book file at a URL the
WebEngine page can `fetch()` (a `qrc:`/`file:`/custom-scheme URL, or a localhost handler). All five
params MUST be supplied — `JSON.parse(null)` on a missing `style` would crash `getCSS`.

`open(file, cfi)` does: `const reader = new Reader(); globalThis.reader = reader; await reader.open(file, cfi)`.
So there is a **global `window.reader`** (the orchestrator) whose `.view` is the `<foliate-view>`.

`reader.open` internals: `getView(file)` builds the book + element → wires view listeners (`load`,
`relocate`, `click-view`, `doctouchstart/move/end`, `create-overlay`, `draw-annotation`,
`show-annotation`, `external-link`, `link`, `click-image`, and `view.history` `pushstate`) →
`setStyle()` → `view.renderer.next()` (first page, only when no resume cfi) → `await view.init({ lastLocation: cfi })`.

`getView(file)` (format dispatch, by magic bytes): `document.createElement('foliate-view')` →
`document.body.append(view)` → `await view.open(book)` → returns the element.

## The `<foliate-view>` element (source-verified from `view.js`)

- Registered: `customElements.define('foliate-view', View)`; `class View extends HTMLElement`.
- **Methods used by the paper (this is the upstream foliate shape, CONFIRMED present in the fork):**
  - `open(book)` — mount a parsed book object.
  - `init({ lastLocation, showTextStart })` — first render / jump to resume point.
  - `goTo(target)` — target = CFI **or** href (used for both). (Plan's `view.goTo` ✓)
  - `goToFraction(frac)` — jump to 0..1 progress.
  - `next(distance)` / `prev(distance)` — page turn (whole view).
  - `renderer.next()` / `renderer.prev()` / `renderer.nextSection()` / `renderer.prevSection()` —
    finer-grained paging on the paginator. **Both layers exist** (plan mentioned only `renderer.prev/next`).
  - `addAnnotation(annotation, remove)` and `deleteAnnotation(annotation)` (= `addAnnotation(a, true)`).
    (Plan's `addAnnotation/deleteAnnotation` ✓ — remove is the 2nd-arg overload.)
  - `search(opts)` — **async generator** (`async * search`), yields `{progress}` items, result objects,
    then the string `'done'`. (Plan's `view.search()` ✓)
  - `resolveNavigation(target)`, `deselect()`, `goToTextStart()`, `initTTS(stop)`, `history.back()/forward()`,
    `history.canGoBack/canGoForward`.
  - Properties: `view.lastLocation` (`{cfi, fraction, location, tocItem, pageItem, chapterLocation, range}`),
    `view.book` (`.metadata`, `.toc`, `.sections`, `.getCover()`), `view.renderer` (the paginator; also
    `renderer.writingMode`, `renderer.setStyles(css)`, `renderer.setAttribute(...)`), `view.tts`.

- **CustomEvents the element dispatches** (via `#emit(name, detail, cancelable)` = `dispatchEvent(new CustomEvent(...))`).
  These are internal — `book.js` listens and re-emits to the host through the bridge below:
  | event | detail | plan match |
  |---|---|---|
  | `relocate` | `= lastLocation` (`{cfi, fraction, location, tocItem, pageItem, chapterLocation, range}`) | ✓ relocate |
  | `load` | `{doc, index}` | ✓ load |
  | `external-link` | `{a, href}` (cancelable) | — |
  | `link` | `{a, href}` (cancelable; footnote/internal nav) | — |
  | `click-view` | `{x, y}` (client px) | — (becomes `onClick`, normalized) |
  | `click-image` | `{img}` | — |
  | `draw-annotation` | `{draw, annotation, doc, range}` | overlayer draw |
  | `show-annotation` | `{value, index, range}` | overlayer click |
  | `create-overlay` | `{index}` | overlayer add |
  | `history.pushstate` / `popstate` | nav state | — |

## THE BRIDGE — the vocabulary our Qt spine must mimic

The paper's host contract is **NOT** raw `<foliate-view>` events. It is a **Flutter-style JS channel**:

### OUT (paper → host): `window.flutter_inappwebview.callHandler(name, data)`

Defined once: `const callFlutter = (name, data) => window.flutter_inappwebview.callHandler(name, data)`.
**Our Qt bridge injects `window.flutter_inappwebview = { callHandler(name, data){ …forward to C++… } }`.**
Handler names the paper emits (name → payload):

| handler | payload | meaning |
|---|---|---|
| `onLoadEnd` | (none) | book finished loading; safe to show |
| `onSetToc` | `reader.toc` (array of `{label, href, subitems, id}`) | table of contents |
| `renderAnnotations` | (none) | host should now push saved annotations back via `window.renderAnnotations()` |
| `onRelocated` | `{chapterTitle, chapterHref, chapterTotalPages, chapterCurrentPage, bookTotalPages, bookCurrentPage, cfi, percentage, bookmark, writingMode}` | **position/progress changed — this is the resume + progress signal (Task 6).** Fires once per page turn. |
| `onPushState` | `{canGoBack, canGoForward}` | nav history changed |
| `onSelectionEnd` | `{index, range, lang, cfi, pos:{left,top,right,bottom} (0..1), text, contextText, footnote}` | user finished a text selection (Task 9 pen) |
| `onSelectionCleared` | (none) | selection collapsed → hide context menu |
| `onClick` | `{x, y}` (normalized 0..1) | tap on page → host toggles chrome (Task 7) |
| `onAnnotationClick` | `{annotation, pos, contextText}` | tapped an existing highlight |
| `onExternalLink` | link detail `{a, href}` | external URL |
| `onImageClick` | base64 data-URL string | tapped an image |
| `onMetadata` | `{...book.metadata, cover}` (cover = base64) | import pass only (`importing=true`) |
| `onSearch` | a result object, or `{process: <0..1>}` | streaming search results (Task 11) |
| `onFootnoteClose` | (none) | footnote popup closed |
| `onPullUp` | (none) | pull-up gesture |
| `handleBookmark` | `{...}` | bookmark toggled at current page |

### IN (host → paper): global `window.*` functions on the page

Call these from C++/QML (WebEngine `runJavaScript`). Verified list from `book.js`:

- **Navigation:** `goToCfi(cfi)`, `goToHref(href)`, `goToPercent(0..1)`, `nextPage()`, `prevPage()`,
  `nextSection()`, `prevSection()`, `back()`, `forward()`.
- **Appearance (Task 10, live-apply):** `changeStyle(newStyle)` (merges + re-applies CSS + layout),
  `setScroll()`, `setPaginated()`, `setNoAnimation()`, `readingFeatures(rules)`, `refreshToc()`,
  `initCodeHighlighting(theme)`.
- **Annotations/bookmarks (Tasks 8/9):** `addAnnotation(annotation)`, `removeAnnotation(cfi)`,
  `renderAnnotations(annotations)`, `addBookmarkHere()`.
  - annotation shape: `{ id, value:<cfi>, type:'highlight'|'underline'|'bookmark', color, note }`.
- **Selection/menu (Task 9):** `showContextMenu()`, `getSelection()`, `clearSelection()`.
- **Search (Task 11):** `search(text, opts)` where `opts = {scope:'book'|'section', matchCase, matchDiacritics, matchWholeWords}`, `clearSearch()`.
- **Content extraction (read-along/AI, Tasks 12–13):** `theChapterContent()`, `previousContent(count)`,
  `getChapterContentByHref(href, opts)`.
- **TTS / read-along (Tasks 12–13):** `initTts()`, `ttsStop()`, `ttsHere()`, `ttsFromCfi(cfi)`,
  `ttsNext()`, `ttsPrev()`, `ttsNextSection()`, `ttsPrevSection(last)`, `ttsPrepare()`,
  `ttsCurrentDetail()`, `ttsCollectDetails(count, includeCurrent, offset)`, `ttsHighlightByCfi(cfi)`.
- **Footnotes:** `isFootNoteOpen()`, `closeFootNote()`. Misc: `pullUp()`.
- Plus the global object `window.reader` with methods `open`, `renderAnnotation`, `addAnnotation`,
  `removeAnnotation`, `showContextMenu`, `getSelection`, `getChapterContent`, `getPreviousContent`, `toc`.

### The `style` object (all keys `changeStyle`/boot accept)

`fontSize`(em), `fontName`(`'book'` = publisher font / `'system'` / family name), `fontPath`(@font-face src),
`fontWeight`, `letterSpacing`(px), `spacing`(line-height), `paragraphSpacing`, `textIndent`(em),
`fontColor`(#rrggbbaa), `backgroundColor`(#rrggbbaa), `topMargin`/`bottomMargin`(px), `sideMargin`(%, → renderer `gap`),
`justify`(bool), `hyphenate`(bool), `pageTurnStyle`(`'slide'|'scroll'|'noAnimation'`), `maxColumnCount`(int),
`writingMode`(`'auto'|'horizontal-tb'|'vertical-rl'`), `backgroundImage`, `allowScript`(bool), `textAlign`,
`useBookStyles`(bool — when true, publisher CSS wins), `headingFontSize`, `customCSS`, `customCSSEnabled`,
`codeHighlightTheme`, `columnThreshold`, optional `bgimgBlur`/`bgimgOpacity`/`bgimgFit`.

## Format support — VERDICT per format (verified from `getView()` magic-byte dispatch in `book.js`)

| format | verdict | how |
|---|---|---|
| **EPUB** | **SUPPORTED** | ZIP magic (`PK\x03\x04`) → `new EPUB(loader).init()` |
| **CBZ** (comic) | **SUPPORTED** | ZIP + `.cbz`/`application/vnd.comicbook+zip` → `makeComicBook` |
| **FB2** | **SUPPORTED** | `.fb2`/`application/x-fictionbook+xml` → `makeFB2` |
| **FBZ** (zipped fb2) | **SUPPORTED** | ZIP + `.fb2.zip`/`.fbz` → `makeFB2` |
| **MOBI / AZW3** | **SUPPORTED** | `isMOBI(file)` → `new MOBI({unzlib: fflate.unzlibSync}).open()` |
| **PDF** | **SUPPORTED** | `%PDF-` magic → `makePDF`; sets `isPdf=true` (changes selection UX); **requires `src/vendor/pdfjs/pdf.js` global loaded first** |
| **Directory** (unzipped epub) | supported | `file.isDirectory` → directory loader → EPUB |
| **TXT (plain text)** | **NOT SUPPORTED** | `getView()` has **no** text/plain branch; unmatched types hit `throw new Error('File type not supported')`. There is no `text.js` module (`text-walker.js` is a search/TTS DOM walker, not a format handler). |

> **Plan correction (notes win):** Task 14 lists "TXT" as a parity target. This fork **cannot open a
> raw `.txt`** as-is. Options for Task 14: (a) wrap `.txt` into minimal HTML/EPUB before handing to the
> paper, or (b) add a small text→FB2/HTML shim in `src/`. Do NOT assume `.txt` "just works". MOBI/FB2/PDF
> all work out of the box.

## Browser proof (2026-07-16, in-app Browser pane, Electron/Chromium 148)

Served the vendored tree with `python -m http.server 8971` (ES modules need http://, not file://) and
opened `debug.html` with all five params, `url` → a real staged EPUB (`Arthur Conan Doyle — A Study In
Scarlet`, 672 KB, from `%APPDATA%/Roaming/Brotherhood/Colosseum/books/`; staged copy deleted before commit).

Verified via console + live DOM (screenshots timed out — a known Electron-pane capture flake, not a
render failure; DOM inspection is the ground truth):
- `globalThis.reader` exists; `reader.view.tagName === 'FOLIATE-VIEW'`.
- Book parsed: `view.book.metadata.title === 'A Study In Scarlet'`, author `Arthur Conan Doyle`;
  `view.book.toc.length === 4`.
- Bridge fired on load: `onPushState`, **`onLoadEnd`**, **`onSetToc`** (4 items), `renderAnnotations`.
- **Pages turn:** driving `view.next()` walked the CFI monotonically across the spine
  `/6/2 → /6/4 → /6/6 → /6/8 → /6/10 → /6/12 → /6/14` (6+ distinct positions), and **`onRelocated`
  fired once per turn**.
- **Real text renders:** page content read from the paginator iframe, e.g.
  `"CHAPTER VI. TOBIAS GREGSON SHOWS WHAT HE CAN DO — THE papers next day were full of the 'Brixton Mystery'…"`.
- Gotcha found: awaiting `view.next()`/`window.nextPage()` (the animated turn) can hang a headless-ish
  pane; the paper is fine, but **host code should fire page turns without awaiting the animation
  promise** (or use `pageTurnStyle: 'noAnimation'`). Worth remembering for the Qt harness (Task 5).

## Bridge shim vocabulary for the Qt spine (Tasks 2/4 handoff)

To make the paper run under Qt WebEngine, the host page must, before `book.js` loads:
1. Define `window.flutter_inappwebview = { callHandler: (name, data) => <post to C++ via QWebChannel> }`.
2. Serve the book bytes at a URL the page can `fetch()` (pass it as the `url` param).
3. Provide all five JSON params (`importing,url,initialCfi,style,readingRules`).
4. Load `src/vendor/pdfjs/pdf.js` (classic) before `src/book.js` (module) if PDF is in scope.
Then drive the reader by calling the `window.*` IN functions and reacting to the `callHandler` OUT names.
