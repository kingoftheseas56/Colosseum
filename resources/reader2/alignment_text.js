// alignment_text.js — the shared canonical-text rules for audiobook↔EPUB read-along.
//
// One small thing: turn the EPUB's *displayed* words into a stable canonical stream
// for matching, while remembering exactly which display character each canonical
// character came from. The native EpubTextIndexer (C++) applies these SAME rules to
// the packaged XHTML; this module applies them to the live DOM the paper renders.
// Because both sides fold identically, a stored canonical offset resolves to the
// right DOM range without ever depending on generated element ids.
//
// Kept as an ES module so paper_glue.js can dynamic-import it in the WebEngine paper
// AND a headless node test (tests/reader2_alignment_text_test.mjs) can import the pure
// functions to prove byte-for-byte parity with the C++ indexer's expected.json oracle.
//
// The canonical fold (must stay identical to EpubTextIndexer.cpp::canonicalFold):
//   1. Per code point: NFD-decompose and DROP non-spacing marks (Mn) — so "café"
//      written NFC (é) or NFD (e + ◌́) both fold to "cafe".
//   2. Fold typographic variants: curly quotes -> ' and ", all dashes -> '-',
//      ellipsis … -> "...", ASCII A–Z -> a–z.
//   3. Collapse every run of whitespace (incl. NBSP and Unicode spaces) to a single
//      ASCII space, trimming leading/trailing.
// Display text is never altered on the page; the fold produces matching evidence only.
//
// [Agent 2 (Claude), biblio]

// Whitespace that collapses to a single space (ASCII + NBSP + Unicode spaces).
const WS = new Set([
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20, 0xA0,
    0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007,
    0x2008, 0x2009, 0x200A, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000,
])
export const isCanonWhitespace = cp => WS.has(cp)

// Block-level tags introduce a separator so adjacent blocks' words never merge; inline
// tags do not (so "re<em>al</em>ly" folds to one word "really"). Skipped entirely:
// non-rendered / non-narrative content.
const BLOCK_TAGS = new Set([
    'P','DIV','BR','LI','UL','OL','TABLE','TR','TD','TH','SECTION','ARTICLE','ASIDE',
    'HEADER','FOOTER','NAV','FIGURE','FIGCAPTION','BLOCKQUOTE','PRE','HR','DL','DT','DD',
    'H1','H2','H3','H4','H5','H6','MAIN','ADDRESS','CAPTION','HGROUP',
])
const SKIP_TAGS = new Set(['SCRIPT','STYLE','HEAD','TEMPLATE','NOSCRIPT'])
export const isBlockTag = tag => BLOCK_TAGS.has(String(tag || '').toUpperCase())

// Fold a single (already mark-free) code point to zero-or-more canonical characters.
function foldChar(cp) {
    // curly / straight single quotes and primes -> '
    if (cp === 0x2018 || cp === 0x2019 || cp === 0x201A || cp === 0x201B || cp === 0x2032) return "'"
    // curly / straight double quotes -> "
    if (cp === 0x201C || cp === 0x201D || cp === 0x201E || cp === 0x201F || cp === 0x2033) return '"'
    // dashes / minus -> hyphen-minus
    if (cp === 0x2010 || cp === 0x2011 || cp === 0x2012 || cp === 0x2013 || cp === 0x2014
        || cp === 0x2015 || cp === 0x2212) return '-'
    // ellipsis -> three dots
    if (cp === 0x2026) return '...'
    // ASCII uppercase -> lowercase
    if (cp >= 0x41 && cp <= 0x5A) return String.fromCharCode(cp + 32)
    return String.fromCodePoint(cp)
}

const MN = /\p{Mn}/u

// canonicalFold(displayText) -> { canonical, map }
//   canonical : the folded matching stream
//   map[k]    : the index in displayText where canonical char k's source begins
export function canonicalFold(displayText) {
    const src = String(displayText == null ? '' : displayText)
    let canonical = ''
    const map = []
    let pendingSpace = false
    let spaceSrc = -1
    let emitted = false

    // Iterate by code point, tracking the UTF-16 index of each.
    let i = 0
    while (i < src.length) {
        const cp = src.codePointAt(i)
        const width = cp > 0xFFFF ? 2 : 1
        if (WS.has(cp)) {
            if (emitted && !pendingSpace) { pendingSpace = true; spaceSrc = i }
            i += width
            continue
        }
        // NFD-decompose this code point and drop non-spacing marks.
        const decomp = String.fromCodePoint(cp).normalize('NFD')
        for (const ch of decomp) {
            if (MN.test(ch)) continue
            const folded = foldChar(ch.codePointAt(0))
            for (const fc of folded) {
                if (pendingSpace) { canonical += ' '; map.push(spaceSrc); pendingSpace = false }
                canonical += fc
                map.push(i)
                emitted = true
            }
        }
        i += width
    }
    return { canonical, map }
}

