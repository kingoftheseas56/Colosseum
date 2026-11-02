// PF-06: TheatreApi.loadMeta uses the caller's exact catalogue movie record only after
// Cinemeta AND installed extensions miss; series, non-tt ids, mismatched ids/types and
// nameless records still fail closed, and live metadata always wins. Runs production
// TheatreApi.js in a VM with a recording XMLHttpRequest and AddonClient double.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { test } from 'node:test';

const source = readFileSync(new URL('../qml/TheatreApi.js', import.meta.url), 'utf8')
    .replace(/^\.(pragma|import).*$/gm, '');

function api({ cinemeta = null, extension = null } = {}) {
    const requests = [];
    class XHR {
        static DONE = 4;
        open(method, url) { this.url = url; requests.push(url); }
        setRequestHeader() {}
        abort() {}
        send() {
            this.readyState = 4;
            this.status = cinemeta ? 200 : 404;
            this.responseText = cinemeta ? JSON.stringify({ meta: cinemeta }) : '';
            this.onreadystatechange && this.onreadystatechange();
        }
    }
    const ctx = vm.createContext({
        XMLHttpRequest: XHR, console, Date, Math, JSON,
        Qt: { callLater: f => f() },
        CatalogDelivery: {}, Rules: {}, Policy: {},
        AddonClient: { loadMetaFromExtensions: (list, type, id, done) => done(extension) },
    });
    vm.runInContext(source, ctx);
    return { ctx, requests };
}
function load(ctx, type, id, record) {
    let out = 'unset';
    ctx.loadMeta(type, id, meta => { out = meta; }, record);
    return out;
}
const record = { id: 'tt0063350', type: 'movie', name: 'Night of the Living Dead',
    releaseInfo: '1968', imdbRating: '7.8', poster: 'https://img/p.jpg', extra: 'ignored' };

test('exact movie record is projected only after every provider misses', () => {
    const { ctx } = api();
    const meta = load(ctx, 'movie', 'tt0063350', record);
    assert.deepEqual(JSON.parse(JSON.stringify(meta)), { id: 'tt0063350', type: 'movie',
        name: 'Night of the Living Dead', releaseInfo: '1968', imdbRating: '7.8', poster: 'https://img/p.jpg' });
    assert.equal(meta.videos, undefined, 'no fabricated episode list');
});

test('live metadata (Cinemeta or extension) always wins', () => {
    const live = { id: 'tt0063350', name: 'Live', type: 'movie' };
    assert.equal(load(api({ cinemeta: live }).ctx, 'movie', 'tt0063350', record).name, 'Live');
    assert.equal(load(api({ extension: live }).ctx, 'movie', 'tt0063350', record).name, 'Live');
});

test('fails closed for series, non-tt ids, mismatched or nameless records', () => {
    const { ctx } = api();
    assert.equal(load(ctx, 'series', 'tt0063350', { ...record, type: 'series' }), null);
    assert.equal(load(ctx, 'movie', 'tt0063350', { ...record, id: 'tt9999999' }), null);
    assert.equal(load(ctx, 'movie', 'tt0063350', { ...record, type: 'series' }), null);
    assert.equal(load(ctx, 'movie', 'tt0063350', { id: 'tt0063350', type: 'movie', name: '  ' }), null);
    assert.equal(load(ctx, 'movie', 'tt0063350', undefined), null, 'callers without a record are unchanged');
    assert.equal(load(ctx, 'movie', 'tmdb:42', { ...record, id: 'tmdb:42' }), null);
});

test('a title-shaped catalogue record (Beta itemData) is accepted', () => {
    const { ctx } = api();
    const meta = load(ctx, 'movie', 'tt0063350', { id: 'tt0063350', type: 'movie', title: 'Night', cover: 'c.jpg' });
    assert.equal(meta.name, 'Night');
    assert.equal(meta.poster, 'c.jpg');
});
