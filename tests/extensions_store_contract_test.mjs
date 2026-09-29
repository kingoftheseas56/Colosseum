// extensions_store_contract_test.mjs — the Extensions Store's rules (qml/ExtensionsStoreApi.js and
// friends), checked offline against hand-made stremio-addons.net rows.
// Run: node tests/extensions_store_contract_test.mjs
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

let failures = 0;
function check(condition, message) {
  console.log((condition ? '  ok   ' : '  FAIL ') + message);
  if (!condition) failures++;
}

const api = {};
vm.createContext(api);
vm.runInContext(readFileSync('qml/ExtensionsStoreApi.js', 'utf8').replace('.pragma library', ''), api);

function row(name, { stars = 1, cats = [], resources = ['stream'], types = ['movie', 'series'], logo = '', hints = {} } = {}) {
  return {
    slug: name.toLowerCase().replace(/\W+/g, '-'), stars,
    manifestUrl: 'https://example.test/' + name + '/manifest.json', configureUrl: null,
    categories: cats.map(c => ({ name: c, slug: c })),
    manifest: { id: 'test.' + name, name, logo, description: name + ' does things. More text.', resources, types, behaviorHints: hints }
  };
}
const all = [
  row('Torrentio', { stars: 900, cats: ['movies', 'tv shows', 'torrents'] }),
  row('Comet | ElfHosted', { stars: 800, cats: ['movies', 'debrid support'] }),
  row('OpenSubtitles v3', { stars: 50, cats: ['subtitles'], resources: ['subtitles'] }),
  row('Streaming Catalogs', { stars: 40, cats: ['movies'], resources: ['catalog'] }),
  row('Anime Kitsu', { stars: 30, cats: ['anime', 'metadata'], resources: ['catalog', 'meta'] }),
  row('MediaFusion', { stars: 700, cats: ['live tv', 'movies', 'tv shows', 'anime'], logo: 'http://x.test/logo.png' }),
  row('USA TV', { stars: 600, cats: ['live tv', 'metadata'] }),
  row('Radio Box', { stars: 500, cats: ['radios'] }),
  row('Trakt Up Next', { stars: 400, cats: ['movies', 'tv shows'], resources: ['catalog'] }),
  row('Blue Film', { stars: 300, cats: ['nsfw', 'movies'] }),
  row('AnimeOnly', { stars: 20, cats: ['anime'] }),
  row('Setup Me', { stars: 10, cats: ['movies'], hints: { configurationRequired: true } })
].map(api.normalize).sort((x, y) => y.stars - x.stars);

const names = rows => rows.flatMap(r => r.items.map(a => a.name));
const shown = names(api.rows(all, false));

check(api.essentials(all).map(a => a.name).join('|') === api.ESSENTIALS.join('|'),
  'Essentials are the five ledger add-ons, in order');
check(!shown.some(n => api.ESSENTIALS.includes(n)), 'Essentials are not repeated in the rows below');
check(!shown.includes('USA TV') && !shown.includes('Radio Box'), 'Live TV and Radio add-ons are out');
check(shown.includes('MediaFusion'), 'a film source that also carries Live TV stays');
check(!shown.includes('Trakt Up Next'), 'tracker-sync add-ons are out');
check(!shown.includes('Blue Film') && names(api.rows(all, true)).includes('Blue Film'),
  'adult add-ons appear only when explicit content is on');
check(all.find(a => a.name === 'MediaFusion').logo.startsWith('https://'), 'http logos are upgraded to https');
check(all.find(a => a.name === 'Comet | ElfHosted').title === 'Comet', 'the ElfHosted suffix is dropped from titles');
check(all.find(a => a.name === 'Setup Me').setupRequired === true, 'configurationRequired marks an add-on as Set up');
check(all.find(a => a.name === 'Setup Me').configurable === true, 'configurationRequired also opens the setup page');
check(api.normalize(row('Opt Setup', { hints: { configurable: true } })).configurable === true,
  'an optional-setup add-on (Comet, MediaFusion) opens its setup page instead of a bare install');
const anime = api.rows(all, false).find(r => r.title === 'Anime');
check(anime && anime.items.some(a => a.name === 'AnimeOnly') && !anime.items.some(a => a.name === 'MediaFusion'),
  'the Anime row takes focused add-ons, not general sources');
check(api.rows(all, false).every(r => Array.isArray(r.all) && r.all.length >= r.items.length),
  'every row keeps its full list for See all');

const sheet = readFileSync('qml/ExtensionsSetupSheet.qml', 'utf8');
const store = readFileSync('qml/ExtensionsStorePage.qml', 'utf8');
check(sheet.includes('WebEngineView') && sheet.includes('stremio://'),
  'setup opens inside the app and catches the stremio:// install link');
check(!/openUrlExternally/.test(sheet + store), 'nothing in the Store opens the outside browser');
check(/topBarBackOnly:\s*true/.test(store), 'the Store top bar carries only Back');

if (failures) { console.error('\n' + failures + ' Store contract check(s) failed'); process.exit(1); }
console.log('\nPASS — Extensions Store contract');
