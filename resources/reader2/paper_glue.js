// paper_glue.js — reader2 "paper" glue (OUR thin protocol seam)
//
// This is the ONLY custom JS running on the paper page. It drives the vendored
// Anx foliate-js fork (`<foliate-view>`) directly and speaks OUR bridge — NOT the
// donor's Flutter host contract.
//
//   Commands DOWN : window.paper.*(...)                       (called by Qt via runJavaScript)
//   Events   UP   : window.bridge.paperEvent(name, jsonString) (forwarded to C++ / logged in bench)
//
// Format support is REUSED from the fork: the magic-byte dispatch below is lifted
// verbatim from vendor/foliate-anx/src/book.js `getView()` (that function is a
// non-exported `const`, and book.js self-boots + requires a #footnote-dialog on
// import, so we cannot import it — we call the SAME book-maker modules it calls).
//
// [Agent 2 (Claude), biblio]
//
// ESM-over-file:// (Qt) — this file is a CLASSIC script (loaded via a plain
// <script src="paper_glue.js">, NOT type=module), so it CANNOT use top-level
// static `import` statements: over file:// in QtWebEngine a module entry script
// with static imports is blocked. Instead the vendored fork's view.js (registers
// <foliate-view>) and overlayer.js (the highlight/underline painter) are pulled
// in via DYNAMIC `import(new URL(..., location.href))` inside boot() at the bottom
// — the proven house pattern (see resources/book_reader/.../engine_foliate.js),
// which works over BOTH file:// (Qt, localContentCanAccessFileUrls) and http://
// (the browser bench). The book-maker modules below (epub/mobi/fb2/pdf/zip) were
// already dynamic-imported, so only these two entry modules needed converting.
let Overlayer = null   // assigned in boot() once overlayer.js resolves
let FootnoteHandler = null  // the fork's footnote extractor class (footnotes.js), assigned in boot()
let footnoteHandler = null  // one instance; its render/before-render listeners attach once
let pendingFootnoteRect = null  // the tapped anchor's rect, captured at 'link' time (render is async)

// ---------------------------------------------------------------------------
// event emit (UP)
// ---------------------------------------------------------------------------
const emit = (name, payload) => {
  const json = JSON.stringify(payload ?? {})
  if (window.bridge && typeof window.bridge.paperEvent === 'function') {
    window.bridge.paperEvent(name, json)
  } else {
    // Browser fallback when no bridge is injected (production paper.html before QML).
    console.log('[paper-event]', name, json)
  }
}

// ---------------------------------------------------------------------------
// base64 -> File  (book bytes cross the Qt bridge as base64; QWebChannel corrupts raw binary)
// ---------------------------------------------------------------------------
const base64ToFile = (b64, name) => {
  const clean = String(b64 || '').replace(/^data:[^,]*,/, '') // tolerate a data: prefix
  const bin = atob(clean)
  const len = bin.length
  const bytes = new Uint8Array(len)
  for (let i = 0; i < len; i++) bytes[i] = bin.charCodeAt(i)
  return new File([bytes], name)
}

// ---------------------------------------------------------------------------
// format dispatch — lifted from book.js getView() (magic bytes, not extension)
// ---------------------------------------------------------------------------
const isZip = async file => {
  const a = new Uint8Array(await file.slice(0, 4).arrayBuffer())
  return a[0] === 0x50 && a[1] === 0x4b && a[2] === 0x03 && a[3] === 0x04
}
const isPDF = async file => {
  const a = new Uint8Array(await file.slice(0, 5).arrayBuffer())
  return a[0] === 0x25 && a[1] === 0x50 && a[2] === 0x44 && a[3] === 0x46 && a[4] === 0x2d
}
const isCBZ = ({ name, type }) =>
  type === 'application/vnd.comicbook+zip' || name.endsWith('.cbz')
const isFB2 = ({ name, type }) =>
  type === 'application/x-fictionbook+xml' || name.endsWith('.fb2')
const isFBZ = ({ name, type }) =>
  type === 'application/x-zip-compressed-fb2'
  || name.endsWith('.fb2.zip') || name.endsWith('.fbz')
// TXT is the one format the fork lacks — plain text has no magic bytes, so it can
// only be identified by extension (checked last, after every magic-byte branch misses).
const isTXT = ({ name }) => /\.txt$/i.test(name || '')

const makeZipLoader = async file => {
  const { configure, ZipReader, BlobReader, TextWriter, BlobWriter } =
    await import('./vendor/foliate-anx/src/vendor/zip.js')
  configure({ useWebWorkers: false })
  const reader = new ZipReader(new BlobReader(file))
  const entries = await reader.getEntries()
  const map = new Map(entries.map(entry => [entry.filename, entry]))
  const load = f => (name, ...args) =>
    map.has(name) ? f(map.get(name), ...args) : null
  const loadText = load(entry => entry.getData(new TextWriter()))
  const loadBlob = load((entry, type) => entry.getData(new BlobWriter(type)))
  const getSize = name => map.get(name)?.uncompressedSize ?? 0
  return { entries, loadText, loadBlob, getSize }
}

