// reader2_paper_alignment_test.mjs — headless proof of the READ-ALONG PAINTER state
// machine in resources/reader2/alignment_text.js (createReadAlongPainter). Run:
//   node tests/reader2_paper_alignment_test.mjs
// Verdict via console ("VERDICT: PASS/FAIL") + process exit code.
//
// WHY node, not the browser bench: the painter's DECISIONS — resolving a cue's canonical
// spans to DOM ranges, idempotent repaint, non-destructive clear, the double-click canonical
// offset, and the range-missing signal — are pure logic and are proven here against a hand-
// rolled fake DOM + a fake Overlayer (records draw/clear calls) + a fake emit (records
// events). The GEOMETRY/VISUAL half — that the wash aligns to glyph rects and that word
// ENLARGEMENT (a positioned clone) does not repaginate or shift line boxes — is layout, has
// no meaning without a real layout engine, and is proven in the browser bench with a real
// EPUB. Do NOT try to assert pixel metrics here. Mirrors reader2_paper_text_test.mjs.
//
// [Agent 2 (Claude), biblio]
import { createReadAlongPainter, canonicalWalk, resolveCanonicalSpan }
    from '../resources/reader2/alignment_text.js'

let fails = 0
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) fails++ }

// ── mutable fake DOM (the painter creates/inserts/removes a clone layer) ──────
function textNode(s) { return { nodeType: 3, nodeValue: s, childNodes: [], parentNode: null } }
function elem(tag) {
    const e = {
        nodeType: 1, tagName: tag, nodeName: tag, childNodes: [], parentNode: null,
        attributes: {}, style: {},
        getAttribute(k) { return k in this.attributes ? this.attributes[k] : null },
        setAttribute(k, v) { this.attributes[k] = String(v) },
        appendChild(c) { c.parentNode = this; this.childNodes.push(c); return c },
        removeChild(c) { const i = this.childNodes.indexOf(c); if (i >= 0) this.childNodes.splice(i, 1); c.parentNode = null; return c },
    }
    Object.defineProperty(e, 'textContent', {
        get() {
            const walk = n => n.nodeType === 3 ? (n.nodeValue || '') : (n.childNodes || []).map(walk).join('')
            return walk(this)
        },
        set(v) { const t = textNode(String(v)); t.parentNode = this; this.childNodes = [t] },
    })
    return e
}
function makeDoc(body) {
    return {
        body,
        defaultView: { scrollX: 0, scrollY: 0 },
        createElement: tag => elem(tag),
        createTextNode: s => textNode(s),
    }
}
const serialize = n => n.nodeType === 3
    ? 'T:' + n.nodeValue
    : '<' + n.tagName + '>' + (n.childNodes || []).map(serialize).join('') + '</' + n.tagName + '>'
const cloneLayerOf = body => (body.childNodes || []).find(c =>
    c.nodeType === 1 && c.getAttribute && c.getAttribute('data-readalong') === 'clone-layer')

// Fixture: "She was really here." with "really" split across an <em> (re|al|ly), so the
// resolved word range crosses inline nodes. Canonical (folded, single-spaced, lowercased):
//   "she was really here."  → sentence [0,20); word "really" [8,14); word "was" [4,7).
const CH = 'Text/ch1.xhtml'
function makeWorld(styleOpts) {
    const t1 = textNode('She was re')
    const t2 = textNode('al')
    const t3 = textNode('ly here.')
    const em = elem('EM'); em.appendChild(t2)
    const p = elem('P'); p.appendChild(t1); p.appendChild(em); p.appendChild(t3)
    const body = elem('BODY'); body.appendChild(p)
    const doc = makeDoc(body)
    const overlayRecords = []
    const events = []
    const painter = createReadAlongPainter({
        sections: () => [{ doc, spineHref: CH }],
        makeOverlay: d => {
            const rec = { doc: d, draws: [], clears: 0 }
            overlayRecords.push(rec)
            return { draw: (kind, rd, o) => rec.draws.push({ kind, rd, o }), clear: () => { rec.clears++ } }
        },
        measure: () => ({ left: 10, top: 20, width: 30, height: 12 }),
        emit: (name, payload) => events.push({ name, payload }),
        getGen: () => 42,
    })
    if (styleOpts) painter.setStyle(styleOpts)
    return { doc, body, t1, t2, t3, painter, overlayRecords, events }
}

