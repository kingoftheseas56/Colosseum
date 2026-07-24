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
// Footnote tap captures, keyed by PER-REQUEST TOKEN (re-review #4 fix, Codex-recommended):
// href alone couldn't distinguish two rapid taps on anchors sharing one note, and FIFO
// pairing broke under out-of-order completion / rejection. The vendored footnotes.js now
// carries an opaque `token` from the link event's detail through to the 'render' detail
// ([Colosseum patch] — additive, upstream-identical without one), so every render consumes
// EXACTLY its own tap's capture and a rejection deletes exactly its own token. A zombie
// chain that never renders (admission-dropped: its unattached view's iframe never fires
// 'load', so the vendor chain never reaches 'render') leaks one entry until the per-open
// clear — bounded and inert (a token is never reused).
const footnoteTaps = new Map()  // token → {gen, rect} (rect may be null)
let footnoteTapSeq = 0          // token source — unique per tap for the page lifetime

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
// Extension checks are CASE-INSENSITIVE: a `.CBZ` / `.FB2` / `.FBZ` (uppercase off some
// exporters/OSes) must match too. Lowercase the name before every .endsWith (the isTXT
// regex already uses /i). The magic-byte branches (isZip/isPDF) are case-agnostic already.
const isCBZ = ({ name, type }) =>
  type === 'application/vnd.comicbook+zip' || String(name || '').toLowerCase().endsWith('.cbz')
const isFB2 = ({ name, type }) =>
  type === 'application/x-fictionbook+xml' || String(name || '').toLowerCase().endsWith('.fb2')
const isFBZ = ({ name, type }) => {
  const n = String(name || '').toLowerCase()
  return type === 'application/x-zip-compressed-fb2'
    || n.endsWith('.fb2.zip') || n.endsWith('.fbz')
}
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
  fontWeight: appearance.fontWeight ?? 400,
  letterSpacing: appearance.letterSpacing ?? 0,
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
      // Case-insensitive inner lookup: an FBZ packed with `BOOK.FB2` (uppercase off some
      // exporters) must still find its FictionBook entry — same rule as the outer
      // extension checks above (ratified constraint: extension matching is case-blind).
      const entry = entries.find(e => e.filename.toLowerCase().endsWith('.fb2'))
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
// Per-open generation (cross-book stale-event guard). QML-ISSUED: ReaderShell passes the gen
// it will wait for into every paperOpen and we echo it on every book-scoped emit ('ready'/
// 'relocated'/'error'/'searchResults'/'footnote'/'selection'/'highlightTapped'); the bench
// (no QML) falls back to self-incrementing. An event from book A already in flight over
// QWebChannel when we switched to book B then carries A's gen and is dropped shell-side.
// (See ReaderShell + L.acceptReady / L.acceptBookEvent.)
let openGen = 0
let footnoteRenderPending = false      // one throwaway footnote render host loads at a time (see setupFootnoteHandler)
let sectionHasText = true              // is the CURRENT section real prose (vs a cover/full-image page)?
                                       // reported as relocated.textPage so the reading ruler only dims
                                       // TEXT pages — a focus band over a cover image is a bug, not an aid.

// read-along (Task 4) — the paper's presentation seam for audiobook↔EPUB alignment.
// The pure paint/resolve state machine lives in alignment_text.js; these hold its wiring.
let AT = null                          // the alignment_text.js module (dynamic-imported in boot)
let readAlong = null                   // the single createReadAlongPainter instance
// Programmatic-navigation tag: set (depth-counted) around view moves WE initiate (init,
// navigateReadAlong, ensureReadAlongVisible) so the relocate handler can tell a reader-
// initiated move (wheel/drag/page-turn/TOC/search — emits manualNavigation) from a
// programmatic one (emits nothing). Cleared on the NEXT tick so a relocate dispatched just
// after the move still counts programmatic. (Depth-counted for overlapping moves.)
let programmaticNav = false
let programmaticDepth = 0