// The fork's epub.js Loader reads `JSON.parse(location.search 'style').allowScript`
// straight off the URL (the donor always boots the page with JSON params in the query).
// We don't self-boot from params, so we mirror our appearance into a fork-shaped `style`
// param on the URL before a book is parsed. Keeps the vendored src untouched.
const forkStyleFromAppearance = () => ({
  fontSize: (appearance.sizePx ?? 18) / 16,
  fontName: appearance.font ?? 'book',
  fontPath: '',
  fontWeight: 400,
  letterSpacing: 0,
  spacing: appearance.lineHeight ?? 1.5,
  paragraphSpacing: 1,
  textIndent: 0,
  fontColor: appearance.theme?.fg ?? '#e6e1d5',
  backgroundColor: appearance.theme?.bg ?? '#000000',
  justify: !!appearance.justify,
  textAlign: 'auto',
  hyphenate: false,
  writingMode: 'auto',
  backgroundImage: '',
  topMargin: appearance.marginPx ?? 48,
  bottomMargin: appearance.marginPx ?? 48,
  sideMargin: 5,
  maxColumnCount: 1,
  columnThreshold: 800,
  pageTurnStyle: 'noAnimation',
  useBookStyles: false,
  headingFontSize: 1,
  customCSS: '',
  customCSSEnabled: false,
  codeHighlightTheme: 'off',
  allowScript: false,
})

const syncVendorParams = () => {
  try {
    const params = new URLSearchParams(window.location.search)
    params.set('style', JSON.stringify(forkStyleFromAppearance()))
    if (!params.has('importing')) params.set('importing', 'false')
    if (!params.has('initialCfi')) params.set('initialCfi', 'null')
    if (!params.has('url')) params.set('url', 'null')
    if (!params.has('readingRules'))
      params.set('readingRules', JSON.stringify({ convertChineseMode: 'none', bionicReadingMode: false }))
    history.replaceState(null, '', window.location.pathname + '?' + params.toString())
  } catch (e) { console.warn('[paper] syncVendorParams failed', e) }
}

// Build a parsed "book" object from a File. Same branches as the fork's getView().
const makeBook = async file => {
  let book
  if (!file.size) throw new Error('File not found')
  else if (await isZip(file)) {
    const loader = await makeZipLoader(file)
    if (isCBZ(file)) {
      const { makeComicBook } = await import('./vendor/foliate-anx/src/comic-book.js')
      book = makeComicBook(loader, file)
    } else if (isFBZ(file)) {
      const { makeFB2 } = await import('./vendor/foliate-anx/src/fb2.js')
      const { entries } = loader
      const entry = entries.find(e => e.filename.endsWith('.fb2'))
      const blob = await loader.loadBlob((entry ?? entries[0]).filename)
      book = await makeFB2(blob)
    } else {
      const { EPUB } = await import('./vendor/foliate-anx/src/epub.js')
      book = await new EPUB(loader).init()
    }
  } else if (await isPDF(file)) {
    const { makePDF } = await import('./vendor/foliate-anx/src/pdf.js')
    book = await makePDF(file)
  } else {
    const { isMOBI, MOBI } = await import('./vendor/foliate-anx/src/mobi.js')
    if (await isMOBI(file)) {
      const fflate = await import('./vendor/foliate-anx/src/vendor/fflate.js')
      book = await new MOBI({ unzlib: fflate.unzlibSync }).open(file)
    } else if (isFB2(file)) {
      const { makeFB2 } = await import('./vendor/foliate-anx/src/fb2.js')
      book = await makeFB2(file)
    } else if (isTXT(file)) {
      // OUR addition (not in the fork): synthesize a reflowable XHTML book from plain
      // text so it reads with full appearance/pagination/selection. See paper_text.js.
      const { makeTextBook } = await import('./paper_text.js')
      book = await makeTextBook(file)
    }
  }
  if (!book) throw new Error('File type not supported')
  return book
}

// ---------------------------------------------------------------------------
// reader state
// ---------------------------------------------------------------------------
let currentView = null
let flatToc = []                       // [{index,label,href}] flattened, DFS order
const annotations = new Map()          // id -> { id, value:cfi, type, color }
let readyEmitted = false               // gate: suppress 'relocated' until 'ready' has fired for THIS book
let sectionHasText = true              // is the CURRENT section real prose (vs a cover/full-image page)?
                                       // reported as relocated.textPage so the reading ruler only dims
                                       // TEXT pages — a focus band over a cover image is a bug, not an aid.

// appearance state + defaults (dark paper)
let appearance = {
  theme: { bg: '#000000', fg: '#e6e1d5' },
  font: 'book',      // 'book' = publisher font, 'system', or a family name
  sizePx: 18,
  lineHeight: 1.5,
  marginPx: 48,
  justify: false,
}

