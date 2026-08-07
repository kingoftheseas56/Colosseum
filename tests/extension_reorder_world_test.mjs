// extension_reorder_world_test.mjs — the reorder arrows move the row the user pointed at,
// in the world he is looking at, and touch no other world's ask-order.
//
// Guards A5's audit P0-3 (agents/audit-extensions-store-ux-2026-07-25.md), which adversarial
// verification found to be worse than written: with the shipped defaults, 4 of Tankoban's 8
// arrow presses were no-ops, and 3 of those silently reordered BIBLIO. A user curating manga
// sources was editing his book sources, with feedback in neither world.
//
// The old contract was Extensions.move(id, ±1) — a swap of GLOBAL neighbours. The new one is
// Catalog.moveDestination(list, world, id, delta) -> absolute index, performed by
// ExtensionsStore::moveTo. This file pins the new behaviour AND re-runs the original defect
// so it cannot come back.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const src = readFileSync(path.join(here, '..', 'qml', 'ExtensionsCatalog.js'), 'utf8');
const mod = {};
new Function('exports', src.replace(/^\.pragma library\s*$/m, '') +
  '\nexports.moveDestination = moveDestination; exports.inWorld = inWorld;' +
  '\nexports.isWell = isWell; exports.isCatalogue = isCatalogue;')(mod);

let failed = 0;
const ok  = m => console.log(`  ok   ${m}`);
const bad = m => { console.log(`  FAIL ${m}`); failed++; };
const eq  = (got, want, m) => {
  const g = JSON.stringify(got), w = JSON.stringify(want);
  g === w ? ok(`${m} → ${g}`) : bad(`${m} → ${g}, expected ${w}`);
};

// ---- the roster exactly as native/engine/ExtensionsStore.cpp seeds it (generation 3) ----
const E = (id, core, resources, types) =>
  ({ id, core, manifest: { id, resources, types } });
const CAT = ['catalog', 'meta'], STR = ['stream'], SUB = ['subtitles'];

const shipped = () => [
  E('com.linvo.cinemeta',              true,  CAT, ['movie', 'series']),
  E('com.stremio.torrentio.addon',     false, STR, ['movie', 'series', 'anime']),
  E('community.anime.kitsu',           false, CAT, ['series', 'movie', 'anime']),
  E('org.stremio.opensubtitlesv3',     false, SUB, ['movie', 'series']),
  E('colosseum.catalogue.vault',       true,  CAT, ['manga', 'comic']),
  E('colosseum.catalogue.anilist',     true,  CAT, ['manga']),
  E('colosseum.catalogue.applebooks',  true,  CAT, ['book']),
  E('colosseum.well.nyaa',             false, STR, ['manga']),
  E('colosseum.well.weebcentral.pages',false, STR, ['manga']),
  E('colosseum.well.getcomics.issues', false, STR, ['comic']),
  E('colosseum.well.libgen',           false, STR, ['book']),
  E('colosseum.well.indexers',         false, STR, ['comic', 'book', 'audiobook']),
  E('colosseum.well.audiobookbay',     false, STR, ['audiobook']),
  // NoTorrent seeds into Theatre as a direct-HTTP stream well (movie+series), after
  // Torrentio (gen 9; VidKing retired gen 10 — House HTTP slice 4).
  E('com.notorrent.addon',             false, STR, ['movie', 'series'])
];

const short = id => id.replace('colosseum.well.', '').replace('colosseum.catalogue.', '');
const wellsIn = (list, world) =>
  list.filter(e => mod.inWorld(e, world) && mod.isWell(e)).map(e => short(e.id));

// What ExtensionsStore::moveTo does to the array: remove at i, insert at j.
function applyMoveTo(list, id, index) {
  const i = list.findIndex(e => e.id === id);
  if (i < 0) return list;
  if (list[i].core === true) return list;            // catalogues are never ranked
  const j = Math.max(0, Math.min(index, list.length - 1));
  if (i === j) return list;
  const out = list.slice();
  out.splice(j, 0, out.splice(i, 1)[0]);
  return out;
}
// The press the user actually makes. moveDestination decides WHICH row travels — the
// clicked one, or its neighbour when that disturbs other worlds less.
function press(list, world, id, delta) {
  const m = mod.moveDestination(list, world, id, delta);
  return m ? applyMoveTo(list, m.id, m.index) : list;
}

console.log('the shipped roster reads as the design says');
eq(wellsIn(shipped(), 'tankoban'), ['nyaa', 'weebcentral.pages', 'getcomics.issues', 'indexers'],
   'Tankoban wells');
eq(wellsIn(shipped(), 'biblio'), ['libgen', 'indexers', 'audiobookbay'], 'Biblio wells');

console.log('\nNoTorrent is a Theatre well and reorders within Theatre, never ranking core Cinemeta');
{
  const theatreWells = () => shipped().filter(e => mod.inWorld(e, 'theatre') && mod.isWell(e)).map(e => e.id);
  eq(theatreWells(), ['com.stremio.torrentio.addon', 'com.notorrent.addon'],
     'Theatre wells = Torrentio then NoTorrent');
  const after = press(shipped(), 'theatre', 'com.notorrent.addon', -1);
  eq(after.filter(e => mod.inWorld(e, 'theatre') && mod.isWell(e)).map(e => e.id),
     ['com.notorrent.addon', 'com.stremio.torrentio.addon'], 'NoTorrent ▲ swaps with Torrentio');
  eq(after[0].id, 'com.linvo.cinemeta', 'Cinemeta stays first — a core catalogue is never ranked');
}