// appearance state + defaults (dark paper)
let appearance = {
  theme: { bg: '#000000', fg: '#e6e1d5' },
  font: 'book',      // 'book' = publisher font, 'system', or a family name
  sizePx: 18,
  fontWeight: 400,
  lineHeight: 1.5,
  marginPx: 48,
  justify: false,
  flow: 'paginated', // 'paginated' | 'scrolled' (2026-07-20 — vertical-scroll reading)
  // PARITY (2026-07-24) — neutral values == pre-parity behavior:
  wordSpacing: 0, letterSpacing: 0, paraSpacing: 0, paraIndent: 'book',
  maxLineWidthPx: 960, hyphens: false, columns: 'single',
  customCss: '', invertImages: true, isDark: true,
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
  // PARITY (2026-07-24): body-text selectors for the typography dials. Headings and
  // b/strong are deliberately NOT forced — publisher emphasis survives the dials.
  const textSel = 'p, li, blockquote, dd'
  const w = appearance.fontWeight ?? 400
  const weightCss = w === 400 ? '' : `${textSel} { font-weight: ${w} !important; }`
  const ws = appearance.wordSpacing ?? 0
  const wordCss = ws > 0 ? `${textSel} { word-spacing: ${ws}rem !important; }` : ''
  const ls = appearance.letterSpacing ?? 0
  const letterCss = ls > 0 ? `${textSel} { letter-spacing: ${ls}rem !important; }` : ''
  const ps = appearance.paraSpacing ?? 0
  const paraCss = ps > 0 ? `p { margin-bottom: ${ps}rem !important; }` : ''
  const indentCss = appearance.paraIndent === 'none' ? 'p { text-indent: 0 !important; }'
    : appearance.paraIndent === 'indent' ? 'p { text-indent: 1.5em !important; }' : ''
  const hyphCss = appearance.hyphens
    ? `${textSel} { hyphens: auto !important; -webkit-hyphens: auto !important; }` : ''
  const invertCss = (appearance.isDark && appearance.invertImages)
    ? 'img, svg, image { filter: invert(1) hue-rotate(180deg) !important; }' : ''
  return `
    @namespace epub "http://www.idpf.org/2007/ops";
    ${FONT_FACE_CSS}
    html { color: ${fg} !important; background-color: transparent !important; font-size: ${size}px !important; }
    body { background: none !important; padding: 0; }
    /* READABLE MEASURE (2026-07-20, the Wool full-bleed finding): with max-column-count 1
       the paginator sizes its one column to the whole viewport and ignores max-inline-size,
       so an unconstrained body renders 1700px+ lines — and justify across a line that long
       shreds word spacing. Centered max-width column is the OLD reader's ratified pattern.
       PARITY (2026-07-24): the value is now the user's Max line width dial (default 960 ==
       the old hardcoded clamp). PAGE MODE + SINGLE COLUMN ONLY — scrolled stays
       edge-to-edge (Hemanth's 2026-07-20 ruling), and in spread mode the two columns
       define the measure themselves. */
    ${(appearance.flow === 'scrolled' || appearance.columns === 'spread') ? '' :
      `body { max-width: ${appearance.maxLineWidthPx ?? 960}px !important;
           margin-left: auto !important; margin-right: auto !important;
           box-sizing: border-box !important; }`}
    p, li, blockquote, dd, div, font { color: ${fg} !important; line-height: ${lh} !important; text-align: ${align}; }
    a, a:link { color: #a76034 !important; }
    ${fontCss}
    ${weightCss}
    ${wordCss}
    ${letterCss}
    ${paraCss}
    ${indentCss}
    ${hyphCss}
    ${invertCss}
    /* user custom CSS — LAST so it wins over every dial (Reader 1's contract) */
    ${appearance.customCss || ''}
  `
}