// ---------------------------------------------------------------------------
// toc helpers
// ---------------------------------------------------------------------------
const flattenToc = (items, out = []) => {
  for (const it of (items || [])) {
    out.push({ index: out.length, label: it.label ?? '', href: it.href ?? '' })
    if (it.subitems && it.subitems.length) flattenToc(it.subitems, out)
  }
  return out
}
const stripFrag = href => String(href || '').split('#')[0]
const tocIndexByHref = href => {
  const h = stripFrag(href)
  if (!h) return -1
  const hit = flatToc.find(t => stripFrag(t.href) === h)
  return hit ? hit.index : -1
}

// ---------------------------------------------------------------------------
// geometry — range client rect including the book iframe offset
// ---------------------------------------------------------------------------
const clientRectOf = (range, doc) => {
  const r = range.getBoundingClientRect()
  let offL = 0, offT = 0
  const frame = doc?.defaultView?.frameElement
  if (frame) {
    const fr = frame.getBoundingClientRect()
    offL = fr.left
    offT = fr.top
  }
  return {
    x: Math.round(r.left + offL),
    y: Math.round(r.top + offT),
    w: Math.round(r.width),
    h: Math.round(r.height),
  }
}

// ---------------------------------------------------------------------------
// appearance -> renderer
// ---------------------------------------------------------------------------
// THE CLASSIC GOTCHA (Task 10): the QML FontLoader that registers our shipped fonts for
// the native chrome does NOT make them available to the WebEngine BOOK page — that page is
// a separate document (the book renders in an iframe whose styles come from setStyles). So
// asking for `* { font-family: Literata }` alone would silently fall back to a system serif.
// The fix is to declare the fonts with @font-face INSIDE the book's own stylesheet, pointing
// at the bundled TTFs by absolute file:// URL. The old reader proves this resolves in
// QtWebEngine (localContentCanAccessFileUrls) — it injects ReadiumCSS @font-face the same way.
// Built once from paper.html's location (../../assets/fonts/), reused on every setStyles.
const fontUrl = rel => {
  try { return new URL(rel, window.location.href).toString() }
  catch (e) { return rel }
}
const FONT_FACE_CSS = `
    @font-face { font-family: 'Literata'; font-style: normal; font-weight: 400;
      src: url('${fontUrl('../../assets/fonts/Literata-Regular.ttf')}') format('truetype'); }
    @font-face { font-family: 'Literata'; font-style: italic; font-weight: 400;
      src: url('${fontUrl('../../assets/fonts/Literata-Italic.ttf')}') format('truetype'); }
    @font-face { font-family: 'Fraunces'; font-style: normal; font-weight: 400;
      src: url('${fontUrl('../../assets/fonts/Fraunces-Regular.ttf')}') format('truetype'); }
    @font-face { font-family: 'Fraunces'; font-style: italic; font-weight: 400;
      src: url('${fontUrl('../../assets/fonts/Fraunces-Italic.ttf')}') format('truetype'); }
    @font-face { font-family: 'Inter'; font-style: normal; font-weight: 400;
      src: url('${fontUrl('../../assets/fonts/Inter-Regular.otf')}') format('opentype'); }
`

const buildCss = (bg, fg) => {
  const font = appearance.font
  const fontCss = (!font || font === 'book') ? ''
    : font === 'system' ? '* { font-family: system-ui !important; }'
    : `* { font-family: ${font} !important; }`
  const lh = appearance.lineHeight ?? 1.5
  const size = appearance.sizePx ?? 18
  const align = appearance.justify ? 'justify' : 'start'
  return `
    @namespace epub "http://www.idpf.org/2007/ops";
    ${FONT_FACE_CSS}
    html { color: ${fg} !important; background-color: transparent !important; font-size: ${size}px !important; }
    body { background: none !important; padding: 0; }
    p, li, blockquote, dd, div, font { color: ${fg} !important; line-height: ${lh} !important; text-align: ${align}; }
    a, a:link { color: #a76034 !important; }
    ${fontCss}
  `
}

const applyAppearance = () => {
  const bg = appearance.theme?.bg ?? '#000000'
  const fg = appearance.theme?.fg ?? '#e6e1d5'
  document.documentElement.style.backgroundColor = bg
  if (document.body) document.body.style.backgroundColor = bg
  const r = currentView?.renderer
  if (!r) return
  // FIXED-LAYOUT FORMATS (PDF, CBZ): the pages are images, not reflowable text, so the
  // reflow knobs — font / size / line-height / margins / justify / columns — have no
  // meaning. Degrade GRACEFULLY: apply just the theme background (shown around the fixed
  // page) and skip the rest. The <foliate-fxl> renderer ignores these attributes and has
  // no setStyles(), so the appearance panel already can't throw here — but setting them
  // is noise, and doing this explicitly keeps a future fxl build from honouring (and
  // distorting) them. The sliders simply have no visible effect on a PDF, by design.
  if (currentView?.isFixedLayout) {
    r.setAttribute('background-color', bg)
    return
  }
  r.setAttribute('flow', 'paginated')
  r.setAttribute('background-color', bg)
  r.setAttribute('top-margin', `${appearance.marginPx ?? 48}px`)
  r.setAttribute('bottom-margin', `${appearance.marginPx ?? 48}px`)
  const gapPct = Math.max(2, Math.min(18,
    Math.round(((appearance.marginPx ?? 48) / (window.innerWidth || 1000)) * 100)))
  r.setAttribute('gap', `${gapPct}%`)
  r.setAttribute('max-column-count', '1')
  r.removeAttribute('animated')                 // no-animation page turns
  r.setStyles?.(buildCss(bg, fg))
}

