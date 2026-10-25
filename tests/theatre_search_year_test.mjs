import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const clean = text => text.replace(/^\.(pragma|import).*$/gm, '');
const sourcePath = process.argv[2] || new URL('../qml/WorldSearch.js', import.meta.url);
const source = fs.readFileSync(sourcePath, 'utf8');
const delivery = vm.createContext({});
vm.runInContext(clean(fs.readFileSync(new URL('../qml/CatalogDelivery.js', import.meta.url), 'utf8')), delivery);
const api = vm.createContext({ CatalogDelivery: delivery, XMLHttpRequest() { assert.fail('Unexpected network'); } });
vm.runInContext(clean(source), api);

// Fixture rows mirror the mixed-year Dune prefix results, including the actual
// catalogue identities and scores that distinguish exact titles from prefixes.
const rows = [
    { tt: 'tt0142032', type: 'mini', title: 'Dune', year: 2000, rating: 6.9 },
    { tt: 'tt0087182', type: 'movie', title: 'Dune', year: 1984, rating: 6.2 },
    { tt: 'tt15239678', type: 'movie', title: 'Dune: Part Two', year: 2024, rating: 8.4 },
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
function ids(items) { return Array.from(items, row => row.data.id); }
function years(items) { return Array.from(items, row => Number.parseInt(row.data.releaseInfo, 10)); }
function sorted(values) { return [...values].sort(); }
function assertPreserved(items, expectedIds, label) {
    const actual = ids(items);
    assert.equal(actual.length, expectedIds.length, `${label}: result count`);
    assert.equal(new Set(actual).size, actual.length, `${label}: no duplicate identities`);
    assert.deepEqual(sorted(actual), sorted(expectedIds), `${label}: preserve every input identity`);
}
function assertYearGroups(items, targetYear, label) {
    const groups = years(items).map(year => year === targetYear ? 0 : Math.abs(year - targetYear) <= 1 ? 1 : 2);
    assert.deepEqual(groups, [...groups].sort((a, b) => a - b),
        `${label}: exact year first, adjacent years second, all other rows last`);
    return groups;
}

// Native catalogue results retain every identity while grouping exact and
// adjacent years before the rest; existing title scoring still leads each group.
let hits = search('Dune (2020)');
assertPreserved(hits, rows.map(row => row.tt), 'native exact-year query');
assert.deepEqual(ids(hits), [
    'tt11835714',       // exact year: Dune Drifter
    'tt1160419',        // adjacent year: highest-rated Dune prefix
    'tt6910678',        // adjacent year: lower-rated Dune prefix
    'tt0142032',        // remaining years: exact title, highest rating
    'tt0087182',        // remaining years: exact title
    'tt15239678',       // remaining years: title prefix
]);
assertYearGroups(hits, 2020, 'native exact-year query');

// No exact-year hit is not an empty result. The ±1 group remains first, and
// the out-of-range rows are still available at the end of the list.
hits = search('Dune (2020)', rows.slice(0, 5));
assertPreserved(hits, rows.slice(0, 5).map(row => row.tt), 'native no-exact-year query');
assert.ok(years(hits).every(year => year !== 2020), 'fixture has no exact-year row');
assertYearGroups(hits, 2020, 'native no-exact-year query');
assert.equal(hits[0].data.id, 'tt1160419', 'adjacent-year title scoring selects the best available prefix');

// A different no-exact-year query makes the ±1 preference explicit.
hits = search('Dune 2022');
assertPreserved(hits, rows.map(row => row.tt), 'native adjacent-year query');
assertYearGroups(hits, 2022, 'native adjacent-year query');
assert.deepEqual(ids(hits).slice(0, 2), ['tt1160419', 'tt6910678'],
    'the 2021 ±1 group ranks by existing title score and rating');

// With neither an exact nor an adjacent year, all rows still remain available
// and the existing title scorer ranks the best title match first. Missing-year
// metadata is also retained in the final group.
const unknownYearRow = { tt: 'tt9000000', type: 'movie', title: 'Dune: Unknown Year', year: null, rating: 0 };
const noNearbyYearRows = [...rows, unknownYearRow];
hits = search('Dune (2010)', noNearbyYearRows);
assertPreserved(hits, noNearbyYearRows.map(row => row.tt), 'native no-exact-or-adjacent-year query');
assertYearGroups(hits, 2010, 'native no-exact-or-adjacent-year query');
assert.equal(hits[0].data.id, 'tt0142032', 'best exact-title match ranks first when no year is near');
assert.ok(ids(hits).includes(unknownYearRow.tt), 'row with unknown year remains in the results');

// Four-digit numbers that are part of full titles stay literal searches.
for (const [title, id, year] of [
    ['1917', 'tt8579674', 2019],
    ['Blade Runner 2049', 'tt1856101', 2017],
    ['Wonder Woman 1984', 'tt7126948', 2020],
    ['Class of 1999', 'tt0099277', 1990],
]) {
    const result = search(title, [{ tt: id, type: 'movie', title, year }]);
    assert.equal(result.length, 1, `${title}: literal numeric title remains searchable`);
    assert.equal(result[0].data.id, id);
}

// Legacy Cinemeta results use the same grouping and preserve movie + series
// identities across all year groups.
const legacyMovies = [
    { id: 'tt11835714', name: 'Dune Drifter', releaseInfo: '2020', imdbRating: '3.6' },
    { id: 'tt0087182', name: 'Dune', releaseInfo: '1984', imdbRating: '6.2' },
    { id: 'tt1160419', name: 'Dune: Part One', releaseInfo: '2021', imdbRating: '8.0' },
    { id: 'tt15239678', name: 'Dune: Part Two', releaseInfo: '2024', imdbRating: '8.4' },
    { id: 'tt6910678', name: 'The Dunes', releaseInfo: '2021', imdbRating: '6.3' },
];
const legacySeries = [
    { id: 'tt0142032', name: 'Dune', releaseInfo: '2000', imdbRating: '6.9' },
];
function legacySearch(query, movies, series) {
    const pending = [];
    const updates = [];
    api.reqJson = (url, callback) => { pending.push({ url, callback }); return () => {}; };
    api.searchTheatre(query, (items, error) => updates.push({ items, error }));
    assert.equal(pending.length, 2, 'movie and series catalogues are requested');
    pending[0].callback({ metas: movies }, '');
    pending[1].callback({ metas: series }, '');
    assert.equal(updates.length, 1, 'combined result is delivered once before hero enrichment');
    return updates[0];
}
let legacy = legacySearch('Dune 2020', legacyMovies, legacySeries);
assert.equal(legacy.error, '');
assertPreserved(legacy.items, [...legacyMovies, ...legacySeries].map(row => row.id), 'legacy exact-year query');
assertYearGroups(legacy.items, 2020, 'legacy exact-year query');
assert.deepEqual(ids(legacy.items).slice(0, 3), ['tt11835714', 'tt1160419', 'tt6910678'],
    'legacy exact year precedes the score-ranked adjacent-year group');

legacy = legacySearch('Dune 2020', legacyMovies.filter(row => row.releaseInfo !== '2020'), legacySeries);
assertPreserved(legacy.items, [...legacyMovies.filter(row => row.releaseInfo !== '2020'), ...legacySeries].map(row => row.id),
    'legacy no-exact-year query');
assertYearGroups(legacy.items, 2020, 'legacy no-exact-year query');
assert.equal(legacy.items[0].data.id, 'tt1160419', 'legacy adjacent-year match remains visible');

// One failed legacy lane still returns all healthy rows and reports the error.
const failedLaneCalls = [];
const failedLaneUpdates = [];
api.reqJson = (url, callback) => { failedLaneCalls.push({ url, callback }); return () => {}; };
api.searchTheatre('Dune (2020)', (items, error) => failedLaneUpdates.push({ items, error }));
failedLaneCalls[0].callback({ metas: legacyMovies }, '');
failedLaneCalls[1].callback(null, api.PROVIDER_UNAVAILABLE);
assert.equal(failedLaneUpdates.length, 1);
assert.equal(failedLaneUpdates[0].error, api.PROVIDER_UNAVAILABLE);
assertPreserved(failedLaneUpdates[0].items, legacyMovies.map(row => row.id), 'legacy partial-provider result');
assertYearGroups(failedLaneUpdates[0].items, 2020, 'legacy partial-provider result');

console.log('THEATRE_SEARCH_YEAR PASS grouped native/legacy results, preserved identities, no-nearby and unknown years, numeric titles, partial failure');