// ---------------------------------------------------------------------------
// spacer-paragraph collapse (2026-07-20 — the Wool Omnibus finding). Sloppy Calibre
// conversions separate EVERY real paragraph with a whitespace-only shim (<p>&nbsp;</p>,
// styled height:1em by book CSS we deliberately discard) — rendered, each shim costs a
// full line box and the page turns skeletal. Rule: if a meaningful share of a section's
// paragraphs are whitespace-only, they're conversion shims — hide them (display:none
// keeps the DOM intact, so CFIs/annotations/search stay valid). A normal book's rare
// blank paragraph (scene break) stays: it never crosses the share threshold.
// ---------------------------------------------------------------------------
const collapseSpacerParagraphs = doc => {
  try {
    const ps = doc.body ? Array.from(doc.body.querySelectorAll('p')) : []
    if (ps.length < 8) return                        // tiny sections: not enough signal
    const isShim = el => !el.querySelector('img, svg, image') &&
                         String(el.textContent || '').replace(/[\s   ]/g, '') === ''
    const shims = ps.filter(isShim)
    if (shims.length < ps.length * 0.25) return      // scene-break territory — leave the book alone
    for (const s of shims) s.style.setProperty('display', 'none', 'important')
  } catch (e) { console.warn('[paper] spacer collapse skipped', e) }
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
  r.setAttribute('flow', appearance.flow === 'scrolled' ? 'scrolled' : 'paginated')
  r.setAttribute('background-color', bg)
  r.setAttribute('top-margin', `${appearance.marginPx ?? 48}px`)
  r.setAttribute('bottom-margin', `${appearance.marginPx ?? 48}px`)
  const gapPct = Math.max(2, Math.min(18,
    Math.round(((appearance.marginPx ?? 48) / (window.innerWidth || 1000)) * 100)))
  r.setAttribute('gap', `${gapPct}%`)
  r.setAttribute('max-column-count',
    (appearance.columns === 'spread' && appearance.flow !== 'scrolled') ? '2' : '1')
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
  //
  // RAPID DOUBLE-TAP GUARD: only ONE render host loads at a time. A second footnote tap
  // whose before-render arrives while the first is still loading is DROPPED (return without
  // attaching) — replaceChildren()-ing a second view in would detach the first's still-
  // loading iframe mid-flight, and its pending paginator load handler (getDirection →
  // getComputedStyle) throws on the now-null doc. The first render wins and emits its card;
  // the flag clears on 'render' (below) or on the handle() promise reject (link handler).
  footnoteHandler.addEventListener('before-render', e => {
    const view = e.detail?.view
    if (!view) return
    if (footnoteRenderPending) return          // a prior footnote is still rendering — drop this one
    footnoteRenderPending = true
    try {
      footnoteHost().replaceChildren(view)
      const r = view.renderer
      if (r) {
        r.setAttribute('flow', 'scrolled')
        r.setAttribute('gap', '5%')
        r.setAttribute('top-margin', '0px')
        r.setAttribute('bottom-margin', '0px')
      }
    } catch (err) { footnoteRenderPending = false; console.warn('[paper] footnote before-render', err) }
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
    // Emit THIS request's own capture: the render detail carries back the exact token our
    // link handler stamped ([Colosseum patch] in vendor footnotes.js), so correlation is
    // per-request — same-href taps, out-of-order completion, and rejections cannot mispair.
    // NO capture → NO emit: a render with no recorded tap is a superseded book's straggler
    // (paperOpen clears the map on switch) — inventing a gen for it would relabel the OLD
    // book's note as the new book's and leak it past the shell's gate.
    const token = e.detail?.token
    const tap = (token !== undefined) ? footnoteTaps.get(token) : undefined
    if (token !== undefined) footnoteTaps.delete(token)   // consumed (or stale — either way done)
    if (tap) emit('footnote', { gen: tap.gen, html: text, rect: tap.rect ?? null })
    footnoteRenderPending = false   // host free again → the next footnote tap can render
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

const attachSelection = (view, doc, index, gen) => {
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
      if (hadSelection) { hadSelection = false; emit('selectionCleared', { gen }) }
      return
    }
    hadSelection = true
    let cfi = ''
    try { cfi = view.getCFI(index, range) } catch (e) { /* boundary ranges */ }
    // gen-stamped (re-review #2): the selection timers (setTimeout/debounce) can outlive a book
    // switch — the shell gen-drops a stale selection instead of opening a popover over book B.
    emit('selection', { gen, text, cfi, rect: clientRectOf(range, doc) })
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
    let hasText = false, selRange = null
    try {
      const sel = doc.getSelection && doc.getSelection()
      if (sel && sel.rangeCount) {
        const range = sel.getRangeAt(0)
        hasText = !!range && !range.collapsed && !!(range.toString() || sel.toString() || '').trim()
        if (hasText) selRange = range
      }
    } catch (e) { /* treat as empty → toggle */ }
    // Read-along seek: a double-click that lands WITHIN a painted sentence/word emits
    // 'alignedDoubleClick' with the clicked word's canonical offset — IN ADDITION to the
    // 'selection' the word-select already produced (both are fine; the controller uses the
    // aligned event to seek). It never fires on empty space (there's no selection there), so
    // the toggleChrome reveal below is untouched.
    if (selRange && readAlong) {
      try {
        readAlong.handleDoubleClick({
          startNode: selRange.startContainer, startOffset: selRange.startOffset,
          endNode: selRange.endContainer, endOffset: selRange.endOffset,
        })
      } catch (e) { /* alignment optional — never break the reveal */ }
    }
    if (!hasText) emit('toggleChrome', {})
  })
}

