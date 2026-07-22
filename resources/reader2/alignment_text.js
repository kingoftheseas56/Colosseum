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

// canonicalWalk(root) -> { canonical, map, nodeAt }
//   nodeAt(k) : the { node, offset } the canonical char k came from (null for a
//               collapsed separator), so the paper can build a DOM Range for any
//               canonical [start,end) span.
export function canonicalWalk(root) {
    const { text, nodes } = extractDisplay(root)
    const { canonical, map } = canonicalFold(text)
    return {
        canonical,
        map,
        nodeAt: k => (k >= 0 && k < map.length ? nodes[map[k]] || null : null),
    }
}