// ---------------------------------------------------------------------------
// view wiring — element events UP to our bridge
// ---------------------------------------------------------------------------
const reAddAnnotations = () => {
  for (const a of annotations.values()) currentView?.addAnnotation(a)
}

// ---------------------------------------------------------------------------
// footnotes (TASK 9 R2) — reuse the fork's FootnoteHandler (footnotes.js), the proven
// path book.js uses. On a footnote/endnote/noteref link tap the handler resolves the
// note href, spins a throwaway <foliate-view> to render just the note fragment, and
// fires 'render'; we read that fragment's TEXT and emit it UP as 'footnote' for a native
// FootnoteCard — we do NOT navigate the page to the note (book.js shows a modal; we show
// a card). The offscreen render host is our own hidden div (not the fork's
// #footnote-dialog) so it always renders regardless of the dialog's display state.
// ---------------------------------------------------------------------------
const footnoteHost = () => {
  let host = document.getElementById('reader2-footnote-host')
  if (!host) {
    host = document.createElement('div')
    host.id = 'reader2-footnote-host'
    // connected + laid out but off-screen: an iframe only loads (fires the view 'load'
    // that drives extraction) while attached to the document; visibility is irrelevant.
    host.style.cssText =
      'position:fixed;left:-99999px;top:0;width:600px;height:600px;overflow:hidden;pointer-events:none;'
    document.body.appendChild(host)
  }
  return host
}

const FOOTNOTE_TEXT_CAP = 4000   // defensive: a well-formed note is short; never ship a chapter

const setupFootnoteHandler = () => {
  if (!FootnoteHandler || footnoteHandler) return
  footnoteHandler = new FootnoteHandler()
  // before-render: attach the throwaway view to our hidden host so its section iframe
  // actually loads (mirrors book.js replaceFootnote, minus the modal styling).
  footnoteHandler.addEventListener('before-render', e => {
    const view = e.detail?.view
    if (!view) return
    try {
      footnoteHost().replaceChildren(view)
      const r = view.renderer
      if (r) {
        r.setAttribute('flow', 'scrolled')
        r.setAttribute('gap', '5%')
        r.setAttribute('top-margin', '0px')
        r.setAttribute('bottom-margin', '0px')
      }
    } catch (err) { console.warn('[paper] footnote before-render', err) }
  })
  // render: the note fragment now lives in the throwaway view's section doc.body — read
  // its text, emit UP, then drop the view. (Text v1: the native card renders plain text.)
  footnoteHandler.addEventListener('render', e => {
    const view = e.detail?.view
    const target = e.detail?.target
    let text = ''
    try {
      const contents = view?.renderer?.getContents?.() || []
      text = contents.map(c => (c?.doc?.body?.textContent || '')).join('\n').trim()
    } catch (err) { /* fall through to target */ }
    if (!text && target) { try { text = String(target.textContent || '').trim() } catch (e) {} }
    if (text.length > FOOTNOTE_TEXT_CAP) text = text.slice(0, FOOTNOTE_TEXT_CAP) + '…'
    emit('footnote', { html: text, rect: pendingFootnoteRect })
    // Do NOT remove the throwaway view here: it still has a pending iframe 'load' handler
    // (paginator getDirection → getComputedStyle) that would throw on a null doc if we detach
    // mid-load. The next footnote's before-render replaceChildren()s it out of the hidden host,
    // so at most ONE inert footnote view ever lingers (off-screen, not on our event seam).
  })
}