// extractDisplay(root) -> { text, nodes }
//   text     : the assembled display string (block separators as '\n')
//   nodes[i] : { node, offset } for a real text-node character, or null for a separator
export function extractDisplay(root) {
    let text = ''
    const nodes = []
    const pushSep = () => { if (text.length && text[text.length - 1] !== '\n') { text += '\n'; nodes.push(null) } }
    const visit = node => {
        const type = node.nodeType
        if (type === 3) { // TEXT_NODE
            const v = node.nodeValue || ''
            for (let o = 0; o < v.length; o++) { text += v[o]; nodes.push({ node, offset: o }) }
            return
        }
        if (type !== 1) return // only element + text
        const tag = String(node.tagName || node.nodeName || '').toUpperCase()
        if (SKIP_TAGS.has(tag)) return
        // aria-hidden content is decorative / non-narrated — skip its whole subtree.
        if (typeof node.getAttribute === 'function' && node.getAttribute('aria-hidden') === 'true') return
        const block = BLOCK_TAGS.has(tag)
        if (block) pushSep()
        const kids = node.childNodes || []
        for (let k = 0; k < kids.length; k++) visit(kids[k])
        if (block) pushSep()
    }
    visit(root)
    return { text, nodes }
}

// canonicalWalk(root) -> { canonical, map, nodes, displayText, nodeAt, indexAt }
//   nodeAt(k) : the { node, offset } the canonical char k came from (null for a
//               collapsed separator), so the paper can build a DOM Range for any
//               canonical [start,end) span.
//   indexAt(node, offset) : the REVERSE map — the canonical index whose display
//               source is at/after (node, offset) within that text node, so a
//               double-clicked DOM point resolves back to a canonical offset.
//               -1 when the node isn't part of this section.
export function canonicalWalk(root) {
    const { text, nodes } = extractDisplay(root)
    const { canonical, map } = canonicalFold(text)
    const nodeAt = k => (k >= 0 && k < map.length ? nodes[map[k]] || null : null)
    const indexAt = (node, offset) => {
        let lastInNode = -1
        for (let k = 0; k < map.length; k++) {
            const src = nodes[map[k]]
            if (!src || src.node !== node) continue
            lastInNode = k
            if (src.offset >= offset) return k
        }
        // offset lies past this node's last mapped char → the boundary just after it
        // (correct exclusive end for a range terminating at the node's tail). -1 if the
        // node never appears (the point isn't in this canonical stream).
        return lastInNode >= 0 ? lastInNode + 1 : -1
    }
    return { canonical, map, nodes, displayText: text, nodeAt, indexAt }
}

// resolveCanonicalSpan(walk, start, end) -> { startNode, startOffset, endNode, endOffset } | null
//   The pure range-resolution: a canonical [start,end) span becomes the DOM Range
//   endpoints a real Range would use, CORRECTLY CROSSING inline element boundaries
//   (the start and end can land in different text nodes — e.g. a word split across an
//   <em>). null when the span is malformed or can't be resolved in this walk. Node-
//   testable: no live DOM, only the walk's forward node map.
export function resolveCanonicalSpan(walk, start, end) {
    if (!walk || typeof walk.nodeAt !== 'function') return null
    if (!Number.isFinite(start) || !Number.isFinite(end) || start < 0 || end <= start) return null
    const s = walk.nodeAt(start)
    const e = walk.nodeAt(end - 1)          // last INCLUDED canonical char
    if (!s || !e || !s.node || !e.node) return null
    return { startNode: s.node, startOffset: s.offset, endNode: e.node, endOffset: e.offset + 1 }
}

// canonicalRangeFromDom(walk, sel) -> { start, end } | null
//   The inverse of resolveCanonicalSpan: a DOM selection ({startNode,startOffset,
//   endNode,endOffset}) becomes the canonical [start,end) span it covers. Used to turn
//   a double-clicked word (the browser selects it) into a canonical offset to seek to.
//   null when either boundary isn't in this walk or the span is empty/inverted.
export function canonicalRangeFromDom(walk, sel) {
    if (!walk || typeof walk.indexAt !== 'function' || !sel) return null
    const start = walk.indexAt(sel.startNode, sel.startOffset)
    const end = walk.indexAt(sel.endNode, sel.endOffset)
    if (start < 0 || end < 0 || end <= start) return null
    return { start, end }
}

