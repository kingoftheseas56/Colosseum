// extension_world_isolation_test.mjs — stage 1a added six house wells that all declare
// resources:["stream"]. streamExtensions() walks EVERY enabled stream provider, so if
// accepts() did not gate on type, pressing play on a film would start asking Nyaa, LibGen
// and AudioBookBay for a movie stream. That is the regression this file exists to prevent,
// and it is a risk that did not exist before the Tankoban/Biblio roster landed.
//
// Also pins acceptance 8's ordering half: the ladder is asked in store order, so a well
// placed above Torrentio is asked first.
import fs from 'fs';

let src = fs.readFileSync('qml/AddonClient.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', 'XMLHttpRequest', src +
  '\nmodule.streamExtensions=streamExtensions;module.accepts=accepts;' +
  'module.torrentioEnabled=torrentioEnabled;')(mod, function () {});

let failures = 0;
const ok  = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failures++; };
const eq  = (a, b, m) => JSON.stringify(a) === JSON.stringify(b)
  ? ok(`${m} → ${JSON.stringify(a)}`)
  : bad(`${m}: expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}`);

const E = (id, types, idPrefixes, enabled = true) =>
  ({ id, enabled, manifest: { id, resources: ['stream'], types, idPrefixes } });

// The seeded roster's stream providers, in store order.
const installed = [
  E('com.stremio.torrentio.addon', ['movie', 'series', 'anime'], ['tt', 'kitsu']),
  E('colosseum.well.nyaa',              ['manga']),
  E('colosseum.well.weebcentral.pages', ['manga']),
  E('colosseum.well.getcomics.issues',  ['comic']),
  E('colosseum.well.libgen',            ['book']),
  E('colosseum.well.indexers',          ['comic', 'book', 'audiobook']),
  E('colosseum.well.audiobookbay',      ['audiobook'])
];
const ask = (t, id) => mod.streamExtensions(installed, t, id).map(e => e.id);

console.log('a Theatre ask never reaches a Tankoban or Biblio well');
for (const t of ['movie', 'series']) {
  const got = ask(t, 'tt0388629');
  eq(got, ['com.stremio.torrentio.addon'], `${t} ladder`);
}

console.log('each world only asks its own wells');
eq(ask('manga', 'x'),     ['colosseum.well.nyaa', 'colosseum.well.weebcentral.pages'], 'manga ladder');
eq(ask('comic', 'x'),     ['colosseum.well.getcomics.issues', 'colosseum.well.indexers'], 'comic ladder');
eq(ask('book', 'x'),      ['colosseum.well.libgen', 'colosseum.well.indexers'], 'book ladder');
eq(ask('audiobook', 'x'), ['colosseum.well.indexers', 'colosseum.well.audiobookbay'], 'audiobook ladder');

console.log('the one shared well answers in three worlds from a single install');
const shared = ['comic', 'book', 'audiobook'].every(t => ask(t, 'x').includes('colosseum.well.indexers'));
shared ? ok('Torrent Indexers answers comic, book and audiobook')
       : bad('Torrent Indexers missing from one of its three worlds');

console.log('acceptance 8 — the ladder is asked in store order');
const above = [E('well.above', ['movie'], ['tt'])].concat(installed);
eq(mod.streamExtensions(above, 'movie', 'tt0388629').map(e => e.id),
   ['well.above', 'com.stremio.torrentio.addon'], 'a well placed above Torrentio is asked first');

console.log('a disabled well is never asked, in any world');
const off = installed.map(e => e.id === 'colosseum.well.nyaa' ? { ...e, enabled: false } : e);
eq(mod.streamExtensions(off, 'manga', 'x').map(e => e.id),
   ['colosseum.well.weebcentral.pages'], 'manga ladder with Nyaa switched off');

console.log('stage 2 — Torrentio is reachable only through the store');
eq(mod.torrentioEnabled(installed), true, 'installed + enabled');
eq(mod.torrentioEnabled(installed.filter(e => e.id !== 'com.stremio.torrentio.addon')), false, 'removed');
eq(mod.torrentioEnabled(installed.map(e =>
     e.id === 'com.stremio.torrentio.addon' ? { ...e, enabled: false } : e)), false, 'switched off');
eq(mod.torrentioEnabled(null), false, 'null-safe');

console.log(failures ? `\n${failures} FAILED` : '\nall green');
process.exit(failures ? 1 : 0);
