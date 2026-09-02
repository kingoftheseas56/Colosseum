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
  'module.isWell=isWell;module.isUniverse=isUniverse;module.jobFor=jobFor;' +
  'module.WORLD_TYPES=WORLD_TYPES;'
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
  // Our catalogues are the private data vault and AniList — NOT WeebCentral/GetComics,
  // which only ever say what is available to download (Hemanth's ruling 2026-07-26).
  E('colosseum.catalogue.vault',       true,  CAT, ['manga', 'comic']),
  E('colosseum.catalogue.anilist',     true,  CAT, ['manga']),
  E('colosseum.catalogue.applebooks',  true,  CAT, ['book']),
  E('colosseum.well.nyaa',             false, STR, ['manga']),
  E('colosseum.well.tankoyomi',false, STR, ['manga']),
  E('colosseum.well.getcomics.issues', false, STR, ['comic']),
  E('colosseum.well.libgen',           false, STR, ['book']),
  E('colosseum.well.indexers',         false, STR, ['comic', 'book', 'audiobook']),
  E('colosseum.well.audiobookbay',     false, STR, ['audiobook']),
  // NoTorrent: direct-HTTP streams. A plain `stream` well; types movie+series put it in
  // Theatre only, exactly like Torrentio — one tab, no scatter. (Gen 9; VidKing retired
  // gen 10 — House HTTP slice 4.)
  E('com.notorrent.addon',             false, STR, ['movie', 'series'])
];

const inW = w => roster.filter(e => mod.inWorld(e, w));
const cats = l => l.filter(mod.isCatalogue);
const wells = l => l.filter(mod.isWell);

console.log('acceptance 1 — per-world catalogue/well counts');
eq([cats(inW('tankoban')).length, wells(inW('tankoban')).length], [2, 4], 'Tankoban [catalogues, wells]');
eq([cats(inW('biblio')).length,   wells(inW('biblio')).length],   [1, 3], 'Biblio [catalogues, wells]');
eq(cats(inW('theatre')).length, 1, 'Theatre catalogues (Cinemeta only)');

console.log('NoTorrent: a direct-HTTP stream extension is a Theatre well, not a catalogue');
const notorrent = roster.find(e => e.id === 'com.notorrent.addon');
eq(mod.worldsFor(notorrent), ['theatre'], 'NoTorrent belongs to Theatre only');
eq(mod.isWell(notorrent), true, 'NoTorrent is a well — it fetches playable streams');
eq(mod.isCatalogue(notorrent), false, 'NoTorrent fills no shelf, so it is not a catalogue');
eq(mod.isUniverse(notorrent), false, 'NoTorrent is not a universe');
// The Theatre reorder places it after Torrentio and never ranks core Cinemeta.
eq(wells(inW('theatre')).map(e => e.id), ['com.stremio.torrentio.addon', 'com.notorrent.addon'],
   'Theatre wells: Torrentio then NoTorrent, Cinemeta (core catalogue) never among them');

console.log('one install, two worlds — a stored `world` field could not express this');
const idx = roster.find(e => e.id === 'colosseum.well.indexers');
eq(mod.worldsFor(idx), ['tankoban', 'biblio'], 'Torrent Indexers worlds');

console.log('per-world rank is the filtered index, so one row ranks differently in each');
eq(wells(inW('tankoban')).findIndex(e => e.id === 'colosseum.well.indexers') + 1, 4, 'Indexers rank in Tankoban');
eq(wells(inW('biblio')).findIndex(e => e.id === 'colosseum.well.indexers') + 1, 2, 'Indexers rank in Biblio');

