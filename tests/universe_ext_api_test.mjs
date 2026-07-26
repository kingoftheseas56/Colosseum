// tests/universe_ext_api_test.mjs — validation rules for a universe payload.
// An invalid entry is DROPPED, never rendered: a video tile that reaches Theatre
// without a type opens a series as a movie and dies (spec §5.2, §5.4).
import { readFileSync } from 'node:fs';
let src = readFileSync('qml/UniverseExtApi.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', 'XMLHttpRequest', src + '\nmodule.validate=validate;module.fileFor=fileFor;')(mod, function(){});

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

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