const wireView = (view, gen) => {
  // Is the CURRENT section real prose (vs a cover / full-image page)? RECOMPUTE from the live
  // rendered doc at relocate time rather than trusting only the cached `sectionHasText` flag:
  // the flag is set in the 'load' handler, and a relocate can, on some timing, fire for a
  // section whose 'load' hasn't updated it yet (the flag would then describe the PREVIOUS
  // section → the ruler could dim a cover or skip a chapter). getContents() reflects what's
  // actually on screen now; fall back to the cached flag only if contents aren't readable.
  const currentSectionHasText = () => {
    try {
      const contents = view.renderer?.getContents?.() || []
      if (contents.length) {
        const txt = contents
          .map(c => (c?.doc?.body ? (c.doc.body.textContent || '') : ''))
          .join(' ').replace(/\s+/g, ' ').trim()
        return txt.length >= 200
      }
    } catch (e) { /* fall through to the cached flag */ }
    return sectionHasText
  }

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
      gen,                                       // THIS view's captured open-gen (closed over, not live)
      cfi: d.cfi ?? '',
      fraction,
      tocIndex: tocIndexByHref(d.tocItem?.href),
      chapterTitle: d.tocItem?.label ?? '',
      pageInChapter: finite(d.chapterLocation?.current),
      pagesInChapter: finite(d.chapterLocation?.total),
      percent: Math.round(fraction * 100),
      // A "text page" for the reading ruler = reflowable prose. FALSE for a whole
      // fixed-layout book (PDF/CBZ — every page is a fixed image/page) AND for a
      // reflowable book's cover/full-image section (little text). The ruler only dims
      // real text pages; a focus band over a PDF page or a cover reads as a bug. Recompute
      // from the live section (robust vs a relocate that races ahead of this section's 'load').
      textPage: !(currentView && currentView.isFixedLayout) && currentSectionHasText(),
    })

    // Read-along (Task 4): a READER-initiated relocate (wheel, drag, page turn, TOC/search/
    // bookmark/annotation jump) emits 'manualNavigation' so the controller detaches audio
    // follow. A move WE initiated (init / navigateReadAlong / ensureReadAlongVisible) is tagged
    // programmatic and stays silent — the return-to-narration path must not detach itself.
    if (!programmaticNav) emit('manualNavigation', { gen })
  })

  view.addEventListener('load', e => {
    const { doc, index } = e.detail || {}
    if (doc) attachSelection(view, doc, index, gen)   // gen: this view's captured open-gen
    if (doc) collapseSpacerParagraphs(doc)            // Calibre &nbsp;-shim cleanup (2026-07-20)
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
    emit('highlightTapped', { gen, id: found?.id ?? null, rect })   // gen-stamped (re-review #2)
  })

  // Footnote/endnote link taps (TASK 9 R2). The view emits 'link' (cancelable) for every
  // internal <a href> click. Hand it to the fork's FootnoteHandler: for a footnote/noteref
  // it calls preventDefault() (so the view does NOT navigate) and renders the note fragment,
  // which our render listener above turns into a 'footnote' event. For a NORMAL internal
  // link handle() returns undefined and does not preventDefault, so the view goToes as usual.
  view.addEventListener('link', e => {
    // Record this tap's anchor rect + open-gen under a fresh per-request TOKEN, and stamp the
    // token onto the link detail — the patched vendor handler threads it through to the
    // 'render' detail, so the async render consumes exactly THIS tap's capture (see
    // footnoteTaps above). A book switch or a second tap mid-render can never relabel it.
    const tapToken = ++footnoteTapSeq
    let tapRect = null
    try {
      const a = e.detail && e.detail.a
      if (a && a.ownerDocument) {
        const rng = a.ownerDocument.createRange()
        rng.selectNode(a)
        tapRect = clientRectOf(rng, a.ownerDocument)
      }
    } catch (err) { /* rect stays null → the card clamps to frame center */ }
    if (e.detail) e.detail.token = tapToken            // ride the vendor chain ([Colosseum patch])
    footnoteTaps.set(tapToken, { gen, rect: tapRect }) // gen: this view's captured open-gen
    try {
      const p = footnoteHandler && footnoteHandler.handle(view.book, e)
      if (p && typeof p.catch === 'function') {
        // Clear the render-pending guard if the render fails (resolveHref/load reject),
        // so a failed footnote can never wedge every later footnote shut. Deleting by OUR
        // exact token can never evict another tap's capture.
        p.catch(err => {
          footnoteRenderPending = false
          footnoteTaps.delete(tapToken)   // no render coming for this tap
          console.warn('[paper] footnote render failed', err)
        })
      } else {
        // handle() returned nothing → a NORMAL internal link (the view navigates): no render
        // will ever consume this entry, so drop it now instead of letting it linger.
        footnoteTaps.delete(tapToken)
      }
    } catch (err) {
      footnoteRenderPending = false
      footnoteTaps.delete(tapToken)
      console.warn('[paper] footnote handle threw', err)
    }
  })
}

