// reader2_alignment_text_test.mjs — headless proof of the shared canonical fold in
// resources/reader2/alignment_text.js. Run:  node tests/reader2_alignment_text_test.mjs
//
// Two jobs: (1) independent inline cases pin each fold rule; (2) canonicalFold over the
// fixture's displaySource must reproduce expected.json's canonical stream + offset
// map — the SAME oracle the C++ epub_text_indexer_harness must match, so the two
// implementations are proven equal transitively. canonicalWalk is exercised over a
// fake DOM to prove inline-node concatenation and script/aria-hidden skipping.
//
// Verdict via console ("VERDICT: PASS/FAIL") + process exit code.  [Agent 2 (Claude), biblio]

import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { canonicalFold, canonicalWalk, resolveCanonicalSpan, canonicalRangeFromDom }
    from '../resources/reader2/alignment_text.js'

let fails = 0
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) fails++ }

// ── 1. inline fold rules (independent of the fixture) ────────────────────────
check(canonicalFold('“Hi”').canonical === '"hi"', 'curly double quotes -> "')
check(canonicalFold('‘a’').canonical === "'a'", 'curly single quotes -> \'')
check(canonicalFold('a—b').canonical === 'a-b', 'em dash -> -')
check(canonicalFold('a–b').canonical === 'a-b', 'en dash -> -')
check(canonicalFold('a−b').canonical === 'a-b', 'minus sign -> -')
check(canonicalFold('x…y').canonical === 'x...y', 'ellipsis -> ...')
check(canonicalFold('A B').canonical === 'a b', 'NBSP collapses to a space')
check(canonicalFold('a   b').canonical === 'a b', 'whitespace run collapses')
check(canonicalFold('  a  ').canonical === 'a', 'leading/trailing whitespace trimmed')
// NFC vs NFD fold to the same stream (marks dropped).
check(canonicalFold('é').canonical === 'e', 'NFC é -> e')
check(canonicalFold('é').canonical === 'e', 'NFD e+◌́ -> e')
check(canonicalFold('é').canonical === canonicalFold('é').canonical, 'NFC and NFD agree')
// map: one canonical char, source index 0, for both composition forms.
check(canonicalFold('é').map.length === 1 && canonicalFold('é').map[0] === 0, 'NFC map length + origin')
check(canonicalFold('é').map.length === 1 && canonicalFold('é').map[0] === 0, 'NFD map length + origin')
// map length always equals canonical length (parity with C++ QVector<int>).
{
    const r = canonicalFold('The “dog” — café…')
    check(r.map.length === r.canonical.length, 'map length equals canonical length')
}

// ── 2. parity with the C++ oracle (expected.json) ────────────────────────────
const expectedPath = fileURLToPath(new URL('./fixtures/alignment/canonical/expected.json', import.meta.url))
const expected = JSON.parse(readFileSync(expectedPath, 'utf8'))
const folded = canonicalFold(expected.displaySource)
check(folded.canonical === expected.canonical, 'canonicalFold(displaySource) matches oracle canonical')
for (const c of expected.mapChecks) {
    check(folded.map[c.canonicalIndex] === c.displayIndex, `map[${c.canonicalIndex}]==${c.displayIndex} (${c.note})`)
}

// ── 3. canonicalWalk over a fake DOM that mirrors the fixture ─────────────────
const el = (tag, children = [], attrs = {}) => ({
    nodeType: 1, tagName: tag, childNodes: children,
    getAttribute: k => (k in attrs ? attrs[k] : null),
})
const txt = s => ({ nodeType: 3, nodeValue: s, childNodes: [] })

const [P1, P2, P3, P4] = expected.displaySource.split('\n')
const ri = P2.indexOf('really')       // split "really" across an inline <em>: re|al|ly
const before = P2.slice(0, ri), after = P2.slice(ri + 'really'.length)
const body = el('BODY', [
    el('P', [txt(P1), el('SPAN', [txt('SKIPME')], { 'aria-hidden': 'true' })]),
    el('SCRIPT', [txt('var skip = 1;')]),
    el('P', [txt(before + 're'), el('EM', [txt('al')]), txt('ly' + after)]),
    el('P', [txt(P3)]),
    el('P', [txt(P4)]),
])
const walked = canonicalWalk(body)
check(walked.canonical === expected.canonical, 'canonicalWalk(fake DOM) matches oracle canonical')
check(!walked.canonical.includes('skipme'), 'aria-hidden span text is skipped')
check(!walked.canonical.includes('var skip'), 'script text is skipped')
// "really" spans two text nodes ("...re" and "ly?"); its first char resolves to a text node.
{
    const k = walked.canonical.indexOf('really')
    const at = walked.nodeAt(k)
    check(at != null && at.node.nodeType === 3, 'canonicalWalk resolves an inline-split word to a text node')
}

// ── 4. resolveCanonicalSpan — canonical [start,end) -> DOM Range endpoints ────
// The sentence/word range resolution the paper paints with. The fixture's "really" is
// split across an <em> (re|al|ly), the canonical cross-inline-node case.
const reNode = body.childNodes[2].childNodes[0]     // 3rd <p>'s "...re" text node
const lyNode = body.childNodes[2].childNodes[2]     // its "ly..." text node
const smithNode = body.childNodes[0].childNodes[0]  // 1st <p>'s single text node
{
    const rk = walked.canonical.indexOf('really')
    const span = resolveCanonicalSpan(walked, rk, rk + 'really'.length)
    check(span != null, 'resolveCanonicalSpan resolves the "really" span')
    check(span && span.startNode === reNode, 'word span start lands in the "...re" text node')
    check(span && span.endNode === lyNode, 'word span end lands in the "ly..." text node (crossed the <em>)')
    check(span && span.startNode !== span.endNode, 'inline-split word yields a cross-node range')
}
{
    const sk = walked.canonical.indexOf('smith')
    const span = resolveCanonicalSpan(walked, sk, sk + 'smith'.length)
    check(span && span.startNode === smithNode && span.endNode === smithNode,
        'a word inside one text node yields a single-node range')
    check(span && span.endOffset - span.startOffset === 'smith'.length, 'single-node span width == word length')
}
check(resolveCanonicalSpan(walked, 5, 5) === null, 'empty span (end<=start) -> null')
check(resolveCanonicalSpan(walked, 99999, 100000) === null, 'out-of-range span -> null')

// ── 5. canonicalRangeFromDom — DOM selection -> canonical [start,end) (double-click) ──
{
    const rk = walked.canonical.indexOf('really')
    const span = resolveCanonicalSpan(walked, rk, rk + 'really'.length)
    const back = canonicalRangeFromDom(walked, span)   // round-trip the cross-node word
    check(back != null && back.start === rk && back.end === rk + 'really'.length,
        'canonicalRangeFromDom round-trips a cross-node word to its canonical span')
}
{
    const sk = walked.canonical.indexOf('smith')
    const span = resolveCanonicalSpan(walked, sk, sk + 'smith'.length)
    const back = canonicalRangeFromDom(walked, span)
    check(back != null && back.start === sk && back.end === sk + 'smith'.length,
        'canonicalRangeFromDom round-trips a single-node word')
}
check(canonicalRangeFromDom(walked, { startNode: txt('orphan'), startOffset: 0, endNode: lyNode, endOffset: 1 }) === null,
    'a selection start not in this walk -> null')

console.log(`PASS canonical text and offsets agree`)
console.log(fails ? 'VERDICT: FAIL' : 'VERDICT: PASS')
process.exit(fails ? 1 : 0)
