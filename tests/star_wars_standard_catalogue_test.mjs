import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

function load(file, context = {}) {
    vm.createContext(context);
    vm.runInContext(fs.readFileSync(file, 'utf8').replace(/^\.(?:pragma|import).*$/gm, ''), context);
    return context;
}
const raw = fs.readFileSync('assets/universes/star-wars.json', 'utf8');
const source = JSON.parse(raw).universe;
const canon = load('qml/Universes.js');
const extension = load('qml/UniverseExtApi.js');
let reads = 0;
extension.setReader(file => {
    assert.equal(file, 'star-wars');
    reads++;
    return raw;
});
const api = load('qml/SagaApi.js', { UDB: canon, UniverseExt: extension, Biblio: {} });
let output;
let pushes = 0;
api.loadShelves('Star Wars', value => { output = value; pushes++; });
assert.equal(reads, 1);
assert.equal(pushes, 1);
assert.match(output.catalogueNotice, /Comics and manga remain in the Tribute/);
assert.deepEqual(Array.from(output.shelves, shelf => shelf.title), ['Films', 'Series', 'Books']);
assert.deepEqual(Array.from(output.shelves,
    shelf => shelf.runs.reduce((n, r) => n + r.items.length, 0)), [20, 29, 72]);
const works = title => output.shelves.find(s => s.title === title).runs.flatMap(r => r.items);
const movies = works('Films'), series = works('Series'), books = works('Books');
assert.equal(new Set(movies.map(w => w.id)).size, 20);
assert.equal(new Set(series.map(w => w.id)).size, 29);
assert.equal(new Set(books.map(w => w.apple)).size, 72);
const sourceVideos = source.sections.filter(s => s.kind === 'video').flatMap(s => s.entries);
const uniqueVideo = Array.from(new Map(sourceVideos.map(e => [e.id, e])).values());
assert.equal(uniqueVideo.length, 49);
for (const e of uniqueVideo) {
    const list = e.type === 'movie' ? movies : series;
    assert.equal(list.filter(w => w.id === e.id).length, 1, 'Lost ' + e.title);
}
for (const e of source.sections.filter(s => s.kind === 'book').flatMap(s => s.entries)) {
    const tile = books.find(w => String(w.apple) === String(e.id));
    assert.ok(tile, 'Missing ' + e.title);
    assert.equal(tile.title, e.title);
    assert.equal(tile.author, e.author);
    assert.equal(tile.medium, 'book');
}
assert.equal(series.find(w => w.id === 'tt0361243').title, 'Star Wars: Clone Wars');
assert.ok(movies.some(w => w.id === 'tt1185834'));
let bookUrl = '', fallback = null;
api.requestJson = (url, done) => { bookUrl = url; done(null); };
api.resolveBook(books[0], book => { fallback = book; });
assert.equal(bookUrl, 'https://itunes.apple.com/lookup?id=' + books[0].apple);
assert.equal(fallback, books[0]);
const missingReader = load('qml/UniverseExtApi.js');
missingReader.setReader(() => '');
const warnings = [];
const degraded = load('qml/SagaApi.js', { UDB: canon, UniverseExt: missingReader,
    Biblio: {}, console: { warn: message => warnings.push(message) } });
let partial;
degraded.loadShelves('Star Wars', value => { partial = value; });
assert.match(partial.catalogueNotice, /Partial catalogue/);
assert.equal(partial.shelves.reduce((n, s) =>
    n + s.runs.reduce((v, r) => v + r.items.length, 0), 0), 28);
assert.equal(warnings.length, 1);
let onePiece = null, naruto = null;
api.loadShelves('One Piece', value => { onePiece = value; });
api.loadShelves('Naruto', value => { naruto = value; });
const size = u => u.shelves.reduce((n,s) =>
    n+s.runs.reduce((m,r) => m+r.items.length, 0), 0);
assert.equal(size(onePiece), 56);
assert.equal(size(naruto), 29);
console.log('Star Wars standard: 20 films + 29 series + 72 books, no duplication; degraded fallback and neighbors passed.');