// ---------------------------------------------------------------------------
// read-along (Task 4) — non-destructive alignment painting + canonical navigation.
// The pure paint/resolve logic lives in alignment_text.js (createReadAlongPainter); these
// supply it the LIVE platform: on-screen foliate sections, a foliate Overlayer wash/emphasis
// layer per section, and word-clone geometry. No model, SQLite, matching, job, or playback
// authority ever enters here (design §"Reader2 paper bridge": QML paints, C++ decides).
// ---------------------------------------------------------------------------
const beginProgrammatic = () => { programmaticDepth++; programmaticNav = true }
const endProgrammatic = () => setTimeout(() => {
  programmaticDepth = Math.max(0, programmaticDepth - 1)
  if (programmaticDepth === 0) programmaticNav = false
}, 0)
const runProgrammatic = async fn => {
  beginProgrammatic()
  try { return await fn() } catch (e) { /* navigation is best-effort */ } finally { endProgrammatic() }
}

// A soft harbor wash for the active sentence; a stronger cast for the active word. Both are
// foliate Overlayer rects in a NON-INTERACTIVE (pointer-events:none) SVG layer, so they never
// steal the pointer from selection/link/footnote/highlight (which keep their exact behavior).
// Opacity is soft so the glyphs read through.
const READALONG_SENTENCE_COLOR = 'rgba(167, 96, 52, 0.20)'
const READALONG_WORD_COLOR = 'rgba(167, 96, 52, 0.45)'