// ---------------------------------------------------------------------------
// in-page keyboard (OLD-READER MODEL) — the web view owns keys; we handle the
// nav/Esc set here and emit semantic events UP. foliate renders each section in an
// IFRAME, so a keydown handler on the TOP document alone misses keys while the iframe
// has focus — we attach to BOTH the top document (once) and each section's iframe doc
// (in the view 'load' handler, mirroring engine_foliate.js bindDocEvents). Reading stays
// immersive: keys NEVER emit a reveal/chrome-wake event (matches the reveal doctrine —
// the naked surface only wakes on a deliberate edge-reach / double-click).
// ---------------------------------------------------------------------------
const handleKeydown = e => {
  // Never hijack modified chords (Ctrl/Alt/Meta) or typing — the book has no text inputs,
  // but stay defensive: only the bare nav/Esc keys below are ours; everything else falls through.
  if (e.ctrlKey || e.metaKey || e.altKey) return
  const t = e.target
  const tag = (t && t.tagName) ? String(t.tagName).toLowerCase() : ''
  if (tag === 'input' || tag === 'textarea' || tag === 'select' || (t && t.isContentEditable)) return

  switch (e.key) {
    case 'ArrowLeft':
    case 'PageUp':
      currentView?.renderer?.prev()
      e.preventDefault()
      break
    case 'ArrowRight':
    case 'PageDown':
    case ' ':                     // Space
      currentView?.renderer?.next()
      e.preventDefault()
      break
    case 'Escape':
      emit('escape', {})
      e.preventDefault()
      break
    default:
      // not one of ours — leave native behavior alone.
  }
}

let topKeysAttached = false
const ensureTopKeys = () => {
  if (topKeysAttached) return          // the top-document listener is attached exactly once
  topKeysAttached = true
  document.addEventListener('keydown', handleKeydown)
}

const attachSelection = (view, doc, index) => {
  // In-page keyboard for THIS section's iframe (keys while the book text has focus) plus
  // the one-time top-document listener (keys before any click / when the page body has focus).
  ensureTopKeys()
  doc.addEventListener('keydown', handleKeydown)

  let hadSelection = false             // track the non-empty -> empty transition for selectionCleared
  const tryEmit = () => {
    const sel = doc.getSelection && doc.getSelection()
    const range = (sel && sel.rangeCount) ? sel.getRangeAt(0) : null
    // range.toString() is the reliable source: sel.toString() can be empty for
    // programmatic / unfocused selections (book.js guards the same way).
    const text = (range && !range.collapsed) ? (range.toString() || sel.toString()).trim() : ''
    if (!text) {
      // Selection went away (clicked elsewhere, a key turned the page, etc.). Emit ONCE on
      // the non-empty -> empty edge so QML can dismiss the SelectionMenu popover.
      if (hadSelection) { hadSelection = false; emit('selectionCleared', {}) }
      return
    }
    hadSelection = true
    let cfi = ''
    try { cfi = view.getCFI(index, range) } catch (e) { /* boundary ranges */ }
    emit('selection', { text, cfi, rect: clientRectOf(range, doc) })
  }
  doc.addEventListener('pointerup', () => setTimeout(tryEmit, 0))
  let debounce
  doc.addEventListener('selectionchange', () => {
    clearTimeout(debounce)
    debounce = setTimeout(tryEmit, 250)
  })

  // Double-click reveal (the POINTER REWORK): QML no longer covers the paper with a
  // full-fill toggle MouseArea (it ate every press → text selection was impossible), so
  // the "double-click toggles the chrome" affordance moves HERE, on the book iframe doc.
  //   • double-click on TEXT → the browser selects a word → the selection path above
  //     already emits 'selection' (which opens the menu); we do NOT toggle the chrome.
  //   • double-click on EMPTY space (a margin / whitespace, no word selected) → emit
  //     'toggleChrome'; ReaderShell routes that to chrome.toggle() — the same reveal
  //     Hemanth ratified, minus the selection-blocking overlay.
  doc.addEventListener('dblclick', () => {
    let hasText = false
    try {
      const sel = doc.getSelection && doc.getSelection()
      if (sel && sel.rangeCount) {
        const range = sel.getRangeAt(0)
        hasText = !!range && !range.collapsed && !!(range.toString() || sel.toString() || '').trim()
      }
    } catch (e) { /* treat as empty → toggle */ }
    if (!hasText) emit('toggleChrome', {})
  })
}

