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

import './vendor/foliate-anx/src/view.js'                 // registers <foliate-view>
import { Overlayer } from './vendor/foliate-anx/src/overlayer.js'

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

const attachSelection = (view, doc, index) => {
  const tryEmit = () => {
    const sel = doc.getSelection && doc.getSelection()
    if (!sel || !sel.rangeCount) return
    const range = sel.getRangeAt(0)
    if (!range || range.collapsed) return
    // range.toString() is the reliable source: sel.toString() can be empty for
    // programmatic / unfocused selections (book.js guards the same way).
    const text = (range.toString() || sel.toString()).trim()
    if (!text) return
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
}

const wireView = view => {
  view.addEventListener('relocate', e => {
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
    })
  })

  view.addEventListener('load', e => {
    const { doc, index } = e.detail || {}
    if (doc) attachSelection(view, doc, index)
  })

  // Highlight/underline rendering — the view asks US to draw (matches book.js Reader.setView).
  view.addEventListener('draw-annotation', e => {
    const { draw, annotation } = e.detail || {}
    if (!draw) return
    const opts = { color: annotation?.color, writingMode: view.renderer?.writingMode }
    if (annotation?.type === 'underline') draw(Overlayer.underline, opts)
    else draw(Overlayer.highlight, opts)
  })

  // A (re)loaded section rebuilds its overlay — re-attach our annotations.
  view.addEventListener('create-overlay', () => reAddAnnotations())

  view.addEventListener('show-annotation', e => {
    const { value, range } = e.detail || {}
    const found = [...annotations.values()].find(a => a.value === value)
    const rect = range ? clientRectOf(range, range.startContainer?.ownerDocument) : null
    emit('highlightTapped', { id: found?.id ?? null, rect })
  })
}

// ---------------------------------------------------------------------------
// commands DOWN — window.paper.*
// ---------------------------------------------------------------------------
const paperOpen = async (path, cfi) => {
  try {
    if (!window.bridge || typeof window.bridge.filesRead !== 'function')
      throw new Error('bridge.filesRead is not available')
    const b64 = await new Promise((resolve) => window.bridge.filesRead(path, resolve))
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

    if (cfi) await view.init({ lastLocation: cfi })
    else await view.init({})                      // pushState(0) + first page (single advance)

    emit('ready', {
      toc: flatToc.map(t => ({ index: t.index, label: t.label, href: t.href })),
      metadata: view.book?.metadata ?? {},
      sections: view.book?.sections?.length ?? 0,
    })
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

const paperSearch = async query => {
  if (!currentView) return
  const q = String(query || '').trim()
  if (!q) return
  const results = []
  try {
    for await (const r of currentView.search({ query: q, index: null })) {
      if (r === 'done') break
      if (!r || typeof r !== 'object') continue
      if ('progress' in r) continue
      if (r.subitems) {
        for (const s of r.subitems)
          results.push({ cfi: s.cfi, excerpt: s.excerpt ?? '', chapterTitle: r.label ?? '' })
      } else if (r.cfi) {
        results.push({ cfi: r.cfi, excerpt: r.excerpt ?? '', chapterTitle: '' })
      }
    }
    emit('searchResults', { query: q, results, done: true })
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

emit('glueLoaded', {})