console.log('roles: catalogue and well stay independent, so a site COULD hold both');
// The house roster no longer exercises this — since 2026-07-26 every house row holds
// exactly one role — but the predicates must stay independent, so a synthetic pair
// guards the machinery that would let a future site appear twice.
const bothCat  = E('colosseum.catalogue.somesite', true,  CAT, ['manga']);
const bothWell = E('colosseum.well.somesite',      false, STR, ['manga']);
eq([mod.isCatalogue(bothCat),  mod.isWell(bothCat)],  [true, false], 'a catalogue row is only a catalogue');
eq([mod.isCatalogue(bothWell), mod.isWell(bothWell)], [false, true], 'a well row is only a well');
eq(roster.filter(e => mod.isCatalogue(e) && mod.isWell(e)).length, 0, 'no house row holds two roles');

console.log('universes: role beats content — one tab, never four');
const uni = { id: 'com.colosseum.universe.onepiece', core: false,
              manifest: { id: 'com.colosseum.universe.onepiece', resources: ['universe'],
                          types: ['movie', 'series', 'manga', 'book'] } };
eq(mod.worldsFor(uni), ['universes'], 'a cross-medium universe');
eq([mod.inWorld(uni, 'theatre'), mod.inWorld(uni, 'tankoban'), mod.inWorld(uni, 'biblio')],
   [false, false, false], 'universe absent from the three media worlds');

console.log('job lines come from the id table, not from types');
eq(mod.jobFor('colosseum.well.nyaa'),              'volume torrents', 'Nyaa job');
eq(mod.jobFor('colosseum.well.tankoyomi'), 'chapter pages',   'Tankoyomi chapter sources job');
eq(mod.jobFor('com.stremio.torrentio.addon'),      '',                'a remote addon has no house job');
// Two wells share the type `manga`, which is exactly why the line cannot be derived.
eq([mod.jobFor('colosseum.well.nyaa') !== mod.jobFor('colosseum.well.tankoyomi'),
    JSON.stringify(roster.find(e => e.id === 'colosseum.well.nyaa').manifest.types) ===
    JSON.stringify(roster.find(e => e.id === 'colosseum.well.tankoyomi').manifest.types)],
   [true, true], 'same type, different job');

console.log('every well in the roster has a job line');
const jobless = wells(roster).filter(e => !mod.jobFor(e.id) && e.id.startsWith('colosseum.'));
jobless.length ? bad('house wells missing a job: ' + jobless.map(e => e.id).join(', '))
               : ok('all house wells have a job line');

// ── universes: the two the house auto-installs (Hemanth 2026-07-26) ──────────────
// Seeded in ExtensionsStore.cpp with resources ["universe"]. They exist to prove the
// role-first rule under real data, not as a hypothetical: One Piece spans manga, anime
// and film, so a content-first derivation would scatter it across three media worlds
// plus its own — four rows sharing one enabled flag.
console.log('universes: the auto-installed pair lands in exactly one world');
const UNI = ['universe'];
const onepiece = E('com.colosseum.universe.onepiece', false, UNI, UNI);
const dcau     = E('com.colosseum.universe.dcau',     false, UNI, UNI);

for (const [u, label] of [[onepiece, 'One Piece'], [dcau, 'DCAU']]) {
  eq(mod.worldsFor(u), ['universes'], `${label} worlds`);
  eq([mod.inWorld(u, 'theatre'), mod.inWorld(u, 'tankoban'), mod.inWorld(u, 'biblio')],
     [false, false, false], `${label} is in no media world`);
  eq(mod.isUniverse(u), true, `${label} is a universe`);
  // Neither role applies: it fills no shelf and fetches no file, so it can never acquire
  // a rank or be asked for a source. That is what makes an ask-order framing wrong for it.
  eq([mod.isCatalogue(u), mod.isWell(u)], [false, false],
     `${label} is neither catalogue nor well`);
}

console.log('a universe never enters an ask ladder');
const withUniverses = roster.concat([onepiece, dcau]);
eq(wells(withUniverses).filter(mod.isUniverse).length, 0,
   'no universe appears among the wells of any world');
eq(withUniverses.filter(e => mod.isUniverse(e) && mod.worldsFor(e).length !== 1).length, 0,
   'every universe belongs to exactly one world');

console.log(failures ? `\n${failures} FAILED` : '\nall green');
process.exit(failures ? 1 : 0);