const wireView = view => {
  view.addEventListener('relocate', e => {
    // Suppress any relocate that fires BEFORE we've emitted 'ready' for this book
    // (the chrome doesn't know the book identity/toc yet — a pre-ready relocated is
    // out of order and the resume seam would mis-save on it). init()'s first relocate
    // lands AFTER ready (see paperOpen ordering), so real positions still flow.
    if (!readyEmitted) return
    const d = e.detail || {}
    // The engine's live per-page fraction is (page-1)/(pages-2); it degenerates to NaN
    // when a section is a single page (pages===2). Prefer it when finite, else fall back
    // to the section's start fraction so progress stays monotonic and well-formed.
    let fraction = d.fraction
    if (!Number.isFinite(fraction)) {
      const secFractions = view.getSectionFractions?.() || []
      const secIndex = d.section?.current
      const f = secFractions?.[secIndex]?.fraction
      fraction = Number.isFinite(f) ? f : 0
    }
    const finite = (n, fb = 0) => (Number.isFinite(n) ? n : fb)
    emit('relocated', {
      cfi: d.cfi ?? '',
      fraction,
      tocIndex: tocIndexByHref(d.tocItem?.href),
      chapterTitle: d.tocItem?.label ?? '',
      pageInChapter: finite(d.chapterLocation?.current),
      pagesInChapter: finite(d.chapterLocation?.total),
      percent: Math.round(fraction * 100),
      textPage: sectionHasText,          // false on a cover/full-image page → ruler stays hidden
    })
  })

  view.addEventListener('load', e => {
    const { doc, index } = e.detail || {}
    if (doc) attachSelection(view, doc, index)
    // Is this section real prose or a cover/full-image page? Cheap heuristic: how much
    // visible text the body carries. Covers/title-image pages have ~none; chapters have lots.
    // Reported on the next relocate so the reading ruler skips non-text pages.
    const txt = (doc && doc.body ? (doc.body.textContent || '') : '').replace(/\s+/g, ' ').trim()
    sectionHasText = txt.length >= 200
  })

  // Highlight/underline rendering — the view asks US to draw (matches book.js Reader.setView).
  // Draw ONLY for the types we know: 'highlight' → wash, 'underline' → rule; anything else
  // is a no-op (Task 2 carry-forward a). The previous default-to-highlight would mis-paint a
  // future non-highlight annotation type as a highlight — mirror book.js's exact-match handler.
  view.addEventListener('draw-annotation', e => {
    const { draw, annotation } = e.detail || {}
    if (!draw) return
    const opts = { color: annotation?.color, writingMode: view.renderer?.writingMode }
    if (annotation?.type === 'highlight') draw(Overlayer.highlight, opts)
    else if (annotation?.type === 'underline') draw(Overlayer.underline, opts)
    // else: unknown annotation type → don't draw (no mis-render).
  })

  // A (re)loaded section rebuilds its overlay — re-attach our annotations.
  view.addEventListener('create-overlay', () => reAddAnnotations())

  view.addEventListener('show-annotation', e => {
    const { value, range } = e.detail || {}
    // GUARD (Task 2 carry-forward b): a click that BOTH ends a text selection AND lands on
    // an existing highlight would otherwise fire 'selection' AND 'highlightTapped' — dueling
    // popovers (the delete menu opening over a fresh selection). If there's a live selection,
    // bail: the selection path owns this click. Check the note's own document first (the
    // selection lives in the section iframe), then the top window (book.js mirrors this).
    const selDoc = (range && range.startContainer) ? range.startContainer.ownerDocument : null
    const selText = (selDoc && selDoc.getSelection && selDoc.getSelection().toString())
                 || (window.getSelection && window.getSelection().toString()) || ''
    if (selText) return
    const found = [...annotations.values()].find(a => a.value === value)
    const rect = range ? clientRectOf(range, range.startContainer?.ownerDocument) : null
    emit('highlightTapped', { id: found?.id ?? null, rect })
  })

  // Footnote/endnote link taps (TASK 9 R2). The view emits 'link' (cancelable) for every
  // internal <a href> click. Hand it to the fork's FootnoteHandler: for a footnote/noteref
  // it calls preventDefault() (so the view does NOT navigate) and renders the note fragment,
  // which our render listener above turns into a 'footnote' event. For a NORMAL internal
  // link handle() returns undefined and does not preventDefault, so the view goToes as usual.
  view.addEventListener('link', e => {
    // Capture the tapped anchor's rect NOW — the render (and thus our emit) is async.
    pendingFootnoteRect = null
    try {
      const a = e.detail && e.detail.a
      if (a && a.ownerDocument) {
        const rng = a.ownerDocument.createRange()
        rng.selectNode(a)
        pendingFootnoteRect = clientRectOf(rng, a.ownerDocument)
      }
    } catch (err) { /* rect stays null → the card clamps to frame center */ }
    try {
      const p = footnoteHandler && footnoteHandler.handle(view.book, e)
      if (p && typeof p.catch === 'function')
        p.catch(err => { console.warn('[paper] footnote render failed', err) })
    } catch (err) { console.warn('[paper] footnote handle threw', err) }
  })
}