const fullCue = { spineHref: CH, sentence: { start: 0, end: 20 }, word: { start: 8, end: 14 } }

// ── (a) paint resolves a sentence WASH + a word EMPHASIS, word crossing inline nodes ──
{
    const W = makeWorld({ style: 'sentence_word', scale: 1.5 })
    W.painter.paint(fullCue)
    const rec = W.overlayRecords[0]
    check(rec != null, 'paint created an overlay for the section')
    const sDraw = rec && rec.draws.find(d => d.kind === 'sentence')
    const wDraw = rec && rec.draws.find(d => d.kind === 'word')
    check(!!sDraw, 'paint drew the sentence wash')
    check(!!wDraw, 'paint drew the word emphasis')
    // sentence range == the independently-resolved canonical span
    const walk = canonicalWalk(W.body)
    const expS = resolveCanonicalSpan(walk, 0, 20)
    check(sDraw && sDraw.rd.startNode === expS.startNode && sDraw.rd.startOffset === expS.startOffset
        && sDraw.rd.endNode === expS.endNode && sDraw.rd.endOffset === expS.endOffset,
        'sentence wash range matches resolveCanonicalSpan(0,20)')
    // word range crosses the <em>: starts in t1 ("...re"), ends in t3 ("ly...")
    check(wDraw && wDraw.rd.startNode === W.t1 && wDraw.rd.endNode === W.t3,
        'word emphasis range crosses the inline <em> (t1 -> t3)')
    // enlargement clone: a positioned clone of the display word, in a clone layer
    const layer = cloneLayerOf(W.body)
    check(!!layer, 'a clone layer was inserted for word enlargement')
    check(layer && layer.childNodes.length === 1 && layer.childNodes[0].textContent === 'really',
        'the enlargement clone carries the DISPLAY word ("really")')
}

// ── (b) painting the SAME cue twice is idempotent (one draw batch, not two) ───
{
    const W = makeWorld({ style: 'sentence_word', scale: 1.5 })
    check(W.painter.paint(fullCue) === 'painted', 'first paint reports painted')
    check(W.painter.paint(fullCue) === 'nochange', 'identical repaint reports nochange')
    const rec = W.overlayRecords[0]
    check(rec.draws.length === 2, 'identical cue drew exactly once (sentence+word), not twice')
    check(cloneLayerOf(W.body).childNodes.length === 1, 'identical cue left exactly one clone')
    // a DIFFERENT cue clears the prior paint, then redraws
    W.painter.paint({ spineHref: CH, sentence: { start: 0, end: 7 }, word: { start: 4, end: 7 } })
    check(rec.clears >= 1, 'a changed cue cleared the prior overlay before repainting')
    check(rec.draws.length === 4, 'a changed cue drew a fresh sentence+word batch')
}

// ── (c) clearReadAlong restores the DOM to its exact prior node structure ─────
{
    const W = makeWorld({ style: 'sentence_word', scale: 1.5 })
    const before = serialize(W.body)
    W.painter.paint(fullCue)
    check(!!cloneLayerOf(W.body), 'clone layer present while painted')
    check(serialize(W.body) !== before, 'painting mutated the DOM (clone layer added)')
    W.painter.clear()
    check(!cloneLayerOf(W.body), 'clear removed the clone layer')
    check(serialize(W.body) === before, 'clear restored the DOM byte-for-byte (no leftover clone nodes)')
    check(W.overlayRecords[0].clears >= 1, 'clear cleared the overlay wash/emphasis too')
}

