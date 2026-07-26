// tests/universe_ext_api_test.mjs — validation rules for a universe payload.
// An invalid entry is DROPPED, never rendered: a video tile that reaches Theatre
// without a type opens a series as a movie and dies (spec §5.2, §5.4).
import { readFileSync } from 'node:fs';
let src = readFileSync('qml/UniverseExtApi.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module',
  src + '\nmodule.validate=validate;module.fileFor=fileFor;module.load=load;module.setReader=setReader;'
)(mod);

let failed = 0;
const ok  = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failed++; };
const eq  = (a, b, m) => JSON.stringify(a) === JSON.stringify(b) ? ok(`${m} → ${JSON.stringify(a)}`)
                                                                 : bad(`${m} → ${JSON.stringify(a)}, expected ${JSON.stringify(b)}`);

console.log('section order is the server\'s, never re-sorted');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'b', title: 'B', kind: 'video', entries: [{ id: 'tt1', type: 'movie', title: 'b' }] },
    { id: 'a', title: 'A', kind: 'video', entries: [{ id: 'tt2', type: 'movie', title: 'a' }] }
  ]}};
  eq(mod.validate(p).sections.map(s => s.id), ['b', 'a'], 'order preserved');
}

console.log('\na video entry with no type is dropped');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'tv', title: 'TV', kind: 'video', entries: [
      { id: 'tt1', type: 'series', title: 'good' },
      { id: 'tt2', title: 'no type' }
    ]}
  ]}};
  eq(mod.validate(p).sections[0].entries.map(e => e.title), ['good'], 'typeless video dropped');
}

console.log('\nan unknown kind drops the whole section');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'v', title: 'V', kind: 'video', entries: [{ id: 'tt1', type: 'movie', title: 'k' }] },
    { id: 'w', title: 'W', kind: 'hologram', entries: [{ id: 'z', title: 'q' }] }
  ]}};
  eq(mod.validate(p).sections.map(s => s.id), ['v'], 'unknown kind skipped');
}

console.log('\na manual entry survives without an id, and keeps its position');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'm', title: 'M', kind: 'manga', entries: [
      { id: '1', provider: 'anilist', title: 'first' },
      { manual: true, title: 'coloured' },
      { id: '2', provider: 'anilist', title: 'third' }
    ]}
  ]}};
  eq(mod.validate(p).sections[0].entries.map(e => e.title), ['first', 'coloured', 'third'],
     'manual entry kept in place');
}

console.log('\na comic entry needs posts, not a tag');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'c', title: 'C', kind: 'comic', entries: [
      { provider: 'getcomics', title: 'good', posts: [123] },
      { provider: 'getcomics', title: 'tagged', tag: 'batman' }
    ]}
  ]}};
  eq(mod.validate(p).sections[0].entries.map(e => e.title), ['good'], 'tag-only comic dropped');
}

console.log('\nan empty section never renders');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'e', title: 'E', kind: 'video', entries: [{ id: 'x', title: 'no type' }] }
  ]}};
  eq(mod.validate(p).sections.length, 0, 'section emptied by validation is removed');
}

console.log('\nextension id maps to its bundled payload');
eq(mod.fileFor('com.colosseum.universe.onepiece'), 'one-piece', 'One Piece file');
eq(mod.fileFor('com.colosseum.universe.dcau'), 'dcau', 'DCAU file');
eq(mod.fileFor('com.example.other'), '', 'unknown extension has no payload');

// load() carries the reader seam + cache policy. Each scenario below gets its own fresh
// module instance so one test's cache never bleeds into the next.
function freshMod() {
  const m = {};
  new Function('module',
    src + '\nmodule.validate=validate;module.fileFor=fileFor;module.load=load;module.setReader=setReader;'
  )(m);
  return m;
}
const okPayload = JSON.stringify({ universe: { title: 'X', sections: [
  { id: 's', title: 'S', kind: 'video', entries: [{ id: 'tt1', type: 'movie', title: 't' }] }
]}});

console.log('\nload(): done is called exactly once on success');
{
  const m = freshMod();
  let calls = 0;
  m.setReader(function (f) { return okPayload; });
  m.load('com.colosseum.universe.onepiece', function () { calls++; });
  eq(calls, 1, 'done called once');
}

console.log('\nload(): missing file (reader returns "") → done(null)');
{
  const m = freshMod();
  let got = 'unset';
  m.setReader(function (f) { return ''; });
  m.load('com.colosseum.universe.onepiece', function (p) { got = p; });
  eq(got, null, 'done(null) on empty reader text');
}

