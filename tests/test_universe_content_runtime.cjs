const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { documents } = require('../scripts/export_universe_content.cjs');
function load(file, imports = {}) {
  const context = { console, ...imports };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../qml', file), 'utf8')
    .replace(/^\.(?:pragma|import).*$/gm, ''), context, { filename: file });
  return context;
}
const bridge = load('UniverseContentApi.js');
let content = documents();
bridge.setAssetResolver(value => value.replace('../assets/', 'https://content.example/assets/'));
bridge.setReader(key => content[key] || {});
const roster = load('Universes.js', { ContentApi: bridge });
assert.deepEqual(JSON.parse(JSON.stringify(roster.rosterFacets(roster.worldRoster([])))).map(f => f.label), ['Books','Manga','Anime','Films','Series']);
const canonicalCounts = { books: 7, manga: 5, anime: 5, films: 9, series: 9 };
for (const [key, count] of Object.entries(canonicalCounts)) assert.equal(roster.filterRoster(roster.worldRoster([]), key).length, count);
for (const [key, label] of Object.entries({ books: 'Novels', manga: 'Manga', anime: 'Anime', films: 'Movies', series: 'Shows', comics: 'Graphic novels' }))
  assert.equal(roster.holds({ chips: [{ t: label }] }, key), true, 'serialized RegExp fallback for ' + key);
for (const facet of content['universes-roster'].FACETS) assert.equal(typeof facet.match.source, 'string', 'RegExp exports as a pattern');
for (const facet of content['universes-roster'].FACETS) facet.match = {};
bridge.reload();
for (const [key, label] of Object.entries({ books: 'Novels', manga: 'Manga', anime: 'Anime', films: 'Movies', series: 'Shows', comics: 'Graphic novels' }))
  assert.equal(roster.holds({ chips: [{ t: label }] }, key), true, 'legacy cached RegExp fallback for ' + key);
assert.equal(roster.configFor('One Piece').anime.id, 'tt0388629');
const east = load('OnePieceEastBlueData.js', { ContentApi: bridge });
const atlas = load('OnePieceEastBlueAtlasData.js', { ContentApi: bridge });
const paradise = load('OnePieceParadiseAdapter.js', { ContentApi: bridge });
assert.ok(atlas.badgeSource('romance').startsWith('https://content.example/'));
assert.ok(paradise.artForArc({ id: 'amazon-lily' }).badgeSource.endsWith('/impel-down.png'));
content['tribute-one-piece-east-blue'].story.arcs[0].summary = 'A refreshed API story';
content['tribute-one-piece-east-blue'].artByArc.romance.posterSource = 'https://content.example/revision-2.png';
content['universes-roster'].universes.find(u => u.name === 'One Piece').blurb = 'Refreshed roster';
bridge.reload();
assert.equal(east.arc('romance').summary, 'A refreshed API story');
assert.equal(atlas.bannerSource('romance'), 'https://content.example/revision-2.png');
assert.equal(roster.configFor('One Piece').blurb, 'Refreshed roster');
assert.equal(roster.configFor('One Piece').anime.id, 'tt0388629');
content = {};
bridge.reload();
assert.equal(east.arc('romance').summary, 'A refreshed API story', 'missing remote document preserves last known data');
const extension = load('UniverseExtApi.js', { ContentApi: bridge });
extension.setReader(() => JSON.stringify({ universe: { title: 'Starter', sections: [{ title: 'Films', kind: 'video', entries: [{ title: 'Film', id: 'tt0814243', type: 'movie' }] }] } }));
let payload;
extension.load('com.colosseum.universe.onepiece', value => payload = value);
assert.equal(payload.title, 'Starter');
content['universe-one-piece'] = { universe: { title: 'API revision', sections: [{ title: 'Films', kind: 'video', entries: [{ title: 'Film', id: 'tt0814243', type: 'movie' }] }] } };
bridge.reload();
extension.load('com.colosseum.universe.onepiece', value => payload = value);
assert.equal(payload.title, 'API revision', 'process lifetime extension cache must invalidate');
console.log('PASS: in-process roster/story/art refresh, exact identity, offline retained values and extension cache invalidation');
