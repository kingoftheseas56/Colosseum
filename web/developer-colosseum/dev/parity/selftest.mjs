import assert from 'node:assert/strict';
import { compare } from './compare.mjs';

const rect = { x: 10, y: 20, width: 80, height: 30 };
const qml = { width: 1280, height: 720,
  words: [{ text: 'Discover', rect }, { text: 'Library', rect }],
  controls: [{ label: 'Discover', rect }, { label: 'Library', rect }] };
const web = structuredClone(qml);
assert.equal(compare(qml, web).pass, true, 'matching capture must pass');
web.words[0].text = 'Wrong';
web.controls.pop();
const planted = compare(qml, web);
assert.equal(planted.pass, false, 'planted defects must fail');
assert(planted.failures.some(x => x.includes('words: qml only: Discover')));
assert(planted.failures.some(x => x.includes('controls: qml only: Library')));
assert(planted.failures.some(x => x.includes('controls: order differs')));
const shifted = structuredClone(qml);
shifted.words[0].rect.y += 25;
assert(compare(qml, shifted).failures.some(x => x.includes('layout words: Discover')),
  'vertical displacement beyond 2% of viewport height must fail');
const repeatQml = { width: 1280, height: 720, words: [], controls: [
  { label: 'Card', rect: { ...rect, x: 10 } },
  { label: 'Card', rect: { ...rect, x: 80 } },
  { label: 'Card', rect: { ...rect, x: 900 } }] };
const repeatWeb = structuredClone(repeatQml);
repeatWeb.controls.splice(1, 1);
const allow = { controls: [{ side: 'qml', text: 'Card', count: 1,
  reason: 'Approved extra control in this probe', contract: 'probe §1' }] };
assert.equal(compare(repeatQml, repeatWeb, allow).pass, true,
  'one allowed duplicate must not remove the other matching controls from order');
console.log('parity compare selftest PASS (match, wrong word, hidden button)');