// ---------------------------------------------------------------------------
// commands DOWN — window.paper.*
// ---------------------------------------------------------------------------
const paperOpen = async (path, cfi) => {
  try {
    // Teardown the PREVIOUS book first (open A -> Esc -> open B): without this the
    // old <foliate-view> stays in the DOM and its relocate/selection listeners keep
    // firing stale events into our seam. close() destroys+removes the renderer and
    // its book iframes (whose docs carry the pointerup/selectionchange listeners) and
    // clears view internal state; remove() drops the <foliate-view> element itself.
    // annotations/flatToc/readyEmitted belong to the previous book — reset them.
    readyEmitted = false
    if (currentView) {
      try { currentView.close?.() } catch (e) { /* vendor teardown best-effort */ }
      try { currentView.remove() } catch (e) { /* already detached */ }
      currentView = null
    }
    annotations.clear()
    flatToc = []

    if (!window.bridge || typeof window.bridge.filesRead !== 'function')
      throw new Error('bridge.filesRead is not available')
    // Timeout guard: the native callback normally fires within a few ms, but if the
    // C++ seam never answers (bridge torn down mid-open, wedged NAM, etc.) the await
    // would hang paperOpen silently. Reject after 8s so the catch below emits an
    // 'error' event the chrome can surface, instead of a dead reader.
    const b64 = await new Promise((resolve, reject) => {
      let settled = false
      const timer = setTimeout(() => {
        if (settled) return
        settled = true
        reject(new Error('filesRead timed out (8s) for ' + path))
      }, 8000)
      try {
        window.bridge.filesRead(path, (data) => {
          if (settled) return
          settled = true
          clearTimeout(timer)
          resolve(data)
        })
      } catch (e) {
        if (settled) return
        settled = true
        clearTimeout(timer)
        reject(e)
      }
    })
    if (!b64) throw new Error('no book bytes for ' + path)

    syncVendorParams()                            // satisfy epub.js Loader's URL 'style' read
    const name = String(path).split(/[\\/]/).pop() || String(path)
    const file = base64ToFile(b64, name)
    const book = await makeBook(file)

    const view = document.createElement('foliate-view')
    document.body.append(view)
    currentView = view
    wireView(view)

    await view.open(book)
    flatToc = flattenToc(view.book?.toc)
    applyAppearance()                             // set flow/margins/bg/styles before first paint

    // Attach a REAL start `fraction` to each toc entry from the fork's per-section
    // fractions (getSectionFractions() is available right after open() — SectionProgress
    // is built there, before init()). We resolve each toc href to its spine-section index
    // and read that section's start fraction. This makes railTicks emit TRUE chapter
    // marks (it already prefers t.fraction) instead of even spacing, and keeps the
    // Contents current-row logic index-based. Best-effort + guarded: if a book can't map
    // a href (or exposes no section fractions), we simply omit `fraction` for that entry
    // and railTicks falls back to even spacing — never throw out of 'ready'.
    let secFractions = []
    try { secFractions = view.getSectionFractions?.() || [] } catch (e) { secFractions = [] }
    const tocForReady = flatToc.map(t => {
      const entry = { index: t.index, label: t.label, href: t.href }
      try {
        const resolved = view.book?.resolveHref?.(t.href)
        const si = (resolved && Number.isFinite(resolved.index)) ? resolved.index : -1
        const f = si >= 0 ? secFractions[si]?.fraction : undefined
        if (Number.isFinite(f)) entry.fraction = f
      } catch (e) { /* leave fraction off → even-spacing fallback in railTicks */ }
      return entry
    })

    // Emit 'ready' BEFORE init(). init() fires the book's FIRST relocate; the resume
    // seam saves progress on 'relocated', so the chrome must already know the book
    // identity/toc (which 'ready' carries) when that first relocated lands. ready's
    // payload is fully available here — right after open() + flattenToc. Setting
    // readyEmitted lets the relocate handler (gated above) start emitting from init on.
    emit('ready', {
      toc: tocForReady,
      metadata: view.book?.metadata ?? {},
      sections: view.book?.sections?.length ?? 0,
    })
    readyEmitted = true

    if (cfi) await view.init({ lastLocation: cfi })
    else await view.init({})                      // pushState(0) + first page (single advance)
  } catch (e) {
    console.error('[paper] open failed', e)
    emit('error', { message: String(e?.message || e) })
  }
}

// Page turns: fire-and-forget on the renderer, animation disabled -> cannot hang the paint loop.
const paperNext = () => { currentView?.renderer?.next() }
const paperPrev = () => { currentView?.renderer?.prev() }

const paperGoTo = target => {
  if (!currentView) return
  let t = target
  try { t = JSON.parse(target) } catch (e) { /* plain cfi/href string */ }
  if (typeof t === 'number') currentView.goToFraction(t)   // fraction 0..1
  else currentView.goTo(t)                                 // cfi or href
}

const paperSetAppearance = json => {
  try {
    const a = JSON.parse(json)
    appearance = {
      ...appearance, ...a,
      theme: { ...appearance.theme, ...(a?.theme || {}) },
    }
  } catch (e) { console.warn('[paper] bad appearance json', e) }
  applyAppearance()
}

// Cap the number of hits we collect + ship to the UI thread. A common word ("the")
// matches THOUSANDS of times; collecting them all builds a giant JSON that is parsed on
// the GUI thread (a stall) and floods a list nobody scrolls to the end of. So we stop at
// SEARCH_RESULT_CAP and set `capped` — and, crucially, BREAK out of the async generator so
// the fork stops scanning the rest of the book (the generator's return() tears it down).
const SEARCH_RESULT_CAP = 300

