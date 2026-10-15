import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const load = (path, context = {}) => {
    vm.createContext(context);
    vm.runInContext(fs.readFileSync(new URL(path, import.meta.url), 'utf8')
        .replace(/^\.(?:pragma|import).*$/gm, ''), context);
    return context;
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
console.log('Saga collections: provider gaps, upcoming titles, stable order, and all saga branches passed.');
