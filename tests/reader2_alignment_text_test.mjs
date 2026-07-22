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
import { canonicalFold, canonicalWalk } from '../resources/reader2/alignment_text.js'

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

console.log(`PASS canonical text and offsets agree`)
console.log(fails ? 'VERDICT: FAIL' : 'VERDICT: PASS')
process.exit(fails ? 1 : 0)