const paperSearch = async query => {
  if (!currentView) return
  const q = String(query || '').trim()
  if (!q) return
  const results = []
  let capped = false
  try {
    for await (const r of currentView.search({ query: q, index: null })) {
      if (r === 'done') break
      if (!r || typeof r !== 'object') continue
      if ('progress' in r) continue
      if (r.subitems) {
        for (const s of r.subitems) {
          if (results.length >= SEARCH_RESULT_CAP) { capped = true; break }
          results.push({ cfi: s.cfi, excerpt: s.excerpt ?? '', chapterTitle: r.label ?? '' })
        }
      } else if (r.cfi) {
        results.push({ cfi: r.cfi, excerpt: r.excerpt ?? '', chapterTitle: '' })
      }
      // At/over the cap → stop iterating the generator (don't scan the whole book).
      if (results.length >= SEARCH_RESULT_CAP) { capped = true; break }
    }
    emit('searchResults', { query: q, results, count: results.length, capped, done: true })
  } catch (e) {
    emit('error', { message: 'search failed: ' + String(e?.message || e) })
  }
}

const paperClearSearch = () => { currentView?.clearSearch?.() }

const paperAddHighlight = json => {
  try {
    const { id, cfi, color } = JSON.parse(json)
    const ann = { id, value: cfi, type: 'highlight', color: color || '#ffd70066' }
    annotations.set(id, ann)
    currentView?.addAnnotation(ann)
  } catch (e) { emit('error', { message: 'addHighlight failed: ' + String(e?.message || e) }) }
}

const paperRemoveHighlight = id => {
  const ann = annotations.get(id)
  if (!ann) return
  currentView?.deleteAnnotation(ann)
  annotations.delete(id)
}

const paperClearSelection = () => { currentView?.deselect?.() }

// ---------------------------------------------------------------------------
// bridge readiness — QWebChannel's handshake is async, so window.bridge may not
// exist the instant the glue finishes loading. Wait for it (bounded) before we
// announce glueLoaded, so the C++ seam actually receives the event (and every
// later emit has a live bridge). In the browser bench window.bridge is defined
// synchronously by mock_bridge.js, so this resolves immediately.
// ---------------------------------------------------------------------------
const bridgeReady = () =>
  !!(window.bridge && typeof window.bridge.paperEvent === 'function')

const waitForBridge = (timeoutMs = 5000) => new Promise((resolve) => {
  if (bridgeReady()) return resolve(true)
  const start = Date.now()
  const iv = setInterval(() => {
    if (bridgeReady()) { clearInterval(iv); resolve(true) }
    else if (Date.now() - start > timeoutMs) { clearInterval(iv); resolve(false) }
  }, 25)
})

// ---------------------------------------------------------------------------
// boot — load the vendor entry modules by DYNAMIC import (file:// safe), then
// publish window.paper and announce glueLoaded. Ordering matters: view.js must
// register <foliate-view> and Overlayer must be assigned BEFORE any book opens,
// and window.paper must exist BEFORE glueLoaded so the shell's open-on-glue works.
// ---------------------------------------------------------------------------
const boot = async () => {
  const mod = rel => new URL(rel, window.location.href).toString()
  await import(mod('./vendor/foliate-anx/src/view.js'))          // registers <foliate-view>
  const ov = await import(mod('./vendor/foliate-anx/src/overlayer.js'))
  Overlayer = ov.Overlayer
  // footnotes: the fork's FootnoteHandler (no self-boot, imports nothing DOM-bound) — the
  // proven note-extraction path. Set up one handler for the page lifetime (its listeners
  // fan out per book via handle(view.book, e)).
  try {
    const fn = await import(mod('./vendor/foliate-anx/src/footnotes.js'))
    FootnoteHandler = fn.FootnoteHandler
    setupFootnoteHandler()
  } catch (e) { console.warn('[paper] footnotes.js load failed (footnotes disabled)', e) }

  // The vendored view.js #handleClick calls window.isFootNoteOpen()/closeFootNote() on every
  // click in the book — globals that the fork's OWN reader (book.js, which we do NOT use)
  // defines. We render footnotes in native QML, so provide inert stubs; without them every tap
  // throws a console error and the handler bails before its (unused-by-us) click-view emit.
  if (typeof window.isFootNoteOpen !== 'function') window.isFootNoteOpen = () => false
  if (typeof window.closeFootNote !== 'function') window.closeFootNote = () => {}

  window.paper = {
    open: (path, cfi) => paperOpen(path, cfi ?? ''),
    next: paperNext,
    prev: paperPrev,
    goTo: paperGoTo,
    setAppearance: paperSetAppearance,
    search: paperSearch,
    clearSearch: paperClearSearch,
    addHighlight: paperAddHighlight,
    removeHighlight: paperRemoveHighlight,
    clearSelection: paperClearSelection,
  }

  await waitForBridge()
  emit('glueLoaded', {})
}

boot().catch(e => {
  console.error('[paper] boot failed', e)
  emit('error', { message: 'boot failed: ' + String(e?.message || e) })
})