console.log('\nload(): malformed JSON → done(null)');
{
  const m = freshMod();
  let got = 'unset';
  m.setReader(function (f) { return '{not json'; });
  m.load('com.colosseum.universe.onepiece', function (p) { got = p; });
  eq(got, null, 'done(null) on malformed JSON');
}

console.log('\nload(): a failed load is NOT cached — a retry with a now-good reader succeeds');
{
  const m = freshMod();
  let readerCalls = 0;
  let bad = true;
  m.setReader(function (f) { readerCalls++; return bad ? '' : okPayload; });
  let first = 'unset';
  m.load('com.colosseum.universe.onepiece', function (p) { first = p; });
  eq(first, null, 'first load fails (reader returned empty text)');
  bad = false;
  let second = 'unset';
  m.load('com.colosseum.universe.onepiece', function (p) { second = p; });
  eq(second && second.sections.length, 1, 'retry succeeds — failure was never cached');
  eq(readerCalls, 2, 'reader invoked again on retry');
}

console.log('\nload(): a cache hit never calls the reader again');
{
  const m = freshMod();
  let readerCalls = 0;
  m.setReader(function (f) { readerCalls++; return okPayload; });
  m.load('com.colosseum.universe.onepiece', function () {});
  m.load('com.colosseum.universe.onepiece', function () {});
  eq(readerCalls, 1, 'reader called once despite two loads');
}

console.log('\nload(): no reader installed → done(null) once, nothing cached');
{
  const m = freshMod();
  let calls = 0;
  m.load('com.colosseum.universe.onepiece', function (p) { calls++; eq(p, null, 'done(null) with no reader'); });
  eq(calls, 1, 'done called exactly once with no reader');
  // a later setReader + load must still succeed — the missing-reader case was NOT cached
  m.setReader(function (f) { return okPayload; });
  let after = 'unset';
  m.load('com.colosseum.universe.onepiece', function (p) { after = p; });
  eq(after && after.sections.length, 1, 'load succeeds once a reader is installed');
}

console.log('\nload(): a payload that validates to zero sections is a failure, not cached');
{
  const m = freshMod();
  const emptyPayload = JSON.stringify({ universe: { title: 'X', sections: [
    { id: 'e', title: 'E', kind: 'video', entries: [{ id: 'x', title: 'no type' }] }
  ]}});
  let bad = true;
  m.setReader(function (f) { return bad ? emptyPayload : okPayload; });
  let first = 'unset';
  m.load('com.colosseum.universe.onepiece', function (p) { first = p; });
  eq(first, null, 'done(null) when validation empties every section');
  bad = false;
  let second = 'unset';
  m.load('com.colosseum.universe.onepiece', function (p) { second = p; });
  eq(second && second.sections.length, 1, 'a subsequent good payload for the same id loads fine');
}

console.log('\nload(): unknown extension id → done(null), reader never called');
{
  const m = freshMod();
  let readerCalls = 0;
  let got = 'unset';
  m.setReader(function (f) { readerCalls++; return ''; });
  m.load('com.example.other', function (p) { got = p; });
  eq(got, null, 'done(null) for unknown extension id');
  eq(readerCalls, 0, 'reader never invoked for an id with no bundled file');
}

console.log('\nload(): real bundled payloads survive end-to-end through the reader seam');
{
  const m = freshMod();
  const texts = {
    'one-piece': readFileSync('assets/universes/one-piece.json', 'utf8'),
    'dcau': readFileSync('assets/universes/dcau.json', 'utf8')
  };
  m.setReader(function (f) { return texts[f] || ''; });

  const rawOnePieceN = JSON.parse(texts['one-piece']).universe.sections
    .reduce((a, s) => a + s.entries.length, 0);
  const rawDcauN = JSON.parse(texts['dcau']).universe.sections
    .reduce((a, s) => a + s.entries.length, 0);

  let onePieceResult = null;
  m.load('com.colosseum.universe.onepiece', function (p) { onePieceResult = p; });
  let dcauResult = null;
  m.load('com.colosseum.universe.dcau', function (p) { dcauResult = p; });

  const onePieceN = onePieceResult.sections.reduce((a, s) => a + s.entries.length, 0);
  const dcauN = dcauResult.sections.reduce((a, s) => a + s.entries.length, 0);
  eq(onePieceN, rawOnePieceN, 'one-piece entries survive through the reader seam');
  eq(dcauN, rawDcauN, 'dcau entries survive through the reader seam');
  eq(onePieceN, 54, 'one-piece = 54 entries');
  eq(dcauN, 31, 'dcau = 31 entries');
}

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
