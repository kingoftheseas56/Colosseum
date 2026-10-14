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
assert.equal(output.catalogueNotice, '', 'all Tribute media are now in the standard template');
assert.deepEqual(Array.from(output.shelves, shelf => shelf.title), ['Films', 'Series', 'Books', 'Comics', 'Manga']);
assert.deepEqual(Array.from(output.shelves,
    shelf => shelf.runs.reduce((n, r) => n + r.items.length, 0)), [20, 29, 72, 53, 1]);
const works = title => output.shelves.find(s => s.title === title).runs.flatMap(r => r.items);
const movies = works('Films'), series = works('Series'),
    books = works('Books'), comics = works('Comics'), manga = works('Manga');
assert.equal(new Set(movies.map(w => w.id)).size, 20);
assert.equal(new Set(series.map(w => w.id)).size, 29);
assert.equal(new Set(books.map(w => w.apple)).size, 72);
assert.equal(new Set(comics.map(w => w.seriesId)).size, 53);
assert.equal(new Set(comics.map(w => w.posts[0])).size, 53);
assert.equal(manga[0].al, 148967);
assert.equal(manga[0].mal, 154395);
assert.equal(manga[0].medium, 'manga');
assert.equal(manga[0].query, 'Star Wars: Visions: The Manga Anthology');
const sourceComics = source.sections.filter(s => s.kind === 'comic').flatMap(s => s.entries);
const firstPostPins = [
    214639, 276678, 264449, 340727, 319606, 318128, 338383, 328774, 310048,
    343451, 374787, 383037, 65203, 39568, 159051, 7004, 277237, 156988,
    179898, 294073, 131538, 112833, 143338, 26442, 118465, 157677, 158373,
    158347, 158330, 39352, 161969, 150155, 152957, 156380, 158743, 264204,
    283345, 312076, 335553, 158343, 370531, 179892, 66476, 156957, 143931,
    292041, 367547, 380917, 396815, 378938, 397350, 378409, 395170
];
assert.deepEqual(sourceComics.map(e => e.posts[0]), firstPostPins, '53 originally curated GetComics collections');
assert.equal(sourceComics.filter(e => e.gcdId > 0).length, 50);
assert.equal(sourceComics.filter(e => (e.gcdIds || []).length > 0).length, 2);
assert.equal(sourceComics.filter(e => !e.gcdId && !(e.gcdIds || []).length).length, 1);
for (const original of sourceComics) {
    const tile = comics.find(e => e.posts[0] === original.posts[0]);
    assert.ok(tile, 'comic collection missing: ' + original.title);
    assert.equal(tile.title, original.title);
    assert.deepEqual(Array.from(tile.posts), original.posts, 'GetComics post list must survive intact');
    assert.equal(tile.gcdId, original.gcdId || 0);
    assert.deepEqual(Array.from(tile.gcdIds), original.gcdIds || []);
    assert.equal(tile.seriesId, original.gcdId ? 'gcd:' + original.gcdId
        : 'gc:posts:' + original.posts[0], 'comic progress needs a distinct stable identity');
}
const correctDarthVader = comics.find(w => w.title === 'Darth Vader by Charles Soule');
assert.equal(correctDarthVader.gcdId, 114713, '2017 Vader cannot inherit the Star Wars 2020 GCD ID');
assert.equal(comics.find(w => w.title === 'Star Wars (2020)').gcdId, 154267);
assert.equal(comics.find(w => w.title === 'Darth Vader (2020)').gcdId, 155697);
assert.equal(comics.find(w => w.title === 'The High Republic · Phase I').gcdId, 0);
assert.deepEqual(Array.from(comics.find(w => w.title === 'The High Republic · Phase I').gcdIds),
    [168398, 190281], 'cross-GCD bundles preserve provenance without false single identity');
const originalManga = source.sections.find(s => s.kind === 'manga').entries[0];
assert.deepEqual([originalManga.provider, originalManga.id, originalManga.malId],
    ['anilist', '148967', '154395']);
assert.equal(manga[0].cover, originalManga.cover);
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
// Execute the production Main.qml entry routers with a fake layer. The distinction
// between a GCD run and multi-GCD GetComics bundle is a progress identity contract.
const main = fs.readFileSync('qml/Main.qml', 'utf8');
const comicStart = main.indexOf('    function openUniverseComic(d) {');
const comicEnd = main.indexOf('    // ---- comic series: a LOCG', comicStart);
assert.ok(comicStart > 0 && comicEnd > comicStart);
const route = { westernLayer: { active: false, item: null }, console: { warn() {} } };
vm.createContext(route);
vm.runInContext(main.slice(comicStart, comicEnd), route);
route.openUniverseComic({ title: 'High Republic Phase I', posts: [214639, 267513, 282584], year: '2021–2022' });
assert.equal(route.westernLayer.baked.packSeriesId, 'gc:posts:214639');
assert.equal(route.westernLayer.baked.gcdId, 0);
assert.equal(route.westernLayer.baked.releases[1].url, 'https://getcomics.org/?p=267513');
assert.equal(route.westernLayer.baked.releases[1].year, 2021);
route.westernLayer.active = true;
route.westernLayer.item = { packSeriesId: 'gc:posts:214639', gcdId: 0, bakedReleases: [] };
route.openUniverseComic({ title: 'Darth Vader by Charles Soule', gcdId: 114713, posts: [277237], year: '2017–2018' });
assert.equal(route.westernLayer.baked.gcdId, 114713);
assert.equal(route.westernLayer.item.packSeriesId, '', 'a prior multi-run identity must be cleared');
assert.equal(route.westernLayer.item.gcdId, 114713);
assert.equal(route.westernLayer.item.bakedReleases.length, 1);
route.openUniverseComic({ title: 'The High Republic: Edge of Balance', posts: [338383], year: '2021–2025' });
assert.equal(route.westernLayer.item.packSeriesId, 'gc:posts:338383');
assert.equal(route.westernLayer.item.gcdId, 0);

const mangaStart = main.indexOf('    function routeUniverseSeries(e) {');
const mangaEnd = main.indexOf('    function openOnePaceArc', mangaStart);
assert.ok(mangaStart > 0 && mangaEnd > mangaStart);
const mangaRoute = { win: { openSeries(...args) { this.lastOpen = args; } } };
vm.createContext(mangaRoute);
vm.runInContext(main.slice(mangaStart, mangaEnd), mangaRoute);
mangaRoute.routeUniverseSeries({ provider: 'anilist', id: '148967', malId: '154395',
    title: 'Star Wars: Visions: The Manga Anthology' });
assert.equal(mangaRoute.win.lastOpen[0], 'Star Wars: Visions: The Manga Anthology');
assert.equal(mangaRoute.win.lastOpen[1], '154395');
console.log('Star Wars standard: 20 films + 29 series + 72 books + 53 comics + 1 manga = 175 unique titles; pinned routes, fallback and neighboring universes passed.');
