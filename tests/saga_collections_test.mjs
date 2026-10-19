import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const load = (path, context = {}) => {
    const env = {
        ContentApi: {
            register() {},
            document() { return {}; },
            asset(value) { return value; },
            resolveAssets(value) { return value; }
        },
        UniverseExt: { load(_id, done) { done(null); } },
        ...context
    };
    vm.createContext(env);
    vm.runInContext(fs.readFileSync(new URL(path, import.meta.url), 'utf8')
        .replace(/^\.(?:pragma|import).*$/gm, ''), env);
    return env;
};
const db = load('../qml/Universes.js');
const lookups = [];
const api = load('../qml/SagaApi.js', {
    UDB: db, Biblio: { lookupBook: (title, done) => lookups.push({ title, done }) }
});
api.requestJson = (_url, done) => done(null);
let current;
api.loadSaga('Harry Potter', value => { current = value; });
assert.equal(current.books.length, 10);
assert.equal(current.films.length, 11);
assert.equal(current.shows.length, 1);
assert.equal(current.books[0].resolved, false);
lookups[1].done({ title: 'Second volume', id: 'book-2' });
assert.equal(current.books[0].resolved, false, 'Later lookups must not take the first-book slot');
assert.equal(current.books[1].id, 'book-2');

const planned = api.sagaSlots([{ t: 'Future adaptation', upcoming: true }], [], 'film');
const future = api.sagaCollections({ books: [], films: planned, shows: [] }, {});
assert.equal(future.upcoming.length, 1, 'Upcoming titles survive missing provider listings');
assert.equal(future.upcoming[0].resolved, false);
assert.equal(api.sagaSlots([{ t: 'Scheduled', releaseDate: '2999-12-25' }], [], 'series')[0].upcoming, true);
assert.equal(api.sagaSlots([{ t: 'Released', releaseDate: '2000-01-01' }], [], 'series')[0].upcoming, false);

for (const cfg of db.universes.concat(db.archive).filter(value => value.category === 'saga')) {
    const payload = {
        books: api.sagaSlots(cfg.novels || [], [], 'book'),
        films: api.sagaSlots(cfg.films || [], [], 'film'),
        shows: api.sagaSlots(cfg.shows || [], [], 'series')
    };
    const groups = api.sagaCollections(payload, cfg);
    const grouped = groups.books.concat(groups.adaptations,
        ...groups.branches.map(branch => branch.books.concat(branch.adaptations)));
    const original = payload.books.concat(payload.films, payload.shows);
    assert.equal(grouped.length, original.length, cfg.name + ' must retain every work');
    assert.equal(new Set(grouped.map(work => work.medium + ':' + work.canonIndex)).size,
        original.length, cfg.name + ' must not duplicate works across branches');
    for (const branch of cfg.sagaBranches || []) {
        for (const kind of ['books', 'films', 'shows']) {
            for (const index of branch[kind] || []) assert.ok(index < payload[kind].length);
        }
    }
}
const hp = api.sagaCollections(current, db.configFor('Harry Potter'));
assert.equal(hp.books.length, 7);
assert.equal(hp.adaptations.length, 8);
assert.equal(hp.branches.find(branch => branch.title === 'Fantastic Beasts').adaptations.length, 3);
// Naruto's cross-medium catalogue grows by pinned English novels without changing the
// manga/anime/film identities or affecting One Piece's explicit five-shelf inventory.
const naruto = db.configFor('Naruto');
const narutoSpecs = api.shelfSpecsFor(naruto);
assert.deepEqual(Array.from(narutoSpecs, shelf => shelf.title), ['Manga', 'Anime', 'Films', 'Novels']);
assert.deepEqual(Array.from(narutoSpecs, shelf => shelf.runs.reduce((n, run) => n + run.works.length, 0)),
    [4, 3, 11, 11]);
const novelIds = [1051140307, 1074095480, 1100454596, 1164244390, 1206630594,
    1524572355, 1533115240, 1553987642, 1590967444, 1610393822, 6443278687];
assert.deepEqual(Array.from(narutoSpecs[3].runs[0].works, novel => novel.apple), novelIds,
    'Books must keep the verified Apple Books trackIds in English-publication order');
assert.equal(new Set(novelIds).size, novelIds.length, 'No duplicate book editions');
assert.ok(narutoSpecs[3].runs[0].works.every(book => book.cover && book.author && book.year),
    'Every pinned novel needs artwork and bibliographic context');
let narutoRendered = null;
api.loadShelves('Naruto', value => { narutoRendered = value; });
assert.deepEqual(Array.from(narutoRendered.shelves, shelf => shelf.title),
    ['Manga', 'Anime', 'Films', 'Novels']);
const bookTile = narutoRendered.shelves[3].runs[0].items[0];
assert.equal(bookTile.medium, 'book');
assert.equal(bookTile.apple, 1051140307);
let bookLookupUrl = '', fallbackBook = null;
const priorRequestJson = api.requestJson;
api.requestJson = (url, done) => { bookLookupUrl = url; done(null); };
api.resolveBook(bookTile, book => { fallbackBook = book; });
api.requestJson = priorRequestJson;
assert.equal(bookLookupUrl, 'https://itunes.apple.com/lookup?id=1051140307');
assert.equal(fallbackBook, bookTile, 'Store outage must keep the pinned novel routable');
const onePieceSpecs = api.shelfSpecsFor(db.configFor('One Piece'));
assert.deepEqual(Array.from(onePieceSpecs, shelf => shelf.title),
    ['Manga', 'Series', 'Films', 'Specials', 'Novels']);
assert.deepEqual(Array.from(onePieceSpecs, shelf => shelf.runs.reduce((n, run) => n + run.works.length, 0)),
    [15, 5, 15, 17, 4], 'Naruto must not alter One Piece or its totals');
assert.deepEqual(Array.from(api.shelfSpecsFor(db.configFor('Dragon Ball')), s => s.title),
    ['Manga', 'Anime', 'Films'], 'Manga-first worlds without novels keep their existing layout');
console.log('Saga collections: provider gaps, upcoming titles, stable order, all saga branches, and Naruto novel pins passed.');
