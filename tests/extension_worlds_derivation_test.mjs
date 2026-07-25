// extension_worlds_derivation_test.mjs — the world of an extension is DERIVED from its
// manifest `types`, never stored (spec §3.2), and a universe is classified by ROLE before
// content (universes design §5.1a). This test pins both rules plus the seeded roster's
// per-world arithmetic, which is acceptance criterion 1: Tankoban shows 2 catalogue rows
// and 4 well rows, Biblio shows 1 and 3.
//
// Loads the real qml/ExtensionsCatalog.js so a change to the tables is caught here.
import fs from 'fs';

let src = fs.readFileSync('qml/ExtensionsCatalog.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
// The file's XMLHttpRequest calls are never reached by the pure functions under test.
const mod = {};
new Function('module', 'XMLHttpRequest', src +
  '\nmodule.worldsFor=worldsFor;module.inWorld=inWorld;module.isCatalogue=isCatalogue;' +
  'module.isWell=isWell;module.jobFor=jobFor;module.WORLD_TYPES=WORLD_TYPES;'
)(mod, function () {});

let failures = 0;
const ok  = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failures++; };
const eq  = (a, b, m) => JSON.stringify(a) === JSON.stringify(b)
  ? ok(`${m} → ${JSON.stringify(a)}`)
  : bad(`${m}: expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}`);

// The seeded roster, mirroring native/engine/ExtensionsStore.cpp appendHouseDefaults().
const E = (id, core, resources, types) =>
  ({ id, core, manifest: { id, resources, types } });
const CAT = ['catalog', 'meta'], STR = ['stream'];

const roster = [
  E('com.linvo.cinemeta',              true,  CAT, ['movie', 'series']),
  E('com.stremio.torrentio.addon',     false, STR, ['movie', 'series', 'anime']),
  E('community.anime.kitsu',           false, CAT, ['series', 'movie', 'anime']),
  E('org.stremio.opensubtitlesv3',     false, ['subtitles'], ['movie', 'series']),
  E('colosseum.catalogue.weebcentral', true,  CAT, ['manga']),
  E('colosseum.catalogue.getcomics',   true,  CAT, ['comic']),
  E('colosseum.catalogue.applebooks',  true,  CAT, ['book']),
  E('colosseum.well.nyaa',             false, STR, ['manga']),
  E('colosseum.well.weebcentral.pages',false, STR, ['manga']),
  E('colosseum.well.getcomics.issues', false, STR, ['comic']),
  E('colosseum.well.libgen',           false, STR, ['book']),
  E('colosseum.well.indexers',         false, STR, ['comic', 'book', 'audiobook']),
  E('colosseum.well.audiobookbay',     false, STR, ['audiobook'])
];

const inW = w => roster.filter(e => mod.inWorld(e, w));
const cats = l => l.filter(mod.isCatalogue);
const wells = l => l.filter(mod.isWell);

console.log('acceptance 1 — per-world catalogue/well counts');
eq([cats(inW('tankoban')).length, wells(inW('tankoban')).length], [2, 4], 'Tankoban [catalogues, wells]');
eq([cats(inW('biblio')).length,   wells(inW('biblio')).length],   [1, 3], 'Biblio [catalogues, wells]');
eq(cats(inW('theatre')).length, 1, 'Theatre catalogues (Cinemeta only)');

console.log('one install, two worlds — a stored `world` field could not express this');
const idx = roster.find(e => e.id === 'colosseum.well.indexers');
eq(mod.worldsFor(idx), ['tankoban', 'biblio'], 'Torrent Indexers worlds');

console.log('per-world rank is the filtered index, so one row ranks differently in each');
eq(wells(inW('tankoban')).findIndex(e => e.id === 'colosseum.well.indexers') + 1, 4, 'Indexers rank in Tankoban');
eq(wells(inW('biblio')).findIndex(e => e.id === 'colosseum.well.indexers') + 1, 2, 'Indexers rank in Biblio');

console.log('roles: a site holding both appears twice, once per role');
const wcCat  = roster.find(e => e.id === 'colosseum.catalogue.weebcentral');
const wcWell = roster.find(e => e.id === 'colosseum.well.weebcentral.pages');
eq([mod.isCatalogue(wcCat), mod.isWell(wcCat)],   [true, false],  'WeebCentral catalogue row');
eq([mod.isCatalogue(wcWell), mod.isWell(wcWell)], [false, true],  'WeebCentral well row');

console.log('universes: role beats content — one tab, never four');
const uni = { id: 'com.colosseum.universe.onepiece', core: false,
              manifest: { id: 'com.colosseum.universe.onepiece', resources: ['universe'],
                          types: ['movie', 'series', 'manga', 'book'] } };
eq(mod.worldsFor(uni), ['universes'], 'a cross-medium universe');
eq([mod.inWorld(uni, 'theatre'), mod.inWorld(uni, 'tankoban'), mod.inWorld(uni, 'biblio')],
   [false, false, false], 'universe absent from the three media worlds');

console.log('job lines come from the id table, not from types');
eq(mod.jobFor('colosseum.well.nyaa'),              'volume torrents', 'Nyaa job');
eq(mod.jobFor('colosseum.well.weebcentral.pages'), 'chapter pages',   'WeebCentral pages job');
eq(mod.jobFor('com.stremio.torrentio.addon'),      '',                'a remote addon has no house job');
// Two wells share the type `manga`, which is exactly why the line cannot be derived.
eq([mod.jobFor('colosseum.well.nyaa') !== mod.jobFor('colosseum.well.weebcentral.pages'),
    JSON.stringify(roster.find(e => e.id === 'colosseum.well.nyaa').manifest.types) ===
    JSON.stringify(roster.find(e => e.id === 'colosseum.well.weebcentral.pages').manifest.types)],
   [true, true], 'same type, different job');

console.log('every well in the roster has a job line');
const jobless = wells(roster).filter(e => !mod.jobFor(e.id) && e.id.startsWith('colosseum.'));
jobless.length ? bad('house wells missing a job: ' + jobless.map(e => e.id).join(', '))
               : ok('all house wells have a job line');

console.log(failures ? `\n${failures} FAILED` : '\nall green');
process.exit(failures ? 1 : 0);