console.log('\nthe defect: a GLOBAL ±1 was a no-op in Tankoban and a write to Biblio');
{
  // Reproduce the old contract exactly: swap with the global neighbour.
  const oldMove = (list, id, delta) => {
    const i = list.findIndex(e => e.id === id);
    const j = Math.max(0, Math.min(i + delta, list.length - 1));
    const out = list.slice();
    out.splice(j, 0, out.splice(i, 1)[0]);
    return out;
  };
  const after = oldMove(shipped(), 'colosseum.well.indexers', -1);
  eq(wellsIn(after, 'tankoban'), wellsIn(shipped(), 'tankoban'),
     'OLD ▲ on Tankoban rank 4 — Tankoban unchanged (the invisible no-op)');
  const moved = JSON.stringify(wellsIn(after, 'biblio')) !== JSON.stringify(wellsIn(shipped(), 'biblio'));
  moved ? ok('OLD ▲ on Tankoban silently reordered BIBLIO → ' + JSON.stringify(wellsIn(after, 'biblio')))
        : bad('expected the old contract to corrupt Biblio, it did not — check the roster');
}

console.log('\nthe fix: the row moves where the user pointed');
{
  const after = press(shipped(), 'tankoban', 'colosseum.well.indexers', -1);
  eq(wellsIn(after, 'tankoban'), ['nyaa', 'weebcentral.pages', 'indexers', 'getcomics.issues'],
     '▲ on Tankoban rank 4 → it becomes rank 3');
  eq(wellsIn(after, 'biblio'), ['libgen', 'indexers', 'audiobookbay'],
     'and Biblio is untouched');
}
{
  const after = press(shipped(), 'biblio', 'colosseum.well.indexers', -1);
  eq(wellsIn(after, 'biblio'), ['indexers', 'libgen', 'audiobookbay'],
     '▲ on Biblio rank 2 → it becomes rank 1');
  eq(wellsIn(after, 'tankoban'), ['nyaa', 'weebcentral.pages', 'getcomics.issues', 'indexers'],
     'and Tankoban is untouched');
}

console.log('\nthe shared well ranks independently in each world, from one stored row');
{
  // Move it to the top of Tankoban, then confirm Biblio still reads its own order.
  let list = shipped();
  list = press(list, 'tankoban', 'colosseum.well.indexers', -1);
  list = press(list, 'tankoban', 'colosseum.well.indexers', -1);
  list = press(list, 'tankoban', 'colosseum.well.indexers', -1);
  eq(wellsIn(list, 'tankoban')[0], 'indexers', 'three ▲ presses put it first in Tankoban');
  // A shared well dragged the length of one world is the hardest case for a single stored
  // order — and it costs Biblio nothing, because each swap moves whichever of the two rows
  // travels more cheaply. Before the cost rule this read ["indexers","libgen","audiobookbay"].
  eq(wellsIn(list, 'biblio'), ['libgen', 'indexers', 'audiobookbay'],
     'and Biblio is STILL untouched, after dragging a shared well across all of Tankoban');
}

console.log('\nevery press either moves the row or is refused — never a silent no-op');
{
  const list = shipped();
  for (const world of ['tankoban', 'biblio']) {
    const wells = list.filter(e => mod.inWorld(e, world) && mod.isWell(e));
    for (let i = 0; i < wells.length; i++) {
      for (const d of [-1, 1]) {
        const m = mod.moveDestination(list, world, wells[i].id, d);
        const atEdge = (d === -1 && i === 0) || (d === 1 && i === wells.length - 1);
        if (atEdge) {
          m === null ? ok(`${world} ${short(wells[i].id)} ${d < 0 ? '▲' : '▼'} refused (at its edge)`)
                     : bad(`${world} ${short(wells[i].id)} at edge but offered ${JSON.stringify(m)}`);
          continue;
        }
        const before = wellsIn(list, world);
        const after = wellsIn(press(list, world, wells[i].id, d), world);
        JSON.stringify(before) !== JSON.stringify(after)
          ? ok(`${world} ${short(wells[i].id)} ${d < 0 ? '▲' : '▼'} moved`)
          : bad(`${world} ${short(wells[i].id)} ${d < 0 ? '▲' : '▼'} was a SILENT NO-OP — the P0-3 defect is back`);
      }
    }
  }
}

console.log('\ncatalogues are never ranked or reordered');
for (const id of ['colosseum.catalogue.vault', 'colosseum.catalogue.anilist', 'com.linvo.cinemeta'])
  eq(mod.moveDestination(shipped(), 'tankoban', id, -1), null, `${short(id)} refuses ▲`);
// and the C++ end refuses too, even if a caller resolved an index anyway
eq(wellsIn(applyMoveTo(shipped(), 'colosseum.catalogue.vault', 12), 'tankoban'),
   wellsIn(shipped(), 'tankoban'), 'moveTo on a core row is a no-op in the store');

console.log('\nrefusals that must not throw');
eq(mod.moveDestination(shipped(), 'tankoban', 'no.such.id', -1), null, 'unknown id');
eq(mod.moveDestination(shipped(), 'tankoban', 'colosseum.well.libgen', 1), null, 'well from another world');
eq(mod.moveDestination(shipped(), 'tankoban', 'colosseum.well.nyaa', 0), null, 'zero delta');
eq(mod.moveDestination([], 'tankoban', 'colosseum.well.nyaa', -1), null, 'empty list');
eq(mod.moveDestination(null, 'tankoban', 'colosseum.well.nyaa', -1), null, 'null list');

console.log(failed === 0 ? '\nall green' : `\n${failed} FAILED`);
process.exit(failed === 0 ? 0 : 1);