// One dedicated Overlayer per section doc, SEPARATE from foliate's annotation overlay so
// clear() never disturbs a reader's highlights. Built lazily; the empty SVG lingering in the
// body after clear is inert (foliate leaves its own annotation SVG the same way).
const makeReadAlongOverlay = doc => {
  const layer = new Overlayer(doc)
  try { (doc.body || doc.documentElement).appendChild(layer.element) } catch (e) {}
  const toRange = rd => { const r = doc.createRange(); r.setStart(rd.startNode, rd.startOffset); r.setEnd(rd.endNode, rd.endOffset); return r }
  return {
    draw(kind, rd) {
      try {
        const color = kind === 'word' ? READALONG_WORD_COLOR : READALONG_SENTENCE_COLOR
        layer.add('readalong-' + kind, toRange(rd), Overlayer.highlight, { color })
      } catch (e) { /* boundary range — skip this frame */ }
    },
    clear() { try { layer.remove('readalong-sentence') } catch (e) {} try { layer.remove('readalong-word') } catch (e) {} },
  }
}

// The word's page-space rect (doc-relative, scroll-corrected) so the enlarged clone sits over
// the word without touching the text flow. Bench-tuned geometry; null on failure.
const measureReadAlong = (doc, rd) => {
  try {
    const range = doc.createRange()
    range.setStart(rd.startNode, rd.startOffset); range.setEnd(rd.endNode, rd.endOffset)
    const r = range.getBoundingClientRect()
    const win = doc.defaultView || {}
    return { left: r.left + (win.scrollX || 0), top: r.top + (win.scrollY || 0), width: r.width, height: r.height }
  } catch (e) { return null }
}

// The on-screen rendered sections, each tagged with its spine href (foliate's section id =
// the manifest-relative href). The painter matches a cue's spineHref against these.
const spineHrefForIndex = index => {
  try { const sec = currentView?.book?.sections?.[index]; return sec ? (sec.id || '') : '' }
  catch (e) { return '' }
}
const readAlongSections = () => {
  try {
    return (currentView?.renderer?.getContents?.() || [])
      .map(c => ({ doc: c.doc, spineHref: spineHrefForIndex(c.index) }))
      .filter(s => s.doc)
  } catch (e) { return [] }
}

const toDomRange = (doc, rd) => {
  const range = doc.createRange()
  range.setStart(rd.startNode, rd.startOffset); range.setEnd(rd.endNode, rd.endOffset)
  return range
}
const parseArg = json => { try { return JSON.parse(json) } catch (e) { return null } }

const paperSetReadAlongStyle = json => { readAlong?.setStyle(parseArg(json)) }
const paperPaintReadAlong = json => { const cue = parseArg(json); if (cue) readAlong?.paint(cue) }
const paperClearReadAlong = () => { readAlong?.clear() }

// ensureReadAlongVisible(location): bring a resolved canonical location into the comfort zone
// with a MINIMAL foliate scroll — programmatic, so no manualNavigation. No text mutation.
const paperEnsureReadAlongVisible = json => {
  const loc = parseArg(json)
  if (!loc || !readAlong || !currentView) return
  const hit = readAlong.resolveLocation(loc)
  if (!hit) return                                 // off-screen/unresolvable — the controller decides
  runProgrammatic(() => currentView.renderer?.scrollToAnchor?.(toDomRange(hit.doc, hit.range)))
}

// navigateReadAlong(location): programmatically move the view to a canonical location (the
// double-click-to-seek RETURN path). PROGRAMMATIC → must NOT emit manualNavigation. If the
// section is on screen we comfort-scroll to the range; otherwise we goTo the spine section and
// resolve the range in the freshly-rendered doc (foliate calls the anchor fn with the new doc).
const paperNavigateReadAlong = json => {
  const loc = parseArg(json)
  if (!loc || !currentView) return
  const hit = readAlong && readAlong.resolveLocation(loc)
  if (hit) {
    runProgrammatic(() => currentView.renderer?.scrollToAnchor?.(toDomRange(hit.doc, hit.range)))
    return
  }
  runProgrammatic(async () => {
    const resolved = currentView.book?.resolveHref?.(String(loc.spineHref || '').split('#')[0])
    if (resolved && Number.isFinite(resolved.index)) {
      await currentView.renderer?.goTo?.({
        index: resolved.index,
        anchor: docNew => {
          try {
            const rd = AT && AT.resolveCanonicalSpan(AT.canonicalWalk(docNew.body || docNew), loc.canonicalStart, loc.canonicalEnd)
            return rd ? toDomRange(docNew, rd) : 0
          } catch (e) { return 0 }
        },
      })
    } else {
      await currentView.goTo?.(String(loc.spineHref || '').split('#')[0])
    }
  })
}

