import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const clean = text => text.replace(/^\.(pragma|import).*$/gm, '');
const source = fs.readFileSync(process.argv[2] || new URL('../qml/WorldSearch.js', import.meta.url), 'utf8');
const delivery = vm.createContext({});
vm.runInContext(clean(fs.readFileSync(new URL('../qml/CatalogDelivery.js', import.meta.url), 'utf8')), delivery);
const api = vm.createContext({ CatalogDelivery: delivery, XMLHttpRequest() { assert.fail('Unexpected network'); } });
vm.runInContext(clean(source), api);
// The first rows mirror the tablet's real Dune prefix query. Exact normalized
// titles precede prefixes even when their release years are much older.
const rows = [
    { tt: 'tt0142032', type: 'mini', title: 'Dune', year: 2000, rating: 6.9 },
    { tt: 'tt0087182', type: 'movie', title: 'Dune', year: 1984, rating: 6.3 },
    { tt: 'tt15239678', type: 'movie', title: 'Dune: Part Two', year: 2024, rating: 8.5 },
    { tt: 'tt1160419', type: 'movie', title: 'Dune: Part One', year: 2021, rating: 8.0 },
    { tt: 'tt6910678', type: 'movie', title: 'The Dunes', year: 2021, rating: 6.3 },
    { tt: 'tt11835714', type: 'movie', title: 'Dune Drifter', year: 2020, rating: 3.6 },
];
function search(query, entries = rows) {
    let result;
    const owner = { available: () => true, search: () => entries,
        searchState: () => ({ source: 'local', status: 'success' }) };
    api.searchFor('Theatre', query, (items, error) => { assert.equal(error, ''); result = items; }, null, { imdbCatalog: owner });
    assert.ok(result);
    return result;
}
let hits = search('Dune (2020)');
assert.ok(hits.every(row => row.data.releaseInfo === '2020'),
    'A release-year query cannot promote the 2000 series or another year');
assert.equal(hits[0].title, 'Dune Drifter', 'The available prefix match keeps its actual title');
assert.equal(hits[0].data.id, rows[5].tt, 'No inferred or substituted IMDb identity');
assert.equal(search('Dune (2020)', rows.slice(0, 5)).length, 0,
    'An absent release year is an empty result, not a different year labelled Top Match');
hits = search('Dune 2021');
assert.equal(hits[0].data.id, 'tt1160419');
assert.equal(hits[0].title, 'Dune: Part One');
assert.equal(hits[0].data.releaseInfo, '2021');
assert.equal(search('Dune (2000)')[0].data.id, 'tt0142032');
assert.equal(search('Dune (1984)')[0].data.id, 'tt0087182');
hits = search('The Dunes (2021)');
assert.equal(hits[0].title, 'The Dunes');
assert.equal(hits[0].data.id, 'tt6910678', 'The Dunes keeps its actual IMDb identity');
assert.equal(hits[0].data.imdbRating, 6.3, 'The Dunes fixture keeps the actual catalogue rating');
assert.equal(search('Blade Runner 2049', [{ tt: 'tt1856101', type: 'movie', title: 'Blade Runner 2049', year: 2017 }])[0].data.id, 'tt1856101');
assert.equal(search('Class of 1999', [{ tt: 'tt0099277', type: 'movie', title: 'Class of 1999', year: 1990 }])[0].data.id, 'tt0099277');
assert.equal(search('1917', [{ tt: 'tt8579674', type: 'movie', title: '1917', year: 2019 }])[0].data.id, 'tt8579674');
assert.equal(search('Matrix', [{ tt: 'tt0133093', type: 'movie', title: 'The Matrix', year: 1999 }])[0].data.id, 'tt0133093');
// The legacy Cinemeta path must use the same year rule and keep its failure truth.
const pending = [];
api.reqJson = (url, callback) => { pending.push({ url, callback }); return () => {}; };
let legacy;
api.searchTheatre('Dune (2020)', (items, error) => { legacy = { items, error }; });
pending[0].callback({ metas: [
    { id: 'tt1160419', name: 'Dune', type: 'movie', releaseInfo: '2021' },
    { id: 'tt12451788', name: 'Dune', type: 'movie', releaseInfo: '2020' },
] }, '');
pending[1].callback(null, api.PROVIDER_UNAVAILABLE);
assert.equal(legacy.items.length, 1);
assert.equal(legacy.items[0].data.id, 'tt12451788');
assert.equal(legacy.error, api.PROVIDER_UNAVAILABLE);
console.log('THEATRE_SEARCH_YEAR PASS native/legacy year, identity, numeric-title, partial-failure cases');