// ── (d) a double-click within a painted range emits alignedDoubleClick ────────
{
    const W = makeWorld({ style: 'sentence_word', scale: 1.5 })
    W.painter.paint(fullCue)                       // sentence [0,20) painted
    // double-click "was" → the browser selects it; the painter maps it to canonical [4,7)
    const hit = W.painter.handleDoubleClick({ startNode: W.t1, startOffset: 4, endNode: W.t1, endOffset: 7 })
    check(hit === true, 'double-click inside the painted sentence is handled')
    const ev = W.events.find(e => e.name === 'alignedDoubleClick')
    check(!!ev, 'alignedDoubleClick was emitted')
    check(ev && ev.payload.gen === 42, 'alignedDoubleClick carries the open gen')
    check(ev && ev.payload.location.spineHref === CH
        && ev.payload.location.canonicalStart === 4 && ev.payload.location.canonicalEnd === 7,
        'alignedDoubleClick reports the clicked word canonical offsets ("was" [4,7))')
}

// ── (d.2) a double-click OUTSIDE the painted range does not emit ──────────────
{
    const W = makeWorld({ style: 'sentence_word', scale: 1.5 })
    W.painter.paint({ spineHref: CH, sentence: { start: 0, end: 7 }, word: { start: 4, end: 7 } }) // "she was"
    // double-click "here" (canonical [15,19)) — outside the painted sentence [0,7)
    const hit = W.painter.handleDoubleClick({ startNode: W.t3, startOffset: 3, endNode: W.t3, endOffset: 7 })
    check(hit === false, 'double-click outside the painted range is not handled')
    check(!W.events.some(e => e.name === 'alignedDoubleClick'), 'no alignedDoubleClick outside the painted range')
    // and with nothing painted, a double-click never emits
    const W2 = makeWorld({ style: 'sentence_word', scale: 1.5 })
    check(W2.painter.handleDoubleClick({ startNode: W2.t1, startOffset: 4, endNode: W2.t1, endOffset: 7 }) === false,
        'double-click with nothing painted is a no-op')
}

// ── (e) an unresolvable cue emits readAlongRangeMissing and paints nothing ────
{
    // wrong section
    const W = makeWorld({ style: 'sentence_word', scale: 1.5 })
    const r1 = W.painter.paint({ spineHref: 'Text/ch9.xhtml', sentence: { start: 0, end: 20 } })
    check(r1 === 'missing', 'a cue for an off-screen section reports missing')
    const ev1 = W.events.find(e => e.name === 'readAlongRangeMissing')
    check(ev1 && ev1.payload.location.spineHref === 'Text/ch9.xhtml' && ev1.payload.gen === 42,
        'readAlongRangeMissing carries the unresolved location + gen')
    check(W.overlayRecords.length === 0, 'an off-screen cue drew no overlay')
    check(!cloneLayerOf(W.body), 'an off-screen cue inserted no clone')

    // in-section but out-of-range canonical span
    const W2 = makeWorld({ style: 'sentence_word', scale: 1.5 })
    const r2 = W2.painter.paint({ spineHref: CH, sentence: { start: 1000, end: 1010 } })
    check(r2 === 'missing', 'an out-of-range canonical span reports missing')
    check(W2.events.some(e => e.name === 'readAlongRangeMissing'), 'out-of-range span emits readAlongRangeMissing')
}

// ── style controls what paints (sentence-only / word-only) ───────────────────
{
    const Ws = makeWorld({ style: 'sentence', scale: 1.5 })
    Ws.painter.paint(fullCue)
    const rs = Ws.overlayRecords[0]
    check(rs.draws.some(d => d.kind === 'sentence') && !rs.draws.some(d => d.kind === 'word'),
        'style "sentence" draws the wash only')
    check(!cloneLayerOf(Ws.body), 'style "sentence" makes no enlargement clone')

    const Ww = makeWorld({ style: 'word', scale: 1.5 })
    Ww.painter.paint(fullCue)
    const rw = Ww.overlayRecords[0]
    check(rw.draws.some(d => d.kind === 'word') && !rw.draws.some(d => d.kind === 'sentence'),
        'style "word" draws the emphasis only')
}

console.log(`PASS read-along painter resolves, repaints idempotently, clears clean, seeks on double-click`)
console.log(fails ? 'VERDICT: FAIL' : 'VERDICT: PASS')
process.exit(fails ? 1 : 0)