// ---------------------------------------------------------------------------
// commands DOWN — window.paper.*
// ---------------------------------------------------------------------------
// Drop a <foliate-view> WE appended once a newer open superseded us: close+remove it, and
// null currentView ONLY if it still points at ours (a newer open may already own currentView,
// and we must never yank the newest book's view out from under it). Used at the two abort
// points that come after the view is mounted (post view.open, post view.init).
const removeSupersededView = view => {
  try { view.close?.() } catch (e) { /* vendor teardown best-effort */ }
  try { view.remove() } catch (e) { /* already detached */ }
  if (currentView === view) currentView = null
}

const paperOpen = async (path, cfi, gen) => {
  // annotations/flatToc/readyEmitted belong to the previous book — reset them (below, in try).
  readyEmitted = false
  // The open's generation is QML-ISSUED (re-review #2): ReaderShell hands us the gen it will
  // wait for and we ECHO it on every book-scoped emit — QML can then match 'ready' exactly to
  // the open it asked for (no adoption). QML's counter is monotonic, so superseded() below
  // keeps working. The +1 fallback covers the browser bench, which opens without a gen.
  openGen = Number.isFinite(gen) ? gen : openGen + 1
  // Capture THIS open's gen in a local. The relocate listener + the ready emit below close
  // over `myGen`, NOT the live module `openGen` — so a stale relocate from a PREVIOUS book
  // (fired after a later open already bumped openGen) carries the OLD book's gen and is
  // correctly dropped by ReaderShell. Reading openGen live would stamp it with the NEW gen
  // and defeat the guard (the exact race it exists to close). Declared OUTSIDE the try so the
  // catch's 'error' emit can gen-tag with `myGen` and superseded() can close over it.
  const myGen = openGen
  footnoteRenderPending = false   // a prior book's in-flight footnote render can't gate this one
  footnoteTaps.clear()            // and its recorded taps can't label a straggler render (no
                                  // entry → the render handler drops the emit entirely)
  // CANCEL-STALE-OPEN (the biggest fix): a newer paperOpen (book B) bumps openGen. After EVERY
  // await below we re-check superseded() — a slow open of book A (a big PDF) whose awaits
  // resolve AFTER B opened must NOT mount its view over B, emit a stale 'ready', or emit a
  // stale 'error'. When true we abort cleanly: tear down any view we appended and RETURN,
  // leaving currentView / state owned by the newest open only.
  const superseded = () => myGen !== openGen
  try {
    // Teardown the PREVIOUS book first (open A -> Esc -> open B): without this the
    // old <foliate-view> stays in the DOM and its relocate/selection listeners keep
    // firing stale events into our seam. close() destroys+removes the renderer and
    // its book iframes (whose docs carry the pointerup/selectionchange listeners) and
    // clears view internal state; remove() drops the <foliate-view> element itself.
    if (currentView) {
      try { currentView.close?.() } catch (e) { /* vendor teardown best-effort */ }
      try { currentView.remove() } catch (e) { /* already detached */ }
      currentView = null
    }
    annotations.clear()
    flatToc = []
    try { readAlong?.invalidate() } catch (e) {}   // a previous book's paint/caches can't cross over

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
    if (superseded()) return                      // a newer open replaced us during filesRead
    if (!b64) throw new Error('no book bytes for ' + path)

    syncVendorParams()                            // satisfy epub.js Loader's URL 'style' read
    const name = String(path).split(/[\\/]/).pop() || String(path)
    const file = base64ToFile(b64, name)
    const book = await makeBook(file)
    if (superseded()) return                      // a newer open replaced us during makeBook (big-PDF parse)

    const view = document.createElement('foliate-view')
    document.body.append(view)
    currentView = view
    wireView(view, myGen)

    await view.open(book)
    if (superseded()) { removeSupersededView(view); return }  // switched during view.open — never mount over B
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
      gen: myGen,                                 // this open's captured gen; ReaderShell sets currentGen from it
      toc: tocForReady,
      metadata: view.book?.metadata ?? {},
      sections: view.book?.sections?.length ?? 0,
    })
    readyEmitted = true

    // Wrap the initial position in a programmatic tag so its first relocate does NOT emit
    // 'manualNavigation' — the book opening isn't a reader-initiated navigation.
    beginProgrammatic()
    try {
      if (cfi) await view.init({ lastLocation: cfi })
      else await view.init({})                      // pushState(0) + first page (single advance)
    } finally { endProgrammatic() }
    if (superseded()) { removeSupersededView(view); return }  // switched during view.init
  } catch (e) {
    if (superseded()) return                       // a newer open owns the surface now — its errors, not ours
    console.error('[paper] open failed', e)
    emit('error', { gen: myGen, message: String(e?.message || e) })
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
  const searchGen = openGen        // the open this search belongs to (cross-book drop, mirrors relocate)
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
    if (searchGen !== openGen) return   // book switched mid-search → drop these (now stale) results
    emit('searchResults', { gen: searchGen, query: q, results, count: results.length, capped, done: true })
  } catch (e) {
    if (searchGen !== openGen) return   // superseded → don't surface a stale search error either
    emit('error', { gen: searchGen, message: 'search failed: ' + String(e?.message || e) })
  }
}