const stripHashFrag = h => String(h || '').split('#')[0]
// Tolerant spine-href match: exact, or one path is a '/'-boundary suffix of the other.
// foliate resolves spine hrefs relative to the OPF (e.g. "OEBPS/Text/ch1.xhtml") while the
// native indexer records the manifest-relative href ("Text/ch1.xhtml"); a suffix match on a
// segment boundary reconciles the two without matching "xch1.xhtml" against "ch1.xhtml".
const hrefMatches = (a, b) => {
    const x = stripHashFrag(a), y = stripHashFrag(b)
    if (!x || !y) return false
    if (x === y) return true
    return x.endsWith('/' + y) || y.endsWith('/' + x)
}

// createReadAlongPainter(deps) -> the read-along paint state machine.
//
// PURE over injected platform deps so a headless node test drives it with a fake DOM +
// fake overlay + fake emit; paper_glue.js constructs ONE instance wired to the live
// foliate view. It owns: the active style, the last-painted cue identity (for idempotent
// repaint), a per-section canonicalWalk cache, and the current paint (for the double-click
// gate). It resolves canonical spans to DOM ranges and asks the overlay to draw a
// non-destructive wash (sentence) + emphasis (word); word enlargement is a positioned
// CLONE in a dedicated absolutely-positioned, non-interactive layer so line boxes and
// pagination never change. clear() removes that layer, restoring the section DOM exactly.
//
// deps:
//   sections()            -> [{ doc, spineHref }]  on-screen rendered sections (doc has .body + createElement)
//   makeOverlay(doc)      -> { draw(kind, rangeDesc, opts), clear() }  wash/emphasis layer (foliate Overlayer live; recorder in tests)
//   measure(doc, rd)      -> { left, top, width, height } | null       clone geometry (live in the bench; fake in node)
//   emit(name, payload)   -> event UP
//   getGen()              -> int                                        current open generation, stamped on every event
export function createReadAlongPainter(deps) {
    const { sections, makeOverlay, measure, emit, getGen } = deps || {}
    let style = 'sentence_word'
    let scale = 1
    const walks = new Map()        // doc -> canonicalWalk result
    const overlays = new Map()     // doc -> overlay
    const cloneLayers = new Map()  // doc -> clone container element
    let lastKey = null             // identity of the last successfully painted cue (idempotence)
    let painted = null             // { doc, spineHref, sentence:{start,end}, word:{start,end}|null }

    const showsSentence = () => style === 'sentence' || style === 'sentence_word'
    const showsWord = () => style === 'word' || style === 'sentence_word'
    const gen = () => (typeof getGen === 'function' ? getGen() : 0)

    const liveSections = () => (typeof sections === 'function' ? (sections() || []) : [])
    const pruneDead = () => {
        const live = new Set(liveSections().map(s => s.doc))
        for (const m of [walks, overlays, cloneLayers])
            for (const doc of [...m.keys()]) if (!live.has(doc)) m.delete(doc)
        if (painted && !live.has(painted.doc)) { painted = null; lastKey = null }
    }
    const walkFor = doc => {
        let w = walks.get(doc)
        if (!w) { w = canonicalWalk(doc.body || doc); walks.set(doc, w) }
        return w
    }
    const overlayFor = doc => {
        let o = overlays.get(doc)
        if (!o) { o = makeOverlay(doc); overlays.set(doc, o) }
        return o
    }
    const findSection = href => liveSections().find(s => hrefMatches(s.spineHref, href)) || null

    const cueIdentity = cue => {
        const s = cue && cue.sentence, w = cue && cue.word
        return [stripHashFrag(cue && cue.spineHref), s && s.start, s && s.end,
            w ? w.start : 'x', w ? w.end : 'x', style, scale].join('|')
    }
    const cueLocation = cue => ({
        spineHref: (cue && cue.spineHref) || '',
        canonicalStart: (cue && cue.sentence && cue.sentence.start) ?? -1,
        canonicalEnd: (cue && cue.sentence && cue.sentence.end) ?? -1,
    })

    const clearClones = () => {
        for (const [doc, layer] of cloneLayers) {
            try { (layer.parentNode || doc.body || doc).removeChild(layer) } catch (e) { /* already gone */ }
        }
        cloneLayers.clear()
    }
    const clearOverlays = () => { for (const o of overlays.values()) { try { o.clear() } catch (e) {} } }
    const clearPaint = () => { clearOverlays(); clearClones(); painted = null }

    const cloneLayerFor = doc => {
        let layer = cloneLayers.get(doc)
        if (!layer) {
            layer = doc.createElement('div')
            layer.setAttribute('data-readalong', 'clone-layer')
            const st = layer.style
            st.position = 'absolute'; st.left = '0'; st.top = '0'
            st.width = '0'; st.height = '0'; st.overflow = 'visible'
            st.pointerEvents = 'none'; st.zIndex = '2147483646'
            ;(doc.body || doc).appendChild(layer)
            cloneLayers.set(doc, layer)
        }
        return layer
    }
    const paintClone = (doc, walk, word, rangeDesc) => {
        const rect = (typeof measure === 'function') ? measure(doc, rangeDesc) : null
        const clone = doc.createElement('span')
        clone.setAttribute('data-readalong', 'word-enlarged')
        const from = walk.map[word.start]
        const to = walk.map[Math.max(word.start, word.end - 1)]
        clone.textContent = walk.displayText.slice(from, (to ?? from) + 1)
        const st = clone.style
        st.position = 'absolute'; st.pointerEvents = 'none'; st.whiteSpace = 'pre'
        st.transformOrigin = 'left top'; st.transform = 'scale(' + scale + ')'
        if (rect) { st.left = rect.left + 'px'; st.top = rect.top + 'px' }
        cloneLayerFor(doc).appendChild(clone)
    }

    return {
        setStyle(opts) {
            if (opts == null) return
            if (typeof opts === 'string') { style = opts; return }
            if (typeof opts.style === 'string') style = opts.style
            if (Number.isFinite(opts.scale)) scale = opts.scale
        },
        // paint(cue) -> 'painted' | 'nochange' | 'missing'. Idempotent: an identical cue
        // (same spineHref + sentence + word + style + scale) is a no-op. On an unresolvable
        // cue it clears any prior paint and emits readAlongRangeMissing.
        paint(cue) {
            if (!cue || !cue.sentence) return 'missing'
            // Prune dead-doc caches FIRST: a relocate swaps the section iframe, so if the
            // painted doc is gone, painted/lastKey reset here and an identical cue repaints
            // against the fresh DOM instead of short-circuiting on stale identity.
            pruneDead()
            const key = cueIdentity(cue)
            if (key === lastKey) return 'nochange'
            const sec = findSection(cue.spineHref)
            const walk = sec ? walkFor(sec.doc) : null
            const sentenceRange = walk ? resolveCanonicalSpan(walk, cue.sentence.start, cue.sentence.end) : null
            if (!sec || !sentenceRange) {
                clearPaint(); lastKey = null
                emit('readAlongRangeMissing', { gen: gen(), location: cueLocation(cue) })
                return 'missing'
            }
            clearPaint()
            const overlay = overlayFor(sec.doc)
            if (showsSentence()) overlay.draw('sentence', sentenceRange, { kind: 'sentence' })
            let wordCanon = null
            if (cue.word && showsWord()) {
                const wordRange = resolveCanonicalSpan(walk, cue.word.start, cue.word.end)
                if (wordRange) {
                    overlay.draw('word', wordRange, { kind: 'word' })
                    if (scale > 1) paintClone(sec.doc, walk, cue.word, wordRange)
                    wordCanon = { start: cue.word.start, end: cue.word.end }
                }
            }
            painted = { doc: sec.doc, spineHref: sec.spineHref,
                sentence: { start: cue.sentence.start, end: cue.sentence.end }, word: wordCanon }
            lastKey = key
            return 'painted'
        },
        clear() { clearPaint(); lastKey = null },
        // handleDoubleClick(sel) -> did it emit? A double-click that lands within the
        // currently-painted sentence emits alignedDoubleClick with the double-clicked word's
        // canonical offsets (seek-to-that-word). Outside any painted range → false, no emit.
        handleDoubleClick(sel) {
            if (!painted || !sel) return false
            const walk = walks.get(painted.doc)
            if (!walk) return false
            const span = canonicalRangeFromDom(walk, sel)
            if (!span) return false
            if (span.start >= painted.sentence.end || span.end <= painted.sentence.start) return false
            emit('alignedDoubleClick', { gen: gen(),
                location: { spineHref: painted.spineHref, canonicalStart: span.start, canonicalEnd: span.end } })
            return true
        },
        // resolveLocation(location) -> { doc, spineHref, range } | null (does NOT move the
        // view). paper_glue uses it to comfort-scroll / navigate to a canonical location.
        resolveLocation(location) {
            if (!location) return null
            pruneDead()
            const sec = findSection(location.spineHref)
            if (!sec) return null
            const range = resolveCanonicalSpan(walkFor(sec.doc), location.canonicalStart, location.canonicalEnd)
            return range ? { doc: sec.doc, spineHref: sec.spineHref, range } : null
        },
        invalidate() { clearPaint(); walks.clear(); overlays.clear(); cloneLayers.clear(); lastKey = null },
        _painted() { return painted },
    }
}