const paperClearSearch = () => { currentView?.clearSearch?.() }

const paperAddHighlight = json => {
  try {
    const { id, cfi, color } = JSON.parse(json)
    const ann = { id, value: cfi, type: 'highlight', color: color || '#ffd70066' }
    annotations.set(id, ann)
    currentView?.addAnnotation(ann)
  } catch (e) {
    // gen-stamped (re-review #3): an UNSTAMPED error delivered during a later book's pre-ready
    // window would classify as that book's failed open shell-side. This is a book-scoped
    // operational error of the CURRENT open — stamp it so errorDisposition routes it right.
    emit('error', { gen: openGen, message: 'addHighlight failed: ' + String(e?.message || e) })
  }
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
  // read-along (Task 4): the shared canonical module + the single paint state machine, wired
  // to the LIVE view (on-screen sections, a foliate Overlayer layer, and clone geometry).
  try {
    AT = await import(mod('./alignment_text.js'))
    readAlong = AT.createReadAlongPainter({
      sections: readAlongSections,
      makeOverlay: makeReadAlongOverlay,
      measure: measureReadAlong,
      emit,
      getGen: () => openGen,
    })
  } catch (e) { console.warn('[paper] read-along module load failed (alignment disabled)', e) }
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
    open: (path, cfi, gen) => paperOpen(path, cfi ?? '', gen),
    next: paperNext,
    prev: paperPrev,
    goTo: paperGoTo,
    setAppearance: paperSetAppearance,
    search: paperSearch,
    clearSearch: paperClearSearch,
    addHighlight: paperAddHighlight,
    removeHighlight: paperRemoveHighlight,
    clearSelection: paperClearSelection,
    // read-along (Task 4) — alignment presentation. Consume canonical locations only.
    setReadAlongStyle: paperSetReadAlongStyle,
    paintReadAlong: paperPaintReadAlong,
    clearReadAlong: paperClearReadAlong,
    ensureReadAlongVisible: paperEnsureReadAlongVisible,
    navigateReadAlong: paperNavigateReadAlong,
  }

  await waitForBridge()
  emit('glueLoaded', {})
}

boot().catch(e => {
  console.error('[paper] boot failed', e)
  emit('error', { message: 'boot failed: ' + String(e?.message || e) })
})
